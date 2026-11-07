#pragma once
// External MIDI-clock follow policy for the Pico firmware. Pure logic: no Pico
// SDK. main.cpp feeds 0xF8 capture stamps from the selected TRS input together
// with the pending local deadline and drives the local tick alarm from the
// smoothed interval plus a bounded phase rebase; protocol_test.cpp and
// clock_follow_test.cpp run the same class on the host.
//
// INVARIANT: the Pico always sends clock. Follow disciplines the local
// generator; it never gates, mutes or stops it. The local grid is retuned by a
// proportional-integral loop, never re-anchored to an input edge: the phase
// correction is a bounded slew of the pending deadline, so arrival jitter is
// filtered and a recovered source never causes a time-skip on the TRS outputs.
// A lost source coasts at the last locked frequency. The per-output
// EV_CLOCK_ROUTE gates gate wires, never the generator. USB is not a clock
// source (the Pico has no USB-device view; forwarding 24-PPQN ticks over SPI
// would reintroduce the jitter this removes).
#include <cmath>
#include <cstdint>

#include "midi.hpp"

namespace mb {
// EV_CLOCK_SOURCE payload[1] source values (EV_CLOCK_SOURCE = 0x17).
constexpr std::uint8_t CLOCK_SOURCE_INTERNAL = 0;
constexpr std::uint8_t CLOCK_SOURCE_TRS1 = 1;
constexpr std::uint8_t CLOCK_SOURCE_TRS2 = 2;
// EV_CLOCK_SOURCE payload[2] transport-follow mask. Bit 2 is USB, which is
// Pi-side (forwarded as EV_TRANSPORT) and must never reach the Pico.
constexpr std::uint8_t CLOCK_FOLLOW_TRS1 = 0x01;
constexpr std::uint8_t CLOCK_FOLLOW_TRS2 = 0x02;
constexpr std::uint8_t CLOCK_FOLLOW_MASK = CLOCK_FOLLOW_TRS1 | CLOCK_FOLLOW_TRS2;

// Tempo window mirrors the firmware generator (kTempoMin/kTempoMax in main.cpp).
constexpr std::int32_t kClockFollowMinBpmX100 = 2000;
constexpr std::int32_t kClockFollowMaxBpmX100 = 30000;
// Smoothing factor: estimate += (interval - estimate) / kClockFollowEmaDiv.
constexpr int kClockFollowEmaDiv = 8;
// Phase-error low-pass for the integral path: filtered += (error - filtered) /
// kClockFollowPhaseDiv. The P term stays on the instantaneous error (bounded by
// the slew clamp) so a clean offset converges without the overshoot a lagging
// filter would add; this filter keeps the slow frequency trim from chasing
// arrival jitter.
constexpr int kClockFollowPhaseDiv = 8;
// P term: fraction of the phase error applied to the pending deadline each
// accepted tick. 0.25 removes 25 % of the error per tick, so a half-tick
// (10.4 ms at 120 bpm) is pulled in under three beats without overshoot.
constexpr double kClockFollowPhaseGain = 0.25;
// Bounded slew: the pending deadline never moves more than this per tick, so a
// phase correction can never look like a dropped/shifted tick on the wire.
constexpr std::int32_t kClockFollowMaxSlewUs = 200;
// I term: slow frequency trim in microseconds of period per microsecond of
// filtered phase error. The EMA already tracks tempo; the integral removes the
// residual bias and is clamped to 0.1 % so it can never itself be audible.
constexpr double kClockFollowIntegralGain = 1.0 / 128.0;
constexpr double kClockFollowMaxTrimFrac = 0.001;
// An interval outside [1/kOutlierHi, kOutlierHi] of the estimate is rejected
// for the estimate; the estimate only moves on in-window intervals (or on the
// first interval after a loss, which re-seeds it).
constexpr int kClockFollowOutlierHi = 2;
// No tick for more than this many expected intervals is a source loss: the
// generator coasts at the last estimate and the next real interval re-seeds.
constexpr int kClockFollowLossIntervals = 3;

// EV_TRANSPORT command for a received realtime byte: 0 stop, 1 start,
// 2 continue, -1 for anything else.
inline int transport_command_for_byte(std::uint8_t status) {
  if (status == 0xFA) return 1;
  if (status == 0xFB) return 2;
  if (status == 0xFC) return 0;
  return -1;
}

// EV_CLOCK_SOURCE layout: [1]=source (0 internal, 1 TRS IN1, 2 TRS IN2),
// [2]=transport-follow mask (bit0 TRS IN1, bit1 TRS IN2). Length 3. Source 3+
// and reserved mask bits are rejected so a bad frame is counted, not applied.
inline bool pack_clock_source(Frame& f, std::uint16_t seq, std::uint32_t session,
                              std::uint8_t source, std::uint8_t transport_mask) {
  if (source > CLOCK_SOURCE_TRS2 || (transport_mask & ~CLOCK_FOLLOW_MASK) != 0) return false;
  f = Frame{};
  f.type = Type::control_event;
  f.sequence = seq;
  f.session = session;
  f.length = 3;
  f.payload[0] = EV_CLOCK_SOURCE;
  f.payload[1] = source;
  f.payload[2] = transport_mask;
  return true;
}

// Follow state, interval estimator and phase-locked loop. Feed only capture
// stamps from the selected UART (main.cpp owns that selection); production
// must use the two-argument onTick() so the loop can measure phase against the
// pending generator deadline. The one-argument overload is the interval-only
// estimator used by the estimator unit tests. Feed transport decisions use
// followsTransport(). The estimate is in the caller's microsecond domain and
// handles 32-bit wraparound.
class ClockFollow {
 public:
  // Applies one EV_CLOCK_SOURCE frame. Returns false (state unchanged) for an
  // unknown source or a reserved mask bit. A source change restarts interval
  // acquisition; changing only the mask keeps the acquired estimate.
  bool apply(std::uint8_t source, std::uint8_t transport_mask) {
    if (source > CLOCK_SOURCE_TRS2 || (transport_mask & ~CLOCK_FOLLOW_MASK) != 0) return false;
    transport_mask_ = transport_mask;
    if (source != source_) {
      source_ = source;
      reset_acquisition();
    }
    return true;
  }

