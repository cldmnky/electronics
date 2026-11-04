#pragma once
// External MIDI-clock follow policy for the Pico firmware. Pure logic: no Pico
// SDK. main.cpp feeds 0xF8 capture stamps from the selected TRS input and
// drives the local tick alarm from the smoothed interval; protocol_test.cpp
// runs the same class on the host.
//
// INVARIANT: the Pico always sends clock. Follow only retunes the local
// generator (frequency); it never gates, mutes or stops it. The local phase
// stays free-running and continuous (never re-phased by input arrival jitter).
// A lost source coasts at the last estimate, and the per-output EV_CLOCK_ROUTE
// gates gate wires, never the generator. The generator never mirrors input
// ticks: local alarm spacing is the smoothed estimate, which removes arrival
// jitter. USB is not a clock source (the Pico has no USB-device view;
// forwarding 24-PPQN ticks over SPI would reintroduce the jitter this removes).
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

// Follow state and interval estimator. Feed only capture stamps from the
// selected UART (main.cpp owns that selection); feed transport decisions use
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

  // One 0xF8 capture stamp from the selected input. True when the interval was
  // used for the estimate; false for the first tick, an outlier, a loss gap or
  // a non-followed source.
  bool onTick(std::uint32_t stamp_us) {
    if (!following()) return false;
    if (!have_last_) {
      have_last_ = true;
      last_stamp_us_ = stamp_us;
      return false;
    }
    const std::uint32_t delta = stamp_us - last_stamp_us_;  // wrap-safe
    last_stamp_us_ = stamp_us;
    if (!have_estimate_) {
      if (delta == 0) {
        ++outliers_;
        return false;
      }
      estimate_us_ = static_cast<double>(delta);
      have_estimate_ = true;
      resume_ = false;
      ++accepted_;
      return true;
    }
    const double expected = estimate_us_;
    // Loss gap: longer than the loss window. It is not a tick interval; keep
    // the estimate (coast) and re-acquire from the next post-loss interval.
    if (static_cast<double>(delta) > kClockFollowLossIntervals * expected) {
      if (resume_) {
        // A second large interval after a loss is the new tick spacing (it may
        // be >3x slower than the old tempo), not another gap: seed it so a
        // slow source can resume instead of being classified as loss forever.
        estimate_us_ = static_cast<double>(delta);
        resume_ = false;
        ++accepted_;
        return true;
      }
      ++losses_;
      resume_ = true;
      return false;
    }
    if (resume_) {
      // First real interval after a loss seeds unconditionally, so a tempo
      // change during the outage is followed instead of rejected forever.
      if (delta == 0) {
        ++outliers_;
        return false;
      }
      estimate_us_ = static_cast<double>(delta);
      resume_ = false;
      ++accepted_;
      return true;
    }
    const double low = expected / kClockFollowOutlierHi;
    const double high = expected * kClockFollowOutlierHi;
    if (delta == 0 || static_cast<double>(delta) < low || static_cast<double>(delta) > high) {
      ++outliers_;
      return false;
    }
    estimate_us_ += (static_cast<double>(delta) - expected) / kClockFollowEmaDiv;
    ++accepted_;
    return true;
  }

  bool hasEstimate() const { return have_estimate_; }
  double intervalUs() const { return estimate_us_; }

  // Local alarm period in microseconds, rounded and clamped to the firmware
  // tempo window. 0 when no estimate has been acquired yet.
  std::uint32_t intervalUsRounded() const {
    if (!have_estimate_) return 0;
    double us = estimate_us_;
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
  void reset_acquisition() {
    have_last_ = false;
    have_estimate_ = false;
    resume_ = false;
    last_stamp_us_ = 0;
    estimate_us_ = 0.0;
  }

  std::uint8_t source_ = CLOCK_SOURCE_INTERNAL;
  std::uint8_t transport_mask_ = 0;
  bool have_last_ = false;
  bool have_estimate_ = false;
  bool resume_ = false;
  std::uint32_t last_stamp_us_ = 0;
  double estimate_us_ = 0.0;
  std::uint64_t outliers_ = 0, losses_ = 0, accepted_ = 0;
};

}  // namespace mb
