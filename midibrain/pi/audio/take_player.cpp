#include "take_player.hpp"

#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstring>

namespace mnky::audio {
namespace {

constexpr std::uint16_t kFormatPcm = 1;
constexpr std::uint16_t kFormatFloat = 3;
constexpr std::uint32_t kRequiredSampleRate = 48000;
constexpr int kReadChunkFrames = 256;

std::uint16_t le16(const unsigned char* bytes) {
    return static_cast<std::uint16_t>(bytes[0]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[1]) << 8);
}

std::uint32_t le32(const unsigned char* bytes) {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8) |
           (static_cast<std::uint32_t>(bytes[2]) << 16) |
           (static_cast<std::uint32_t>(bytes[3]) << 24);
}

float leFloat(const unsigned char* bytes) {
    const std::uint32_t bits = le32(bytes);
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

std::string quoted(const std::string& path) {
    return "\"" + path + "\"";
}

}  // namespace

TakePlayer::TakePlayer() {
    static_assert((kRingFrames & (kRingFrames - 1)) == 0,
                  "take playback ring must be a power of two");
}

TakePlayer::~TakePlayer() { clearAll(); }

void TakePlayer::enterControl() {
    mControlBusy.store(true, std::memory_order_seq_cst);
    while (mActiveRenderers.load(std::memory_order_acquire) != 0) {
        std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
}

void TakePlayer::leaveControl() {
    mControlBusy.store(false, std::memory_order_release);
}

void TakePlayer::refuse(const std::string& message, std::string* error) {
    if (error != nullptr) *error = message;
    // The reason must be visible even when the caller ignores the return value.
    std::fprintf(stderr, "mnky audio: take refused: %s\n", message.c_str());
}

bool TakePlayer::inspectWav(const std::string& path, WavInfo& info,
                            std::string* error) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        refuse("cannot open " + quoted(path), error);
        return false;
    }
    if (std::fseek(file, 0, SEEK_END) != 0) {
        std::fclose(file);
        refuse("cannot measure " + quoted(path), error);
        return false;
    }
    const long fileSize = std::ftell(file);
    if (fileSize < 12 || std::fseek(file, 0, SEEK_SET) != 0) {
        std::fclose(file);
        refuse(quoted(path) + " is not a readable RIFF/WAVE file", error);
        return false;
    }

    unsigned char header[12] = {};
    if (std::fread(header, 1, sizeof header, file) != sizeof header ||
        std::memcmp(header, "RIFF", 4) != 0 ||
        std::memcmp(header + 8, "WAVE", 4) != 0) {
        std::fclose(file);
        refuse(quoted(path) + " is not a RIFF/WAVE file", error);
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
        const std::uint32_t chunkBytes = le32(chunk + 4);
        const std::uint64_t payload = offset + 8;
        if (payload > total) break;
        const std::uint64_t available = total - payload;
        const std::uint64_t present =
            std::min<std::uint64_t>(chunkBytes, available);

        if (std::memcmp(chunk, "fmt ", 4) == 0) {
            if (present < 16) {
                std::fclose(file);
                refuse(quoted(path) + " has a short fmt chunk", error);
                return false;
            }
            unsigned char fmt[16] = {};
            if (std::fread(fmt, 1, sizeof fmt, file) != sizeof fmt) {
                std::fclose(file);
                refuse(quoted(path) + " has an unreadable fmt chunk", error);
                return false;
            }
            found.format = le16(fmt);
            found.channels = le16(fmt + 2);
            found.sampleRate = le32(fmt + 4);
            found.bits = le16(fmt + 14);
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
        refuse(quoted(path) + " is missing a fmt or data chunk", error);
        return false;
    }
    if (found.sampleRate != kRequiredSampleRate) {
        refuse(quoted(path) + " is " + std::to_string(found.sampleRate) +
                   " Hz; take playback requires 48000 Hz and never resamples",
               error);
        return false;
    }
    if (found.channels < 1 || found.channels > 2) {
        refuse(quoted(path) + " has " + std::to_string(found.channels) +
                   " channels; take playback accepts mono or stereo",
               error);
        return false;
    }
    const bool float32 =
        found.format == kFormatFloat && found.bits == 32;
    const bool pcm16 = found.format == kFormatPcm && found.bits == 16;
    if (!float32 && !pcm16) {
        refuse(quoted(path) + " uses WAV format " +
                   std::to_string(found.format) + "/" +
                   std::to_string(found.bits) +
                   " bit; take playback accepts 32-bit float or 16-bit PCM",
               error);
        return false;
    }
    const std::uint64_t frameBytes =
        static_cast<std::uint64_t>(found.channels) * (found.bits / 8u);
    if (frameBytes == 0 || found.dataBytes < frameBytes) {
        refuse(quoted(path) + " has no audio frames", error);
        return false;
    }
    info = found;
    return true;
}

