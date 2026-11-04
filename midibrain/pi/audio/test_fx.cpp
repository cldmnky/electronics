// Assert-style acceptance tests for mnky::audio::fx (no external framework).
// Build (from midibrain/pi/audio), one command:
//   clang++ -std=c++20 -Wall -Wextra -Werror -I. test_fx.cpp
//     fx/comp.cpp fx/delay.cpp fx/effect.cpp fx/eq.cpp fx/params.cpp
//     fx/registry.cpp fx/reverb.cpp engine.cpp -o /tmp/t_fx && /tmp/t_fx

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "engine.hpp"
#include "fx/comp.hpp"
#include "fx/delay.hpp"
#include "fx/effect.hpp"
#include "fx/eq.hpp"
#include "fx/params.hpp"
#include "fx/registry.hpp"
#include "fx/reverb.hpp"

namespace {

using namespace mnky::audio;
using namespace mnky::audio::fx;

int gFailures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++gFailures;                                                \
        }                                                               \
    } while (0)

void checkNear(const char* what, double actual, double expected, double eps) {
    if (std::fabs(actual - expected) <= eps) return;
    std::printf("FAIL %s: actual %.9g expected %.9g (eps %.3g)\n", what, actual, expected, eps);
    ++gFailures;
}

void fill(float* buffer, int n, float value) {
    for (int i = 0; i < n; ++i) buffer[i] = value;
}

double maxDifference(const float* a, const float* b, int n) {
    double worst = 0.0;
    for (int i = 0; i < n; ++i) {
        const double difference = std::fabs(static_cast<double>(a[i]) - static_cast<double>(b[i]));
        if (difference > worst) worst = difference;
    }
    return worst;
}

bool allFinite(const float* a, int n) {
    for (int i = 0; i < n; ++i) {
        if (!std::isfinite(static_cast<double>(a[i]))) return false;
    }
    return true;
}

float runConstant(Effect& effect, float value, int blocks) {
    float left[kBlockSize] = {};
    float right[kBlockSize] = {};
    for (int block = 0; block < blocks; ++block) {
        fill(left, kBlockSize, value);
        fill(right, kBlockSize, value);
        float* buffers[2] = {left, right};
        effect.process(buffers, 2, kBlockSize);
    }
    return left[kBlockSize - 1];
}

float runAlternating(Effect& effect, float amplitude, int blocks) {
    float left[kBlockSize] = {};
    float right[kBlockSize] = {};
    int phase = 0;
    for (int block = 0; block < blocks; ++block) {
        for (int i = 0; i < kBlockSize; ++i) {
            const float value = (phase++ % 2 == 0) ? amplitude : -amplitude;
            left[i] = value;
            right[i] = value;
        }
        float* buffers[2] = {left, right};
        effect.process(buffers, 2, kBlockSize);
    }
    return left[kBlockSize - 1];
}

// ---------------------------------------------------------------- ramp / API

void testRamp() {
    Ramp ramp;
    ramp.snap(0.0);
    ramp.setTarget(1.0, 1024);
    CHECK(ramp.target == 1.0);

    double previous = 0.0;
    double largestStep = 0.0;
    for (int block = 0; block < 16; ++block) {
        const double value = ramp.next(kBlockSize);
        CHECK(value >= previous - 1e-12);
        const double step = value - previous;
        if (step > largestStep) largestStep = step;
        previous = value;
    }
    // 128 samples of a 1024-sample ramp: bounded, monotonic, converged.
    CHECK(largestStep <= 0.126);
    checkNear("ramp converges", previous, 1.0, 1e-12);

    ramp.setTarget(-1.0, 512);
    previous = 1.0;
    largestStep = 0.0;
    for (int block = 0; block < 8; ++block) {
        const double value = ramp.next(kBlockSize);
        const double step = std::fabs(value - previous);
        if (step > largestStep) largestStep = step;
        previous = value;
    }
    CHECK(largestStep <= 0.51);
    checkNear("ramp retarget converges", previous, -1.0, 1e-12);

    ramp.snap(0.25);
    CHECK(ramp.next(0) == 0.25);
    ramp.setTarget(0.75, 0);
    CHECK(ramp.current == 0.75);

    Eq eq;
    CHECK(!eq.setParam(-1, 0.0));
    CHECK(!eq.setParam(Eq::kParamCount, 0.0));
    CHECK(!eq.setParam(Eq::kBand0Gain, std::nan("")));
    CHECK(eq.setParam(Eq::kBand0Gain, 1000.0));
    checkNear("parameter clamps to max", eq.param(Eq::kBand0Gain), 24.0, 0.0);
    CHECK(eq.setParam(Eq::kBand0Gain, -1000.0));
    checkNear("parameter clamps to min", eq.param(Eq::kBand0Gain), -24.0, 0.0);
    CHECK(eq.setParam(Eq::kBand0Type, 3.7));
    checkNear("stepped parameter rounds", eq.param(Eq::kBand0Type), 4.0, 0.0);

    std::printf("PASS fx: ramp smoothing bounds and parameter clamping\n");
}

// ----------------------------------------------------------------- registry

