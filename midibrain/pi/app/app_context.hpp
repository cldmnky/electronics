#pragma once
// Service bundle handed to every screen. The app owns these objects for its
// whole lifetime (MainComponent), so screens hold plain references.

namespace mnky::app {

class AppState;
class AudioGlue;
class FxManager;
class LinkClient;
class MidiGlue;
class MixerModel;

struct AppContext {
  AppState& state;
  MixerModel& mixer;
  FxManager& fx;
  MidiGlue& midi;
  AudioGlue& audio;
  LinkClient& link;
};

}  // namespace mnky::app
