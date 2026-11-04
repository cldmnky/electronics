#pragma once
// CLAP send slots for the app. mnky::clap::Host is single-threaded by contract,
// so the GUI thread owns loading/params/state and the audio thread only calls
// Host::process() behind a try-lock: while the UI is mid-load the send passes
// audio through dry instead of racing a half-built plugin.
#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

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

  bool load(const std::string& path, double sampleRate, int maxFrames);
  void unload();

  bool loaded() const;
  std::string pluginPath() const;
  std::string pluginName() const;
  std::string lastError() const;

  std::vector<clap::Param> params() const;
  bool setParam(clap_id id, double value);
  // Stable parameter order used by the session format; values include pending
  // UI writes. Out-of-range indices are ignored / return 0.
  std::size_t paramCount() const;
  double paramValue(std::size_t index) const;
  bool setParamValue(std::size_t index, double value);

  // CLAP plugin id of the loaded module (empty when no plugin).
  std::string pluginId() const;
  bool bypassed() const { return bypassed_.load(); }
  void setBypassed(bool bypassed) { bypassed_.store(bypassed); }

  bool saveState(const std::string& path);
  bool loadState(const std::string& path);

  // First existing default plugin: the in-tree mnkygain target compiled into
  // MNKY_DEFAULT_PLUGIN, an MNKY_PLUGIN_PATH override, or a conventional
  // install location. Empty when nothing exists (the UI then says so).
  static std::string defaultPluginPath();

 private:
  class Adapter;

  mutable std::mutex lock_;
  std::atomic<bool> bypassed_{false};
  clap::Host host_;
  std::unique_ptr<Adapter> adapter_;
  const int index_;
  std::string path_;
  std::string lastError_;
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
