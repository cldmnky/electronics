// Host scenarios for the firmware phase-locked clock follow (clock_follow.hpp)
// against the deterministic external-clock model in pi/emulator/clock_sim.
// Plain asserts, built by `make host`. The harness models the source's arrival
// jitter, wander, dropout and tempo step; this adapter runs the real firmware
// ClockFollow + bounded pending-deadline rebase and records the generated grid.
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "../../pi/emulator/clock_sim.hpp"
#include "clock_follow.hpp"

namespace {
using namespace mnky::emulator::clocksim;

constexpr Ps kUnitsPerUs = 9'000;
constexpr double kTicksPerBeat = 24.0;

double toMs(Ps units) { return static_cast<double>(units) / 9'000'000.0; }

Ps nominalPeriod() { return ClockRate::forTempo(12000).periodAt(0); }

// Runs the firmware policy as the target of the emulator's ExternalClock seam:
// emit the holdover/free-running grid up to the capture instant, feed the real
// capture stamp plus the pending deadline into ClockFollow, then apply the
// one-shot bounded phase nudge to that pending deadline and retune the period.
class FirmwarePllGen final : public seam::ExternalClock {
 public:
  FirmwarePllGen(mb::ClockFollow& follow, Ps firstDeadline)
      : follow_(follow), period_(nominalPeriod()), next_(firstDeadline) {}

  void onPulse(Ps capture, Ps phaseReference) override {
    (void)phaseReference;  // firmware has only the capture stamp; jitter is filtered
    emitUntil(capture);
    follow_.onTick(static_cast<std::uint32_t>(capture / kUnitsPerUs),
                   static_cast<std::uint32_t>(next_ / kUnitsPerUs));
    next_ += static_cast<Ps>(follow_.takePhaseNudgeUs()) * kUnitsPerUs;
    const std::uint32_t period = follow_.intervalUsRounded();
    if (period != 0) period_ = static_cast<Ps>(period) * kUnitsPerUs;
  }

  void flushTo(Ps time) { emitUntil(time); }
  const std::vector<Ps>& generated() const { return generated_; }

 private:
  void emitUntil(Ps time) {
    while (next_ <= time) {
      generated_.push_back(next_);
      next_ += period_;
    }
  }

  mb::ClockFollow& follow_;
  Ps period_;
  Ps next_;
  std::vector<Ps> generated_;
};

struct Run {
  std::vector<Ps> generated;
  std::vector<Ps> ideal;  // source.idealPulse(k + 1) for generated[k]
};

Run runScenario(const ExternalClockConfig& config, Ps initialOffset, Ps duration) {
  Run run;
  Scheduler scheduler(0);
  mb::ClockFollow follow;
  assert(follow.apply(mb::CLOCK_SOURCE_TRS1, 0));
  FirmwarePllGen gen(follow, nominalPeriod() + initialOffset);
  ExternalClockSource source(scheduler, config, gen);
  source.start(0);
  scheduler.runUntil(duration);
  gen.flushTo(duration);
  run.generated = gen.generated();
  run.ideal.reserve(run.generated.size());
  for (std::size_t k = 0; k < run.generated.size(); ++k) {
    run.ideal.push_back(source.idealPulse(static_cast<std::uint64_t>(k + 1)));
  }
  return run;
}

// First index of 24 consecutive generated ticks within `toleranceMs` of the
// jitter-free source grid, expressed in beats (24 ticks). generated.size()
// when it never locks.
double lockBeats(const Run& run, double toleranceMs = 1.0, Ps fromTime = 0) {
  std::size_t stable = 0;
  for (std::size_t k = 0; k < run.generated.size(); ++k) {
    if (run.generated[k] < fromTime) {
      stable = 0;
      continue;
    }
    const bool within = std::abs(toMs(run.generated[k] - run.ideal[k])) <= toleranceMs;
    stable = within ? stable + 1 : 0;
    if (stable >= 24) {
      const std::size_t lock = k + 1 - 24;
      return toMs(run.generated[lock] - fromTime) /
             (toMs(nominalPeriod()) * kTicksPerBeat);
    }
  }
  return static_cast<double>(run.generated.size()) / kTicksPerBeat;
}

double maxPhaseMs(const Run& run, std::size_t from) {
  double worst = 0.0;
  for (std::size_t k = from; k < run.generated.size(); ++k) {
    worst = std::max(worst, std::abs(toMs(run.generated[k] - run.ideal[k])));
  }
  return worst;
}

// Tempo from the generated span after `from`, in % of 120 bpm.
double tempoErrorPct(const Run& run, std::size_t from) {
  if (run.generated.size() < from + 2) return 0.0;
  const double spanS =
      static_cast<double>(run.generated.back() - run.generated[from]) / 9'000'000'000.0;
  const double bpm =
      static_cast<double>(run.generated.size() - 1 - from) * 60.0 / (spanS * kTicksPerBeat);
  return std::abs(bpm - 120.0) / 120.0 * 100.0;
}

// Largest deviation of one generated interval from nominal (one-tick wobble).
double maxIntervalStepMs(const Run& run, std::size_t from = 1) {
  double worst = 0.0;
  for (std::size_t k = from; k < run.generated.size(); ++k) {
    worst = std::max(worst,
                     std::abs(toMs(run.generated[k] - run.generated[k - 1]) -
                              toMs(nominalPeriod())));
  }
  return worst;
}

}  // namespace

