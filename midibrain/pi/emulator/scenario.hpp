#pragma once
// Scripted ~5 s emulator session for the headless selftest (no display, no JUCE).
//
// The scenario drives the real core modules through the emulator backends:
// mnky::link::EmulatorTransport + PicoLink for the handshake, tick stream and
// MIDI frames, mnky::midi::Router for routing, mnky::audio::Engine + Recorder
// for a two-channel take and mnky::clap::Host for the FX send. Every stage
// reports its own PASS/FAIL/SKIP, so a failure names the broken seam instead of
// just "selftest failed".
#include <cstdint>
#include <string>
#include <vector>

namespace mnky::emulator {

enum class StageStatus { Pass, Fail, Skip };

struct StageResult {
  std::string name;
  StageStatus status = StageStatus::Fail;
  /// Human-readable evidence on PASS, the first broken assertion on FAIL and the
  /// reason on SKIP.
  std::string detail;
};

struct ScenarioOptions {
  /// Directory the record stage creates its `take-<utc>/` in (the selftest
  /// passes an mkdtemp path).
  std::string workDir;
  /// CLAP effect module for the FX stage. Empty or missing on disk means the
  /// stage runs against a local stub gain and reports SKIP with that reason.
  std::string pluginPath;
};

struct ScenarioReport {
  std::vector<StageResult> stages;

  /// True when no stage failed. A Skip (no loadable CLAP module) is reported
  /// loudly but does not fail the run: the stub gain covers the same seam.
  bool passed() const;
  int failedCount() const;
  int skippedCount() const;
};

ScenarioReport runScenario(const ScenarioOptions& options);

}  // namespace mnky::emulator
