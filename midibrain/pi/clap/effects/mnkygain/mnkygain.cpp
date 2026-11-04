// mnkygain — example CLAP stereo effect (gain + drive + tone + mix).
//
// Plain C++20, no framework: the plugin only depends on the CLAP headers and
// exports `clap_entry`. It is the reference effect for the mnkybox sends and the
// emulator; keep it small and allocation-free on the audio thread.

#include <clap/clap.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

constexpr double kPi = 3.14159265358979323846;

enum ParamId : clap_id {
   kGainId = 0,
   kDriveId = 1,
   kToneId = 2,
   kMixId = 3,
   kParamCount = 4,
};

struct ParamSpec {
   clap_id id;
   const char* name;
   double min_value;
   double max_value;
   double default_value;
};

const ParamSpec kParams[kParamCount] = {
   {kGainId, "Gain", -24.0, 24.0, 0.0},     // dB
   {kDriveId, "Drive", 0.0, 1.0, 0.0},      // 0..1 -> 1x..10x into tanh()
   {kToneId, "Tone", 200.0, 18000.0, 8000.0},  // one-pole low-pass, Hz
   {kMixId, "Mix", 0.0, 1.0, 1.0},          // dry/wet
};

constexpr uint32_t kStateMagic = 0x4d4b4741u;  // "MKGA"
constexpr uint32_t kStateVersion = 1;

struct MnkyGain {
   clap_plugin_t iface{};
   const clap_host_t* host = nullptr;
   double value[kParamCount] = {0.0, 0.0, 8000.0, 1.0};
   double sample_rate = 48000.0;
   float lowpass_state[2] = {0.0f, 0.0f};
};

MnkyGain& self(const clap_plugin_t* plugin) { return *static_cast<MnkyGain*>(plugin->plugin_data); }

double clamp_to(const ParamSpec& spec, double value) {
   if (value < spec.min_value) return spec.min_value;
   if (value > spec.max_value) return spec.max_value;
   return value;
}

void apply_param_events(MnkyGain& plugin, const clap_input_events_t* events) {
   if (events == nullptr || events->size == nullptr || events->get == nullptr) return;
   const uint32_t count = events->size(events);
   for (uint32_t index = 0; index < count; ++index) {
      const clap_event_header_t* header = events->get(events, index);
      if (header == nullptr) continue;
      if (header->space_id != CLAP_CORE_EVENT_SPACE_ID) continue;
      if (header->type != CLAP_EVENT_PARAM_VALUE) continue;
      const auto* event = reinterpret_cast<const clap_event_param_value_t*>(header);
      if (event->param_id >= kParamCount) continue;
      plugin.value[event->param_id] = clamp_to(kParams[event->param_id], event->value);
   }
}

// ---------------------------------------------------------------- plugin core

bool CLAP_ABI plugin_init(const clap_plugin_t*) { return true; }

void CLAP_ABI plugin_destroy(const clap_plugin_t* plugin) { delete &self(plugin); }

bool CLAP_ABI plugin_activate(const clap_plugin_t* plugin,
                              double sample_rate,
                              uint32_t /*min_frames_count*/,
                              uint32_t /*max_frames_count*/) {
   MnkyGain& instance = self(plugin);
   if (sample_rate <= 0.0) return false;
   instance.sample_rate = sample_rate;
   instance.lowpass_state[0] = 0.0f;
   instance.lowpass_state[1] = 0.0f;
   return true;
}

void CLAP_ABI plugin_deactivate(const clap_plugin_t*) {}

bool CLAP_ABI plugin_start_processing(const clap_plugin_t*) { return true; }

void CLAP_ABI plugin_stop_processing(const clap_plugin_t*) {}

void CLAP_ABI plugin_reset(const clap_plugin_t* plugin) {
   MnkyGain& instance = self(plugin);
   instance.lowpass_state[0] = 0.0f;
   instance.lowpass_state[1] = 0.0f;
}