int main() {
  const Ps period = nominalPeriod();

  // (a)+(b) Free-run starts 8 ms behind a clean 120 bpm source: the case the
  // phase term exists for. Report lock time in beats and the steady-state
  // residual.
  {
    ExternalClockConfig clean;
    clean.duration = seconds(6);
    const Run run = runScenario(clean, ms(8), clean.duration);
    const double beats = lockBeats(run);
    const std::size_t lockIndex = static_cast<std::size_t>(beats * kTicksPerBeat);
    // Steady state is measured after one more beat of settling (the 8 ms
    // transient is the lock itself, not the residual).
    const std::size_t steadyFrom =
        std::max(lockIndex + static_cast<std::size_t>(kTicksPerBeat), run.generated.size() / 2);
    const double residualMs = maxPhaseMs(run, steadyFrom);
    const double tempoPct = tempoErrorPct(run, steadyFrom);
    const bool pass = beats <= 3.0 && residualMs <= 0.5 && tempoPct <= 0.1;
    std::printf(
        "%s clock.follow-lock: 120 bpm clean, 8 ms initial offset: lock in %.2f beats, "
        "steady residual %.3f ms, tempo error %.4f%%\n",
        pass ? "PASS" : "FAIL", beats, residualMs, tempoPct);
    assert(pass);
  }

  // (c) +/-1 ms and +/-2 ms uniform arrival jitter: tempo stays inside 0.1 %,
  // filtered phase stays bounded and each interval moves at most the slew
  // limit (no tempo wobble).
  for (const std::int64_t jitterMs : {1, 2}) {
    ExternalClockConfig jitter;
    jitter.jitterPeak = ms(jitterMs);
    jitter.duration = seconds(12);
    const Run run = runScenario(jitter, 0, jitter.duration);
    const std::size_t afterLock = static_cast<std::size_t>(4.0 * kTicksPerBeat);
    const double phaseMs = maxPhaseMs(run, afterLock);
    const double tempoPct = tempoErrorPct(run, afterLock);
    const double slewMs = maxIntervalStepMs(run, afterLock);
    // Each input edge is slew-limited to 200 us. Jitter can cluster up to
    // three input edges between two generated ticks, so one wire interval may
    // carry up to three slew steps; the average tempo and phase stay bounded.
    const double slewLimitMs = 3.0 * static_cast<double>(mb::kClockFollowMaxSlewUs) / 1000.0;
    const bool pass = phaseMs <= 2.0 + static_cast<double>(jitterMs) &&
                      tempoPct <= 0.1 && slewMs <= slewLimitMs + 0.05;
    std::printf(
        "%s clock.follow-jitter +/-%lldms: tempo error %.4f%%, max phase %.3f ms, "
        "max interval step %.3f ms (3 x %.3f ms per-edge slew)\n",
        pass ? "PASS" : "FAIL", static_cast<long long>(jitterMs), tempoPct, phaseMs, slewMs,
        static_cast<double>(mb::kClockFollowMaxSlewUs) / 1000.0);
    assert(pass);
  }

  // (d) Slow +/-1.5 bpm wander over 8 s: the loop tracks tempo and keeps phase
  // bounded instead of accumulating the interval-estimate lag.
  {
    ExternalClockConfig wander;
    wander.jitterPeak = ms(1) / 2;
    wander.wanderBpm = 1.5;
    wander.wanderPeriod = seconds(8);
    wander.duration = seconds(12);
    const Run run = runScenario(wander, 0, wander.duration);
    const std::size_t afterSettle = static_cast<std::size_t>(2.0 * kTicksPerBeat);
    const double phaseMs = maxPhaseMs(run, afterSettle);
    const bool pass = phaseMs <= 3.0;
    std::printf("%s clock.follow-wander: +/-1.5 bpm over 8 s: max phase %.3f ms (<= 3 ms)\n",
                pass ? "PASS" : "FAIL", phaseMs);
    assert(pass);
  }

  // (e)+(f) 550 ms hard dropout: holdover keeps generating at the last locked
  // frequency with no gap, then a 121 bpm source returns. Measure holdover
  // error, resync time and the largest interval step (no time-skip).
  {
    ExternalClockConfig dropout;
    dropout.duration = seconds(8);
    dropout.dropoutStart = seconds(3);
    dropout.dropoutEnd = seconds(3) + ms(550);
    dropout.stepBpmX100 = 12100;
    dropout.stepAt = seconds(4);
    const Run run = runScenario(dropout, 0, dropout.duration);

    std::uint64_t duringDropout = 0;
    double holdoverPct = 0.0;
    bool gap = false;
    for (std::size_t k = 1; k < run.generated.size(); ++k) {
      const Ps at = run.generated[k];
      if (at > dropout.dropoutStart && at < dropout.dropoutEnd) {
        ++duringDropout;
        holdoverPct = std::abs(toMs(run.generated[k] - run.generated[k - 1]) -
                               toMs(period)) /
                      toMs(period) * 100.0;
      }
      if (run.generated[k] - run.generated[k - 1] > period * 3 / 2) gap = true;
    }
    const bool holdoverPass = duringDropout >= 8 && !gap && holdoverPct <= 0.1;
    std::printf(
        "%s clock.follow-holdover: 550 ms dropout: %llu pulses generated, max gap %s, "
        "holdover tempo error %.4f%%\n",
        holdoverPass ? "PASS" : "FAIL", static_cast<unsigned long long>(duringDropout),
        gap ? "> 1.5x nominal" : "<= 1.5x nominal", holdoverPct);
    assert(holdoverPass);

    // Resync: time from the returning source to 24 consecutive in-phase ticks,
    // plus the worst one-tick interval step across the return (never a jump).
    const double resyncBeats = lockBeats(run, 2.0, dropout.dropoutEnd);
    const double resyncMs = [&] {
      std::size_t stable = 0;
      for (std::size_t k = 0; k < run.generated.size(); ++k) {
        if (run.generated[k] < dropout.dropoutEnd) {
          stable = 0;
          continue;
        }
        const bool within = std::abs(toMs(run.generated[k] - run.ideal[k])) <= 2.0;
        stable = within ? stable + 1 : 0;
        if (stable >= 24) {
          return toMs(run.ideal[k + 1 - 24] - dropout.dropoutEnd);
        }
      }
      return toMs(run.ideal.back() - dropout.dropoutEnd);
    }();
    const double slewMs = maxIntervalStepMs(run);
    // After the step, the reference target is <= 2 ms once the loop settles;
    // check the second half of the post-dropout window.
    std::size_t afterStep = run.generated.size();
    for (std::size_t k = 0; k < run.generated.size(); ++k) {
      if (run.generated[k] >= dropout.stepAt + seconds(2)) {
        afterStep = k;
        break;
      }
    }
    const double settledMs = maxPhaseMs(run, afterStep);
    const bool resyncPass = resyncBeats <= 4.0 && slewMs <= 0.25 && settledMs <= 2.0;
    std::printf(
        "%s clock.follow-resync: after 550 ms dropout: resync to 2 ms in %.2f beats "
        "(%.0f ms), 2 s after the 121 bpm step phase %.3f ms, "
        "max interval step %.3f ms, no gap %s\n",
        resyncPass ? "PASS" : "FAIL", resyncBeats, resyncMs, settledMs, slewMs,
        gap ? "VIOLATED" : "yes");
    assert(resyncPass);
  }

  std::printf("PASS: external clock follow PLL (lock, residual, jitter, wander, "
              "holdover, smooth resync)\n");
  return 0;
}