void testRegistry() {
    Registry registry;
    CHECK(registry.size() == 4u);
    CHECK(registry.contains("eq"));
    CHECK(registry.contains("comp"));
    CHECK(registry.contains("delay"));
    CHECK(registry.contains("reverb"));
    CHECK(!registry.contains("nope"));

    int seen = 0;
    for (const EffectInfo& info : registry.list()) {
        CHECK(!info.id.empty());
        CHECK(!info.manufacturer.empty());
        CHECK(!info.type.empty());
        CHECK(!info.category.empty());
        CHECK(!info.name.empty());
        const std::unique_ptr<IEffect> effect = registry.create(info.id);
        CHECK(effect != nullptr);
        if (effect != nullptr) {
            CHECK(std::string(effect->name()) == info.name);
            const auto* typed = dynamic_cast<const Effect*>(effect.get());
            CHECK(typed != nullptr);
            if (typed != nullptr) CHECK(typed->id() == info.id);
        }
        ++seen;
    }
    CHECK(seen == 4);
    CHECK(registry.info("eq") != nullptr);
    CHECK(registry.info("eq")->manufacturer == "mnkybox");
    CHECK(registry.info("eq")->type == "effect");
    CHECK(registry.info("nope") == nullptr);
    CHECK(registry.create("nope") == nullptr);

    const Registry::Creator duplicate = []() -> std::unique_ptr<IEffect> { return std::make_unique<Eq>(); };
    CHECK(!registry.add({"eq", "mnkybox", "effect", "EQ", "Duplicate"}, duplicate));
    CHECK(!registry.add({"", "mnkybox", "effect", "EQ", "Empty"}, duplicate));
    CHECK(!registry.add({"bad id", "mnkybox", "effect", "EQ", "Bad"}, duplicate));
    CHECK(!registry.add({"extra", "", "effect", "EQ", "No maker"}, duplicate));
    CHECK(!registry.add({"extra", "mnkybox", "effect", "EQ", "No creator"}, Registry::Creator{}));

    std::printf("PASS fx: registry list/create/unknown-id and metadata\n");
}

// ------------------------------------------------------------------- bypass

void testBypass() {
    Registry registry;
    for (const EffectInfo& info : registry.list()) {
        std::unique_ptr<IEffect> effect = registry.create(info.id);
        CHECK(effect != nullptr);
        if (effect == nullptr) continue;
        auto* base = dynamic_cast<Effect*>(effect.get());
        CHECK(base != nullptr);
        if (base == nullptr) continue;
        for (int i = 0; i < static_cast<int>(base->params().size()); ++i) {
            const Param& param = base->params()[static_cast<size_t>(i)];
            const double midway = param.min + 0.5 * (param.max - param.min);
            CHECK(base->setParam(i, midway));
        }

        // Warm the DSP state first, then bypass; bypass must still be exact.
        float warmLeft[kBlockSize] = {};
        float warmRight[kBlockSize] = {};
        for (int i = 0; i < kBlockSize; ++i) {
            warmLeft[i] = 0.01f * static_cast<float>(i % 17);
            warmRight[i] = -warmLeft[i];
        }
        float* warmBuffers[2] = {warmLeft, warmRight};
        effect->process(warmBuffers, 2, kBlockSize);
        CHECK(allFinite(warmLeft, kBlockSize));
        CHECK(allFinite(warmRight, kBlockSize));
        base->setBypass(true);

        float left[kBlockSize] = {};
        float right[kBlockSize] = {};
        for (int i = 0; i < kBlockSize; ++i) {
            left[i] = static_cast<float>((i * 37) % 101) / 101.0f - 0.5f;
            right[i] = static_cast<float>((i * 53) % 89) / 89.0f - 0.5f;
        }
        float expectedLeft[kBlockSize] = {};
        float expectedRight[kBlockSize] = {};
        std::memcpy(expectedLeft, left, sizeof left);
        std::memcpy(expectedRight, right, sizeof right);
        float* buffers[2] = {left, right};
        effect->process(buffers, 2, kBlockSize);
        CHECK(std::memcmp(left, expectedLeft, sizeof left) == 0);
        CHECK(std::memcmp(right, expectedRight, sizeof right) == 0);
    }

    std::printf("PASS fx: bypass is bit-exact dry for every builtin\n");
}

// ----------------------------------------------------------------------- EQ

