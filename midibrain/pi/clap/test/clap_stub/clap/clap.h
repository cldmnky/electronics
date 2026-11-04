// TEST-ONLY ABI STAND-IN — NOT VENDORED CLAP, DO NOT SHIP.
//
// Why this file exists: `pi/third_party/clap/include` (free-audio/clap 1.2.10) is
// provided by the setup team's CMake FetchContent and is not checked in yet. Host
// and plugin sources therefore always `#include <clap/clap.h>`; the test build
// reaches this directory through `-Itest/clap_stub` when no real header tree is
// present, so nothing in production code knows about the stand-in.
//
// Rules:
//   * test-only: referenced exclusively from test/ (never from host.cpp or the
//     mnkygain plugin target),
//   * only the declarations the host core actually touches are mirrored,
//   * field order/types are copied from clap 1.2.10 headers (see NOTES.md for the
//     verification recipe); when the real headers land, build with
//     -DCLAP_HEADER_DIR=<clap include dir> and this file is simply not used.
//
// `CLAP_ABI_STUB` marks a translation unit that is compiled against this stand-in.

#pragma once

#ifndef CLAP_ABI_STUB
#define CLAP_ABI_STUB 1
#endif

#include <cstdint>

#ifndef CLAP_ABI
#define CLAP_ABI
#endif

#ifndef CLAP_EXPORT
#define CLAP_EXPORT __attribute__((visibility("default")))
#endif

enum {
   CLAP_NAME_SIZE = 256,
   CLAP_PATH_SIZE = 1024,
};

typedef uint32_t clap_id;
static constexpr clap_id CLAP_INVALID_ID = UINT32_MAX;

typedef struct clap_version {
   uint32_t major;
   uint32_t minor;
   uint32_t revision;
} clap_version_t;

#define CLAP_VERSION_MAJOR 1
#define CLAP_VERSION_MINOR 2
#define CLAP_VERSION_REVISION 10
#define CLAP_VERSION_INIT                                                                          \
   { (uint32_t)CLAP_VERSION_MAJOR, (uint32_t)CLAP_VERSION_MINOR, (uint32_t)CLAP_VERSION_REVISION }

static constexpr clap_version_t CLAP_VERSION = CLAP_VERSION_INIT;

static inline bool clap_version_is_compatible(const clap_version_t v) { return v.major >= 1; }

// ------------------------------------------------------------------- process

typedef struct clap_audio_buffer {
   float **data32;
   double **data64;
   uint32_t channel_count;
   uint32_t latency;
   uint64_t constant_mask;
} clap_audio_buffer_t;

typedef struct clap_event_header {
   uint32_t size;
   uint32_t time;
   uint16_t space_id;
   uint16_t type;
   uint32_t flags;
} clap_event_header_t;

static constexpr uint16_t CLAP_CORE_EVENT_SPACE_ID = 0;

enum {
   CLAP_EVENT_PARAM_VALUE = 5,
};

typedef struct clap_event_param_value {
   clap_event_header_t header;
   clap_id param_id;
   void *cookie;
   int32_t note_id;
   int16_t port_index;
   int16_t channel;
   int16_t key;
   double value;
} clap_event_param_value_t;

typedef struct clap_input_events {
   void *ctx;
   uint32_t(CLAP_ABI *size)(const struct clap_input_events *list);
   const clap_event_header_t *(CLAP_ABI *get)(const struct clap_input_events *list, uint32_t index);
} clap_input_events_t;

typedef struct clap_output_events {
   void *ctx;
   bool(CLAP_ABI *try_push)(const struct clap_output_events *list,
                            const clap_event_header_t *event);
} clap_output_events_t;

enum {
   CLAP_PROCESS_ERROR = 0,
   CLAP_PROCESS_CONTINUE = 1,
   CLAP_PROCESS_CONTINUE_IF_NOT_QUIET = 2,
   CLAP_PROCESS_TAIL = 3,
   CLAP_PROCESS_SLEEP = 4,
};
typedef int32_t clap_process_status;

