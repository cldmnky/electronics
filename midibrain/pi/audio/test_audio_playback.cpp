// Assert-style acceptance tests for engine take playback (no framework).
// Build: clang++ -std=c++20 -Wall -Wextra -Werror -I. test_audio_playback.cpp
//        engine.cpp take_player.cpp recorder.cpp click.cpp -o /tmp/t_play -pthread
//
// Covers per-track selection, the newest-wins overlap rule with synthetic
// float32 WAVs, the disarmed/armed input-path rule, the 48 kHz refusal, the
// monitor off/auto/on/input-only policy (including the suppressed take that
// keeps tracking position), and per-track take looping (beat-mapped range
// loops, the end<=start whole-take rule, the seam-declick timing, loop+overlap
// and loop+monitor behaviour).

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <unistd.h>
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

void checkNear(const char* what, float actual, float expected, float eps = 1e-6f) {
    if (std::fabs(actual - expected) <= eps) return;
    std::printf("FAIL %s: actual %.9g expected %.9g\n", what, actual, expected);
    ++gFailures;
}

std::filesystem::path testRoot() {
    return std::filesystem::temp_directory_path() /
           ("mnky_audio_playback_" + std::to_string(static_cast<long>(::getpid())));
}

bool writeWav(const std::filesystem::path& path, uint32_t sampleRate, int channels,
              const std::vector<float>& interleaved) {
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

void processLive(Engine& engine, const float* live, float* outL, float* outR) {
    const float* inputs[kMaxInputs] = {};
    int channels = 0;
    if (live != nullptr) {
        inputs[0] = live;
        channels = 1;
    }
    engine.process(inputs, channels, kBlockSize, outL, outR);
}

// Renders `frames` (a multiple of kBlockSize) of Main L into `out`.
void renderMain(Engine& engine, const float* live, int frames,
                std::vector<float>& out) {
    std::vector<float> left(static_cast<std::size_t>(kBlockSize));
    std::vector<float> right(static_cast<std::size_t>(kBlockSize));
    out.clear();
    out.reserve(static_cast<std::size_t>(frames));
    for (int done = 0; done < frames; done += kBlockSize) {
        const float* inputs[kMaxInputs] = {};
        int channels = 0;
        if (live != nullptr) {
            inputs[0] = live;
            channels = 1;
        }
        engine.process(inputs, channels, kBlockSize, left.data(), right.data());
        out.insert(out.end(), left.begin(), left.end());
    }
}

// Ramp take: every frame has a distinct value, so a wrong wrap position or a
// wrong loop length shows up immediately.
std::vector<float> ramp(int frames, float step) {
    std::vector<float> data(static_cast<std::size_t>(frames));
    for (int i = 0; i < frames; ++i) {
        data[static_cast<std::size_t>(i)] = step * static_cast<float>(i);
    }
    return data;
}

// The seam ramp TakePlayer documents: loopSeamFade() frames fade in from zero
// at the loop start and out to zero at the loop end.
float seamGain(std::uint64_t offset, std::uint64_t loopFrames) {
    const std::uint64_t fade = TakePlayer::loopSeamFade(loopFrames);
    if (offset < fade) {
        return static_cast<float>(offset) / static_cast<float>(fade);
    }
    if (offset + fade >= loopFrames) {
        return static_cast<float>(loopFrames - 1 - offset) /
               static_cast<float>(fade);
    }
    return 1.0f;
}

// Full-scale strip 0 on one take path: gain/vol unity, hard left, so Main L is
// the take sample itself. `bpmX100` sets the beat -> frame mapping the loop
// points are expressed in.
void selectLoopTake(Engine& engine, const std::string& take, int bpmX100) {
    Channel strip;
    strip.gain = 1.0f;
    strip.vol = 1.0f;
    strip.pan = 0.0f;  // hard left makes the take level directly readable
    engine.setChannel(0, strip);
    engine.setTransport(true, 0, 24, bpmX100);
    CHECK(engine.setActiveTake(0, take, nullptr));
}

// ------------------------------------------------------- selection / disarming

void testSelectionAndArm() {
    const std::filesystem::path root = testRoot();
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    CHECK(!ec);

    const std::string take = (root / "take.wav").string();
    CHECK(writeWav(take, 48000, 1, constant(kBlockSize, 0.5f)));

    Engine engine;
    Channel strip;
    strip.gain = 1.0f;
    strip.vol = 1.0f;
    strip.pan = 0.0f;  // hard left makes the take level directly readable
    engine.setChannel(0, strip);
    engine.setTransport(true, 0, 24, 12000);

    std::string error;
    CHECK(engine.setActiveTake(0, take, &error));
    CHECK(error.empty());
    CHECK(engine.activeTakeCount(0) == 1);
    CHECK(engine.activeTake(0) == take);

    std::vector<float> live(static_cast<std::size_t>(kBlockSize), 0.9f);
    float outL[kBlockSize] = {};
    float outR[kBlockSize] = {};
    processLive(engine, live.data(), outL, outR);
    checkNear("disarmed strip plays the take, not the live input", outL[0], 0.5f);
    checkNear("disarmed strip plays the take (R)", outR[0], 0.0f);
    const float* tap = engine.tap(0);
    CHECK(tap != nullptr);
    if (tap != nullptr) {
        checkNear("tap stays on the live input while a take plays", tap[0], 0.9f);
    }

    // An armed strip records the live input; the take is not its source.
    strip.arm = true;
    engine.setChannel(0, strip);
    std::vector<float> armed(static_cast<std::size_t>(kBlockSize), 0.25f);
    processLive(engine, armed.data(), outL, outR);
    checkNear("armed strip ignores the take", outL[0], 0.25f);

    // Clearing restores live monitoring for a disarmed strip.
    engine.clearActiveTake(0);
    CHECK(engine.activeTakeCount(0) == 0);
    strip.arm = false;
    engine.setChannel(0, strip);
    std::vector<float> monitored(static_cast<std::size_t>(kBlockSize), 0.125f);
    processLive(engine, monitored.data(), outL, outR);
    checkNear("cleared take returns to live monitoring", outL[0], 0.125f);

    std::filesystem::remove_all(root, ec);
    std::printf("PASS take playback: per-track selection and arm rule\n");
}

// ---------------------------------------------------------- newest-wins layers

void testNewestWinsOverlap() {
    const std::filesystem::path root = testRoot();
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    CHECK(!ec);

    const std::string older = (root / "older.wav").string();
    const std::string newer = (root / "newer.wav").string();
    CHECK(writeWav(older, 48000, 1, constant(2 * kBlockSize, 0.25f)));
    CHECK(writeWav(newer, 48000, 1, constant(kBlockSize, 0.75f)));

    Engine engine;
    Channel strip;
    strip.gain = 1.0f;
    strip.vol = 1.0f;
    strip.pan = 0.0f;
    engine.setChannel(0, strip);
    engine.setTransport(true, 0, 24, 12000);

    std::string error;
    CHECK(engine.setActiveTakes(0, {older, newer}, &error));
    CHECK(error.empty());
    CHECK(engine.activeTakeCount(0) == 2);
    CHECK(engine.activeTake(0) == newer);

    float outL[kBlockSize] = {};
    float outR[kBlockSize] = {};
    processLive(engine, nullptr, outL, outR);
    checkNear("newest take wins the overlap", outL[0], 0.75f);

    processLive(engine, nullptr, outL, outR);
    checkNear("older take resumes after the newer file ends", outL[0], 0.25f);

    processLive(engine, nullptr, outL, outR);
    checkNear("both layers are silent past their end", outL[0], 0.0f);

    std::filesystem::remove_all(root, ec);
    std::printf("PASS take playback: newest-wins layered overlap\n");
}

// ------------------------------------------------------------ 48 kHz refusal

void testNon48kRefused() {
    const std::filesystem::path root = testRoot();
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    CHECK(!ec);

    const std::string good = (root / "good.wav").string();
    const std::string bad = (root / "bad-44k.wav").string();
    CHECK(writeWav(good, 48000, 1, constant(kBlockSize, 0.5f)));
    CHECK(writeWav(bad, 44100, 1, constant(kBlockSize, 0.5f)));

    Engine engine;
    engine.setTransport(true, 0, 24, 12000);
    std::string error;
    CHECK(engine.setActiveTake(0, good, &error));
    CHECK(error.empty());

    error.clear();
    CHECK(!engine.setActiveTake(0, bad, &error));
    CHECK(!error.empty());
    CHECK(error.find("48000") != std::string::npos);
    CHECK(engine.activeTake(0) == good);  // refusal leaves the selection intact

    // A track with no selection also refuses the 44.1 kHz file and stays empty.
    error.clear();
    CHECK(!engine.setActiveTake(1, bad, &error));
    CHECK(!error.empty());
    CHECK(engine.activeTakeCount(1) == 0);

    std::filesystem::remove_all(root, ec);
    std::printf("PASS take playback: 48 kHz-only refusal\n");
}

// ------------------------------------------------------------ stereo downmix

void testStereoDownmix() {
    const std::filesystem::path root = testRoot();
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    CHECK(!ec);

    const std::string take = (root / "stereo.wav").string();
    std::vector<float> interleaved;
    for (int i = 0; i < kBlockSize; ++i) {
        interleaved.push_back(0.8f);   // L
        interleaved.push_back(0.2f);   // R
    }
    CHECK(writeWav(take, 48000, 2, interleaved));

    Engine engine;
    Channel strip;
    strip.pan = 0.0f;
    engine.setChannel(0, strip);
    engine.setTransport(true, 0, 24, 12000);
    CHECK(engine.setActiveTake(0, take, nullptr));

    float outL[kBlockSize] = {};
    float outR[kBlockSize] = {};
    processLive(engine, nullptr, outL, outR);
    checkNear("stereo take downmixes 0.5*(L+R)", outL[0], 0.5f);

    std::filesystem::remove_all(root, ec);
    std::printf("PASS take playback: stereo take downmix\n");
}

// ------------------------------------------------------- input monitoring

void testMonitorModes() {
    const std::filesystem::path root = testRoot();
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    CHECK(!ec);

    const std::string longTake = (root / "long.wav").string();
    const std::string shortTake = (root / "short.wav").string();
    CHECK(writeWav(longTake, 48000, 1, constant(4 * kBlockSize, 0.5f)));
    CHECK(writeWav(shortTake, 48000, 1, constant(kBlockSize, 0.5f)));

    auto prepareEngine = [](Engine& engine, const std::string& take) {
        Channel strip;
        strip.gain = 1.0f;
        strip.vol = 1.0f;
        strip.pan = 0.0f;  // hard left
        engine.setChannel(0, strip);
        engine.setTransport(true, 0, 24, 12000);
        CHECK(engine.setActiveTake(0, take, nullptr));
    };

    std::vector<float> live(static_cast<std::size_t>(kBlockSize), 0.9f);
    float outL[kBlockSize] = {};
    float outR[kBlockSize] = {};

    // Auto + disarmed + take playing: take only, no live doubling.
    {
        Engine engine;
        prepareEngine(engine, longTake);
        CHECK(engine.monitorMode(0) == MonitorMode::Auto);
        processLive(engine, live.data(), outL, outR);
        checkNear("auto take-only is not doubled by live input", outL[0], 0.5f);
        const float* tap = engine.tap(0);
        CHECK(tap != nullptr);
        if (tap != nullptr) checkNear("auto tap still sees live input", tap[0], 0.9f);

        // Armed + Auto: the take voice is ignored and live input passes.
        Channel strip = engine.channel(0);
        strip.arm = true;
        engine.setChannel(0, strip);
        processLive(engine, live.data(), outL, outR);
        checkNear("armed auto passes live input", outL[0], 0.9f);
    }

    // Off: live input never reaches Main, but the take voice is audible.
    {
        Engine engine;
        prepareEngine(engine, longTake);
        engine.setMonitorMode(0, MonitorMode::Off);
        processLive(engine, live.data(), outL, outR);
        checkNear("off keeps the take voice", outL[0], 0.5f);
        const float* tap = engine.tap(0);
        CHECK(tap != nullptr);
        if (tap != nullptr) checkNear("off does not change the tap", tap[0], 0.9f);

        engine.clearActiveTake(0);
        processLive(engine, live.data(), outL, outR);
        checkNear("off blocks live input without a take", outL[0], 0.0f);
    }

    // On: live input always passes, layering with an audible take.
    {
        Engine engine;
        prepareEngine(engine, longTake);
        engine.setMonitorMode(0, MonitorMode::On);
        processLive(engine, live.data(), outL, outR);
        checkNear("on layers live input with the take", outL[0], 1.4f);
        CHECK(engine.monitorMode(0) == MonitorMode::On);
    }

    // Auto + transport stopped: the take is not playing, so live input passes.
    {
        Engine engine;
        Channel strip;
        strip.pan = 0.0f;
        engine.setChannel(0, strip);
        engine.setTransport(false, 0, 24, 12000);
        CHECK(engine.setActiveTake(0, longTake, nullptr));
        processLive(engine, live.data(), outL, outR);
        checkNear("stopped auto monitors live input", outL[0], 0.9f);
    }

    // Auto + take ended: live input returns instead of staying silent.
    {
        Engine engine;
        prepareEngine(engine, shortTake);
        processLive(engine, live.data(), outL, outR);
        checkNear("short take plays its block", outL[0], 0.5f);
        processLive(engine, live.data(), outL, outR);
        checkNear("auto returns to live after the take ends", outL[0], 0.9f);
    }

    // Mute wins over every monitor mode while the tap stays live.
    {
        Engine engine;
        prepareEngine(engine, longTake);
        Channel strip = engine.channel(0);
        strip.mute = true;
        strip.monitor = MonitorMode::On;
        engine.setChannel(0, strip);
        processLive(engine, live.data(), outL, outR);
        checkNear("muted track is silent in monitor on", outL[0], 0.0f);
        const float* tap = engine.tap(0);
        CHECK(tap != nullptr);
        if (tap != nullptr) checkNear("muted tap stays live", tap[0], 0.9f);
    }

    // Mode storage and invalid-value fallback.
    {
        Engine engine;
        CHECK(engine.monitorMode(0) == MonitorMode::Auto);
        engine.setMonitorMode(0, MonitorMode::Off);
        CHECK(engine.monitorMode(0) == MonitorMode::Off);
        engine.setMonitorMode(0, MonitorMode::On);
        CHECK(engine.monitorMode(0) == MonitorMode::On);
        engine.setMonitorMode(0, static_cast<MonitorMode>(9));
        CHECK(engine.monitorMode(0) == MonitorMode::Auto);
        CHECK(engine.monitorMode(-1) == MonitorMode::Auto);
    }

    std::filesystem::remove_all(root, ec);
    std::printf("PASS take playback: monitor off/auto/on policy\n");
}

// --------------------------------------------------- monitor-only (InputOnly)

void testMonitorInputOnly() {
    const std::filesystem::path root = testRoot();
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    CHECK(!ec);

    // The constant take is long enough for the transition walk, the ramp take
    // makes a stale or reset playback position show up immediately, and the
    // two overlap takes prove every suppressed layer keeps advancing.
    const std::string longTake = (root / "long.wav").string();
    const std::string rampTake = (root / "ramp.wav").string();
    const std::string olderTake = (root / "older.wav").string();
    const std::string newerTake = (root / "newer.wav").string();
    const std::vector<float> rampMaterial = ramp(8 * kBlockSize, 0.001f);
    CHECK(writeWav(longTake, 48000, 1, constant(8 * kBlockSize, 0.5f)));
    CHECK(writeWav(rampTake, 48000, 1, rampMaterial));
    CHECK(writeWav(olderTake, 48000, 1, constant(2 * kBlockSize, 0.25f)));
    CHECK(writeWav(newerTake, 48000, 1, constant(kBlockSize, 0.75f)));

    const auto prepare = [](Engine& engine, const std::string& take) {
        Channel strip;
        strip.gain = 1.0f;
        strip.vol = 1.0f;
        strip.pan = 0.0f;  // hard left: the strip level is directly readable
        engine.setChannel(0, strip);
        engine.setTransport(true, 0, 24, 12000);
        CHECK(engine.setActiveTake(0, take, nullptr));
    };

    std::vector<float> live(static_cast<std::size_t>(kBlockSize), 0.9f);
    float outL[kBlockSize] = {};
    float outR[kBlockSize] = {};

    // The 0..3 matrix: the live input (0.9) and the take voice (0.5) per mode.
    // Auto stays take-only, i.e. the live input is not doubled on top.
    struct ModeRow {
        MonitorMode mode;
        const char* name;
        float withTake;
        float withoutTake;
    };
    const ModeRow rows[] = {
        {MonitorMode::Off, "off", 0.5f, 0.0f},
        {MonitorMode::Auto, "auto", 0.5f, 0.9f},
        {MonitorMode::On, "on", 1.4f, 0.9f},
        {MonitorMode::InputOnly, "input-only", 0.9f, 0.9f},
    };
    for (const ModeRow& row : rows) {
        Engine engine;
        prepare(engine, longTake);
        engine.setMonitorMode(0, row.mode);
        CHECK(engine.monitorMode(0) == row.mode);
        processLive(engine, live.data(), outL, outR);
        const std::string withLabel =
            std::string("mode ") + row.name + " with a take";
        checkNear(withLabel.c_str(), outL[0], row.withTake);
        const float* tap = engine.tap(0);
        CHECK(tap != nullptr);
        if (tap != nullptr) {
            const std::string tapLabel =
                std::string("mode ") + row.name + " keeps the tap live";
            checkNear(tapLabel.c_str(), tap[0], 0.9f);
        }
        engine.clearActiveTake(0);
        processLive(engine, live.data(), outL, outR);
        const std::string withoutLabel =
            std::string("mode ") + row.name + " without a take";
        checkNear(withoutLabel.c_str(), outL[0], row.withoutTake);
    }

    // The session wire value is the enum value (session.hpp: 3 = monitor only).
    CHECK(static_cast<int>(MonitorMode::InputOnly) == 3);

    // Transition walk on one engine: the mode is per strip, every switch lands
    // on the next block, and the take position survives the walk.
    {
        Engine engine;
        prepare(engine, longTake);
        const MonitorMode walk[] = {MonitorMode::Off, MonitorMode::Auto,
                                    MonitorMode::On, MonitorMode::InputOnly,
                                    MonitorMode::Off};
        const char* names[] = {"off", "auto", "on", "input-only", "off"};
        const float expected[] = {0.5f, 0.5f, 1.4f, 0.9f, 0.5f};
        for (int i = 0; i < 5; ++i) {
            engine.setMonitorMode(0, walk[i]);
            processLive(engine, live.data(), outL, outR);
            const std::string label =
                std::string("transition to ") + names[i];
            checkNear(label.c_str(), outL[0], expected[i]);
        }
        CHECK(engine.monitorMode(0) == MonitorMode::Off);
        engine.setMonitorMode(0, static_cast<MonitorMode>(4));
        CHECK(engine.monitorMode(0) == MonitorMode::Auto);
    }

    // Mute/solo still win over InputOnly; the tap stays the live input.
    {
        Engine engine;
        prepare(engine, longTake);
        engine.setMonitorMode(0, MonitorMode::InputOnly);
        Channel strip = engine.channel(0);
        strip.mute = true;
        engine.setChannel(0, strip);
        processLive(engine, live.data(), outL, outR);
        checkNear("input-only mute is silent", outL[0], 0.0f);
        const float* tap = engine.tap(0);
        CHECK(tap != nullptr);
        if (tap != nullptr) {
            checkNear("input-only mute keeps the tap live", tap[0], 0.9f);
        }

        strip.mute = false;
        engine.setChannel(0, strip);
        Channel soloStrip;
        soloStrip.pan = 0.0f;
        soloStrip.solo = true;
        engine.setChannel(1, soloStrip);
        std::vector<float> stripOne(static_cast<std::size_t>(kBlockSize), 0.25f);
        const float* inputs[kMaxInputs] = {};
        inputs[0] = live.data();
        inputs[1] = stripOne.data();
        engine.process(inputs, 2, kBlockSize, outL, outR);
        checkNear("input-only does not defeat solo", outL[0], 0.25f);
    }

    // InputOnly suppresses the take voice while the take still advances: with
    // no live input the strip is silent for those blocks, and switching back to
    // Off ("playback") resumes at the frame the take reached meanwhile.
    {
        Engine engine;
        prepare(engine, rampTake);
        engine.setMonitorMode(0, MonitorMode::InputOnly);
        std::vector<float> out;
        renderMain(engine, nullptr, 2 * kBlockSize, out);
        CHECK(engine.takeUnderruns() == 0);
        for (int i = 0; i < 2 * kBlockSize; ++i) {
            if (out[static_cast<std::size_t>(i)] == 0.0f) continue;
            std::printf("FAIL input-only leaked the take at frame %d: %.9g\n", i,
                        out[static_cast<std::size_t>(i)]);
            ++gFailures;
            break;
        }
        engine.setMonitorMode(0, MonitorMode::Off);
        renderMain(engine, nullptr, kBlockSize, out);
        checkNear("input-only suppressed take resumes in place", out[0],
                  rampMaterial[static_cast<std::size_t>(2 * kBlockSize)], 1e-6f);
        checkNear("input-only suppressed take resumes sample-exact",
                  out[static_cast<std::size_t>(kBlockSize - 1)],
                  rampMaterial[static_cast<std::size_t>(3 * kBlockSize - 1)],
                  1e-6f);
    }

    // Newest-wins overlap: every layer is suppressed while all layers keep
    // running, so Off resumes the older layer at its next frame instead of
    // replaying the block it would have had.
    {
        Engine engine;
        Channel strip;
        strip.gain = 1.0f;
        strip.vol = 1.0f;
        strip.pan = 0.0f;
        engine.setChannel(0, strip);
        engine.setTransport(true, 0, 24, 12000);
        engine.setMonitorMode(0, MonitorMode::InputOnly);
        CHECK(engine.setActiveTakes(0, {olderTake, newerTake}, nullptr));
        processLive(engine, live.data(), outL, outR);
        checkNear("input-only suppresses the newest take layer", outL[0], 0.9f);
        processLive(engine, live.data(), outL, outR);
        checkNear("input-only suppresses the older layer after the newest ends",
                  outL[0], 0.9f);
        engine.setMonitorMode(0, MonitorMode::Off);
        processLive(engine, live.data(), outL, outR);
        checkNear("suppressed overlap layers keep their position", outL[0],
                  0.0f);
    }

    std::filesystem::remove_all(root, ec);
    std::printf("PASS take playback: monitor-only (InputOnly) policy\n");
}

// ------------------------------------------------ monitor-only on a stereo pair

void testMonitorInputOnlyPair() {
    const std::filesystem::path root = testRoot();
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    CHECK(!ec);

    const std::string take = (root / "pair.wav").string();
    CHECK(writeWav(take, 48000, 1, constant(2 * kBlockSize, 0.5f)));

    // Strip 3 claims device channels 4/5 as one stereo pair, so it carries one
    // arm/monitor/take set and the mode governs both sides at once. Every other
    // strip is muted: identity strips 4 and 5 would otherwise duplicate the
    // pair's device channels on Main.
    Engine engine;
    for (int c = 0; c < kMaxInputs; ++c) {
        if (c == 3) continue;
        Channel silent;
        silent.mute = true;
        engine.setChannel(c, silent);
    }
    Channel strip;
    strip.gain = 1.0f;
    strip.vol = 1.0f;
    strip.pan = 0.5f;  // balance centre: both pair sides at unity
    engine.setChannel(3, strip);
    engine.setInputSource(3, 4);
    engine.setInputPair(3, true);
    engine.setTransport(true, 0, 24, 12000);
    CHECK(engine.setActiveTake(3, take, nullptr));
    engine.setMonitorMode(3, MonitorMode::InputOnly);
    CHECK(engine.monitorMode(3) == MonitorMode::InputOnly);
    CHECK(engine.inputPair(3));

    std::vector<float> liveL(static_cast<std::size_t>(kBlockSize), 0.5f);
    std::vector<float> liveR(static_cast<std::size_t>(kBlockSize), 0.25f);
    const float* inputs[kMaxInputs] = {};
    inputs[4] = liveL.data();
    inputs[5] = liveR.data();
    float outL[kBlockSize] = {};
    float outR[kBlockSize] = {};

    engine.process(inputs, 6, kBlockSize, outL, outR);
    checkNear("input-only pair passes the live L side", outL[0], 0.5f);
    checkNear("input-only pair passes the live R side", outR[0], 0.25f);
    const float* tap = engine.tap(3);
    CHECK(tap != nullptr);
    if (tap != nullptr) {
        checkNear("input-only pair tap averages the pair", tap[0], 0.375f);
    }

    // On is the contrast: the one take voice joins the centre of both sides.
    // There is no separate R-strip mode to set, by the pairing contract.
    engine.setMonitorMode(3, MonitorMode::On);
    engine.process(inputs, 6, kBlockSize, outL, outR);
    checkNear("pair on layers the take once (L)", outL[0], 1.0f);
    checkNear("pair on layers the take once (R)", outR[0], 0.75f);

    std::filesystem::remove_all(root, ec);
    std::printf("PASS take playback: monitor-only shares a stereo pair\n");
}

// ------------------------------------------------------ range loop timing

void testRangeLoopWrapTiming() {
    const std::filesystem::path root = testRoot();
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    CHECK(!ec);

    const int frames = 1024;
    const int loopStart = 256;
    const int loopFrames = 512;
    const std::vector<float> material = ramp(frames, 0.0005f);
    const std::string take = (root / "ramp.wav").string();
    CHECK(writeWav(take, 48000, 1, material));

    Engine engine;
    // 120 bpm at 48 kHz is 24000 frames per beat, so these beats map onto
    // exactly the take frames 256..767.
    selectLoopTake(engine, take, 12000);

    TakeLoop loop;
    loop.enabled = true;
    loop.startBeats = static_cast<double>(loopStart) / 24000.0;
    loop.endBeats = static_cast<double>(loopStart + loopFrames) / 24000.0;
    engine.setTakeLoop(0, loop);
    CHECK(engine.takeLoop(0) == loop);

    // Three passes; the whole loop is primed before the first block, so the
    // sample-for-sample expectation is not a timing race.
    const int rendered = 3 * loopFrames;
    std::vector<float> out;
    renderMain(engine, nullptr, rendered, out);
    CHECK(out.size() == static_cast<std::size_t>(rendered));
    CHECK(engine.takeUnderruns() == 0);
    for (int i = 0; i < rendered; ++i) {
        const std::uint64_t offset =
            static_cast<std::uint64_t>(i) % static_cast<std::uint64_t>(loopFrames);
        const float expected =
            material[static_cast<std::size_t>(loopStart + offset)] *
            seamGain(offset, static_cast<std::uint64_t>(loopFrames));
        if (std::fabs(out[static_cast<std::size_t>(i)] - expected) > 1e-6f) {
            std::printf("FAIL range loop wrap: frame %d actual %.9g expected %.9g\n",
                        i, out[static_cast<std::size_t>(i)], expected);
            ++gFailures;
            break;
        }
    }
    // The seam is exactly silent on both sides of the wrap: that is the declick
    // and it also pins the wrap to take frame loopStart + loopFrames.
    checkNear("loop pass fades out to the seam",
              out[static_cast<std::size_t>(loopFrames - 1)], 0.0f, 1e-9f);
    checkNear("next pass fades in from the seam",
              out[static_cast<std::size_t>(loopFrames)], 0.0f, 1e-9f);
    // Unity-gain loop material starts at the mapped frame, not at take frame 0.
    checkNear("loop starts at the beat-mapped take frame",
              out[static_cast<std::size_t>(TakePlayer::kLoopFadeFrames)],
              material[static_cast<std::size_t>(loopStart +
                                                TakePlayer::kLoopFadeFrames)],
              1e-6f);
    checkNear("mid-loop frame is untouched loop material",
              out[static_cast<std::size_t>(loopFrames / 2)],
              material[static_cast<std::size_t>(loopStart + loopFrames / 2)],
              1e-6f);

    std::filesystem::remove_all(root, ec);
    std::printf("PASS take looping: beat-mapped range loop wrap timing\n");
}

// ------------------------------------------------------ whole-take (end<=start)

void testWholeTakeLoop() {
    const std::filesystem::path root = testRoot();
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    CHECK(!ec);

    const int frames = 320;
    const std::vector<float> material = ramp(frames, 0.001f);
    const std::string take = (root / "whole.wav").string();
    CHECK(writeWav(take, 48000, 1, material));

    Engine engine;
    // end < start is the whole-take rule, and the loop state survives being set
    // before the take is selected.
    TakeLoop loop;
    loop.enabled = true;
    loop.startBeats = 4.0;
    loop.endBeats = 1.0;
    engine.setTakeLoop(0, loop);
    selectLoopTake(engine, take, 12000);
    CHECK(engine.takeLoop(0) == loop);

    const int rendered = 1024;  // 3.2 passes of the 320-frame take
    std::vector<float> out;
    renderMain(engine, nullptr, rendered, out);
    CHECK(engine.takeUnderruns() == 0);
    for (int i = 0; i < rendered; ++i) {
        const std::uint64_t offset =
            static_cast<std::uint64_t>(i) % static_cast<std::uint64_t>(frames);
        const float expected =
            material[static_cast<std::size_t>(offset)] *
            seamGain(offset, static_cast<std::uint64_t>(frames));
        if (std::fabs(out[static_cast<std::size_t>(i)] - expected) > 1e-6f) {
            std::printf("FAIL whole-take loop: frame %d actual %.9g expected %.9g\n",
                        i, out[static_cast<std::size_t>(i)], expected);
            ++gFailures;
            break;
        }
    }

    // start == end is the same rule.
    TakeLoop equal;
    equal.enabled = true;
    equal.startBeats = 2.0;
    equal.endBeats = 2.0;
    engine.setTakeLoop(0, equal);
    CHECK(engine.takeLoop(0) == equal);
    renderMain(engine, nullptr, 2 * kBlockSize, out);
    for (int i = 0; i < 2 * kBlockSize; ++i) {
        const std::uint64_t offset =
            static_cast<std::uint64_t>(i) % static_cast<std::uint64_t>(frames);
        const float expected =
            material[static_cast<std::size_t>(offset)] *
            seamGain(offset, static_cast<std::uint64_t>(frames));
        if (std::fabs(out[static_cast<std::size_t>(i)] - expected) > 1e-6f) {
            std::printf("FAIL whole-take loop (start==end): frame %d actual %.9g "
                        "expected %.9g\n",
                        i, out[static_cast<std::size_t>(i)], expected);
            ++gFailures;
            break;
        }
    }

    std::filesystem::remove_all(root, ec);
    std::printf("PASS take looping: whole take when end <= start\n");
}

// -------------------------------------------------------- disabled = linear

void testDisabledLoopIsLinear() {
    const std::filesystem::path root = testRoot();
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    CHECK(!ec);

    const int frames = 384;
    const std::vector<float> material = ramp(frames, 0.001f);
    const std::string take = (root / "linear.wav").string();
    CHECK(writeWav(take, 48000, 1, material));

    Engine engine;
    TakeLoop loop;
    loop.enabled = false;  // points are kept but must not loop
    loop.startBeats = 0.5;
    loop.endBeats = 2.0;
    engine.setTakeLoop(0, loop);
    selectLoopTake(engine, take, 12000);
    CHECK(engine.takeLoop(0) == loop);

    std::vector<float> out;
    renderMain(engine, nullptr, 4 * kBlockSize, out);
    CHECK(engine.takeUnderruns() == 0);
    for (int i = 0; i < frames; ++i) {
        checkNear("disabled loop plays the take linearly",
                  out[static_cast<std::size_t>(i)],
                  material[static_cast<std::size_t>(i)], 1e-6f);
        if (gFailures != 0) break;
    }
    checkNear("disabled loop ends with the take", out[frames], 0.0f, 1e-9f);

    // An enabled range that lies past the take degrades to linear playback: the
    // state is stored, but clamping never invents audio.
    TakeLoop past;
    past.enabled = true;
    past.startBeats = 8.0;   // 192000 frames at 120 bpm, far past the take
    past.endBeats = 16.0;
    engine.setTakeLoop(0, past);
    CHECK(engine.takeLoop(0) == past);
    renderMain(engine, nullptr, 4 * kBlockSize, out);
    for (int i = 0; i < frames; ++i) {
        checkNear("out-of-range loop falls back to linear playback",
                  out[static_cast<std::size_t>(i)],
                  material[static_cast<std::size_t>(i)], 1e-6f);
        if (gFailures != 0) break;
    }
    checkNear("out-of-range loop still ends with the take", out[frames], 0.0f,
              1e-9f);

    // Out-of-range tracks are ignored, and the getter stays at its default.
    engine.setTakeLoop(-1, past);
    CHECK(engine.takeLoop(-1) == TakeLoop{});
    CHECK(engine.takeLoop(kMaxInputs) == TakeLoop{});

    std::filesystem::remove_all(root, ec);
    std::printf("PASS take looping: disabled and out-of-range loops are linear\n");
}

// ---------------------------------------------------- loop + newest-wins overlap

void testLoopKeepsNewestWins() {
    const std::filesystem::path root = testRoot();
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    CHECK(!ec);

    const std::string older = (root / "older-loop.wav").string();
    const std::string newer = (root / "newer-loop.wav").string();
    CHECK(writeWav(older, 48000, 1, constant(2 * kBlockSize, 0.25f)));
    CHECK(writeWav(newer, 48000, 1, constant(kBlockSize, 0.75f)));

    Engine engine;
    Channel strip;
    strip.gain = 1.0f;
    strip.vol = 1.0f;
    strip.pan = 0.0f;
    engine.setChannel(0, strip);
    engine.setTransport(true, 0, 24, 12000);
    CHECK(engine.setActiveTakes(0, {older, newer}, nullptr));

    TakeLoop loop;
    loop.enabled = true;  // end <= start: each layer loops its whole stream
    engine.setTakeLoop(0, loop);

    std::vector<float> out;
    renderMain(engine, nullptr, 3 * kBlockSize, out);
    CHECK(engine.takeUnderruns() == 0);
    for (int i = 0; i < 3 * kBlockSize; ++i) {
        if (!(out[static_cast<std::size_t>(i)] <= 0.75f + 1e-6f)) {
            std::printf("FAIL looped newest-wins: frame %d is %.9g\n", i,
                        out[static_cast<std::size_t>(i)]);
            ++gFailures;
            break;
        }
    }
    // The newer layer's loop seam is silent, and the older layer would still
    // have frames there: a leak would show up as exactly 0.25.
    checkNear("older layer stays muted at the newer layer's loop seam", out[0],
              0.0f, 1e-9f);
    // The newest layer is the one that wraps (128-frame take, 32-frame seam
    // ramp) and it keeps winning on the next pass.
    checkNear("the winning take is the looped one",
              out[static_cast<std::size_t>(kBlockSize + 64)], 0.75f, 1e-6f);
    checkNear("the winning take still wins on its second pass",
              out[static_cast<std::size_t>(2 * kBlockSize + 64)], 0.75f, 1e-6f);

    // Loop off returns to linear newest-wins (today's behaviour, unchanged).
    engine.setTakeLoop(0, TakeLoop{});
    renderMain(engine, nullptr, 4 * kBlockSize, out);
    checkNear("loop off: newest take plays first", out[0], 0.75f, 1e-6f);
    checkNear("loop off: older take resumes after the newer file ends",
              out[static_cast<std::size_t>(kBlockSize)], 0.25f, 1e-6f);
    checkNear("loop off: both layers end",
              out[static_cast<std::size_t>(2 * kBlockSize)], 0.0f, 1e-9f);

    std::filesystem::remove_all(root, ec);
    std::printf("PASS take looping: newest-wins overlap still applies\n");
}

// ------------------------------------------------- loop + input monitoring

void testLoopWithMonitorModes() {
    const std::filesystem::path root = testRoot();
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    CHECK(!ec);

    const int frames = 256;
    const std::string take = (root / "loop-monitor.wav").string();
    CHECK(writeWav(take, 48000, 1, constant(frames, 0.5f)));

    const auto prepare = [&](Engine& engine) {
        Channel strip;
        strip.gain = 1.0f;
        strip.vol = 1.0f;
        strip.pan = 0.0f;
        engine.setChannel(0, strip);
        engine.setTransport(true, 0, 24, 12000);
        CHECK(engine.setActiveTake(0, take, nullptr));
        TakeLoop loop;
        loop.enabled = true;  // whole take
        engine.setTakeLoop(0, loop);
    };

    std::vector<float> live(static_cast<std::size_t>(kBlockSize), 0.9f);
    std::vector<float> out;

    // Auto + disarmed: the loop keeps the take "playing", so live input never
    // doubles it, not even on later passes.
    {
        Engine engine;
        prepare(engine);
        CHECK(engine.monitorMode(0) == MonitorMode::Auto);
        renderMain(engine, live.data(), 4 * kBlockSize, out);
        CHECK(engine.takeUnderruns() == 0);
        checkNear("looping take with Auto is not doubled by live input", out[64],
                  0.5f, 1e-6f);
        checkNear("Auto stays take-only after the loop wraps",
                  out[static_cast<std::size_t>(frames + 64)], 0.5f, 1e-6f);
        const float* tap = engine.tap(0);
        CHECK(tap != nullptr);
        if (tap != nullptr) {
            checkNear("looping take does not change the live tap", tap[0], 0.9f,
                      1e-6f);
        }
    }

    // Off: live input never reaches Main, the looping take stays audible.
    {
        Engine engine;
        prepare(engine);
        engine.setMonitorMode(0, MonitorMode::Off);
        renderMain(engine, live.data(), 4 * kBlockSize, out);
        checkNear("off keeps the looping take voice", out[64], 0.5f, 1e-6f);
        checkNear("off keeps the looping take voice after the wrap",
                  out[static_cast<std::size_t>(frames + 64)], 0.5f, 1e-6f);
    }

    // On: live input still layers on top of the looping take.
    {
        Engine engine;
        prepare(engine);
        engine.setMonitorMode(0, MonitorMode::On);
        renderMain(engine, live.data(), 4 * kBlockSize, out);
        checkNear("on layers live input over the looping take", out[64], 1.4f,
                  1e-6f);
        checkNear("on layers live input over the second pass",
                  out[static_cast<std::size_t>(frames + 64)], 1.4f, 1e-6f);
    }

    // InputOnly: the looping take is suppressed, live input passes alone.
    {
        Engine engine;
        prepare(engine);
        engine.setMonitorMode(0, MonitorMode::InputOnly);
        renderMain(engine, live.data(), 4 * kBlockSize, out);
        checkNear("input-only suppresses the looping take", out[64], 0.9f, 1e-6f);
        checkNear("input-only suppresses the looping take after the wrap",
                  out[static_cast<std::size_t>(frames + 64)], 0.9f, 1e-6f);
        const float* tap = engine.tap(0);
        CHECK(tap != nullptr);
        if (tap != nullptr) {
            checkNear("input-only keeps the looping take's tap live", tap[0],
                      0.9f, 1e-6f);
        }
    }

    // Armed: the take and its loop are ignored, live input passes.
    {
        Engine engine;
        prepare(engine);
        Channel strip = engine.channel(0);
        strip.arm = true;
        engine.setChannel(0, strip);
        renderMain(engine, live.data(), kBlockSize, out);
        checkNear("armed track ignores a looping take", out[0], 0.9f, 1e-6f);
    }

    // Stopped transport: a loop never keeps a stopped take "playing".
    {
        Engine engine;
        prepare(engine);
        engine.setTransport(false, 0, 24, 12000);
        renderMain(engine, live.data(), kBlockSize, out);
        checkNear("stopped loop transport monitors live input", out[0], 0.9f,
                  1e-6f);
    }

    std::filesystem::remove_all(root, ec);
    std::printf("PASS take looping: monitor modes unchanged\n");
}

}  // namespace

int main() {
    testSelectionAndArm();
    testNewestWinsOverlap();
    testNon48kRefused();
    testStereoDownmix();
    testMonitorModes();
    testMonitorInputOnly();
    testMonitorInputOnlyPair();
    testRangeLoopWrapTiming();
    testWholeTakeLoop();
    testDisabledLoopIsLinear();
    testLoopKeepsNewestWins();
    testLoopWithMonitorModes();

    if (gFailures != 0) {
        std::printf("FAIL take playback: %d check(s) failed\n", gFailures);
        return 1;
    }
    std::printf("PASS\n");
    return 0;
}