  std::uint8_t source() const { return source_; }
  bool following() const { return source_ != CLOCK_SOURCE_INTERNAL; }
  std::uint8_t transportMask() const { return transport_mask_; }
  bool followsTransport(std::uint8_t port) const {
    return port < 2 && ((transport_mask_ >> port) & 1u) != 0;
  }

  // One 0xF8 capture stamp from the selected input, interval-only (unit
  // tests and callers that do not own a generator deadline). True when the
  // interval was used for the estimate; false for the first tick, an outlier,
  // a loss gap or a non-followed source.
  bool onTick(std::uint32_t stamp_us) { return feed(stamp_us, 0, false); }

  // Production entry point: the capture stamp plus the local generator's
  // pending deadline (scheduler.nextDeadlineUs()). The signed difference is
  // the phase error, wrapped to +/- half a period; the loop filters it and
  // exposes a bounded per-tick deadline rebase through takePhaseNudgeUs().
  bool onTick(std::uint32_t stamp_us, std::uint32_t pending_us) {
    return feed(stamp_us, pending_us, true);
  }

  // Bounded phase rebase for the pending scheduler deadline, in microseconds.
  // Consumed exactly once per tick by main.cpp; the magnitude never exceeds
  // kClockFollowMaxSlewUs, so the output grid is slewed, never jumped.
  std::int32_t takePhaseNudgeUs() {
    const std::int32_t nudge = phase_nudge_us_;
    phase_nudge_us_ = 0;
    return nudge;
  }

  bool hasEstimate() const { return have_estimate_; }
  double intervalUs() const { return estimate_us_; }

  // Local alarm period in microseconds: the smoothed interval plus the slow
  // integral trim, rounded and clamped to the firmware tempo window. 0 when
  // no estimate has been acquired yet.
  std::uint32_t intervalUsRounded() const {
    if (!have_estimate_) return 0;
    double us = estimate_us_ + phase_integral_us_;
    const double max_bpm = static_cast<double>(kClockFollowMaxBpmX100) / 100.0;
    const double min_bpm = static_cast<double>(kClockFollowMinBpmX100) / 100.0;
    const double min_us = 60'000'000.0 / (max_bpm * 24.0);
    const double max_us = 60'000'000.0 / (min_bpm * 24.0);
    if (us < min_us) us = min_us;
    if (us > max_us) us = max_us;
    return static_cast<std::uint32_t>(us + 0.5);
  }

  // Tempo implied by the smoothed interval, rounded and clamped to 20..300 bpm
  // (x100). 0 when no estimate has been acquired yet; derived from the same
  // estimate that drives the generator.
  std::int32_t bpmX100() const {
    if (!have_estimate_) return 0;
    double bpm = 60'000'000.0 * 100.0 / (estimate_us_ * 24.0);
    if (bpm < static_cast<double>(kClockFollowMinBpmX100)) bpm = kClockFollowMinBpmX100;
    if (bpm > static_cast<double>(kClockFollowMaxBpmX100)) bpm = kClockFollowMaxBpmX100;
    return static_cast<std::int32_t>(bpm + 0.5);
  }

  // True when no tick has arrived for more than kClockFollowLossIntervals
  // expected intervals. The estimate is unchanged (coast); the alarm keeps
  // returning it, so downstream clock never disappears.
  bool coasting(std::uint32_t now_us) const {
    if (!have_estimate_) return false;
    const std::uint32_t since = now_us - last_stamp_us_;
    return static_cast<double>(since) > kClockFollowLossIntervals * estimate_us_;
  }

  std::uint64_t outliers() const { return outliers_; }
  std::uint64_t losses() const { return losses_; }
  std::uint64_t accepted() const { return accepted_; }

