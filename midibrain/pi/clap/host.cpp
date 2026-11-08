#include "host.hpp"

#include <dlfcn.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <utility>

namespace mnky::clap {
namespace {

std::string boundedString(const char* text, size_t capacity) {
   size_t length = 0;
   while (length < capacity && text[length] != '\0') ++length;
   return std::string(text, length);
}

// Per-block event list handed to clap_plugin::process() / params->flush().
// Parameter writes are latched by the host, so the list is rebuilt every block
// and the events carry frame offset 0.
struct InputEvents {
   std::vector<clap_event_param_value_t> events;

   static uint32_t CLAP_ABI count(const clap_input_events_t* list) {
      return static_cast<uint32_t>(static_cast<const InputEvents*>(list->ctx)->events.size());
   }

   static const clap_event_header_t* CLAP_ABI at(const clap_input_events_t* list, uint32_t index) {
      const auto* self = static_cast<const InputEvents*>(list->ctx);
      if (index >= self->events.size()) return nullptr;
      return &self->events[index].header;
   }

   clap_input_events_t view() { return clap_input_events_t{this, &count, &at}; }
};

// Plugin -> host events are accepted and counted, then dropped: this host has no
// automation recorder.
struct OutputSink {
   uint32_t dropped = 0;

   static bool CLAP_ABI push(const clap_output_events_t* list, const clap_event_header_t* event) {
      if (event == nullptr) return false;
      static_cast<OutputSink*>(list->ctx)->dropped++;
      return true;
   }