void testEq() {
    Eq flat;
    CHECK(flat.params().size() == static_cast<size_t>(Eq::kParamCount));
    CHECK(flat.setActiveBand(0));
    CHECK(flat.activeBand() == 0);
    CHECK(flat.params()[static_cast<size_t>(Eq::kBand0Type)].stepped);
    CHECK(flat.bandType(0) == Eq::BandType::LowShelf);
    CHECK(flat.bandType(1) == Eq::BandType::Parametric);
    CHECK(flat.bandType(2) == Eq::BandType::Parametric);
    CHECK(flat.bandType(3) == Eq::BandType::HighShelf);

    // Default gains are zero, so the four default bands are identity.
    float left[kBlockSize] = {};
    float right[kBlockSize] = {};
    for (int block = 0; block < 8; ++block) {
        for (int i = 0; i < kBlockSize; ++i) {
            left[i] = static_cast<float>((block * kBlockSize + i) % 97) / 97.0f - 0.5f;
            right[i] = left[i];
        }
        float dry[kBlockSize] = {};
        std::memcpy(dry, left, sizeof dry);
        float* buffers[2] = {left, right};
        flat.process(buffers, 2, kBlockSize);
        CHECK(maxDifference(left, dry, kBlockSize) < 1e-6);
    }

    // Low shelf +6 dB at DC is A^2 = 2x; -6 dB is half.
    Eq lowBoost;
    CHECK(lowBoost.setBandType(0, Eq::BandType::LowShelf));
    CHECK(lowBoost.setParam(Eq::kBand0Gain, 6.0));
    const float lowPlus = runConstant(lowBoost, 0.1f, 64);
    checkNear("EQ low shelf +6 dB", lowPlus, 0.1 * 1.9952623149688795, 0.002);

    Eq lowMinusEq;
    CHECK(lowMinusEq.setBandType(0, Eq::BandType::LowShelf));
    CHECK(lowMinusEq.setParam(Eq::kBand0Gain, -6.0));
    const float lowMinus = runConstant(lowMinusEq, 0.1f, 64);
    checkNear("EQ low shelf -6 dB", lowMinus, 0.1 * 0.5011872336272722, 0.002);
    CHECK(lowPlus > lowMinus);

    // High shelf +6 dB at Nyquist (alternating samples) is also A^2.
    Eq highBoost;
    CHECK(highBoost.setBandType(3, Eq::BandType::HighShelf));
    CHECK(highBoost.setParam(Eq::kBand3Gain, 6.0));
    const float highPlus = std::fabs(runAlternating(highBoost, 0.1f, 64));
    checkNear("EQ high shelf +6 dB at Nyquist", highPlus, 0.1 * 1.9952623149688795, 0.002);

    Eq highMinusEq;
    CHECK(highMinusEq.setBandType(3, Eq::BandType::HighShelf));
    CHECK(highMinusEq.setParam(Eq::kBand3Gain, -6.0));
    const float highMinus = std::fabs(runAlternating(highMinusEq, 0.1f, 64));
    checkNear("EQ high shelf -6 dB at Nyquist", highMinus, 0.1 * 0.5011872336272722, 0.002);
    CHECK(highPlus > highMinus);
    checkNear("+6 dB is 2x", lowPlus / 0.1, 2.0, 0.02);

    // Low cut removes DC, high cut removes Nyquist.
    Eq lowCut;
    CHECK(lowCut.setBandType(0, Eq::BandType::LowCut));
    CHECK(lowCut.setParam(Eq::kBand0Freq, 1000.0));
    CHECK(lowCut.setParam(Eq::kBand0Q, 0.707));
    CHECK(std::fabs(runConstant(lowCut, 0.1f, 64)) < 0.001f);

    Eq highCut;
    CHECK(highCut.setBandType(3, Eq::BandType::HighCut));
    CHECK(highCut.setParam(Eq::kBand3Freq, 1000.0));
    CHECK(highCut.setParam(Eq::kBand3Q, 0.707));
    CHECK(std::fabs(runAlternating(highCut, 0.1f, 64)) < 0.001f);

    // Parametric band boosts its centre frequency.
    Eq peak;
    CHECK(peak.setBandType(1, Eq::BandType::Parametric));
    CHECK(peak.setParam(Eq::kBand1Freq, 1000.0));
    CHECK(peak.setParam(Eq::kBand1Q, 1.0));
    CHECK(peak.setParam(Eq::kBand1Gain, 12.0));
    constexpr double kTwoPi = 6.28318530717958647692;
    double phase = 0.0;
    double outputEnergy = 0.0;
    for (int block = 0; block < 200; ++block) {
        float leftBlock[kBlockSize] = {};
        float rightBlock[kBlockSize] = {};
        for (int i = 0; i < kBlockSize; ++i) {
            const float value = static_cast<float>(0.1 * std::sin(kTwoPi * 1000.0 * phase / 48000.0));
            ++phase;
            leftBlock[i] = value;
            rightBlock[i] = value;
        }
        float* buffers[2] = {leftBlock, rightBlock};
        peak.process(buffers, 2, kBlockSize);
        if (block >= 180) {
            for (int i = 0; i < kBlockSize; ++i) outputEnergy += static_cast<double>(leftBlock[i]) * leftBlock[i];
        }
    }
    const double outputRms = std::sqrt(outputEnergy / (20.0 * kBlockSize));
    CHECK(outputRms > 0.2);
    CHECK(outputRms < 0.4);

    // Type select, active-band surface and per-track/main-bus independence.
    Eq track;
    Eq mainBus;
    CHECK(track.setBandType(2, Eq::BandType::HighCut));
    CHECK(track.bandType(2) == Eq::BandType::HighCut);
    CHECK(track.setActiveBand(2));
    CHECK(track.activeBand() == 2);
    checkNear("active band parameter", track.param(Eq::kActiveBand), 2.0, 0.0);
    CHECK(mainBus.bandType(2) == Eq::BandType::Parametric);
    CHECK(track.setBandType(0, Eq::BandType::None));
    CHECK(track.bandType(0) == Eq::BandType::None);
    CHECK(!track.setBandType(4, Eq::BandType::None));
    CHECK(!track.setActiveBand(4));
    CHECK(std::string(Eq::bandTypeName(Eq::BandType::HighCut)) == "High Cut");

    // A boosted band followed by silence settles without NaN or blow-up.
    for (int block = 0; block < 200; ++block) {
        fill(left, kBlockSize, 0.0f);
        fill(right, kBlockSize, 0.0f);
        float* buffers[2] = {left, right};
        lowBoost.process(buffers, 2, kBlockSize);
        CHECK(allFinite(left, kBlockSize));
        CHECK(allFinite(right, kBlockSize));
    }
    CHECK(std::fabs(left[kBlockSize - 1]) < 1e-6f);

    std::printf("PASS fx: 4-band EQ types, shelves/peak/cuts, +6 dB and stability\n");
}