bool TakePlayer::startLayer(Layer& layer, const std::string& path,
                            const WavInfo& info, std::uint64_t startFrame,
                            std::string* error) {
    const std::uint64_t frameBytes =
        static_cast<std::uint64_t>(info.channels) * (info.bits / 8u);
    const std::uint64_t totalFrames = info.dataBytes / frameBytes;
    if (totalFrames == 0) {
        refuse(quoted(path) + " has no audio frames", error);
        return false;
    }

    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        refuse("cannot open " + quoted(path), error);
        return false;
    }
    layer.path = path;
    layer.file = file;
    layer.channels = info.channels;
    layer.bytesPerSample = info.bits / 8u;
    layer.floatSamples = info.format == kFormatFloat;
    layer.lengthFrames =
        startFrame < totalFrames ? totalFrames - startFrame : 0;
    layer.remainingFrames = layer.lengthFrames;
    layer.writeFrame.store(0, std::memory_order_relaxed);
    layer.readFrame.store(0, std::memory_order_relaxed);
    layer.eof.store(false, std::memory_order_relaxed);
    layer.stop.store(false, std::memory_order_relaxed);

    if (layer.lengthFrames > 0) {
        const std::uint64_t byteOffset =
            info.dataOffset + startFrame * frameBytes;
        if (byteOffset > static_cast<std::uint64_t>(LONG_MAX) ||
            std::fseek(file, static_cast<long>(byteOffset), SEEK_SET) != 0) {
            std::fclose(file);
            layer.file = nullptr;
            layer.path.clear();
            refuse("cannot seek " + quoted(path), error);
            return false;
        }
    }

    // Prime the ring before publishing the layer so a normal first block does
    // not start with an avoidable dropout. The reader thread streams the rest.
    std::uint64_t primed = 0;
    const std::uint64_t target =
        std::min<std::uint64_t>(layer.lengthFrames, kRingFrames / 2);
    while (primed < target) {
        const int want = static_cast<int>(
            std::min<std::uint64_t>(kReadChunkFrames, target - primed));
        const int got = readFrames(layer, layer.ring.data() + primed, want);
        if (got <= 0) break;
        primed += static_cast<std::uint64_t>(got);
    }
    layer.writeFrame.store(primed, std::memory_order_release);

    layer.active.store(true, std::memory_order_release);
    if (layer.lengthFrames > 0) {
        layer.thread = std::thread(&TakePlayer::readLoop, this, &layer);
    } else {
        layer.eof.store(true, std::memory_order_relaxed);
    }
    return true;
}

void TakePlayer::stopLayer(Layer& layer) {
    layer.active.store(false, std::memory_order_release);
    layer.stop.store(true, std::memory_order_release);
    if (layer.thread.joinable()) layer.thread.join();
    if (layer.file != nullptr) {
        std::fclose(layer.file);
        layer.file = nullptr;
    }
    layer.path.clear();
    layer.remainingFrames = 0;
    layer.lengthFrames = 0;
    layer.channels = 0;
    layer.bytesPerSample = 0;
    layer.floatSamples = false;
    layer.writeFrame.store(0, std::memory_order_relaxed);
    layer.readFrame.store(0, std::memory_order_relaxed);
    layer.eof.store(false, std::memory_order_relaxed);
    layer.stop.store(false, std::memory_order_relaxed);
}

