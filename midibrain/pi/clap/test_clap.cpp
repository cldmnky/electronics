// Host core test: drives mnky::clap::Host against an in-process mock plugin (no
// .clap module, no dlopen) through the ModuleHooks seam, plus the failure paths
// that a partially featured or hostile plugin can produce.
//
// Header path: this file includes <clap/clap.h>. When pi/third_party/clap exists
// the real 1.2.10 headers are used; otherwise the build adds -Itest/clap_stub and
// CLAP_ABI_STUB selects the test-only ABI stand-in (banner prints which one ran).

#include <clap/clap.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "host.hpp"

namespace {

using mnky::clap::Host;
using mnky::clap::ModuleHooks;

int g_checks = 0;
int g_failures = 0;

void check(const char* name, bool ok, const std::string& detail = std::string()) {
   ++g_checks;
   if (ok) {
      std::printf("PASS: %s\n", name);
      return;
   }
   ++g_failures;
   std::printf("FAIL: %s%s%s\n", name, detail.empty() ? "" : " - ", detail.c_str());
}

// ----------------------------------------------------------------- mock plugin

constexpr clap_id kGain = 0;
constexpr clap_id kMix = 1;

struct Mock {
   clap_plugin_t plugin{};
   const clap_host_t* host = nullptr;

   double gain = 1.0;
   double mix = 0.5;

   bool fail_init = false;
   bool fail_activate = false;
   bool fail_start_processing = false;
   bool fail_process = false;
   bool provide_params = true;
   bool provide_audio_ports = true;
   bool provide_state = true;
   bool request_restart_in_process = false;
   bool log_in_init = false;

