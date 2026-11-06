// Assert-style acceptance tests for the DAW engine wave: REC SOURCE tap points
// (PRE / POST / POSTFX), RENDER REGION TO TRACK, the SONG clip scheduler and
// RENDER SONG, plus a CPU probe for the clip voice.
// Build (standalone): part of the audio module, see CMakeLists.txt.
//
// Contract under test:
//   - tap() returns the live input at the selected recSource; take/clip audio
//     never enters a recording; the default PRE is the historical tap.
//   - region renders write a new 48 kHz mono `NN-<name>-<NNN>.wav` with the
//     region's offset/length/gain/fades/normalize.
//   - clips are position-driven per block, sorted by start, auto-declicked at
//     5 ms, loop in place until the next clip, muted/skipped, double-buffered
//     for edits while running.
//   - RENDER SONG writes the master stereo 48 kHz WAV through the ordinary
//     strip path (EQ, rack, fader, pan, master comp/Main EQ; click excluded).

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

#include "engine.hpp"
#include "recorder.hpp"

namespace {

using namespace mnky::audio;

int gFailures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            ++gFailures;                                                     \
        }                                                                    \
    } while (0)

void checkNear(const char* what, double actual, double expected,
               double eps = 1e-4) {
    if (std::fabs(actual - expected) <= eps) return;
    std::printf("FAIL %s: actual %.9g expected %.9g (eps %.3g)\n", what, actual,
                expected, eps);
    ++gFailures;
}

constexpr double kFramesPerBeat = 24000.0;  // 120 bpm at 48 kHz

std::filesystem::path testRoot() {
    return std::filesystem::temp_directory_path() /
           ("mnky_daw_song_" + std::to_string(static_cast<long>(::getpid())));
}

bool writeWav(const std::filesystem::path& path, uint32_t sampleRate,
              int channels, const std::vector<float>& interleaved) {
    WavFileWriter writer;
    if (!writer.open(path.string(), channels, sampleRate)) return false;
    const int frames = static_cast<int>(interleaved.size() /
                                        static_cast<std::size_t>(channels));
    if (!writer.writeFrames(interleaved.data(), frames)) {
        writer.close();
        return false;
    }
    return writer.close();
}

bool writeConstant(const std::filesystem::path& path, int frames, float value) {
    return writeWav(path, kTakeSampleRate, 1,
                    std::vector<float>(static_cast<std::size_t>(frames), value));
}

/// Reads a 48 kHz mono/stereo float WAV back as per-channel frames.
bool readWav(const std::filesystem::path& path, std::vector<float>& left,
             std::vector<float>& right) {
    WavInfo info;
    std::string reason;
    if (!inspectTakeWav(path.string(), info, reason)) {
        std::printf("FAIL readWav %s: %s\n", path.c_str(), reason.c_str());
        return false;
    }
    std::FILE* file = std::fopen(path.string().c_str(), "rb");
    if (file == nullptr) return false;
    if (std::fseek(file, static_cast<long>(info.dataOffset), SEEK_SET) != 0) {
        std::fclose(file);
        return false;
    }
    const std::uint64_t frames = info.frames();
    left.assign(static_cast<std::size_t>(frames), 0.0f);
    right.assign(static_cast<std::size_t>(frames), 0.0f);
    std::vector<unsigned char> raw(
        static_cast<std::size_t>(info.frameBytes()));
    for (std::uint64_t frame = 0; frame < frames; ++frame) {
        if (std::fread(raw.data(), raw.size(), 1, file) != 1) {
            std::fclose(file);
            return false;
        }
        const bool floatSamples = info.floatSamples();
        const auto sample = [&](int channel) {
            const unsigned char* bytes =
                raw.data() + static_cast<std::size_t>(channel) *
                                 static_cast<std::size_t>(info.bits / 8u);
            if (floatSamples) return wav_detail::leFloat(bytes);
            return static_cast<float>(
                       static_cast<std::int16_t>(wav_detail::le16(bytes))) /
                   32768.0f;
        };
        left[static_cast<std::size_t>(frame)] = sample(0);
        right[static_cast<std::size_t>(frame)] =
            info.channels > 1 ? sample(1) : sample(0);
    }
    std::fclose(file);
    return true;
}

