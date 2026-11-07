// Standalone acceptance entry for the clock-emulation harness. Pure host code,
// no wall-clock sleeps: it runs the hardware models and the review's acceptance
// matrix and prints the table the bench run later reproduces.
//
// Exit code 0 only when every stage passes. Wired into CTest as
// mnky_clock_sim_test and into the repository `make test` host target.
#include <cstdio>

#include "clock_sim.hpp"

int main() {
  const mnky::emulator::clocksim::ClockSimResult result =
      mnky::emulator::clocksim::runClockSimulation();

  std::printf("mnkybox MIDI-clock emulation harness (virtual time, deterministic)\n");
  int failed = 0;
  for (const auto& stage : result.stages) {
    std::printf("%-4s  %-26s %s\n", stage.pass ? "PASS" : "FAIL", stage.name.c_str(),
                stage.detail.c_str());
    if (!stage.pass) ++failed;
  }
  std::printf("\n%s", result.acceptanceTable.c_str());
  std::printf("%zu stages: %zu passed, %d failed\n", result.stages.size(),
              result.stages.size() - static_cast<std::size_t>(failed), failed);
  std::printf("RESULT: %s\n", result.passed() ? "PASS" : "FAIL");
  return result.passed() ? 0 : 1;
}