   int init_calls = 0;
   int activate_calls = 0;
   int deactivate_calls = 0;
   int start_calls = 0;
   int stop_calls = 0;
   int process_calls = 0;
   int flush_calls = 0;
   int destroy_calls = 0;
   uint32_t events_seen = 0;
   double last_event_value = 0.0;
   uint32_t frames_seen = 0;
   uint32_t max_frames_seen = 0;
   double sample_rate = 0.0;
   float last_out_left = 0.0f;
};

Mock& mock_of(const clap_plugin_t* plugin) { return *static_cast<Mock*>(plugin->plugin_data); }

double* param_slot(Mock& mock, clap_id id) {
   if (id == kGain) return &mock.gain;
   if (id == kMix) return &mock.mix;
   return nullptr;
}

void apply_param_events(Mock& mock, const clap_input_events_t* events) {
   if (events == nullptr || events->size == nullptr || events->get == nullptr) return;
   const uint32_t count = events->size(events);
   mock.events_seen += count;
   for (uint32_t index = 0; index < count; ++index) {
      const clap_event_header_t* header = events->get(events, index);
      if (header == nullptr) continue;
      if (header->space_id != CLAP_CORE_EVENT_SPACE_ID) continue;
      if (header->type != CLAP_EVENT_PARAM_VALUE) continue;
      const auto* event = reinterpret_cast<const clap_event_param_value_t*>(header);
      if (double* slot = param_slot(mock, event->param_id); slot != nullptr) *slot = event->value;
      mock.last_event_value = event->value;
   }
}

bool CLAP_ABI mock_init(const clap_plugin_t* plugin) {
   Mock& mock = mock_of(plugin);
   ++mock.init_calls;
   if (mock.log_in_init && mock.host != nullptr && mock.host->get_extension != nullptr) {
      const auto* log = static_cast<const clap_host_log_t*>(
         mock.host->get_extension(mock.host, CLAP_EXT_LOG));
      if (log != nullptr && log->log != nullptr) log->log(mock.host, 1, "mock plugin initialised");
   }
   return !mock.fail_init;
}

void CLAP_ABI mock_destroy(const clap_plugin_t* plugin) { ++mock_of(plugin).destroy_calls; }

bool CLAP_ABI mock_activate(const clap_plugin_t* plugin,
                            double sample_rate,
                            uint32_t min_frames,
                            uint32_t max_frames) {
   Mock& mock = mock_of(plugin);
   ++mock.activate_calls;
   mock.frames_seen = min_frames;
   mock.max_frames_seen = max_frames;
   mock.sample_rate = sample_rate;
   return !mock.fail_activate;
}

void CLAP_ABI mock_deactivate(const clap_plugin_t* plugin) { ++mock_of(plugin).deactivate_calls; }

bool CLAP_ABI mock_start_processing(const clap_plugin_t* plugin) {
   Mock& mock = mock_of(plugin);
   ++mock.start_calls;
   return !mock.fail_start_processing;
}

void CLAP_ABI mock_stop_processing(const clap_plugin_t* plugin) { ++mock_of(plugin).stop_calls; }

void CLAP_ABI mock_reset(const clap_plugin_t*) {}

clap_process_status CLAP_ABI mock_process(const clap_plugin_t* plugin,
                                          const clap_process_t* process) {
   Mock& mock = mock_of(plugin);
   ++mock.process_calls;
   if (mock.fail_process) return CLAP_PROCESS_ERROR;
   if (mock.request_restart_in_process && mock.host != nullptr &&
       mock.host->request_restart != nullptr) {
      mock.host->request_restart(mock.host);
   }
   apply_param_events(mock, process->in_events);

   const clap_audio_buffer_t& in = process->audio_inputs[0];
   clap_audio_buffer_t& out = process->audio_outputs[0];
   for (uint32_t frame = 0; frame < process->frames_count; ++frame) {
      const float dry = in.data32[0][frame];
      const float wet = dry * static_cast<float>(mock.gain) + static_cast<float>(mock.mix);
      out.data32[0][frame] = wet;
      out.data32[1][frame] = wet;
   }
   mock.last_out_left = out.data32[0][0];
   return CLAP_PROCESS_CONTINUE;
}

uint32_t CLAP_ABI params_count(const clap_plugin_t*) { return 2; }

bool CLAP_ABI params_get_info(const clap_plugin_t*, uint32_t index, clap_param_info_t* info) {
   if (info == nullptr) return false;
   *info = clap_param_info_t{};
   info->flags = CLAP_PARAM_IS_AUTOMATABLE;
   if (index == 0) {
      info->id = kGain;
      std::snprintf(info->name, sizeof(info->name), "%s", "Gain");
      info->min_value = 0.0;
      info->max_value = 2.0;
      info->default_value = 1.0;
      return true;
   }
   if (index == 1) {
      info->id = kMix;
      std::snprintf(info->name, sizeof(info->name), "%s", "Mix");
      info->min_value = 0.0;
      info->max_value = 0.5;
      info->default_value = 0.5;
      return true;
   }
   return false;
}

bool CLAP_ABI params_get_value(const clap_plugin_t* plugin, clap_id id, double* out_value) {
   if (out_value == nullptr) return false;
   const double* slot = param_slot(mock_of(plugin), id);
   if (slot == nullptr) return false;
   *out_value = *slot;
   return true;
}

bool CLAP_ABI params_value_to_text(const clap_plugin_t*,
                                   clap_id id,
                                   double value,
                                   char* out_buffer,
                                   uint32_t capacity) {
   if (out_buffer == nullptr || capacity == 0) return false;
   std::snprintf(out_buffer, capacity, "%u=%.3f", id, value);
   return true;
}

bool CLAP_ABI params_text_to_value(const clap_plugin_t*, clap_id, const char*, double*) {
   return false;
}

void CLAP_ABI params_flush(const clap_plugin_t* plugin,
                           const clap_input_events_t* in,
                           const clap_output_events_t*) {
   Mock& mock = mock_of(plugin);
   ++mock.flush_calls;
   apply_param_events(mock, in);
}

uint32_t CLAP_ABI ports_count(const clap_plugin_t*, bool) { return 1; }

bool CLAP_ABI ports_get(const clap_plugin_t*,
                        uint32_t index,
                        bool is_input,
                        clap_audio_port_info_t* info) {
   if (info == nullptr || index != 0) return false;
   *info = clap_audio_port_info_t{};
   info->id = is_input ? 0u : 1u;
   info->flags = CLAP_AUDIO_PORT_IS_MAIN;
   info->channel_count = 2;
   info->port_type = CLAP_PORT_STEREO;
   info->in_place_pair = is_input ? 1u : CLAP_INVALID_ID;
   std::snprintf(info->name, sizeof(info->name), "%s", is_input ? "Input" : "Output");
   return true;
}

bool CLAP_ABI state_save(const clap_plugin_t* plugin, const clap_ostream_t* stream) {
   if (stream == nullptr || stream->write == nullptr) return false;
   double values[2] = {mock_of(plugin).gain, mock_of(plugin).mix};
   unsigned char bytes[sizeof(values)];
   std::memcpy(bytes, values, sizeof(values));
   uint64_t offset = 0;
   while (offset < sizeof(bytes)) {
      const int64_t written = stream->write(stream, bytes + offset, sizeof(bytes) - offset);
      if (written <= 0) return false;
      offset += static_cast<uint64_t>(written);
   }
   return true;
}

bool CLAP_ABI state_load(const clap_plugin_t* plugin, const clap_istream_t* stream) {
   if (stream == nullptr || stream->read == nullptr) return false;
   unsigned char bytes[sizeof(double) * 2];
   uint64_t offset = 0;
   while (offset < sizeof(bytes)) {
      const int64_t read = stream->read(stream, bytes + offset, sizeof(bytes) - offset);
      if (read <= 0) return false;
      offset += static_cast<uint64_t>(read);
   }
   double values[2];
   std::memcpy(values, bytes, sizeof(values));
   mock_of(plugin).gain = values[0];
   mock_of(plugin).mix = values[1];
   return true;
}

clap_plugin_params_t g_params_ext{&params_count, &params_get_info, &params_get_value,
                                  &params_value_to_text, &params_text_to_value, &params_flush};
clap_plugin_audio_ports_t g_ports_ext{&ports_count, &ports_get};
clap_plugin_state_t g_state_ext{&state_save, &state_load};

const void* CLAP_ABI plugin_get_extension(const clap_plugin_t* plugin, const char* id) {
   const Mock& mock = mock_of(plugin);
   if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return mock.provide_params ? &g_params_ext : nullptr;
   if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) {
      return mock.provide_audio_ports ? &g_ports_ext : nullptr;
   }
   if (std::strcmp(id, CLAP_EXT_STATE) == 0) return mock.provide_state ? &g_state_ext : nullptr;
   return nullptr;
}