 private:
  bool feed(std::uint32_t stamp_us, std::uint32_t pending_us, bool phase) {
    if (!following()) return false;
    if (!have_last_) {
      have_last_ = true;
      last_stamp_us_ = stamp_us;
      return false;
    }
    const std::uint32_t delta = stamp_us - last_stamp_us_;  // wrap-safe
    last_stamp_us_ = stamp_us;
    bool used = false;
    if (!have_estimate_) {
      if (delta == 0) {
        ++outliers_;
      } else {
        estimate_us_ = static_cast<double>(delta);
        have_estimate_ = true;
        resume_ = false;
        ++accepted_;
        used = true;
      }
    } else {
      const double expected = estimate_us_;
      // Loss gap: longer than the loss window. It is not a tick interval;
      // keep the estimate (coast) and re-acquire from the next post-loss
      // interval.
      if (static_cast<double>(delta) > kClockFollowLossIntervals * expected) {
        if (resume_) {
          // A second large interval after a loss is the new tick spacing (it
          // may be >3x slower than the old tempo), not another gap: seed it so
          // a slow source can resume instead of being classified as loss
          // forever.
          estimate_us_ = static_cast<double>(delta);
          resume_ = false;
          ++accepted_;
          used = true;
        } else {
          ++losses_;
          resume_ = true;
        }
      } else if (resume_) {
        // First real interval after a loss seeds unconditionally, so a tempo
        // change during the outage is followed instead of rejected forever.
        if (delta == 0) {
          ++outliers_;
        } else {
          estimate_us_ = static_cast<double>(delta);
          resume_ = false;
          ++accepted_;
          used = true;
        }
      } else {
        const double low = expected / kClockFollowOutlierHi;
        const double high = expected * kClockFollowOutlierHi;
        if (delta == 0 || static_cast<double>(delta) < low ||
            static_cast<double>(delta) > high) {
          ++outliers_;
        } else {
          estimate_us_ += (static_cast<double>(delta) - expected) / kClockFollowEmaDiv;
          ++accepted_;
          used = true;
        }
      }
    }
    // Phase is measured on every real tick once an interval exists, including
    // the first tick after a loss: that tick is still a real grid point, and
    // correcting from it is what makes resync smooth. A rejected interval only
    // leaves the frequency estimate alone.
    if (phase && have_estimate_) update_phase(stamp_us, pending_us, delta);
    return used;
  }

  void update_phase(std::uint32_t stamp_us, std::uint32_t pending_us,
                    std::uint32_t delta_us) {
    const double period = estimate_us_;
    double error = static_cast<double>(static_cast<std::int32_t>(stamp_us - pending_us));
    while (error >= period * 0.5) error -= period;
    while (error < -period * 0.5) error += period;
    if (!have_phase_) {
      phase_filtered_us_ = error;
      have_phase_ = true;
    } else {
      phase_filtered_us_ += (error - phase_filtered_us_) / kClockFollowPhaseDiv;
    }
    phase_integral_us_ += kClockFollowIntegralGain * phase_filtered_us_;
    const double trim = period * kClockFollowMaxTrimFrac;
    if (phase_integral_us_ > trim) phase_integral_us_ = trim;
    if (phase_integral_us_ < -trim) phase_integral_us_ = -trim;
    // Rate-limit to one slew step per GENERATED tick, not per input edge:
    // jitter can put two input edges between two output ticks, and two full
    // nudges there would be a 2x slew step on the wire. The pending deadline
    // grows by about one period only when the alarm emitted a tick; a smaller
    // growth means the previous nudge is the only movement, so this edge must
    // not add another one. `delta_us` is unused for the bound (kept in the
    // signature for the interval context) but documents the jitter source.
    (void)delta_us;
    std::uint32_t pending_step = pending_us - last_pending_us_;
    if (!have_pending_ || static_cast<double>(pending_step) < period * 0.5) {
      pending_step = 0;
    }
    const double budget =
        (pending_step > 0 || !have_pending_) ? static_cast<double>(kClockFollowMaxSlewUs) : 0.0;
    last_pending_us_ = pending_us;
    have_pending_ = true;
    double nudge = kClockFollowPhaseGain * error;
    if (nudge > budget) nudge = budget;
    if (nudge < -budget) nudge = -budget;
    phase_nudge_us_ = static_cast<std::int32_t>(std::llround(nudge));
  }

  void reset_acquisition() {
    have_last_ = false;
    have_estimate_ = false;
    resume_ = false;
    last_stamp_us_ = 0;
    estimate_us_ = 0.0;
    have_phase_ = false;
    phase_filtered_us_ = 0.0;
    phase_integral_us_ = 0.0;
    have_pending_ = false;
    last_pending_us_ = 0;
    phase_nudge_us_ = 0;
  }

  std::uint8_t source_ = CLOCK_SOURCE_INTERNAL;
  std::uint8_t transport_mask_ = 0;
  bool have_last_ = false;
  bool have_estimate_ = false;
  bool resume_ = false;
  bool have_phase_ = false;
  std::uint32_t last_stamp_us_ = 0;
  double estimate_us_ = 0.0;
  double phase_filtered_us_ = 0.0;
  double phase_integral_us_ = 0.0;
  bool have_pending_ = false;
  std::uint32_t last_pending_us_ = 0;
  std::int32_t phase_nudge_us_ = 0;
  std::uint64_t outliers_ = 0, losses_ = 0, accepted_ = 0;
};

}  // namespace mb
