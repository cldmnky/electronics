#pragma once
// Audio backend selection model for the Qt 6 app: the System screen's
// default/device/emulator choice, the requested period size and the fallback
// rules used when a device is gone or a platform has no output at all.
// Deliberately Qt-free so the same rules run in the app and under
// `mnkybox --unittests` (logic_tests.cpp).
//
// JACK is deliberately absent: the previous JUCE device manager had an
// ALSA/JACK type switch, while Qt Multimedia exposes devices, not host APIs.
// On Linux Qt Multimedia uses PulseAudio when a server is present (PipeWire's
// Pulse compatibility layer on Raspberry Pi OS) and ALSA otherwise; the
// System screen therefore offers the Qt default and the explicit device list.
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace mnky::app {

enum class AudioBackend : std::uint8_t { Default, Device, Emulator };

inline constexpr int kAudioBackendCount = 3;
inline constexpr AudioBackend kAudioBackends[kAudioBackendCount] = {
    AudioBackend::Default,
    AudioBackend::Device,
    AudioBackend::Emulator,
};

// ALSA/PulseAudio/CoreAudio stay the default: the platform default device is
// what a fresh install should use. The emulator generator is an explicit
// choice and also the documented fallback when no device opens.
inline constexpr AudioBackend kDefaultAudioBackend = AudioBackend::Default;

// Stable keys for QSettings (`audio/backend`), never localised.
inline constexpr std::string_view audioBackendKey(AudioBackend backend) {
  switch (backend) {
    case AudioBackend::Device:
      return "device";
    case AudioBackend::Emulator:
      return "emulator";
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
// explicit output/input device ids. The emulator is always available.
struct AudioBackendAvailability {
  bool defaultOutput = false;
  std::vector<std::string> outputDeviceIds;
  std::vector<std::string> inputDeviceIds;
};

inline bool audioBackendAvailable(AudioBackend backend,
                                  const AudioBackendAvailability& availability) {
  switch (backend) {
    case AudioBackend::Device:
      return !availability.outputDeviceIds.empty();
    case AudioBackend::Emulator:
      return true;
    case AudioBackend::Default:
      break;
  }
  return availability.defaultOutput;
}

struct AudioBackendSelection {
  AudioBackend effective = kDefaultAudioBackend;
  std::string outputDeviceId;  // empty for Default/Emulator
  bool fellBack = false;
  std::string note;  // why the request was not used ("" when it was)
};

// The startup path and the Apply path both go through this: a request for a
// device that disappeared falls back to the default output, and a machine with
// no output at all falls back to the emulator generator with the reason.
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

  if (availability.defaultOutput) return selection;
  selection.effective = AudioBackend::Emulator;
  selection.fellBack = true;
  selection.note = "no audio output device on this platform; using the emulator generator";
  return selection;
}

// Buffer sizes offered by the System screen. 128 is audio::kBlockSize, the
// engine's block size; Qt Multimedia may report a larger device buffer, which
// the System live line shows instead of assuming the request was honoured.
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

}  // namespace mnky::app
