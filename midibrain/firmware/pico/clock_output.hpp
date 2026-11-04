#pragma once
// Per-output clock division and delay for the Pico tick path. Pure logic: no
// Pico SDK, host-tested in protocol_test.cpp. main.cpp owns the UART writes.
//
// tempoDiv divides the 24-PPQN stream by dropping ticks: div 1 passes every
// tick, div 2 emits on even tick indexes (12 PPQN) and div 4 on every fourth
// tick (6 PPQN). The index is the global Pico tick count, which resets on
// START, so the divided phase also restarts with transport.
//
// syncDelayMs delays only 0xF8 clock bytes through a per-output FIFO. Transport
// bytes FA/FB/FC do not enter this line and keep their order among themselves,
// so a delayed clock byte may arrive after a transport byte. That is deliberate:
// transport timing stays authoritative while the clock phase is shifted to
// align late hardware.
#include <array>
#include <cstdint>

namespace mb {
inline constexpr std::uint16_t kClockDelayMaxMs = 500;
// 300 bpm is one tick every 8.33 ms, so 500 ms queues at most 61 ticks.
inline constexpr std::uint32_t kClockDelayDepth = 80;

// True when the tick at `tick_index` survives the divider. An invalid divider
// passes everything: the wire path clamps to 1/2/4, and a corrupt RAM value
// must never silence the clock.
inline bool clock_div_due(std::uint32_t tick_index, std::uint8_t div) {
  if (div <= 1 || (div != 2 && div != 4)) return true;
  return tick_index % div == 0;
}

// Fixed-capacity delay line for one physical TRS output. push() runs in the
// clock-alarm IRQ; popDue() runs in the main loop with interrupts masked by the
// caller. due[] stores only the deadline because every byte in the line is the
// same 0xF8 clock byte.
class ClockDelayLine {
 public:
  // A config change starts a fresh line (stale queued ticks would otherwise
  // arrive with the old delay). The value is clamped to 0..500 ms.
  void setDelayMs(std::uint16_t ms) {
    if (ms > kClockDelayMaxMs) ms = kClockDelayMaxMs;
    delay_us_ = static_cast<std::uint32_t>(ms) * 1000u;
    clear();
  }
  std::uint16_t delayMs() const {
    return static_cast<std::uint16_t>(delay_us_ / 1000u);
  }
  std::uint32_t delayUs() const { return delay_us_; }

  // Queues one clock byte to become due after delayUs(). Unsigned addition is
  // wrap-safe; a full line drops the newest byte and counts it.
  bool push(std::uint32_t now_us) {
    if (count_ >= kClockDelayDepth) {
      ++drops_;
      return false;
    }
    due_[tail_] = now_us + delay_us_;
    tail_ = (tail_ + 1) % kClockDelayDepth;
    ++count_;
    return true;
  }

  // Pops the oldest byte when its deadline has passed (wrap-safe signed
  // difference). Returns false while the head is still early or the line empty.
  bool popDue(std::uint32_t now_us) {
    if (count_ == 0) return false;
    if (static_cast<std::int32_t>(now_us - due_[head_]) < 0) return false;
    head_ = (head_ + 1) % kClockDelayDepth;
    --count_;
    return true;
  }

  void clear() {
    head_ = 0;
    tail_ = 0;
    count_ = 0;
  }
  std::uint32_t pending() const { return count_; }
  std::uint64_t drops() const { return drops_; }

 private:
  std::array<std::uint32_t, kClockDelayDepth> due_{};
  std::uint32_t head_ = 0, tail_ = 0, count_ = 0;
  std::uint32_t delay_us_ = 0;
  std::uint64_t drops_ = 0;
};

}  // namespace mb
