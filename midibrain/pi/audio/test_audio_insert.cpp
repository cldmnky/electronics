// Assert-style acceptance tests for the per-track FX insert (engine wave 9):
// one built-in delay/reverb instance per track with its own settings, placed
// post-EQ, pre-fader and pre-sends, plus the external (CLAP) seam.
//
// Build (from midibrain/pi/audio), one command:
//   /usr/bin/c++ -std=c++20 -Wall -Wextra -Werror -I. test_audio_insert.cpp
//     engine.cpp take_player.cpp click.cpp fx/effect.cpp fx/params.cpp
//     fx/delay.cpp fx/reverb.cpp -pthread -o /tmp/t_audio_insert &&
//     /tmp/t_audio_insert
//
// Every check injects a known signal and compares the engine output, so the
// placement claims are proven by the audio path rather than by reading the
// source. The CPU probe prints numbers only: a measurement on this host is not
// a Pi 5 timing claim.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>

#include "engine.hpp"
#include "fx/delay.hpp"
#include "fx/reverb.hpp"
#include "recorder.hpp"

namespace {

using namespace mnky::audio;

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
    std::printf("FAIL %s: actual %.12g expected %.12g (eps %.3g)\n", what,
                actual, expected, eps);
    ++gFailures;
}

void checkIdentical(const char* what, const std::vector<float>& actual,
                    const std::vector<float>& expected) {
    if (actual.size() == expected.size() &&
        (actual.empty() ||
         std::memcmp(actual.data(), expected.data(),
                     actual.size() * sizeof(float)) == 0)) {
        return;
    }
    std::printf("FAIL %s: buffers differ (actual %zu frames, expected %zu)\n",
                what, actual.size(), expected.size());
    ++gFailures;
}

// --------------------------------------------------------------- fixtures

// Stable parameter vectors in the documented index order.
TrackInsert delayInsert(double timeMs, double feedback, double mix) {
    TrackInsert insert;
    insert.id = "builtin.delay";
    insert.params = {timeMs, feedback, mix, 0.0, 3.0, 0.0, 20000.0, 0.1};
    return insert;
}

TrackInsert reverbInsert(double decay, double mix) {
    TrackInsert insert;
    insert.id = "builtin.reverb";
    insert.params = {0.0, decay, 0.5, mix, 0.0, 1.0};
    return insert;
}

// Hard-left pan makes gainL exactly 1, so Main L is the strip signal at
// unity: gain -> EQ -> insert -> vol -> pan.
void configureStrip(Engine& engine, int track,
                    OutputDestination destination = OutputDestination::Main) {
    Channel channel;
    channel.gain = 1.0f;
    channel.vol = 1.0f;
    channel.pan = 0.0f;
    channel.monitor = MonitorMode::On;
    engine.setChannel(track, channel);
    // Tracks past the device width wrap onto a real device input, so a
    // 64-track CPU probe has every track active.
    engine.setInputSource(track, track % kMaxDeviceInputs);
    engine.setInputPair(track, false);
    engine.setOutputDestination(track, destination);
}

class Noise {
public:
    explicit Noise(std::uint32_t seed = 1u) : mState(seed) {}

    float next() {
        mState = mState * 1664525u + 1013904223u;
        return static_cast<float>(static_cast<double>(mState >> 8) / 8388608.0 -
                                  1.0);
    }

private:
    std::uint32_t mState;
};

std::vector<float> noiseBuffer(std::size_t frames, float gain = 1.0f,
                               std::uint32_t seed = 1u) {
    Noise noise(seed);
    std::vector<float> buffer(frames);
    for (float& value : buffer) value = gain * noise.next();
    return buffer;
}

// Every engine bus for one render, so a test can read Main, CUE/phones and
// OUT3&4 from the same take.
struct Capture {
    std::vector<float> mainL, mainR, phonesL, phonesR, out34L, out34R;
};

