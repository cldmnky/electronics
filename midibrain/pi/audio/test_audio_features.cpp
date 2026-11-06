// Assert-style acceptance tests for the additive engine/recorder features:
// Channel::tapPoint provenance and Recorder master arming (no framework).
// Build: clang++ -std=c++20 -Wall -Wextra -Werror -I. test_audio_features.cpp engine.cpp recorder.cpp -o /tmp/t_audio_features -pthread && /tmp/t_audio_features

#include <algorithm>
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
    CHECK(engine.tap(kMaxTracks) == nullptr);

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

// ------------------------------------------------- send sources and monitors

void testSendSourcesAndMonitors() {
    Engine engine;
    Channel strip;
    strip.gain = 0.5f;
    strip.vol = 0.25f;
    strip.pan = 0.0f;  // hard left
    strip.cueSend = 1.0f;
    strip.out34Send = 1.0f;
    engine.setChannel(0, strip);
    engine.setCueStyle(CueStyle::Level);
    engine.setPhonesSource(PhonesSource::Cue);
    engine.setOut34Mode(Out34Mode::Stereo);
    engine.setSendSource(SendDestination::Cue, SendSource::PreFader);
    engine.setSendSource(SendDestination::Out34, SendSource::PostFader);

    std::vector<float> input(static_cast<std::size_t>(kBlockSize), 1.0f);
    const float* inputs[kMaxInputs] = {input.data()};
    float mainL[kBlockSize] = {};
    float mainR[kBlockSize] = {};
    float phonesL[kBlockSize] = {};
    float phonesR[kBlockSize] = {};
    float out34L[kBlockSize] = {};
    float out34R[kBlockSize] = {};
    OutputBuses outputs;
    outputs.mainL = mainL;
    outputs.mainR = mainR;
    outputs.phonesL = phonesL;
    outputs.phonesR = phonesR;
    outputs.out34L = out34L;
    outputs.out34R = out34R;

    engine.process(inputs, 1, kBlockSize, outputs);
    checkNear("dry main uses gain * vol", mainL[0], 0.5f * 0.25f);
    checkNear("dry main follows pan (R)", mainR[0], 0.0f);
    checkNear("pre-fader cue applies gain, not vol, pre-pan L", phonesL[0], 0.5f);
    checkNear("pre-fader cue applies gain, not vol, pre-pan R", phonesR[0], 0.5f);
    checkNear("post-fader out34 applies vol and pan L", out34L[0], 0.125f);
    checkNear("post-fader out34 applies vol and pan R", out34R[0], 0.0f);

    engine.setSendSource(SendDestination::Cue, SendSource::PostFader);
    engine.setSendSource(SendDestination::Out34, SendSource::PreFader);
    engine.process(inputs, 1, kBlockSize, outputs);
    checkNear("post-fader cue follows pan L", phonesL[0], 0.125f);
    checkNear("post-fader cue follows pan R", phonesR[0], 0.0f);
    checkNear("pre-fader out34 is gain-only and centred L", out34L[0], 0.5f);
    checkNear("pre-fader out34 is gain-only and centred R", out34R[0], 0.5f);

    // Dual-mono OUT3&4 writes the unpanned send to both jacks.
    engine.setOut34Mode(Out34Mode::DualMono);
    engine.setSendSource(SendDestination::Out34, SendSource::PostFader);
    engine.process(inputs, 1, kBlockSize, outputs);
    checkNear("dual-mono out34 post-fader L", out34L[0], 0.125f);
    checkNear("dual-mono out34 post-fader R", out34R[0], 0.125f);
    engine.setSendSource(SendDestination::Out34, SendSource::PreFader);
    engine.process(inputs, 1, kBlockSize, outputs);
    checkNear("dual-mono out34 pre-fader L", out34L[0], 0.5f);
    checkNear("dual-mono out34 pre-fader R", out34R[0], 0.5f);

    // CUE toggle style is unity whenever the per-track send is non-zero.
    strip.cueSend = 0.25f;
    engine.setChannel(0, strip);
    engine.setCueStyle(CueStyle::Toggle);
    engine.setSendSource(SendDestination::Cue, SendSource::PreFader);
    engine.process(inputs, 1, kBlockSize, outputs);
    checkNear("cue toggle uses unity, not the send level", phonesL[0], 0.5f);

    // Mute closes dry and every send, pre or post.
    strip.mute = true;
    engine.setChannel(0, strip);
    engine.process(inputs, 1, kBlockSize, outputs);
    checkNear("muted strip out of main", mainL[0], 0.0f);
    checkNear("muted strip out of cue", phonesL[0], 0.0f);
    checkNear("muted strip out of out34", out34L[0], 0.0f);

    // FX pre/post source changes the bus level but keeps the default post path.
    Engine fxEngine;
    Channel fxStrip;
    fxStrip.gain = 0.5f;
    fxStrip.vol = 0.25f;
    fxStrip.pan = 0.5f;
    fxStrip.send[0] = 0.5f;
    fxEngine.setChannel(0, fxStrip);
    fxEngine.setFxReturn(0, 1.0f);
    fxEngine.process(inputs, 1, kBlockSize, outputs);
    constexpr float kCentreGain = 0.70710678f;
    const float fxDry = 0.5f * 0.25f * kCentreGain;
    const float fxPostSend = 0.5f * 0.25f * kCentreGain * 0.5f;
    const float fxPreSend = 0.5f * 0.5f;
    checkNear("default post-fader FX send L", mainL[0], fxDry + fxPostSend);
    checkNear("default post-fader FX send R", mainR[0], fxDry + fxPostSend);
    fxEngine.setSendSource(SendDestination::Fx1, SendSource::PreFader);
    fxEngine.process(inputs, 1, kBlockSize, outputs);
    checkNear("pre-fader FX send L", mainL[0], fxDry + fxPreSend);
    checkNear("pre-fader FX send R", mainR[0], fxDry + fxPreSend);

    std::printf("PASS audio engine: cue/out34/FX send sources and monitor buses\n");
}

