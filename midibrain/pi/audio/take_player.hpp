#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "capacity.hpp"
#include "wav.hpp"

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

/// One non-destructive region of an edited track: play `lengthBeats` of `take`
/// starting at `offsetBeats` (0 = the take start) with a gain and linear fades
/// applied at render. `lengthBeats` 0 means "to the end of the take", and both
/// fades are measured from the region's own edges. This is the audio-side
/// mirror of the session's frozen `takes.edit` entry (43130e8): files are never
/// rewritten, and the only destructive path (render to a new take) is a later
/// op that is not part of this API.
struct TakeRegion {
    std::string take;
    double offsetBeats = 0.0;
    double lengthBeats = 0.0;
    float gainDb = 0.0f;  // -96..+12
    double fadeInBeats = 0.0;
    double fadeOutBeats = 0.0;
    bool normalize = false;
    friend bool operator==(const TakeRegion&, const TakeRegion&) = default;
};

/// Peak a normalised region is scaled to: -1 dBFS, one decibel below full
/// scale so a normalised region leaves headroom for gain staging downstream
/// instead of landing on 0 dBFS.
inline constexpr float kNormalizePeakDbfs = -1.0f;

/// One take region placed on a song track (session::SongClip is the serialized
/// spelling of exactly these fields). `startBeats` is the song position;
/// `lengthBeats` 0 means "to the end of the take"; `loop` repeats the region in
/// place until the next clip on the track starts (a trailing clip with no
/// successor plays its region once, because nothing defines where it would
/// stop); `muted` skips the clip. Clips on one track must not overlap - the
/// scheduler clamps a defensive overlap to the next clip start and logs it.
struct SongClip {
    int track = 0;
    double startBeats = 0.0;
    std::string take;
    double offsetBeats = 0.0;
    double lengthBeats = 0.0;
    float gainDb = 0.0f;  // -96..+12
    double fadeInBeats = 0.0;
    double fadeOutBeats = 0.0;
    bool normalize = false;
    bool loop = false;
    bool muted = false;
    friend bool operator==(const SongClip&, const SongClip&) = default;
};

/// Auto declick at a clip edge when the clip sets no fade: 5 ms. It is applied
/// at the rendered span's start and end, and the loop-in-place seam uses the
/// shorter kLoopFadeFrames seam ramp instead (the same rule as take looping).
inline constexpr double kSongAutoFadeSeconds = 0.005;

/// Result of TakePlayer::renderRegionToTake()/Engine::renderRegionToTake():
/// the new `NN-<name>-<n>.wav` and its length in 48 kHz frames.
struct RenderedTake {
    std::string path;
    std::uint64_t frames = 0;
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
///
/// A track can also play a non-destructive region edit (setRegions()) instead of
/// its stack: an ordered list of slices of take files, rendered as one edited
/// stream with per-region gain, fades and optional normalize. The edit is
/// resolved on the control thread; the audio thread still only copies a ring.
/// Looping then wraps the edited stream, and clearing the edit resumes the
/// stack.
class TakePlayer {
public:
    /// Mixer tracks; engine.cpp asserts this matches audio::kMaxTracks.
    static constexpr int kMaxTracks = mnky::audio::kMaxTracks;
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

    /// Control-thread region edit for `track` (replace all regions). The track
    /// then plays the region list as one edited stream instead of its take
    /// stack: region after region, each one a slice of its own take file mapped
    /// from session beats with `framesPerBeat`, with its `gainDb`, its linear
    /// fade in/out and - when `normalize` is set - a gain that puts the region's
    /// own peak at kNormalizePeakDbfs. The peak pass and every beat mapping run
    /// here, on the control thread, so `render()` only copies frames.
    ///
    /// Clamping is logged and never fatal: a region that starts at or past the
    /// end of its take is skipped, a length past the take end is clamped to it,
    /// and a fade longer than the region is clamped to the region; every one of
    /// those writes a reason to stderr. Unreadable or non-48 kHz takes refuse
    /// the whole list and leave the previous state untouched, exactly like
    /// setTakes(). An edit that resolves to no frames leaves the track
    /// explicitly silent (logged), not playing the unedited take. An empty list
    /// is clearRegions().
    ///
    /// `startFrame` is the session frame the caller considers "now": it is the
    /// origin the beat loop window is mapped from (the edited stream itself
    /// always starts at its first region). Re-apply after a tempo change.
    bool setRegions(int track, const std::vector<TakeRegion>& regions,
                    double framesPerBeat, std::uint64_t startFrame,
                    std::string* error = nullptr);
    /// Drops the region edit and returns the track to its take stack.
    void clearRegions(int track);
    /// Applied region count (0 when the track plays its stack).
    int regionCount(int track) const;
    /// Length of the edited stream in frames; 0 when no region edit is set.
    std::uint64_t editedFrames(int track) const;