// Renders `inputs[track]` into strip `track` and returns every output bus.
// All inputs must be the same length; the block size is the engine's maximum
// unless a test asks for a smaller one.
Capture render(Engine& engine, const std::vector<const std::vector<float>*>& inputs,
               int blockFrames = kBlockSize) {
    std::size_t frames = 0;
    for (const std::vector<float>* input : inputs) {
        if (input != nullptr) {
            frames = input->size();
            break;
        }
    }
    Capture capture;
    capture.mainL.assign(frames, 0.0f);
    capture.mainR.assign(frames, 0.0f);
    capture.phonesL.assign(frames, 0.0f);
    capture.phonesR.assign(frames, 0.0f);
    capture.out34L.assign(frames, 0.0f);
    capture.out34R.assign(frames, 0.0f);
    for (std::size_t offset = 0; offset < frames; offset += blockFrames) {
        const int n =
            static_cast<int>(std::min<std::size_t>(blockFrames, frames - offset));
        const float* pointers[kMaxInputs] = {};
        for (std::size_t index = 0; index < inputs.size(); ++index) {
            if (inputs[index] != nullptr) {
                pointers[index] = inputs[index]->data() + offset;
            }
        }
        OutputBuses buses;
        buses.mainL = capture.mainL.data() + offset;
        buses.mainR = capture.mainR.data() + offset;
        buses.phonesL = capture.phonesL.data() + offset;
        buses.phonesR = capture.phonesR.data() + offset;
        buses.out34L = capture.out34L.data() + offset;
        buses.out34R = capture.out34R.data() + offset;
        engine.process(pointers, static_cast<int>(inputs.size()), n, buses);
    }
    return capture;
}

// Records every block it is handed. Used as the per-track insert (the external
// CLAP seam) and as a send-bus effect (setFx), so a test can read exactly what
// signal reaches that stage.
class ProbeEffect final : public IEffect {
public:
    void process(float* const* ch, int nch, int n) override {
        ++calls;
        if (ch == nullptr || nch <= 0 || n <= 0) return;
        left.insert(left.end(), ch[0], ch[0] + n);
        if (nch > 1 && ch[1] != nullptr) {
            right.insert(right.end(), ch[1], ch[1] + n);
        }
    }
    const char* name() const override { return "insert-probe"; }

    int calls = 0;
    std::vector<float> left;
    std::vector<float> right;
};

// External-seam stand-in for a CLAP insert: a known gain, in place.
class GainEffect final : public IEffect {
public:
    explicit GainEffect(float gain) : mGain(gain) {}

    void process(float* const* ch, int nch, int n) override {
        ++calls;
        if (ch == nullptr) return;
        for (int channel = 0; channel < nch; ++channel) {
            if (ch[channel] == nullptr) continue;
            for (int i = 0; i < n; ++i) ch[channel][i] *= mGain;
        }
    }
    const char* name() const override { return "gain-probe"; }

    int calls = 0;

private:
    float mGain;
};

// -------------------------------------------------- built-in insert path

void testBuiltinIds() {
    // Delay: mix = 1, feedback = 0, 1 ms -> the impulse comes back 48 frames
    // later and the dry copy is gone.
    {
        Engine engine;
        configureStrip(engine, 0);
        engine.setTrackInsert(0, delayInsert(1.0, 0.0, 1.0));
        std::vector<float> input(kBlockSize * 2, 0.0f);
        input[0] = 1.0f;
        const std::vector<const std::vector<float>*> inputs{&input};
        const Capture out = render(engine, inputs);
        checkNear("delay: no dry at frame 0", out.mainL[0], 0.0, 1e-6);
        checkNear("delay: echo at 48 frames (1 ms)", out.mainL[48], 1.0, 1e-6);
        checkNear("delay: silence after the echo", out.mainL[49], 0.0, 1e-6);
        checkNear("delay: no right output at hard left", out.mainR[48], 0.0,
                  1e-6);
    }
    // Reverb: wet only (mix = 1) -> no dry impulse, a decaying tail. The
    // shortest comb is ~25 ms, so render well past it.
    {
        Engine engine;
        configureStrip(engine, 0);
        engine.setTrackInsert(0, reverbInsert(0.7, 1.0));
        std::vector<float> input(kBlockSize * 32, 0.0f);
        input[0] = 1.0f;
        const std::vector<const std::vector<float>*> inputs{&input};
        const Capture out = render(engine, inputs);
        CHECK(std::fabs(out.mainL[0]) < 0.5f);
        double tail = 0.0;
        for (std::size_t i = 1; i < out.mainL.size(); ++i) {
            tail += std::fabs(out.mainL[i]);
        }
        CHECK(tail > 0.01);
    }
    // No insert at all: the strip is the dry reference.
    {
        Engine engine;
        configureStrip(engine, 0);
        std::vector<float> input(kBlockSize * 2, 0.0f);
        input[0] = 1.0f;
        const std::vector<const std::vector<float>*> inputs{&input};
        const Capture out = render(engine, inputs);
        checkNear("dry: impulse at frame 0", out.mainL[0], 1.0, 1e-6);
        checkNear("dry: no echo", out.mainL[48], 0.0, 1e-6);
    }
}

