#pragma once
// Minimal RIFF/WAVE inspection and decoding for take playback and the peak
// scanner (JUCE-free, portable C++20). Header-only so the player and the
// scanner share exactly one spelling of the accepted take format without a
// second translation unit in the session standalone build.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace mnky::audio {

/// Take playback rate. Takes are never resampled, so a file at another rate is
/// refused rather than pitched.
inline constexpr std::uint32_t kTakeSampleRate = 48000;
/// Frames decoded per stack buffer inside decodeTakeFrames().
inline constexpr int kTakeDecodeChunkFrames = 256;

/// Decoded RIFF/WAVE layout of one take file.
struct WavInfo {
    std::uint64_t dataOffset = 0;
    std::uint64_t dataBytes = 0;
    std::uint32_t sampleRate = 0;
    std::uint16_t channels = 0;
    std::uint16_t format = 0;
    std::uint16_t bits = 0;

    /// Bytes per interleaved frame; 0 for an unset info.
    std::uint64_t frameBytes() const {
        return static_cast<std::uint64_t>(channels) *
               static_cast<std::uint64_t>(bits / 8u);
    }
    /// Complete frames in the data chunk.
    std::uint64_t frames() const {
        const std::uint64_t bytes = frameBytes();
        return bytes == 0 ? 0 : dataBytes / bytes;
    }
    /// 32-bit IEEE float samples rather than 16-bit PCM.
    bool floatSamples() const { return format == 3 && bits == 32; }
};

namespace wav_detail {

inline std::uint16_t le16(const unsigned char* bytes) {
    return static_cast<std::uint16_t>(bytes[0]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[1]) << 8);
}

inline std::uint32_t le32(const unsigned char* bytes) {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8) |
           (static_cast<std::uint32_t>(bytes[2]) << 16) |
           (static_cast<std::uint32_t>(bytes[3]) << 24);
}

inline float leFloat(const unsigned char* bytes) {
    const std::uint32_t bits = le32(bytes);
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

inline std::string quoted(const std::string& path) { return "\"" + path + "\""; }

}  // namespace wav_detail