float maxStep(const std::vector<float>& samples, std::size_t from,
              std::size_t to) {
    float step = 0.0f;
    for (std::size_t i = from + 1; i < to && i < samples.size(); ++i) {
        step = std::max(step, std::fabs(samples[i] - samples[i - 1]));
    }
    return step;
}

// ------------------------------------------------------------- test helpers

/// Constant multiplier effect for the FX-rack chain (the CLAP seam).
class GainEffect final : public IEffect {
public:
    explicit GainEffect(float gain) : mGain(gain) {}
    void process(float* const* ch, int nch, int n) override {
        for (int channel = 0; channel < nch; ++channel) {
            for (int i = 0; i < n; ++i) ch[channel][i] *= mGain;
        }
    }
    const char* name() const override { return "test.gain"; }

private:
    float mGain;
};

void configureStrip(Engine& engine, int track, float gain = 1.0f,
                    float vol = 1.0f, float pan = 0.5f) {
    Channel channel;
    channel.gain = gain;
    channel.vol = vol;
    channel.pan = pan;
    channel.monitor = MonitorMode::Off;
    engine.setChannel(track, channel);
}

// ------------------------------------------------------------- REC SOURCE tap

void testRecSourceTaps() {
    Engine engine;
    const float inputValue = 0.25f;
    std::vector<float> input(kBlockSize, inputValue);
    const float* inputs[kMaxInputs] = {input.data()};
    float outL[kBlockSize] = {};
    float outR[kBlockSize] = {};

    Channel strip;
    strip.gain = 2.0f;
    strip.vol = 0.5f;
    strip.monitor = MonitorMode::Off;
    strip.tapPoint = TapPoint::PostGain;
    // Low shelf +12 dB at 100 Hz: the steady-state (DC) gain is exactly
    // 10^(12/20), so a constant input gives a predictable POST level.
    EqBand shelf;
    shelf.type = 2;  // low shelf
    shelf.gainDb = 12.0f;
    shelf.freqHz = 100.0f;
    shelf.q = 0.707f;
    engine.setTrackEqBand(0, 0, shelf);
    engine.setChannel(0, strip);

    const auto settle = [&]() {
        for (int block = 0; block < 24; ++block) {
            engine.process(inputs, 1, kBlockSize, outL, outR);
        }
    };

    // Default is PRE = today's post-gain tap.
    CHECK(strip.recSource == RecSource::Pre);
    settle();
    const float* tap = engine.tap(0);
    CHECK(tap != nullptr);
    checkNear("PRE is the historical post-gain tap", tap[0],
              inputValue * strip.gain);

    // POST = gain -> EQ -> fader, pre-rack, pre-pan.
    engine.setRecSource(0, RecSource::Post);
    settle();
    const double shelfGain = std::pow(10.0, 12.0 / 20.0);
    const double post = inputValue * 2.0 * shelfGain * 0.5;
    checkNear("POST is post-EQ and post-fader", tap[0], post, 1e-3);
    // Mute/solo never gate the recorder tap.
    strip.mute = true;
    strip.recSource = RecSource::Post;
    engine.setChannel(0, strip);
    settle();
    checkNear("POST tap ignores mute", tap[0], post, 1e-3);
    strip.mute = false;
    engine.setChannel(0, strip);

    // POSTFX adds the FX rack (FX1/FX2 wet) before the fader.
    GainEffect half(0.25f);
    TrackFxSlot slot;
    slot.id = "test.gain";
    slot.wet = 1.0f;
    engine.setTrackFxSlot(0, 0, slot);
    engine.setTrackFxSlotEffect(0, 0, &half);
    engine.setRecSource(0, RecSource::PostFx);
    settle();
    checkNear("POSTFX is POST through the FX rack", tap[0], post * 0.25, 1e-3);

    // POST stays upstream of the same rack.
    engine.setRecSource(0, RecSource::Post);
    settle();
    checkNear("POST is upstream of the FX rack", tap[0], post, 1e-3);

    // Invariant: take audio never enters a recording, at any tap point.
    const std::filesystem::path take =
        testRoot() / "tap-voice" / "01-voice-001.wav";
    std::error_code ec;
    std::filesystem::create_directories(take.parent_path(), ec);
    CHECK(writeConstant(take, 4 * static_cast<int>(kFramesPerBeat), 0.9f));
    engine.setTransport(true, 0, 960, 12000);
    CHECK(engine.setActiveTake(0, take.string()));
    strip.monitor = MonitorMode::On;  // take audible alongside the live input
    engine.setChannel(0, strip);
    for (const RecSource source :
         {RecSource::Pre, RecSource::Post, RecSource::PostFx}) {
        engine.setRecSource(0, source);
        settle();
        // The take voice is loud in the main output, but the tap stays live-only.
        const double expected =
            source == RecSource::Pre
                ? inputValue * strip.gain
                : (source == RecSource::PostFx ? post * 0.25 : post);
        checkNear("tap stays live-only with a take playing", tap[0], expected,
                  1e-3);
    }

    // A silent live source taps silence even while the take plays.
    engine.setInputSource(0, kNoInputSource);
    engine.setRecSource(0, RecSource::PostFx);
    settle();
    checkNear("no live source taps silence", tap[0], 0.0);

    std::printf("PASS daw: REC SOURCE PRE/POST/POSTFX tap matrix\n");
}

