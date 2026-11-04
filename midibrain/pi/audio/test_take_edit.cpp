// Assert-style acceptance tests for non-destructive region edits in the take
// voice (no framework).
// Build: clang++ -std=c++20 -Wall -Wextra -Werror -I. test_take_edit.cpp
//        engine.cpp take_player.cpp recorder.cpp peaks.cpp click.cpp
//        -o /tmp/t_edit -pthread
//
// Covers the beat-mapped region window, per-region gain, linear fade ramps in
// beats, length 0 = to the end of the take, the normalize peak pass, the clamp
// and skip reasons, refusal of a non-48 kHz take, looping the edited stream
// instead of the raw file, the unchanged newest-wins/monitor composition, and
// that a missing `.peaks` sidecar does not affect rendering.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

#include "engine.hpp"
#include "peaks.hpp"
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
               double eps = 1e-5) {
    if (std::fabs(actual - expected) <= eps) return;
    std::printf("FAIL %s: actual %.9g expected %.9g\n", what, actual, expected);
    ++gFailures;
}

// 120 bpm at 48 kHz: one session beat is 24000 take frames.
constexpr double kFramesPerBeat = 24000.0;

std::filesystem::path testRoot() {
    return std::filesystem::temp_directory_path() /
           ("mnky_take_edit_" +
            std::to_string(static_cast<long>(::getpid())));
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

std::vector<float> constant(int frames, float value) {
    return std::vector<float>(static_cast<std::size_t>(frames), value);
}

/// A take whose sample value is its own frame index / 48000, so a rendered
/// sample names the exact source frame it came from.
std::vector<float> ramp(int frames) {
    std::vector<float> samples(static_cast<std::size_t>(frames));
    for (int i = 0; i < frames; ++i) {
        samples[static_cast<std::size_t>(i)] =
            static_cast<float>(i) / 48000.0f;
    }
    return samples;
}

/// Captures the process's stderr into a temporary file so a test can assert the
/// logged clamp/skip reason of a region edit.
class StderrCapture {
public:
    StderrCapture() : mSaved(::dup(STDERR_FILENO)), mFile(std::tmpfile()) {
        if (mFile != nullptr) ::dup2(::fileno(mFile), STDERR_FILENO);
    }
    ~StderrCapture() { (void)finish(); }
    StderrCapture(const StderrCapture&) = delete;
    StderrCapture& operator=(const StderrCapture&) = delete;

    std::string finish() {
        if (mSaved < 0) return {};
        std::fflush(stderr);
        ::dup2(mSaved, STDERR_FILENO);
        ::close(mSaved);
        mSaved = -1;
        if (mFile == nullptr) return {};
        std::rewind(mFile);
        std::string text;
        char buffer[512];
        std::size_t got = 0;
        while ((got = std::fread(buffer, 1, sizeof buffer, mFile)) > 0) {
            text.append(buffer, got);
        }
        std::fclose(mFile);
        mFile = nullptr;
        return text;
    }

private:
    int mSaved = -1;
    std::FILE* mFile = nullptr;
};

/// Renders `frames` of `track` through the take voice (no engine pan/master, so
/// samples are the take voice's own values). `played` reports whether any block
/// called itself playing.
///
/// Each block is followed by a short sleep, i.e. the loop consumes about ten
/// times faster than real time instead of as fast as the CPU allows. A full-speed
/// loop can outrun the ring and legitimately produce counted underruns (silence);
/// that is playback policy, not what these level/timing checks are about.
std::vector<float> renderTrack(TakePlayer& player, int track, int frames,
                               bool* played = nullptr) {
    constexpr int kBlock = 256;
    std::vector<std::vector<float>> buffers(
        static_cast<std::size_t>(TakePlayer::kMaxTracks),
        std::vector<float>(kBlock));
    std::vector<float*> pointers(
        static_cast<std::size_t>(TakePlayer::kMaxTracks));
    for (int i = 0; i < TakePlayer::kMaxTracks; ++i) {
        pointers[static_cast<std::size_t>(i)] =
            buffers[static_cast<std::size_t>(i)].data();
    }
    bool active[TakePlayer::kMaxTracks] = {};
    bool playing[TakePlayer::kMaxTracks] = {};
    std::vector<float> out;
    out.reserve(static_cast<std::size_t>(frames));
    bool any = false;
    for (int done = 0; done < frames; done += kBlock) {
        const int n = std::min(kBlock, frames - done);
        player.render(true, n, pointers.data(), active, playing);
        any = any || playing[track];
        out.insert(out.end(), buffers[static_cast<std::size_t>(track)].begin(),
                   buffers[static_cast<std::size_t>(track)].begin() + n);
        std::this_thread::sleep_for(std::chrono::microseconds(500));
    }
    if (played != nullptr) *played = any;
    return out;
}

bool allZero(const std::vector<float>& samples) {
    for (float value : samples) {
        if (value != 0.0f) return false;
    }
    return true;
}

// ------------------------------------------------------- region window / gain

void testRegionWindowAndGain() {
    const std::filesystem::path root = testRoot();
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    CHECK(!ec);

    const std::string take = (root / "ramp.wav").string();
    CHECK(writeWav(take, 48000, 1, ramp(48000)));

    TakePlayer player;
    TakeRegion region;
    region.take = take;
    region.offsetBeats = 0.5;  // frame 12000
    region.lengthBeats = 0.25;  // 6000 frames
    std::string error;
    CHECK(player.setRegions(0, {region}, kFramesPerBeat, 0, &error));
    CHECK(error.empty());
    CHECK(player.regionCount(0) == 1);
    CHECK(player.editedFrames(0) == 6000);

    bool played = false;
    const std::vector<float> out = renderTrack(player, 0, 6000, &played);
    CHECK(played);
    CHECK(out.size() == 6000);
    checkNear("region starts at the offset beat", out[0], 12000.0 / 48000.0);
    checkNear("region frame 3000", out[3000], 15000.0 / 48000.0);
    checkNear("region last frame", out[5999], 17999.0 / 48000.0);

    // Past the end of the edit the voice is silent and no longer playing.
    bool playedAfter = true;
    const std::vector<float> tail = renderTrack(player, 0, 512, &playedAfter);
    CHECK(!playedAfter);
    CHECK(allZero(tail));

    // +6.0206 dB is exactly 2x, applied to the same window.
    region.offsetBeats = 0.5;
    region.lengthBeats = 0.25;
    region.gainDb = 6.0206f;
    CHECK(player.setRegions(0, {region}, kFramesPerBeat, 0, nullptr));
    const std::vector<float> louder = renderTrack(player, 0, 1024);
    checkNear("gain +6.0206 dB doubles the region", louder[0],
              2.0 * 12000.0 / 48000.0);

    std::filesystem::remove_all(root, ec);
    std::printf("PASS take edit: beat-mapped region window and gain\n");
}

// ------------------------------------------------------------- fade ramps

void testFadeRamps() {
    const std::filesystem::path root = testRoot();
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    CHECK(!ec);

    const std::string take = (root / "flat.wav").string();
    CHECK(writeWav(take, 48000, 1, constant(48000, 0.5f)));

    TakePlayer player;
    TakeRegion region;
    region.take = take;
    region.offsetBeats = 0.0;
    region.lengthBeats = 0.5;    // 12000 frames
    region.fadeInBeats = 0.125;  // 3000 frames
    region.fadeOutBeats = 0.125; // 3000 frames
    CHECK(player.setRegions(0, {region}, kFramesPerBeat, 0, nullptr));
    CHECK(player.editedFrames(0) == 12000);

    const std::vector<float> out = renderTrack(player, 0, 12000);
    checkNear("fade-in starts at silence", out[0], 0.0);
    checkNear("fade-in midpoint is half gain", out[1500], 0.25);
    checkNear("fade-in complete after its beats", out[3000], 0.5);
    checkNear("fade-in stays at unity in the body", out[6000], 0.5);
    checkNear("fade-out midpoint", out[10500], 0.5 * 1499.0 / 3000.0);
    checkNear("fade-out ends at silence", out[11999], 0.0);

    // Both fades reach their region edge exactly, so a longer fade covers the
    // whole region and still never exceeds unity in between.
    region.fadeInBeats = 0.5;   // 12000 frames == the region
    region.fadeOutBeats = 0.5;
    CHECK(player.setRegions(0, {region}, kFramesPerBeat, 0, nullptr));
    const std::vector<float> whole = renderTrack(player, 0, 12000);
    checkNear("region covering fade-in starts at silence", whole[0], 0.0);
    // Both ramps cover the whole region and multiply, so the midpoint is the
    // fade-in value times the fade-out value of that same frame.
    checkNear("region covering fade-in midpoint", whole[6000],
              0.5 * 0.5 * (5999.0 / 12000.0));

    std::filesystem::remove_all(root, ec);
    std::printf("PASS take edit: linear fade in/out ramps in beats\n");
}

// ------------------------------------------------------ length 0 and normalize

void testToEndAndNormalize() {
    const std::filesystem::path root = testRoot();
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    CHECK(!ec);

    // Two halves with different levels so each region's normalize pass sees
    // only its own frames.
    const std::string take = (root / "levels.wav").string();
    std::vector<float> samples = constant(24000, 0.25f);
    const std::vector<float> second = constant(24000, 0.9f);
    samples.insert(samples.end(), second.begin(), second.end());
    CHECK(writeWav(take, 48000, 1, samples));

    TakePlayer player;
    TakeRegion first;
    first.take = take;
    first.offsetBeats = 0.0;
    first.lengthBeats = 1.0;  // 24000 frames, the quiet half
    first.normalize = true;
    TakeRegion secondRegion;
    secondRegion.take = take;
    secondRegion.offsetBeats = 1.0;  // 24000 frames in
    secondRegion.lengthBeats = 0.0;  // to the end of the take
    secondRegion.gainDb = 6.0206f;
    secondRegion.normalize = true;
    std::string error;
    CHECK(player.setRegions(0, {first, secondRegion}, kFramesPerBeat, 0,
                            &error));
    CHECK(error.empty());
    CHECK(player.regionCount(0) == 2);
    // 24000 frames of the quiet half plus 24000 to the end of the take.
    CHECK(player.editedFrames(0) == 48000);

    const std::vector<float> out = renderTrack(player, 0, 48000);
    const double target = std::pow(10.0, kNormalizePeakDbfs / 20.0);
    CHECK(player.underruns() == 0);
    checkNear("normalize puts the region peak at -1 dBFS", out[0], target);
    checkNear("normalize covers the whole first region", out[23999], target);
    checkNear("length 0 runs to the end of the take at the region gain",
              out[24000], target * 2.0);
    checkNear("second region ends at the take end", out[47999], target * 2.0);

    std::filesystem::remove_all(root, ec);
    std::printf("PASS take edit: length 0 = to end, per-region normalize\n");
}

// ------------------------------------------------------------ clamp reasons

void testClampReasons() {
    const std::filesystem::path root = testRoot();
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    CHECK(!ec);

    const std::string take = (root / "ramp.wav").string();
    CHECK(writeWav(take, 48000, 1, ramp(48000)));

    TakePlayer player;
    {   // A region that starts past the end of its take is skipped, logged, and
        // the rest of the edit still plays.
        TakeRegion outside;
        outside.take = take;
        outside.offsetBeats = 10.0;  // 240000 frames
        outside.lengthBeats = 0.5;
        TakeRegion inside;
        inside.take = take;
        inside.offsetBeats = 0.0;
        inside.lengthBeats = 0.0;
        StderrCapture capture;
        std::string error;
        CHECK(player.setRegions(0, {outside, inside}, kFramesPerBeat, 0,
                                &error));
        CHECK(error.empty());
        const std::string log = capture.finish();
        CHECK(log.find("past the end") != std::string::npos);
        CHECK(player.editedFrames(0) == 48000);
        const std::vector<float> out = renderTrack(player, 0, 1024);
        checkNear("the playable region still renders", out[0], 0.0);
        checkNear("the playable region renders its second frame", out[1],
                  1.0 / 48000.0);
    }
    {   // A length past the take end clamps to it instead of inventing audio.
        TakeRegion longRegion;
        longRegion.take = take;
        longRegion.offsetBeats = 0.0;
        longRegion.lengthBeats = 10.0;
        StderrCapture capture;
        CHECK(player.setRegions(0, {longRegion}, kFramesPerBeat, 0, nullptr));
        const std::string log = capture.finish();
        CHECK(log.find("clamped to the take end") != std::string::npos);
        CHECK(player.editedFrames(0) == 48000);
    }
    {   // A fade longer than the region clamps to the region.
        TakeRegion faded;
        faded.take = take;
        faded.offsetBeats = 0.0;
        faded.lengthBeats = 0.25;  // 6000 frames
        faded.fadeInBeats = 1.0;   // 24000 frames
        StderrCapture capture;
        CHECK(player.setRegions(0, {faded}, kFramesPerBeat, 0, nullptr));
        const std::string log = capture.finish();
        CHECK(log.find("fade-in is longer than the region") !=
              std::string::npos);
        CHECK(player.editedFrames(0) == 6000);
        const std::vector<float> out = renderTrack(player, 0, 6000);
        checkNear("clamped fade-in still starts at silence", out[0], 0.0);
    }
    {   // An edit that clamps away entirely is an explicit, logged silence.
        TakeRegion outside;
        outside.take = take;
        outside.offsetBeats = 100.0;
        StderrCapture capture;
        CHECK(player.setRegions(0, {outside}, kFramesPerBeat, 0, nullptr));
        const std::string log = capture.finish();
        CHECK(log.find("resolves to no frames") != std::string::npos);
        CHECK(player.regionCount(0) == 1);
        CHECK(player.editedFrames(0) == 0);
        bool played = true;
        const std::vector<float> out = renderTrack(player, 0, 512, &played);
        CHECK(!played);
        CHECK(allZero(out));
    }
    {   // An unreadable or non-48 kHz take refuses the whole list and leaves the
        // previous edit untouched.
        const std::string badRate = (root / "44k.wav").string();
        CHECK(writeWav(badRate, 44100, 1, constant(48000, 0.5f)));
        TakeRegion bad;
        bad.take = badRate;
        const std::uint64_t before = player.editedFrames(0);
        std::string error;
        StderrCapture capture;
        CHECK(!player.setRegions(0, {bad}, kFramesPerBeat, 0, &error));
        CHECK(!error.empty());
        CHECK(error.find("48000") != std::string::npos);
        capture.finish();
        CHECK(player.editedFrames(0) == before);
    }

    std::filesystem::remove_all(root, ec);
    std::printf("PASS take edit: clamp and skip reasons\n");
}

// --------------------------------------------------------- loop the edit

void testLoopWrapsTheEditedStream() {
    const std::filesystem::path root = testRoot();
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    CHECK(!ec);

    const std::string take = (root / "ramp.wav").string();
    CHECK(writeWav(take, 48000, 1, ramp(48000)));

    TakePlayer player;
    TakeRegion region;
    region.take = take;
    region.offsetBeats = 0.5;   // frame 12000
    region.lengthBeats = 0.25;  // 6000 frames
    CHECK(player.setRegions(0, {region}, kFramesPerBeat, 0, nullptr));
    CHECK(player.editedFrames(0) == 6000);

    // end <= start is the whole-material rule: the edited stream is 6000 frames
    // long, so the wrap must land there and not at the raw take's end.
    TakeLoop loop;
    loop.enabled = true;
    CHECK(player.setLoop(0, loop, kFramesPerBeat, nullptr));

    const std::vector<float> out = renderTrack(player, 0, 13000);
    checkNear("loop plays the edit", out[500], 12500.0 / 48000.0);
    checkNear("wrap repeats the edited region at its own end", out[6500],
              out[500]);
    checkNear("the seam starts at silence", out[6000], 0.0);
    const std::uint64_t fade = TakePlayer::loopSeamFade(6000);
    checkNear("the seam is declicked", out[6000 + fade],
              1.0 * (12000.0 + static_cast<double>(fade)) / 48000.0);

    // A beat-range loop maps over the edited stream, not the source take.
    TakeLoop range;
    range.enabled = true;
    range.startBeats = 0.0;
    range.endBeats = 0.125;  // 3000 edited frames
    CHECK(player.setLoop(0, range, kFramesPerBeat, nullptr));
    const std::vector<float> ranged = renderTrack(player, 0, 7000);
    checkNear("beat-range loop starts at the edited origin", ranged[128],
              12128.0 / 48000.0);
    checkNear("beat-range loop wraps at the edited beat", ranged[3128],
              ranged[128]);
    checkNear("beat-range loop seam is silent", ranged[3000], 0.0);

    std::filesystem::remove_all(root, ec);
    std::printf("PASS take edit: the loop wraps the edited region\n");
}

// ------------------------------------------- composition stays unchanged

void testStackAndMonitorComposition() {
    const std::filesystem::path root = testRoot();
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    CHECK(!ec);

    const std::string take = (root / "flat.wav").string();
    CHECK(writeWav(take, 48000, 1, constant(48000, 0.5f)));
    const std::string other = (root / "other.wav").string();
    CHECK(writeWav(other, 48000, 1, constant(48000, 0.75f)));

    // Track 1 keeps its take stack while track 0 carries an edit: selecting an
    // edit is per track and does not disturb the newest-wins stack elsewhere.
    TakePlayer player;
    CHECK(player.setTakes(1, {take, other}, 0, nullptr));
    CHECK(player.takeCount(1) == 2);
    CHECK(player.takePath(1, 1) == other);
    TakeRegion region;
    region.take = take;
    region.offsetBeats = 0.5;
    region.lengthBeats = 0.25;
    CHECK(player.setRegions(0, {region}, kFramesPerBeat, 0, nullptr));
    CHECK(player.takeCount(1) == 2);
    CHECK(player.takePath(1, 1) == other);
    const std::vector<float> stack = renderTrack(player, 1, 512);
    checkNear("the other track keeps playing its newest take", stack[0], 0.75);
    const std::vector<float> edited = renderTrack(player, 0, 512);
    checkNear("the edited track plays its own region", edited[0], 0.5);

    // clearRegions() returns the track to its (unedited) take stack.
    CHECK(player.setTake(0, take, 0, nullptr));
    CHECK(player.regionCount(0) == 1);
    player.clearRegions(0);
    CHECK(player.regionCount(0) == 0);
    CHECK(player.editedFrames(0) == 0);
    const std::vector<float> restored = renderTrack(player, 0, 512);
    checkNear("clearing the edit resumes the take stack", restored[0], 0.5);

    // Engine composition: an edit plays through the strip; a disarmed Auto
    // strip hears only the take while it plays, and live input returns when the
    // transport stops. Monitor On layers live input as before.
    Engine engine;
    Channel strip;
    strip.gain = 1.0f;
    strip.vol = 1.0f;
    strip.pan = 0.0f;  // hard left makes the take level directly readable
    engine.setChannel(0, strip);
    engine.setTransport(true, 0, 24, 12000);
    CHECK(engine.setActiveTake(0, take, nullptr));  // the stack the edit replaces
    CHECK(engine.setTakeRegions(0, {region}, nullptr));
    CHECK(engine.takeRegions(0).size() == 1);
    CHECK(engine.takeRegions(0)[0] == region);

    std::vector<float> live(static_cast<std::size_t>(kBlockSize), 0.25f);
    float outL[kBlockSize] = {};
    float outR[kBlockSize] = {};
    const float* inputs[kMaxInputs] = {};
    inputs[0] = live.data();
    engine.process(inputs, 1, kBlockSize, outL, outR);
    checkNear("edit only on a disarmed auto strip", outL[0], 0.5);
    CHECK(engine.tap(0) != nullptr);
    if (engine.tap(0) != nullptr) {
        checkNear("tap stays live while the edit plays", engine.tap(0)[0], 0.25);
    }

    engine.setTransport(false, 0, 24, 12000);
    engine.process(inputs, 1, kBlockSize, outL, outR);
    checkNear("stopped transport returns live input", outL[0], 0.25);

    engine.setTransport(true, 0, 24, 12000);
    strip.monitor = MonitorMode::On;
    engine.setChannel(0, strip);
    engine.process(inputs, 1, kBlockSize, outL, outR);
    checkNear("monitor On layers the live input", outL[0], 0.75);

    // A missing `.peaks` sidecar costs the UI its overview, not playback.
    CHECK(!std::filesystem::exists(peaksPath(take)));
    strip.monitor = MonitorMode::Auto;
    engine.setChannel(0, strip);
    engine.process(inputs, 1, kBlockSize, outL, outR);
    checkNear("edit renders without a peak sidecar", outL[0], 0.5);

    // Clearing the edit is what restores the stack; the engine keeps the list.
    engine.clearTakeRegions(0);
    CHECK(engine.takeRegions(0).empty());
    engine.process(inputs, 1, kBlockSize, outL, outR);
    checkNear("cleared engine edit plays the take from its start", outL[0],
              0.5);

    // A 44.1 kHz engine refuses an edit with the take-playback reason.
    Engine resampled;
    resampled.setSampleRate(44100.0);
    resampled.setTransport(true, 0, 24, 12000);
    std::string error;
    CHECK(!resampled.setTakeRegions(0, {region}, &error));
    CHECK(!error.empty());
    CHECK(error.find("48000") != std::string::npos);
    CHECK(resampled.takeRegions(0).empty());

    std::filesystem::remove_all(root, ec);
    std::printf("PASS take edit: stack and monitor composition unchanged\n");
}

// ---------------------------------------------------------------- re-apply

void testReapplyAfterTempoChange() {
    const std::filesystem::path root = testRoot();
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    CHECK(!ec);

    const std::string take = (root / "ramp.wav").string();
    CHECK(writeWav(take, 48000, 1, ramp(48000)));

    // The same beats at half the tempo cover twice the frames: the mapping is
    // resolved at apply time from the tempo the engine passes.
    TakeRegion region;
    region.take = take;
    region.offsetBeats = 0.5;
    region.lengthBeats = 0.25;

    TakePlayer slow;
    CHECK(slow.setRegions(0, {region}, kFramesPerBeat / 2.0, 0, nullptr));
    CHECK(slow.editedFrames(0) == 3000);
    const std::vector<float> slowOut = renderTrack(slow, 0, 3000);
    checkNear("half tempo starts at frame 6000", slowOut[0], 6000.0 / 48000.0);

    std::filesystem::remove_all(root, ec);
    std::printf("PASS take edit: beat mapping follows the applied tempo\n");
}

}  // namespace

int main() {
    testRegionWindowAndGain();
    testFadeRamps();
    testToEndAndNormalize();
    testClampReasons();
    testLoopWrapsTheEditedStream();
    testStackAndMonitorComposition();
    testReapplyAfterTempoChange();
    if (gFailures != 0) {
        std::printf("%d take-edit check(s) failed\n", gFailures);
        return 1;
    }
    std::printf("all take-edit checks passed\n");
    return 0;
}
