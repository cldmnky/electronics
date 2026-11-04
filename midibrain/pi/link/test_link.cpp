// Host tests for the JUCE-free Pico link core. Asserts only (no framework):
// builds with the plain `c++ -std=c++20 -Wall -Wextra -Werror` line from
// pi/ARCHITECTURE.md and prints one PASS line per behaviour.
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