void testPerTrackInstances() {
    // Two tracks, two delay instances with different times, on different
    // output destinations so each can be read on its own.
    Engine engine;
    configureStrip(engine, 0, OutputDestination::Main);
    configureStrip(engine, 1, OutputDestination::Out34);
    engine.setTrackInsert(0, delayInsert(1.0, 0.0, 1.0));   // 48 frames
    engine.setTrackInsert(1, delayInsert(2.0, 0.0, 1.0));   // 96 frames
    std::vector<float> input0(kBlockSize * 2, 0.0f);
    std::vector<float> input1(kBlockSize * 2, 0.0f);
    input0[0] = 1.0f;
    input1[0] = 1.0f;
    const std::vector<const std::vector<float>*> inputs{&input0, &input1};
    const Capture out = render(engine, inputs);
    checkNear("track 0 delay: echo at 48", out.mainL[48], 1.0, 1e-6);
    checkNear("track 0 delay: nothing at 96", out.mainL[96], 0.0, 1e-6);
    checkNear("track 1 delay: echo at 96", out.out34L[96], 1.0, 1e-6);
    checkNear("track 1 delay: nothing at 48", out.out34L[48], 0.0, 1e-6);
}

void testBypassBitTransparent() {
    // A bypassed insert is a hard dry switch: bit-identical to an engine that
    // never had an insert, whatever the instance's internal state.
    const std::vector<float> input = noiseBuffer(kBlockSize * 4, 0.25f);
    const std::vector<const std::vector<float>*> inputs{&input};
    {
        Engine dry;
        configureStrip(dry, 0);
        const Capture reference = render(dry, inputs);

        Engine engine;
        configureStrip(engine, 0);
        TrackInsert insert = delayInsert(1.0, 0.5, 1.0);
        insert.bypass = true;
        engine.setTrackInsert(0, insert);
        const Capture out = render(engine, inputs);
        checkIdentical("delay bypass is bit-transparent", out.mainL,
                       reference.mainL);
        checkIdentical("delay bypass is bit-transparent (right)", out.mainR,
                       reference.mainR);
    }
    {
        Engine dry;
        configureStrip(dry, 0);
        const Capture reference = render(dry, inputs);

        Engine engine;
        configureStrip(engine, 0);
        TrackInsert insert = reverbInsert(0.7, 1.0);
        insert.bypass = true;
        engine.setTrackInsert(0, insert);
        const Capture out = render(engine, inputs);
        checkIdentical("reverb bypass is bit-transparent", out.mainL,
                       reference.mainL);
    }
    // Bypass after the insert has been running: the next blocks still match a
    // fresh dry engine fed only those blocks, i.e. the insert does not leak
    // state into the output while bypassed.
    {
        const std::vector<float> first = noiseBuffer(kBlockSize * 2, 0.25f, 7u);
        const std::vector<float> second = noiseBuffer(kBlockSize * 2, 0.25f, 9u);
        Engine engine;
        configureStrip(engine, 0);
        engine.setTrackInsert(0, delayInsert(1.0, 0.7, 1.0));
        const std::vector<const std::vector<float>*> firstInputs{&first};
        (void)render(engine, firstInputs);
        TrackInsert insert = delayInsert(1.0, 0.7, 1.0);
        insert.bypass = true;
        engine.setTrackInsert(0, insert);
        const std::vector<const std::vector<float>*> secondInputs{&second};
        const Capture out = render(engine, secondInputs);

        Engine dry;
        configureStrip(dry, 0);
        const Capture reference = render(dry, secondInputs);
        checkIdentical("mid-stream bypass is bit-transparent", out.mainL,
                       reference.mainL);
    }
}