// --------------------------------------------------------------- compressor

void testCompressor() {
    Comp comp;
    CHECK(comp.setParam(Comp::kThresholdDb, -30.0));
    CHECK(comp.setParam(Comp::kRatio, 20.0));
    CHECK(comp.setParam(Comp::kAttackMs, 0.1));
    CHECK(comp.setParam(Comp::kReleaseMs, 100.0));
    CHECK(comp.setParam(Comp::kMakeupDb, 0.0));

    constexpr double kTwoPi = 6.28318530717958647692;
    double phase = 0.0;
    double peakOut = 0.0;
    for (int block = 0; block < 200; ++block) {
        float leftBlock[kBlockSize] = {};
        float rightBlock[kBlockSize] = {};
        for (int i = 0; i < kBlockSize; ++i) {
            const float value = static_cast<float>(0.8 * std::sin(kTwoPi * 1000.0 * phase / 48000.0));
            ++phase;
            leftBlock[i] = value;
            rightBlock[i] = value;
        }
        float* buffers[2] = {leftBlock, rightBlock};
        comp.process(buffers, 2, kBlockSize);
        CHECK(allFinite(leftBlock, kBlockSize));
        if (block >= 190) {
            for (int i = 0; i < kBlockSize; ++i) {
                peakOut = std::max(peakOut, std::fabs(static_cast<double>(leftBlock[i])));
            }
        }
    }
    CHECK(peakOut < 0.2);
    CHECK(peakOut > 0.005);

    // Makeup +6 dB on a signal below threshold is 2x (no gain reduction).
    Comp makeup;
    CHECK(makeup.setParam(Comp::kThresholdDb, -30.0));
    CHECK(makeup.setParam(Comp::kRatio, 20.0));
    CHECK(makeup.setParam(Comp::kAttackMs, 0.1));
    CHECK(makeup.setParam(Comp::kReleaseMs, 100.0));
    CHECK(makeup.setParam(Comp::kMakeupDb, 6.0));
    const float madeUp = runConstant(makeup, 0.01f, 100);
    checkNear("comp makeup +6 dB is 2x", madeUp, 0.019952623149688797, 0.0005);

    // Silence in -> silence out, finite.
    for (int block = 0; block < 100; ++block) {
        float leftBlock[kBlockSize] = {};
        float rightBlock[kBlockSize] = {};
        float* buffers[2] = {leftBlock, rightBlock};
        comp.process(buffers, 2, kBlockSize);
        CHECK(allFinite(leftBlock, kBlockSize));
        for (int i = 0; i < kBlockSize; ++i) CHECK(leftBlock[i] == 0.0f);
    }

    std::printf("PASS fx: compressor peak reduction, makeup and silence\n");
}

// -------------------------------------------------------------------- delay

