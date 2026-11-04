#pragma once
// USB clock discipline for the mnkybox Pi core. Pure logic: no timers, no
// clock source, no RtMidi - the caller feeds Pico ticks and Pi times and asks
// which clock bytes are due (docs/protocol.md, "USB clock mirror").
//
// The Pico is the single clock master. The Pi does not mirror arrivals: each
// EV_CLOCK_TICK carries the Pico capture stamp, which is authoritative, and
// the offset maps that stamp onto the Pi timeline. The offset EMA follows the
// *fastest* recent delivery (its lower envelope) and creeps up slowly so link
// and Linux scheduling latency can never pull the mapping late; latencyUs()
// reports the remaining excess over that floor for diagnostics. Scheduling
// then advances a local phase by one tick interval per emitted byte, slewing a
// fraction of the observed error per tick so a dropped tick costs continuity,
// not phase.
//
// Per-output gates mirror EV_CLOCK_ROUTE (docs/protocol.md): clock bytes are
// emitted only while the clock gate is on, and the mirror's START/CONTINUE/
// STOP bytes only while the transport gate is on. Both default on.
#include <cstdint>
#include <optional>

namespace mnky::midi {

constexpr int kTicksPerQuarter = 24;
constexpr std::uint8_t kClockByte = 0xF8;
constexpr std::uint8_t kStartByte = 0xFA;
constexpr std::uint8_t kContinueByte = 0xFB;
constexpr std::uint8_t kStopByte = 0xFC;

class UsbClock {
 public:
  static constexpr std::uint16_t kMinBpmX100 = 2000;    // 20.00 bpm
  static constexpr std::uint16_t kMaxBpmX100 = 30000;   // 300.00 bpm
  static constexpr std::uint16_t kDefaultBpmX100 = 12000;  // 120.00 bpm

  // Clamped to kMinBpmX100..kMaxBpmX100. Takes effect on the next tick
  // interval; the phase itself is only corrected by incoming ticks.
  void setTempo(std::uint16_t bpmX100);
  std::uint16_t tempo() const { return bpmX100_; }
  double intervalUs() const;

  // Per-output mirror gates (EV_CLOCK_ROUTE semantics). Both default on. A
  // closed clock gate suppresses scheduled clock bytes without discarding the
  // phase; intervals skipped while gated are counted when the gate re-opens.
  void setClockEnabled(bool enabled);
  bool clockEnabled() const { return clockEnabled_; }
  void setTransportEnabled(bool enabled);
  bool transportEnabled() const { return transportEnabled_; }
  // True when the USB mirror may emit `byte` under the current gates: 0xF8
  // needs the clock gate, START/CONTINUE/STOP need the transport gate, and
  // every other byte is not mirror-generated and passes unchanged.
  bool mirrorAllows(std::uint8_t byte) const;

  // USB mirror emits clock bytes only while rolling. Ticks keep arriving while
  // stopped and keep the offset warm; entering roll re-pegs the phase one
  // interval after the last tick.
  void setRolling(bool rolling);
  bool rolling() const { return rolling_; }

  // One EV_CLOCK_TICK. `tick` is the Pico tick count (wrap-safe), `picoStampUs`
  // its capture stamp in the Pico domain, `piNowUs` the receipt time. Stale or
  // duplicate tick numbers are counted in reordered() and change nothing:
  // the phase never moves backwards.
  void addTick(std::uint32_t tick, std::uint32_t picoStampUs, std::uint64_t piNowUs);

  // Pi time of the next clock byte, or nullopt while stopped / before the first
  // tick. A phase more than one interval behind piNowUs is reported as due at
  // piNowUs so a stalled caller emits one byte instead of a catch-up burst.
  std::optional<std::uint64_t> nextDueUs(std::uint64_t piNowUs) const;

  // True when a clock byte is due at or before piNowUs; advances the phase by
  // one tick interval. Intervals missed while the caller was blocked are
  // skipped and counted in skipped().
  bool popDue(std::uint64_t piNowUs);

  // Diagnostics and timestamp mapping.
  double offsetUs() const { return offsetUs_; }    // pi_time - pico_stamp
  double latencyUs() const { return latencyUs_; }  // EMA excess over that floor
  bool primed() const { return haveTick_; }
  std::uint32_t lastTick() const { return lastTick_; }
  std::uint64_t skipped() const { return skipped_; }
  std::uint64_t reordered() const { return reordered_; }
  std::uint64_t mapStamp(std::uint32_t picoStampUs) const;

 private:
  // Offset rises at 0.2% of the observed gap per tick and snaps down to any
  // faster delivery, so it follows ~100 ppm clock drift while staying near the
  // fastest delivery; latency and phase use larger gains because they must
  // follow the Pico within a few ticks.
  static constexpr double kOffsetRise = 0.002;
  static constexpr double kLatencyGain = 0.25;
  static constexpr double kPhaseSlew = 0.25;

  std::uint16_t bpmX100_ = kDefaultBpmX100;
  bool rolling_ = false;
  bool clockEnabled_ = true;
  bool transportEnabled_ = true;
  bool haveTick_ = false;
  std::uint32_t lastTick_ = 0;
  double mappedUs_ = 0.0;   // Pi time of the last accepted tick
  double offsetUs_ = 0.0;
  double latencyUs_ = 0.0;
  double nextDueUs_ = 0.0;  // phase: Pi time of the next clock byte
  std::uint64_t skipped_ = 0;
  std::uint64_t reordered_ = 0;
};

}  // namespace mnky::midi