void testParamsHotApply() {
    // (a) A parameter write must not clear the delay line. The impulse is fed
    // with mix = 0 and the echo arrives only after mix has been raised; a
    // reset would erase it (exactly 0), a live instance ramps and keeps it.
    // The first render consumed 16 frames, so capture index 32 is the echo's
    // absolute frame 48.
    {
        Engine engine;
        configureStrip(engine, 0);
        engine.setTrackInsert(0, delayInsert(1.0, 0.5, 0.0));
        std::vector<float> input(16, 0.0f);
        input[0] = 1.0f;
        const std::vector<const std::vector<float>*> inputs{&input};
        (void)render(engine, inputs);
        engine.setTrackInsert(0, delayInsert(1.0, 0.5, 1.0));
        std::vector<float> silence(kBlockSize, 0.0f);
        const std::vector<const std::vector<float>*> after{&silence};
        const Capture out = render(engine, after);
        CHECK(out.mainL[32] > 1e-3);   // the pre-write impulse survived
        CHECK(out.mainL[32] < 0.2);    // still on the 20 ms mix ramp
    }
    // (b) Hot apply ramps instead of snapping. A 96-frame (2 ms) delay inverts
    // a 192-frame square wave, so mix = 0 and mix = 1 give opposite signs: the
    // first block after the write must still be mostly dry, and once the ramp
    // has run the output must be the inverted echo.
    {
        Engine engine;
        configureStrip(engine, 0);
        engine.setTrackInsert(0, delayInsert(2.0, 0.0, 0.0));
        const auto square = [](std::size_t frames) {
            std::vector<float> buffer(frames);
            for (std::size_t i = 0; i < frames; ++i) {
                buffer[i] = ((i / 96) % 2 == 0) ? 0.5f : -0.5f;
            }
            return buffer;
        };
        std::vector<float> input = square(kBlockSize * 2);
        const std::vector<const std::vector<float>*> inputs{&input};
        (void)render(engine, inputs);
        engine.setTrackInsert(0, delayInsert(2.0, 0.0, 1.0));
        std::vector<float> after = square(kBlockSize);
        const std::vector<const std::vector<float>*> afterInputs{&after};
        const Capture out = render(engine, afterInputs);
        CHECK(out.mainL[0] > 0.3f);  // mostly dry: the ramp is running
        // After the 20 ms ramp the wet (inverted) signal owns the output.
        std::vector<float> settled = square(kBlockSize * 10);
        const std::vector<const std::vector<float>*> settledInputs{&settled};
        const Capture later = render(engine, settledInputs);
        const float dry = settled[settled.size() - 1];
        checkNear("settled wet is the inverted echo",
                  later.mainL[settled.size() - 1], -dry, 0.05);
    }
    // (c) The applied state is what capture returns: the effect clamps out of
    // range values, and the stored vector has the effect's parameter count.
    {
        Engine engine;
        configureStrip(engine, 0);
        TrackInsert insert = delayInsert(1.0, 0.0, 0.0);
        insert.params[2] = 9.0;   // mix clamps to 1
        insert.params[3] = 0.4;   // stepped beat_mode rounds to 0
        engine.setTrackInsert(0, insert);
        const TrackInsert applied = engine.trackInsert(0);
        CHECK(applied.id == "builtin.delay");
        CHECK(applied.params.size() == 8);
        checkNear("capture: mix clamped", applied.params[2], 1.0, 1e-9);
        checkNear("capture: stepped value rounded", applied.params[3], 0.0,
                  1e-9);
    }
}

void testIdChangeReinitializes() {
    // Track 0 switches id and comes back to the delay: the old tail must be
    // gone. Track 1 keeps running untouched.
    std::vector<float> impulse(kBlockSize * 2, 0.0f);
    impulse[0] = 1.0f;
    std::vector<float> silence(kBlockSize * 2, 0.0f);

    Engine engine;
    configureStrip(engine, 0, OutputDestination::Main);
    configureStrip(engine, 1, OutputDestination::Out34);
    engine.setTrackInsert(0, delayInsert(1.0, 0.6, 1.0));
    engine.setTrackInsert(1, delayInsert(1.0, 0.6, 1.0));
    const std::vector<const std::vector<float>*> impulses{&impulse, &impulse};
    (void)render(engine, impulses);

    engine.setTrackInsert(0, reverbInsert(0.7, 1.0));
    engine.setTrackInsert(0, delayInsert(1.0, 0.0, 1.0));
    const std::vector<const std::vector<float>*> tails{&silence, &silence};
    const Capture out = render(engine, tails);

    double mainEnergy = 0.0;
    for (const float value : out.mainL) mainEnergy += std::fabs(value);
    checkNear("id change clears the track's insert tail", mainEnergy, 0.0,
              1e-9);

    // The reference track never changed id, so its tail is bit-identical.
    Engine reference;
    configureStrip(reference, 0, OutputDestination::Main);
    configureStrip(reference, 1, OutputDestination::Out34);
    reference.setTrackInsert(0, delayInsert(1.0, 0.6, 1.0));
    reference.setTrackInsert(1, delayInsert(1.0, 0.6, 1.0));
    (void)render(reference, impulses);
    const Capture expected = render(reference, tails);
    checkIdentical("id change leaves other tracks untouched", out.out34L,
                   expected.out34L);
}

// ------------------------------------------------------- placement proofs

