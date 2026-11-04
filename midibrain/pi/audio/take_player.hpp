#pragma once

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

namespace mnky::audio {

/// Per-track take loop, expressed in session beats (fractional allowed).
/// `enabled` with `endBeats <= startBeats` loops the whole active take.
/// Looping is playback-only: it never changes recording, the recorder tap or
/// the monitor modes.
struct TakeLoop {
    bool enabled = false;
    double startBeats = 0.0;
    double endBeats = 0.0;
    friend bool operator==(const TakeLoop&, const TakeLoop&) = default;
};

/// Streamed take playback for the engine's disarmed input strips.
///
/// A take is a RIFF/WAVE file decoded to the engine's float path (no Opus and
/// no hidden resampling). Only 48 kHz mono/stereo IEEE-float (format 3) or
/// 16-bit PCM (format 1) files are accepted; anything else is refused with a
/// reason and a stderr log line. Stereo take data is downmixed to mono with
/// 0.5 * (L + R) because the engine has one input path per track.
///
/// Each track holds an ordered take stack, oldest first. `setTakes()` replaces
/// the stack; `setTake()` is the single-take convenience form. Where stacks
/// overlap, the newest entry (the last path) wins sample-for-sample and older
/// entries are muted there. Older audio becomes audible again after the newer
/// file ends, which is the layered-take "newest-wins" rule.
///
/// Playback starts at the session frame supplied by the engine when the stack
/// is selected, or at the loop start when the track has an enabled loop. The
/// reader thread streams from that file frame into a bounded ring; the audio
/// thread only copies from the ring. Underruns are counted and rendered as
/// silence, never as a fallback to the live input. Selection is a
/// control-thread operation; the audio thread sees the track as active or not
/// and never opens a file or allocates.
///
/// Looping keeps the reader thread streaming: at the loop end it seeks back to
/// the loop start, so the ring always holds a continuous looped stream and the
/// audio thread never re-seeks. Each seam is declicked with a short fade to
/// silence on both sides (see loopSeamFade()).
class TakePlayer {
public:
    /// Must match Engine's kMaxInputs; engine.cpp asserts this.
    static constexpr int kMaxTracks = 18;
    static constexpr int kMaxTakesPerTrack = 4;
    /// ~171 ms at 48 kHz. A selection primes half of it synchronously so the
    /// first block after setTakes() normally has data.
    static constexpr int kRingFrames = 8192;
    /// Declick ramp at each loop seam, capped at a quarter of the loop.
    static constexpr std::uint64_t kLoopFadeFrames = 64;

    TakePlayer();
    ~TakePlayer();
    TakePlayer(const TakePlayer&) = delete;
    TakePlayer& operator=(const TakePlayer&) = delete;

    /// Replaces `track`'s take stack with `paths` (oldest first). `startFrame`
    /// is the session frame that maps to the first sample of every file.
    /// Returns false and leaves the previous stack untouched when any path is
    /// unreadable, not 48 kHz, or not a supported WAV layout.
    bool setTakes(int track, const std::vector<std::string>& paths,
                  std::uint64_t startFrame, std::string* error = nullptr);
    /// Single-take convenience wrapper; an empty path clears the track.
    bool setTake(int track, const std::string& path, std::uint64_t startFrame,
                 std::string* error = nullptr);

    /// Control-thread loop selection for `track`. `framesPerBeat` is the
    /// transport's beat length in frames (the engine passes its tempo), which
    /// maps the beat window onto take frames: the loop covers
    /// [startBeats * framesPerBeat, endBeats * framesPerBeat) in session
    /// frames, clamped to the selected stream. `endBeats <= startBeats` loops
    /// each layer's whole selected stream. A change restarts the track's stack
    /// at the loop start; an unchanged state is a no-op. The loop state is kept
    /// when the selection is cleared, so a later setTakes() inherits it.
    /// Returns false only for an out-of-range track or an unreadable file.
    bool setLoop(int track, const TakeLoop& loop, double framesPerBeat,
                 std::string* error = nullptr);

    void clearTrack(int track);
    void clearAll();

    int takeCount(int track) const;
    /// Path of layer `layer` (0 = oldest); empty for an invalid track/layer.
    std::string takePath(int track, int layer) const;