    int takeCount(int track) const;
    /// Path of layer `layer` (0 = oldest); empty for an invalid track/layer.
    std::string takePath(int track, int layer) const;

    // --- RENDER REGION TO TRACK -------------------------------------------
    /// Bakes one region (the TakeRegion fields) to a new 48 kHz mono float WAV
    /// `directory/NN-<name>-<NNN>.wav` (the app's per-track take convention:
    /// track+1 as two digits, the sanitized lowercase name and the next free
    /// three-digit number for that track/name). Offsets, length, gain, fades and
    /// normalize behave exactly like the region edit in setRegions(); `track`
    /// only forms the file name and names the log line. The write happens on
    /// this (control) thread, never in process(). Returns false with a reason
    /// for an unreadable take, a region that resolves to no frames or an I/O
    /// failure, and leaves no partial file behind. This is the renderer behind
    /// RENDER REGION TO TRACK and RENDER SONG's take material.
    bool renderRegionToTake(const TakeRegion& region, double framesPerBeat,
                            const std::string& directory, int track,
                            const std::string& name, RenderedTake& out,
                            std::string* error = nullptr);

    // --- SONG clip scheduler ----------------------------------------------
    /// Replaces `track`'s song lane with `clips` (sorted internally by
    /// startBeats; muted clips are kept for capture but render nothing).
    /// `framesPerBeat` maps the beat fields to 48 kHz frames exactly like
    /// setRegions(); `startFrame` is the transport frame the lane starts
    /// streaming from. A lane with clips gets one reader thread that fills an
    /// absolute-frame ring; the audio thread only copies from the ring, so
    /// process() never opens a file or allocates. The lane is published with an
    /// atomic pointer swap: an edit while playing takes effect within one ring
    /// lookahead (about 85 ms) and never stops or restarts the audio path.
    /// Unreadable/non-48 kHz takes refuse the whole list and leave the previous
    /// lane untouched; an overlap with the next clip is clamped to that clip's
    /// start and logged. Re-apply after a tempo change, like setRegions().
    bool setSongClips(int track, const std::vector<SongClip>& clips,
                      double framesPerBeat, std::uint64_t startFrame,
                      std::string* error = nullptr);
    /// Drops the song lane and stops its reader thread.
    void clearSongClips(int track);
    /// Applied (not muted-filtered) clip count, 0 for an empty lane.
    int songClipCount(int track) const;
    /// Applied clip list in start order (capture reads this back).
    std::vector<SongClip> songClips(int track) const;
    /// Beat length the lane was resolved with; 0 when no clips are applied.
    double songFramesPerBeat(int track) const;

    /// Audio-thread SONG entry point: renders the song lane of every track at
    /// absolute song frame `songFrame` (the transport's frame). `active[t]` is
    /// true while the track has clips (the clip voice owns the strip);
    /// `playing[t]` is true while a clip actually covers the position, so the
    /// engine's Auto monitor keeps passing live input through a gap. The ring
    /// is consumed on every rolling block, gaps included, so the reader can
    /// pre-produce the next clip and a boundary never needs a seek; a transport
    /// jump asks the reader for a seek and renders one silent block.
    void renderSong(bool rolling, int n, std::uint64_t songFrame,
                    float* const* monoOut, bool* active, bool* playing);

    /// Offline RENDER SONG reader: fills `mono` with the song lane at
    /// `songFrame` straight from the take files (no ring, no reader thread).
    /// Control thread only, for Engine::renderSong(). Returns true when a clip
    /// covers the block. beginSongRender()/endSongRender() reset and close the
    /// offline file handles.
    void beginSongRender();
    bool renderSongTrack(int track, std::uint64_t songFrame, int frames,
                         float* mono);
    void endSongRender();

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

    /// One resolved region: the file slice, its render gain envelope and where
    /// it sits in the edited stream. Built by setRegions(); immutable while the
    /// reader thread runs (stop/join precedes any rebuild).
    struct Region {
        std::string path;
        WavInfo info;
        std::uint64_t sourceStart = 0;  // first frame of the slice
        std::uint64_t frames = 0;       // slice length
        std::uint64_t fadeInFrames = 0;
        std::uint64_t fadeOutFrames = 0;
        float gain = 1.0f;              // gainDb and normalize combined
        std::uint64_t streamStart = 0;  // offset in the edited stream