clap_process_status CLAP_ABI plugin_process(const clap_plugin_t* plugin,
                                            const clap_process_t* process) {
   MnkyGain& instance = self(plugin);
   apply_param_events(instance, process->in_events);

   if (process->audio_inputs_count != 1 || process->audio_outputs_count != 1) {
      return CLAP_PROCESS_ERROR;
   }
   const clap_audio_buffer_t& in = process->audio_inputs[0];
   clap_audio_buffer_t& out = process->audio_outputs[0];
   if (in.data32 == nullptr || out.data32 == nullptr) return CLAP_PROCESS_ERROR;

   const uint32_t channels = in.channel_count < out.channel_count ? in.channel_count : out.channel_count;
   if (channels == 0 || channels > 2) return CLAP_PROCESS_ERROR;

   // Parameters are block-static: coefficients are updated once per process().
   const double gain_linear = std::pow(10.0, instance.value[kGainId] / 20.0);
   const double drive_gain = 1.0 + 9.0 * instance.value[kDriveId];
   const double mix = instance.value[kMixId];
   const double cutoff = instance.value[kToneId];
   double lowpass = 1.0 - std::exp(-2.0 * kPi * cutoff / instance.sample_rate);
   if (!(lowpass > 0.0)) lowpass = 0.0;
   if (lowpass > 1.0) lowpass = 1.0;

   for (uint32_t channel = 0; channel < channels; ++channel) {
      const float* input = in.data32[channel];
      float* output = out.data32[channel];
      float state = instance.lowpass_state[channel];
      for (uint32_t frame = 0; frame < process->frames_count; ++frame) {
         const double dry = static_cast<double>(input[frame]);
         const double driven = std::tanh(dry * gain_linear * drive_gain);
         state += static_cast<float>(lowpass * (driven - static_cast<double>(state)));
         output[frame] = static_cast<float>(dry * (1.0 - mix) + static_cast<double>(state) * mix);
      }
      instance.lowpass_state[channel] = state;
   }
   return CLAP_PROCESS_CONTINUE;
}

// -------------------------------------------------------------- clap.params

uint32_t CLAP_ABI params_count(const clap_plugin_t*) { return kParamCount; }

bool CLAP_ABI params_get_info(const clap_plugin_t*, uint32_t index, clap_param_info_t* info) {
   if (info == nullptr || index >= kParamCount) return false;
   const ParamSpec& spec = kParams[index];
   *info = clap_param_info_t{};
   info->id = spec.id;
   info->flags = CLAP_PARAM_IS_AUTOMATABLE;
   info->cookie = nullptr;
   std::snprintf(info->name, sizeof(info->name), "%s", spec.name);
   info->module[0] = '\0';
   info->min_value = spec.min_value;
   info->max_value = spec.max_value;
   info->default_value = spec.default_value;
   return true;
}

bool CLAP_ABI params_get_value(const clap_plugin_t* plugin, clap_id id, double* out_value) {
   if (out_value == nullptr || id >= kParamCount) return false;
   *out_value = self(plugin).value[id];
   return true;
}

bool CLAP_ABI params_value_to_text(const clap_plugin_t*,
                                   clap_id id,
                                   double value,
                                   char* out_buffer,
                                   uint32_t capacity) {
   if (out_buffer == nullptr || capacity == 0 || id >= kParamCount) return false;
   switch (id) {
      case kGainId:
         std::snprintf(out_buffer, capacity, "%.1f dB", value);
         return true;
      case kDriveId:
         std::snprintf(out_buffer, capacity, "%.0f %%", value * 100.0);
         return true;
      case kToneId:
         std::snprintf(out_buffer, capacity, "%.0f Hz", value);
         return true;
      case kMixId:
         std::snprintf(out_buffer, capacity, "%.0f %%", value * 100.0);
         return true;
      default:
         return false;
   }
}

bool CLAP_ABI params_text_to_value(const clap_plugin_t*,
                                   clap_id id,
                                   const char* text,
                                   double* out_value) {
   if (text == nullptr || out_value == nullptr || id >= kParamCount) return false;
   char* end = nullptr;
   const double parsed = std::strtod(text, &end);
   if (end == text) return false;
   *out_value = clamp_to(kParams[id], parsed);
   return true;
}

void CLAP_ABI params_flush(const clap_plugin_t* plugin,
                           const clap_input_events_t* in,
                           const clap_output_events_t* /*out*/) {
   apply_param_events(self(plugin), in);
}

// ----------------------------------------------------------- clap.audio-ports

uint32_t CLAP_ABI ports_count(const clap_plugin_t*, bool /*is_input*/) { return 1; }

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

// ---------------------------------------------------------------- clap.state

bool write_all(const clap_ostream_t* stream, const void* data, uint64_t size) {
   const auto* bytes = static_cast<const unsigned char*>(data);
   uint64_t offset = 0;
   while (offset < size) {
      const int64_t written = stream->write(stream, bytes + offset, size - offset);
      if (written <= 0) return false;
      offset += static_cast<uint64_t>(written);
   }
   return true;
}

bool read_all(const clap_istream_t* stream, void* data, uint64_t size) {
   auto* bytes = static_cast<unsigned char*>(data);
   uint64_t offset = 0;
   while (offset < size) {
      const int64_t read = stream->read(stream, bytes + offset, size - offset);
      if (read <= 0) return false;
      offset += static_cast<uint64_t>(read);
   }
   return true;
}

