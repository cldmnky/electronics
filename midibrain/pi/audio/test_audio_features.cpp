// Assert-style acceptance tests for the additive engine/recorder features:
// Channel::tapPoint provenance and Recorder master arming (no framework).
// Build: clang++ -std=c++20 -Wall -Wextra -Werror -I. test_audio_features.cpp engine.cpp recorder.cpp -o /tmp/t_audio_features -pthread && /tmp/t_audio_features

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
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

// ------------------------------------------------------- tap point provenance

void testTapPointProvenance() {
    Engine engine;
    float input[kBlockSize];
    for (int i = 0; i < kBlockSize; ++i) {
        input[i] = 0.25f + 0.001f * static_cast<float>(i);
    }
    const float* inputs[kMaxInputs] = {&input[0]};
    float outL[kBlockSize];
    float outR[kBlockSize];

    // The default is today's post-gain tap.
    Channel strip;
    CHECK(strip.tapPoint == TapPoint::PostGain);
    strip.gain = 2.0f;
    engine.setChannel(0, strip);
    engine.process(inputs, 1, kBlockSize, outL, outR);
    const float* tap = engine.tap(0);
    CHECK(tap != nullptr);
    if (tap != nullptr) {
        checkNear("default tap is post-gain", tap[0], input[0] * 2.0f);
        checkNear("default tap is post-gain (last frame)", tap[kBlockSize - 1],
                  input[kBlockSize - 1] * 2.0f);
    }

    // A gain change moves the post-gain tap...
    strip.gain = 0.5f;
    engine.setChannel(0, strip);
    engine.process(inputs, 1, kBlockSize, outL, outR);
    if (tap != nullptr) checkNear("post-gain tap follows gain", tap[0], input[0] * 0.5f);

    // ...and is invisible to the pre-gain tap.
    strip.tapPoint = TapPoint::PreGain;
    engine.setChannel(0, strip);
    engine.process(inputs, 1, kBlockSize, outL, outR);
    if (tap != nullptr) {
        checkNear("pre-gain tap is the raw input", tap[0], input[0]);
        checkNear("pre-gain tap is the raw input (last frame)", tap[kBlockSize - 1],
                  input[kBlockSize - 1]);
    }

    strip.gain = 4.0f;
    engine.setChannel(0, strip);
    engine.process(inputs, 1, kBlockSize, outL, outR);
    if (tap != nullptr) checkNear("pre-gain tap ignores a gain change", tap[0], input[0]);

    // Mute and solo do not change either tap; they still pull the strip from
    // the master.
    strip.mute = true;
    engine.setChannel(0, strip);
    engine.process(inputs, 1, kBlockSize, outL, outR);
    if (tap != nullptr) checkNear("pre-gain tap ignores mute", tap[0], input[0]);
    checkNear("muted strip is out of the master", outL[0], 0.0f);
    checkNear("muted strip is out of the master (R)", outR[0], 0.0f);

    strip.tapPoint = TapPoint::PostGain;
    engine.setChannel(0, strip);
    engine.process(inputs, 1, kBlockSize, outL, outR);
    if (tap != nullptr) checkNear("post-gain tap ignores mute", tap[0], input[0] * 4.0f);

    // Channels outside the supplied block stay silent for both tap points.
    const float* empty[kMaxInputs] = {};
    engine.process(empty, 0, kBlockSize, outL, outR);
    if (tap != nullptr) checkNear("missing channel taps silence", tap[0], 0.0f);
    CHECK(engine.tap(-1) == nullptr);
    CHECK(engine.tap(kMaxInputs) == nullptr);

    std::printf("PASS audio engine: tap provenance pre vs post gain\n");
}

// ------------------------------------------------------- recorder master arming

void testRecorderMasterArm() {
    std::error_code ec;
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "mnky_audio_features_take";
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    CHECK(!ec);

    std::vector<float> ch0(kBlockSize, 0.25f);
    std::vector<float> ch1(kBlockSize, -0.5f);
    std::vector<float> masterL(kBlockSize, 0.125f);
    std::vector<float> masterR(kBlockSize, -0.25f);
    const float* taps[kMaxInputs] = {};
    taps[0] = ch0.data();
    taps[1] = ch1.data();

    // The existing three-argument call still records the master (default on).
    Recorder withMaster;
    CHECK(withMaster.startTake((root / "with-master").string(), 0x3u, 48000));
    withMaster.writeBlock(taps, masterL.data(), masterR.data(), kBlockSize);
    const Recorder::Take defaultTake = withMaster.stopTake();
    CHECK(defaultTake.files == 3);
    CHECK(defaultTake.frames == static_cast<uint64_t>(kBlockSize));
    const std::filesystem::path defaultDir(defaultTake.path);
    CHECK(std::filesystem::exists(defaultDir / "ch00.wav"));
    CHECK(std::filesystem::exists(defaultDir / "ch01.wav"));
    CHECK(std::filesystem::exists(defaultDir / "master.wav"));

    // An explicit master arm is the same as the default.
    Recorder explicitMaster;
    CHECK(explicitMaster.startTake((root / "explicit-master").string(), 0x1u, 48000, true));
    explicitMaster.writeBlock(taps, masterL.data(), masterR.data(), kBlockSize);
    const Recorder::Take explicitTake = explicitMaster.stopTake();
    CHECK(explicitTake.files == 2);
    CHECK(std::filesystem::exists(std::filesystem::path(explicitTake.path) / "ch00.wav"));
    CHECK(std::filesystem::exists(std::filesystem::path(explicitTake.path) / "master.wav"));

    // Master disarmed: the channel WAVs are written, master.wav is not.
    Recorder channelOnly;
    CHECK(channelOnly.startTake((root / "channels-only").string(), 0x3u, 48000, false));
    CHECK(channelOnly.recording());
    channelOnly.writeBlock(taps, masterL.data(), masterR.data(), kBlockSize);
    const Recorder::Take channelTake = channelOnly.stopTake();
    CHECK(channelTake.files == 2);
    CHECK(channelTake.frames == static_cast<uint64_t>(kBlockSize));
    CHECK(channelTake.drops == 0);
    const std::filesystem::path channelDir(channelTake.path);
    CHECK(std::filesystem::exists(channelDir / "ch00.wav"));
    CHECK(std::filesystem::exists(channelDir / "ch01.wav"));
    CHECK(!std::filesystem::exists(channelDir / "master.wav"));

    // A disarmed master does not relax the non-empty arm-mask requirement.
    Recorder rejected;
    CHECK(!rejected.startTake((root / "empty").string(), 0u, 48000, false));
    CHECK(!rejected.recording());

    std::filesystem::remove_all(root, ec);
    std::printf("PASS audio recorder: master arming on/off file sets\n");
}

}  // namespace

int main() {
    testTapPointProvenance();
    testRecorderMasterArm();

    if (gFailures != 0) {
        std::printf("FAIL audio features: %d check(s) failed\n", gFailures);
        return 1;
    }
    std::printf("PASS\n");
    return 0;
}
