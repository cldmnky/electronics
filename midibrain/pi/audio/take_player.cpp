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
// Shortest loop that still gets a seam ramp: below this a wrap is not audible.
constexpr std::uint64_t kMinLoopFrames = 4;

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

std::uint64_t TakePlayer::loopSeamFade(std::uint64_t loopFrames) {
    const std::uint64_t fade = std::min(loopFrames / 4, kLoopFadeFrames);
    return fade > 0 ? fade : 1;
}

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

bool TakePlayer::startLayer(int track, Layer& layer, const std::string& path,
                            const WavInfo& info, std::uint64_t startFrame,
                            std::string* error) {
    const std::uint64_t frameBytes =
        static_cast<std::uint64_t>(info.channels) * (info.bits / 8u);
    const std::uint64_t totalFrames = info.dataBytes / frameBytes;
    if (totalFrames == 0) {
        refuse(quoted(path) + " has no audio frames", error);
        return false;
    }
    const std::uint64_t lengthFrames =
        startFrame < totalFrames ? totalFrames - startFrame : 0;
    const LoopWindow window =
        loopWindowFor(track, mTracks[track], lengthFrames);
    const std::uint64_t streamStart =
        window.looping ? window.startFrames : 0;

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
    layer.lengthFrames = lengthFrames;
    layer.remainingFrames = lengthFrames - streamStart;
    layer.loopStartFrames = window.startFrames;
    layer.loopEndFrames = window.endFrames;
    layer.dataOffset = info.dataOffset;
    layer.originFrame = startFrame;
    layer.frameBytes = frameBytes;
    layer.writeFrame.store(0, std::memory_order_relaxed);
    layer.readFrame.store(0, std::memory_order_relaxed);
    layer.eof.store(false, std::memory_order_relaxed);
    layer.stop.store(false, std::memory_order_relaxed);
    layer.looping.store(window.looping, std::memory_order_relaxed);

    if (streamStart < lengthFrames) {
        const std::uint64_t byteOffset =
            info.dataOffset + (startFrame + streamStart) * frameBytes;
        if (byteOffset > static_cast<std::uint64_t>(LONG_MAX) ||
            std::fseek(file, static_cast<long>(byteOffset), SEEK_SET) != 0) {
            stopLayer(layer);
            refuse("cannot seek " + quoted(path), error);
            return false;
        }
    }

    // Prime the ring before publishing the layer so a normal first block does
    // not start with an avoidable dropout. The reader thread streams the rest.
    // A looped stream never ends, so the whole half ring is worth priming.
    const std::uint64_t half = static_cast<std::uint64_t>(kRingFrames) / 2;
    const std::uint64_t target =
        window.looping ? half
                       : std::min<std::uint64_t>(layer.remainingFrames, half);
    std::uint64_t primed = 0;
    while (primed < target) {
        const int want = static_cast<int>(
            std::min<std::uint64_t>(kReadChunkFrames, target - primed));
        const int got = produceFrames(layer, layer.ring.data() + primed, want);
        if (got <= 0) break;
        primed += static_cast<std::uint64_t>(got);
    }
    layer.writeFrame.store(primed, std::memory_order_release);

    layer.active.store(true, std::memory_order_release);
    // A looped layer wraps instead of ending, so it keeps streaming even when
    // priming stopped exactly on the loop end (remainingFrames == 0).
    if (window.looping || layer.remainingFrames > 0) {
        layer.thread = std::thread(&TakePlayer::readLoop, this, &layer);
    } else {
        layer.eof.store(true, std::memory_order_relaxed);
    }
    return true;
}