bool TakePlayer::setTakes(int track, const std::vector<std::string>& paths,
                          std::uint64_t startFrame, std::string* error) {
    if (track < 0 || track >= kMaxTracks) {
        refuse("track " + std::to_string(track) + " is out of range", error);
        return false;
    }
    if (paths.size() > static_cast<std::size_t>(kMaxTakesPerTrack)) {
        refuse("track " + std::to_string(track) + " has " +
                   std::to_string(paths.size()) + " takes; the limit is " +
                   std::to_string(kMaxTakesPerTrack),
               error);
        return false;
    }
    if (paths.empty()) {
        clearTrack(track);
        return true;
    }

    // Validate the whole new stack before touching the old one, so a refused
    // non-48k file cannot silently erase the current selection.
    std::vector<WavInfo> info;
    info.reserve(paths.size());
    for (const std::string& path : paths) {
        WavInfo parsed;
        if (!inspectWav(path, parsed, error)) return false;
        info.push_back(parsed);
    }

    enterControl();
    clearTrackLocked(track);
    Track& slot = mTracks[track];
    // Keep the track marked as take-fed while the new stack is being opened,
    // so a concurrent render silences it instead of leaking live input.
    slot.count.store(static_cast<int>(paths.size()), std::memory_order_release);
    for (std::size_t i = 0; i < paths.size(); ++i) {
        if (!startLayer(slot.layers[i], paths[i], info[i], startFrame, error)) {
            clearTrackLocked(track);
            leaveControl();
            return false;
        }
    }
    leaveControl();
    return true;
}

bool TakePlayer::setTake(int track, const std::string& path,
                         std::uint64_t startFrame, std::string* error) {
    if (path.empty()) {
        if (track < 0 || track >= kMaxTracks) {
            refuse("track " + std::to_string(track) + " is out of range",
                   error);
            return false;
        }
        clearTrack(track);
        return true;
    }
    return setTakes(track, std::vector<std::string>{path}, startFrame, error);
}

void TakePlayer::clearTrackLocked(int track) {
    if (track < 0 || track >= kMaxTracks) return;
    Track& slot = mTracks[track];
    slot.count.store(0, std::memory_order_release);
    for (Layer& layer : slot.layers) stopLayer(layer);
}

void TakePlayer::clearTrack(int track) {
    if (track < 0 || track >= kMaxTracks) return;
    enterControl();
    clearTrackLocked(track);
    leaveControl();
}

void TakePlayer::clearAll() {
    enterControl();
    for (int track = 0; track < kMaxTracks; ++track) clearTrackLocked(track);
    leaveControl();
}

int TakePlayer::takeCount(int track) const {
    if (track < 0 || track >= kMaxTracks) return 0;
    return mTracks[track].count.load(std::memory_order_acquire);
}

std::string TakePlayer::takePath(int track, int layer) const {
    if (track < 0 || track >= kMaxTracks) return {};
    const Track& slot = mTracks[track];
    const int count = slot.count.load(std::memory_order_acquire);
    if (layer < 0 || layer >= count) return {};
    return slot.layers[layer].path;
}

int TakePlayer::readFrames(Layer& layer, float* mono, int frames) {
    if (layer.file == nullptr || mono == nullptr || frames <= 0 ||
        layer.remainingFrames == 0) {
        return 0;
    }
    const int wanted = static_cast<int>(
        std::min<std::uint64_t>(static_cast<std::uint64_t>(frames),
                                layer.remainingFrames));
    const int frameBytes = layer.channels * layer.bytesPerSample;
    unsigned char raw[kReadChunkFrames * 2 * 4] = {};
    const std::size_t got =
        std::fread(raw, static_cast<std::size_t>(frameBytes),
                   static_cast<std::size_t>(wanted), layer.file);
    if (got == 0) return 0;

    for (std::size_t frame = 0; frame < got; ++frame) {
        const unsigned char* sample = raw + frame * static_cast<std::size_t>(frameBytes);
        float value = 0.0f;
        if (layer.floatSamples) {
            value = leFloat(sample);
            if (layer.channels == 2) {
                value = 0.5f * (value + leFloat(sample + 4));
            }
        } else {
            const std::int16_t left =
                static_cast<std::int16_t>(le16(sample));
            value = static_cast<float>(left) / 32768.0f;
            if (layer.channels == 2) {
                const std::int16_t right =
                    static_cast<std::int16_t>(le16(sample + 2));
                value = 0.5f * (value + static_cast<float>(right) / 32768.0f);
            }
        }
        mono[frame] = value;
    }
    layer.remainingFrames -= static_cast<std::uint64_t>(got);
    return static_cast<int>(got);
}

