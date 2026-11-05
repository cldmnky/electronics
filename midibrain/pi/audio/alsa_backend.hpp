#pragma once
// snd_pcm fallback backend: the product path is PipeWire, ALSA is used when no
// PipeWire server is reachable or the settings ask for it. Opens `hw:` first
// (exact period control) and falls back to `plughw:` when the card does not
// support one of the negotiated formats/channels/rate.
#include <memory>
#include <string>

#include "device_backend.hpp"

namespace mnky::audio {

/// True when this build compiled against libasound.
bool alsaCompiled();

/// ALSA endpoints with real card id, PCM name and reported channel widths.
/// Never throws; an unusable card contributes nothing.
DeviceInventory enumerateAlsaDevices();

/// Null when not compiled in; `error` names the missing dependency.
std::unique_ptr<DeviceBackend> createAlsaBackend(std::string* error);

}  // namespace mnky::audio