void testPlacementPostEq() {
    // EQ +6 dB low shelf at 120 Hz on a DC input: the insert must see the
    // boosted signal, so the probe input equals the EQ-only engine output.
    EqBand shelf;
    shelf.type = 2;  // low shelf
    shelf.freqHz = 120.0f;
    shelf.gainDb = 6.0f;
    shelf.q = 0.707f;

    std::vector<float> input(kBlockSize * 64, 0.1f);
    const std::vector<const std::vector<float>*> inputs{&input};

    Engine reference;
    configureStrip(reference, 0);
    reference.setTrackEqBand(0, 0, shelf);
    const Capture referenceOut = render(reference, inputs);

    Engine engine;
    configureStrip(engine, 0);
    engine.setTrackEqBand(0, 0, shelf);
    ProbeEffect probe;
    engine.setTrackInsertEffect(0, &probe);
    TrackInsert insert;
    insert.id = "test.probe";
    engine.setTrackInsert(0, insert);
    const Capture out = render(engine, inputs);

    CHECK(probe.calls > 0);
    CHECK(!probe.left.empty());
    checkIdentical("insert sees the post-EQ signal", probe.left,
                   referenceOut.mainL);
    checkNear("EQ boost is upstream of the insert",
              probe.left[probe.left.size() - 1], 0.1995, 0.002);
    checkIdentical("pass-through insert leaves the strip unchanged", out.mainL,
                   referenceOut.mainL);
}

void testPlacementPreFader() {
    // The insert input must not follow the fader: vol = 0.25 scales the output
    // but leaves the probe input identical to the vol = 1 take.
    std::vector<float> input = noiseBuffer(kBlockSize, 0.25f);
    const std::vector<const std::vector<float>*> inputs{&input};

    ProbeEffect probeFull;
    Engine full;
    configureStrip(full, 0);
    full.setTrackInsertEffect(0, &probeFull);
    TrackInsert insert;
    insert.id = "test.probe";
    full.setTrackInsert(0, insert);
    const Capture fullOut = render(full, inputs);

    ProbeEffect probeQuiet;
    Engine quiet;
    configureStrip(quiet, 0);
    Channel channel = quiet.channel(0);
    channel.vol = 0.25f;
    quiet.setChannel(0, channel);
    quiet.setTrackInsertEffect(0, &probeQuiet);
    quiet.setTrackInsert(0, insert);
    const Capture quietOut = render(quiet, inputs);

    checkIdentical("fader does not reach the insert input", probeQuiet.left,
                   probeFull.left);
    std::vector<float> expected = probeFull.left;
    for (float& value : expected) value *= 0.25f;
    checkIdentical("fader scales the insert output", quietOut.mainL, expected);
    checkIdentical("fader does not touch the insert take", fullOut.mainL,
                   probeFull.left);
}

void testPlacementPreSends() {
    // A 1 ms wet-only delay on an impulse: every send stage (FX1 pre and post
    // fader, CUE, OUT3&4) must carry the delayed signal, never the dry one.
    ProbeEffect fx1;
    Engine engine;
    configureStrip(engine, 0);
    Channel channel = engine.channel(0);
    channel.send[0] = 1.0f;  // FX1
    channel.cueSend = 1.0f;
    channel.out34Send = 1.0f;
    engine.setChannel(0, channel);
    engine.setFx(0, &fx1);
    engine.setFxReturn(0, 0.0f);  // keep the return out of the Main check
    engine.setTrackInsert(0, delayInsert(1.0, 0.0, 1.0));

    std::vector<float> input(kBlockSize * 2, 0.0f);
    input[0] = 1.0f;
    const std::vector<const std::vector<float>*> inputs{&input};
    const Capture out = render(engine, inputs);

    CHECK(!fx1.left.empty());
    checkNear("post-fader FX1 send sees the insert at 48", fx1.left[48], 1.0,
              1e-6);
    checkNear("post-fader FX1 send has no dry at 0", fx1.left[0], 0.0, 1e-6);
    checkNear("CUE send sees the insert at 48", out.phonesL[48], 1.0, 1e-6);
    checkNear("CUE send has no dry at 0", out.phonesL[0], 0.0, 1e-6);
    checkNear("OUT3&4 send sees the insert at 48", out.out34L[48], 1.0, 1e-6);
    checkNear("OUT3&4 send has no dry at 0", out.out34L[0], 0.0, 1e-6);
    checkNear("dry destination sees the insert at 48", out.mainL[48], 1.0,
              1e-6);

    // Pre-fader FX1 is upstream of the fader but still downstream of the
    // insert: the delayed impulse is there and the dry one is not.
    ProbeEffect fx1Pre;
    Engine pre;
    configureStrip(pre, 0);
    Channel preChannel = pre.channel(0);
    preChannel.send[0] = 1.0f;
    pre.setChannel(0, preChannel);
    pre.setSendSource(SendDestination::Fx1, SendSource::PreFader);
    pre.setFx(0, &fx1Pre);
    pre.setFxReturn(0, 0.0f);
    pre.setTrackInsert(0, delayInsert(1.0, 0.0, 1.0));
    (void)render(pre, inputs);
    CHECK(!fx1Pre.left.empty());
    checkNear("pre-fader FX1 send sees the insert at 48", fx1Pre.left[48], 1.0,
              1e-6);
    checkNear("pre-fader FX1 send has no dry at 0", fx1Pre.left[0], 0.0, 1e-6);
    checkNear("pre-fader FX1 send is mono-centred", fx1Pre.right[48], 1.0,
              1e-6);
}