// ------------------------------------------------------ RENDER REGION TO TRACK

void testRenderRegionToTake() {
    const std::filesystem::path root = testRoot() / "render-region";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    CHECK(!ec);

    // A take whose sample value names its own frame: i / 48000.
    std::vector<float> ramp(2 * static_cast<int>(kFramesPerBeat));
    for (std::size_t i = 0; i < ramp.size(); ++i) {
        ramp[i] = static_cast<float>(i) / 48000.0f;
    }
    const std::filesystem::path take = root / "source.wav";
    CHECK(writeWav(take, kTakeSampleRate, 1, ramp));

    TakePlayer player;
    TakeRegion region;
    region.take = take.string();
    region.offsetBeats = 1.0;       // frame 24000
    region.lengthBeats = 0.5;       // 12000 frames
    region.gainDb = -6.0f;
    region.fadeInBeats = 0.1;       // 2400 frames
    region.fadeOutBeats = 0.1;      // 2400 frames
    RenderedTake rendered;
    std::string error;
    CHECK(player.renderRegionToTake(region, kFramesPerBeat, root.string(), 0,
                                    "Bake Test", rendered, &error));
    CHECK(error.empty());
    CHECK(rendered.frames == 12000);
    CHECK(std::filesystem::path(rendered.path).filename() ==
          "01-bake-test-001.wav");

    std::vector<float> left;
    std::vector<float> right;
    CHECK(readWav(rendered.path, left, right));
    CHECK(left.size() == 12000);
    const double gain = std::pow(10.0, -6.0 / 20.0);
    checkNear("region render offset", left[0], 0.0);  // fade-in starts at 0
    checkNear("region render mid fade-in",
              left[1200], ramp[25200] * gain * 0.5, 1e-6);
    checkNear("region render full gain", left[5000], ramp[29000] * gain, 1e-6);
    checkNear("region render fade-out start", left[9600],
              ramp[33600] * gain * (2399.0 / 2400.0), 1e-6);
    checkNear("region render last frame", left[11999], 0.0, 1e-6);
    std::printf("PASS daw: RENDER REGION offset/gain/fades\n");

    // normalize puts the region peak at -1 dBFS.
    const std::filesystem::path flat = root / "flat.wav";
    CHECK(writeConstant(flat, 2 * static_cast<int>(kFramesPerBeat), 0.25f));
    TakeRegion normalized;
    normalized.take = flat.string();
    normalized.lengthBeats = 0.5;
    normalized.normalize = true;
    RenderedTake second;
    CHECK(player.renderRegionToTake(normalized, kFramesPerBeat, root.string(),
                                    0, "Bake Test", second, &error));
    CHECK(std::filesystem::path(second.path).filename() ==
          "01-bake-test-002.wav");
    CHECK(readWav(second.path, left, right));
    checkNear("region render normalize", left[0], std::pow(10.0, -1.0 / 20.0),
              1e-5);
    CHECK(second.frames == 12000);
    std::printf("PASS daw: RENDER REGION normalize + take naming\n");
}

// --------------------------------------------------------- clip scheduler

struct ClipRender {
    std::vector<float> samples;
    int blocks = 0;
    int silentBlocks = 0;
};

