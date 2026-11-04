#include "take_player.hpp"

#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstring>
#include <limits>

namespace mnky::audio {
namespace {

constexpr int kReadChunkFrames = 256;
// Shortest loop that still gets a seam ramp: below this a wrap is not audible.
constexpr std::uint64_t kMinLoopFrames = 4;

float dbToLinear(float db) { return std::pow(10.0f, db / 20.0f); }

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

void TakePlayer::note(const std::string& message) {
    // A clamp or a skipped region is not a failure: the rest of the edit still
    // plays, but the reason must be visible rather than silently swallowed.
    std::fprintf(stderr, "mnky audio: take edit: %s\n", message.c_str());
}

bool TakePlayer::inspectWav(const std::string& path, WavInfo& info,
                            std::string* error) {
    std::string reason;
    if (inspectTakeWav(path, info, reason)) return true;
    refuse(reason, error);
    return false;
}


bool TakePlayer::startLayer(int track, Layer& layer, const std::string& path,
                            const WavInfo& info, std::uint64_t startFrame,
                            std::string* error) {
    const std::uint64_t frameBytes =
        static_cast<std::uint64_t>(info.channels) * (info.bits / 8u);
    const std::uint64_t totalFrames = info.dataBytes / frameBytes;
    if (totalFrames == 0) {
        refuse(wav_detail::quoted(path) + " has no audio frames", error);
        return false;
    }
    const std::uint64_t lengthFrames =
        startFrame < totalFrames ? totalFrames - startFrame : 0;
    const LoopWindow window =
        loopWindowFor(track, mTracks[track], lengthFrames, startFrame);
    const std::uint64_t streamStart =
        window.looping ? window.startFrames : 0;

    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        refuse("cannot open " + wav_detail::quoted(path), error);
        return false;
    }
    layer.path = path;
    layer.file = file;
    layer.channels = info.channels;
    layer.bytesPerSample = info.bits / 8u;
    layer.floatSamples = info.floatSamples();
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
            refuse("cannot seek " + wav_detail::quoted(path), error);
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
                                                 std::uint64_t lengthFrames,
                                                 std::uint64_t originFrame) const {
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
    const std::int64_t origin = static_cast<std::int64_t>(originFrame);
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
    // Clearing a track drops its whole take voice: the stack *and* the region
    // edit. A caller that wants the unedited stack back uses clearRegions().
    slot.regionMode.store(false, std::memory_order_release);
    slot.regionEdits.clear();
    slot.edit.clear();
    slot.editFrames = 0;
    slot.pathCount = 0;
    for (std::string& path : slot.paths) path.clear();
}

void TakePlayer::stopTrackLocked(int track) {
    Track& slot = mTracks[track];
    slot.count.store(0, std::memory_order_release);
    for (Layer& layer : slot.layers) stopLayer(layer);
    stopEditStream(slot.editStream);
}

bool TakePlayer::startTrackLocked(int track, std::string* error) {
    Track& slot = mTracks[track];
    if (slot.regionMode.load(std::memory_order_acquire)) {
        return startEditLocked(track, error);
    }
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

int TakePlayer::regionCount(int track) const {
    if (track < 0 || track >= kMaxTracks) return 0;
    const Track& slot = mTracks[track];
    if (!slot.regionMode.load(std::memory_order_acquire)) return 0;
    return static_cast<int>(slot.regionEdits.size());
}

std::uint64_t TakePlayer::editedFrames(int track) const {
    if (track < 0 || track >= kMaxTracks) return 0;
    const Track& slot = mTracks[track];
    if (!slot.regionMode.load(std::memory_order_acquire)) return 0;
    return slot.editFrames;
}

float TakePlayer::Region::gainAt(std::uint64_t position) const {
    if (position >= frames) return 0.0f;
    float value = gain;
    if (fadeInFrames > 0 && position < fadeInFrames) {
        value *= static_cast<float>(position) /
                 static_cast<float>(fadeInFrames);
    }
    if (fadeOutFrames > 0) {
        const std::uint64_t tail = frames - 1 - position;
        if (tail < fadeOutFrames) {
            value *= static_cast<float>(tail) /
                     static_cast<float>(fadeOutFrames);
        }
    }
    return value;
}

bool TakePlayer::regionPeak(const std::string& path, const WavInfo& info,
                            std::uint64_t startFrame, std::uint64_t frames,
                            float& peak, std::string& error) {
    peak = 0.0f;
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        error = "cannot open " + wav_detail::quoted(path) + " to normalize it";
        return false;
    }
    const std::uint64_t byteOffset =
        info.dataOffset + startFrame * info.frameBytes();
    if (byteOffset > static_cast<std::uint64_t>(LONG_MAX) ||
        std::fseek(file, static_cast<long>(byteOffset), SEEK_SET) != 0) {
        std::fclose(file);
        error = "cannot seek " + wav_detail::quoted(path) + " to normalize it";
        return false;
    }
    constexpr int kPeakChunkFrames = 1024;
    float chunk[kPeakChunkFrames] = {};
    std::uint64_t remaining = frames;
    while (remaining > 0) {
        const int want = static_cast<int>(std::min<std::uint64_t>(
            remaining, static_cast<std::uint64_t>(kPeakChunkFrames)));
        const int got = decodeTakeFrames(file, info.channels, info.bits / 8u,
                                         info.floatSamples(), chunk, want);
        if (got <= 0) {
            note(wav_detail::quoted(path) +
                 " is shorter than its header claims; normalized the frames "
                 "that are present");
            break;
        }
        for (int i = 0; i < got; ++i) {
            peak = std::max(peak, std::fabs(chunk[i]));
        }
        remaining -= static_cast<std::uint64_t>(got);
    }
    std::fclose(file);
    // A NaN sample would poison every later multiply; treat it as silence.
    if (!std::isfinite(peak)) peak = 0.0f;
    return true;
}

bool TakePlayer::setRegions(int track, const std::vector<TakeRegion>& regions,
                            double framesPerBeat, std::uint64_t startFrame,
                            std::string* error) {
    if (track < 0 || track >= kMaxTracks) {
        refuse("track " + std::to_string(track) + " is out of range", error);
        return false;
    }
    if (regions.empty()) {
        clearRegions(track);
        return true;
    }
    if (!std::isfinite(framesPerBeat) || framesPerBeat <= 0.0) {
        refuse("track " + std::to_string(track) +
                   " region edit needs a tempo to map beats to frames",
               error);
        return false;
    }
    const auto beatFrames = [framesPerBeat](double beats) {
        if (!std::isfinite(beats) || beats <= 0.0) return std::uint64_t{0};
        const double frames = std::floor(beats * framesPerBeat + 0.5);
        if (!(frames > 0.0)) return std::uint64_t{0};
        const double limit = static_cast<double>(
            std::numeric_limits<std::uint64_t>::max() / 8);
        if (frames > limit) return static_cast<std::uint64_t>(limit);
        return static_cast<std::uint64_t>(frames);
    };

    // Resolve the whole list before touching the live voice: a refused take
    // must leave the previous state intact, and the normalize peak pass belongs
    // at load/apply time, not in the reader thread.
    std::vector<Region> plan;
    plan.reserve(regions.size());
    std::uint64_t cursor = 0;
    for (std::size_t index = 0; index < regions.size(); ++index) {
        const TakeRegion& region = regions[index];
        const std::string where =
            "track " + std::to_string(track) + " region " +
            std::to_string(index);
        if (region.take.empty()) {
            note(where + " names no take; skipped");
            continue;
        }
        WavInfo info;
        std::string reason;
        if (!inspectTakeWav(region.take, info, reason)) {
            refuse(where + ": " + reason, error);
            return false;
        }
        const std::uint64_t sourceFrames = info.frames();
        std::uint64_t sliceStart = 0;
        if (!std::isfinite(region.offsetBeats) || region.offsetBeats < 0.0) {
            note(where +
                 " has a negative or non-finite offset; starting at the take "
                 "start");
        } else {
            sliceStart = beatFrames(region.offsetBeats);
        }
        if (sliceStart >= sourceFrames) {
            note(where + " starts at or past the end of " +
                 wav_detail::quoted(region.take) + "; skipped");
            continue;
        }
        std::uint64_t sliceFrames = sourceFrames - sliceStart;
        if (region.lengthBeats > 0.0) {
            const std::uint64_t wanted = beatFrames(region.lengthBeats);
            if (wanted > sliceFrames) {
                note(where + " is longer than the rest of " +
                     wav_detail::quoted(region.take) +
                     "; clamped to the take end");
            } else {
                sliceFrames = wanted;
            }
        }
        if (sliceFrames == 0) {
            note(where + " is shorter than one frame; skipped");
            continue;
        }
        std::uint64_t fadeIn = beatFrames(region.fadeInBeats);
        if (fadeIn > sliceFrames) {
            note(where +
                 " fade-in is longer than the region; clamped to the region");
            fadeIn = sliceFrames;
        }
        std::uint64_t fadeOut = beatFrames(region.fadeOutBeats);
        if (fadeOut > sliceFrames) {
            note(where +
                 " fade-out is longer than the region; clamped to the region");
            fadeOut = sliceFrames;
        }
        Region resolved;
        resolved.path = region.take;
        resolved.info = info;
        resolved.sourceStart = sliceStart;
        resolved.frames = sliceFrames;
        resolved.fadeInFrames = fadeIn;
        resolved.fadeOutFrames = fadeOut;
        float gainDb = std::isfinite(region.gainDb) ? region.gainDb : 0.0f;
        gainDb = std::clamp(gainDb, -96.0f, 12.0f);
        resolved.gain = dbToLinear(gainDb);
        if (region.normalize) {
            float peak = 0.0f;
            if (!regionPeak(resolved.path, resolved.info, resolved.sourceStart,
                            resolved.frames, peak, reason)) {
                refuse(where + ": " + reason, error);
                return false;
            }
            // One peak pass per region, scaled to kNormalizePeakDbfs. A region
            // with no signal has no peak to scale to, so its gain is unchanged.
            if (peak > 0.0f) {
                const float normalize = dbToLinear(kNormalizePeakDbfs) / peak;
                if (std::isfinite(normalize) &&
                    std::isfinite(resolved.gain * normalize)) {
                    resolved.gain *= normalize;
                } else {
                    note(where +
                         " peak is below the float range; normalize left the "
                         "gain unchanged");
                }
            }
        }
        resolved.streamStart = cursor;
        cursor += resolved.frames;
        plan.push_back(std::move(resolved));
    }

    enterControl();
    Track& slot = mTracks[track];
    stopTrackLocked(track);
    slot.regionEdits = regions;
    slot.edit = std::move(plan);
    slot.editFrames = cursor;
    slot.framesPerBeat = framesPerBeat;
    slot.editStartFrame = startFrame;
    slot.regionMode.store(true, std::memory_order_release);
    bool ok = startEditLocked(track, error);
    if (!ok) {
        // The plan was resolved before the switch, so this is an I/O failure
        // (for example a file removed between the peak pass and the open). Fall
        // back to the unedited stack rather than leave a half-applied edit.
        slot.regionMode.store(false, std::memory_order_release);
        slot.regionEdits.clear();
        slot.edit.clear();
        slot.editFrames = 0;
        stopEditStream(slot.editStream);
        restartTrackLocked(track, nullptr);
    }
    const bool emptyEdit = ok && slot.editFrames == 0;
    leaveControl();
    if (emptyEdit) {
        note("track " + std::to_string(track) +
             " region edit resolves to no frames; the track plays silence");
    }
    return ok;
}

void TakePlayer::clearRegions(int track) {
    if (track < 0 || track >= kMaxTracks) return;
    enterControl();
    Track& slot = mTracks[track];
    const bool wasRegionMode =
        slot.regionMode.exchange(false, std::memory_order_acq_rel);
    if (wasRegionMode) {
        stopEditStream(slot.editStream);
        slot.regionEdits.clear();
        slot.edit.clear();
        slot.editFrames = 0;
        // The stack was stored while the edit played, so it resumes now.
        if (slot.pathCount > 0) restartTrackLocked(track, nullptr);
    }
    leaveControl();
}

int TakePlayer::readFrames(Layer& layer, float* mono, int frames) {
    if (layer.file == nullptr || mono == nullptr || frames <= 0 ||
        layer.remainingFrames == 0) {
        return 0;
    }
    const int wanted = static_cast<int>(
        std::min<std::uint64_t>(static_cast<std::uint64_t>(frames),
                                layer.remainingFrames));
    const int got = decodeTakeFrames(layer.file, layer.channels,
                                     layer.bytesPerSample, layer.floatSamples,
                                     mono, wanted);
    if (got <= 0) return 0;
    layer.remainingFrames -= static_cast<std::uint64_t>(got);
    return got;
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

void TakePlayer::applySeamFade(std::uint64_t loopStart, std::uint64_t loopEnd,
                               std::uint64_t position, int frames,
                               float* mono) {
    const std::uint64_t loopFrames = loopEnd - loopStart;
    const std::uint64_t fade = loopSeamFade(loopFrames);
    for (int i = 0; i < frames; ++i) {
        const std::uint64_t offset =
            position + static_cast<std::uint64_t>(i) - loopStart;
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
    applySeamFade(layer.loopStartFrames, layer.loopEndFrames, position, got,
                  mono);
    return got;
}

bool TakePlayer::startEditLocked(int track, std::string* error) {
    Track& slot = mTracks[track];
    EditStream& stream = slot.editStream;
    stopEditStream(stream);
    const LoopWindow window =
        loopWindowFor(track, slot, slot.editFrames, slot.editStartFrame);
    stream.loopStartFrames = window.startFrames;
    stream.loopEndFrames = window.endFrames;
    stream.looping.store(window.looping, std::memory_order_relaxed);

    if (slot.editFrames == 0) {
        // An edit that clamps away entirely is an explicit, logged silence, not
        // a fallback to the unedited take and never a crash.
        stream.active.store(true, std::memory_order_release);
        stream.eof.store(true, std::memory_order_relaxed);
        return true;
    }
    const std::uint64_t streamStart = window.looping ? window.startFrames : 0;
    if (streamStart == 0 ? !openEditRegion(slot, 0)
                         : !seekEditStream(slot, streamStart)) {
        const std::size_t at = static_cast<std::size_t>(stream.region);
        const std::string path =
            at < slot.edit.size() ? slot.edit[at].path : std::string();
        refuse("track " + std::to_string(track) + " region edit: cannot open " +
                   wav_detail::quoted(path),
               error);
        stream.active.store(false, std::memory_order_release);
        stream.eof.store(true, std::memory_order_relaxed);
        return false;
    }
    stream.active.store(true, std::memory_order_release);

    // Prime the ring like a layer selection: a normal first block after an
    // apply should not start with an avoidable dropout.
    const std::uint64_t half = static_cast<std::uint64_t>(kRingFrames) / 2;
    const std::uint64_t material =
        slot.editFrames > streamStart ? slot.editFrames - streamStart : 0;
    const std::uint64_t target =
        window.looping ? half : std::min<std::uint64_t>(material, half);
    std::uint64_t primed = 0;
    while (primed < target) {
        const int want = static_cast<int>(
            std::min<std::uint64_t>(kReadChunkFrames, target - primed));
        const int got =
            produceEditFrames(slot, stream.ring.data() + primed, want);
        if (got <= 0) break;
        primed += static_cast<std::uint64_t>(got);
    }
    stream.writeFrame.store(primed, std::memory_order_release);

    if (window.looping || primed < material) {
        stream.thread = std::thread(&TakePlayer::editReadLoop, this, &slot);
    } else {
        stream.eof.store(true, std::memory_order_relaxed);
    }
    return true;
}

bool TakePlayer::openEditRegion(Track& slot, int index) {
    EditStream& stream = slot.editStream;
    if (stream.file != nullptr) {
        std::fclose(stream.file);
        stream.file = nullptr;
    }
    if (index < 0 || index >= static_cast<int>(slot.edit.size())) {
        stream.region = index;
        return false;
    }
    const Region& region = slot.edit[static_cast<std::size_t>(index)];
    std::FILE* file = std::fopen(region.path.c_str(), "rb");
    if (file == nullptr) return false;
    const std::uint64_t byteOffset =
        region.info.dataOffset + region.sourceStart * region.info.frameBytes();
    if (byteOffset > static_cast<std::uint64_t>(LONG_MAX) ||
        std::fseek(file, static_cast<long>(byteOffset), SEEK_SET) != 0) {
        std::fclose(file);
        return false;
    }
    stream.file = file;
    stream.region = index;
    stream.position = 0;
    stream.channels = region.info.channels;
    stream.bytesPerSample = region.info.bits / 8u;
    stream.floatSamples = region.info.floatSamples();
    return true;
}

bool TakePlayer::seekEditStream(Track& slot, std::uint64_t streamPosition) {
    // Regions are ordered and contiguous, so the containing one is the last
    // whose streamStart is <= the target.
    int index = -1;
    for (std::size_t i = 0; i < slot.edit.size(); ++i) {
        if (slot.edit[i].streamStart > streamPosition) break;
        index = static_cast<int>(i);
    }
    if (index < 0) return false;
    const Region& region = slot.edit[static_cast<std::size_t>(index)];
    EditStream& stream = slot.editStream;
    const std::uint64_t offset = streamPosition - region.streamStart;
    if (offset >= region.frames) {
        // The wrap landed exactly on the next region's first frame.
        stream.region = index;
        stream.position = region.frames;
        stream.streamPosition = streamPosition;
        return true;
    }
    if (!openEditRegion(slot, index)) return false;
    const std::uint64_t byteOffset = region.info.dataOffset +
                                     (region.sourceStart + offset) *
                                         region.info.frameBytes();
    if (byteOffset > static_cast<std::uint64_t>(LONG_MAX) ||
        std::fseek(stream.file, static_cast<long>(byteOffset), SEEK_SET) != 0) {
        return false;
    }
    stream.position = offset;
    stream.streamPosition = streamPosition;
    return true;
}

void TakePlayer::stopEditStream(EditStream& stream) {
    stream.active.store(false, std::memory_order_release);
    stream.stop.store(true, std::memory_order_release);
    if (stream.thread.joinable()) stream.thread.join();
    if (stream.file != nullptr) {
        std::fclose(stream.file);
        stream.file = nullptr;
    }
    stream.region = 0;
    stream.channels = 0;
    stream.bytesPerSample = 0;
    stream.floatSamples = false;
    stream.position = 0;
    stream.streamPosition = 0;
    stream.loopStartFrames = 0;
    stream.loopEndFrames = 0;
    stream.writeFrame.store(0, std::memory_order_relaxed);
    stream.readFrame.store(0, std::memory_order_relaxed);
    stream.eof.store(false, std::memory_order_relaxed);
    stream.stop.store(false, std::memory_order_relaxed);
    stream.looping.store(false, std::memory_order_relaxed);
}

int TakePlayer::readEditFrames(Track& slot, float* mono, int frames) {
    EditStream& stream = slot.editStream;
    if (mono == nullptr || frames <= 0 || stream.file == nullptr) return 0;
    int produced = 0;
    while (produced < frames) {
        if (stream.region < 0 ||
            stream.region >= static_cast<int>(slot.edit.size())) {
            break;
        }
        const Region& region = slot.edit[static_cast<std::size_t>(stream.region)];
        if (stream.position >= region.frames) {
            if (!openEditRegion(slot, stream.region + 1)) break;
            continue;
        }
        const std::uint64_t left = region.frames - stream.position;
        const int want = static_cast<int>(
            std::min<std::uint64_t>(static_cast<std::uint64_t>(frames - produced),
                                    left));
        const int got = decodeTakeFrames(stream.file, stream.channels,
                                         stream.bytesPerSample,
                                         stream.floatSamples, mono + produced,
                                         want);
        if (got <= 0) break;
        const std::uint64_t start = stream.position;
        for (int i = 0; i < got; ++i) {
            mono[produced + i] *=
                region.gainAt(start + static_cast<std::uint64_t>(i));
        }
        stream.position += static_cast<std::uint64_t>(got);
        stream.streamPosition += static_cast<std::uint64_t>(got);
        produced += got;
    }
    return produced;
}

int TakePlayer::produceEditFrames(Track& slot, float* mono, int frames) {
    if (mono == nullptr || frames <= 0 || slot.editFrames == 0) return 0;
    EditStream& stream = slot.editStream;
    if (!stream.looping.load(std::memory_order_relaxed)) {
        return readEditFrames(slot, mono, frames);
    }
    if (stream.streamPosition >= stream.loopEndFrames &&
        !seekEditStream(slot, stream.loopStartFrames)) {
        return 0;
    }
    frames = static_cast<int>(std::min<std::uint64_t>(
        static_cast<std::uint64_t>(frames),
        stream.loopEndFrames - stream.streamPosition));
    const std::uint64_t position = stream.streamPosition;
    const int got = readEditFrames(slot, mono, frames);
    if (got <= 0) return 0;
    applySeamFade(stream.loopStartFrames, stream.loopEndFrames, position, got,
                  mono);
    return got;
}

void TakePlayer::editReadLoop(Track* slot) {
    EditStream& stream = slot->editStream;
    float scratch[kReadChunkFrames] = {};
    while (!stream.stop.load(std::memory_order_acquire)) {
        const std::uint64_t write =
            stream.writeFrame.load(std::memory_order_relaxed);
        const std::uint64_t read =
            stream.readFrame.load(std::memory_order_acquire);
        const std::uint64_t used = write - read;
        if (used >= static_cast<std::uint64_t>(kRingFrames)) {
            std::this_thread::sleep_for(std::chrono::microseconds(200));
            continue;
        }
        const int space = kRingFrames - static_cast<int>(used);
        const int want = std::min(kReadChunkFrames, space);
        const int got = produceEditFrames(*slot, scratch, want);
        if (got <= 0) {
            stream.eof.store(true, std::memory_order_relaxed);
            break;
        }
        for (int i = 0; i < got; ++i) {
            stream.ring[static_cast<std::size_t>(
                (write + static_cast<std::uint64_t>(i)) &
                static_cast<std::uint64_t>(kRingFrames - 1))] = scratch[i];
        }
        stream.writeFrame.store(write + static_cast<std::uint64_t>(got),
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
            const bool region =
                mTracks[track].regionMode.load(std::memory_order_acquire);
            const int count =
                mTracks[track].count.load(std::memory_order_acquire);
            const bool takeFed = region || count > 0;
            if (active != nullptr) active[track] = takeFed;
            if (playing != nullptr) playing[track] = rolling && takeFed;
            float* out = monoOut != nullptr ? monoOut[track] : nullptr;
            if (out == nullptr) continue;
            for (int i = 0; i < frames; ++i) out[i] = 0.0f;
        }
        mActiveRenderers.fetch_sub(1, std::memory_order_acq_rel);
        return;
    }
    for (int track = 0; track < kMaxTracks; ++track) {
        Track& slot = mTracks[track];
        const bool region = slot.regionMode.load(std::memory_order_acquire);
        const int count = slot.count.load(std::memory_order_acquire);
        if (active != nullptr) active[track] = region || count > 0;

        bool anyPlaying = false;
        if (region) {
            // One edited stream per track: the region list is the composition,
            // so there is no layering to resolve here.
            const EditStream& stream = slot.editStream;
            anyPlaying =
                stream.active.load(std::memory_order_acquire) &&
                (stream.looping.load(std::memory_order_acquire) ||
                 (slot.editFrames > 0 &&
                  stream.readFrame.load(std::memory_order_relaxed) <
                      slot.editFrames));
        }
        for (int index = 0; !region && index < count; ++index) {
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
        if (!rolling || frames <= 0) continue;
        if (region) {
            EditStream& stream = slot.editStream;
            if (!stream.active.load(std::memory_order_acquire)) continue;
            const bool looping = stream.looping.load(std::memory_order_acquire);
            const std::uint64_t position =
                stream.readFrame.load(std::memory_order_relaxed);
            if (!looping && position >= slot.editFrames) continue;
            const int want =
                looping
                    ? frames
                    : static_cast<int>(std::min<std::uint64_t>(
                          static_cast<std::uint64_t>(frames),
                          slot.editFrames - position));
            const std::uint64_t write =
                stream.writeFrame.load(std::memory_order_acquire);
            const std::uint64_t available =
                write > position ? write - position : 0;
            for (int i = 0; i < want; ++i) {
                if (static_cast<std::uint64_t>(i) < available) {
                    out[i] = stream.ring[static_cast<std::size_t>(
                        (position + static_cast<std::uint64_t>(i)) &
                        static_cast<std::uint64_t>(kRingFrames - 1))];
                } else {
                    // The reader has not delivered this frame yet; render
                    // silence and count it instead of stalling the callback.
                    out[i] = 0.0f;
                    mUnderruns.fetch_add(1, std::memory_order_relaxed);
                }
            }
            stream.readFrame.store(position + static_cast<std::uint64_t>(want),
                                   std::memory_order_release);
            continue;
        }
        if (count == 0) continue;

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