typedef struct clap_process {
   int64_t steady_time;
   uint32_t frames_count;
   const void *transport; // real clap: const clap_event_transport_t *; host always passes null
   const clap_audio_buffer_t *audio_inputs;
   clap_audio_buffer_t *audio_outputs;
   uint32_t audio_inputs_count;
   uint32_t audio_outputs_count;
   const clap_input_events_t *in_events;
   const clap_output_events_t *out_events;
} clap_process_t;


// ----------------------------------------------------------------- host side

typedef struct clap_host {
   clap_version_t clap_version;
   void *host_data;
   const char *name;
   const char *vendor;
   const char *url;
   const char *version;
   const void *(CLAP_ABI *get_extension)(const struct clap_host *host, const char *extension_id);
   void(CLAP_ABI *request_restart)(const struct clap_host *host);
   void(CLAP_ABI *request_process)(const struct clap_host *host);
   void(CLAP_ABI *request_callback)(const struct clap_host *host);
} clap_host_t;

typedef struct clap_host_thread_check {
   bool(CLAP_ABI *is_main_thread)(const clap_host_t *host);
   bool(CLAP_ABI *is_audio_thread)(const clap_host_t *host);
} clap_host_thread_check_t;

typedef int32_t clap_log_severity;

typedef struct clap_host_log {
   void(CLAP_ABI *log)(const clap_host_t *host, clap_log_severity severity, const char *msg);
} clap_host_log_t;

static constexpr const char CLAP_EXT_THREAD_CHECK[] = "clap.thread-check";
static constexpr const char CLAP_EXT_LOG[] = "clap.log";

// ---------------------------------------------------------------- plugin side

typedef struct clap_plugin_descriptor {
   clap_version_t clap_version;
   const char *id;
   const char *name;
   const char *vendor;
   const char *url;
   const char *manual_url;
   const char *support_url;
   const char *version;
   const char *description;
   const char *const *features;
} clap_plugin_descriptor_t;

typedef struct clap_plugin {
   const clap_plugin_descriptor_t *desc;
   void *plugin_data;
   bool(CLAP_ABI *init)(const struct clap_plugin *plugin);
   void(CLAP_ABI *destroy)(const struct clap_plugin *plugin);
   bool(CLAP_ABI *activate)(const struct clap_plugin *plugin,
                            double sample_rate,
                            uint32_t min_frames_count,
                            uint32_t max_frames_count);
   void(CLAP_ABI *deactivate)(const struct clap_plugin *plugin);
   bool(CLAP_ABI *start_processing)(const struct clap_plugin *plugin);
   void(CLAP_ABI *stop_processing)(const struct clap_plugin *plugin);
   void(CLAP_ABI *reset)(const struct clap_plugin *plugin);
   clap_process_status(CLAP_ABI *process)(const struct clap_plugin *plugin,
                                          const clap_process_t *process);
   const void *(CLAP_ABI *get_extension)(const struct clap_plugin *plugin, const char *id);
   void(CLAP_ABI *on_main_thread)(const struct clap_plugin *plugin);
} clap_plugin_t;

typedef struct clap_plugin_entry {
   clap_version_t clap_version;
   bool(CLAP_ABI *init)(const char *plugin_path);
   void(CLAP_ABI *deinit)(void);
   const void *(CLAP_ABI *get_factory)(const char *factory_id);
} clap_plugin_entry_t;

typedef struct clap_plugin_factory {
   uint32_t(CLAP_ABI *get_plugin_count)(const struct clap_plugin_factory *factory);
   const clap_plugin_descriptor_t *(CLAP_ABI *get_plugin_descriptor)(
      const struct clap_plugin_factory *factory, uint32_t index);
   const clap_plugin_t *(CLAP_ABI *create_plugin)(const struct clap_plugin_factory *factory,
                                                  const clap_host_t *host,
                                                  const char *plugin_id);
} clap_plugin_factory_t;