/// Renders `frames` from `player` at 1/4 real time (sleep per block) so the
/// reader thread is never the limiting factor in the assertions.
ClipRender renderClipVoice(TakePlayer& player, int track, int frames,
                           std::uint64_t startFrame = 0) {
    constexpr int kBlock = 128;
    std::vector<std::vector<float>> buffers(
        static_cast<std::size_t>(TakePlayer::kMaxTracks),
        std::vector<float>(kBlock, 0.0f));
    std::vector<float*> pointers(
        static_cast<std::size_t>(TakePlayer::kMaxTracks));
    for (int i = 0; i < TakePlayer::kMaxTracks; ++i) {
        pointers[static_cast<std::size_t>(i)] =
            buffers[static_cast<std::size_t>(i)].data();
    }
    std::array<bool, TakePlayer::kMaxTracks> active{};
    std::array<bool, TakePlayer::kMaxTracks> playing{};
    ClipRender result;
    result.samples.reserve(static_cast<std::size_t>(frames));
    for (int frame = 0; frame < frames; frame += kBlock) {
        const int n = std::min(kBlock, frames - frame);
        player.renderSong(true, n, startFrame + static_cast<std::uint64_t>(frame),
                          pointers.data(), active.data(), playing.data());
        bool any = false;
        for (int i = 0; i < n; ++i) {
            result.samples.push_back(buffers[static_cast<std::size_t>(track)]
                                            [static_cast<std::size_t>(i)]);
            any = any || buffers[static_cast<std::size_t>(track)]
                                 [static_cast<std::size_t>(i)] != 0.0f;
        }
        if (!any) ++result.silentBlocks;
        ++result.blocks;
        std::this_thread::sleep_for(std::chrono::microseconds(400));
    }
    return result;
}

void testSongScheduler() {
    const std::filesystem::path root = testRoot() / "scheduler";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    CHECK(!ec);
    const std::filesystem::path takeA = root / "a.wav";
    const std::filesystem::path takeB = root / "b.wav";
    const std::filesystem::path takeC = root / "c.wav";
    CHECK(writeConstant(takeA, 2 * static_cast<int>(kFramesPerBeat), 0.5f));
    CHECK(writeConstant(takeB, 2 * static_cast<int>(kFramesPerBeat), -0.5f));
    CHECK(writeConstant(takeC, 2 * static_cast<int>(kFramesPerBeat), 0.25f));

    TakePlayer player;
    SongClip a;
    a.take = takeA.string();
    a.startBeats = 0.0;
    a.lengthBeats = 1.0;
    SongClip b;
    b.take = takeB.string();
    b.startBeats = 1.0;
    b.lengthBeats = 1.0;
    std::string error;
    CHECK(player.setSongClips(0, {a, b}, kFramesPerBeat, 0, &error));
    CHECK(error.empty());
    CHECK(player.songClipCount(0) == 2);
    CHECK(player.songFramesPerBeat(0) == kFramesPerBeat);

    const int frames = static_cast<int>(2 * kFramesPerBeat);
    const ClipRender rendered = renderClipVoice(player, 0, frames);
    CHECK(rendered.silentBlocks == 0);
    checkNear("clip A plays", rendered.samples[1000], 0.5, 1e-6);
    checkNear("clip A still playing before the boundary",
              rendered.samples[23700], 0.5, 1e-6);
    checkNear("clip B plays after the boundary",
              rendered.samples[25000], -0.5, 1e-6);
    checkNear("clip B plays to the end", rendered.samples[47000], -0.5, 1e-6);
    // 5 ms auto-fade at 48 kHz = 240 frames: a hard cut would step by 1.0.
    const float boundaryStep = maxStep(rendered.samples, 23800, 24300);
    CHECK(boundaryStep < 0.01f);
    std::printf("PASS daw: clip boundary, 5 ms auto-fade (max step %.5f)\n",
                static_cast<double>(boundaryStep));

    // loop in place until the next clip starts at beat 2.
    SongClip loopClip = a;
    loopClip.loop = true;
    SongClip later = b;
    later.startBeats = 2.0;
    CHECK(player.setSongClips(0, {loopClip, later}, kFramesPerBeat, 0,
                              &error));
    const ClipRender looped =
        renderClipVoice(player, 0, static_cast<int>(3 * kFramesPerBeat));
    checkNear("loop clip wraps in place", looped.samples[25000], 0.5, 1e-6);
    checkNear("loop clip still playing at the second wrap",
              looped.samples[47000], 0.5, 1e-6);
    checkNear("next clip takes over after the loop",
              looped.samples[48500], -0.5, 1e-6);
    CHECK(maxStep(looped.samples, 23900, 24100) < 0.01f);
    std::printf("PASS daw: clip loop-in-place + seam declick\n");

    // A muted clip renders nothing.
    SongClip muted = b;
    muted.startBeats = 0.0;
    muted.muted = true;
    CHECK(player.setSongClips(0, {muted}, kFramesPerBeat, 0, &error));
    const ClipRender silent =
        renderClipVoice(player, 0, static_cast<int>(kFramesPerBeat) / 2);
    CHECK(silent.silentBlocks == silent.blocks);
    std::printf("PASS daw: muted clip is silent\n");

    // An empty lane is silence and reports inactive.
    player.clearSongClips(0);
    CHECK(player.songClipCount(0) == 0);
    const ClipRender empty =
        renderClipVoice(player, 0, static_cast<int>(kFramesPerBeat) / 4);
    CHECK(empty.silentBlocks == empty.blocks);

    // Editing the lane while it runs never stops the audio path.
    CHECK(player.setSongClips(0, {a, b}, kFramesPerBeat, 0, &error));
    // Prime several blocks, then publish clip C at 0 and keep rendering.
    const int before = 8 * 128;
    ClipRender first = renderClipVoice(player, 0, before);
    CHECK(first.silentBlocks == 0);
    SongClip c;
    c.take = takeC.string();
    c.startBeats = 0.0;
    c.lengthBeats = 2.0;
    CHECK(player.setSongClips(0, {c}, kFramesPerBeat, 0, &error));
    const ClipRender after =
        renderClipVoice(player, 0, static_cast<int>(kFramesPerBeat),
                        static_cast<std::uint64_t>(before));
    CHECK(after.silentBlocks == 0);
    checkNear("edit takes effect within the ring lookahead",
              after.samples[20000], 0.25, 1e-6);
    std::printf("PASS daw: clip edit while running is glitch-free\n");
}

