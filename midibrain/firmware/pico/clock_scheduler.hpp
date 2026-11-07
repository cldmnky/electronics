#pragma once
// Deadline-based 24-PPQN clock grid for the Pico tick alarm. Pure integer
// logic: no Pico SDK, host-tested in protocol_test.cpp. main.cpp turns the
// absolute deadline into an absolute alarm; this class owns the phase grid.
//
// A tick at 120.00 bpm is 60 s / (120 * 24) = 20833.333... us, which no
// integer microsecond period represents. The grid therefore carries the
// period as an exact rational number of nanoseconds:
//
//   period = 60 s * 1e9 ns * 100 / (bpm_x100 * 24)
//          = 250'000'000'000 / bpm_x100 ns
//
// `phase_` keeps the division remainder between ticks and `deadline_ns_` is an
// absolute point on that grid. Every next deadline is advanced from the
// PREVIOUS SCHEDULED deadline, never from "now" or from an alarm callback's
// return. ISR entry latency and callback execution time therefore do not
// accumulate as tempo drift; they only delay one individual edge.
//
// On a missed deadline the grid steps forward over whole ticks (bounded) so a
// stalled main loop cannot produce a catch-up burst faster than 24 PPQN. The
// grid phase is unchanged: skipped ticks are dropped, not moved.
#include <cstdint>

namespace mb {
inline constexpr std::uint64_t kClockTickNumeratorNs = 250'000'000'000ull;
inline constexpr std::int32_t kClockSchedulerMinBpmX100 = 2000;
inline constexpr std::int32_t kClockSchedulerMaxBpmX100 = 30000;
// Longer than any watchdog-bounded stall at the fastest tick (8.33 ms): a
// stall that would skip more ticks than this is already a reboot-class fault.
inline constexpr std::uint32_t kClockSchedulerMaxSkip = 4096;

class ClockScheduler {
 public:
  // Anchors the grid at `now_us`; the first tick is one period later. The
  // fractional part of that first period is carried into the next deadline.
  void reset(std::int32_t bpm_x100, std::uint64_t now_us) {
    setPeriodNumerator(kClockTickNumeratorNs, clampBpm(bpm_x100));
    deadline_ns_ = now_us * 1000ull + period_num_ns_ / period_den_;
    phase_ = period_num_ns_ % period_den_;
  }

  // Retunes from the pending deadline onward. The pending deadline does not
  // move, so a tempo change never re-phases or drops the already-scheduled
  // tick; the next interval uses the new period and starts a fresh exact
  // rational accumulation from that grid point.
  void setTempoX100(std::int32_t bpm_x100) {
    const std::int32_t bpm = clampBpm(bpm_x100);
    const std::uint64_t num = kClockTickNumeratorNs;
    const std::uint32_t den = static_cast<std::uint32_t>(bpm);
    if (num == period_num_ns_ && den == period_den_) return;
    phase_ = 0;
    setPeriodNumerator(num, bpm);
  }

  // Period override for external clock follow (integer nanoseconds). The
  // followed estimate enters the same absolute grid, so retuning cannot
  // reintroduce per-tick rounding drift. A zero period is ignored.
  void setPeriodNs(std::uint64_t period_ns) {
    if (period_ns == 0) return;
    if (period_ns == period_num_ns_ && period_den_ == 1) return;
    phase_ = 0;
    period_num_ns_ = period_ns;
    period_den_ = 1;
  }

  // Phase rebase for external clock follow: moves the pending deadline (and
  // therefore the absolute grid anchor) by a bounded delta without changing
  // the period or its rational fractional carry. main.cpp applies the PLL's
  // per-tick nudge here, so follow corrects phase on the existing deadline
  // path instead of re-anchoring the grid to an input edge. The caller bounds
  // |delta| well below one period; an already-armed alarm is not moved (the
  // correction shows up on the following tick), which keeps the output slew
  // continuous. Returns the moved deadline.
  std::uint64_t rebasePendingNs(std::int64_t delta_ns) {
    if (delta_ns < 0 && static_cast<std::uint64_t>(-delta_ns) > deadline_ns_) {
      deadline_ns_ = 0;
      return deadline_ns_;
    }
    deadline_ns_ = static_cast<std::uint64_t>(static_cast<std::int64_t>(deadline_ns_) +
                                              delta_ns);
    return deadline_ns_;
  }

  // Absolute deadline of the next tick in nanoseconds.
  std::uint64_t nextDeadlineNs() const { return deadline_ns_; }

  // Absolute microsecond deadline for the 1 MHz alarm API. Rounded UP so a
  // tick is never emitted before its grid point; the fractional part stays in
  // the nanosecond grid and cannot accumulate.
  std::uint64_t nextDeadlineUs() const {
    return (deadline_ns_ + 999ull) / 1000ull;
  }

  // Nominal period rounded to whole microseconds (diagnostics only; the grid
  // itself uses the exact rational period).
  std::uint32_t intervalUsRounded() const {
    return static_cast<std::uint32_t>((period_num_ns_ / period_den_ + 500ull) /
                                      1000ull);
  }

  // Moves to the next grid point from the previous scheduled deadline and
  // returns it in nanoseconds. No wall clock is consulted.
  std::uint64_t advance() {
    deadline_ns_ += period_num_ns_ / period_den_;
    phase_ += period_num_ns_ % period_den_;
    if (phase_ >= period_den_) {
      ++deadline_ns_;
      phase_ -= period_den_;
    }
    return deadline_ns_;
  }

  // After a missed deadline, skips whole grid points until the next deadline
  // is strictly after `now_us`. Missed ticks are dropped rather than emitted
  // in a burst; the absolute grid does not move. Returns the number skipped.
  std::uint32_t skipToAfter(std::uint64_t now_us) {
    std::uint32_t skipped = 0;
    while (nextDeadlineUs() < now_us && skipped < kClockSchedulerMaxSkip) {
      advance();
      ++skipped;
    }
    return skipped;
  }

 private:
  static std::int32_t clampBpm(std::int32_t bpm_x100) {
    if (bpm_x100 < kClockSchedulerMinBpmX100) return kClockSchedulerMinBpmX100;
    if (bpm_x100 > kClockSchedulerMaxBpmX100) return kClockSchedulerMaxBpmX100;
    return bpm_x100;
  }
  void setPeriodNumerator(std::uint64_t num, std::int32_t bpm_x100) {
    period_num_ns_ = num;
    period_den_ = static_cast<std::uint32_t>(bpm_x100);
  }

  std::uint64_t deadline_ns_ = 0;
  std::uint64_t period_num_ns_ = kClockTickNumeratorNs;
  std::uint32_t period_den_ = 12000;  // bpm_x100, never zero
  std::uint64_t phase_ = 0;           // fractional-ns numerator, < period_den_
};

}  // namespace mb
