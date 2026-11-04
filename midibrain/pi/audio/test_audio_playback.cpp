// Assert-style acceptance tests for engine take playback (no framework).
// Build: clang++ -std=c++20 -Wall -Wextra -Werror -I. test_audio_playback.cpp
//        engine.cpp take_player.cpp recorder.cpp click.cpp -o /tmp/t_play -pthread
//
// Covers per-track selection, the newest-wins overlap rule with synthetic
// float32 WAVs, the disarmed/armed input-path rule and the 48 kHz refusal.

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

}  // namespace

int main() {
    testSelectionAndArm();
    testNewestWinsOverlap();
    testNon48kRefused();
    testStereoDownmix();
    testMonitorModes();

    if (gFailures != 0) {
        std::printf("FAIL take playback: %d check(s) failed\n", gFailures);
        return 1;
    }
    std::printf("PASS\n");
    return 0;
}
