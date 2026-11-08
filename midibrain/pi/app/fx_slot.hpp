#pragma once
// CLAP hosting for the app. mnky::clap::Host is single-threaded by contract, so
// the GUI thread owns loading/params/state and the audio thread only calls
// Host::process() behind a try-lock: while the UI is mid-load the slot passes
// audio through dry instead of racing a half-built plugin.
//
// FxSlot is the engine-facing adapter plus one plugin instance. FxManager owns
// two legacy send-bus slots (kept only for the session fx.bus round-trip) and
// the per-track rack hosts: one wrapper per track slot, created on the first
// CLAP selection only. Built-in and empty slots never allocate a host, so a
// project without plugins pays nothing for the capability.
#include <array>
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

  // `pluginId` selects one factory entry from a multi-plugin module; empty
  // takes the module's first plugin (the historical default).
  bool load(const std::string& path, double sampleRate, int maxFrames,
            const std::string& pluginId = {});
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
  // install location. Empty when nothing exists.
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
  // At most 16 simultaneous per-track CLAP instances. This ceiling is the UI's
  // admission rule, not a measured CPU guarantee: a loaded instance costs CPU
  // on the target, so the number stays small enough to keep the graph live.
  static constexpr int kMaxTrackInstances = 16;
  static constexpr int kTrackSlots = audio::kTrackFxSlots;

  // One discovered CLAP plugin: the module path, its factory identity and the
  // plugin's own parameter table at defaults (the vector a slot starts from).
  struct PluginInfo {
    std::string id;
    std::string name;
    std::string path;
    std::vector<clap::Param> params;
  };

  FxManager();

  // ---- legacy send-bus slots (session fx.bus round-trip only) -------------
  FxSlot& slot(int index);
  // Audio thread: always non-null, see FxSlot::effect().
  audio::IEffect* effect(int index);

  // ---- plugin discovery ---------------------------------------------------
  // Scans MNKY_DEFAULT_PLUGIN / MNKY_PLUGIN_PATH plus the conventional CLAP
  // directories once, loading each module just long enough to read its factory
  // descriptor and parameter table. No plugin stays instantiated. Ordered by
  // discovery path; duplicate ids keep the first occurrence.
  const std::vector<PluginInfo>& plugins();
  const PluginInfo* findPlugin(const std::string& id);

  // ---- per-track hosts (control thread only) ------------------------------
  // Loads the plugin `pluginId` into (track, slot), or reuses the live instance
  // when it already holds that id. The first call creates the wrapper; a
  // built-in or empty slot never reaches here. Fails when the 16-instance cap
  // is reached (the caller refuses the selection) or the module cannot load.
  bool loadTrackEffect(int track, int slot, const std::string& pluginId,
                       double sampleRate, int maxFrames);
  // Detaches nothing (the engine seam is the caller's): deactivates, destroys
  // and dlclose()s the plugin instance. The wrapper stays parked for reuse so
  // an in-flight audio callback can never dereference freed memory.
  void unloadTrackEffect(int track, int slot);
  bool trackEffectLoaded(int track, int slot) const;
  audio::IEffect* trackEffect(int track, int slot);
  std::string trackEffectId(int track, int slot) const;
  std::string trackEffectName(int track, int slot) const;
  std::string trackEffectError(int track, int slot) const;
  // The live plugin's parameter table; empty when no instance is loaded (the
  // view model then falls back to the discovered plugin's defaults).
  std::vector<clap::Param> trackParams(int track, int slot);
  // Live value including a latched-but-not-yet-processed UI write.
  double trackParamValue(int track, int slot, std::size_t index) const;
  bool setTrackParam(int track, int slot, std::size_t index, double value);
  int trackInstanceCount() const;
  // True when (track, slot) already holds an instance (a switch reuses that
  // admission) or the simultaneous cap still has room.
  bool canLoadTrackEffect(int track, int slot) const;

 private:
  static bool validSlot(int track, int slot);
  static std::size_t flatSlot(int track, int slot);
  void scanPlugins();

  FxSlot slotA_;
  FxSlot slotB_;
  std::array<std::array<std::unique_ptr<FxSlot>, kTrackSlots>, audio::kMaxTracks>
      trackSlots_{};
  std::array<std::string, audio::kMaxTracks * kTrackSlots> trackErrors_{};
  std::vector<PluginInfo> plugins_;
  bool scanned_ = false;
};

}  // namespace mnky::app
