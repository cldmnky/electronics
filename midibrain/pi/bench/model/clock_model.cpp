// clock_model - Pi-side scheduling measurement plus UsbClock model probe.
//
// Why this exists: with no Pico attached the app runs `--no-pico`, and in that
// path LinkClient::pumpNoPicoClock() reports latencyUs() = 0 by construction
// (there is no Pico stamp to compare against). So the only honest statement
// about UsbClock's discipline on this bench is:
//
//   real     - measured wake-up lateness of the Pi's own timer loops, which is
//              what actually limits a 1 ms polled emitter (LinkClient::run()
//              sleeps 1 ms per pass) and an absolute-deadline emitter;
//   modelled - those measured lateness values fed straight into the repository's
//              UsbClock (midi/clock.cpp) so its latencyUs() and phase error can
//              be reported. This half is a model: it proves what the algorithm
//              does with a measured lateness sequence, not what a Pico would do
//              on a wire.
//
// usage: clock_model <mode> <seconds> [interval_us]
//   sched      measure clock_nanosleep(ABSTIME) lateness on both cadences
//   freerun    drive UsbClock::advanceFreeRun() with the measured lateness
//   pico       drive UsbClock::addTick()/popDue() with measured lateness
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include "clock.hpp"

namespace {

std::uint64_t monoUs() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<std::uint64_t>(ts.tv_sec) * 1000000ULL +
         static_cast<std::uint64_t>(ts.tv_nsec) / 1000ULL;
}

void deadlineSleep(std::uint64_t dueUs) {
  struct timespec ts;
  ts.tv_sec = static_cast<time_t>(dueUs / 1000000ULL);
  ts.tv_nsec = static_cast<long>((dueUs % 1000000ULL) * 1000ULL);
  int rc = 0;
  do {
    rc = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr);
  } while (rc == EINTR);
}

double percentile(std::vector<double> v, double p) {
  if (v.empty()) return 0.0;
  std::sort(v.begin(), v.end());
  const double k = (v.size() - 1) * (p / 100.0);
  const std::size_t lo = static_cast<std::size_t>(k);
  const std::size_t hi = lo + 1 < v.size() ? lo + 1 : lo;
  return v[lo] + (v[hi] - v[lo]) * (k - static_cast<double>(lo));
}

struct Dist {
  std::size_t n = 0;
  double min = 0.0;
  double avg = 0.0;
  double p50 = 0.0;
  double p95 = 0.0;
  double p99 = 0.0;
  double max = 0.0;
  double stdev = 0.0;
};

Dist summarize(std::vector<double> v) {
  Dist d;
  if (v.empty()) return d;
  double sum = 0.0;
  d.min = v[0];
  d.max = v[0];
  for (double x : v) {
    sum += x;
    if (x < d.min) d.min = x;
    if (x > d.max) d.max = x;
  }
  d.n = v.size();
  d.avg = sum / static_cast<double>(v.size());
  double acc = 0.0;
  for (double x : v) acc += (x - d.avg) * (x - d.avg);
  d.stdev = std::sqrt(acc / static_cast<double>(v.size()));
  d.p50 = percentile(v, 50);
  d.p95 = percentile(v, 95);
  d.p99 = percentile(v, 99);
  return d;
}

void printDist(const char* name, const Dist& d) {
  std::printf("%s n=%zu min=%.1f avg=%.1f p50=%.1f p95=%.1f p99=%.1f max=%.1f stdev=%.1f\n",
              name, d.n, d.min, d.avg, d.p50, d.p95, d.p99, d.max, d.stdev);
}

