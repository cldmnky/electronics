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
  testTickCallbackOrdering();
  testSessionChangeReconnects();
#ifndef __linux__
  testLinuxStubsThrow();
#endif
  std::cout << "PASS link core" << std::endl;
  return 0;
}
