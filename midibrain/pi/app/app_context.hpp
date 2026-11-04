#pragma once
// Service bundle handed to the project/session glue. The Qt controller owns
// these objects for the app's whole lifetime, so the glue holds references.

namespace mnky::app {

class AppState;
class AudioGlue;
class EqModel;
class FxManager;
class LinkClient;
class MidiGlue;
class MixerModel;
class PerformanceModel;
class SessionSettings;

struct AppContext {
  AppState& state;
  MixerModel& mixer;
  FxManager& fx;
  MidiGlue& midi;
  AudioGlue& audio;
  LinkClient& link;
  SessionSettings& settings;
  // EQ bands and the X/Y performance positions ride in the session's fx section
  // (session v2 Snapshot::fx.eq / fx.xy); the project glue captures and applies
  // them and folds them into the dirty fingerprint.
  EqModel& eq;
  PerformanceModel& performance;
};

}  // namespace mnky::app