// Measures wake-up lateness (wake time minus deadline) for `seconds`, returning
// the samples so the model modes can reuse exactly the same measured sequence.
std::vector<double> measureLateness(double seconds, double intervalUs) {
  std::vector<double> late;
  const std::uint64_t start = monoUs();
  const auto interval = static_cast<std::uint64_t>(llround(intervalUs));
  const std::uint64_t stop = start + static_cast<std::uint64_t>(seconds * 1e6);
  for (std::uint64_t k = 1;; ++k) {
    const std::uint64_t due = start + k * interval;
    if (due > stop) break;
    deadlineSleep(due);
    late.push_back(static_cast<double>(monoUs()) - static_cast<double>(due));
  }
  return late;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <sched|freerun|pico> <seconds> [interval_us]\n", argv[0]);
    return 2;
  }
  const std::string mode = argv[1];
  const double seconds = std::atof(argv[2]);
  const double intervalUs = argc > 3 ? std::atof(argv[3]) : 20833.3333333;

  std::printf("clock_model mode=%s seconds=%.1f interval_us=%.3f\n", mode.c_str(), seconds,
              intervalUs);

  if (mode == "sched") {
    // 20.833 ms: one clock byte at 120 BPM / 24 ppqn.
    const Dist clock = summarize(measureLateness(seconds, intervalUs));
    // 1 ms: the cadence of LinkClient::run()'s poll loop, which gates every
    // clock byte in the app today.
    const Dist poll = summarize(measureLateness(seconds, 1000.0));
    printDist("sched_20ms_lateness_us", clock);
    printDist("sched_1ms_lateness_us", poll);
    return 0;
  }

  // The remaining modes are models driven by the same measured lateness. The
  // lateness series is measured on the 1 ms poll cadence because that is how the
  // app emits; the model then advances the UsbClock phase at the nominal clock
  // interval and applies a poll-sized lateness sample to each deadline.
  const std::vector<double> late = measureLateness(seconds, 1000.0);
  printDist("measured_1ms_lateness_us", summarize(late));
  if (late.empty()) return 1;

  mnky::midi::UsbClock clock;
  std::vector<double> emissionLate;
  std::vector<double> intervalErr;
  std::vector<double> latencyUs;

  std::uint64_t now = monoUs();
  if (mode == "freerun") {
    clock.setFreeRun(true);
    clock.setRolling(true);
    clock.startFreeRun(now);
  } else if (mode == "pico") {
    clock.setFreeRun(false);
    clock.setRolling(true);
  } else {
    std::fprintf(stderr, "unknown mode %s\n", mode.c_str());
    return 2;
  }

  const std::uint64_t interval = static_cast<std::uint64_t>(llround(intervalUs));
  double previousEmit = 0.0;
  std::uint32_t picoStamp = 0;
  std::uint32_t tick = 0;
  for (std::uint64_t k = 1;; ++k) {
    const std::uint64_t nominalDue = now + k * interval;
    const double lateSample = late[k % late.size()];
    const std::uint64_t wake = nominalDue + static_cast<std::uint64_t>(std::llround(lateSample));
    if (wake > now + static_cast<std::uint64_t>(seconds * 1e6)) break;
    bool emitted = false;
    double emitLate = 0.0;
    if (mode == "freerun") {
      if (clock.advanceFreeRun(wake)) {
        emitted = true;
        emitLate = static_cast<double>(wake) - static_cast<double>(nominalDue);
      }
    } else {
      // One Pico tick per clock interval, delivered with the measured Pi wake-up
      // lateness; then the polled caller asks for and emits the due byte.
      picoStamp = static_cast<std::uint32_t>((k - 1) * interval);
      clock.addTick(++tick, picoStamp, wake);
      latencyUs.push_back(clock.latencyUs());
      const auto scheduled = clock.nextDueUs(wake);
      if (scheduled && clock.popDue(wake)) {
        emitted = true;
        emitLate = static_cast<double>(wake) - static_cast<double>(*scheduled);
      }
    }
    if (!emitted) continue;
    emissionLate.push_back(emitLate);
    if (previousEmit > 0.0) intervalErr.push_back(static_cast<double>(wake) - previousEmit - intervalUs);
    previousEmit = static_cast<double>(wake);
  }

  if (mode == "freerun") {
    printDist("model_freerun_emission_late_us", summarize(emissionLate));
  } else {
    // latencyUs() only ever moves when a Pico tick arrives; without one it is
    // exactly 0.0, which is what the app reports in --no-pico mode.
    printDist("model_pico_latencyUs", summarize(latencyUs));
    printDist("model_pico_emission_late_us", summarize(emissionLate));
  }
  printDist("model_emission_interval_err_us", summarize(intervalErr));
  std::printf("model_note=UsbClock model only; no Pico, no SPI, no wire\n");
  return 0;
}