void testTapStaysPreInsert() {
    // The recorder tap is documented pre-insert (the tapPoint policy): a wet
    // insert must not change it.
    Engine engine;
    configureStrip(engine, 0);
    engine.setTrackInsert(0, delayInsert(1.0, 0.0, 1.0));
    std::vector<float> input(kBlockSize, 0.0f);
    input[0] = 1.0f;
    const std::vector<const std::vector<float>*> inputs{&input};
    (void)render(engine, inputs);
    const float* tap = engine.tap(0);
    CHECK(tap != nullptr);
    checkNear("tap keeps the dry impulse at 0", tap[0], 1.0, 1e-6);
    checkNear("tap has no insert echo at 48", tap[48], 0.0, 1e-6);
}

// ------------------------------------------------ empty id / external seam

void testEmptyIdZeroCost() {
    const std::vector<float> input = noiseBuffer(kBlockSize * 4, 0.25f);
    const std::vector<const std::vector<float>*> inputs{&input};

    Engine reference;
    configureStrip(reference, 0);
    const Capture dry = render(reference, inputs);

    // An attached external effect with no insert id must never be called: the
    // engine skips the insert entirely rather than running a no-op.
    Engine engine;
    configureStrip(engine, 0);
    ProbeEffect probe;
    engine.setTrackInsertEffect(0, &probe);
    const Capture out = render(engine, inputs);
    CHECK(probe.calls == 0);
    checkIdentical("empty id is bit-transparent", out.mainL, dry.mainL);
    CHECK(engine.trackInsert(0).id.empty());
    CHECK(engine.trackInsert(0).params.empty());

    // A built-in id runs the built-in instance, not the attached effect.
    engine.setTrackInsert(0, delayInsert(1.0, 0.0, 1.0));
    const Capture wet = render(engine, inputs);
    CHECK(probe.calls == 0);
    CHECK(wet.mainL != dry.mainL);

    // Clearing the id detaches again: no DSP and no external call.
    engine.setTrackInsert(0, TrackInsert{});
    const Capture cleared = render(engine, inputs);
    CHECK(probe.calls == 0);
    checkIdentical("cleared id is bit-transparent", cleared.mainL, dry.mainL);
    CHECK(engine.trackInsert(0).id.empty());
}

void testExternalSeam() {
    const std::vector<float> input = noiseBuffer(kBlockSize * 3, 0.25f);
    const std::vector<const std::vector<float>*> inputs{&input};

    Engine dryEngine;
    configureStrip(dryEngine, 0);
    const Capture dry = render(dryEngine, inputs);

    Engine engine;
    configureStrip(engine, 0);
    GainEffect gain(0.5f);
    engine.setTrackInsertEffect(0, &gain);
    TrackInsert insert;
    insert.id = "clap.mnkygain";
    insert.params = {0.5};
    engine.setTrackInsert(0, insert);
    const Capture out = render(engine, inputs);
    CHECK(gain.calls > 0);
    std::vector<float> expected = dry.mainL;
    for (float& value : expected) value *= 0.5f;
    checkIdentical("external insert runs in place", out.mainL, expected);
    // The engine echoes the CLAP parameters it cannot map itself.
    CHECK(engine.trackInsert(0).id == "clap.mnkygain");
    CHECK(engine.trackInsert(0).params.size() == 1);

    // No attached effect for an unknown id: dry, not a crash or a partial
    // block. This is the documented per-track CLAP gap.
    Engine unattached;
    configureStrip(unattached, 0);
    unattached.setTrackInsert(0, insert);
    const Capture outUnattached = render(unattached, inputs);
    checkIdentical("unattached CLAP id passes the strip dry", outUnattached.mainL,
                   dry.mainL);

    // Engine-level bypass wins over the attached effect.
    const int callsBefore = gain.calls;
    insert.bypass = true;
    engine.setTrackInsert(0, insert);
    const Capture bypassed = render(engine, inputs);
    CHECK(gain.calls == callsBefore);
    checkIdentical("bypass skips the external insert", bypassed.mainL, dry.mainL);
}

