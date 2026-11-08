#pragma once
// Audio backend selection model for the Qt 6 app: the System screen's
// default/device/emulator choice, the persisted audio/backend key, the
// requested period size and the fallback rules used when a device is gone or a
// platform has no output at all. Qt-free so the same rules run in the app and
// under `mnkybox --unittests` (logic_tests.cpp).
//
// The audio path itself is native: libpipewire (product) or ALSA snd_pcm
// (fallback), see audio/device_backend.hpp. Qt Multimedia is not part of audio
// I/O; it stays a UI dependency only. The persisted key therefore selects
// the audio API explicitly:
//   "default"  -> PipeWire when a PipeWire sink exists, then ALSA, then emulator
//   "pipewire" -> native PipeWire streams
//   "alsa"     -> snd_pcm
//   "device"   -> one enumerated endpoint (its id carries the API)
//   "emulator" -> the clocked generator, always available
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace mnky::app {

enum class AudioBackend : std::uint8_t { Default, Device, Emulator, PipeWire, Alsa };

inline constexpr int kAudioBackendCount = 5;
inline constexpr AudioBackend kAudioBackends[kAudioBackendCount] = {
    AudioBackend::Default,
    AudioBackend::Device,
    AudioBackend::Emulator,
    AudioBackend::PipeWire,
    AudioBackend::Alsa,
};

// The platform default is what a fresh install should use: PipeWire first,
// then ALSA. The emulator generator is an explicit choice and the documented
// fallback when no device opens.
inline constexpr AudioBackend kDefaultAudioBackend = AudioBackend::Default;

// Stable keys for QSettings (`audio/backend`), never localised.
inline constexpr std::string_view audioBackendKey(AudioBackend backend) {
  switch (backend) {
    case AudioBackend::Device:
      return "device";
    case AudioBackend::Emulator:
      return "emulator";
    case AudioBackend::PipeWire:
      return "pipewire";
    case AudioBackend::Alsa:
      return "alsa";
    case AudioBackend::Default:
      break;
  }
  return "default";
}

inline AudioBackend audioBackendFromKey(std::string_view key) {
  for (const auto backend : kAudioBackends) {
    if (audioBackendKey(backend) == key) return backend;
  }
  return kDefaultAudioBackend;
}

inline std::string audioBackendLabel(AudioBackend backend) {
  switch (backend) {
    case AudioBackend::Device:
      return "Audio device";
    case AudioBackend::Emulator:
      return "Emulator generator";
    case AudioBackend::PipeWire:
      return "PipeWire";
    case AudioBackend::Alsa:
      return "ALSA";
    case AudioBackend::Default:
      break;
  }
  return "Default output";
}

// Menu text. An unavailable item keeps its name and says so, because a silently
// missing entry is indistinguishable from a build that never had the backend.
inline std::string audioBackendLabel(AudioBackend backend, bool available) {
  return available ? audioBackendLabel(backend) : audioBackendLabel(backend) + " (unavailable)";
}

// What the platform currently offers: a default output plus the enumerated
// explicit output/input device ids. The ids are prefixed with the audio API
// ("pipewire:<node.name>" / "alsa:<pcm>"), so selecting one device also selects
// the API that opens it; the per-API flags decide the default and the
// pipewire->alsa fallback. The emulator is always available.
struct AudioBackendAvailability {
  bool defaultOutput = false;  // any usable default output (PipeWire or ALSA)
  bool pipewireOutput = false;
  bool pipewireInput = false;
  bool alsaOutput = false;
  bool alsaInput = false;
  std::vector<std::string> outputDeviceIds;
  std::vector<std::string> inputDeviceIds;
  // Real per-device USB MIDI names (RtMidi client/port names grouped per
  // device), so the System device list can show "UMC1820" instead of one
  // generic "USB" label. Never an audio endpoint; the MIDI engine owns I/O.
  std::vector<std::string> midiInputNames;
  std::vector<std::string> midiOutputNames;
};

inline bool audioBackendAvailable(AudioBackend backend,
                                  const AudioBackendAvailability& availability) {
  switch (backend) {
    case AudioBackend::Device:
      return !availability.outputDeviceIds.empty();
    case AudioBackend::Emulator:
      return true;
    case AudioBackend::PipeWire:
      return availability.pipewireOutput;
    case AudioBackend::Alsa:
      return availability.alsaOutput;
    case AudioBackend::Default:
      break;
  }
  return availability.defaultOutput;
}

struct AudioBackendSelection {
  AudioBackend effective = kDefaultAudioBackend;
  std::string outputDeviceId;  // empty for Default/PipeWire/Alsa/Emulator
  bool fellBack = false;
  std::string note;  // why the request was not used ("" when it was)
};

// The API the "Default output" entry opens: native PipeWire first, then ALSA,
// then the emulator when the host has no output at all.
inline AudioBackend defaultOutputBackend(const AudioBackendAvailability& availability) {
  if (availability.pipewireOutput) return AudioBackend::PipeWire;
  if (availability.alsaOutput) return AudioBackend::Alsa;
  return AudioBackend::Emulator;
}

// The startup path and the Apply path both go through this: a request for a
// device that disappeared falls back to the default output, an explicit API
// that is not available falls back to the other API, and a machine with no
// output at all falls back to the emulator generator with the reason.
inline AudioBackendSelection resolveAudioBackend(
    AudioBackend requested, const std::string& requestedDeviceId,
    const AudioBackendAvailability& availability) {
  AudioBackendSelection selection;
  selection.effective = requested;
  if (requested == AudioBackend::Emulator) return selection;

  if (requested == AudioBackend::Device) {
    const bool known = !requestedDeviceId.empty() &&
                       std::find(availability.outputDeviceIds.begin(),
                                 availability.outputDeviceIds.end(),
                                 requestedDeviceId) != availability.outputDeviceIds.end();
    if (known) {
      selection.outputDeviceId = requestedDeviceId;
      return selection;
    }
    selection.fellBack = true;
    if (availability.defaultOutput) {
      selection.effective = AudioBackend::Default;
      selection.note = "output device unavailable; using the default output";
    } else {
      selection.effective = AudioBackend::Emulator;
      selection.note = "output device unavailable and no default output; using the "
                       "emulator generator";
    }
    return selection;
  }

  if (requested == AudioBackend::PipeWire || requested == AudioBackend::Alsa) {
    const bool available = audioBackendAvailable(requested, availability);
    if (available) return selection;
    selection.fellBack = true;
    const AudioBackend other =
        requested == AudioBackend::PipeWire ? AudioBackend::Alsa : AudioBackend::PipeWire;
    if (audioBackendAvailable(other, availability)) {
      selection.effective = other;
      selection.note = audioBackendLabel(requested) + " is unavailable; using " +
                       audioBackendLabel(other);
      return selection;
    }
    selection.effective = AudioBackend::Emulator;
    selection.note = audioBackendLabel(requested) +
                     " is unavailable and no audio output was found; using the emulator "
                     "generator";
    return selection;
  }

  if (availability.defaultOutput) return selection;
  selection.effective = AudioBackend::Emulator;
  selection.fellBack = true;
  selection.note = "no audio output device on this platform; using the emulator generator";
  return selection;
}

// Buffer sizes offered by the System screen. 128 is audio::kBlockSize, the
// engine's block size; the backend reports the period it actually negotiated,
// which the System live line shows instead of assuming the request was honoured.
inline constexpr int kAudioBufferSizes[] = {64, 128, 256, 512, 1024};
inline constexpr int kAudioBufferSizeCount =
    static_cast<int>(sizeof(kAudioBufferSizes) / sizeof(kAudioBufferSizes[0]));
inline constexpr int kDefaultAudioBufferSize = 128;

inline bool isAudioBufferSize(int frames) {
  for (const int size : kAudioBufferSizes) {
    if (size == frames) return true;
  }
  return false;
}

// Nearest offered size; a tie goes to the shorter period.
inline int clampAudioBufferSize(int frames) {
  int best = kAudioBufferSizes[0];
  int bestDistance = std::abs(frames - best);
  for (const int size : kAudioBufferSizes) {
    const int distance = std::abs(frames - size);
    if (distance < bestDistance) {
      best = size;
      bestDistance = distance;
    }
  }
  return best;
}

inline std::string audioBufferSizeLabel(int frames) {
  return std::to_string(clampAudioBufferSize(frames)) + " frames";
}

// The stored output device is used only while it is still listed; an unplugged
// or renamed device falls back to the type's default device (an empty id).
inline std::string resolveAudioDeviceName(std::string_view stored,
                                          const std::vector<std::string>& available) {
  for (const auto& name : available) {
    if (name == stored) return name;
  }
  return {};
}

// One persisted selection: backend, its explicit output/input device and the
// requested period size. Empty device ids mean "not selected".
struct AudioBackendChoice {
  AudioBackend backend = kDefaultAudioBackend;
  std::string outputDeviceId;
  std::string inputDeviceId;
  int bufferSize = kDefaultAudioBufferSize;
};

inline bool operator==(const AudioBackendChoice& a, const AudioBackendChoice& b) {
  return a.backend == b.backend && a.outputDeviceId == b.outputDeviceId &&
         a.inputDeviceId == b.inputDeviceId && a.bufferSize == b.bufferSize;
}

inline bool operator!=(const AudioBackendChoice& a, const AudioBackendChoice& b) {
  return !(a == b);
}

// RtMidi on ALSA names a port "<client>:<port> <card>:<device>", e.g.
// "UMC1820:UMC1820 MIDI 1 28:0"; CoreMIDI reports a bare name. Group the port
// list per device (the client part, or the whole name when there is no colon)
// so a device with several ports is listed once and nothing collapses into a
// single "USB" label.
inline std::vector<std::string> midiDeviceNamesFromPortNames(
    const std::vector<std::string>& portNames) {
  std::vector<std::string> devices;
  for (const auto& port : portNames) {
    std::string name = port;
    const std::size_t colon = name.find(':');
    if (colon != std::string::npos && colon > 0) name.resize(colon);
    while (!name.empty() && name.back() == ' ') name.pop_back();
    if (name.empty()) continue;
    if (std::find(devices.begin(), devices.end(), name) == devices.end()) {
      devices.push_back(std::move(name));
    }
  }
  return devices;
}

}  // namespace mnky::app