// ------------------------------------------------------ main-bus compressor

void testMainCompressor() {
    Engine engine;
    Channel strip;
    strip.gain = 1.0f;
    strip.vol = 1.0f;
    strip.pan = 0.0f;
    strip.out34Send = 1.0f;
    engine.setChannel(0, strip);
    engine.setOut34Mode(Out34Mode::Stereo);
    engine.setSendSource(SendDestination::Out34, SendSource::PostFader);

    std::vector<float> input(static_cast<std::size_t>(kBlockSize), 1.0f);
    const float* inputs[kMaxInputs] = {input.data()};
    float mainL[kBlockSize] = {};
    float mainR[kBlockSize] = {};
    float out34L[kBlockSize] = {};
    float out34R[kBlockSize] = {};
    OutputBuses outputs;
    outputs.mainL = mainL;
    outputs.mainR = mainR;
    outputs.out34L = out34L;
    outputs.out34R = out34R;

    // Off is an exact bypass.
    engine.process(inputs, 1, kBlockSize, outputs);
    checkNear("compressor off is a bypass", mainL[0], 1.0f);

    MainCompSettings settings;
    settings.on = true;
    settings.threshDb = -12.0f;
    settings.ratio = 4.0f;
    settings.attackMs = 0.0f;
    settings.releaseMs = 0.0f;
    settings.makeupDb = 0.0f;
    settings.mode = CompressorMode::Peak;
    engine.setMainComp(settings);
    engine.process(inputs, 1, kBlockSize, outputs);
    const float peakExpected = std::pow(10.0f, -9.0f / 20.0f);  // 12 * (1 - 1/4)
    checkNear("peak comp attenuation", mainL[0], peakExpected, 1e-5f);
    checkNear("out34 is never compressed", out34L[0], 1.0f);

    settings.makeupDb = 6.0f;
    engine.setMainComp(settings);
    engine.process(inputs, 1, kBlockSize, outputs);
    checkNear("makeup is applied after compression", mainL[0],
              peakExpected * std::pow(10.0f, 6.0f / 20.0f), 1e-5f);

    // Peak vs RMS on a 1 kHz sine: RMS detects ~3 dB less level, so it
    // compresses less than peak at the same threshold.
    auto runSine = [](CompressorMode mode) {
        Engine sineEngine;
        Channel sineStrip;
        sineStrip.pan = 0.0f;
        sineEngine.setChannel(0, sineStrip);
        MainCompSettings sineSettings;
        sineSettings.on = true;
        sineSettings.threshDb = -12.0f;
        sineSettings.ratio = 4.0f;
        sineSettings.attackMs = 0.0f;
        sineSettings.releaseMs = 0.0f;
        sineSettings.mode = mode;
        sineEngine.setMainComp(sineSettings);
        float lastPeak = 0.0f;
        double phase = 0.0;
        for (int block = 0; block < 32; ++block) {
            float sine[kBlockSize] = {};
            for (int i = 0; i < kBlockSize; ++i) {
                sine[i] = static_cast<float>(std::sin(phase));
                phase += 2.0 * 3.14159265358979323846 * 1000.0 / 48000.0;
            }
            const float* sineInputs[kMaxInputs] = {sine};
            float left[kBlockSize] = {};
            float right[kBlockSize] = {};
            sineEngine.process(sineInputs, 1, kBlockSize, left, right);
            if (block == 31) {
                for (int i = 0; i < kBlockSize; ++i) {
                    lastPeak = std::max(lastPeak, std::fabs(left[i]));
                }
            }
        }
        return lastPeak;
    };
    const float peakPeak = runSine(CompressorMode::Peak);
    const float rmsPeak = runSine(CompressorMode::Rms);
    CHECK(peakPeak > 0.2f);
    CHECK(rmsPeak > peakPeak * 1.1f);

    // Range clamps and NaN fallback.
    MainCompSettings extreme;
    extreme.threshDb = -100.0f;
    extreme.ratio = 0.0f;
    extreme.attackMs = -5.0f;
    extreme.releaseMs = 5000.0f;
    extreme.makeupDb = 50.0f;
    extreme.mode = CompressorMode::Rms;
    extreme.on = true;
    Engine clamped;
    clamped.setMainComp(extreme);
    const MainCompSettings& live = clamped.mainComp();
    CHECK(live.threshDb == -48.0f);
    CHECK(live.ratio == 1.0f);
    CHECK(live.attackMs == 0.0f);
    CHECK(live.releaseMs == 1000.0f);
    CHECK(live.makeupDb == 36.0f);
    CHECK(live.mode == CompressorMode::Rms);

    std::printf("PASS audio engine: main-bus compressor and OUT3&4 isolation\n");
}

