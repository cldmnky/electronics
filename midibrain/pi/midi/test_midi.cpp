// Host self-test for the mnkybox midi core (router, USB clock discipline,
// button map, RtMidi wrapper plumbing). No framework: a failing check prints
// FAIL and the process exits non-zero, so `test_midi && echo PASS` is the
// acceptance gate. Runs headless; no MIDI device is required.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <vector>

#include "buttons.hpp"
#include "clock.hpp"
#include "router.hpp"
#include "transport_follow.hpp"
#include "usb.hpp"

namespace {

int g_failures = 0;

void check(bool ok, const char* what, const char* file, int line) {
  if (ok) return;
  std::fprintf(stderr, "FAIL %s (%s:%d)\n", what, file, line);
  ++g_failures;
}

#define CHECK(cond) check((cond), #cond, __FILE__, __LINE__)

void pass(const char* name) { std::printf("PASS %s\n", name); }

using mnky::midi::Action;
using mnky::midi::DeliverFn;
using mnky::midi::MidiEvent;
using mnky::midi::PipeConfig;
using mnky::midi::PipeType;
using mnky::midi::Port;
using mnky::midi::Router;
using mnky::midi::UsbClock;
using mnky::midi::UsbMidi;
using mnky::midi::UsbTransportFollow;

struct Delivery {
  Port dst;
  MidiEvent ev;
};

MidiEvent message(Port source, std::uint8_t ch, std::uint8_t status, std::uint8_t d1,
                  std::uint8_t d2, std::uint32_t stampUs = 1000) {
  MidiEvent ev;
  ev.source = source;
  ev.size = 3;
  ev.data[0] = static_cast<std::uint8_t>(status | (ch & 0x0F));
  ev.data[1] = d1;
  ev.data[2] = d2;
  ev.stampUs = stampUs;
  return ev;
}

MidiEvent oneByte(Port source, std::uint8_t status, std::uint32_t stampUs = 1000) {
  MidiEvent ev;
  ev.source = source;
  ev.size = 1;
  ev.data[0] = status;
  ev.stampUs = stampUs;
  return ev;
}

// ---------------------------------------------------------------------------
// router: route matrix, merge fan-out, channel mask, transpose
// ---------------------------------------------------------------------------
void routingMatrix() {
  std::vector<Delivery> log;
  const DeliverFn capture = [&log](Port dst, const MidiEvent& ev) { log.push_back({dst, ev}); };
  Router r;

  // Everything is off until the app enables it (no accidental USB echo).
  r.route(message(Port::TrsIn1, 0, 0x90, 60, 100), capture);
  CHECK(log.empty());
  CHECK(!r.routeEnabled(Port::TrsIn1, Port::TrsOut1));

  CHECK(r.setRoute(Port::TrsIn1, Port::TrsOut1, true));
  CHECK(r.routeEnabled(Port::TrsIn1, Port::TrsOut1));
  CHECK(!r.routeEnabled(Port::TrsIn2, Port::TrsOut1));
  r.route(message(Port::TrsIn1, 0, 0x90, 60, 100), capture);
  CHECK(log.size() == 1);
  CHECK(log[0].dst == Port::TrsOut1);
  CHECK(log[0].ev.data[1] == 60);

  // Sources are independent: an unrouted source delivers nothing.
  log.clear();
  r.route(message(Port::TrsIn2, 0, 0x90, 61, 100), capture);
  CHECK(log.empty());

  // Only input -> output pairs exist.
  CHECK(!r.setRoute(Port::TrsOut1, Port::TrsOut2, true));
  CHECK(!r.setRoute(Port::TrsIn1, Port::TrsIn2, true));
  CHECK(!r.setRoute(Port::UsbIn, Port::UsbIn, true));
  CHECK(!r.routeEnabled(Port::TrsOut1, Port::TrsOut2));

  // Disabling stops delivery; other routes stay as they were.
  CHECK(r.setRoute(Port::TrsIn1, Port::TrsOut1, false));
  log.clear();
  r.route(message(Port::TrsIn1, 0, 0x90, 60, 100), capture);
  CHECK(log.empty());

  // Merge: both TRS inputs into one output, in a single pass each.
  CHECK(r.setRoute(Port::TrsIn1, Port::TrsOut1, true));
  CHECK(r.setRoute(Port::TrsIn2, Port::TrsOut1, true));
  log.clear();
  r.route(message(Port::TrsIn1, 0, 0x90, 60, 100), capture);
  r.route(message(Port::TrsIn2, 0, 0x90, 64, 100), capture);
  CHECK(log.size() == 2);
  CHECK(log[0].dst == Port::TrsOut1 && log[0].ev.data[1] == 60);
  CHECK(log[1].dst == Port::TrsOut1 && log[1].ev.data[1] == 64);

  // Invalid events are not routed at all.
  log.clear();
  MidiEvent empty;
  empty.source = Port::TrsIn1;
  r.route(empty, capture);
  MidiEvent fromOutput = message(Port::TrsOut1, 0, 0x90, 60, 100);
  r.route(fromOutput, capture);
  CHECK(log.empty());
  pass("router matrix");
  pass("router merge");
}

void fanOut() {
  std::vector<Delivery> log;
  const DeliverFn capture = [&log](Port dst, const MidiEvent& ev) { log.push_back({dst, ev}); };
  Router r;
  CHECK(r.setRoute(Port::UsbIn, Port::TrsOut1, true));
  CHECK(r.setRoute(Port::UsbIn, Port::TrsOut2, true));
  CHECK(r.setRoute(Port::UsbIn, Port::UsbOut, true));

  MidiEvent ev = message(Port::UsbIn, 3, 0xB0, 7, 64, 424242);
  ev.device = 2;
  r.route(ev, capture);
  CHECK(log.size() == 3);
  CHECK(log[0].dst == Port::TrsOut1);
  CHECK(log[1].dst == Port::TrsOut2);
  CHECK(log[2].dst == Port::UsbOut);
  for (const auto& d : log) {
    CHECK(d.ev.source == Port::UsbIn);
    CHECK(d.ev.device == 2);
    CHECK(d.ev.stampUs == 424242);
    CHECK(d.ev.size == 3);
    CHECK(d.ev.data[0] == (0xB0 | 3));
    CHECK(d.ev.data[1] == 7);
    CHECK(d.ev.data[2] == 64);
  }
  pass("router fan-out");
}

void channelFilter() {
  std::vector<Delivery> log;
  const DeliverFn capture = [&log](Port dst, const MidiEvent& ev) { log.push_back({dst, ev}); };
  Router r;
  CHECK(r.setRoute(Port::TrsIn1, Port::TrsOut1, true));
  CHECK(r.channelMask(Port::TrsIn1) == Router::kAllChannels);

  // Bit n = channel n+1: channel 1 only.
  r.setChannelMask(Port::TrsIn1, 0x0001);
  r.route(message(Port::TrsIn1, 0, 0x90, 60, 100), capture);
  CHECK(log.size() == 1);
  r.route(message(Port::TrsIn1, 1, 0x90, 60, 100), capture);
  CHECK(log.size() == 1);
  r.route(message(Port::TrsIn1, 1, 0xB0, 7, 100), capture);  // CC is channel voice too
  CHECK(log.size() == 1);

  // Realtime and system common are unfiltered.
  r.route(oneByte(Port::TrsIn1, mnky::midi::kClockByte), capture);
  CHECK(log.size() == 2);
  MidiEvent songPos = oneByte(Port::TrsIn1, 0xF2);
  songPos.size = 3;
  songPos.data[1] = 0;
  songPos.data[2] = 8;
  r.route(songPos, capture);
  CHECK(log.size() == 3);

  // Higher channels select by their own bit; 0 masks the whole source.
  r.setChannelMask(Port::TrsIn1, 0x0010);
  r.route(message(Port::TrsIn1, 4, 0xC0, 5, 0), capture);
  CHECK(log.size() == 4);
  r.route(message(Port::TrsIn1, 3, 0xC0, 5, 0), capture);
  CHECK(log.size() == 4);
  r.setChannelMask(Port::TrsIn1, 0x0000);
  r.route(message(Port::TrsIn1, 0, 0x90, 60, 100), capture);
  CHECK(log.size() == 4);
  r.route(oneByte(Port::TrsIn1, mnky::midi::kClockByte), capture);
  CHECK(log.size() == 5);

  // Masks are per source.
  r.setChannelMask(Port::TrsIn2, 0x0002);
  CHECK(r.channelMask(Port::TrsIn2) == 0x0002);
  CHECK(r.channelMask(Port::TrsIn1) == 0x0000);
  pass("channel mask");
}

void transpose() {
  std::vector<Delivery> log;
  const DeliverFn capture = [&log](Port dst, const MidiEvent& ev) { log.push_back({dst, ev}); };
  Router r;
  CHECK(r.setRoute(Port::TrsIn1, Port::TrsOut1, true));
  CHECK(r.transpose(Port::TrsIn1) == 0);

  r.setTranspose(Port::TrsIn1, 7);
  r.route(message(Port::TrsIn1, 0, 0x90, 60, 100), capture);
  CHECK(log.size() == 1);
  CHECK(log[0].ev.data[1] == 67);
  CHECK(log[0].ev.data[2] == 100);  // velocity untouched
  r.route(message(Port::TrsIn1, 0, 0x80, 60, 64), capture);
  CHECK(log.size() == 2);
  CHECK(log[1].ev.data[1] == 67);

  // Only note on/off are transposed.
  log.clear();
  r.route(message(Port::TrsIn1, 0, 0xB0, 60, 100), capture);
  r.route(message(Port::TrsIn1, 0, 0xA0, 60, 100), capture);
  r.route(message(Port::TrsIn1, 0, 0xE0, 60, 100), capture);
  CHECK(log.size() == 3);
  for (const auto& d : log) CHECK(d.ev.data[1] == 60);
  r.route(oneByte(Port::TrsIn1, 0xF8), capture);
  CHECK(log.back().ev.data[0] == 0xF8);

  // Out-of-range notes are dropped, never clamped onto another pitch.
  log.clear();
  r.route(message(Port::TrsIn1, 0, 0x90, 124, 100), capture);
  r.route(message(Port::TrsIn1, 0, 0x90, 120, 100), capture);
  CHECK(log.size() == 1);
  CHECK(log[0].ev.data[1] == 127);

  // Negative shifts drop low notes and pass the rest.
  log.clear();
  r.setTranspose(Port::TrsIn1, -80);
  r.route(message(Port::TrsIn1, 0, 0x90, 60, 100), capture);
  CHECK(log.empty());
  r.route(message(Port::TrsIn1, 0, 0x90, 100, 100), capture);
  CHECK(log.size() == 1);
  CHECK(log[0].ev.data[1] == 20);

  // Transpose is per source, and absurd values are clamped.
  CHECK(r.transpose(Port::TrsIn2) == 0);
  r.setTranspose(Port::TrsIn2, 500);
  CHECK(r.transpose(Port::TrsIn2) == 127);
  CHECK(r.transpose(Port::TrsIn1) == -80);
  log.clear();
  r.route(message(Port::TrsIn1, 0, 0x90, 60, 100), capture);
  CHECK(log.empty());  // TrsIn1 keeps its -80 and still drops note 60
  pass("transpose");
}

// ---------------------------------------------------------------------------
// MIDI FX pipes: Pi-owned USB routes evaluate once, TRS->TRS stays firmware's
// ---------------------------------------------------------------------------
PipeConfig fxPipe(PipeType type, std::initializer_list<std::int32_t> params,
                  bool bypass = false) {
  PipeConfig config;
  config.type = type;
  config.bypass = bypass;
  for (std::int32_t value : params) {
    if (config.paramCount >= config.params.size()) break;
    config.params[config.paramCount++] = value;
  }
  return config;
}

void routingPipes() {
  // Side ownership is one rule and covers all nine routes.
  CHECK(!mnky::midi::piOwnsPipeChain(Port::TrsIn1, Port::TrsOut1));
  CHECK(!mnky::midi::piOwnsPipeChain(Port::TrsIn1, Port::TrsOut2));
  CHECK(!mnky::midi::piOwnsPipeChain(Port::TrsIn2, Port::TrsOut1));
  CHECK(!mnky::midi::piOwnsPipeChain(Port::TrsIn2, Port::TrsOut2));
  CHECK(mnky::midi::piOwnsPipeChain(Port::UsbIn, Port::TrsOut1));
  CHECK(mnky::midi::piOwnsPipeChain(Port::UsbIn, Port::TrsOut2));
  CHECK(mnky::midi::piOwnsPipeChain(Port::UsbIn, Port::UsbOut));
  CHECK(mnky::midi::piOwnsPipeChain(Port::TrsIn1, Port::UsbOut));
  CHECK(mnky::midi::piOwnsPipeChain(Port::TrsIn2, Port::UsbOut));

  std::vector<Delivery> log;
  const DeliverFn capture = [&log](Port dst, const MidiEvent& ev) { log.push_back({dst, ev}); };

  // A chain on the USB destination route runs; the same source's TRS route
  // stays a plain copy. A note-on-only filter is the cheapest proof.
  Router r;
  const PipeConfig noteOnly = fxPipe(PipeType::Filter, {1, 0, 0, 0, 0, 0, 0, 0});
  CHECK(r.setPipes(Port::TrsIn1, Port::UsbOut, &noteOnly, 1));
  CHECK(r.pipeCount(Port::TrsIn1, Port::UsbOut) == 1);
  CHECK(r.pipeCount(Port::TrsIn1, Port::TrsOut1) == 0);
  CHECK(!r.setPipes(Port::TrsOut1, Port::TrsIn1, &noteOnly, 1));
  CHECK(r.setRoute(Port::TrsIn1, Port::TrsOut1, true));
  CHECK(r.setRoute(Port::TrsIn1, Port::UsbOut, true));

  r.route(message(Port::TrsIn1, 0, 0x90, 60, 100), capture);
  CHECK(log.size() == 2);
  CHECK(log[0].dst == Port::TrsOut1 && log[0].ev.data[0] == 0x90);
  CHECK(log[1].dst == Port::UsbOut && log[1].ev.data[0] == 0x90);
  log.clear();
  r.route(message(Port::TrsIn1, 0, 0xB0, 7, 100), capture);
  CHECK(log.size() == 1);              // only the unpiped TRS route
  CHECK(log[0].dst == Port::TrsOut1);
  CHECK(r.pipeDiagnostics().evaluations == 2);

  // No double processing: a piped TRS->TRS route is delivered unprocessed and
  // the Pi never evaluates its chain (the Pico does, via EV_MIDI_ROUTE_SEND).
  Router firmwareOwned;
  const PipeConfig dropAll = fxPipe(PipeType::Chance, {100});
  CHECK(firmwareOwned.setPipes(Port::TrsIn1, Port::TrsOut1, &dropAll, 1));
  CHECK(firmwareOwned.pipeCount(Port::TrsIn1, Port::TrsOut1) == 1);
  CHECK(firmwareOwned.setRoute(Port::TrsIn1, Port::TrsOut1, true));
  log.clear();
  firmwareOwned.route(message(Port::TrsIn1, 0, 0x90, 60, 100), capture);
  CHECK(log.size() == 1);  // 100 % chance would have dropped it if evaluated
  CHECK(log[0].dst == Port::TrsOut1 && log[0].ev.data[1] == 60);
  CHECK(firmwareOwned.pipeDiagnostics().evaluations == 0);
  firmwareOwned.tick(1000, capture);  // tick is Pi-owned routes only
  CHECK(log.size() == 1);

  // Mask -> pipes -> transpose: a dispatcher rotates channel 1 onto channel 2,
  // but the mask already removed channel 1, so nothing is delivered.
  Router order;
  const PipeConfig dispatcher = fxPipe(PipeType::Dispatcher, {2, 2});
  CHECK(order.setPipes(Port::TrsIn1, Port::UsbOut, &dispatcher, 1));
  order.setChannelMask(Port::TrsIn1, 0x0002);  // channel 2 only
  CHECK(order.setRoute(Port::TrsIn1, Port::UsbOut, true));
  log.clear();
  order.route(message(Port::TrsIn1, 0, 0x90, 60, 100), capture);
  CHECK(log.empty());  // a mask applied after the pipes would have passed ch2
  order.route(message(Port::TrsIn1, 1, 0x90, 60, 100), capture);
  CHECK(log.size() == 1);
  CHECK((log[0].ev.data[0] & 0x0F) == 1);  // already channel 2, slot 0

  // Pipes then transpose: C# quantizes down to C, then +1 semitone -> C#.
  // Transposing first would give D -> D, so the order is observable.
  Router quantized;
  const PipeConfig cMajor = fxPipe(PipeType::ScaleQuantize, {1, 0});
  CHECK(quantized.setPipes(Port::TrsIn1, Port::UsbOut, &cMajor, 1));
  quantized.setTranspose(Port::TrsIn1, 1);
  CHECK(quantized.setRoute(Port::TrsIn1, Port::UsbOut, true));
  CHECK(quantized.setRoute(Port::TrsIn1, Port::TrsOut2, true));
  log.clear();
  quantized.route(message(Port::TrsIn1, 0, 0x90, 61, 100), capture);
  CHECK(log.size() == 2);
  // Destination order is fixed: TRS OUT2 before the USB fan-out.
  CHECK(log[0].dst == Port::TrsOut2 && log[0].ev.data[1] == 62);
  CHECK(log[1].dst == Port::UsbOut && log[1].ev.data[1] == 61);

  // Wave-2 chains configure (Wire/session tolerance) but refuse evaluation;
  // the app log seam reports the pipe index and a static reason.
  Router wave2;
  const PipeConfig arp = fxPipe(PipeType::Arp, {});
  CHECK(wave2.setPipes(Port::UsbIn, Port::TrsOut1, &arp, 1));
  CHECK(wave2.setRoute(Port::UsbIn, Port::TrsOut1, true));
  log.clear();
  wave2.route(message(Port::UsbIn, 0, 0x90, 60, 100), capture);
  CHECK(log.empty());
  CHECK(wave2.pipeDiagnostics().rejected == 1);
  CHECK(wave2.pipeDiagnostics().lastRejectReason != nullptr);
  CHECK(wave2.pipeDiagnostics().lastRejectPipe == 0);

  // NoteLength on a Pi-owned route: the note-off is due on the time line and
  // Router::tick emits it, not the next incoming message.
  Router length;
  const PipeConfig fixed = fxPipe(PipeType::NoteLength, {100});
  CHECK(length.setPipes(Port::UsbIn, Port::TrsOut2, &fixed, 1));
  CHECK(length.setRoute(Port::UsbIn, Port::TrsOut2, true));
  length.setPipeSeed(Port::UsbIn, Port::TrsOut2, 777);
  CHECK(length.pipeSeed(Port::UsbIn, Port::TrsOut2) == 777);
  log.clear();
  length.route(message(Port::UsbIn, 0, 0x90, 60, 100, 1000000), capture);
  CHECK(log.size() == 1 && log[0].ev.data[0] == 0x90);
  log.clear();
  length.tick(1099, capture);
  CHECK(log.empty());
  length.tick(1100, capture);
  CHECK(log.size() == 1);
  CHECK(log[0].dst == Port::TrsOut2 && log[0].ev.data[0] == 0x80);
  CHECK(log[0].ev.data[1] == 60);

  // Invalid chains leave the previous config in place.
  const PipeConfig chance = fxPipe(PipeType::Chance, {0});
  CHECK(length.setPipes(Port::UsbIn, Port::TrsOut2, &chance, 1));
  CHECK(!length.setPipes(Port::UsbIn, Port::TrsOut2, nullptr, 9));
  CHECK(length.pipeCount(Port::UsbIn, Port::TrsOut2) == 1);
  pass("routing pipes");
}

// ---------------------------------------------------------------------------
// clock: offset discipline under jitter, phase scheduling, transport gating
// ---------------------------------------------------------------------------
void clockTempoAndTransport() {
  UsbClock c;
  CHECK(c.tempo() == UsbClock::kDefaultBpmX100);
  CHECK(std::fabs(c.intervalUs() - 20833.333) < 1.0);
  CHECK(!c.nextDueUs(0).has_value());
  CHECK(!c.popDue(1000000));
  CHECK(!c.primed());

  c.setTempo(30000);
  CHECK(c.tempo() == 30000);
  CHECK(std::fabs(c.intervalUs() - 8333.333) < 1.0);  // 120 ticks/s at 300 bpm
  c.setTempo(24000);
  CHECK(std::fabs(c.intervalUs() - 10416.667) < 1.0);
  c.setTempo(1);
  CHECK(c.tempo() == UsbClock::kMinBpmX100);
  CHECK(std::fabs(c.intervalUs() - 125000.0) < 1e-6);
  c.setTempo(60000);
  CHECK(c.tempo() == UsbClock::kMaxBpmX100);
  c.setTempo(12000);

  // Rolling without a tick still has no phase to emit.
  c.setRolling(true);
  CHECK(c.rolling());
  CHECK(!c.nextDueUs(1000).has_value());
  CHECK(!c.popDue(1000));

  c.addTick(7, 1000, 5000);  // offset = 4000 -> mapped = 5000
  CHECK(c.primed());
  CHECK(c.lastTick() == 7);
  const auto due = c.nextDueUs(5000);
  CHECK(due.has_value());
  CHECK(*due == 5000);

  c.setRolling(false);
  CHECK(!c.nextDueUs(5000).has_value());
  CHECK(!c.popDue(6000000));
  c.setRolling(true);
  // Re-enter roll one tick interval after the last tick, without waiting.
  const auto resumed = c.nextDueUs(5000);
  CHECK(resumed.has_value());
  CHECK(*resumed == 5000 + 20833);
  pass("clock tempo/transport");
}

void clockOffsetUnderJitter() {
  UsbClock c;
  c.setTempo(12000);
  c.setRolling(true);

  constexpr std::uint64_t kPiEpoch = 100'000'000;
  constexpr std::uint32_t kPicoEpoch = 7'000'000;
  constexpr std::uint32_t kTickStep = 20833;  // Pico tick spacing at 120.00 bpm
  const double trueOffset = static_cast<double>(kPiEpoch) - static_cast<double>(kPicoEpoch);

  // Deterministic arrival jitter 0..6000 us (delivery latency, not tick time).
  std::uint64_t rng = 0x2545F4914F6CDD1DULL;
  const auto jitter = [&rng]() {
    rng = rng * 6364136223846793005ULL + 1442695040888963407ULL;
    return static_cast<std::int64_t>((rng >> 33) % 6001);
  };

  double arrivalSum = 0.0;
  double worstOffsetError = 0.0;
  double worstStampError = 0.0;
  const int kTicks = 400;
  for (int i = 0; i < kTicks; ++i) {
    const std::uint32_t picoStamp = kPicoEpoch + static_cast<std::uint32_t>(i) * kTickStep;
    const std::uint64_t truePiUs = kPiEpoch + static_cast<std::uint64_t>(i) * kTickStep;
    const std::uint64_t piNowUs = truePiUs + static_cast<std::uint64_t>(jitter());
    arrivalSum += static_cast<double>(piNowUs) - static_cast<double>(picoStamp);
    c.addTick(static_cast<std::uint32_t>(i), picoStamp, piNowUs);
    if (i > 20) {  // after the EMA has seen a spread of arrivals
      const double offsetError = c.offsetUs() - trueOffset;
      if (std::fabs(offsetError) > worstOffsetError) worstOffsetError = std::fabs(offsetError);
      const double stampError = static_cast<double>(c.mapStamp(picoStamp)) - static_cast<double>(truePiUs);
      if (std::fabs(stampError) > worstStampError) worstStampError = std::fabs(stampError);
    }
  }
  const double arrivalOffset = arrivalSum / kTicks;  // "arrival is the tick" mapping
  CHECK(arrivalOffset - trueOffset > 2000.0);        // the naive mapping is ~3 ms late
  CHECK(worstOffsetError < 1500.0);                  // ... the disciplined one is not
  CHECK(worstStampError < 1500.0);
  CHECK(c.latencyUs() > 0.0);
  CHECK(c.latencyUs() < 6000.0 + 1500.0);
  CHECK(c.reordered() == 0);

  // Duplicate / stale tick numbers never move the phase backwards.
  const double offsetBefore = c.offsetUs();
  const auto phaseBefore = c.nextDueUs(0);
  c.addTick(10, kPicoEpoch + 10 * kTickStep, 999'999'999ULL);
  c.addTick(399, kPicoEpoch + 399 * kTickStep, 999'999'999ULL);
  CHECK(c.reordered() == 2);
  CHECK(c.offsetUs() == offsetBefore);
  CHECK(c.nextDueUs(0) == phaseBefore);
  CHECK(c.lastTick() == 399);
  pass("clock offset EMA under jitter");
}

void clockPhaseScheduling() {
  UsbClock p;
  p.setTempo(12000);
  p.setRolling(true);

  constexpr std::uint64_t kMapOffset = 4'000'000;  // Pi time = Pico stamp + this
  constexpr std::int64_t kLatency = 2'000;
  constexpr std::uint32_t kTickStep = 20833;
  const double interval = p.intervalUs();

  std::uint64_t nowUs = 4'000'000;
  const std::uint64_t stopUs = 4'502'000 + 1'000'000;  // 1 s after the first byte
  std::uint32_t picoStamp = 500'000;
  std::uint32_t tick = 0;
  std::uint64_t nextArrivalUs = static_cast<std::uint64_t>(picoStamp) + kMapOffset +
                                static_cast<std::uint64_t>(kLatency);
  std::vector<std::uint64_t> emitted;
  while (nowUs < stopUs) {
    if (nowUs >= nextArrivalUs) {
      p.addTick(tick, picoStamp, nowUs);
      ++tick;
      picoStamp += kTickStep;
      nextArrivalUs = static_cast<std::uint64_t>(picoStamp) + kMapOffset +
                      static_cast<std::uint64_t>(kLatency);
    }
    while (p.popDue(nowUs)) emitted.push_back(nowUs);
    nowUs += 100;  // 100 us scheduler granularity
  }

  // 1 s at 120 bpm = 2 quarters = 48 clock bytes.
  CHECK(emitted.size() >= 47 && emitted.size() <= 49);
  CHECK(!emitted.empty());
  const double first = static_cast<double>(emitted.front());
  CHECK(std::fabs(first - (500'000.0 + kMapOffset + kLatency)) <= 200.0);
  for (std::size_t i = 1; i < emitted.size(); ++i) {
    const double gap = static_cast<double>(emitted[i] - emitted[i - 1]);
    CHECK(std::fabs(gap - interval) <= 200.0);
  }
  CHECK(p.skipped() == 0);
  CHECK(p.reordered() == 0);

  // A caller blocked for several ticks emits one byte and skips the rest.
  UsbClock s;
  s.setTempo(12000);
  s.setRolling(true);
  s.addTick(0, 0, 10'000);  // offset 10000, phase 10000
  CHECK(s.popDue(10'000));
  const std::uint64_t resume = 10'000 + 2 * 20834 + 3 * 20834;
  CHECK(s.popDue(resume));
  CHECK(s.skipped() >= 3);
  CHECK(!s.popDue(resume + 1));  // phase re-pegged to resume + interval
  pass("clock phase scheduling");
}

// ---------------------------------------------------------------------------
// buttons: EV_CONTROL ids -> Action
// ---------------------------------------------------------------------------
void buttonMap() {
  struct Case {
    std::uint8_t id;
    std::uint8_t value;
    bool shift;
    Action expected;
  };
  const Case cases[] = {
      {mb::CTL_PLAY, 1, false, Action::Play},
      {mb::CTL_PLAY, 0, false, Action::None},
      {mb::CTL_PLAY_START, 1, false, Action::PlayStart},
      {mb::CTL_STOP, 1, false, Action::Stop},
      {mb::CTL_STOP, 0, false, Action::None},
      {mb::CTL_REC, 1, false, Action::RecToggle},
      {mb::CTL_REC, 0, false, Action::None},
      {mb::CTL_REC, 1, true, Action::RecToggle},  // SHIFT never changes a toggle
      {mb::CTL_OVER_DUB, 1, false, Action::OverdubToggle},
      {mb::CTL_NAV_UP, 1, false, Action::NavUp},
      {mb::CTL_NAV_DOWN, 1, false, Action::NavDown},
      {mb::CTL_NAV_LEFT, 1, false, Action::NavLeft},
      {mb::CTL_NAV_RIGHT, 1, false, Action::NavRight},
      {mb::CTL_NAV_PRESS, 1, false, Action::NavPress},
      {mb::CTL_NAV_PRESS, 0, false, Action::None},
      {mb::CTL_ENCODER_SW, 1, false, Action::EncoderSw},
      {mb::CTL_ENCODER_SW, 0, false, Action::None},
      {mb::CTL_ENCODER_TURN, 1, false, Action::TempoCoarse},
      {mb::CTL_ENCODER_TURN, 0xFF, false, Action::TempoCoarse},  // -1 detent
      {mb::CTL_ENCODER_TURN, 1, true, Action::TempoFine},
      {mb::CTL_ENCODER_TURN, 0, false, Action::None},  // no movement, no action
      {mb::CTL_SHIFT_EXIT, 1, true, Action::None},     // modifier only
      {mb::CTL_SHIFT_EXIT, 0, false, Action::None},
      {0x7F, 1, false, Action::None},  // unknown id
  };
  for (const auto& c : cases) {
    check(mnky::midi::mapControl(c.id, c.value, c.shift) == c.expected, "mapControl case",
          __FILE__, __LINE__);
  }
  pass("button map");
}

// ---------------------------------------------------------------------------
// USB wrapper: no device required, message clamping is pure logic
// ---------------------------------------------------------------------------
void usbWrapper() {
  UsbMidi usb;
  CHECK(usb.inputCount() == 0);
  CHECK(usb.outputCount() == 0);
  CHECK(!usb.send(oneByte(Port::UsbOut, mnky::midi::kClockByte)));  // no outputs open

  const std::uint8_t noteOn[3] = {0x90, 60, 100};
  const MidiEvent ev = UsbMidi::makeEvent(noteOn, 3, 2, 777);
  CHECK(ev.source == Port::UsbIn);
  CHECK(ev.device == 2);
  CHECK(ev.size == 3);
  CHECK(ev.data[0] == 0x90 && ev.data[1] == 60 && ev.data[2] == 100);
  CHECK(ev.stampUs == 777);

  const std::uint8_t clockByte[1] = {mnky::midi::kClockByte};
  const MidiEvent tick = UsbMidi::makeEvent(clockByte, 1, UsbMidi::kVirtualDevice, 5);
  CHECK(tick.size == 1);
  CHECK(tick.device == UsbMidi::kVirtualDevice);
  CHECK(tick.data[0] == mnky::midi::kClockByte);
  CHECK(tick.data[1] == 0 && tick.data[2] == 0);

  // Not forwardable: SysEx/longer, empty, and data bytes that never had a status.
  const std::uint8_t sysex[6] = {0xF0, 0x7D, 1, 2, 3, 0xF7};
  CHECK(UsbMidi::makeEvent(sysex, 6, 0, 0).size == 0);
  CHECK(UsbMidi::makeEvent(noteOn, 0, 0, 0).size == 0);
  const std::uint8_t dataOnly[2] = {60, 100};
  CHECK(UsbMidi::makeEvent(dataOnly, 2, 0, 0).size == 0);
  CHECK(UsbMidi::makeEvent(nullptr, 3, 0, 0).size == 0);

  // Enumeration must not throw even with no devices attached.
  const auto inputs = usb.listInputs();
  const auto outputs = usb.listOutputs();
  CHECK(inputs.size() == usb.listInputs().size());
  CHECK(outputs.size() == usb.listOutputs().size());
  usb.closeAll();
  usb.closeAll();  // idempotent
  CHECK(usb.inputCount() == 0);

  // Pi timeline is monotonic.
  const std::uint64_t first = mnky::midi::piNowUs();
  const std::uint64_t second = mnky::midi::piNowUs();
  CHECK(second >= first);
  pass("usb wrapper");
}

// ---------------------------------------------------------------------------
// clock: per-output mirror gates (EV_CLOCK_ROUTE semantics)
// ---------------------------------------------------------------------------
void clockRoutingGates() {
  UsbClock c;
  CHECK(c.clockEnabled());
  CHECK(c.transportEnabled());
  CHECK(c.mirrorAllows(mnky::midi::kClockByte));
  CHECK(c.mirrorAllows(mnky::midi::kStartByte));
  CHECK(c.mirrorAllows(mnky::midi::kContinueByte));
  CHECK(c.mirrorAllows(mnky::midi::kStopByte));
  CHECK(c.mirrorAllows(0x90));  // ordinary MIDI is not mirror-generated

  c.setTempo(12000);
  c.setRolling(true);
  c.addTick(0, 1000, 5000);  // offset 4000, first byte due at 5000
  const auto first_due = c.nextDueUs(5000);
  CHECK(first_due.has_value() && *first_due == 5000);
  CHECK(c.popDue(5000));
  const std::uint64_t skipped_before = c.skipped();

  // Clock gate off: no scheduled bytes, but the phase and the transport gate
  // keep their state, so a transport byte is still allowed.
  c.setClockEnabled(false);
  CHECK(!c.clockEnabled());
  CHECK(!c.nextDueUs(5000 + 20834).has_value());
  CHECK(!c.popDue(5000 + 20834));
  CHECK(!c.popDue(5000 + 2 * 20834));
  CHECK(c.skipped() == skipped_before);
  CHECK(c.mirrorAllows(mnky::midi::kStartByte));
  CHECK(!c.mirrorAllows(mnky::midi::kClockByte));

  // Transport gate off: START/CONTINUE/STOP are suppressed independently.
  c.setTransportEnabled(false);
  CHECK(!c.transportEnabled());
  CHECK(!c.mirrorAllows(mnky::midi::kStartByte));
  CHECK(!c.mirrorAllows(mnky::midi::kContinueByte));
  CHECK(!c.mirrorAllows(mnky::midi::kStopByte));
  CHECK(c.mirrorAllows(0x90));

  // Re-opening the clock gate emits one byte immediately and counts the
  // gated intervals as skipped instead of bursting them back to back.
  c.setClockEnabled(true);
  CHECK(c.clockEnabled());
  const std::uint64_t reopened = 5000 + 3 * 20834;
  const auto reopened_due = c.nextDueUs(reopened);
  CHECK(reopened_due.has_value() && *reopened_due == reopened);
  CHECK(c.popDue(reopened));
  CHECK(c.skipped() > skipped_before);
  CHECK(c.mirrorAllows(mnky::midi::kClockByte));
  CHECK(!c.popDue(reopened));  // phase advanced one interval past `reopened`

  c.setTransportEnabled(true);
  CHECK(c.transportEnabled());
  CHECK(c.mirrorAllows(mnky::midi::kStartByte));
  CHECK(c.mirrorAllows(mnky::midi::kContinueByte));
  CHECK(c.mirrorAllows(mnky::midi::kStopByte));
  pass("clock routing gates");
}

// ---------------------------------------------------------------------------
// clock: no-Pico free-run (no Pico ticks; Pi owns the phase)
// ---------------------------------------------------------------------------
void clockFreeRunScheduling() {
  UsbClock c;
  CHECK(!c.freeRun());
  c.setFreeRun(true);
  CHECK(c.freeRun());
  CHECK(!c.primed());  // no Pico stamp is involved in free-run
  c.setTempo(12000);
  c.setRolling(true);
  CHECK(!c.popDue(1000));  // no phase until the transport start pegs it

  constexpr std::uint64_t kStart = 1'000'000;
  c.startFreeRun(kStart);
  CHECK(c.primed());
  const auto interval = static_cast<std::uint64_t>(std::ceil(c.intervalUs()));
  CHECK(!c.popDue(kStart + interval - 2));
  CHECK(c.popDue(kStart + interval));
  CHECK(c.popDue(kStart + 2 * interval));

  // A tempo edit applies to the next interval instead of re-pegging the phase.
  c.setTempo(24000);
  const auto afterTempo = c.nextDueUs(0);
  CHECK(afterTempo.has_value());
  CHECK(c.popDue(*afterTempo));
  CHECK(!c.popDue(*afterTempo));  // exactly one interval advanced

  // A tick cannot disturb the local phase in free-run.
  const auto phase = c.nextDueUs(0);
  c.addTick(42, 7, 9'999'999);
  CHECK(c.nextDueUs(0) == phase);
  CHECK(c.lastTick() == 0);
  CHECK(c.reordered() == 0);

  // The clock gate still suppresses bytes, and stop/start pegs a fresh phase.
  c.setClockEnabled(false);
  CHECK(!c.nextDueUs(0).has_value());
  CHECK(!c.popDue(2'000'000'000));
  // ...but the free-running generator keeps ticking while the output is muted.
  const std::uint64_t skippedBefore = c.skipped();
  CHECK(c.advanceFreeRun(2'000'000'000));
  CHECK(c.skipped() > skippedBefore);
  CHECK(!c.advanceFreeRun(2'000'000'000));  // one interval per call
  c.setClockEnabled(true);
  c.setRolling(false);
  CHECK(!c.popDue(3'000'000'000));
  c.setRolling(true);
  c.startFreeRun(3'000'000'000);
  const auto restartedInterval = static_cast<std::uint64_t>(std::ceil(c.intervalUs()));
  CHECK(c.popDue(3'000'000'000 + restartedInterval));
  pass("clock free-run no-Pico scheduling");
}

// ---------------------------------------------------------------------------
// USB transport follow (Pi-side EV_TRANSPORT forwarding policy)
// ---------------------------------------------------------------------------
void usbTransportFollow() {
  UsbTransportFollow follow;
  CHECK(!follow.enabled());  // the Pico stays transport master by default

  MidiEvent ev;
  ev.source = Port::UsbIn;
  ev.size = 1;
  ev.data[0] = mnky::midi::kStartByte;
  CHECK(follow.commandFor(ev) == -1);  // disabled: nothing forwarded
  CHECK(!follow.forwards(ev));

  follow.setEnabled(true);
  CHECK(follow.enabled());
  CHECK(follow.commandFor(ev) == 1);
  CHECK(follow.forwards(ev));
  std::vector<std::uint8_t> sent;
  CHECK(follow.forward(ev, [&sent](std::uint8_t command) { sent.push_back(command); }));
  CHECK(sent.size() == 1 && sent[0] == 1);  // existing EV_TRANSPORT path
  ev.data[0] = mnky::midi::kContinueByte;
  CHECK(follow.commandFor(ev) == 2);
  ev.data[0] = mnky::midi::kStopByte;
  CHECK(follow.commandFor(ev) == 0);

  // USB is never a clock source: 0xF8 is not a transport command.
  ev.data[0] = mnky::midi::kClockByte;
  CHECK(follow.commandFor(ev) == -1);
  CHECK(!follow.forwards(ev));
  sent.clear();
  CHECK(!follow.forward(ev, [&sent](std::uint8_t command) { sent.push_back(command); }));
  CHECK(sent.empty());

  // Only 1-byte USB transport messages qualify; TRS input is Pico-side.
  ev.data[0] = mnky::midi::kStartByte;
  ev.size = 3;
  ev.data[1] = 60;
  ev.data[2] = 100;
  CHECK(follow.commandFor(ev) == -1);
  ev.size = 1;
  ev.source = Port::TrsIn1;
  CHECK(follow.commandFor(ev) == -1);
  ev.source = Port::UsbOut;
  CHECK(follow.commandFor(ev) == -1);

  follow.setEnabled(false);
  ev.source = Port::UsbIn;
  CHECK(follow.commandFor(ev) == -1);
  pass("usb transport follow");
}

}  // namespace

int main() {
  routingMatrix();
  fanOut();
  channelFilter();
  transpose();
  routingPipes();
  clockTempoAndTransport();
  clockOffsetUnderJitter();
  clockPhaseScheduling();
  clockRoutingGates();
  clockFreeRunScheduling();
  usbTransportFollow();
  buttonMap();
  usbWrapper();

  if (g_failures != 0) {
    std::fprintf(stderr, "FAILURES %d\n", g_failures);
    return 1;
  }
  std::printf("PASS test_midi\n");
  return 0;
}
