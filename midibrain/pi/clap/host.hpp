// CLAP host core (JUCE-free, portable C++20).
//
// One Host instance owns one in-process CLAP plugin instance: it dlopen()s a
// `.clap` module (or takes the same entry points from a test seam), creates a
// plugin through the factory, latches parameter writes per block and hands raw
// float stereo buffers to clap_plugin::process().
//
// Extension policy: audio-ports, params and state are optional. A missing
// params extension means `params()` is empty and `setParam()` fails; a missing
// audio-ports extension cannot be processed at all, so `activate()` fails with a
// message in `lastError()`; a missing state extension makes `saveState()` /
// `loadState()` fail. Nothing aborts or throws on a partially featured plugin.
//
// Threading: single-threaded by contract. load/activate/process/params/state all
// run on the caller's thread; no locks. The audio adapter shim from `mnky::audio`
// (`IEffect`) lives in the app layer, not here.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <clap/clap.h>

namespace mnky::clap {

struct Param {
   clap_id id = CLAP_INVALID_ID;
   std::string name;
   double min = 0.0;
   double max = 0.0;
   double value = 0.0;
};

// The three calls a CLAP module exposes at its ABI boundary. `load()` fills this
// from `dlopen()` + the `clap_entry` symbol; tests (and a statically linked
// plugin) fill it by hand via `loadInProcess()`.
struct ModuleHooks {
   bool (*init)(const char* plugin_path) = nullptr;
   void (*deinit)() = nullptr;
   const void* (*getFactory)(const char* factory_id) = nullptr;
};

class Host {
 public:
   Host();
   ~Host();

   Host(const Host&) = delete;
   Host& operator=(const Host&) = delete;

   // Dlopen `path` (.clap file, or a macOS `.clap` bundle directory — CLAP's
   // plugin_path is the bundle, so the Mach-O inside Contents/MacOS is resolved
   // and the bundle path is what the module sees), init the module, create the
   // plugin whose id matches `pluginId` (nullptr: the first one).
   bool load(const std::string& path, const char* pluginId = nullptr);

   // Module binary to dlopen for `path`: the path itself when it is a file (or
   // does not exist), Contents/MacOS/<binary> for a bundle directory, empty when
   // a directory holds no module.
   static std::string bundleBinaryPath(const std::string& path);

   // Same as load() without dlopen: the caller supplies the module entry points.
   bool loadInProcess(const ModuleHooks& hooks,
                      const std::string& pluginPath,
                      const char* pluginId = nullptr);

   // Deactivate, destroy the plugin, deinit the module, dlclose. Idempotent.
   void unload();
   bool loaded() const { return plugin_ != nullptr; }

   // Empty when the plugin has no params extension.
   std::vector<Param> params() const;

   // Latch a parameter write; delivered as a CLAP_EVENT_PARAM_VALUE at frame 0 of
   // the next process() (or at the next saveState(), via params->flush()).
   // Returns false for unknown ids or when the params extension is missing.
   bool setParam(clap_id id, double value);

   // Latched value if a write is still pending, otherwise the plugin's value.
   double getParam(clap_id id) const;

   bool saveState(const std::string& path);
   bool loadState(const std::string& path);

   // Requires one main stereo input and one main stereo output port.
   bool activate(double sample_rate, uint32_t max_frames);
   void deactivate();
   bool isActive() const { return active_; }

   // Raw float buffers, `frames` per channel. `inputs`/`outputs` may alias
   // (in-place). Inputs are read-only by CLAP contract. Returns false when not
   // active, when the channel counts differ from the declared ports, when
   // `frames` exceeds the activated maximum, or when process fails.
   bool process(float* const* inputs,
                uint32_t inputChannels,
                float* const* outputs,
                uint32_t outputChannels,
                uint32_t frames);

   const clap_host_t* hostHandle() const { return &host_; }
   const std::string& id() const { return id_; }
   const std::string& name() const { return name_; }
   const std::string& lastError() const { return last_error_; }
   // Last message the plugin sent through the clap.log host extension.
   const std::string& lastLog() const { return last_log_; }

   // Host callback requests the plugin raised; read-and-clear.
   bool takeRestartRequest();
   bool takeProcessRequest();
   bool takeCallbackRequest();
   // Plugin -> host events the host currently discards (no automation recording).
   uint32_t droppedOutputEvents() const { return dropped_output_events_; }

 private:
   struct PendingParam {
      clap_id id;
      double value;
   };

   static const void* hostGetExtension(const clap_host_t* host, const char* extension_id);
   static void hostRequestRestart(const clap_host_t* host);
   static void hostRequestProcess(const clap_host_t* host);
   static void hostRequestCallback(const clap_host_t* host);
   static bool hostIsMainThread(const clap_host_t* host);
   static bool hostIsAudioThread(const clap_host_t* host);
   static void hostLog(const clap_host_t* host, clap_log_severity severity, const char* msg);

   bool openModule(const ModuleHooks& hooks,
                   const std::string& plugin_path,
                   const char* plugin_id);
   void cacheExtensions();
   void fillPendingEvents(std::vector<clap_event_param_value_t>& out) const;
   void publishPendingParams();
   bool fail(const std::string& message);

   clap_host_t host_{};
   clap_host_thread_check_t thread_check_{};
   clap_host_log_t log_{};

   ModuleHooks hooks_{};
   bool module_inited_ = false;
   void* dl_handle_ = nullptr;

   const clap_plugin_t* plugin_ = nullptr;
   const clap_plugin_params_t* params_ext_ = nullptr;
   const clap_plugin_audio_ports_t* audio_ports_ext_ = nullptr;
   const clap_plugin_state_t* state_ext_ = nullptr;

   std::vector<PendingParam> pending_;
   std::string id_;
   std::string name_;
   std::string last_error_;
   std::string last_log_;

   bool active_ = false;
   bool processing_ = false;
   double sample_rate_ = 0.0;
   uint32_t max_frames_ = 0;
   uint32_t in_channels_ = 0;
   uint32_t out_channels_ = 0;
   int64_t steady_time_ = 0;

   uint32_t restart_requests_ = 0;
   uint32_t process_requests_ = 0;
   uint32_t callback_requests_ = 0;
   uint32_t dropped_output_events_ = 0;
};

}  // namespace mnky::clap