void TakePlayer::readLoop(Layer* layer) {
    float scratch[kReadChunkFrames] = {};
    while (!layer->stop.load(std::memory_order_acquire)) {
        const std::uint64_t write =
            layer->writeFrame.load(std::memory_order_relaxed);
        const std::uint64_t read =
            layer->readFrame.load(std::memory_order_acquire);
        const std::uint64_t used = write - read;
        if (used >= static_cast<std::uint64_t>(kRingFrames)) {
            std::this_thread::sleep_for(std::chrono::microseconds(200));
            continue;
        }
        const int space =
            kRingFrames - static_cast<int>(used);
        const int want = std::min(kReadChunkFrames, space);
        const int got = readFrames(*layer, scratch, want);
        if (got <= 0) {
            layer->eof.store(true, std::memory_order_relaxed);
            break;
        }
        for (int i = 0; i < got; ++i) {
            layer->ring[static_cast<std::size_t>(
                (write + static_cast<std::uint64_t>(i)) &
                static_cast<std::uint64_t>(kRingFrames - 1))] = scratch[i];
        }
        layer->writeFrame.store(write + static_cast<std::uint64_t>(got),
                                std::memory_order_release);
    }
}

void TakePlayer::render(bool rolling, int n, float* const* monoOut,
                        bool* active, bool* playing) {
    const int frames = std::min(n, kReadChunkFrames);
    mActiveRenderers.fetch_add(1, std::memory_order_acq_rel);
    if (mControlBusy.load(std::memory_order_acquire)) {
        // A control thread is changing a stack. Report selection and silence
        // this block rather than reading a ring that is being reset. Treat a
        // rolling selection as playing so a swap cannot leak live input.
        for (int track = 0; track < kMaxTracks; ++track) {
            const int count = mTracks[track].count.load(std::memory_order_acquire);
            if (active != nullptr) active[track] = count > 0;
            if (playing != nullptr) playing[track] = rolling && count > 0;
            float* out = monoOut != nullptr ? monoOut[track] : nullptr;
            if (out == nullptr) continue;
            for (int i = 0; i < frames; ++i) out[i] = 0.0f;
        }
        mActiveRenderers.fetch_sub(1, std::memory_order_acq_rel);
        return;
    }
    for (int track = 0; track < kMaxTracks; ++track) {
        Track& slot = mTracks[track];
        const int count = slot.count.load(std::memory_order_acquire);
        if (active != nullptr) active[track] = count > 0;

        bool anyPlaying = false;
        for (int index = 0; index < count; ++index) {
            const Layer& layer = slot.layers[index];
            if (!layer.active.load(std::memory_order_acquire)) continue;
            if (layer.readFrame.load(std::memory_order_relaxed) <
                layer.lengthFrames) {
                anyPlaying = true;
                break;
            }
        }
        if (playing != nullptr) playing[track] = rolling && anyPlaying;

        float* out = monoOut != nullptr ? monoOut[track] : nullptr;
        if (out == nullptr) continue;
        for (int i = 0; i < frames; ++i) out[i] = 0.0f;
        if (!rolling || count == 0 || frames <= 0) continue;

        bool covered[kReadChunkFrames] = {};
        for (int index = count - 1; index >= 0; --index) {
            Layer& layer = slot.layers[index];
            if (!layer.active.load(std::memory_order_acquire)) continue;
            const std::uint64_t position =
                layer.readFrame.load(std::memory_order_relaxed);
            if (position >= layer.lengthFrames) continue;
            const int want = static_cast<int>(
                std::min<std::uint64_t>(static_cast<std::uint64_t>(frames),
                                        layer.lengthFrames - position));
            const std::uint64_t write =
                layer.writeFrame.load(std::memory_order_acquire);
            const std::uint64_t available =
                write > position ? write - position : 0;
            for (int i = 0; i < want; ++i) {
                if (covered[i]) continue;
                covered[i] = true;
                if (static_cast<std::uint64_t>(i) < available) {
                    out[i] = layer.ring[static_cast<std::size_t>(
                        (position + static_cast<std::uint64_t>(i)) &
                        static_cast<std::uint64_t>(kRingFrames - 1))];
                } else {
                    // The newer layer owns this sample even when the reader
                    // has not delivered it yet; never leak the older take.
                    out[i] = 0.0f;
                    mUnderruns.fetch_add(1, std::memory_order_relaxed);
                }
            }
            layer.readFrame.store(position + static_cast<std::uint64_t>(want),
                                  std::memory_order_release);
        }
    }
    mActiveRenderers.fetch_sub(1, std::memory_order_acq_rel);
}

}  // namespace mnky::audio