void CLAP_ABI plugin_on_main_thread(const clap_plugin_t*) {}

// One in-process module: mock instance + factory + entry hooks. The factory has
// no host pointer of its own, so the test keeps a single active module.
struct TestModule {
   Mock mock;
   clap_plugin_descriptor_t descriptor{};
   clap_plugin_factory_t factory{};
   std::string init_path;
   int module_init_calls = 0;
   int module_deinit_calls = 0;
   bool fail_module_init = false;
   bool omit_factory = false;

   TestModule();

   const clap_plugin_descriptor_t* descriptor_pointer() const { return &descriptor; }
   const clap_plugin_t* plugin_pointer() { return &mock.plugin; }
};

TestModule* g_module = nullptr;

const char* const kFeatures[] = {"audio-effect", "stereo", "utility", nullptr};

uint32_t CLAP_ABI factory_count(const clap_plugin_factory_t*) { return 1; }

const clap_plugin_descriptor_t* CLAP_ABI factory_descriptor(const clap_plugin_factory_t*,
                                                            uint32_t index) {
   if (g_module == nullptr || index != 0) return nullptr;
   return g_module->descriptor_pointer();
}

const clap_plugin_t* CLAP_ABI factory_create(const clap_plugin_factory_t*,
                                             const clap_host_t* host,
                                             const char* plugin_id) {
   if (g_module == nullptr || plugin_id == nullptr) return nullptr;
   if (std::strcmp(plugin_id, g_module->descriptor.id) != 0) return nullptr;
   g_module->mock.host = host;
   return g_module->plugin_pointer();
}