void testSampleRate() {
    // An insert created after a rate change allocates at the new rate.
    {
        Engine engine;
        engine.setSampleRate(96000.0);
        configureStrip(engine, 0);
        engine.setTrackInsert(0, delayInsert(1.0, 0.0, 1.0));
        std::vector<float> input(kBlockSize * 2, 0.0f);
        input[0] = 1.0f;
        const std::vector<const std::vector<float>*> inputs{&input};
        const Capture out = render(engine, inputs);
        checkNear("96 kHz insert: echo at 96 frames", out.mainL[96], 1.0, 1e-6);
        checkNear("96 kHz insert: nothing at 48 frames", out.mainL[48], 0.0,
                  1e-6);
    }
    // An existing insert is re-allocated by Engine::setSampleRate.
    {
        Engine engine;
        configureStrip(engine, 0);
        engine.setTrackInsert(0, delayInsert(1.0, 0.0, 1.0));
        std::vector<float> input(kBlockSize * 2, 0.0f);
        input[0] = 1.0f;
        const std::vector<const std::vector<float>*> inputs{&input};
        const Capture before = render(engine, inputs);
        checkNear("48 kHz insert: echo at 48 frames", before.mainL[48], 1.0,
                  1e-6);
        engine.setSampleRate(96000.0);
        std::vector<float> second(kBlockSize * 2, 0.0f);
        second[0] = 1.0f;
        const std::vector<const std::vector<float>*> secondInputs{&second};
        const Capture after = render(engine, secondInputs);
        checkNear("rate change: echo at 96 frames", after.mainL[96], 1.0, 1e-6);
        checkNear("rate change: nothing at 48 frames", after.mainL[48], 0.0,
                  1e-6);
    }
}

// ------------------------------------------------------------- capacity

void testCapacity() {
    static_assert(kMaxTracks == 64, "product track capacity");
    static_assert(kMaxChannels == 128, "two internal channels per stereo track");
    static_assert(kMaxInputs == kMaxDeviceInputs, "kMaxInputs is the device width");

    // Default mapping: device inputs are identity for tracks 0..17; tracks
    // 18..63 have no live source until one is selected.
    Engine engine;
    for (int track = 0; track < kMaxTracks; ++track) {
        const int expected = track < kMaxDeviceInputs ? track : kNoInputSource;
        CHECK(engine.inputSource(track) == expected);
    }
    CHECK(engine.inputSource(kMaxTracks) == kNoInputSource);
    CHECK(engine.tap(kMaxTracks) == nullptr);
    CHECK(engine.channel(kMaxTracks).gain == 1.0f);
    CHECK(engine.setActiveTake(kMaxTracks - 1, ""));
    CHECK(!engine.setActiveTake(kMaxTracks, ""));

    // The last track processes as an independent stereo pair (device 16/17).
    configureStrip(engine, kMaxTracks - 1, OutputDestination::Out34);
    Channel pairChannel = engine.channel(kMaxTracks - 1);
    pairChannel.pan = 0.5f;  // balance: both sides at unity
    engine.setChannel(kMaxTracks - 1, pairChannel);
    engine.setInputSource(kMaxTracks - 1, 16);
    engine.setInputPair(kMaxTracks - 1, true);
    engine.setTrackInsert(kMaxTracks - 1, delayInsert(1.0, 0.0, 1.0));
    std::vector<float> inL(kBlockSize * 2, 0.0f);
    std::vector<float> inR(kBlockSize * 2, 0.0f);
    inL[0] = 1.0f;
    inR[0] = 0.5f;
    std::vector<const std::vector<float>*> devices(kMaxDeviceInputs, nullptr);
    devices[16] = &inL;
    devices[17] = &inR;
    const Capture out = render(engine, devices);
    checkNear("track 63 pair: left echo at 48", out.out34L[48], 1.0, 1e-6);
    checkNear("track 63 pair: right echo at 48", out.out34R[48], 0.5, 1e-6);
    checkNear("track 63 pair: no dry at 0", out.out34L[0], 0.0, 1e-6);

    // A track that loses its live source zeroes its tap once and stops
    // costing per-sample work.
    Engine tapEngine;
    configureStrip(tapEngine, 5);
    tapEngine.setInputSource(5, 0);
    std::vector<float> input(kBlockSize, 0.25f);
    const std::vector<const std::vector<float>*> one{&input};
    (void)render(tapEngine, one);
    CHECK(tapEngine.tap(5) != nullptr);
    checkNear("active track tap carries the input", tapEngine.tap(5)[0], 0.25,
              1e-6);
    tapEngine.setInputSource(5, kNoInputSource);
    std::vector<float> silence(kBlockSize, 0.0f);
    const std::vector<const std::vector<float>*> silent{&silence};
    (void)render(tapEngine, silent);
    checkNear("inactive track tap is zeroed", tapEngine.tap(5)[0], 0.0, 1e-9);

    // 64-bit arm mask: the recorder records track 63 into ch63.wav.
    std::error_code ec;
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "mnky_audio_insert_capacity";
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    CHECK(!ec);
    Recorder recorder;
    CHECK(recorder.startTake(root.string(), uint64_t{1} << 63, 48000, false));
    std::vector<float> block(kBlockSize, 0.25f);
    const float* taps[kMaxTracks] = {};
    taps[63] = block.data();
    recorder.writeBlock(taps, block.data(), block.data(), kBlockSize);
    const Recorder::Take take = recorder.stopTake();
    CHECK(take.files == 1);
    CHECK(std::filesystem::exists(std::filesystem::path(take.path) /
                                  "ch63.wav"));
    std::filesystem::remove_all(root, ec);
}

