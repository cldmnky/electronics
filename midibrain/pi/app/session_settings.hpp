#pragma once
// App-owned session-v2 settings: punch markers, monitor/cue configuration,
// per-destination send sources, the main-bus compressor and the active take
// per track. The session core deliberately leaves these sections alone
// (session.hpp: capture() leaves them at defaults, apply() does not touch
// them), so the app is their single owner and project_io copies them in and
// out of the snapshot. Mixer values that belong to a strip (cue/out34 sends,
// OUT3&4 mode) live in MixerModel instead; click state lives in AppState
// because the audio callback reads it every block.
#include <array>
#include <mutex>
#include <string>

#include "../audio/engine.hpp"
#include "../session/session.hpp"

namespace mnky::app {

class SessionSettings {
 public:
  // Active take pointer per track: empty means the live input. Non-empty is
  // the take file name relative to the take directory, or an absolute path for
  // an imported pointer. Never a copy of the audio.
  std::string activeTake(int track) const;
  void setActiveTake(int track, std::string value);
  bool hasActiveTake(int track) const;

  // Defaults for all app-owned sections (project load/new).
  void reset();

  session::PunchState punch;
  session::CueState cue;
  session::SendsState sends;
  session::MainCompState mainComp;
  // Settings-only project marker (session v2 snapshot.isTemplate).
  bool isTemplate = false;

 private:
  mutable std::mutex takeLock_;
  std::array<std::string, audio::kMaxInputs> activeTakes_{};
};

}  // namespace mnky::app