void testDelay() {
    checkNear("tempo sync 120 bpm eighth", tempoSyncedMs(120.0, 0.5), 250.0, 1e-9);
    checkNear("tempo sync 90 bpm quarter", tempoSyncedMs(90.0, 1.0), 60000.0 / 90.0, 1e-9);
    CHECK(tempoSyncedMs(0.0, 1.0) == 0.0);
    CHECK(tempoSyncedMs(120.0, 0.0) == 0.0);
    CHECK(tempoSyncedMs(std::nan(""), 1.0) == 0.0);
    checkNear("division 1/8 beats", Delay::divisionBeats(Delay::kDiv1_8), 0.5, 0.0);
    checkNear("division 1/4 ms at 120", Delay::divisionMs(120.0, Delay::kDiv1_4), 500.0, 1e-9);
    CHECK(Delay::divisionMs(0.0, Delay::kDiv1_4) == 0.0);
    CHECK(std::string(Delay::divisionName(Delay::kDiv1_4)) == "1/4");

    auto runImpulse = [](Delay& effect, int blocks, std::vector<float>& leftOut, std::vector<float>& rightOut) {
        for (int block = 0; block < blocks; ++block) {
            float left[kBlockSize] = {};
            float right[kBlockSize] = {};
            if (block == 0) {
                left[0] = 1.0f;
                right[0] = 1.0f;
            }
            float* buffers[2] = {left, right};
            effect.process(buffers, 2, kBlockSize);
            for (int i = 0; i < kBlockSize; ++i) {
                leftOut.push_back(left[i]);
                rightOut.push_back(right[i]);
            }
        }
    };

    // Free-run 10 ms at 48 kHz = 480 samples; echo must arrive within one block.
    Delay delay;
    CHECK(delay.setParam(Delay::kBeatMode, 0.0));
    CHECK(delay.setParam(Delay::kTimeMs, 10.0));
    CHECK(delay.setParam(Delay::kFeedback, 0.0));
    CHECK(delay.setParam(Delay::kMix, 1.0));
    CHECK(delay.setTempoSync(120.0, 0.5));
    CHECK(delay.param(Delay::kBeatMode) == 0.0);
    CHECK(delay.param(Delay::kTimeMs) >= 249.0);
    CHECK(delay.param(Delay::kTimeMs) <= 251.0);
    CHECK(delay.setParam(Delay::kTimeMs, 10.0));

    std::vector<float> impulseLeft;
    std::vector<float> impulseRight;
    runImpulse(delay, 16, impulseLeft, impulseRight);
    int found = -1;
    for (int i = 0; i < static_cast<int>(impulseLeft.size()); ++i) {
        if (std::fabs(impulseLeft[static_cast<size_t>(i)]) > 0.5f) {
            found = i;
            break;
        }
    }
    CHECK(found >= 0);
    CHECK(std::abs(found - 480) <= kBlockSize);
    for (int i = 0; i < 480 && i < static_cast<int>(impulseLeft.size()); ++i) {
        CHECK(impulseLeft[static_cast<size_t>(i)] == 0.0f);
    }

    // The feedback path is bandpassed: first echo is full, repeat is filtered.
    Delay feedback;
    CHECK(feedback.setParam(Delay::kTimeMs, 10.0));
    CHECK(feedback.setParam(Delay::kFeedback, 0.5));
    CHECK(feedback.setParam(Delay::kMix, 1.0));
    CHECK(feedback.setParam(Delay::kBpCutoffHz, 1000.0));
    CHECK(feedback.setParam(Delay::kBpWidthOct, 4.0));
    std::vector<float> feedbackLeft;
    std::vector<float> feedbackRight;
    runImpulse(feedback, 16, feedbackLeft, feedbackRight);
    checkNear("delay first echo", feedbackLeft[480], 1.0, 0.01);
    CHECK(feedbackLeft[960] > 0.001f);
    CHECK(feedbackLeft[960] < 0.5f);

    // A narrower feedback bandpass removes more of the repeat.
    Delay narrow;
    CHECK(narrow.setParam(Delay::kTimeMs, 10.0));
    CHECK(narrow.setParam(Delay::kFeedback, 0.5));
    CHECK(narrow.setParam(Delay::kMix, 1.0));
    CHECK(narrow.setParam(Delay::kBpCutoffHz, 1000.0));
    CHECK(narrow.setParam(Delay::kBpWidthOct, 0.5));
    std::vector<float> narrowLeft;
    std::vector<float> narrowRight;
    runImpulse(narrow, 16, narrowLeft, narrowRight);
    CHECK(narrowLeft[960] > 0.0f);
    CHECK(narrowLeft[960] < feedbackLeft[960]);

    // Ping-pong: first echo hard left, second echo hard right.
    Delay pingPong;
    CHECK(pingPong.setParam(Delay::kTimeMs, 10.0));
    CHECK(pingPong.setParam(Delay::kFeedback, 0.8));
    CHECK(pingPong.setParam(Delay::kMix, 1.0));
    CHECK(pingPong.setParam(Delay::kPingPong, 1.0));
    CHECK(pingPong.setParam(Delay::kBpCutoffHz, 4000.0));
    CHECK(pingPong.setParam(Delay::kBpWidthOct, 4.0));
    std::vector<float> pingLeft;
    std::vector<float> pingRight;
    runImpulse(pingPong, 16, pingLeft, pingRight);
    CHECK(std::fabs(pingLeft[480]) > 0.5f);
    CHECK(std::fabs(pingRight[480]) < 0.01f);
    CHECK(pingRight[960] > 0.001f);
    CHECK(std::fabs(pingLeft[960]) < pingRight[960]);

    // Beat mode: 1/8 note at 120 bpm = 250 ms = 12000 samples.
    Delay beat;
    CHECK(beat.setParam(Delay::kBeatMode, 1.0));
    CHECK(beat.setParam(Delay::kDivision, static_cast<double>(Delay::kDiv1_8)));
    CHECK(beat.setParam(Delay::kFeedback, 0.0));
    CHECK(beat.setParam(Delay::kMix, 1.0));
    beat.setBpm(120.0);
    CHECK(beat.bpm() == 120.0);
    std::vector<float> beatLeft;
    std::vector<float> beatRight;
    runImpulse(beat, 100, beatLeft, beatRight);
    int beatFound = -1;
    for (int i = 0; i < static_cast<int>(beatLeft.size()); ++i) {
        if (std::fabs(beatLeft[static_cast<size_t>(i)]) > 0.5f) {
            beatFound = i;
            break;
        }
    }
    CHECK(beatFound >= 0);
    CHECK(std::abs(beatFound - 12000) <= kBlockSize);

    // The external tempo is re-read each block: half tempo doubles the time.
    Delay slowBeat;
    CHECK(slowBeat.setParam(Delay::kBeatMode, 1.0));
    CHECK(slowBeat.setParam(Delay::kDivision, static_cast<double>(Delay::kDiv1_8)));
    CHECK(slowBeat.setParam(Delay::kFeedback, 0.0));
    CHECK(slowBeat.setParam(Delay::kMix, 1.0));
    slowBeat.setBpm(60.0);
    CHECK(slowBeat.bpm() == 60.0);
    slowBeat.setBpm(0.0);
    CHECK(slowBeat.bpm() == 60.0);
    std::vector<float> slowLeft;
    std::vector<float> slowRight;
    runImpulse(slowBeat, 200, slowLeft, slowRight);
    int slowFound = -1;
    for (int i = 0; i < static_cast<int>(slowLeft.size()); ++i) {
        if (std::fabs(slowLeft[static_cast<size_t>(i)]) > 0.5f) {
            slowFound = i;
            break;
        }
    }
    CHECK(slowFound >= 0);
    CHECK(std::abs(slowFound - 24000) <= kBlockSize);

    // Mix 0 is dry only.
    Delay dry;
    CHECK(dry.setParam(Delay::kTimeMs, 10.0));
    CHECK(dry.setParam(Delay::kFeedback, 0.5));
    CHECK(dry.setParam(Delay::kMix, 0.0));
    float leftBlock[kBlockSize] = {};
    float rightBlock[kBlockSize] = {};
    float expected[kBlockSize] = {};
    for (int i = 0; i < kBlockSize; ++i) {
        leftBlock[i] = 0.25f;
        rightBlock[i] = -0.25f;
        expected[i] = 0.25f;
    }
    float* buffers[2] = {leftBlock, rightBlock};
    dry.process(buffers, 2, kBlockSize);
    CHECK(maxDifference(leftBlock, expected, kBlockSize) < 1e-7);
    checkNear("delay mix 0 leaves right dry", rightBlock[0], -0.25, 1e-7);

    // A long feedback tail decays and stays finite.
    Delay tail;
    CHECK(tail.setParam(Delay::kTimeMs, 5.0));
    CHECK(tail.setParam(Delay::kFeedback, 0.9));
    CHECK(tail.setParam(Delay::kMix, 1.0));
    for (int block = 0; block < 300; ++block) {
        float tailLeft[kBlockSize] = {};
        float tailRight[kBlockSize] = {};
        if (block == 0) {
            tailLeft[0] = 1.0f;
            tailRight[0] = 1.0f;
        }
        float* tailBuffers[2] = {tailLeft, tailRight};
        tail.process(tailBuffers, 2, kBlockSize);
        CHECK(allFinite(tailLeft, kBlockSize));
    }

    std::printf("PASS fx: delay free/beat timing, ping-pong, feedback bandpass and dry mix\n");
}