bool CLAP_ABI state_save(const clap_plugin_t* plugin, const clap_ostream_t* stream) {
   if (stream == nullptr || stream->write == nullptr) return false;
   MnkyGain& instance = self(plugin);
   if (!write_all(stream, &kStateMagic, sizeof(kStateMagic))) return false;
   if (!write_all(stream, &kStateVersion, sizeof(kStateVersion))) return false;
   return write_all(stream, instance.value, sizeof(instance.value));
}

bool CLAP_ABI state_load(const clap_plugin_t* plugin, const clap_istream_t* stream) {
   if (stream == nullptr || stream->read == nullptr) return false;
   uint32_t magic = 0;
   uint32_t version = 0;
   double values[kParamCount] = {0.0, 0.0, 8000.0, 1.0};
   if (!read_all(stream, &magic, sizeof(magic)) || magic != kStateMagic) return false;
   if (!read_all(stream, &version, sizeof(version)) || version != kStateVersion) return false;
   if (!read_all(stream, values, sizeof(values))) return false;
   MnkyGain& instance = self(plugin);
   for (uint32_t index = 0; index < kParamCount; ++index) {
      instance.value[index] = clamp_to(kParams[index], values[index]);
   }
   return true;
}

clap_plugin_params_t g_params_ext{&params_count,   &params_get_info, &params_get_value,
                                 &params_value_to_text, &params_text_to_value, &params_flush};
clap_plugin_audio_ports_t g_audio_ports_ext{&ports_count, &ports_get};
clap_plugin_state_t g_state_ext{&state_save, &state_load};

const void* CLAP_ABI plugin_get_extension(const clap_plugin_t*, const char* id) {
   if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &g_params_ext;
   if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &g_audio_ports_ext;
   if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &g_state_ext;
   return nullptr;
}

void CLAP_ABI plugin_on_main_thread(const clap_plugin_t*) {}

const char* const kFeatures[] = {"audio-effect", "stereo", "utility", nullptr};

const clap_plugin_descriptor_t kDescriptor = {
   CLAP_VERSION_INIT,
   "com.cldmnky.mnkygain",
   "mnkygain",
   "cldmnky",
   "https://github.com/cldmnky/electronics",
   "",
   "",
   "0.1.0",
   "mnkybox example gain: gain, tanh drive, one-pole tone, dry/wet mix",
   kFeatures,
};

// ---------------------------------------------------------------- factory/api

uint32_t CLAP_ABI factory_get_plugin_count(const clap_plugin_factory_t*) { return 1; }

const clap_plugin_descriptor_t* CLAP_ABI factory_get_plugin_descriptor(const clap_plugin_factory_t*,
                                                                      uint32_t index) {
   return index == 0 ? &kDescriptor : nullptr;
}

const clap_plugin_t* CLAP_ABI factory_create_plugin(const clap_plugin_factory_t*,
                                                    const clap_host_t* host,
                                                    const char* plugin_id) {
   if (plugin_id == nullptr || std::strcmp(plugin_id, kDescriptor.id) != 0) return nullptr;
   auto* instance = new MnkyGain();
   instance->host = host;
   for (uint32_t index = 0; index < kParamCount; ++index) {
      instance->value[index] = kParams[index].default_value;
   }
   instance->iface.desc = &kDescriptor;
   instance->iface.plugin_data = instance;
   instance->iface.init = &plugin_init;
   instance->iface.destroy = &plugin_destroy;
   instance->iface.activate = &plugin_activate;
   instance->iface.deactivate = &plugin_deactivate;
   instance->iface.start_processing = &plugin_start_processing;
   instance->iface.stop_processing = &plugin_stop_processing;
   instance->iface.reset = &plugin_reset;
   instance->iface.process = &plugin_process;
   instance->iface.get_extension = &plugin_get_extension;
   instance->iface.on_main_thread = &plugin_on_main_thread;
   return &instance->iface;
}

const clap_plugin_factory_t kFactory = {&factory_get_plugin_count, &factory_get_plugin_descriptor,
                                        &factory_create_plugin};

}  // namespace

// init/deinit must tolerate repeated balanced calls (CLAP 1.2); this module has
// no global state to set up, so both are no-ops.
static bool CLAP_ABI entry_init(const char* /*plugin_path*/) { return true; }
static void CLAP_ABI entry_deinit() {}

static const void* CLAP_ABI entry_get_factory(const char* factory_id) {
   if (factory_id != nullptr && std::strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID) == 0) {
      return &kFactory;
   }
   return nullptr;
}

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
   CLAP_VERSION_INIT,
   &entry_init,
   &entry_deinit,
   &entry_get_factory,
};
