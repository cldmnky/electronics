#pragma once
// Minimal RIFF/WAVE header reader for the Recorder take list. The recorder
// writes float32 WAV; the app only needs channel count, sample rate and frame
// count to show a take duration, so it does not link an audio-format library.
#include <cstdint>
#include <filesystem>

namespace mnky::app {

struct WavInfo {
  bool ok = false;
  std::uint16_t channels = 0;
  std::uint32_t sampleRate = 0;
  std::uint16_t bitsPerSample = 0;
  std::uint16_t format = 0;  // 1 = PCM, 3 = IEEE float
  std::uint64_t frames = 0;
};

WavInfo readWavInfo(const std::filesystem::path& path);

// 0 when the file is missing, unreadable or has no frames.
double wavDurationSeconds(const std::filesystem::path& path);

}  // namespace mnky::app