// ------------------------------------------------------------------- reverb

void testReverb() {
    Reverb reverb;
    CHECK(reverb.setParam(Reverb::kPredelayMs, 10.0));
    CHECK(reverb.setParam(Reverb::kDecay, 0.7));
    CHECK(reverb.setParam(Reverb::kDamping, 0.5));
    CHECK(reverb.setParam(Reverb::kMix, 0.5));
    auto processImpulseThenSilence = [](Reverb& effect, int blocks, double& maximum, double& lastBlockPeak) {
        maximum = 0.0;
        lastBlockPeak = 0.0;
        bool finite = true;
        for (int block = 0; block < blocks; ++block) {
            float left[kBlockSize] = {};
            float right[kBlockSize] = {};
            if (block == 0) {
                left[0] = 1.0f;
                right[0] = 1.0f;
            }
            float* buffers[2] = {left, right};
            effect.process(buffers, 2, kBlockSize);
            if (!allFinite(left, kBlockSize) || !allFinite(right, kBlockSize)) finite = false;
            double blockPeak = 0.0;
            for (int i = 0; i < kBlockSize; ++i) {
                maximum = std::max(maximum, std::fabs(static_cast<double>(left[i])));
                blockPeak = std::max(blockPeak, std::fabs(static_cast<double>(left[i])));
            }
            if (block == blocks - 1) lastBlockPeak = blockPeak;
        }
        return finite;
    };

    double maximum = 0.0;
    double lastBlockPeak = 0.0;
    CHECK(processImpulseThenSilence(reverb, 300, maximum, lastBlockPeak));
    CHECK(maximum < 4.0);
    CHECK(lastBlockPeak < 0.01);

    // Mix 0 is dry.
    Reverb dry;
    CHECK(dry.setParam(Reverb::kPredelayMs, 10.0));
    CHECK(dry.setParam(Reverb::kDecay, 0.7));
    CHECK(dry.setParam(Reverb::kDamping, 0.5));
    CHECK(dry.setParam(Reverb::kMix, 0.0));
    float dryLeft[kBlockSize] = {};
    float dryRight[kBlockSize] = {};
    float dryExpected[kBlockSize] = {};
    for (int block = 0; block < 4; ++block) {
        for (int i = 0; i < kBlockSize; ++i) {
            dryLeft[i] = static_cast<float>((block * kBlockSize + i) % 61) / 61.0f - 0.5f;
            dryRight[i] = dryLeft[i];
            dryExpected[i] = dryLeft[i];
        }
        float* buffers[2] = {dryLeft, dryRight};
        dry.process(buffers, 2, kBlockSize);
        CHECK(maxDifference(dryLeft, dryExpected, kBlockSize) < 1e-7);
    }

    // Predelay removes the first 10 ms of wet output.
    Reverb predelayed;
    CHECK(predelayed.setParam(Reverb::kPredelayMs, 10.0));
    CHECK(predelayed.setParam(Reverb::kDecay, 0.7));
    CHECK(predelayed.setParam(Reverb::kDamping, 0.5));
    CHECK(predelayed.setParam(Reverb::kMix, 1.0));
    float preLeft[kBlockSize] = {};
    float preRight[kBlockSize] = {};
    preLeft[0] = 1.0f;
    preRight[0] = 1.0f;
    int checked = 0;
    for (int block = 0; block < 4; ++block) {
        float* buffers[2] = {preLeft, preRight};
        if (block != 0) {
            fill(preLeft, kBlockSize, 0.0f);
            fill(preRight, kBlockSize, 0.0f);
        }
        predelayed.process(buffers, 2, kBlockSize);
        for (int i = 0; i < kBlockSize && checked < 480; ++i, ++checked) {
            CHECK(preLeft[i] == 0.0f);
        }
    }

    // Maximum decay stays bounded over a long silence tail.
    Reverb hot;
    CHECK(hot.setParam(Reverb::kPredelayMs, 0.0));
    CHECK(hot.setParam(Reverb::kDecay, 0.98));
    CHECK(hot.setParam(Reverb::kDamping, 0.2));
    CHECK(hot.setParam(Reverb::kMix, 0.5));
    const auto hotFinite = processImpulseThenSilence(hot, 200, maximum, lastBlockPeak);
    CHECK(hotFinite);
    CHECK(maximum < 4.0);

    std::printf("PASS fx: reverb predelay, decay tail, dry mix and stability\n");
}