bool module_init(const char* plugin_path) {
   if (g_module == nullptr) return false;
   ++g_module->module_init_calls;
   g_module->init_path = plugin_path != nullptr ? plugin_path : "";
   return !g_module->fail_module_init;
}

void module_deinit() {
   if (g_module != nullptr) ++g_module->module_deinit_calls;
}

const void* module_get_factory(const char* factory_id) {
   if (g_module == nullptr || g_module->omit_factory) return nullptr;
   if (std::strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID) != 0) return nullptr;
   return &g_module->factory;
}

TestModule::TestModule() {
   descriptor.clap_version = CLAP_VERSION;
   descriptor.id = "com.cldmnky.mnkygain.mock";
   descriptor.name = "mnkygain (mock)";
   descriptor.vendor = "cldmnky";
   descriptor.url = "";
   descriptor.manual_url = "";
   descriptor.support_url = "";
   descriptor.version = "0.1.0";
   descriptor.description = "in-process mock used by test_clap";
   descriptor.features = kFeatures;

   factory.get_plugin_count = &factory_count;
   factory.get_plugin_descriptor = &factory_descriptor;
   factory.create_plugin = &factory_create;

   mock.plugin.desc = &descriptor;
   mock.plugin.plugin_data = &mock;
   mock.plugin.init = &mock_init;
   mock.plugin.destroy = &mock_destroy;
   mock.plugin.activate = &mock_activate;
   mock.plugin.deactivate = &mock_deactivate;
   mock.plugin.start_processing = &mock_start_processing;
   mock.plugin.stop_processing = &mock_stop_processing;
   mock.plugin.reset = &mock_reset;
   mock.plugin.process = &mock_process;
   mock.plugin.get_extension = &plugin_get_extension;
   mock.plugin.on_main_thread = &plugin_on_main_thread;

   g_module = this;
}

ModuleHooks hooks_of(TestModule& module) {
   g_module = &module;
   return ModuleHooks{&module_init, &module_deinit, &module_get_factory};
}

constexpr uint32_t kMaxFrames = 128;

struct Block {
   std::vector<float> left;
   std::vector<float> right;

   explicit Block(uint32_t frames) : left(frames, 0.25f), right(frames, -0.25f) {}

   float* const* channels() {
      pointers_[0] = left.data();
      pointers_[1] = right.data();
      return pointers_;
   }

 private:
   float* pointers_[2]{};
};

// ---------------------------------------------------------------------- checks