        /// Render gain at `position` frames into the region: the region gain
        /// times the linear fade ramps, which both reach exactly 0 at their
        /// region edge and 1 at their inner end.
        float gainAt(std::uint64_t position) const;
    };

    /// The edited take voice: one ring and one reader thread walking the region
    /// list in order. Gain and fades are applied by the reader thread while it
    /// fills the ring, so the audio thread only copies samples.
    struct EditStream {
        EditStream() : ring(kRingFrames, 0.0f) {}

        std::FILE* file = nullptr;
        int region = 0;                        // reader-thread cursor
        int channels = 0;
        int bytesPerSample = 0;
        bool floatSamples = false;
        std::uint64_t position = 0;        // frame inside the current region
        std::uint64_t streamPosition = 0;  // frame inside the edited stream
        std::uint64_t loopStartFrames = 0;
        std::uint64_t loopEndFrames = 0;
        std::vector<float> ring;
        std::atomic<std::uint64_t> writeFrame{0};
        std::atomic<std::uint64_t> readFrame{0};
        std::atomic<bool> active{false};
        std::atomic<bool> stop{false};
        std::atomic<bool> eof{false};
        std::atomic<bool> looping{false};
        std::thread thread;
    };

    /// One resolved song clip: the take slice, its song-frame placement and the
    /// render envelope. Resolved on the control thread; immutable while the
    /// lane is published.
    struct SongClipPlan {
        std::string path;
        WavInfo info;
        std::uint64_t startFrame = 0;   // song frame of the clip start
        std::uint64_t endFrame = 0;     // song frame where the clip stops
        std::uint64_t sourceStart = 0;  // first take frame of the region
        std::uint64_t regionFrames = 0; // slice length
        std::uint64_t fadeInFrames = 0;
        std::uint64_t fadeOutFrames = 0;
        float gain = 1.0f;              // gainDb and normalize combined
        bool loop = false;

        /// Render gain at `position` song frames after the clip start: the
        /// region gain, the linear fades over the clip's play span and - for a
        /// loop clip - the seam ramp around every wrap of the region.
        float gainAt(std::uint64_t position) const;
    };

    /// An immutable song lane. `id` lets a reader detect a lane swap and reopen
    /// its take file; the clip list is sorted by startFrame and the spans never
    /// overlap.
    struct SongLane {
        std::uint64_t id = 0;
        double framesPerBeat = 0.0;
        std::vector<SongClipPlan> clips;

        /// Index of the clip covering `frame`, or -1 in a gap.
        int covering(std::uint64_t frame) const;
    };

    /// Reader-thread state for one song stream: the open take file and the
    /// next source frame to decode. Reset whenever the lane id changes.
    struct SongReadState {
        std::uint64_t laneId = 0;
        std::FILE* file = nullptr;
        int clip = -1;
        std::uint64_t sourceFrame = 0;
        bool shortRead = false;
    };

    /// One track's song stream: an absolute-song-frame ring plus the reader
    /// thread that keeps it about half full. `readFrame` is the audio thread's
    /// next song frame, `writeFrame` the reader's; `seekFrame` is a transport
    /// jump request (kNoSongFrame when none).
    struct SongStream {
        SongStream() : ring(kRingFrames, 0.0f) {}

        std::vector<float> ring;
        std::atomic<std::uint64_t> writeFrame{0};
        std::atomic<std::uint64_t> readFrame{0};
        std::atomic<std::uint64_t> seekFrame{
            std::numeric_limits<std::uint64_t>::max()};
        std::atomic<bool> stop{false};
        std::atomic<bool> running{false};
        std::thread thread;
        SongReadState state;
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
        // Region edit (control-thread state; the audio thread reads regionMode
        // and the edit stream's atomics only). A non-empty edit replaces the
        // stack as the track's take voice.
        std::atomic<bool> regionMode{false};
        std::vector<TakeRegion> regionEdits;
        std::vector<Region> edit;
        std::uint64_t editFrames = 0;
        std::uint64_t editStartFrame = 0;
        EditStream editStream;
        // Song lane (control-thread edits) and the reader thread that streams
        // it. The atomic lane pointer is the only scheduler state the audio
        // thread loads besides this stream's ring.
        std::vector<SongClip> songEdits;
        double songFramesPerBeat = 0.0;
        SongStream songStream;
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
                             std::uint64_t lengthFrames,
                             std::uint64_t origin) const;
    bool startLayer(int track, Layer& layer, const std::string& path,
                    const WavInfo& info, std::uint64_t startFrame,
                    std::string* error);
    void stopLayer(Layer& layer);
    void readLoop(Layer* layer);
    int produceFrames(Layer& layer, float* mono, int frames);
    int readFrames(Layer& layer, float* mono, int frames);
    static bool seekStream(Layer& layer, std::uint64_t position);
    static void applySeamFade(std::uint64_t loopStart, std::uint64_t loopEnd,
                              std::uint64_t position, int frames, float* mono);
    static void refuse(const std::string& message, std::string* error);
    /// Clamp/skip reason that does not fail the apply (setRegions).
    static void note(const std::string& message);