static constexpr const char CLAP_PLUGIN_FACTORY_ID[] = "clap.plugin-factory";

// -------------------------------------------------------- extensions (subset)

static constexpr const char CLAP_EXT_PARAMS[] = "clap.params";
static constexpr const char CLAP_EXT_AUDIO_PORTS[] = "clap.audio-ports";
static constexpr const char CLAP_EXT_STATE[] = "clap.state";
static constexpr const char CLAP_PORT_MONO[] = "mono";
static constexpr const char CLAP_PORT_STEREO[] = "stereo";

enum {
   CLAP_PARAM_IS_STEPPED = 1 << 0,
   CLAP_PARAM_IS_HIDDEN = 1 << 2,
   CLAP_PARAM_IS_BYPASS = 1 << 4,
   CLAP_PARAM_IS_AUTOMATABLE = 1 << 5,
};
typedef uint32_t clap_param_info_flags;

typedef struct clap_param_info {
   clap_id id;
   clap_param_info_flags flags;
   void *cookie;
   char name[CLAP_NAME_SIZE];
   char module[CLAP_PATH_SIZE];
   double min_value;
   double max_value;
   double default_value;
} clap_param_info_t;

typedef struct clap_plugin_params {
   uint32_t(CLAP_ABI *count)(const clap_plugin_t *plugin);
   bool(CLAP_ABI *get_info)(const clap_plugin_t *plugin,
                            uint32_t param_index,
                            clap_param_info_t *param_info);
   bool(CLAP_ABI *get_value)(const clap_plugin_t *plugin, clap_id param_id, double *out_value);
   bool(CLAP_ABI *value_to_text)(const clap_plugin_t *plugin,
                                 clap_id param_id,
                                 double value,
                                 char *out_buffer,
                                 uint32_t out_buffer_capacity);
   bool(CLAP_ABI *text_to_value)(const clap_plugin_t *plugin,
                                 clap_id param_id,
                                 const char *param_value_text,
                                 double *out_value);
   void(CLAP_ABI *flush)(const clap_plugin_t *plugin,
                         const clap_input_events_t *in,
                         const clap_output_events_t *out);
} clap_plugin_params_t;

enum {
   CLAP_AUDIO_PORT_IS_MAIN = 1 << 0,
   CLAP_AUDIO_PORT_SUPPORTS_64BITS = 1 << 1,
   CLAP_AUDIO_PORT_PREFERS_64BITS = 1 << 2,
   CLAP_AUDIO_PORT_REQUIRES_COMMON_SAMPLE_SIZE = 1 << 3,
};

typedef struct clap_audio_port_info {
   clap_id id;
   char name[CLAP_NAME_SIZE];
   uint32_t flags;
   uint32_t channel_count;
   const char *port_type;
   clap_id in_place_pair;
} clap_audio_port_info_t;

typedef struct clap_plugin_audio_ports {
   uint32_t(CLAP_ABI *count)(const clap_plugin_t *plugin, bool is_input);
   bool(CLAP_ABI *get)(const clap_plugin_t *plugin,
                       uint32_t index,
                       bool is_input,
                       clap_audio_port_info_t *info);
} clap_plugin_audio_ports_t;

typedef struct clap_istream {
   void *ctx;
   int64_t(CLAP_ABI *read)(const struct clap_istream *stream, void *buffer, uint64_t size);
} clap_istream_t;

typedef struct clap_ostream {
   void *ctx;
   int64_t(CLAP_ABI *write)(const struct clap_ostream *stream, const void *buffer, uint64_t size);
} clap_ostream_t;

typedef struct clap_plugin_state {
   bool(CLAP_ABI *save)(const clap_plugin_t *plugin, const clap_ostream_t *stream);
   bool(CLAP_ABI *load)(const clap_plugin_t *plugin, const clap_istream_t *stream);
} clap_plugin_state_t;

#pragma message("CLAP ABI stub in use (test-only); host core is NOT built against real clap headers")
