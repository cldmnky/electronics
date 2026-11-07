// Host tests for P1-2 link-message reliability: per-frame ack/retry with the
// receiver's dedup (exactly-once delivery), Note Off protection across the
// 16-slot app queue, and the hang-note scenario. Built by `make host`.
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>

#include "link.hpp"

namespace {
using mnky::link::EmulatorTransport;
using mnky::link::ISpiTransport;
using mnky::link::PicoLink;

void pump(PicoLink& link, int transactions) {
  for (int i = 0; i < transactions; ++i) {
    link.poll();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

template <typename Pred>
bool pumpUntil(PicoLink& link, int max_transactions, Pred done) {
  for (int i = 0; i < max_transactions; ++i) {
    link.poll();
    if (done()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return false;
}

// Fault injector around the scripted Pico: drop one outbound app frame, or
// lose one per-frame ACK by corrupting the MISO response.
class FaultyTransport : public ISpiTransport {
 public:
  explicit FaultyTransport(EmulatorTransport& inner) : inner_(inner) {}
  bool transfer(const std::uint8_t out[32], std::uint8_t in[32]) override {
    mb::Bytes wire{};
    std::copy(out, out + mb::frame_size, wire.begin());
    mb::Frame f{};
    const bool control =
        mb::decode(wire, f) && f.type == mb::Type::control_event;
    if (control && drop_next_frame) {
      drop_next_frame = false;
      return false;  // aborted exchange: the Pi re-sends the same frame
    }
    if (!inner_.transfer(out, in)) return false;
    if (control && drop_next_ack) {
      drop_next_ack = false;
      std::fill(in, in + 32, 0);  // the Pico applied it, but the ack is lost
    }
    return true;
  }
  bool drop_next_frame = false;
  bool drop_next_ack = false;

 private:
  EmulatorTransport& inner_;
};

std::size_t countMidi(const EmulatorTransport& pico, std::uint8_t status,
                      std::uint8_t data1) {
  std::size_t n = 0;
  for (const auto& sent : pico.sentMidi()) {
    if (sent.size >= 2 && sent.data[0] == status && sent.data[1] == data1) ++n;
  }
  return n;
}

void test_dropped_frame_retried_once() {
  EmulatorTransport pico;
  FaultyTransport faulty(pico);
  PicoLink link(faulty);
  assert(pumpUntil(link, 300, [&] { return link.linkUp(); }));
  pump(link, 30);
  PicoLink::Event unused{};
  while (link.nextEvent(unused)) {
  }
  pico.clearSentMidi();

  const std::uint8_t note[3] = {0x90, 60, 100};
  faulty.drop_next_frame = true;
  assert(link.sendMidi(0, note, 3));
  pump(link, 60);
  assert(link.stats().bad >= 1);
  assert(countMidi(pico, 0x90, 60) == 1);  // re-sent, applied once
  std::printf("PASS reliability: aborted app transfer re-sent and delivered exactly once\n");
}

void test_lost_ack_retried_deduped() {
  EmulatorTransport pico;
  FaultyTransport faulty(pico);
  PicoLink link(faulty);
  assert(pumpUntil(link, 300, [&] { return link.linkUp(); }));
  pump(link, 30);
  PicoLink::Event unused{};
  while (link.nextEvent(unused)) {
  }
  pico.clearSentMidi();

  const std::uint8_t cc[3] = {0xB0, 7, 42};
  faulty.drop_next_ack = true;
  assert(link.sendMidi(0, cc, 3));
  pump(link, 80);
  assert(link.stats().retries >= 1);
  assert(link.stats().acks >= 1);
  assert(countMidi(pico, 0xB0, 7) == 1);  // replay deduped: exactly once
  std::printf("PASS reliability: lost ack -> same-sequence retry, dedup applies once\n");
}

void test_note_off_survives_overflow() {
  EmulatorTransport pico;
  PicoLink link(pico);
  assert(pumpUntil(link, 300, [&] { return link.linkUp(); }));
  pump(link, 30);
  PicoLink::Event unused{};
  while (link.nextEvent(unused)) {
  }
  pico.clearSentMidi();

  // Fill all 16 app slots without polling, then ask for the Note Off only a
  // hung-note-producing queue would drop.
  for (int i = 0; i < 16; ++i) {
    const std::uint8_t noteOn[3] = {0x90, static_cast<std::uint8_t>(40 + i), 100};
    assert(link.sendMidi(0, noteOn, 3));
  }
  const std::uint8_t noteOff[3] = {0x80, 40, 0};
  assert(link.sendMidi(0, noteOff, 3));
  assert(link.stats().evicted == 1);
  assert(link.stats().noteOffDrops == 0);

  pump(link, 160);
  assert(countMidi(pico, 0x80, 40) == 1);  // the Note Off reached the Pico
  std::printf("PASS reliability: Note Off kept across 16-slot overflow, no hung note\n");
}

void test_hang_note_eliminated() {
  EmulatorTransport pico;
  PicoLink link(pico);
  assert(pumpUntil(link, 300, [&] { return link.linkUp(); }));
  pump(link, 30);
  PicoLink::Event unused{};
  while (link.nextEvent(unused)) {
  }
  pico.clearSentMidi();

  const std::uint8_t noteOn[3] = {0x90, 64, 110};
  assert(link.sendMidi(1, noteOn, 3));
  pump(link, 40);
  assert(countMidi(pico, 0x90, 64) == 1);

  // A burst of 16 CCs arrives while the matching Note Off is queued. The Off
  // evicts a CC, not another Note Off, and still reaches the Pico.
  for (int i = 0; i < 16; ++i) {
    const std::uint8_t cc[3] = {0xB0, static_cast<std::uint8_t>(20 + i), 1};
    assert(link.sendMidi(1, cc, 3));
  }
  const std::uint8_t noteOff[3] = {0x80, 64, 0};
  assert(link.sendMidi(1, noteOff, 3));
  pump(link, 160);
  assert(countMidi(pico, 0x80, 64) == 1);
  std::printf("PASS reliability: note-on/CC burst cannot strand a queued Note Off\n");
}
}  // namespace

int main() {
  test_dropped_frame_retried_once();
  test_lost_ack_retried_deduped();
  test_note_off_survives_overflow();
  test_hang_note_eliminated();
  std::printf("link_reliability_test: all checks passed\n");
  return 0;
}