// ------------------------------------------------------------- RENDER SONG

void testRenderSong() {
    const std::filesystem::path root = testRoot() / "render-song";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    CHECK(!ec);
    const std::filesystem::path takeL = root / "left.wav";
    const std::filesystem::path takeR = root / "right.wav";
    CHECK(writeConstant(takeL, 4 * static_cast<int>(kFramesPerBeat), 0.25f));
    CHECK(writeConstant(takeR, 4 * static_cast<int>(kFramesPerBeat), 0.5f));

    Engine engine;
    engine.setTransport(true, 0, 960, 12000);
    configureStrip(engine, 0, 1.0f, 0.5f, 0.0f);  // hard left, fader 0.5
    configureStrip(engine, 1, 1.0f, 1.0f, 1.0f);  // hard right
    SongClip left;
    left.take = takeL.string();
    left.startBeats = 0.0;
    left.lengthBeats = 4.0;
    SongClip right;
    right.take = takeR.string();
    right.startBeats = 0.0;
    right.lengthBeats = 4.0;
    std::string error;
    CHECK(engine.setSongClips(0, {left}, &error));
    CHECK(engine.setSongClips(1, {right}, &error));

    // Track 0's rack must be part of the render (external gain 0.5).
    GainEffect half(0.5f);
    TrackFxSlot slot;
    slot.id = "test.gain";
    slot.wet = 1.0f;
    engine.setTrackFxSlot(0, 0, slot);
    engine.setTrackFxSlotEffect(0, 0, &half);

    // A metronome is not mix content: OnPlay must not leak into the file.
    engine.setClickMode(ClickMode::OnPlay);
    const std::filesystem::path master = root / "song.wav";
    RenderSongResult result;
    CHECK(engine.renderSong(master.string(), 1, result, &error));
    CHECK(error.empty());
    CHECK(result.frames == 96000);  // one 4/4 bar at 120 bpm

    std::vector<float> leftOut;
    std::vector<float> rightOut;
    CHECK(readWav(master, leftOut, rightOut));
    CHECK(leftOut.size() == 96000);
    CHECK(rightOut.size() == 96000);
    checkNear("RENDER SONG left clip, fader and rack",
              leftOut[1000], 0.25 * 0.5 * 0.5, 1e-6);
    checkNear("RENDER SONG right clip", rightOut[1000], 0.5, 1e-6);
    // Fade-in at the start (5 ms), and no click anywhere in the steady body.
    checkNear("RENDER SONG auto fade-in start", leftOut[0], 0.0, 1e-6);
    CHECK(maxStep(leftOut, 1000, 90000) < 0.001f);
    CHECK(maxStep(rightOut, 1000, 90000) < 0.001f);
    std::printf("PASS daw: RENDER SONG levels, rack, pan, fader, click off\n");

    // Mute is honoured; the file length is the requested bar count.
    Channel muted = engine.channel(1);
    muted.mute = true;
    engine.setChannel(1, muted);
    RenderSongResult second;
    CHECK(engine.renderSong((root / "song-muted.wav").string(), 1, second,
                            &error));
    CHECK(readWav(root / "song-muted.wav", leftOut, rightOut));
    checkNear("RENDER SONG mute", rightOut[1000], 0.0, 1e-9);
    std::printf("PASS daw: RENDER SONG mute + length\n");
}

