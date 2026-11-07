// Headless emulator selftest: no display, no JUCE, no audio device.
//
// Runs the scripted scenario (link handshake, MIDI loopback, two-channel take,
// CLAP gain pass, WAV verification) inside a fresh mkdtemp directory, prints one
// PASS/FAIL/SKIP line per stage and exits 0 only when no stage failed.
//
// Usage: mnky_selftest [clap-module-path]
//   The optional module is the FX under test (e.g. mnkygain.dylib). Without it
//   (or when the path does not exist) the FX stage runs against a local stub
//   gain and reports SKIP with the reason. MNKY_SELFTEST_CLAP_PLUGIN is read as
//   a fallback so CTest can supply the module through the environment.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

// mkdtemp() is POSIX: this macOS SDK declares it in <unistd.h>, glibc in
// <stdlib.h>. Including both keeps the global-namespace prototype visible on
// the macOS dev machine and on the Pi.
#include <stdlib.h>
#include <unistd.h>

#include "clock_sim.hpp"
#include "scenario.hpp"

namespace {

const char* label(mnky::emulator::StageStatus status) {
  switch (status) {
    case mnky::emulator::StageStatus::Pass:
      return "PASS";
    case mnky::emulator::StageStatus::Fail:
      return "FAIL";
    case mnky::emulator::StageStatus::Skip:
      return "SKIP";
  }
  return "FAIL";
}

// mkdtemp() gives a private, collision-free directory; the take inside it is
// left behind on purpose so a failure can be inspected after the fact.
std::string makeWorkDir() {
  char pattern[] = "/tmp/mnkybox-selftest.XXXXXX";
  if (::mkdtemp(pattern) == nullptr) return std::string();
  return pattern;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc > 2) {
    std::fprintf(stderr, "usage: %s [clap-module-path]\n", argv[0]);
    return 2;
  }

  std::string pluginPath;
  if (argc == 2) pluginPath = argv[1];
  if (pluginPath.empty()) {
    const char* fromEnv = std::getenv("MNKY_SELFTEST_CLAP_PLUGIN");
    if (fromEnv != nullptr) pluginPath = fromEnv;
  }

  const std::string workDir = makeWorkDir();
  if (workDir.empty()) {
    std::fprintf(stderr, "FAIL  setup  could not create a mkdtemp work directory\n");
    return 1;
  }

  std::printf("mnkybox emulator selftest (headless, no display, no JUCE)\n");
  std::printf("work dir: %s\n", workDir.c_str());
  std::printf("clap module: %s\n", pluginPath.empty() ? "(none supplied)" : pluginPath.c_str());
  std::fflush(stdout);

  mnky::emulator::ScenarioOptions options;
  options.workDir = workDir;
  options.pluginPath = pluginPath;
  mnky::emulator::ScenarioReport report = mnky::emulator::runScenario(options);
  const mnky::emulator::clocksim::ClockSimResult clock =
      mnky::emulator::clocksim::runClockSimulation();
  for (const auto& stage : clock.stages) {
    report.stages.push_back(mnky::emulator::StageResult{
        stage.name, stage.pass ? mnky::emulator::StageStatus::Pass
                               : mnky::emulator::StageStatus::Fail,
        stage.detail});
  }

  int passed = 0;
  int failed = 0;
  int skipped = 0;
  for (const auto& stage : report.stages) {
    std::printf("%-4s  %-15s %s\n", label(stage.status), stage.name.c_str(), stage.detail.c_str());
    switch (stage.status) {
      case mnky::emulator::StageStatus::Pass:
        ++passed;
        break;
      case mnky::emulator::StageStatus::Fail:
        ++failed;
        break;
      case mnky::emulator::StageStatus::Skip:
        ++skipped;
        // Loud on purpose: a skipped stage is visible in CI logs, even when the
        // exit code is green. Flush stdout first so the line lands in order.
        std::fflush(stdout);
        std::fprintf(stderr, "SKIP  %s: %s\n", stage.name.c_str(), stage.detail.c_str());
        break;
    }
  }

  std::printf("\n%s", clock.acceptanceTable.c_str());
  std::printf("%zu stages: %d passed, %d failed, %d skipped\n", report.stages.size(), passed,
              failed, skipped);
  std::printf("RESULT: %s\n", report.passed() ? "PASS" : "FAIL");
  std::fflush(stdout);
  return report.passed() ? 0 : 1;
}