   clap_output_events_t view() { return clap_output_events_t{this, &push}; }
};

int64_t CLAP_ABI writeFile(const clap_ostream_t* stream, const void* buffer, uint64_t size) {
   if (stream == nullptr || stream->ctx == nullptr) return -1;
   return static_cast<int64_t>(
      std::fwrite(buffer, 1, static_cast<size_t>(size), static_cast<std::FILE*>(stream->ctx)));
}

int64_t CLAP_ABI readFile(const clap_istream_t* stream, void* buffer, uint64_t size) {
   if (stream == nullptr || stream->ctx == nullptr) return -1;
   std::FILE* file = static_cast<std::FILE*>(stream->ctx);
   const size_t read = std::fread(buffer, 1, static_cast<size_t>(size), file);
   if (read == 0 && std::ferror(file) != 0) return -1;
   return static_cast<int64_t>(read);
}


}  // namespace

Host::Host() {
   host_.clap_version = CLAP_VERSION;
   host_.host_data = this;
   host_.name = "mnkybox";
   host_.vendor = "cldmnky";
   host_.url = "";
   host_.version = "0.1";
   host_.get_extension = &Host::hostGetExtension;
   host_.request_restart = &Host::hostRequestRestart;
   host_.request_process = &Host::hostRequestProcess;
   host_.request_callback = &Host::hostRequestCallback;

   thread_check_.is_main_thread = &Host::hostIsMainThread;
   thread_check_.is_audio_thread = &Host::hostIsAudioThread;
   log_.log = &Host::hostLog;
}

Host::~Host() { unload(); }

std::string Host::bundleBinaryPath(const std::string& path) {
   std::error_code error;
   if (!std::filesystem::is_directory(path, error) || error) return path;

   const std::filesystem::path macos_dir =
      std::filesystem::path(path) / "Contents" / "MacOS";
   std::filesystem::directory_iterator entry(macos_dir, error);
   if (error) return std::string();
   for (const auto& candidate : entry) {
      std::error_code candidate_error;
      if (candidate.is_regular_file(candidate_error) && !candidate_error) {
         return candidate.path().string();
      }
   }
   return std::string();
}

bool Host::load(const std::string& path, const char* plugin_id) {
   if (loaded()) return fail("load: a plugin is already loaded");
   const std::string binary = bundleBinaryPath(path);
   if (binary.empty()) return fail("load: no module binary inside " + path);
   void* handle = dlopen(binary.c_str(), RTLD_NOW | RTLD_LOCAL);
   if (handle == nullptr) {
      const char* error = dlerror();
      return fail(std::string("load: dlopen failed for ") + binary + ": " +
                  (error != nullptr ? error : "unknown error"));
   }
   const auto* entry = static_cast<const clap_plugin_entry_t*>(dlsym(handle, "clap_entry"));
   if (entry == nullptr) {
      dlclose(handle);
      return fail("load: " + path + " does not export clap_entry");
   }
   if (!clap_version_is_compatible(entry->clap_version)) {
      dlclose(handle);
      return fail("load: " + path + " was built against an incompatible CLAP version");
   }
   dl_handle_ = handle;
   const ModuleHooks hooks{entry->init, entry->deinit, entry->get_factory};
   return openModule(hooks, path, plugin_id);
}

bool Host::loadInProcess(const ModuleHooks& hooks,
                         const std::string& plugin_path,
                         const char* plugin_id) {
   if (loaded()) return fail("loadInProcess: a plugin is already loaded");
   return openModule(hooks, plugin_path, plugin_id);
}

bool Host::openModule(const ModuleHooks& hooks,
                      const std::string& plugin_path,
                      const char* plugin_id) {
   hooks_ = hooks;
   if (hooks_.init == nullptr || hooks_.getFactory == nullptr) {
      unload();
      return fail("load: module entry points are incomplete");
   }
   if (!hooks_.init(plugin_path.c_str())) {
      unload();
      return fail("load: module init() failed");
   }
   module_inited_ = true;

   const auto* factory =
      static_cast<const clap_plugin_factory_t*>(hooks_.getFactory(CLAP_PLUGIN_FACTORY_ID));
   if (factory == nullptr || factory->get_plugin_count == nullptr ||
       factory->get_plugin_descriptor == nullptr || factory->create_plugin == nullptr) {
      unload();
      return fail("load: module does not provide clap.plugin-factory");
   }
   const uint32_t plugin_count = factory->get_plugin_count(factory);
   if (plugin_count == 0) {
      unload();
      return fail("load: plugin factory is empty");
   }

   const clap_plugin_descriptor_t* descriptor = nullptr;
   if (plugin_id == nullptr) {
      descriptor = factory->get_plugin_descriptor(factory, 0);
   } else {
      for (uint32_t index = 0; index < plugin_count; ++index) {
         const clap_plugin_descriptor_t* candidate = factory->get_plugin_descriptor(factory, index);
         if (candidate != nullptr && candidate->id != nullptr &&
             std::strcmp(candidate->id, plugin_id) == 0) {
            descriptor = candidate;
            break;
         }
      }
   }
   if (descriptor == nullptr || descriptor->id == nullptr || descriptor->id[0] == '\0') {
      unload();
      return fail("load: no matching plugin descriptor");
   }

   plugin_ = factory->create_plugin(factory, &host_, descriptor->id);
   if (plugin_ == nullptr) {
      unload();
      return fail("load: create_plugin() failed");
   }
   if (plugin_->init == nullptr || plugin_->destroy == nullptr || plugin_->process == nullptr ||
       !plugin_->init(plugin_)) {
      unload();
      return fail("load: plugin init() failed");
   }

   cacheExtensions();
   id_ = descriptor->id;
   name_ = descriptor->name != nullptr ? descriptor->name : "";
   return true;
}

void Host::cacheExtensions() {
   params_ext_ = static_cast<const clap_plugin_params_t*>(plugin_->get_extension(plugin_, CLAP_EXT_PARAMS));
   audio_ports_ext_ =
      static_cast<const clap_plugin_audio_ports_t*>(plugin_->get_extension(plugin_, CLAP_EXT_AUDIO_PORTS));
   state_ext_ = static_cast<const clap_plugin_state_t*>(plugin_->get_extension(plugin_, CLAP_EXT_STATE));
}

void Host::unload() {
   if (plugin_ != nullptr) {
      deactivate();
      plugin_->destroy(plugin_);
      plugin_ = nullptr;
   }
   params_ext_ = nullptr;
   audio_ports_ext_ = nullptr;
   state_ext_ = nullptr;
   pending_.clear();
   id_.clear();
   name_.clear();
   active_ = false;
   processing_ = false;
   sample_rate_ = 0.0;
   max_frames_ = 0;
   in_channels_ = 0;
   out_channels_ = 0;
   steady_time_ = 0;
   if (module_inited_ && hooks_.deinit != nullptr) hooks_.deinit();
   module_inited_ = false;
   hooks_ = ModuleHooks{};
   if (dl_handle_ != nullptr) {
      dlclose(dl_handle_);
      dl_handle_ = nullptr;
   }
}

std::vector<Param> Host::params() const {
   std::vector<Param> result;
   if (plugin_ == nullptr || params_ext_ == nullptr || params_ext_->count == nullptr) return result;

   const uint32_t count = params_ext_->count(plugin_);
   result.reserve(count);
   for (uint32_t index = 0; index < count; ++index) {
      clap_param_info_t info{};
      if (params_ext_->get_info == nullptr || !params_ext_->get_info(plugin_, index, &info)) continue;
      Param param;
      param.id = info.id;
      param.name = boundedString(info.name, CLAP_NAME_SIZE);
      param.min = info.min_value;
      param.max = info.max_value;
      param.value = info.default_value;
      double value = 0.0;
      if (params_ext_->get_value != nullptr && params_ext_->get_value(plugin_, info.id, &value)) {
         param.value = value;
      }
      result.push_back(std::move(param));
   }
   return result;
}

bool Host::setParam(clap_id id, double value) {
   if (plugin_ == nullptr) return fail("setParam: no plugin loaded");
   if (params_ext_ == nullptr || params_ext_->get_info == nullptr) {
      return fail("setParam: plugin has no clap.params extension");
   }

   bool known = false;
   double minimum = 0.0;
   double maximum = 0.0;
   const uint32_t count = params_ext_->count(plugin_);
   for (uint32_t index = 0; index < count; ++index) {
      clap_param_info_t info{};
      if (!params_ext_->get_info(plugin_, index, &info)) continue;
      if (info.id != id) continue;
      known = true;
      minimum = info.min_value;
      maximum = info.max_value;
      break;
   }
   if (!known) return fail("setParam: unknown parameter id");

   const double clamped = value < minimum ? minimum : (value > maximum ? maximum : value);
   for (auto& pending : pending_) {
      if (pending.id == id) {
         pending.value = clamped;
         return true;
      }
   }
   pending_.push_back(PendingParam{id, clamped});
   return true;
}

double Host::getParam(clap_id id) const {
   for (const auto& pending : pending_) {
      if (pending.id == id) return pending.value;
   }
   double value = 0.0;
   if (plugin_ != nullptr && params_ext_ != nullptr && params_ext_->get_value != nullptr &&
       params_ext_->get_value(plugin_, id, &value)) {
      return value;
   }
   return 0.0;
}

bool Host::saveState(const std::string& path) {
   if (plugin_ == nullptr) return fail("saveState: no plugin loaded");
   if (state_ext_ == nullptr || state_ext_->save == nullptr) {
      return fail("saveState: plugin has no clap.state extension");
   }
   publishPendingParams();

   std::FILE* file = std::fopen(path.c_str(), "wb");
   if (file == nullptr) return fail("saveState: cannot open " + path + " for writing");
   clap_ostream_t stream{file, &writeFile};
   const bool saved = state_ext_->save(plugin_, &stream);
   const bool closed = std::fclose(file) == 0;
   if (!saved) return fail("saveState: plugin save() failed");
   if (!closed) return fail("saveState: writing " + path + " failed");
   return true;
}

bool Host::loadState(const std::string& path) {
   if (plugin_ == nullptr) return fail("loadState: no plugin loaded");
   if (state_ext_ == nullptr || state_ext_->load == nullptr) {
      return fail("loadState: plugin has no clap.state extension");
   }

   std::FILE* file = std::fopen(path.c_str(), "rb");
   if (file == nullptr) return fail("loadState: cannot open " + path + " for reading");
   clap_istream_t stream{file, &readFile};
   const bool loaded_state = state_ext_->load(plugin_, &stream);
   std::fclose(file);
   if (!loaded_state) return fail("loadState: plugin load() failed");
   pending_.clear();  // the plugin's own values now win
   return true;
}

bool Host::activate(double sample_rate, uint32_t max_frames) {
   if (plugin_ == nullptr) return fail("activate: no plugin loaded");
   if (active_) return fail("activate: plugin is already active");
   if (audio_ports_ext_ == nullptr) {
      return fail("activate: plugin has no clap.audio-ports extension");
   }
   if (sample_rate <= 0.0 || max_frames == 0) {
      return fail("activate: invalid sample rate or block size");
   }
   if (audio_ports_ext_->count(plugin_, true) != 1 ||
       audio_ports_ext_->count(plugin_, false) != 1) {
      return fail("activate: host supports exactly one input and one output port");
   }

   clap_audio_port_info_t info{};
   if (!audio_ports_ext_->get(plugin_, 0, true, &info) || info.channel_count == 0) {
      return fail("activate: cannot read the input port");
   }
   in_channels_ = info.channel_count;
   info = clap_audio_port_info_t{};
   if (!audio_ports_ext_->get(plugin_, 0, false, &info) || info.channel_count == 0) {
      in_channels_ = 0;
      return fail("activate: cannot read the output port");
   }
   out_channels_ = info.channel_count;

   if (plugin_->activate == nullptr || !plugin_->activate(plugin_, sample_rate, 1, max_frames)) {
      in_channels_ = 0;
      out_channels_ = 0;
      return fail("activate: plugin refused activation");
   }
   active_ = true;
   sample_rate_ = sample_rate;
   max_frames_ = max_frames;
   steady_time_ = 0;
   if (plugin_->start_processing != nullptr && !plugin_->start_processing(plugin_)) {
      plugin_->deactivate(plugin_);
      active_ = false;
      in_channels_ = 0;
      out_channels_ = 0;
      return fail("activate: plugin refused start_processing");
   }
   processing_ = true;
   return true;
}

void Host::deactivate() {
   if (plugin_ == nullptr || !active_) return;
   if (processing_ && plugin_->stop_processing != nullptr) plugin_->stop_processing(plugin_);
   plugin_->deactivate(plugin_);
   active_ = false;
   processing_ = false;
   in_channels_ = 0;
   out_channels_ = 0;
}

bool Host::process(float* const* inputs,
                   uint32_t input_channels,
                   float* const* outputs,
                   uint32_t output_channels,
                   uint32_t frames) {
   if (plugin_ == nullptr) return fail("process: no plugin loaded");
   if (!active_) return fail("process: plugin is not active");
   if (inputs == nullptr || outputs == nullptr) return fail("process: null buffer");
   if (frames == 0) return fail("process: zero frames");
   if (frames > max_frames_) return fail("process: block larger than the activated maximum");
   if (input_channels != in_channels_ || output_channels != out_channels_) {
      return fail("process: channel count does not match the plugin's audio ports");
   }

   InputEvents input_events;
   fillPendingEvents(input_events.events);
   OutputSink output_sink;
   clap_input_events_t input_view = input_events.view();
   clap_output_events_t output_view = output_sink.view();

   clap_audio_buffer_t audio_in{};
   audio_in.data32 = const_cast<float**>(inputs);  // CLAP reads inputs, never writes them
   audio_in.channel_count = input_channels;
   clap_audio_buffer_t audio_out{};
   audio_out.data32 = const_cast<float**>(outputs);
   audio_out.channel_count = output_channels;

   clap_process_t block{};
   block.steady_time = steady_time_;
   block.frames_count = frames;
   block.transport = nullptr;
   block.audio_inputs = &audio_in;
   block.audio_outputs = &audio_out;
   block.audio_inputs_count = 1;
   block.audio_outputs_count = 1;
   block.in_events = &input_view;
   block.out_events = &output_view;

   const clap_process_status status = plugin_->process(plugin_, &block);
   dropped_output_events_ += output_sink.dropped;
   if (status <= CLAP_PROCESS_ERROR) {
      return fail("process: plugin reported CLAP_PROCESS_ERROR");
   }

   steady_time_ += frames;
   pending_.clear();
   return true;
}

void Host::fillPendingEvents(std::vector<clap_event_param_value_t>& out) const {
   out.clear();
   out.reserve(pending_.size());
   for (const auto& param : pending_) {
      clap_event_param_value_t event{};
      event.header.size = sizeof(clap_event_param_value_t);
      event.header.time = 0;
      event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
      event.header.type = CLAP_EVENT_PARAM_VALUE;
      event.param_id = param.id;
      event.cookie = nullptr;
      event.note_id = -1;   // wildcard: no note/port/channel/key targeting
      event.port_index = -1;
      event.channel = -1;
      event.key = -1;
      event.value = param.value;
      out.push_back(event);
   }
}

void Host::publishPendingParams() {
   if (pending_.empty() || plugin_ == nullptr || params_ext_ == nullptr ||
       params_ext_->flush == nullptr) {
      return;
   }
   InputEvents input_events;
   fillPendingEvents(input_events.events);
   OutputSink output_sink;
   clap_input_events_t input_view = input_events.view();
   clap_output_events_t output_view = output_sink.view();
   params_ext_->flush(plugin_, &input_view, &output_view);
   dropped_output_events_ += output_sink.dropped;
   pending_.clear();
}

bool Host::takeRestartRequest() {
   const bool requested = restart_requests_ != 0;
   restart_requests_ = 0;
   return requested;
}

bool Host::takeProcessRequest() {
   const bool requested = process_requests_ != 0;
   process_requests_ = 0;
   return requested;
}

bool Host::takeCallbackRequest() {
   const bool requested = callback_requests_ != 0;
   callback_requests_ = 0;
   return requested;
}

bool Host::fail(const std::string& message) {
   last_error_ = message;
   return false;
}

const void* Host::hostGetExtension(const clap_host_t* host, const char* extension_id) {
   if (host == nullptr || host->host_data == nullptr || extension_id == nullptr) return nullptr;
   const auto* self = static_cast<const Host*>(host->host_data);
   if (std::strcmp(extension_id, CLAP_EXT_THREAD_CHECK) == 0) return &self->thread_check_;
   if (std::strcmp(extension_id, CLAP_EXT_LOG) == 0) return &self->log_;
   return nullptr;  // no host params/audio-ports/state/timer extensions
}

void Host::hostRequestRestart(const clap_host_t* host) {
   if (host == nullptr || host->host_data == nullptr) return;
   static_cast<Host*>(host->host_data)->restart_requests_++;
}

void Host::hostRequestProcess(const clap_host_t* host) {
   if (host == nullptr || host->host_data == nullptr) return;
   static_cast<Host*>(host->host_data)->process_requests_++;
}

void Host::hostRequestCallback(const clap_host_t* host) {
   if (host == nullptr || host->host_data == nullptr) return;
   static_cast<Host*>(host->host_data)->callback_requests_++;
}

bool Host::hostIsMainThread(const clap_host_t*) { return true; }

bool Host::hostIsAudioThread(const clap_host_t*) { return true; }

void Host::hostLog(const clap_host_t* host, clap_log_severity, const char* message) {
   if (host == nullptr || host->host_data == nullptr || message == nullptr) return;
   static_cast<Host*>(host->host_data)->last_log_ = message;
}

}  // namespace mnky::clap