// ------------------------------------------------------------- CPU probe

// Mean microseconds per 64-frame block for `tracks` strips with `insert`.
double measureInsert(const TrackInsert& insert, int tracks, int blocks) {
    constexpr int kProbeFrames = 64;
    Engine engine;
    std::vector<std::vector<float>> input(
        static_cast<std::size_t>(tracks),
        std::vector<float>(kProbeFrames, 0.0f));
    Noise noise(3u);
    for (auto& buffer : input) {
        for (float& value : buffer) value = 0.25f * noise.next();
    }
    for (int track = 0; track < tracks; ++track) {
        configureStrip(engine, track);
        if (!insert.id.empty()) engine.setTrackInsert(track, insert);
    }
    const float* pointers[kMaxTracks] = {};
    for (int track = 0; track < tracks; ++track) {
        pointers[track] = input[static_cast<std::size_t>(track)].data();
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

void testCpuCost() {
    // 48 kHz / 64 frames = 1333 us of wall time per block. Measured on this
    // host with this build's optimization flags; the Pi 5 target must
    // re-measure before any limit is claimed.
    constexpr double kBudgetUs = 64.0 / 48000.0 * 1e6;
    std::printf(
        "INFO insert cpu: 48 kHz / 64-frame blocks, %.0f us budget, this "
        "build's flags\n",
        kBudgetUs);
    struct Row {
        const char* name;
        double us;
        int tracks;
    };
    std::vector<Row> rows;
    for (const int tracks : {1, 18, kMaxTracks}) {
        const int blocks = 36000 / tracks;
        rows.push_back({"delay", measureInsert(delayInsert(250.0, 0.35, 0.3),
                                               tracks, blocks),
                        tracks});
    }
    for (const int tracks : {1, 18, kMaxTracks}) {
        const int blocks = 36000 / tracks;
        rows.push_back({"reverb", measureInsert(reverbInsert(0.7, 0.25), tracks,
                                                blocks),
                        tracks});
    }
    const double emptyUs = measureInsert(TrackInsert{}, kMaxTracks, 2000);
    for (const Row& row : rows) {
        std::printf(
            "INFO insert cpu: %s N=%2d: %8.2f us/block (%.2f%% of %.0f us) "
            "%.1f ns/frame\n",
            row.name, row.tracks, row.us, 100.0 * row.us / kBudgetUs,
            kBudgetUs, row.us * 1000.0 / 64.0);
    }
    std::printf("INFO insert cpu: %d tracks, no insert: %8.2f us/block\n",
                kMaxTracks, emptyUs);
    // Marginal cost of one instance, from the N=1 and N=kMaxTracks rows.
    for (const char* name : {"delay", "reverb"}) {
        double one = 0.0;
        double full = 0.0;
        for (const Row& row : rows) {
            if (std::strcmp(row.name, name) != 0) continue;
            if (row.tracks == 1) one = row.us;
            if (row.tracks == kMaxTracks) full = row.us;
        }
        const double perInstance =
            (full - one) / static_cast<double>(kMaxTracks - 1);
        const double limit = perInstance > 0.0
                                 ? std::floor(0.5 * kBudgetUs / perInstance)
                                 : 0.0;
        std::printf(
            "INFO insert cpu: %s marginal %.2f us/block/instance; at 50%% of "
            "the 48k/64 budget the practical limit is %.0f instances "
            "(%d tracks available)\n",
            name, perInstance, limit, kMaxTracks);
    }
    CHECK(!rows.empty());
    CHECK(emptyUs > 0.0);
}

}  // namespace

int main() {
    testBuiltinIds();
    testPerTrackInstances();
    testBypassBitTransparent();
    testParamsHotApply();
    testIdChangeReinitializes();
    testPlacementPostEq();
    testPlacementPreFader();
    testPlacementPreSends();
    testTapStaysPreInsert();
    testEmptyIdZeroCost();
    testExternalSeam();
    testSampleRate();
    testCapacity();
    testCpuCost();
    if (gFailures != 0) {
        std::printf("FAIL insert: %d assertion(s)\n", gFailures);
        return 1;
    }
    std::printf("PASS insert: all checks\n");
    return 0;
}