// ------------------------------------------------------------- CPU probe

double measureSongBlocks(Engine& engine, int blockFrames, int blocks) {
    std::vector<float> outL(static_cast<std::size_t>(blockFrames));
    std::vector<float> outR(static_cast<std::size_t>(blockFrames));
    const float* inputs[kMaxInputs] = {};
    for (int block = 0; block < 200; ++block) {
        engine.setTransport(true, static_cast<std::uint32_t>(block * 64), 960,
                            12000);
        engine.process(inputs, 0, blockFrames, outL.data(), outR.data());
    }
    const auto start = std::chrono::steady_clock::now();
    for (int block = 0; block < blocks; ++block) {
        engine.setTransport(true, static_cast<std::uint32_t>(block * 64), 960,
                            12000);
        engine.process(inputs, 0, blockFrames, outL.data(), outR.data());
    }
    const auto stop = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(stop - start).count() * 1e6 / blocks;
}

/// Audio-thread cost of the recorder tap chain for `tracks` live strips at the
/// selected source. Monitor is Off, so only the tap chain (and the skipped
/// strip fast path) is measured; POST/POSTFX include the mirrored EQ and, for
/// POSTFX, a built-in delay in FX1.
double measureTap(RecSource source, int tracks, int blocks) {
    constexpr int kProbeFrames = 64;
    Engine engine;
    const float* pointers[kMaxTracks] = {};
    for (int track = 0; track < tracks; ++track) {
        Channel channel;
        channel.gain = 1.0f;
        channel.vol = 0.5f;
        channel.monitor = MonitorMode::Off;
        channel.recSource = source;
        engine.setChannel(track, channel);
        engine.setInputSource(track, track % kMaxDeviceInputs);
        EqBand shelf;
        shelf.type = 2;
        shelf.gainDb = 12.0f;
        shelf.freqHz = 100.0f;
        shelf.q = 0.707f;
        engine.setTrackEqBand(track, 0, shelf);
        if (source == RecSource::PostFx) {
            TrackFxSlot slot;
            slot.id = "builtin.delay";
            slot.wet = 1.0f;
            engine.setTrackFxSlot(track, 0, slot);
        }
    }
    std::vector<std::vector<float>> buffers(
        static_cast<std::size_t>(tracks),
        std::vector<float>(kProbeFrames, 0.25f));
    for (int track = 0; track < tracks; ++track) {
        pointers[track] = buffers[static_cast<std::size_t>(track)].data();
    }
    std::vector<float> outL(kProbeFrames);
    std::vector<float> outR(kProbeFrames);
    for (int block = 0; block < 100; ++block) {
        engine.process(pointers, tracks, kProbeFrames, outL.data(), outR.data());
    }
    const auto start = std::chrono::steady_clock::now();
    for (int block = 0; block < blocks; ++block) {
        engine.process(pointers, tracks, kProbeFrames, outL.data(), outR.data());
    }
    const auto stop = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(stop - start).count() * 1e6 / blocks;
}