void test_happy_path() {
   TestModule module;
   Host host;
   const bool loaded = host.loadInProcess(hooks_of(module), "/tmp/mnkygain.mock.clap");
   check("load: in-process module creates a plugin", loaded, host.lastError());
   check("load: descriptor id is reported", host.id() == module.descriptor.id, host.id());
   check("load: descriptor name is reported", host.name() == module.descriptor.name, host.name());
   check("load: plugin init() ran exactly once", module.mock.init_calls == 1);
   check("load: module init() received the plugin path",
         module.init_path == "/tmp/mnkygain.mock.clap", module.init_path);
   const clap_host_t* host_handle = host.hostHandle();
   check("load: thread-check host extension is offered",
         host_handle->get_extension(host_handle, CLAP_EXT_THREAD_CHECK) != nullptr);

   const std::vector<mnky::clap::Param> params = host.params();
   check("params: two parameters are listed", params.size() == 2);
   check("params: ids/names/ranges are exposed",
         !params.empty() && params[0].id == kGain && params[0].name == "Gain" &&
            params[0].min == 0.0 && params[0].max == 2.0 && params[0].value == 1.0);

   check("params: write is latched before process", host.setParam(kGain, 0.5));
   check("params: latched value is readable", host.getParam(kGain) == 0.5);
   check("params: value above max is clamped", host.setParam(kGain, 99.0) && host.getParam(kGain) == 2.0);
   check("params: value below min is clamped", host.setParam(kGain, -5.0) && host.getParam(kGain) == 0.0);
   const bool unknown_rejected = host.setParam(42, 0.5);
   check("params: unknown id is rejected", !unknown_rejected, host.lastError());

   const bool activated = host.activate(48000.0, kMaxFrames);
   check("activate: stereo ports are accepted", activated, host.lastError());
   check("activate: plugin activate+start_processing ran",
         module.mock.activate_calls == 1 && module.mock.start_calls == 1);
   check("activate: sample rate and block size are forwarded",
         module.mock.sample_rate == 48000.0 && module.mock.max_frames_seen == kMaxFrames);

   Block block(kMaxFrames);
   check("process: parameter events are latched per block",
         host.setParam(kGain, 0.5) && host.setParam(kMix, 0.25));
   const bool processed = host.process(block.channels(), 2, block.channels(), 2, kMaxFrames);
   check("process: block is processed", processed, host.lastError());
   check("process: plugin saw one param event per write", module.mock.events_seen == 2);
   check("process: latched gain reached the plugin", module.mock.gain == 0.5);
   check("process: latched mix reached the plugin", module.mock.mix == 0.25);
   check("process: audio is written (0.25 input * 0.5 gain + 0.25 mix)",
         block.left[0] == 0.375f && block.right[0] == 0.375f);
   check("process: pending writes are cleared after the block", host.getParam(kGain) == 0.5);
   check("process: steady time advances with the block", host.process(block.channels(), 2,
                                                                    block.channels(), 2, kMaxFrames));

   Block short_block(32);
   check("process: smaller block is accepted",
         host.process(short_block.channels(), 2, short_block.channels(), 2, 32));
   const bool bad_channels = host.process(short_block.channels(), 1, short_block.channels(), 2, 32);
   check("process: too few input channels are rejected", !bad_channels, host.lastError());
   const bool too_long = host.process(short_block.channels(), 2, short_block.channels(), 2, kMaxFrames + 1);
   check("process: block above the activated maximum is rejected", !too_long, host.lastError());
   const bool zero_frames = host.process(short_block.channels(), 2, short_block.channels(), 2, 0);
   check("process: zero frames are rejected", !zero_frames, host.lastError());
   float* const* null_buffers = nullptr;
   const bool null_process = host.process(null_buffers, 2, null_buffers, 2, 32);
   check("process: null buffers are rejected", !null_process, host.lastError());

   module.mock.request_restart_in_process = true;
   check("process: block with a restart request still succeeds",
         host.process(short_block.channels(), 2, short_block.channels(), 2, 32));
   check("host: request_restart is latched read-and-clear",
         host.takeRestartRequest() && !host.takeRestartRequest());
   check("host: process/callback requests are latched read-and-clear",
         host.takeProcessRequest() == false && host.takeCallbackRequest() == false);

   const std::string state_path = "/tmp/mnkygain_mock.state";
   std::remove(state_path.c_str());
   check("state: pending write before save", host.setParam(kMix, 0.125));
   const bool saved = host.saveState(state_path);
   check("state: save writes a file", saved, host.lastError());
   check("state: pending writes are flushed before saving",
         module.mock.flush_calls == 1 && module.mock.mix == 0.125);
   std::FILE* state_file = std::fopen(state_path.c_str(), "rb");
   long state_size = -1;
   if (state_file != nullptr) {
      std::fseek(state_file, 0, SEEK_END);
      state_size = std::ftell(state_file);
      std::fclose(state_file);
   }
   check("state: saved file holds the plugin's bytes", state_size == static_cast<long>(sizeof(double) * 2));

   module.mock.gain = 1.5;
   module.mock.mix = 0.0;
   const bool restored = host.loadState(state_path);
   check("state: load restores the plugin state", restored, host.lastError());
   check("state: restored values are visible to the host",
         module.mock.gain == 0.5 && module.mock.mix == 0.125 && host.getParam(kGain) == 0.5);
   const bool missing_state = host.loadState("/tmp/mnkygain_does_not_exist.state");
   check("state: loading a missing file fails cleanly", !missing_state, host.lastError());

   host.deactivate();
   check("deactivate: stop_processing and deactivate ran",
         module.mock.stop_calls == 1 && module.mock.deactivate_calls == 1);
   const bool inactive_process = host.process(block.channels(), 2, block.channels(), 2, 32);
   check("deactivate: process is refused while inactive", !inactive_process, host.lastError());

   host.unload();
   check("unload: plugin destroyed once", module.mock.destroy_calls == 1);
   check("unload: module deinit ran once", module.module_deinit_calls == 1);
   check("unload: host reports no plugin", !host.loaded());
   host.unload();
   check("unload: second unload is a no-op",
         module.mock.destroy_calls == 1 && module.module_deinit_calls == 1);
   std::remove(state_path.c_str());
}

