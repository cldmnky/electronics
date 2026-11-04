#pragma once
// CLAP send slots for the app. mnky::clap::Host is single-threaded by contract,
// so the message thread owns loading/params/state and the audio thread only
// calls Host::process() behind a try-lock: while the UI is mid-load the send
// passes audio through dry instead of racing a half-built plugin.
#include <atomic>
#include <memory>
#include <vector>

#include <juce_core/juce_core.h>

#include "../audio/engine.hpp"
#include "../clap/host.hpp"

namespace mnky::app {

class FxSlot {
 public:
  explicit FxSlot(int index);
  ~FxSlot();
  FxSlot(const FxSlot&) = delete;
  FxSlot& operator=(const FxSlot&) = delete;

  // Stable identity for Engine::setFx: valid even with no plugin loaded, in
  // which case process() is a no-op (the send bus passes through untouched).
  audio::IEffect& effect();

  bool load(const juce::String& path, double sampleRate, int maxFrames);
  void unload();

  bool loaded() const;
  juce::String pluginPath() const;
  juce::String lastError() const;

  std::vector<clap::Param> params() const;
  bool setParam(clap_id id, double value);

  bool saveState(const juce::File& file);
  bool loadState(const juce::File& file);

  // First existing default plugin: the in-tree mnkygain target compiled into
  // MNKY_DEFAULT_PLUGIN, an MNKY_PLUGIN_PATH override, or a conventional
  // install location. Empty when nothing exists (the UI then offers a picker).
  static juce::String defaultPluginPath();

 private:
  class Adapter;

  mutable juce::SpinLock lock_;
  clap::Host host_;
  std::unique_ptr<Adapter> adapter_;
  const int index_;
  juce::String path_;
  juce::String lastError_;
};

class FxManager {
 public:
  FxManager();

  FxSlot& slot(int index);
  // Audio thread: always non-null, see FxSlot::effect().
  audio::IEffect* effect(int index);

 private:
  FxSlot slotA_;
  FxSlot slotB_;
};

}  // namespace mnky::app
