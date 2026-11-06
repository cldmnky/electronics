#pragma once
// App-owned session-v2 settings: punch markers, monitor/cue configuration,
// per-destination send sources, the main-bus compressor, the per-track take
// loop and the per-output clock/transport routes. The session core deliberately
// leaves these sections alone
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
  // Per-track take loop in session beats (session v2 takes.loop). Written on
  // the GUI thread only; the engine copy lives in AudioGlue. Sized to the mixer
  // track capacity (audio::kMaxTracks): the session snapshot carries whatever
  // its own store width is, and project_io copies by container size.
  std::array<session::LoopState, audio::kMaxTracks> takeLoop{};
  // Per-track non-destructive region edits (session v2 takes.edit, landed in
  // 43130e8). The TimelineViewModel owns the edits and applies them through
  // AudioGlue; this array is what project_io saves and loads.
  std::array<std::vector<session::TakeRegion>, audio::kMaxTracks> takeEdit{};
  // Per-output clock/transport gates (session v2 clock). Index 0/1 = TRS OUT1/
  // OUT2 via ClockState::trs, usb covers the USB mirror; the tempo divider and
  // sync delay in ClockRoute are session-only because EV_CLOCK_ROUTE carries
  // only the two gate bits.
  session::ClockState clock;
  // Which device channel pair carries Main 1&2, OUT3&4 and the phones bus
  // (session v2 deviceOut, 0-based: default Main 1&2, OUT3&4, phones 5&6). The
  // System screen's pickers write it and project_io copies it to and from the
  // snapshot. The engine side exists (audio::Engine::setDeviceOut and
  // audio::DeviceOut in audio/engine.cpp); the app-side handoff does not:
  // AudioGlue has no applyDeviceOut(), so these values are saved and displayed
  // but do not move the buses on the opened device yet.
  session::DeviceOutState deviceOut;
  // Settings-only project marker (session v2 snapshot.isTemplate).
  bool isTemplate = false;

 private:
  mutable std::mutex takeLock_;
  std::array<std::string, audio::kMaxTracks> activeTakes_{};
};

}  // namespace mnky::app