void testTapCpu() {
    constexpr double kBudgetUs = 64.0 / 48000.0 * 1e6;
    constexpr int kBlocks = 2000;
    const double pre = measureTap(RecSource::Pre, kMaxTracks, kBlocks);
    const double post = measureTap(RecSource::Post, kMaxTracks, kBlocks);
    const double postFx = measureTap(RecSource::PostFx, kMaxTracks, kBlocks);
    std::printf(
        "INFO daw cpu: 64 live taps PRE %6.2f us/block, POST %6.2f us, POSTFX "
        "with FX1 delay %6.2f us (budget %.0f us)\n",
        pre, post, postFx, kBudgetUs);
    CHECK(pre > 0.0 && post > 0.0 && postFx > post);
    std::printf("PASS daw: REC SOURCE tap CPU probe\n");
}

void testCpuProbe() {
    const std::filesystem::path root = testRoot() / "cpu";
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    const std::filesystem::path take = root / "voice.wav";
    CHECK(writeConstant(take, 2 * static_cast<int>(kFramesPerBeat), 0.25f));

    // Every block crosses a clip boundary: one 64-frame clip per beat, no loop.
    const auto build = [&](Engine& engine, int tracks, bool boundaryEveryBlock) {
        engine.setTransport(true, 0, 960, 12000);
        engine.setSongMode(true);
        for (int track = 0; track < tracks; ++track) {
            configureStrip(engine, track);
            std::vector<SongClip> clips;
            if (boundaryEveryBlock) {
                for (int clip = 0; clip < 600; ++clip) {
                    SongClip item;
                    item.take = take.string();
                    item.startBeats = static_cast<double>(clip) * 64.0 /
                                      kFramesPerBeat;
                    item.lengthBeats = 64.0 / kFramesPerBeat;
                    clips.push_back(std::move(item));
                }
            } else {
                SongClip item;
                item.take = take.string();
                item.startBeats = 0.0;
                item.lengthBeats = 2.0;
                clips.push_back(std::move(item));
            }
            std::string error;
            if (!engine.setSongClips(track, clips, &error)) {
                std::printf("FAIL cpu lane %d: %s\n", track, error.c_str());
                ++gFailures;
            }
        }
    };

    // 48 kHz / 64 frames = 1333 us of wall time per block; this measures the
    // audio-thread scheduler/copy cost only (the reader threads and disk are a
    // separate, non-real-time budget). Numbers are host-specific: the studio Pi
    // must re-measure before any limit is claimed.
    constexpr int kBlock = 64;
    constexpr double kBudgetUs = 64.0 / 48000.0 * 1e6;
    struct Row {
        int tracks;
        bool boundaries;
        double us;
    };
    std::vector<Row> rows;
    for (const int tracks : {1, 8, kMaxTracks}) {
        Engine engine;
        build(engine, tracks, false);
        rows.push_back(
            {tracks, false, measureSongBlocks(engine, kBlock, 2000)});
    }
    {
        Engine engine;
        build(engine, kMaxTracks, true);
        rows.push_back(
            {kMaxTracks, true, measureSongBlocks(engine, kBlock, 1000)});
    }
    for (const Row& row : rows) {
        std::printf(
            "INFO daw cpu: %2d clip tracks%s: %8.2f us/block (%.2f%% of %.0f "
            "us), %.2f ns/frame\n",
            row.tracks, row.boundaries ? " (boundary every block)" : "",
            row.us, 100.0 * row.us / kBudgetUs, kBudgetUs, row.us * 1000.0 / 64.0);
        CHECK(row.us > 0.0);
    }
    std::printf("PASS daw: clip scheduler CPU probe\n");
}

}  // namespace

int main() {
    testRecSourceTaps();
    testRenderRegionToTake();
    testSongScheduler();
    testRenderSong();
    testCpuProbe();
    testTapCpu();
    std::error_code ec;
    std::filesystem::remove_all(testRoot(), ec);
    if (gFailures == 0) {
        std::printf("PASS daw song engine\n");
        return 0;
    }
    std::printf("FAILURES: %d\n", gFailures);
    return 1;
}
