// Host tests for the JUCE-free Pico link core. Asserts only (no framework):
// builds with the plain `c++ -std=c++20 -Wall -Wextra -Werror` line from
// pi/ARCHITECTURE.md and prints one PASS line per behaviour.
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

#include "link.hpp"
#include "../../firmware/pico/clock_follow.hpp"

namespace {

using mnky::link::EmulatorTransport;
using mnky::link::ISpiTransport;
using mnky::link::PicoLink;

constexpr int kPollMs = 1;

// One poll is one paced transaction; sleep between polls like the app loop.
void pump(PicoLink& link, int ms) {
  for (int i = 0; i < ms; ++i) {
    link.poll();
    std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
  }
}

template <typename Pred>
bool pumpUntil(PicoLink& link, int max_ms, Pred done) {
  for (int i = 0; i < max_ms; ++i) {
    link.poll();
    if (done()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
  }
  return false;
}

std::vector<PicoLink::Event> drain(PicoLink& link) {
  std::vector<PicoLink::Event> out;
  PicoLink::Event e{};
  while (link.nextEvent(e)) out.push_back(e);
  return out;
}

int countKind(const std::vector<PicoLink::Event>& events, PicoLink::Event::Kind kind) {
  int n = 0;
  for (const auto& e : events)
    if (e.kind == kind) ++n;
  return n;
}

const PicoLink::Event* findKind(const std::vector<PicoLink::Event>& events,
                                PicoLink::Event::Kind kind) {
  for (const auto& e : events)
    if (e.kind == kind) return &e;
  return nullptr;
}

class CountingTransport : public ISpiTransport {
 public:
  explicit CountingTransport(ISpiTransport& inner) : inner_(inner) {}
  bool transfer(const std::uint8_t out[32], std::uint8_t in[32]) override {
    ++calls;
    return inner_.transfer(out, in);
  }
  unsigned calls = 0;

 private:
  ISpiTransport& inner_;
};

// Records every outbound 32-byte frame (idle-fill repeats included), so a test
// can check the wire bytes of a specific sender.
class RecordingTransport : public ISpiTransport {
 public:
  explicit RecordingTransport(ISpiTransport& inner) : inner_(inner) {}
  bool transfer(const std::uint8_t out[32], std::uint8_t in[32]) override {
    mb::Bytes wire{};
    std::copy(out, out + mb::frame_size, wire.begin());
    frames.push_back(wire);
    return inner_.transfer(out, in);
  }
  std::vector<mb::Bytes> frames;

 private:
  ISpiTransport& inner_;
};

void testHandshake() {
  EmulatorTransport pico;
  PicoLink link(pico);
  assert(!link.linkUp());
  assert(link.piSession() != 0);
  assert(link.session() == 0);  // no Pico frame exchanged yet
  assert(pumpUntil(link, 200, [&] { return link.linkUp(); }));
  assert(link.session() == pico.session());
  assert(link.session() != link.piSession());
  pump(link, 10);  // the Pico queues ACK, then state and tempo

  const auto events = drain(link);
  assert(countKind(events, PicoLink::Event::Linked) == 1);
  const auto* state = findKind(events, PicoLink::Event::State);
  assert(state != nullptr && state->a[0] == 0 && state->u32a == 0 && state->n == 1);
  const auto* tempo = findKind(events, PicoLink::Event::Tempo);
  assert(tempo != nullptr && tempo->u32a == mnky::link::kTempoDefaultX100);

  const auto stats = link.stats();
  assert(stats.tx >= 2 && stats.rx >= 2);
  assert(stats.bad == 0);
  assert(stats.dup >= 1);  // HELLO/idle repeats were deduped, not re-applied
  std::cout << "PASS handshake: HELLO/ACK link-up, boot state + tempo, idle-fill dedup\n";
}

void testPacing() {
  EmulatorTransport pico;
  CountingTransport counted(pico);
  PicoLink link(counted);
  link.poll();
  link.poll();  // same millisecond: must be swallowed by the >= 1 ms pacing
  assert(counted.calls == 1);
  std::this_thread::sleep_for(std::chrono::milliseconds(2));
  link.poll();
  assert(counted.calls == 2);
  std::cout << "PASS pacing: at most one transaction per >=1 ms window\n";
}

void testMidiAndDedup() {
  EmulatorTransport pico;
  PicoLink link(pico);
  assert(pumpUntil(link, 200, [&] { return link.linkUp(); }));
  drain(link);

  const std::uint8_t note[3] = {0x90, 0x40, 0x7f};
  assert(pico.injectMidi(1, note, 3));
  pump(link, 60);  // the frame plus ~55 idle-fill repeats of it
  const auto events = drain(link);
  assert(countKind(events, PicoLink::Event::MidiIn) == 1);  // duplicate ignored
  for (const auto& e : events) {
    if (e.kind != PicoLink::Event::MidiIn) continue;
    assert(e.n == 3 && e.a[0] == 0x90 && e.a[1] == 0x40 && e.a[2] == 0x7f);
    assert(e.u32a == 1);  // MIDI IN 2 (UART1)
  }
  assert(link.stats().dup > 1);
  assert(pico.injectMidi(2, note, 3) == false);  // only ports 0/1 exist
  assert(pico.injectMidi(0, note, 4) == false);
  std::cout << "PASS MIDI in: timestamped MIDI_SEND-free MIDI event, repeats deduped\n";
}

void testButtons() {
  EmulatorTransport pico;
  PicoLink link(pico);
  assert(pumpUntil(link, 200, [&] { return link.linkUp(); }));
  drain(link);

  pico.injectButton(mb::CTL_PLAY, 1);
  pico.injectButton(mb::CTL_ENCODER_TURN, 0xff);  // int8 delta -1
  pump(link, 30);
  const auto events = drain(link);
  int controls = 0;
  bool saw_play = false, saw_encoder = false;
  for (const auto& e : events) {
    if (e.kind != PicoLink::Event::Control) continue;
    ++controls;
    assert(e.n == 2);
    if (e.a[0] == mb::CTL_PLAY) {
      saw_play = true;
      assert(e.a[1] == 1);
    }
    if (e.a[0] == mb::CTL_ENCODER_TURN) {
      saw_encoder = true;
      assert(static_cast<std::int8_t>(e.a[1]) == -1);
    }
  }
  assert(controls == 2 && saw_play && saw_encoder);
  std::cout << "PASS controls: button state + signed encoder delta events\n";
}

void testSendMidiCapture() {
  EmulatorTransport pico;
  PicoLink link(pico);
  assert(pumpUntil(link, 200, [&] { return link.linkUp(); }));

  const std::uint8_t cc[3] = {0xb0, 7, 100};
  assert(link.sendMidi(1, cc, 3));
  pump(link, 40);  // repeated as idle fill afterwards: must still emit once
  assert(pico.sentMidi().size() == 1);
  const auto& sent = pico.sentMidi()[0];
  assert(sent.port == 1 && sent.size == 3);
  assert(sent.data[0] == 0xb0 && sent.data[1] == 7 && sent.data[2] == 100);
  assert(link.sendMidi(2, cc, 3) == false);  // no such UART
  assert(link.sendMidi(0, cc, 4) == false);  // payload limit is 3 bytes
  pico.clearSentMidi();
  std::cout << "PASS MIDI_SEND: captured once, invalid port/length rejected\n";
}

void testTempoTransportManual() {
  EmulatorTransport pico;
  PicoLink link(pico);
  assert(pumpUntil(link, 200, [&] { return link.linkUp(); }));
  // Flush the boot announcement batch so each phase sees only its own frames.
  pump(link, 25);
  drain(link);

  assert(link.setTempo(15000));
  pump(link, 20);
  assert(pico.tempoX100() == 15000);
  const auto tempo_events = drain(link);
  const auto* echo = findKind(tempo_events, PicoLink::Event::Tempo);
  assert(echo != nullptr && echo->u32a == 15000);

  assert(link.transport(1));  // start: resets the Pico tick count
  pump(link, 20);
  assert(pico.rolling());
  const auto start_events = drain(link);
  const auto* start = findKind(start_events, PicoLink::Event::State);
  assert(start != nullptr && start->a[0] == 1 && start->u32a == 0);
  assert(link.transport(3) == false);

  assert(link.transport(0));
  pump(link, 20);
  assert(!pico.rolling());
  const auto stop_events = drain(link);
  const auto* stop = findKind(stop_events, PicoLink::Event::State);
  assert(stop != nullptr && stop->a[0] == 0);

  assert(link.setTempo(40000));  // clamped to the firmware window (20-300 bpm)
  pump(link, 20);
  assert(pico.tempoX100() == mnky::link::kTempoMaxX100);
  assert(link.setTempo(1000));
  pump(link, 20);
  assert(pico.tempoX100() == mnky::link::kTempoMinX100);

  assert(link.setManual(true));
  assert(link.clockEnable(true));
  assert(link.ledSet(mb::LED_REC | mb::LED_PLAY, mb::LED_PLAY));
  pump(link, 20);
  assert(pico.manual());
  assert(pico.clockEnableSeen());  // accepted but ignored by the firmware
  assert(pico.ledMask() == (mb::LED_REC | mb::LED_PLAY));
  assert(pico.ledStates() == mb::LED_PLAY);
  drain(link);
  std::cout << "PASS tempo/transport/mode: echo + state frames, clamp, panel mode, LEDs\n";
}

void testClockRouteSender() {
  EmulatorTransport pico;
  PicoLink link(pico);
  assert(pumpUntil(link, 200, [&] { return link.linkUp(); }));
  pump(link, 25);  // flush the boot announcement batch
  drain(link);

  // Boot default: both outputs on (legacy behavior).
  assert(pico.clockRouteFlags(0) == mb::CLOCK_ROUTE_DEFAULT);
  assert(pico.clockRouteFlags(1) == mb::CLOCK_ROUTE_DEFAULT);

  assert(link.clockRoute(0, mb::CLOCK_ROUTE_TRANSPORT));  // OUT1: clock off
  pump(link, 20);
  assert(pico.clockRouteFlags(0) == mb::CLOCK_ROUTE_TRANSPORT);
  assert(pico.clockRouteFlags(1) == mb::CLOCK_ROUTE_DEFAULT);

  // The sender validates before queueing: no such port, no reserved bits.
  assert(!link.clockRoute(2, mb::CLOCK_ROUTE_DEFAULT));
  assert(!link.clockRoute(0, 0x80));
  pump(link, 20);
  assert(pico.clockRouteFlags(0) == mb::CLOCK_ROUTE_TRANSPORT);
  assert(pico.clockRouteFlags(1) == mb::CLOCK_ROUTE_DEFAULT);

  // A Pico power cycle resets RAM gates to defaults; after the re-handshake
  // the link re-sends the last-known route for the output the Pi had set.
  pico.reboot();
  assert(pumpUntil(link, 300, [&] { return link.linkUp(); }));
  pump(link, 30);
  assert(pico.clockRouteFlags(0) == mb::CLOCK_ROUTE_TRANSPORT);
  assert(pico.clockRouteFlags(1) == mb::CLOCK_ROUTE_DEFAULT);

  // RESET_LINK keeps the Pico gates (RAM survives) and the Pi re-asserts the
  // cached route for both outputs anyway.
  assert(link.clockRoute(1, mb::CLOCK_ROUTE_CLOCK));
  pump(link, 20);
  assert(pico.clockRouteFlags(1) == mb::CLOCK_ROUTE_CLOCK);
  const std::uint32_t reset_from = link.session();
  assert(link.resetLink());
  assert(pumpUntil(link, 300, [&] { return link.session() != reset_from && link.linkUp(); }));
  pump(link, 30);
  assert(pico.clockRouteFlags(0) == mb::CLOCK_ROUTE_TRANSPORT);
  assert(pico.clockRouteFlags(1) == mb::CLOCK_ROUTE_CLOCK);
  std::cout << "PASS clock route: per-output flags validated, applied and resent after HELLO\n";
}

void testClockSourceSender() {
  EmulatorTransport pico;
  PicoLink link(pico);
  assert(pumpUntil(link, 200, [&] { return link.linkUp(); }));
  pump(link, 25);
  drain(link);

  // Boot default: internal generator, no transport follow.
  assert(pico.clockSource() == mb::CLOCK_SOURCE_INTERNAL);
  assert(pico.transportFollowMask() == 0);

  assert(link.clockSource(mb::CLOCK_SOURCE_TRS1,
                          mb::CLOCK_FOLLOW_TRS1 | mb::CLOCK_FOLLOW_TRS2));
  pump(link, 20);
  assert(pico.clockSource() == mb::CLOCK_SOURCE_TRS1);
  assert(pico.transportFollowMask() == (mb::CLOCK_FOLLOW_TRS1 | mb::CLOCK_FOLLOW_TRS2));

  // Validation happens before queueing: source 3+ and the Pi-side USB bit are
  // rejected and change nothing.
  assert(!link.clockSource(3, 0));
  assert(!link.clockSource(mb::CLOCK_SOURCE_TRS2, 0x04));
  pump(link, 20);
  assert(pico.clockSource() == mb::CLOCK_SOURCE_TRS1);
  assert(pico.transportFollowMask() == (mb::CLOCK_FOLLOW_TRS1 | mb::CLOCK_FOLLOW_TRS2));

  // A Pico power cycle resets RAM follow state to internal/no-follow; the
  // reconnect re-sends the last-known config.
  pico.reboot();
  assert(pumpUntil(link, 300, [&] { return link.linkUp(); }));
  pump(link, 30);
  assert(pico.clockSource() == mb::CLOCK_SOURCE_TRS1);
  assert(pico.transportFollowMask() == (mb::CLOCK_FOLLOW_TRS1 | mb::CLOCK_FOLLOW_TRS2));

  // RESET_LINK keeps the Pico RAM state and the Pi re-asserts it too.
  assert(link.clockSource(mb::CLOCK_SOURCE_TRS2, mb::CLOCK_FOLLOW_TRS2));
  pump(link, 20);
  const std::uint32_t reset_from = link.session();
  assert(link.resetLink());
  assert(pumpUntil(link, 300, [&] { return link.session() != reset_from && link.linkUp(); }));
  pump(link, 30);
  assert(pico.clockSource() == mb::CLOCK_SOURCE_TRS2);
  assert(pico.transportFollowMask() == mb::CLOCK_FOLLOW_TRS2);
  std::cout << "PASS clock source: source/mask validated, applied and resent after HELLO\n";
}

void testMidiRouteSender() {
  // 0x19 carries the per-pipe channel scope after the positional params, so a
  // TRS chain's chMask reaches the Pico (legacy mask-less frames stay valid).
  assert(mnky::link::kMidiRoutePipeCarriesChannelScope);
  EmulatorTransport pico;
  PicoLink link(pico);
  assert(pumpUntil(link, 200, [&] { return link.linkUp(); }));
  pump(link, 25);
  drain(link);

  // Boot: every TRS route is off with an empty chain (RAM defaults).
  for (std::uint8_t src = 0; src < 2; ++src) {
    for (std::uint8_t dst = 0; dst < 2; ++dst) {
      assert(pico.midiRoute(src, dst).flags == 0);
      assert(pico.midiRoute(src, dst).pipe_count == 0);
    }
  }

  // Begin + a two-chunk Filter: 4 params then 4 more at offset 4, exactly the
  // current frame limit (route_pipe_param_frame_max), with a channel scope.
  const std::int16_t filter[4] = {1, 0, 1, 1};
  const std::int16_t tail[4] = {0, 1, 1, 0};
  assert(link.midiRoute(0, 1, true));
  assert(link.midiRoutePipe(0, 1, 0, 1, false, 0, filter, 4, 0x0003));
  // A continuation chunk's chMask is ignored (like its bypass byte).
  assert(link.midiRoutePipe(0, 1, 0, 1, false, 4, tail, 4, 0x0001));
  pump(link, 30);
  assert(pico.midiRoute(0, 1).flags == mb::ROUTE_FLAG_ON);
  assert(pico.midiRoute(0, 1).pipe_count == 1);
  const auto& filter_pipe = pico.midiRoute(0, 1).pipes[0];
  assert(filter_pipe.type == 1 && !filter_pipe.bypass);
  assert(filter_pipe.chMask == 0x0003);  // the offset-0 chunk's mask
  assert(filter_pipe.param_count == 8);
  assert(filter_pipe.params[0] == 1 && filter_pipe.params[3] == 1);
  assert(filter_pipe.params[4] == 0 && filter_pipe.params[7] == 0);

  // index == pipe_count appends; a gap is rejected before it reaches the wire.
  const std::int16_t chance = 35;
  assert(!link.midiRoutePipe(0, 1, 2, 6, false, 0, &chance, 1));
  assert(link.midiRoutePipe(0, 1, 1, 6, false, 0, &chance, 1));
  pump(link, 20);
  assert(pico.midiRoute(0, 1).pipe_count == 2);
  assert(pico.midiRoute(0, 1).pipes[1].type == 6);
  assert(pico.midiRoute(0, 1).pipes[1].param_count == 1);
  assert(pico.midiRoute(0, 1).pipes[1].params[0] == 35);

  // Sender-side validation mirrors the wire limits: only the two physical TRS
  // routes exist, types are 1..16, 4 params per chunk (a pre-scope frame's 5
  // are not produced) and 10 per pipe, and a continuation must match the pipe
  // it extends.
  assert(!link.midiRoute(2, 0, true));
  assert(!link.midiRoute(0, 2, true));
  assert(!link.midiRoutePipe(2, 0, 0, 1, false, 0, nullptr, 0));
  assert(!link.midiRoutePipe(0, 0, 0, 0, false, 0, nullptr, 0));
  assert(!link.midiRoutePipe(0, 0, 0, 17, false, 0, nullptr, 0));
  assert(!link.midiRoutePipe(0, 0, 0, 1, false, 0, nullptr, 5));
  assert(!link.midiRoutePipe(0, 0, 0, 1, false, 0, nullptr, 6));
  assert(!link.midiRoutePipe(0, 0, 0, 1, false, 6, nullptr, 4));
  assert(!link.midiRoutePipe(0, 0, 0, 1, false, 0, nullptr, 1));
  assert(!link.midiRoutePipe(0, 0, 1, 1, false, 0, nullptr, 0));
  assert(link.midiRoute(0, 0, true));
  assert(link.midiRoutePipe(0, 0, 0, 1, false, 0, filter, 4));
  const std::int16_t one = 1;
  assert(!link.midiRoutePipe(0, 0, 0, 2, false, 5, &one, 1));  // type mismatch
  pump(link, 20);
  assert(pico.midiRoute(0, 0).pipe_count == 1);
  assert(pico.midiRoute(0, 0).pipes[0].type == 1);

  // A continuation chunk's bypass byte is ignored, matching the firmware's
  // setPipeChunk (the type is the only identity check for offset > 0).
  assert(link.midiRoute(1, 1, true));
  assert(link.midiRoutePipe(1, 1, 0, 2, false, 0, &one, 1));
  const std::int16_t window[2] = {60, 72};
  assert(link.midiRoutePipe(1, 1, 0, 2, true, 1, window, 2));
  pump(link, 20);
  assert(!pico.midiRoute(1, 1).pipes[0].bypass);  // the begin chunk's value
  assert(pico.midiRoute(1, 1).pipes[0].param_count == 3);
  assert(pico.midiRoute(1, 1).pipes[0].params[1] == 60);
  assert(pico.midiRoute(1, 1).pipes[0].params[2] == 72);

  // Pseudo-source route kinds (0x18/0x19 src byte): a generator chain headed
  // by the LFO, and output-insert slots 0 and 2 on the same output.
  const std::uint8_t generator = mb::route_src_generator;
  const std::uint8_t insert0 = mb::route_src_insert(0);
  const std::uint8_t insert2 = mb::route_src_insert(2);
  const std::int16_t lfo[5] = {1, 0, 2, 64, 64};
  assert(link.midiRoute(generator, 1, true));
  assert(link.midiRoutePipe(generator, 1, 0, 14, false, 0, lfo, 4));
  assert(link.midiRoutePipe(generator, 1, 0, 14, false, 4, &lfo[4], 1));
  const std::int16_t cc_to_pc[5] = {1, 0, 127, 2, 5};
  assert(link.midiRoute(insert0, 1, true));
  assert(link.midiRoutePipe(insert0, 1, 0, 11, false, 0, cc_to_pc, 4));
  assert(link.midiRoutePipe(insert0, 1, 0, 11, false, 4, &cc_to_pc[4], 1));
  assert(link.midiRoute(insert2, 0, true));
  pump(link, 40);
  assert(pico.midiRoute(generator, 1).flags == mb::ROUTE_FLAG_ON);
  assert(pico.midiRoute(generator, 1).pipe_count == 1);
  assert(pico.midiRoute(generator, 1).pipes[0].type == 14);
  assert(pico.midiRoute(insert0, 1).pipe_count == 1);
  assert(pico.midiRoute(insert0, 1).pipes[0].type == 11);
  assert(pico.midiRoute(insert0, 1).pipes[0].param_count == 5);
  assert(pico.midiRoute(insert2, 0).flags == mb::ROUTE_FLAG_ON);

  // A generator chain's head must have the schema role generator; the link
  // refuses the frame before it reaches the Pico.
  assert(link.midiRoute(generator, 0, true));
  const std::int16_t one_head = 0;
  assert(!link.midiRoutePipe(generator, 0, 0, 12, false, 0, &one_head, 1));
  pump(link, 20);
  assert(pico.midiRoute(generator, 0).pipe_count == 0);

  // Undefined source bytes are refused on both 0x18 and 0x19; the legacy
  // source bytes 0/1 above keep their meaning.
  assert(!link.midiRoute(0xFA, 0, true));
  assert(!link.midiRoutePipe(0xFA, 0, 0, 1, false, 0, nullptr, 0));

  // A Pico power cycle drops the RAM chains; the reconnect re-sends 0x18
  // before the chunks, so the pipe count and params come back.
  pico.reboot();
  assert(pumpUntil(link, 300, [&] { return link.linkUp(); }));
  pump(link, 40);
  assert(pico.midiRoute(0, 1).flags == mb::ROUTE_FLAG_ON);
  assert(pico.midiRoute(0, 1).pipe_count == 2);
  assert(pico.midiRoute(0, 1).pipes[0].param_count == 8);
  assert(pico.midiRoute(0, 1).pipes[0].params[5] == 1);
  assert(pico.midiRoute(0, 1).pipes[0].chMask == 0x0003);
  assert(pico.midiRoute(0, 1).pipes[1].params[0] == 35);
  assert(pico.midiRoute(0, 0).pipe_count == 1);
  assert(pico.midiRoute(generator, 1).pipes[0].type == 14);
  assert(pico.midiRoute(insert0, 1).pipes[0].param_count == 5);
  assert(pico.midiRoute(insert2, 0).flags == mb::ROUTE_FLAG_ON);

  // RESET_LINK keeps the Pico RAM state and the Pi re-asserts the cache.
  const std::uint32_t reset_from = link.session();
  assert(link.resetLink());
  assert(pumpUntil(link, 300, [&] { return link.session() != reset_from && link.linkUp(); }));
  pump(link, 40);
  assert(pico.midiRoute(0, 1).flags == mb::ROUTE_FLAG_ON);
  assert(pico.midiRoute(0, 1).pipe_count == 2);

  // A fresh begin clears the chain on both sides (the firmware begin() contract).
  assert(link.midiRoute(0, 1, false));
  pump(link, 20);
  assert(pico.midiRoute(0, 1).flags == 0);
  assert(pico.midiRoute(0, 1).pipe_count == 0);
  std::cout << "PASS midi route: chain begin/chunks/chMask validated, applied and resent after HELLO\n";
}

void testMidiRouteEncoding() {
  EmulatorTransport pico;
  RecordingTransport recorded(pico);
  PicoLink link(recorded);
  assert(pumpUntil(link, 200, [&] { return link.linkUp(); }));
  pump(link, 20);
  recorded.frames.clear();

  assert(link.midiRoute(1, 0, true));
  const std::int16_t params[2] = {-7, 300};
  assert(link.midiRoutePipe(1, 0, 0, 11, true, 0, params, 2, 0x0005));
  pump(link, 20);

  bool saw_route = false;
  bool saw_pipe = false;
  for (const auto& wire : recorded.frames) {
    mb::Frame frame{};
    assert(mb::decode(wire, frame));
    if (frame.type != mb::Type::control_event) continue;
    if (frame.payload[0] == mb::EV_MIDI_ROUTE && !saw_route) {
      assert(frame.length == 4);
      assert(frame.payload[1] == 1 && frame.payload[2] == 0);
      assert(frame.payload[3] == mb::ROUTE_FLAG_ON);
      saw_route = true;
    }
    if (frame.payload[0] == mb::EV_MIDI_ROUTE_PIPE && !saw_pipe) {
      assert(frame.length == 10 + 2 * 2);  // header + params + chMask
      assert(frame.payload[1] == 1 && frame.payload[2] == 0);
      assert(frame.payload[3] == 0);   // pipe index
      assert(frame.payload[4] == 11);  // Transform
      assert(frame.payload[5] == 1);   // bypass
      assert(frame.payload[6] == 0 && frame.payload[7] == 2);
      const auto low = static_cast<std::uint16_t>(frame.payload[8] |
                                                  (frame.payload[9] << 8));
      const auto high = static_cast<std::uint16_t>(frame.payload[10] |
                                                   (frame.payload[11] << 8));
      const auto mask = static_cast<std::uint16_t>(frame.payload[12] |
                                                   (frame.payload[13] << 8));
      assert(mask == 0x0005);
      assert(static_cast<std::int16_t>(low) == -7);
      assert(static_cast<std::int16_t>(high) == 300);
      saw_pipe = true;
    }
  }
  assert(saw_route);
  assert(saw_pipe);
  std::cout << "PASS midi route encoding: 0x18/0x19 payloads, int16 LE params, chMask\n";
}

void testMidiRouteSend() {
  EmulatorTransport pico;
  RecordingTransport recorded(pico);
  PicoLink link(recorded);
  assert(pumpUntil(link, 200, [&] { return link.linkUp(); }));
  pump(link, 20);
  recorded.frames.clear();

  // 0x1A carries src, dst, size and the bytes verbatim; the Pico evaluates the
  // chain configured by 0x18/0x19 for that pair.
  const std::uint8_t noteOn[3] = {0x90, 60, 100};
  assert(link.sendMidiRoute(1, 0, noteOn, 3));
  const std::uint8_t clockByte = 0xF8;
  assert(link.sendMidiRoute(0, 1, &clockByte, 1));
  pump(link, 20);

  bool sawSend = false;
  for (const auto& wire : recorded.frames) {
    mb::Frame frame{};
    assert(mb::decode(wire, frame));
    if (frame.type != mb::Type::control_event ||
        frame.payload[0] != mb::EV_MIDI_ROUTE_SEND)
      continue;
    std::uint8_t src = 0;
    std::uint8_t dst = 0;
    std::uint8_t size = 0;
    std::uint8_t data[3]{};
    assert(mb::unpack_midi_route_send(frame, src, dst, data, size));
    if (!sawSend) {
      assert(src == 1 && dst == 0 && size == 3);
      assert(data[0] == 0x90 && data[1] == 60 && data[2] == 100);
      sawSend = true;
    } else {
      assert(src == 0 && dst == 1 && size == 1 && data[0] == 0xF8);
    }
  }
  assert(sawSend);

  // Sender-side validation matches the wire: two physical TRS ports, 1..3
  // bytes, and a valid message pointer.
  assert(!link.sendMidiRoute(2, 0, noteOn, 3));
  assert(!link.sendMidiRoute(0, 2, noteOn, 3));
  assert(!link.sendMidiRoute(0, 0, noteOn, 0));
  assert(!link.sendMidiRoute(0, 0, noteOn, 4));
  assert(!link.sendMidiRoute(0, 0, nullptr, 1));
  std::cout << "PASS midi route send: 0x1A carries src/dst/size/bytes\n";
}

void testStatusRequest() {
  EmulatorTransport pico;
  PicoLink link(pico);
  assert(pumpUntil(link, 200, [&] { return link.linkUp(); }));
  pump(link, 25);  // flush the boot announcement batch
  drain(link);

  // Default snapshot is derived from emulator state: link up, stopped.
  assert(link.statusRequest());
  pump(link, 20);
  mb::LinkStatus live{};
  assert(link.lastStatus(live));
  assert((live.flags & mb::STATUS_LINK_UP) != 0);
  assert((live.flags & mb::STATUS_ROLLING) == 0);

  // Scripted values prove the host passes every field through unchanged.
  mb::LinkStatus script{};
  script.uptime_s = 0x01020304;
  script.ticks = 0x11223344;
  script.txq_depth = 7;
  script.txov = 0x2233;
  script.rxov = 0x4455;
  script.flags = mb::STATUS_LINK_UP | mb::STATUS_ROLLING;
  pico.scriptStatus(script);
  assert(link.statusRequest());
  pump(link, 20);
  mb::LinkStatus got{};
  assert(link.lastStatus(got));
  assert(got.uptime_s == script.uptime_s && got.ticks == script.ticks);
  assert(got.txq_depth == script.txq_depth);
  assert(got.txov == script.txov && got.rxov == script.rxov);
  assert(got.flags == script.flags);
  std::cout << "PASS status: STATUS_REQ/STATUS_RESP live + scripted fields\n";
}

void testTxOverflowStatus() {
  EmulatorTransport pico;
  PicoLink link(pico);
  assert(pumpUntil(link, 200, [&] { return link.linkUp(); }));
  pump(link, 25);
  drain(link);

  // 20 queued frames into a 16-deep queue: 4 drop-newest, counted as txov.
  for (int i = 0; i < 20; ++i) pico.setTempo(static_cast<std::uint16_t>(12000 + i));
  pump(link, 40);
  drain(link);
  assert(link.statusRequest());
  pump(link, 20);
  mb::LinkStatus status{};
  assert(link.lastStatus(status));
  assert(status.txov >= 4);
  std::cout << "PASS status counters: emulator tx overflow reported in STATUS_RESP\n";
}

void testRebootAndResetLink() {
  EmulatorTransport pico;
  PicoLink link(pico);
  assert(pumpUntil(link, 200, [&] { return link.linkUp(); }));
  pump(link, 25);
  drain(link);

  assert(link.setTempo(18000));
  assert(link.transport(1));
  pump(link, 30);
  drain(link);
  const std::uint32_t tick_before = pico.ticks();
  assert(pico.tempoX100() == 18000 && pico.rolling());
  assert(link.statusRequest());
  pump(link, 20);
  mb::LinkStatus before_reset{};
  assert(link.lastStatus(before_reset));
  assert((before_reset.flags & mb::STATUS_LINK_UP) != 0);
  assert((before_reset.flags & mb::STATUS_ROLLING) != 0);

  // RESET_LINK: fresh session, but tempo/transport/tick count survive.
  const std::uint32_t reset_from = link.session();
  assert(link.resetLink());
  assert(pumpUntil(link, 300, [&] { return link.session() != reset_from && link.linkUp(); }));
  pump(link, 30);
  const auto reset_events = drain(link);
  assert(countKind(reset_events, PicoLink::Event::LinkLost) == 1);
  assert(countKind(reset_events, PicoLink::Event::Linked) >= 1);
  assert(pico.tempoX100() == 18000 && pico.rolling());
  assert(pico.ticks() >= tick_before);
  const auto* reset_state = findKind(reset_events, PicoLink::Event::State);
  assert(reset_state != nullptr && reset_state->a[0] == 1);
  const auto* reset_tempo = findKind(reset_events, PicoLink::Event::Tempo);
  assert(reset_tempo != nullptr && reset_tempo->u32a == 18000);
  mb::LinkStatus stale{};
  assert(!link.lastStatus(stale));  // old-session snapshot was invalidated
  assert(link.statusRequest());
  pump(link, 20);
  mb::LinkStatus after_reset{};
  assert(link.lastStatus(after_reset));
  assert((after_reset.flags & mb::STATUS_LINK_UP) != 0);
  assert((after_reset.flags & mb::STATUS_ROLLING) != 0);

  // Mode 1 (USB boot) is recorded and leaves the emulated application link
  // running; mode 0 is a real reboot with a new session.
  assert(link.rebootPico(true));
  pump(link, 20);
  assert(pico.rebootRequested() && pico.usbBootRequested());
  const std::uint32_t reboot_from = link.session();
  assert(link.rebootPico(false));
  assert(pumpUntil(link, 300, [&] { return link.session() != reboot_from && link.linkUp(); }));
  const auto reboot_events = drain(link);
  assert(countKind(reboot_events, PicoLink::Event::LinkLost) == 1);
  assert(countKind(reboot_events, PicoLink::Event::Linked) >= 1);
  std::cout << "PASS control plane: reboot modes + RESET_LINK resync (no reboot)\n";
}

void testTickCallbackOrdering() {
  EmulatorTransport pico;
  PicoLink link(pico);
  struct Callback {
    std::uint32_t tick;
    std::uint32_t stamp;
    std::uint64_t now;
  };
  std::vector<Callback> callbacks;
  link.onTick([&](std::uint32_t tick, std::uint32_t stamp, std::uint64_t now) {
    callbacks.push_back({tick, stamp, now});
  });
  assert(pumpUntil(link, 200, [&] { return link.linkUp(); }));
  assert(link.setTempo(24000));  // 10.4 ms per 24-PPQN tick keeps the test short

  std::vector<PicoLink::Event> ticks;
  for (int i = 0; i < 400 && ticks.size() < 6; ++i) {
    link.poll();
    for (const auto& e : drain(link))
      if (e.kind == PicoLink::Event::Tick) ticks.push_back(e);
    std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
  }
  assert(ticks.size() >= 6);
  assert(callbacks.size() == ticks.size());
  for (std::size_t i = 0; i < ticks.size(); ++i) {
    assert(ticks[i].n == 0);
    assert(callbacks[i].tick == ticks[i].u32a);
    assert(callbacks[i].stamp == ticks[i].u32b);
    if (i > 0) {
      assert(ticks[i].u32a == ticks[i - 1].u32a + 1);  // one tick notice per tick
      assert(ticks[i].u32b > ticks[i - 1].u32b);       // Pico stamps increase
      assert(callbacks[i].now >= callbacks[i - 1].now);
    }
  }
  std::cout << "PASS ticks: callback and Tick events agree, monotonic tick/stamp/now\n";
}

void testSessionChangeReconnects() {
  EmulatorTransport pico;
  PicoLink link(pico);
  assert(pumpUntil(link, 200, [&] { return link.linkUp(); }));
  const std::uint32_t first_session = link.session();
  assert(first_session != 0);
  drain(link);

  pico.reboot();  // power cycle: new Pico session id
  const int lost_before = countKind(drain(link), PicoLink::Event::LinkLost);
  assert(lost_before == 0);
  assert(pumpUntil(link, 100, [&] { return !link.linkUp(); }));
  assert(link.session() == pico.session());
  assert(link.session() != first_session);
  assert(pumpUntil(link, 300, [&] { return link.linkUp(); }));

  const auto events = drain(link);
  assert(countKind(events, PicoLink::Event::LinkLost) == 1);
  assert(countKind(events, PicoLink::Event::Linked) == 1);
  std::cout << "PASS reconnect: new Pico session => LinkLost then Linked\n";
}

#ifndef __linux__
void testLinuxStubsThrow() {
  bool threw = false;
  try {
    mnky::link::LinuxSpidevTransport transport;
  } catch (const std::runtime_error&) {
    threw = true;
  }
  assert(threw);
  threw = false;
  try {
    mnky::link::ReadyGpio ready;
  } catch (const std::runtime_error&) {
    threw = true;
  }
  assert(threw);
  std::cout << "PASS portable stubs: spidev/GPIO constructors throw off Linux\n";
}
#endif

}  // namespace

int main() {
  testHandshake();
  testPacing();
  testMidiAndDedup();
  testButtons();
  testSendMidiCapture();
  testTempoTransportManual();
  testClockRouteSender();
  testClockSourceSender();
  testMidiRouteSender();
  testMidiRouteEncoding();
  testMidiRouteSend();
  testStatusRequest();
  testTxOverflowStatus();
  testRebootAndResetLink();
  testTickCallbackOrdering();
  testSessionChangeReconnects();
#ifndef __linux__
  testLinuxStubsThrow();
#endif
  std::cout << "PASS link core" << std::endl;
  return 0;
}
