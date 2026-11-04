#pragma once
// Service bundle handed to the project/session glue. The Qt controller owns
// these objects for the app's whole lifetime, so the glue holds references.

namespace mnky::app {

class AppState;
class AudioGlue;
class FxManager;
class LinkClient;
class MidiGlue;
class MixerModel;
class SessionSettings;

struct AppContext {
  AppState& state;
  MixerModel& mixer;
  FxManager& fx;
  MidiGlue& midi;
  AudioGlue& audio;
  LinkClient& link;
  SessionSettings& settings;
};

}  // namespace mnky::app