TakePlayer::LoopWindow TakePlayer::loopWindowFor(int track, const Track& slot,
                                                 std::uint64_t lengthFrames) const {
    LoopWindow window;  // linear by default
    if (!slot.loop.enabled) return window;
    if (!(slot.loop.endBeats > slot.loop.startBeats)) {
        // end <= start (or NaN) is the whole-take rule: wrap at the layer end.
        if (lengthFrames < kMinLoopFrames) return window;
        window.looping = true;
        window.startFrames = 0;
        window.endFrames = lengthFrames;
        return window;
    }
    if (!(slot.framesPerBeat > 0.0)) return window;  // no tempo: cannot map

    // Session beats -> take frames, clamped into this layer's stream. Anything
    // past the layer clamps to the end, so a range outside the take degrades to
    // linear playback instead of inventing audio.
    const double limit = static_cast<double>(lengthFrames) + 1.0;
    const auto beatsToFrames = [&](double beats) {
        double frames = std::floor(beats * slot.framesPerBeat + 0.5);
        if (!(frames > 0.0)) frames = 0.0;
        if (frames > limit) frames = limit;
        return static_cast<std::int64_t>(frames);
    };
    const std::int64_t origin = static_cast<std::int64_t>(slot.startFrame);
    const std::int64_t lastFrame = static_cast<std::int64_t>(lengthFrames);
    const std::int64_t start = std::clamp<std::int64_t>(
        beatsToFrames(slot.loop.startBeats) - origin, 0, lastFrame);
    const std::int64_t end = std::clamp<std::int64_t>(
        beatsToFrames(slot.loop.endBeats) - origin, 0, lastFrame);
    if (end - start < static_cast<std::int64_t>(kMinLoopFrames)) {
        std::fprintf(stderr,
                     "mnky audio: take loop on track %d does not fit the take "
                     "(%llu frames); playing linearly\n",
                     track, static_cast<unsigned long long>(lengthFrames));
        return window;
    }
    window.looping = true;
    window.startFrames = static_cast<std::uint64_t>(start);
    window.endFrames = static_cast<std::uint64_t>(end);
    return window;
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
    layer.loopStartFrames = 0;
    layer.loopEndFrames = 0;
    layer.dataOffset = 0;
    layer.originFrame = 0;
    layer.frameBytes = 0;
    layer.channels = 0;
    layer.bytesPerSample = 0;
    layer.floatSamples = false;
    layer.writeFrame.store(0, std::memory_order_relaxed);
    layer.readFrame.store(0, std::memory_order_relaxed);
    layer.eof.store(false, std::memory_order_relaxed);
    layer.stop.store(false, std::memory_order_relaxed);
    layer.looping.store(false, std::memory_order_relaxed);
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
    Track& slot = mTracks[track];
    stopTrackLocked(track);
    slot.pathCount = static_cast<int>(paths.size());
    for (std::size_t i = 0; i < paths.size(); ++i) {
        slot.paths[i] = paths[i];
        slot.infos[i] = info[i];
    }
    slot.startFrame = startFrame;
    const bool ok = startTrackLocked(track, error);
    if (!ok) {
        slot.pathCount = 0;
        for (std::string& path : slot.paths) path.clear();
    }
    leaveControl();
    return ok;
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
    stopTrackLocked(track);
    slot.pathCount = 0;
    for (std::string& path : slot.paths) path.clear();
}

void TakePlayer::stopTrackLocked(int track) {
    Track& slot = mTracks[track];
    slot.count.store(0, std::memory_order_release);
    for (Layer& layer : slot.layers) stopLayer(layer);
}

bool TakePlayer::startTrackLocked(int track, std::string* error) {
    Track& slot = mTracks[track];
    if (slot.pathCount == 0) {
        slot.count.store(0, std::memory_order_release);
        return true;
    }
    // Keep the track marked as take-fed while the stack is being opened, so a
    // concurrent render silences it instead of leaking live input.
    slot.count.store(slot.pathCount, std::memory_order_release);
    for (int i = 0; i < slot.pathCount; ++i) {
        if (!startLayer(track, slot.layers[i], slot.paths[i], slot.infos[i],
                        slot.startFrame, error)) {
            stopTrackLocked(track);
            return false;
        }
    }
    return true;
}

bool TakePlayer::restartTrackLocked(int track, std::string* error) {
    stopTrackLocked(track);
    return startTrackLocked(track, error);
}

