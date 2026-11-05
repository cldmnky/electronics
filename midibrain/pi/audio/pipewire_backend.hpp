#pragma once
// Native PipeWire backend: one SPA audio stream per direction on a single
// pw_main_loop thread, node name "mnkybox", 48 kHz float32 device frames. The
// engine block size is requested as the graph quantum (128/48000) and the
// negotiated period is reported, never assumed.
#include <memory>
#include <string>

#include "device_backend.hpp"

namespace mnky::audio {

/// True when this build compiled against libpipewire.
bool pipewireCompiled();

/// PipeWire sinks/sources with their real node.name and node.description.
DeviceInventory enumeratePipeWireDevices();

/// Null when not compiled in; `error` names the missing dependency.
std::unique_ptr<DeviceBackend> createPipeWireBackend(std::string* error);

}  // namespace mnky::audio