void test_bundle_layout() {
   const std::string bundle = "/tmp/mnkygain_test_bundle.clap";
   std::error_code error;
   std::filesystem::remove_all(bundle, error);
   std::filesystem::create_directories(bundle + "/Contents/MacOS", error);
   std::filesystem::create_directories(bundle + "/Contents/Resources", error);
   std::FILE* binary = std::fopen((bundle + "/Contents/MacOS/mnkygain").c_str(), "wb");
   if (binary != nullptr) std::fclose(binary);

   check("bundle: a plain file path is used as-is",
         Host::bundleBinaryPath("/tmp/mnkygain.dylib") == "/tmp/mnkygain.dylib");
   check("bundle: a missing path is used as-is (dlopen reports it)",
         Host::bundleBinaryPath("/tmp/mnkygain_missing.clap") == "/tmp/mnkygain_missing.clap");
   check("bundle: Contents/MacOS is resolved inside a .clap directory",
         Host::bundleBinaryPath(bundle) == bundle + "/Contents/MacOS/mnkygain",
         Host::bundleBinaryPath(bundle));

   const std::string empty_bundle = "/tmp/mnkygain_empty.clap";
   std::filesystem::remove_all(empty_bundle, error);
   std::filesystem::create_directories(empty_bundle + "/Contents/Resources", error);
   check("bundle: a bundle without a binary resolves to nothing",
         Host::bundleBinaryPath(empty_bundle).empty());

   Host host;
   const bool loaded = host.load(bundle);
   check("bundle: loading a bundle without a valid module fails",
         !loaded && host.lastError().find("Contents/MacOS") != std::string::npos, host.lastError());
   std::filesystem::remove_all(bundle, error);
   std::filesystem::remove_all(empty_bundle, error);
}

void test_missing_extensions() {
   TestModule module;
   module.mock.provide_params = false;
   module.mock.provide_state = false;
   module.mock.provide_audio_ports = false;
   Host host;
   const bool loaded = host.loadInProcess(hooks_of(module), "/tmp/mnkygain.bare.clap");
   check("missing-ext: plugin without params/state/audio-ports still loads", loaded, host.lastError());
   check("missing-ext: params() is empty", host.params().empty());
   const bool param_write = host.setParam(kGain, 0.5);
   check("missing-ext: setParam fails", !param_write, host.lastError());
   const bool saved = host.saveState("/tmp/mnkygain.none");
   check("missing-ext: saveState fails", !saved, host.lastError());
   const bool loaded_state = host.loadState("/tmp/mnkygain.none");
   check("missing-ext: loadState fails", !loaded_state, host.lastError());
   const bool activated = host.activate(48000.0, kMaxFrames);
   check("missing-ext: activate fails without audio-ports", !activated, host.lastError());
   Block block(32);
   const bool processed = host.process(block.channels(), 2, block.channels(), 2, 32);
   check("missing-ext: process fails while inactive", !processed, host.lastError());
   host.unload();
   check("missing-ext: unload still destroys the plugin", module.mock.destroy_calls == 1);
}