    // --- region edit internals -------------------------------------------
    bool startEditLocked(int track, std::string* error);
    bool openEditRegion(Track& slot, int index);
    bool seekEditStream(Track& slot, std::uint64_t streamPosition);
    void stopEditStream(EditStream& stream);
    void editReadLoop(Track* slot);
    int produceEditFrames(Track& slot, float* mono, int frames);
    int readEditFrames(Track& slot, float* mono, int frames);
    /// Mono peak of one region's frames, for `normalize`. Reads the file in
    /// chunks and returns false when the file cannot be read.
    static bool regionPeak(const std::string& path, const WavInfo& info,
                           std::uint64_t startFrame, std::uint64_t frames,
                           float& peak, std::string& error);
    /// Resolves one TakeRegion to its slice, gain/fades and normalize gain.
    /// `skip` reports a logged clamp/skip (the caller keeps going); false is a
    /// hard error (unreadable take, unreadable normalize pass).
    bool resolveRegion(const TakeRegion& region, const std::string& where,
                       double framesPerBeat, Region& resolved, bool& skip,
                       std::string* error);

    // --- song scheduler internals ----------------------------------------
    static constexpr int kSongLaneSlots = 4;
    /// Resolves `clips` into a new lane (peak passes, clamping, sorting).
    bool resolveSongLane(int track, const std::vector<SongClip>& clips,
                         double framesPerBeat, SongLane& lane,
                         std::string* error);
    /// Waits until no audio render and no song reader holds a lane pointer, so
    /// a lane slot can be rewritten or freed safely. Never called by the audio
    /// thread.
    void waitSongQuiescent();
    void startSongStreamLocked(int track, const SongLane* lane,
                               std::uint64_t startFrame);
    void stopSongStreamLocked(int track);
    void songReadLoop(int track);
    /// Produces up to `frames` song frames at absolute `position` from `lane`.
    /// Gaps render zeros; the function always returns `frames` so a reader can
    /// keep its absolute cursor exact. Used by the reader thread, the priming
    /// pass and the offline renderer.
    int readSongFrames(const SongLane& lane, SongReadState& state,
                       std::uint64_t position, float* mono, int frames);
    static void closeSongReadState(SongReadState& state);

    /// New `NN-<name>-<NNN>.wav` path for renderRegionToTake(), scanning
    /// `directory` for the next free number of that track/name.
    static std::string nextRenderedTakePath(const std::string& directory,
                                            int track, const std::string& name);

    Track mTracks[kMaxTracks];
    // Song lanes: per track a small pool of immutable lanes, published with one
    // atomic pointer store. A slot is rewritten only after waitSongQuiescent(),
    // so a reader that loaded the previous pointer can finish first.
    std::array<std::array<std::unique_ptr<SongLane>, kSongLaneSlots>, kMaxTracks>
        mSongLaneSlots;
    std::atomic<const SongLane*> mSongLane[kMaxTracks] = {};
    int mSongLaneNext[kMaxTracks] = {};
    std::atomic<std::uint64_t> mSongLaneSerial{0};
    std::atomic<int> mSongReadersActive{0};
    // Offline render cursors (RENDER SONG), control thread only.
    SongReadState mSongRenderState[kMaxTracks];

    std::atomic<std::uint64_t> mUnderruns{0};
    // Selection hand-off: render() backs out while a control thread mutates a
    // stack, and the control thread waits for an in-flight render to finish.
    std::atomic<bool> mControlBusy{false};
    std::atomic<int> mActiveRenderers{0};
};

}  // namespace mnky::audio