// -------------------------------------------------------------- smoothing

void testSmoothing() {
    Eq eq;
    // Settle on DC unity.
    for (int block = 0; block < 16; ++block) (void)runConstant(eq, 1.0f, 1);

    CHECK(eq.setParam(Eq::bandParam(0, Eq::kType), static_cast<double>(Eq::BandType::LowShelf)));
    CHECK(eq.setParam(Eq::bandParam(0, Eq::kGain), 12.0));
    double previous = 1.0;
    double largestStep = 0.0;
    for (int block = 0; block < 200; ++block) {
        const double value = runConstant(eq, 1.0f, 1);
        const double step = std::fabs(value - previous);
        if (step > largestStep) largestStep = step;
        previous = value;
    }
    CHECK(largestStep < 0.6);
    checkNear("smoothed EQ step converges", previous, 3.9810717055349722, 0.01);

    std::printf("PASS fx: parameter step is bounded and converges without clicks\n");
}

// ------------------------------------------------------------ state / JSON

void testState() {
    Registry registry;
    for (const EffectInfo& info : registry.list()) {
        std::unique_ptr<IEffect> effect = registry.create(info.id);
        CHECK(effect != nullptr);
        if (effect == nullptr) continue;
        auto* base = dynamic_cast<Effect*>(effect.get());
        CHECK(base != nullptr);
        if (base == nullptr) continue;

        for (int i = 0; i < static_cast<int>(base->params().size()); ++i) {
            const Param& param = base->params()[static_cast<size_t>(i)];
            const double value = param.def + 0.25 * (param.max - param.def);
            CHECK(base->setParam(i, value));
        }
        base->setBypass(true);
        const EffectState saved = base->state();
        const std::string json = saved.toJson();

        CHECK(json.find("\"id\":\"" + info.id + "\"") != std::string::npos);
        CHECK(json.find("\"v\":" + std::to_string(saved.version)) != std::string::npos);
        CHECK(json.find("\"bypass\":true") != std::string::npos);
        CHECK(json.find("\"params\":[") != std::string::npos);
        CHECK(json.find(' ') == std::string::npos);

        EffectState parsed;
        std::string parseError;
        CHECK(EffectState::fromJson(json, parsed, &parseError));
        CHECK(parsed.id == saved.id);
        CHECK(parsed.version == saved.version);
        CHECK(parsed.bypass == saved.bypass);
        CHECK(parsed.params.size() == saved.params.size());
        for (size_t i = 0; i < parsed.params.size() && i < saved.params.size(); ++i) {
            checkNear("state param round-trip", parsed.params[i], saved.params[i], 1e-12);
        }

        std::unique_ptr<IEffect> restoredEffect = registry.create(info.id);
        CHECK(restoredEffect != nullptr);
        auto* restored = dynamic_cast<Effect*>(restoredEffect.get());
        CHECK(restored != nullptr);
        if (restored != nullptr) {
            std::string applyError;
            CHECK(restored->applyState(parsed, &applyError));
            CHECK(restored->state().toJson() == json);
        }

        // Version mismatch is rejected.
        std::unique_ptr<IEffect> versionVictim = registry.create(info.id);
        auto* versionBase = dynamic_cast<Effect*>(versionVictim.get());
        CHECK(versionBase != nullptr);
        if (versionBase != nullptr) {
            EffectState bad = parsed;
            bad.version = parsed.version + 1;
            std::string error;
            CHECK(!versionBase->applyState(bad, &error));
            CHECK(!error.empty());
        }

        // Parameter-count mismatch is rejected.
        std::unique_ptr<IEffect> countVictim = registry.create(info.id);
        auto* countBase = dynamic_cast<Effect*>(countVictim.get());
        CHECK(countBase != nullptr);
        if (countBase != nullptr) {
            EffectState bad = parsed;
            bad.params.pop_back();
            std::string error;
            CHECK(!countBase->applyState(bad, &error));
            CHECK(!error.empty());
        }

        // Effect-id mismatch is rejected.
        std::unique_ptr<IEffect> idVictim = registry.create(info.id);
        auto* idBase = dynamic_cast<Effect*>(idVictim.get());
        CHECK(idBase != nullptr);
        if (idBase != nullptr) {
            EffectState bad = parsed;
            bad.id = "other";
            std::string error;
            CHECK(!idBase->applyState(bad, &error));
            CHECK(!error.empty());
        }
    }

    EffectState junk;
    CHECK(!EffectState::fromJson("not json", junk));
    CHECK(!EffectState::fromJson("{\"id\":\"eq\",\"v\":1,\"bypass\":false}", junk));
    CHECK(!EffectState::fromJson("{\"id\":\"eq\",\"v\":1,\"bypass\":false,\"params\":[0,0,0,0,0],\"extra\":1}",
                                 junk));
    CHECK(!EffectState::fromJson("{\"id\":\"eq\",\"v\":1,\"bypass\":false,\"params\":[0,0,0,0,1e999]}", junk));
    CHECK(!EffectState::fromJson("{\"id\":\"eq\",\"v\":1,\"bypass\":false,\"params\":[0,0,0,0,0],}", junk));

    // Reordered keys and whitespace are accepted.
    EffectState reordered;
    CHECK(EffectState::fromJson(
        " { \"params\" : [ 0, 0, 1000, 0.7, 0 ], \"bypass\" : false, \"v\" : 1, \"id\" : \"eq\" } ", reordered));
    CHECK(reordered.id == "eq");
    CHECK(reordered.params.size() == 5u);

    std::printf("PASS fx: state JSON round-trip and mismatch rejection\n");
}