bool TakePlayer::setLoop(int track, const TakeLoop& loop, double framesPerBeat,
                         std::string* error) {
    if (track < 0 || track >= kMaxTracks) {
        refuse("track " + std::to_string(track) + " is out of range", error);
        return false;
    }
    Track& slot = mTracks[track];
    const double perBeat = std::isfinite(framesPerBeat) && framesPerBeat > 0.0
                               ? framesPerBeat
                               : 0.0;
    // An unchanged state (and a cleared loop at an unchanged tempo) needs no
    // restart: restarting would audibly reposition the take for nothing.
    if (slot.loop == loop && (!loop.enabled || slot.framesPerBeat == perBeat)) {
        return true;
    }
    enterControl();
    slot.loop = loop;
    slot.framesPerBeat = perBeat;
    const bool ok = restartTrackLocked(track, error);
    leaveControl();
    return ok;
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
        const int got = produceFrames(*layer, scratch, want);
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

bool TakePlayer::seekStream(Layer& layer, std::uint64_t position) {
    if (layer.file == nullptr || position > layer.lengthFrames) return false;
    const std::uint64_t byteOffset =
        layer.dataOffset + (layer.originFrame + position) * layer.frameBytes;
    if (byteOffset > static_cast<std::uint64_t>(LONG_MAX) ||
        std::fseek(layer.file, static_cast<long>(byteOffset), SEEK_SET) != 0) {
        refuse("cannot loop-seek \"" + layer.path + "\"", nullptr);
        return false;
    }
    layer.remainingFrames = layer.lengthFrames - position;
    return true;
}

void TakePlayer::applyLoopFade(const Layer& layer, std::uint64_t position,
                               int frames, float* mono) {
    const std::uint64_t loopFrames =
        layer.loopEndFrames - layer.loopStartFrames;
    const std::uint64_t fade = loopSeamFade(loopFrames);
    for (int i = 0; i < frames; ++i) {
        const std::uint64_t offset = position +
                                     static_cast<std::uint64_t>(i) -
                                     layer.loopStartFrames;
        float gain = 1.0f;
        if (offset < fade) {
            gain = static_cast<float>(offset) / static_cast<float>(fade);
        } else if (offset + fade >= loopFrames) {
            gain = static_cast<float>(loopFrames - 1 - offset) /
                   static_cast<float>(fade);
        }
        mono[i] *= gain;
    }
}

int TakePlayer::produceFrames(Layer& layer, float* mono, int frames) {
    if (mono == nullptr || frames <= 0 || layer.file == nullptr) return 0;
    const bool looping = layer.looping.load(std::memory_order_relaxed);
    if (!looping) return readFrames(layer, mono, frames);

    // Wrap inside the take buffer: the stream is continuous, so the audio
    // thread never sees the seam as a gap, only as the seam ramp.
    std::uint64_t position = layer.lengthFrames - layer.remainingFrames;
    if (position >= layer.loopEndFrames &&
        !seekStream(layer, layer.loopStartFrames)) {
        return 0;
    }
    position = layer.lengthFrames - layer.remainingFrames;
    frames = static_cast<int>(std::min<std::uint64_t>(
        static_cast<std::uint64_t>(frames), layer.loopEndFrames - position));
    const int got = readFrames(layer, mono, frames);
    if (got <= 0) return 0;
    applyLoopFade(layer, position, got, mono);
    return got;
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
            if (layer.looping.load(std::memory_order_acquire)) {
                // A looped stream only stops when the take cannot be read.
                if (!layer.eof.load(std::memory_order_acquire)) {
                    anyPlaying = true;
                    break;
                }
                continue;
            }
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
            const bool looping = layer.looping.load(std::memory_order_acquire);
            const std::uint64_t position =
                layer.readFrame.load(std::memory_order_relaxed);
            if (!looping && position >= layer.lengthFrames) continue;
            // A looping layer covers the whole block: it never runs out, so a
            // newer looping take keeps winning over older layers.
            const int want = looping
                                 ? frames
                                 : static_cast<int>(std::min<std::uint64_t>(
                                       static_cast<std::uint64_t>(frames),
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