    /// Audio-thread entry point. `monoOut[t]` receives the newest-wins mix for
    /// track t and is zeroed even when the transport is stopped. `active[t]`
    /// is true while a take stack is selected; `playing[t]` is true only while
    /// the transport is rolling and at least one selected layer still owns
    /// frames. The engine uses the pair to keep a disarmed take track from
    /// doubling its live input. `n` must be <= 256 (Engine passes <= kBlockSize).
    void render(bool rolling, int n, float* const* monoOut, bool* active,
                bool* playing);

    std::uint64_t underruns() const {
        return mUnderruns.load(std::memory_order_relaxed);
    }

    /// Length of the seam declick ramp: kLoopFadeFrames, capped at a quarter of
    /// `loopFrames` and never below 1.
    static std::uint64_t loopSeamFade(std::uint64_t loopFrames);

private:
    struct WavInfo {
        std::uint64_t dataOffset = 0;
        std::uint64_t dataBytes = 0;
        std::uint32_t sampleRate = 0;
        std::uint16_t channels = 0;
        std::uint16_t format = 0;
        std::uint16_t bits = 0;
    };

    /// Layer-local loop window in take frames. `looping` false is the plain
    /// linear stream.
    struct LoopWindow {
        bool looping = false;
        std::uint64_t startFrames = 0;
        std::uint64_t endFrames = 0;
    };

    struct Layer {
        Layer() : ring(kRingFrames, 0.0f) {}

        std::string path;
        std::FILE* file = nullptr;
        std::uint64_t remainingFrames = 0;
        std::uint64_t lengthFrames = 0;
        // Loop window (written before the reader thread starts, then stable
        // until it is joined again) and the file mapping a wrap needs.
        std::uint64_t loopStartFrames = 0;
        std::uint64_t loopEndFrames = 0;
        std::uint64_t dataOffset = 0;
        std::uint64_t originFrame = 0;
        std::uint64_t frameBytes = 0;
        int channels = 0;
        int bytesPerSample = 0;
        bool floatSamples = false;
        std::vector<float> ring;
        std::atomic<std::uint64_t> writeFrame{0};
        std::atomic<std::uint64_t> readFrame{0};
        std::atomic<bool> active{false};
        std::atomic<bool> stop{false};
        std::atomic<bool> eof{false};
        std::atomic<bool> looping{false};
        std::thread thread;
    };

    struct Track {
        Layer layers[kMaxTakesPerTrack];
        std::atomic<int> count{0};
        // Control-thread selection, kept so a loop change can re-open the same
        // stack without asking the caller again.
        std::string paths[kMaxTakesPerTrack];
        WavInfo infos[kMaxTakesPerTrack];
        int pathCount = 0;
        std::uint64_t startFrame = 0;
        TakeLoop loop;
        double framesPerBeat = 0.0;
    };

    static bool inspectWav(const std::string& path, WavInfo& info,
                           std::string* error);
    void enterControl();
    void leaveControl();
    void clearTrackLocked(int track);
    void stopTrackLocked(int track);
    bool startTrackLocked(int track, std::string* error);
    bool restartTrackLocked(int track, std::string* error);
    LoopWindow loopWindowFor(int track, const Track& slot,
                             std::uint64_t lengthFrames) const;
    bool startLayer(int track, Layer& layer, const std::string& path,
                    const WavInfo& info, std::uint64_t startFrame,
                    std::string* error);
    void stopLayer(Layer& layer);
    void readLoop(Layer* layer);
    int produceFrames(Layer& layer, float* mono, int frames);
    int readFrames(Layer& layer, float* mono, int frames);
    static bool seekStream(Layer& layer, std::uint64_t position);
    static void applyLoopFade(const Layer& layer, std::uint64_t position,
                              int frames, float* mono);
    static void refuse(const std::string& message, std::string* error);

    Track mTracks[kMaxTracks];
    std::atomic<std::uint64_t> mUnderruns{0};
    // Selection hand-off: render() backs out while a control thread mutates a
    // stack, and the control thread waits for an in-flight render to finish.
    std::atomic<bool> mControlBusy{false};
    std::atomic<int> mActiveRenderers{0};
};

}  // namespace mnky::audio