/// Reads the RIFF/WAVE header of `path` and validates the take format: 48 kHz
/// mono or stereo, 32-bit IEEE float (format 3) or 16-bit PCM (format 1), at
/// least one audio frame. On failure returns false and writes the reason (which
/// names the offending value) to `error`; it never logs, because the caller owns
/// the "take refused:" line.
inline bool inspectTakeWav(const std::string& path, WavInfo& info,
                           std::string& error) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        error = "cannot open " + wav_detail::quoted(path);
        return false;
    }
    if (std::fseek(file, 0, SEEK_END) != 0) {
        std::fclose(file);
        error = "cannot measure " + wav_detail::quoted(path);
        return false;
    }
    const long fileSize = std::ftell(file);
    if (fileSize < 12 || std::fseek(file, 0, SEEK_SET) != 0) {
        std::fclose(file);
        error = wav_detail::quoted(path) + " is not a readable RIFF/WAVE file";
        return false;
    }

    unsigned char header[12] = {};
    if (std::fread(header, 1, sizeof header, file) != sizeof header ||
        std::memcmp(header, "RIFF", 4) != 0 ||
        std::memcmp(header + 8, "WAVE", 4) != 0) {
        std::fclose(file);
        error = wav_detail::quoted(path) + " is not a RIFF/WAVE file";
        return false;
    }

    WavInfo found;
    bool haveFmt = false;
    bool haveData = false;
    std::uint64_t offset = 12;
    const std::uint64_t total = static_cast<std::uint64_t>(fileSize);
    while (offset + 8 <= total) {
        unsigned char chunk[8] = {};
        if (std::fseek(file, static_cast<long>(offset), SEEK_SET) != 0 ||
            std::fread(chunk, 1, sizeof chunk, file) != sizeof chunk) {
            break;
        }
        const std::uint32_t chunkBytes = wav_detail::le32(chunk + 4);
        const std::uint64_t payload = offset + 8;
        if (payload > total) break;
        const std::uint64_t available = total - payload;
        const std::uint64_t present =
            std::min<std::uint64_t>(chunkBytes, available);

        if (std::memcmp(chunk, "fmt ", 4) == 0) {
            if (present < 16) {
                std::fclose(file);
                error = wav_detail::quoted(path) + " has a short fmt chunk";
                return false;
            }
            unsigned char fmt[16] = {};
            if (std::fread(fmt, 1, sizeof fmt, file) != sizeof fmt) {
                std::fclose(file);
                error = wav_detail::quoted(path) + " has an unreadable fmt chunk";
                return false;
            }
            found.format = wav_detail::le16(fmt);
            found.channels = wav_detail::le16(fmt + 2);
            found.sampleRate = wav_detail::le32(fmt + 4);
            found.bits = wav_detail::le16(fmt + 14);
            haveFmt = true;
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            found.dataOffset = payload;
            found.dataBytes = present;
            haveData = true;
        }
        offset = payload + present + (chunkBytes & 1u);
    }
    std::fclose(file);

    if (!haveFmt || !haveData) {
        error = wav_detail::quoted(path) + " is missing a fmt or data chunk";
        return false;
    }
    if (found.sampleRate != kTakeSampleRate) {
        error = wav_detail::quoted(path) + " is " + std::to_string(found.sampleRate) +
                " Hz; take playback requires 48000 Hz and never resamples";
        return false;
    }
    if (found.channels < 1 || found.channels > 2) {
        error = wav_detail::quoted(path) + " has " + std::to_string(found.channels) +
                " channels; take playback accepts mono or stereo";
        return false;
    }
    const bool float32 = found.format == 3 && found.bits == 32;
    const bool pcm16 = found.format == 1 && found.bits == 16;
    if (!float32 && !pcm16) {
        error = wav_detail::quoted(path) + " uses WAV format " +
                std::to_string(found.format) + "/" +
                std::to_string(found.bits) +
                " bit; take playback accepts 32-bit float or 16-bit PCM";
        return false;
    }
    if (found.frameBytes() == 0 || found.frames() == 0) {
        error = wav_detail::quoted(path) + " has no audio frames";
        return false;
    }
    info = found;
    return true;
}

/// Decodes up to `frames` frames from the current file position into `mono`,
/// downmixing stereo to 0.5 * (L + R) because the take voice has one mono path
/// per track. Returns the frames produced (0 at end of file) and leaves the file
/// positioned after the last decoded frame. The caller owns the file and the
/// seek; this never allocates and never seeks.
inline int decodeTakeFrames(std::FILE* file, int channels, int bytesPerSample,
                            bool floatSamples, float* mono, int frames) {
    if (file == nullptr || mono == nullptr || frames <= 0) return 0;
    const int frameBytes = channels * bytesPerSample;
    if (frameBytes <= 0) return 0;
    unsigned char raw[kTakeDecodeChunkFrames * 2 * 4] = {};
    int produced = 0;
    while (produced < frames) {
        const int want = std::min(kTakeDecodeChunkFrames, frames - produced);
        const std::size_t got = std::fread(
            raw, static_cast<std::size_t>(frameBytes),
            static_cast<std::size_t>(want), file);
        if (got == 0) break;
        for (std::size_t frame = 0; frame < got; ++frame) {
            const unsigned char* sample =
                raw + frame * static_cast<std::size_t>(frameBytes);
            float value = 0.0f;
            if (floatSamples) {
                value = wav_detail::leFloat(sample);
                if (channels == 2) {
                    value = 0.5f * (value + wav_detail::leFloat(sample + 4));
                }
            } else {
                value =
                    static_cast<float>(
                        static_cast<std::int16_t>(wav_detail::le16(sample))) /
                    32768.0f;
                if (channels == 2) {
                    const float right =
                        static_cast<float>(
                            static_cast<std::int16_t>(wav_detail::le16(sample + 2))) /
                        32768.0f;
                    value = 0.5f * (value + right);
                }
            }
            mono[produced + static_cast<int>(frame)] = value;
        }
        produced += static_cast<int>(got);
        if (static_cast<int>(got) < want) break;  // short read: end of data
    }
    return produced;
}

}  // namespace mnky::audio