// --------------------------------------------------------- engine integration

void testEngineMute() {
    Engine engine;
    Registry registry;
    std::unique_ptr<IEffect> effect = registry.create("eq");
    auto* eq = dynamic_cast<Eq*>(effect.get());
    CHECK(eq != nullptr);
    if (eq == nullptr) return;
    CHECK(eq->setParam(Eq::bandParam(0, Eq::kType), static_cast<double>(Eq::BandType::LowShelf)));
    CHECK(eq->setParam(Eq::bandParam(0, Eq::kGain), 6.0));

    engine.setFx(0, eq);
    Channel strip;
    strip.gain = 1.0f;
    strip.pan = 0.5f;
    strip.send[0] = 1.0f;
    engine.setChannel(0, strip);

    float input[kBlockSize] = {};
    fill(input, kBlockSize, 1.0f);
    const float* inputs[kMaxInputs] = {input};
    float outLeft[kBlockSize] = {};
    float outRight[kBlockSize] = {};
    engine.process(inputs, 1, kBlockSize, outLeft, outRight);
    CHECK(std::fabs(outLeft[0]) > 0.0f);

    strip.mute = true;
    engine.setChannel(0, strip);
    // Clear the EQ's filter tail so this check isolates the engine's mute
    // routing: a post-fader send must receive exact zeros, not a dying tail.
    eq->reset();
    engine.process(inputs, 1, kBlockSize, outLeft, outRight);
    for (int i = 0; i < kBlockSize; ++i) {
        CHECK(outLeft[i] == 0.0f);
        CHECK(outRight[i] == 0.0f);
    }

    std::printf("PASS fx: muted strip feeds neither dry nor FX return (mute == 0)\n");
}

// ------------------------------------------------- engine sample rate

void testEngineSampleRate() {
    // Time-based FX must track the engine rate: a 10 ms delay echoes at 480
    // samples @48 kHz and at 441 samples @44.1 kHz. Without forwarding, the
    // delay line stays at its default allocation and the echo never moves.
    Engine engine;
    Delay delay;
    CHECK(delay.setParam(Delay::kBeatMode, 0.0));
    CHECK(delay.setParam(Delay::kTimeMs, 10.0));
    CHECK(delay.setParam(Delay::kFeedback, 0.0));
    CHECK(delay.setParam(Delay::kMix, 1.0));
    engine.setFx(0, &delay);
    Channel strip;
    strip.gain = 1.0f;
    strip.send[0] = 1.0f;
    engine.setChannel(0, strip);

    // Returns the first echo position, skipping the dry impulse at index 0
    // (the channel passes dry to master while the send bus carries wet).
    auto echoAt = [&](double rate) {
        engine.setSampleRate(rate);
        int found = -1;
        int position = 0;
        for (int block = 0; block < 16; ++block) {
            float left[kBlockSize] = {};
            float right[kBlockSize] = {};
            if (block == 0) {
                left[0] = 1.0f;
                right[0] = 1.0f;
            }
            float* buffers[2] = {left, right};
            float outL[kBlockSize] = {};
            float outR[kBlockSize] = {};
            engine.process(buffers, 1, kBlockSize, outL, outR);
            for (int i = 0; i < kBlockSize; ++i) {
                if (position + i > 0 && std::fabs(outL[i]) > 0.5f && found < 0) {
                    found = position + i;
                }
            }
            position += kBlockSize;
        }
        return found;
    };

    const int at48k = echoAt(48000.0);
    CHECK(std::abs(at48k - 480) <= kBlockSize);
    const int at441k = echoAt(44100.0);
    CHECK(std::abs(at441k - 441) <= kBlockSize);

    std::printf("PASS fx: engine sample rate reaches time-based FX\n");
}

}  // namespace

int main() {
    testRamp();
    testRegistry();
    testBypass();
    testEq();
    testCompressor();
    testDelay();
    testReverb();
    testSmoothing();
    testState();
    testEngineMute();
    testEngineSampleRate();

    if (gFailures != 0) {
        std::printf("FAIL fx: %d check(s) failed\n", gFailures);
        return 1;
    }
    std::printf("PASS\n");
    return 0;
}
