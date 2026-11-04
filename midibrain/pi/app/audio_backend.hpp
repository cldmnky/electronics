#pragma once
// Audio backend selection model: the System screen's default/ALSA/JACK choice,
// the device name and buffer-size options, and the fallback rules used when a
// backend is not usable on this machine. "Not usable" covers all three real
// cases: macOS has no ALSA device type, a Linux build configured without the
// JACK headers has no JACK type, and a host with no running jackd/PipeWire
// reports no JACK device. Deliberately JUCE-free so the same rules run in the
// app and under `mnkybox --unittests` (logic_tests.cpp).
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace mnky::app {

enum class AudioBackend : std::uint8_t { Default, Alsa, Jack };

inline constexpr int kAudioBackendCount = 3;
inline constexpr AudioBackend kAudioBackends[kAudioBackendCount] = {
    AudioBackend::Default,
    AudioBackend::Alsa,
    AudioBackend::Jack,
};

// ALSA stays the default: Backend::Default leaves JUCE's platform default
// device type alone, which is ALSA on the Pi and CoreAudio on the dev machine.
inline constexpr AudioBackend kDefaultAudioBackend = AudioBackend::Default;

// Stable keys for the app's PropertiesFile (`audio/backend`), never localised.
inline constexpr std::string_view audioBackendKey(AudioBackend backend) {
  switch (backend) {
    case AudioBackend::Alsa:
      return "alsa";
    case AudioBackend::Jack:
      return "jack";
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

// JUCE device type name; empty means "keep the platform default type".
inline constexpr std::string_view audioDeviceTypeName(AudioBackend backend) {
  switch (backend) {
    case AudioBackend::Alsa:
      return "ALSA";
    case AudioBackend::Jack:
      return "JACK";
    case AudioBackend::Default:
      break;
  }
  return {};
}

inline std::string audioBackendLabel(AudioBackend backend) {
  switch (backend) {
    case AudioBackend::Alsa:
      return "ALSA";
    case AudioBackend::Jack:
      return "JACK";
    case AudioBackend::Default:
      break;
  }
  return "Default";
}

// Menu text. An unavailable item keeps its name and says so, because a silently
// missing entry is indistinguishable from a build that never had the backend.
inline std::string audioBackendLabel(AudioBackend backend, bool available) {
  return available ? audioBackendLabel(backend) : audioBackendLabel(backend) + " (unavailable)";
}

// A backend can be selected only when its device type is present and lists at
// least one output device. Default is the platform's own fallback and is always
// offered.
struct AudioBackendAvailability {
  bool alsa = false;
  bool jack = false;
};

inline bool audioBackendAvailable(AudioBackend backend,
                                  const AudioBackendAvailability& availability) {
  switch (backend) {
    case AudioBackend::Alsa:
      return availability.alsa;
    case AudioBackend::Jack:
      return availability.jack;
    case AudioBackend::Default:
      break;
  }
  return true;
}

struct AudioBackendSelection {
  AudioBackend requested = kDefaultAudioBackend;
  AudioBackend effective = kDefaultAudioBackend;
  bool fellBack = false;
  std::string note;  // why the request was not used ("" when it was)
};

// The startup path and the Apply path both go through this: a request for a
// backend that is not available never reaches the device manager, it falls back
// to the platform default and says why.
inline AudioBackendSelection resolveAudioBackend(
    AudioBackend requested, const AudioBackendAvailability& availability) {
  AudioBackendSelection selection;
  selection.requested = requested;
  if (audioBackendAvailable(requested, availability)) {
    selection.effective = requested;
    return selection;
  }
  selection.effective = kDefaultAudioBackend;
  selection.fellBack = true;
  selection.note = requested == AudioBackend::Jack
                       ? "JACK unavailable (no JACK device type or no jackd server); "
                         "using the default backend"
                       : "ALSA unavailable on this platform; using the default backend";
  return selection;
}

// Buffer sizes offered by the System screen. 128 is audio::kBlockSize, the
// engine's block size. JACK ignores the request: the server owns its period, so
// the System readout shows the size the device actually runs.
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

// The stored output device is used only while the chosen backend still lists
// it; an unplugged or renamed device falls back to the type's default device
// (an empty name).
inline std::string resolveAudioDeviceName(std::string_view stored,
                                          const std::vector<std::string>& available) {
  for (const auto& name : available) {
    if (name == stored) return name;
  }
  return {};
}

// One persisted selection: backend, its output device ("" = type default) and
// the requested period size.
struct AudioBackendChoice {
  AudioBackend backend = kDefaultAudioBackend;
  std::string deviceName;
  int bufferSize = kDefaultAudioBufferSize;
};

inline bool operator==(const AudioBackendChoice& a, const AudioBackendChoice& b) {
  return a.backend == b.backend && a.deviceName == b.deviceName &&
         a.bufferSize == b.bufferSize;
}

inline bool operator!=(const AudioBackendChoice& a, const AudioBackendChoice& b) {
  return !(a == b);
}

}  // namespace mnky::app
