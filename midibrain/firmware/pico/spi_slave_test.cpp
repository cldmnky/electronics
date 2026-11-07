// Host tests for the non-blocking SPI-slave engine (spi_slave.hpp) against the
// clock_sim SPI model semantics: mid-frame abort recovery, out-of-frame
// garbage, CRC rejection, byte-gap timeout, and the clock/UART path staying
// alive through every incident. Plain asserts, built by `make host`.
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>

#include "../../pi/emulator/clock_sim.hpp"
#include "clock_scheduler.hpp"
#include "midi_tx_scheduler.hpp"
#include "spi_slave.hpp"

namespace {
using mnky::emulator::clocksim::SpiBytes;
using mnky::emulator::clocksim::SpiFrame;
using mnky::emulator::clocksim::SpiLinkModel;
using mnky::emulator::clocksim::seconds;

mb::Bytes valid_frame(std::uint16_t sequence = 7) {
  mb::Frame f{};
  f.type = mb::Type::control_event;
  f.sequence = sequence;
  f.session = 0xabcdu;
  f.length = 3;
  f.payload[0] = 0x01;  // EV_MIDI_MSG-shaped payload; only the CRC matters here
  f.payload[1] = 0;
  f.payload[2] = 0x90;
  mb::Bytes wire{};
  assert(mb::encode(f, wire));
  return wire;
}

// One CSn transaction: 32 clocked bytes then CSn high.
void transfer(mb::SpiSlaveLink& link, const mb::Bytes& wire, std::uint32_t& t) {
  link.begin(t);
  for (std::uint8_t byte : wire) link.feed(byte, t++);
  link.end(t);
}

void short_transfer(mb::SpiSlaveLink& link, const mb::Bytes& wire,
                    std::size_t bytes, std::uint32_t& t) {
  link.begin(t);
  for (std::size_t i = 0; i < bytes; ++i) link.feed(wire[i], t++);
  link.end(t);  // CSn deasserted mid-frame
}

void test_midframe_abort_resyncs() {
  mb::SpiSlaveLink link;
  const mb::Bytes wire = valid_frame();
  std::uint32_t t = 0;
  short_transfer(link, wire, 17, t);
  assert(link.abortedFrames() == 1);
  assert(!link.frameReady());
  transfer(link, wire, t);
  mb::Bytes got{};
  assert(link.takeFrame(got) && got == wire);
  assert(link.completedFrames() == 1 && link.badFrames() == 0);

  // Byte-gap timeout: CSn stays low, the master stops clocking. The receiver
  // drops the partial frame after 100 us instead of waiting for the watchdog.
  link.begin(t);
  for (std::size_t i = 0; i < 10; ++i) link.feed(wire[i], t++);
  assert(!link.poll(t + 50));
  assert(link.poll(t + mb::SpiSlaveLink::kByteGapTimeoutUs));
  assert(link.abortedFrames() == 2);
  transfer(link, wire, t);
  assert(link.takeFrame(got) && got == wire);
  std::printf("PASS spi mid-frame abort + byte-gap timeout resync without reboot\n");
}

void test_out_of_frame_garbage_ignored() {
  mb::SpiSlaveLink link;
  mb::Bytes garbage{};
  garbage.fill(0x11);
  std::uint32_t t = 0;
  transfer(link, garbage, t);
  assert(!link.frameReady());
  assert(link.ignoredBytes() >= mb::frame_size);
  assert(link.completedFrames() == 0);

  // A plausible start with a wrong version is discarded the same way.
  mb::Bytes bad_header = garbage;
  bad_header[0] = 'M';
  bad_header[1] = 'B';
  bad_header[2] = 2;
  transfer(link, bad_header, t);
  assert(!link.frameReady());

  const mb::Bytes wire = valid_frame();
  transfer(link, wire, t);
  mb::Bytes got{};
  assert(link.takeFrame(got) && got == wire);
  std::printf("PASS spi out-of-frame garbage ignored until a valid header\n");
}

void test_crc_failure_dropped() {
  mb::SpiSlaveLink link;
  mb::Bytes corrupt = valid_frame();
  corrupt[20] ^= 0x40;
  std::uint32_t t = 0;
  transfer(link, corrupt, t);
  assert(!link.frameReady());
  assert(link.badFrames() == 1 && link.completedFrames() == 0);
  const mb::Bytes good = valid_frame();
  transfer(link, good, t);
  mb::Bytes got{};
  assert(link.takeFrame(got) && got == good);
  std::printf("PASS spi CRC failure dropped + counted, next frame accepted\n");
}

void test_clock_and_uart_never_see_spi_incident() {
  mb::SpiSlaveLink link;
  mb::MidiTxScheduler tx;
  mb::ClockScheduler clock;
  clock.reset(12000, 0);
  const std::uint64_t first_deadline = clock.nextDeadlineNs();

  const mb::Bytes wire = valid_frame();
  mb::Bytes corrupt = wire;
  corrupt[20] ^= 0x40;
  mb::Bytes garbage{};
  garbage.fill(0x55);
  std::uint32_t t = 0;
  std::uint32_t emitted = 0;
  for (int round = 0; round < 100; ++round) {
    // The clock alarm only enqueues; SPI incidents happen concurrently.
    for (int i = 0; i < 3; ++i) assert(tx.enqueueRealtime(0xF8));
    for (int i = 0; i < 3; ++i) {
      const int byte = tx.nextByte();
      assert(byte == 0xF8);
      ++emitted;
    }
    clock.advance();
    if (round % 3 == 0) short_transfer(link, wire, 9, t);
    if (round % 3 == 1) transfer(link, corrupt, t);
    if (round % 3 == 2) transfer(link, garbage, t);
  }
  assert(emitted == 300);
  assert(tx.droppedRealtime() == 0 && tx.droppedNormal() == 0 &&
         tx.rejectedNormal() == 0);
  assert(!tx.pending());
  // The grid advances from scheduled deadlines only; SPI work never re-phases.
  assert(clock.nextDeadlineNs() > first_deadline);
  std::printf("PASS spi incidents: 0 dropped F8, 0 rejected MIDI, clock grid intact\n");
}

void test_clock_sim_model_parity() {
  // Same incident sequence through the clock_sim SPI model (the test oracle):
  // short transfer then a full one recovers with no watchdog restart.
  const mb::Bytes wire = valid_frame();
  SpiBytes oracle_wire{};
  std::copy(wire.begin(), wire.end(), oracle_wire.begin());
  SpiFrame decoded{};
  SpiLinkModel model(0xabcdu);
  SpiBytes miso{};
  model.transferShort(oracle_wire, miso, 17, seconds(1));
  const bool model_recovered = model.transfer(oracle_wire, miso) &&
                               model.abortRecoveries() == 1 &&
                               model.watchdogRestarts() == 0;
  assert(model_recovered);
  assert(mnky::emulator::clocksim::spiDecode(oracle_wire, decoded));

  mb::SpiSlaveLink link;
  std::uint32_t t = 0;
  short_transfer(link, wire, 17, t);
  transfer(link, wire, t);
  mb::Bytes got{};
  assert(link.takeFrame(got) && got == wire);
  assert(link.abortedFrames() == 1 && link.badFrames() == 0);
  std::printf("PASS spi parity with clock_sim model: abort recovered, watchdog untouched\n");
}
}  // namespace

int main() {
  test_midframe_abort_resyncs();
  test_out_of_frame_garbage_ignored();
  test_crc_failure_dropped();
  test_clock_and_uart_never_see_spi_incident();
  test_clock_sim_model_parity();
  std::printf("spi_slave_test: all checks passed\n");
  return 0;
}
