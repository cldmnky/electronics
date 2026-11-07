#pragma once
// Non-blocking SPI-slave transaction engine for the Pi link. Pure logic: no
// Pico SDK, host-tested in spi_slave_test.cpp.
//
// main.cpp calls begin() on the CSn falling edge, feed() from the SPI RX
// interrupt (one byte per clock) and end() on the CSn rising edge; the main
// loop calls poll() so a master that stops mid-frame (Linux aborted transfer,
// killed process, cable glitch) is detected by the byte-gap timeout instead of
// blocking until the 8 s watchdog. Nothing here waits for the master and the
// clock alarm/UART path never calls into it.
//
// Resynchronisation: the wire unit is one 32-byte frame per CSn assertion. A
// byte sequence that does not start with the "MB", version 1 header is ignored
// until an 'M' appears (out-of-frame garbage), a partial frame is discarded
// whole, and a complete frame that fails CRC16/format validation is dropped and
// counted. The next CSn assertion always starts a fresh frame, so no reboot is
// needed after a mid-frame abort.
#include <cstdint>

#include "protocol.hpp"

namespace mb {
class SpiSlaveLink {
 public:
  // 1 MHz SPI clocks one byte every 8 us; 100 us is >10 byte times, so a real
  // transaction never trips it while a killed transfer is detected promptly.
  static constexpr std::uint32_t kByteGapTimeoutUs = 100;

  // Response bytes are preloaded by the main loop and shifted out by the IRQ.
  // Between queued frames the last frame repeats (the Pi dedups it).
  void loadResponse(const Bytes& b) { response_ = b; }
  const Bytes& response() const { return response_; }
  std::uint8_t responseByte(std::uint32_t index) const {
    return response_[index % frame_size];
  }

  // CSn asserted. A still-open frame is an abort (the master left the
  // transaction without a rising edge we saw).
  void begin(std::uint32_t now_us) {
    if (receiving_ && count_ != 0) ++aborted_;
    receiving_ = true;
    count_ = 0;
    last_byte_us_ = now_us;
  }

  // One MOSI byte clocked by the master. Returns false when the byte was
  // ignored as out-of-frame garbage and true once a plausible header is open.
  bool feed(std::uint8_t byte, std::uint32_t now_us) {
    if (!receiving_) begin(now_us);
    last_byte_us_ = now_us;
    switch (count_) {
      case 0:
        if (byte != 'M') {
          ++ignored_bytes_;
          return false;
        }
        break;
      case 1:
        if (byte != 'B') return restart(byte);
        break;
      case 2:
        if (byte != 1) return restart(byte);
        break;
      default:
        break;
    }
    rx_[count_++] = byte;
    if (count_ == frame_size) finish();
    return true;
  }

  // CSn deasserted. A short transaction is discarded and counted; a full one
  // has already been validated by feed().
  void end(std::uint32_t /*now_us*/) {
    if (receiving_ && count_ != 0 && count_ != frame_size) ++aborted_;
    receiving_ = false;
    count_ = 0;
  }

  // Byte-gap timeout: CSn is still low but the master stopped clocking. Drops
  // the partial frame and resynchronises on the next begin().
  bool poll(std::uint32_t now_us) {
    if (!receiving_ || count_ == 0) return false;
    if (static_cast<std::uint32_t>(now_us - last_byte_us_) <
        kByteGapTimeoutUs)
      return false;
    ++aborted_;
    receiving_ = false;
    count_ = 0;
    return true;
  }

  bool frameReady() const { return ready_; }
  // Copies and clears the completed frame (one-slot mailbox).
  bool takeFrame(Bytes& out) {
    if (!ready_) return false;
    out = frame_;
    ready_ = false;
    return true;
  }

  std::uint64_t completedFrames() const { return completed_; }
  std::uint64_t abortedFrames() const { return aborted_; }
  std::uint64_t badFrames() const { return bad_; }
  std::uint64_t ignoredBytes() const { return ignored_bytes_; }

 private:
  // Header mismatch at byte 1/2: a fresh 'M' starts a new candidate frame,
  // anything else stays ignored. Bounded to the current transaction.
  bool restart(std::uint8_t byte) {
    count_ = 0;
    if (byte == 'M') {
      rx_[count_++] = byte;
      return true;
    }
    ++ignored_bytes_;
    return false;
  }

  void finish() {
    Frame decoded{};
    if (decode(rx_, decoded)) {
      frame_ = rx_;
      ready_ = true;
      ++completed_;
    } else {
      ++bad_;
    }
    receiving_ = false;
    count_ = 0;
  }

  Bytes response_{}, rx_{}, frame_{};
  std::uint32_t count_ = 0;
  std::uint32_t last_byte_us_ = 0;
  bool receiving_ = false, ready_ = false;
  std::uint64_t completed_ = 0, aborted_ = 0, bad_ = 0, ignored_bytes_ = 0;
};
}  // namespace mb
