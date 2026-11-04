#pragma once
// System screen: link/clock/READY diagnostics, the audio backend selector and
// the in-app log tail. The selector is the only place to switch the device type
// (default/ALSA/JACK), the output device and the requested period size; Apply
// restarts audio through the one AudioGlue device manager. This is also the only
// place a user can see why something is silent on a headless Pi.
#include "Screen.hpp"

#include "../audio_backend.hpp"

namespace mnky::app {

class SystemScreen final : public Screen {
 public:
  explicit SystemScreen(AppContext& context);

  void resized() override;
  void refresh() override;

 private:
  void rebuildBackendChoices();
  void rebuildBufferChoices();
  void rebuildDeviceChoices();
  void applyAudioSelection();
  void refreshBackends();

  AudioBackend selectedBackend() const;
  std::string selectedDeviceName() const;
  int selectedBufferSize() const;

  juce::Label audioCaption_;
  juce::ComboBox backendBox_;
  juce::ComboBox deviceBox_;
  juce::ComboBox bufferBox_;
  juce::TextButton applyButton_{"APPLY"};
  juce::TextButton refreshButton_{"REFRESH"};
  juce::Label audioLive_;
  juce::Label audioNote_;

  juce::TextEditor stats_;
  juce::TextEditor logTail_;
  juce::String statsCache_;
  juce::String logCache_;
  juce::String liveCache_;
  juce::String noteCache_;

  JUCE_DECLARE_NON_COPYABLE(SystemScreen)
};

}  // namespace mnky::app