// ---------------------------------------------------- transport / punch source

void testTransportPosition() {
    Engine engine;
    engine.setTransport(true, 96, 24, 12000);
    const TransportPosition& downbeat = engine.transportPosition();
    CHECK(downbeat.valid);
    CHECK(downbeat.rolling);
    CHECK(downbeat.measure == 2);
    CHECK(downbeat.beat == 1);
    CHECK(downbeat.tickInBeat == 0);
    CHECK(downbeat.frame == 96000);  // 120 bpm, 24 ppqn -> 1000 frames/tick

    engine.setTransport(false, 100, 24, 12000);
    const TransportPosition& late = engine.transportPosition();
    CHECK(!late.rolling);
    CHECK(late.measure == 2);
    CHECK(late.beat == 1);
    CHECK(late.tickInBeat == 4);
    CHECK(late.frame == 100000);

    engine.setTransport(true, 0, 0, 12000);
    const TransportPosition& invalid = engine.transportPosition();
    CHECK(!invalid.valid);
    CHECK(invalid.measure == 1);
    CHECK(invalid.beat == 1);
    CHECK(invalid.frame == 0);

    std::printf("PASS audio engine: transport position source for punch\n");
}

}  // namespace

int main() {
    testTapPointProvenance();
    testRecorderMasterArm();
    testSendSourcesAndMonitors();
    testMainCompressor();
    testTransportPosition();

    if (gFailures != 0) {
        std::printf("FAIL audio features: %d check(s) failed\n", gFailures);
        return 1;
    }
    std::printf("PASS\n");
    return 0;
}