void test_audio_only_plugin() {
   TestModule module;
   module.mock.provide_params = false;
   module.mock.provide_state = false;
   Host host;
   const bool loaded = host.loadInProcess(hooks_of(module), "/tmp/mnkygain.stereo.clap");
   check("audio-only: load succeeds", loaded, host.lastError());
   const bool activated = host.activate(44100.0, 64);
   check("audio-only: activate succeeds", activated, host.lastError());
   Block block(64);
   const bool processed = host.process(block.channels(), 2, block.channels(), 2, 64);
   check("audio-only: process succeeds", processed, host.lastError());
   host.unload();
}

void test_failure_paths() {
   {
      TestModule module;
      module.mock.fail_init = true;
      Host host;
      const bool loaded = host.loadInProcess(hooks_of(module), "/tmp/mnkygain.bad.clap");
      check("fail: plugin init rejection fails load", !loaded, host.lastError());
      check("fail: rejected plugin is destroyed",
            module.mock.destroy_calls == 1 && module.module_deinit_calls == 1);
   }
   {
      TestModule module;
      module.omit_factory = true;
      Host host;
      const bool loaded = host.loadInProcess(hooks_of(module), "/tmp/mnkygain.nofactory.clap");
      check("fail: module without factory fails load", !loaded, host.lastError());
   }
   {
      Host host;
      const ModuleHooks empty_hooks{};
      const bool loaded = host.loadInProcess(empty_hooks, "/tmp/mnkygain.empty.clap");
      check("fail: empty module hooks fail load", !loaded, host.lastError());
   }
   {
      Host host;
      const bool loaded = host.load("/tmp/definitely-not-a-plugin.clap");
      check("fail: dlopen of a missing module fails", !loaded, host.lastError());
      check("fail: dlopen error is reported", host.lastError().find("dlopen") != std::string::npos,
            host.lastError());
      check("fail: a failed dlopen leaves the host unloaded", !host.loaded());
   }
   {
      TestModule module;
      module.mock.fail_activate = true;
      Host host;
      host.loadInProcess(hooks_of(module), "/tmp/mnkygain.noact.clap");
      const bool activated = host.activate(48000.0, 32);
      check("fail: plugin refusing activation fails activate", !activated, host.lastError());
      check("fail: a refused activation leaves the host inactive", !host.isActive());
   }
   {
      TestModule module;
      module.mock.fail_start_processing = true;
      Host host;
      host.loadInProcess(hooks_of(module), "/tmp/mnkygain.nostart.clap");
      const bool activated = host.activate(48000.0, 32);
      check("fail: start_processing refusal fails activate", !activated, host.lastError());
      check("fail: start_processing refusal deactivates again",
            !host.isActive() && module.mock.deactivate_calls == 1);
   }
   {
      TestModule module;
      module.mock.fail_process = true;
      Host host;
      host.loadInProcess(hooks_of(module), "/tmp/mnkygain.err.clap");
      host.activate(48000.0, 32);
      Block block(32);
      const bool processed = host.process(block.channels(), 2, block.channels(), 2, 32);
      check("fail: CLAP_PROCESS_ERROR is surfaced", !processed, host.lastError());
   }
   {
      TestModule module;
      module.mock.log_in_init = true;
      Host host;
      host.loadInProcess(hooks_of(module), "/tmp/mnkygain.log.clap");
      check("host: clap.log messages are captured", host.lastLog() == "mock plugin initialised",
            host.lastLog());
   }
}

}  // namespace

int main() {
   test_happy_path();
   test_bundle_layout();
   test_missing_extensions();
   test_audio_only_plugin();
   test_failure_paths();

#ifdef CLAP_ABI_STUB
   const char* headers = "CLAP_ABI_STUB test stand-in (test/clap_stub)";
#else
   const char* headers = "real <clap/clap.h>";
#endif
   std::printf("\n%s: clap host core, %d checks, %d failures, headers: %s\n",
               g_failures == 0 ? "PASS" : "FAIL", g_checks, g_failures, headers);
   return g_failures == 0 ? 0 : 1;
}
