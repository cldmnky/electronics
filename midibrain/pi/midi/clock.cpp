#include "clock.hpp"

#include <cmath>

namespace mnky::midi {

void UsbClock::setTempo(std::uint16_t bpmX100) {
  if (bpmX100 < kMinBpmX100) bpmX100 = kMinBpmX100;
  if (bpmX100 > kMaxBpmX100) bpmX100 = kMaxBpmX100;
  bpmX100_ = bpmX100;
}

double UsbClock::intervalUs() const {
  return 6.0e9 / (static_cast<double>(bpmX100_) * kTicksPerQuarter);
}

void UsbClock::setRolling(bool rolling) {
  if (rolling_ == rolling) return;
  rolling_ = rolling;
  // One interval after the last tick is where the next tick must land, so the
  // first mirrored byte does not wait for a fresh tick after START.
  if (rolling_ && haveTick_) nextDueUs_ = mappedUs_ + intervalUs();
}

void UsbClock::addTick(std::uint32_t tick, std::uint32_t picoStampUs, std::uint64_t piNowUs) {
  const double delay = static_cast<double>(piNowUs) - static_cast<double>(picoStampUs);
  if (haveTick_ && static_cast<std::int32_t>(tick - lastTick_) <= 0) {
    ++reordered_;  // duplicate or stale: never let the phase run backwards
    return;
  }
  const bool first = !haveTick_;
  if (first) {
    haveTick_ = true;
    offsetUs_ = delay;  // no second sample yet; later ticks find the floor
    latencyUs_ = 0.0;
  } else {
    if (delay < offsetUs_) {
      offsetUs_ = delay;  // fastest delivery so far: the best available floor
    } else {
      offsetUs_ += kOffsetRise * (delay - offsetUs_);  // track clock drift
    }
    latencyUs_ += kLatencyGain * ((delay - offsetUs_) - latencyUs_);
  }
  lastTick_ = tick;
  mappedUs_ = static_cast<double>(picoStampUs) + offsetUs_;

  if (!rolling_ || first) {
    nextDueUs_ = mappedUs_;  // nothing is emitting yet: peg the phase hard
    return;
  }
  const double limit = intervalUs() * 0.5;
  double err = mappedUs_ - nextDueUs_;
  if (err > limit) err = limit;
  if (err < -limit) err = -limit;
  nextDueUs_ += kPhaseSlew * err;
}

std::optional<std::uint64_t> UsbClock::nextDueUs(std::uint64_t piNowUs) const {
  if (!rolling_ || !haveTick_) return std::nullopt;
  const double now = static_cast<double>(piNowUs);
  if (nextDueUs_ + intervalUs() <= now) return piNowUs;  // stale phase: due now
  if (nextDueUs_ <= now) return piNowUs;                 // due now
  return static_cast<std::uint64_t>(std::floor(nextDueUs_ + 0.5));
}

bool UsbClock::popDue(std::uint64_t piNowUs) {
  if (!rolling_ || !haveTick_) return false;
  const double now = static_cast<double>(piNowUs);
  if (nextDueUs_ > now) return false;
  const double interval = intervalUs();
  const double lateness = now - nextDueUs_;
  if (lateness >= interval) {
    // The caller was blocked for at least one tick: re-peg instead of firing
    // the missed bytes back to back.
    skipped_ += static_cast<std::uint64_t>(lateness / interval);
    nextDueUs_ = now;
  }
  nextDueUs_ += interval;
  return true;
}

std::uint64_t UsbClock::mapStamp(std::uint32_t picoStampUs) const {
  const double mapped = static_cast<double>(picoStampUs) + offsetUs_;
  return static_cast<std::uint64_t>(std::floor(mapped + 0.5));
}

}  // namespace mnky::midi
