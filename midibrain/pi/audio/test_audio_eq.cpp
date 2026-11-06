// Assert-style acceptance tests for the engine four-band EQ inserts.
// Build (from midibrain/pi/audio), one command:
//   clang++ -std=c++20 -Wall -Wextra -Werror -I. test_audio_eq.cpp
//     engine.cpp take_player.cpp click.cpp -pthread -o /tmp/t_audio_eq &&
//     /tmp/t_audio_eq
//
// The insert reuses the shipped fx biquad (fx/biquad.hpp), so the spot checks
// compare it against fx::Eq configured with the same band values; the shape
// checks pin the documented boost/cut/shelf/cut responses.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "engine.hpp"
#include "eq_insert.hpp"
#include "fx/eq.hpp"

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

constexpr double kTwoPi = 6.28318530717958647692;
constexpr double kShelf6Db = 1.9952623149688795;  // 10^(6/20)
constexpr double kCentrePan = 0.70710678118654752;

void checkNear(const char* what, double actual, double expected, double eps) {
    if (std::fabs(actual - expected) <= eps) return;
    std::printf("FAIL %s: actual %.12g expected %.12g (eps %.3g)\n", what,
                actual, expected, eps);
    ++gFailures;
}

void fill(float* buffer, int n, float value) {
    for (int i = 0; i < n; ++i) buffer[i] = value;
}

EqBand band(int type, float freqHz, float gainDb, float q) {
    EqBand value;
    value.type = type;
    value.freqHz = freqHz;
    value.gainDb = gainDb;
    value.q = q;
    return value;
}

// fx::Eq needs the band type parameter applied as a stepped integer.
void configureEffectBand(fx::Eq& eq, int index, int type, double freqHz,
                         double gainDb, double q) {
    for (int b = 0; b < fx::Eq::kBandCount; ++b) {
        eq.setBandType(b, fx::Eq::BandType::None);
    }
    eq.setParam(fx::Eq::bandParam(index, fx::Eq::kType), static_cast<double>(type));
    eq.setParam(fx::Eq::bandParam(index, fx::Eq::kFreq), freqHz);
    eq.setParam(fx::Eq::bandParam(index, fx::Eq::kGain), gainDb);
    eq.setParam(fx::Eq::bandParam(index, fx::Eq::kQ), q);
    eq.reset();
}

float runInsertConstant(EqInsert& insert, float value, int blocks) {
    float left[kBlockSize] = {};
    float right[kBlockSize] = {};
    for (int block = 0; block < blocks; ++block) {
        fill(left, kBlockSize, value);
        fill(right, kBlockSize, value);
        insert.process(left, right, kBlockSize);
    }
    return left[kBlockSize - 1];
}

float runInsertAlternating(EqInsert& insert, float amplitude, int blocks) {
    float left[kBlockSize] = {};
    float right[kBlockSize] = {};
    for (int block = 0; block < blocks; ++block) {
        for (int i = 0; i < kBlockSize; ++i) {
            left[i] = (i % 2 == 0) ? amplitude : -amplitude;
            right[i] = left[i];
        }
        insert.process(left, right, kBlockSize);
    }
    return left[kBlockSize - 1];
}

// --------------------------------------------------------------- band shapes

void testBandShapes() {
    {
        EqInsert insert;
        insert.setBand(0, band(2, 120.0f, 6.0f, 0.707f));  // low shelf
        checkNear("low shelf +6 dB at DC", runInsertConstant(insert, 0.1f, 64),
                  0.1 * kShelf6Db, 0.002);
    }
    {
        EqInsert insert;
        insert.setBand(0, band(2, 120.0f, -6.0f, 0.707f));
        checkNear("low shelf -6 dB at DC", runInsertConstant(insert, 0.1f, 64),
                  0.1 * (1.0 / kShelf6Db), 0.002);
    }
    {
        EqInsert insert;
        insert.setBand(3, band(4, 8000.0f, 6.0f, 0.707f));  // high shelf
        checkNear("high shelf +6 dB at Nyquist",
                  std::fabs(runInsertAlternating(insert, 0.1f, 64)),
                  0.1 * kShelf6Db, 0.002);
    }
    {
        EqInsert insert;
        insert.setBand(3, band(4, 8000.0f, -6.0f, 0.707f));
        checkNear("high shelf -6 dB at Nyquist",
                  std::fabs(runInsertAlternating(insert, 0.1f, 64)),
                  0.1 * (1.0 / kShelf6Db), 0.002);
    }
    {
        EqInsert insert;
        insert.setBand(0, band(1, 1000.0f, 0.0f, 0.707f));  // low cut
        checkNear("low cut removes DC", runInsertConstant(insert, 0.1f, 64), 0.0,
                  0.001);
    }
    {
        EqInsert insert;
        insert.setBand(3, band(5, 1000.0f, 0.0f, 0.707f));  // high cut
        checkNear("high cut removes Nyquist",
                  std::fabs(runInsertAlternating(insert, 0.1f, 64)), 0.0, 0.001);
    }
    {
        // Parametric +12 dB at its centre. RMS of the settled 1 kHz sine.
        EqInsert insert;
        insert.setBand(1, band(3, 1000.0f, 12.0f, 1.0f));
        double phase = 0.0;
        double energy = 0.0;
        for (int block = 0; block < 200; ++block) {
            float left[kBlockSize] = {};
            float right[kBlockSize] = {};
            for (int i = 0; i < kBlockSize; ++i) {
                const float value = static_cast<float>(
                    0.1 * std::sin(kTwoPi * 1000.0 * phase / 48000.0));
                ++phase;
                left[i] = value;
                right[i] = value;
            }
            insert.process(left, right, kBlockSize);
            if (block >= 180) {
                for (int i = 0; i < kBlockSize; ++i) {
                    energy += static_cast<double>(left[i]) * left[i];
                }
            }
        }
        const double rms = std::sqrt(energy / (20.0 * kBlockSize));
        CHECK(rms > 0.2);
        CHECK(rms < 0.4);
    }

    // Spot-check every shape against the shipped fx::Eq with the same band
    // values. The insert snaps coefficients while fx::Eq ramps over 20 ms;
    // after 64 blocks both have settled on the same coefficients.
    struct Case {
        int band;
        int type;
        float freq;
        float gain;
        float q;
    };
    const Case cases[] = {
        {0, 1, 1000.0f, 0.0f, 0.707f},   // low cut
        {0, 2, 120.0f, 6.0f, 0.707f},    // low shelf
        {1, 3, 1000.0f, 9.0f, 1.2f},     // parametric
        {2, 4, 6000.0f, -6.0f, 0.707f},  // high shelf
        {3, 5, 2000.0f, 0.0f, 0.707f},   // high cut
    };
    for (const Case& item : cases) {
        EqInsert insert;
        insert.setBand(item.band, band(item.type, item.freq, item.gain, item.q));
        fx::Eq effect;
        configureEffectBand(effect, item.band, item.type, item.freq, item.gain,
                            item.q);
        float insertLeft[kBlockSize] = {};
        float insertRight[kBlockSize] = {};
        float effectLeft[kBlockSize] = {};
        float effectRight[kBlockSize] = {};
        double phase = 0.0;
        double worst = 0.0;
        for (int block = 0; block < 80; ++block) {
            for (int i = 0; i < kBlockSize; ++i) {
                const float value = static_cast<float>(
                    0.2 * std::sin(kTwoPi * 750.0 * phase / 48000.0));
                ++phase;
                insertLeft[i] = effectLeft[i] = value;
                insertRight[i] = effectRight[i] = value * 0.5f;
            }
            insert.process(insertLeft, insertRight, kBlockSize);
            float* effectBuffers[2] = {effectLeft, effectRight};
            effect.process(effectBuffers, 2, kBlockSize);
            if (block >= 72) {
                for (int i = 0; i < kBlockSize; ++i) {
                    worst = std::max(
                        worst, std::fabs(static_cast<double>(insertLeft[i]) -
                                         static_cast<double>(effectLeft[i])));
                    worst = std::max(
                        worst, std::fabs(static_cast<double>(insertRight[i]) -
                                         static_cast<double>(effectRight[i])));
                }
            }
        }
        checkNear("insert matches fx::Eq band math", worst, 0.0, 1e-7);
    }

    std::printf("PASS eq: band shapes match the shipped biquad math\n");
}

// ------------------------------------------------------- None = transparent

void testNoneTransparent() {
    // An all-None insert must not touch the buffer, including denormals.
    EqInsert insert;
    CHECK(!insert.active());
    float left[kBlockSize] = {};
    float right[kBlockSize] = {};
    for (int i = 0; i < kBlockSize; ++i) {
        left[i] = (i % 3 == 0) ? 1.0e-30f : static_cast<float>(i) / 128.0f - 0.5f;
        right[i] = -left[i];
    }
    float expectedLeft[kBlockSize] = {};
    float expectedRight[kBlockSize] = {};
    std::memcpy(expectedLeft, left, sizeof expectedLeft);
    std::memcpy(expectedRight, right, sizeof expectedRight);
    insert.process(left, right, kBlockSize);
    CHECK(std::memcmp(left, expectedLeft, sizeof expectedLeft) == 0);
    CHECK(std::memcmp(right, expectedRight, sizeof expectedRight) == 0);

    // The engine path is bit-identical too: enabling and then disabling a band
    // must leave the output exactly where a fresh engine puts it.
    Engine reference;
    Engine toggled;
    Channel channel;
    channel.gain = 0.75f;
    channel.vol = 0.5f;
    channel.pan = 0.3f;
    reference.setChannel(0, channel);
    toggled.setChannel(0, channel);
    toggled.setTrackEqBand(0, 0, band(1, 1000.0f, 0.0f, 0.707f));
    toggled.setTrackEqBand(0, 1, band(3, 1000.0f, 12.0f, 1.0f));
    toggled.setTrackEqBand(0, 0, band(0, 1000.0f, 0.0f, 1.0f));
    toggled.setTrackEqBand(0, 1, band(0, 1000.0f, 0.0f, 1.0f));
    CHECK(!toggled.trackEqBand(0, 0).type);
    CHECK(!toggled.trackEqBand(0, 1).type);

    float input0[kBlockSize] = {};
    float input1[kBlockSize] = {};
    for (int i = 0; i < kBlockSize; ++i) {
        input0[i] = static_cast<float>((i * 37) % 101) / 101.0f - 0.5f;
        input1[i] = static_cast<float>((i * 53) % 89) / 89.0f - 0.5f;
    }
    const float* inputs[kMaxInputs] = {input0, input1};
    float refL[kBlockSize] = {};
    float refR[kBlockSize] = {};
    float gotL[kBlockSize] = {};
    float gotR[kBlockSize] = {};
    reference.process(inputs, 2, kBlockSize, refL, refR);
    toggled.process(inputs, 2, kBlockSize, gotL, gotR);
    CHECK(std::memcmp(refL, gotL, sizeof refL) == 0);
    CHECK(std::memcmp(refR, gotR, sizeof refR) == 0);

    std::printf("PASS eq: type None is a bit-transparent bypass\n");
}

// ------------------------------------------------- per-track insert placement

void testTrackPlacement() {
    Engine engine;
    Channel channel;
    channel.gain = 2.0f;
    channel.vol = 0.25f;
    channel.pan = 0.5f;
    channel.cueSend = 1.0f;
    channel.out34Send = 1.0f;
    engine.setChannel(0, channel);
    engine.setSendSource(SendDestination::Cue, SendSource::PreFader);
    engine.setOut34Mode(Out34Mode::Stereo);
    engine.setTrackEqBand(0, 0, band(2, 120.0f, 6.0f, 0.707f));  // low shelf

    float input0[kBlockSize] = {};
    fill(input0, kBlockSize, 0.5f);
    const float* inputs[kMaxInputs] = {input0};
    float mainL[kBlockSize] = {};
    float mainR[kBlockSize] = {};
    float phonesL[kBlockSize] = {};
    float phonesR[kBlockSize] = {};
    float out34L[kBlockSize] = {};
    float out34R[kBlockSize] = {};
    OutputBuses buses;
    buses.mainL = mainL;
    buses.mainR = mainR;
    buses.phonesL = phonesL;
    buses.phonesR = phonesR;
    buses.out34L = out34L;
    buses.out34R = out34R;
    for (int block = 0; block < 64; ++block) {
        engine.process(inputs, 1, kBlockSize, buses);
    }

    const double eqGain = kShelf6Db;
    const double expectedMain = 0.5 * 2.0 * eqGain * 0.25 * kCentrePan;
    checkNear("strip EQ is post-gain pre-fader on Main", mainL[kBlockSize - 1],
              expectedMain, 2e-3);
    checkNear("strip EQ keeps the stereo pan law", mainR[kBlockSize - 1],
              expectedMain, 2e-3);
    // The recorder tap is the historical live input * gain, deliberately pre-EQ.
    const float* tap = engine.tap(0);
    CHECK(tap != nullptr);
    if (tap != nullptr) {
        checkNear("recorder tap stays pre-EQ", tap[kBlockSize - 1], 1.0, 1e-6);
    }
    // CUE pre-fader sees gain + EQ but neither vol nor pan.
    checkNear("pre-fader cue carries strip EQ", phonesL[kBlockSize - 1],
              0.5 * 2.0 * eqGain, 2e-3);
    checkNear("pre-fader cue stays mono-centred", phonesR[kBlockSize - 1],
              0.5 * 2.0 * eqGain, 2e-3);
    // OUT3&4 post-fader follows vol and pan, and carries the strip EQ.
    checkNear("post-fader OUT3&4 carries strip EQ", out34L[kBlockSize - 1],
              expectedMain, 2e-3);

    // Type None returns the strip to the documented dry path.
    engine.setTrackEqBand(0, 0, band(0, 120.0f, 0.0f, 0.707f));
    for (int block = 0; block < 64; ++block) {
        engine.process(inputs, 1, kBlockSize, buses);
    }
    checkNear("None band is the dry path", mainL[kBlockSize - 1],
              0.5 * 2.0 * 0.25 * kCentrePan, 1e-6);
    checkNear("None band leaves the tap unchanged", tap[kBlockSize - 1], 1.0,
              1e-6);

    std::printf("PASS eq: per-track insert placement (post-gain, pre-fader, post-tap)\n");
}

// --------------------------------------------------- main-bus insert placement

double runEngineConstant(Engine& engine, float value, int blocks,
                         float* lastL, float* lastR,
                         OutputBuses* buses = nullptr) {
    float input[kBlockSize] = {};
    fill(input, kBlockSize, value);
    const float* inputs[kMaxInputs] = {input};
    float mainL[kBlockSize] = {};
    float mainR[kBlockSize] = {};
    OutputBuses local;
    local.mainL = mainL;
    local.mainR = mainR;
    OutputBuses out = buses != nullptr ? *buses : local;
    for (int block = 0; block < blocks; ++block) {
        engine.process(inputs, 1, kBlockSize, out);
    }
    if (lastL != nullptr && out.mainL != nullptr) {
        *lastL = out.mainL[kBlockSize - 1];
    }
    if (lastR != nullptr && out.mainR != nullptr) {
        *lastR = out.mainR[kBlockSize - 1];
    }
    return out.mainL != nullptr ? out.mainL[kBlockSize - 1] : 0.0f;
}

void testMainPlacement() {
    MainCompSettings comp;
    comp.on = true;
    comp.threshDb = -12.0f;
    comp.ratio = 4.0f;
    comp.attackMs = 1.0f;
    comp.releaseMs = 10.0f;
    comp.makeupDb = 0.0f;
    comp.mode = CompressorMode::Peak;

    Channel channel;
    channel.pan = 0.5f;

    Engine compressed;
    compressed.setChannel(0, channel);
    compressed.setMainComp(comp);
    float compOnly = 0.0f;
    runEngineConstant(compressed, 0.5f, 100, &compOnly, nullptr);

    Engine boosted;
    boosted.setChannel(0, channel);
    boosted.setMainComp(comp);
    boosted.setMainEqBand(0, band(2, 120.0f, 6.0f, 0.707f));
    float compEq = 0.0f;
    runEngineConstant(boosted, 0.5f, 100, &compEq, nullptr);

    Engine preComp;
    preComp.setChannel(0, channel);
    preComp.setMainComp(comp);
    float preCompLevel = 0.0f;
    runEngineConstant(preComp, static_cast<float>(0.5 * kShelf6Db), 100,
                      &preCompLevel, nullptr);

    const double expectedPost = static_cast<double>(compOnly) * kShelf6Db;
    checkNear("main EQ is post-compressor", compEq, expectedPost, 3e-3);
    CHECK(std::fabs(static_cast<double>(compEq) - expectedPost) <
          std::fabs(static_cast<double>(compEq) -
                    static_cast<double>(preCompLevel)));
    CHECK(std::fabs(static_cast<double>(compEq) -
                    static_cast<double>(preCompLevel)) > 0.05);

    // Main only: OUT3&4 and the CUE bus never see the main insert, and a
    // Phones-as-Main duplicate does.
    Engine routed;
    Channel sends = channel;
    sends.cueSend = 1.0f;
    sends.out34Send = 1.0f;
    routed.setChannel(0, sends);
    routed.setMainComp(comp);
    routed.setMainEqBand(0, band(2, 120.0f, 6.0f, 0.707f));
    routed.setCueStyle(CueStyle::Level);
    routed.setSendSource(SendDestination::Cue, SendSource::PostFader);
    routed.setSendSource(SendDestination::Out34, SendSource::PostFader);

    float mainL[kBlockSize] = {};
    float mainR[kBlockSize] = {};
    float phonesL[kBlockSize] = {};
    float phonesR[kBlockSize] = {};
    float out34L[kBlockSize] = {};
    float out34R[kBlockSize] = {};
    OutputBuses buses;
    buses.mainL = mainL;
    buses.mainR = mainR;
    buses.phonesL = phonesL;
    buses.phonesR = phonesR;
    buses.out34L = out34L;
    buses.out34R = out34R;
    runEngineConstant(routed, 0.5f, 100, nullptr, nullptr, &buses);
    checkNear("main EQ reaches Main", mainL[kBlockSize - 1], expectedPost, 3e-3);

    Engine reference;
    reference.setChannel(0, sends);
    reference.setMainComp(comp);
    reference.setCueStyle(CueStyle::Level);
    reference.setSendSource(SendDestination::Cue, SendSource::PostFader);
    reference.setSendSource(SendDestination::Out34, SendSource::PostFader);
    float refMainL[kBlockSize] = {};
    float refMainR[kBlockSize] = {};
    float refPhonesL[kBlockSize] = {};
    float refPhonesR[kBlockSize] = {};
    float refOut34L[kBlockSize] = {};
    float refOut34R[kBlockSize] = {};
    OutputBuses refBuses;
    refBuses.mainL = refMainL;
    refBuses.mainR = refMainR;
    refBuses.phonesL = refPhonesL;
    refBuses.phonesR = refPhonesR;
    refBuses.out34L = refOut34L;
    refBuses.out34R = refOut34R;
    runEngineConstant(reference, 0.5f, 100, nullptr, nullptr, &refBuses);

    CHECK(std::memcmp(out34L, refOut34L, sizeof out34L) == 0);
    CHECK(std::memcmp(out34R, refOut34R, sizeof out34R) == 0);
    CHECK(std::memcmp(phonesL, refPhonesL, sizeof phonesL) == 0);
    CHECK(std::memcmp(phonesR, refPhonesR, sizeof phonesR) == 0);

    // Phones follows Main when PhonesSource::Main.
    Engine phonesMain;
    phonesMain.setChannel(0, channel);
    phonesMain.setMainComp(comp);
    phonesMain.setMainEqBand(0, band(2, 120.0f, 6.0f, 0.707f));
    phonesMain.setPhonesSource(PhonesSource::Main);
    float pmMainL[kBlockSize] = {};
    float pmMainR[kBlockSize] = {};
    float pmPhonesL[kBlockSize] = {};
    float pmPhonesR[kBlockSize] = {};
    OutputBuses pmBuses;
    pmBuses.mainL = pmMainL;
    pmBuses.mainR = pmMainR;
    pmBuses.phonesL = pmPhonesL;
    pmBuses.phonesR = pmPhonesR;
    runEngineConstant(phonesMain, 0.5f, 100, nullptr, nullptr, &pmBuses);
    for (int i = 0; i < kBlockSize; ++i) {
        checkNear("phones Main duplicate carries main EQ", pmPhonesL[i],
                  pmMainL[i], 1e-6);
    }

    std::printf("PASS eq: main insert is post-compressor, Main-only\n");
}

// -------------------------------------------------------------------- clamps

void testClamps() {
    Engine engine;
    EqBand value = band(99, 1.0f, 100.0f, 0.001f);
    engine.setTrackEqBand(3, 2, value);
    EqBand got = engine.trackEqBand(3, 2);
    CHECK(got.type == 5);
    CHECK(got.freqHz == 20.0f);
    CHECK(got.gainDb == 24.0f);
    CHECK(got.q == 0.1f);

    value = band(-7, 1.0e9f, -100.0f, 1.0e6f);
    engine.setTrackEqBand(3, 2, value);
    got = engine.trackEqBand(3, 2);
    CHECK(got.type == 0);
    CHECK(got.freqHz == 20000.0f);
    CHECK(got.gainDb == -24.0f);
    CHECK(got.q == 24.0f);

    value = band(3, std::nanf(""), std::nanf(""), std::nanf(""));
    engine.setTrackEqBand(0, 1, value);
    got = engine.trackEqBand(0, 1);
    CHECK(got.type == 3);
    CHECK(got.freqHz == 1000.0f);
    CHECK(got.gainDb == 0.0f);
    CHECK(got.q == 1.0f);

    value = band(2, 800.0f, 3.0f, 2.0f);
    engine.setMainEqBand(1, value);
    got = engine.mainEqBand(1);
    CHECK(got == value);
    engine.setMainEqBand(1, band(42, 1.0f, 100.0f, 0.0f));
    got = engine.mainEqBand(1);
    CHECK(got.type == 5);
    CHECK(got.freqHz == 20.0f);
    CHECK(got.gainDb == 24.0f);
    CHECK(got.q == 0.1f);

    // Out-of-range tracks/bands are ignored and read as the session default.
    const EqBand untouched = engine.trackEqBand(3, 1);
    CHECK(untouched == EqBand{});
    engine.setTrackEqBand(-1, 0, band(3, 1000.0f, 6.0f, 1.0f));
    engine.setTrackEqBand(kMaxTracks, 0, band(3, 1000.0f, 6.0f, 1.0f));
    engine.setTrackEqBand(0, -1, band(3, 1000.0f, 6.0f, 1.0f));
    engine.setTrackEqBand(0, 4, band(3, 1000.0f, 6.0f, 1.0f));
    CHECK(engine.trackEqBand(-1, 0) == EqBand{});
    CHECK(engine.trackEqBand(kMaxTracks, 0) == EqBand{});
    CHECK(engine.trackEqBand(0, -1) == EqBand{});
    CHECK(engine.trackEqBand(0, 4) == EqBand{});
    engine.setMainEqBand(-1, band(3, 1000.0f, 6.0f, 1.0f));
    engine.setMainEqBand(4, band(3, 1000.0f, 6.0f, 1.0f));
    CHECK(engine.mainEqBand(-1) == EqBand{});
    CHECK(engine.mainEqBand(4) == EqBand{});

    EqInsert insert;
    insert.setSampleRate(1000.0);
    CHECK(insert.sampleRate() == 48000.0);
    insert.setSampleRate(96000.0);
    CHECK(insert.sampleRate() == 96000.0);
    insert.setSampleRate(std::nan(""));
    CHECK(insert.sampleRate() == 96000.0);

    std::printf("PASS eq: session clamps and out-of-range no-ops\n");
}

// ----------------------------------------------------------- sample-rate link

void testSampleRate() {
    EqInsert insert;
    insert.setSampleRate(96000.0);
    insert.setBand(0, band(3, 1000.0f, 12.0f, 1.0f));
    double phase = 0.0;
    double energy = 0.0;
    for (int block = 0; block < 200; ++block) {
        float left[kBlockSize] = {};
        float right[kBlockSize] = {};
        for (int i = 0; i < kBlockSize; ++i) {
            const float sample = static_cast<float>(
                0.1 * std::sin(kTwoPi * 1000.0 * phase / 96000.0));
            ++phase;
            left[i] = sample;
            right[i] = sample;
        }
        insert.process(left, right, kBlockSize);
        if (block >= 180) {
            for (int i = 0; i < kBlockSize; ++i) {
                energy += static_cast<double>(left[i]) * left[i];
            }
        }
    }
    const double rms = std::sqrt(energy / (20.0 * kBlockSize));
    CHECK(rms > 0.2);
    CHECK(rms < 0.4);

    // The engine forwards the rate to its inserts: a 1 kHz parametric boost at
    // 96 kHz must still peak at 1 kHz, not at the stale 48 kHz coefficient.
    Engine engine;
    engine.setSampleRate(96000.0);
    Channel channel;
    channel.pan = 0.0f;
    engine.setChannel(0, channel);
    engine.setMainEqBand(0, band(3, 1000.0f, 12.0f, 1.0f));
    float input[kBlockSize] = {};
    const float* inputs[kMaxInputs] = {input};
    float outL[kBlockSize] = {};
    float outR[kBlockSize] = {};
    double engineEnergy = 0.0;
    double enginePhase = 0.0;
    for (int block = 0; block < 200; ++block) {
        for (int i = 0; i < kBlockSize; ++i) {
            input[i] = static_cast<float>(
                0.1 * std::sin(kTwoPi * 1000.0 * enginePhase / 96000.0));
            ++enginePhase;
        }
        engine.process(inputs, 1, kBlockSize, outL, outR);
        if (block >= 180) {
            for (int i = 0; i < kBlockSize; ++i) {
                engineEnergy += static_cast<double>(outL[i]) * outL[i];
            }
        }
    }
    const double engineRms = std::sqrt(engineEnergy / (20.0 * kBlockSize));
    CHECK(engineRms > 0.2);
    CHECK(engineRms < 0.4);

    std::printf("PASS eq: sample rate reaches the insert biquads\n");
}

// ---------------------------------------------------------------- CPU honesty

void testCpuCost() {
    EqInsert inserts[19];
    for (EqInsert& insert : inserts) {
        insert.setBand(0, band(1, 80.0f, 0.0f, 0.707f));
        insert.setBand(1, band(2, 120.0f, 3.0f, 0.707f));
        insert.setBand(2, band(3, 2000.0f, -4.0f, 1.0f));
        insert.setBand(3, band(5, 9000.0f, 0.0f, 0.707f));
    }
    float left[kBlockSize] = {};
    float right[kBlockSize] = {};
    double phase = 0.0;
    for (int i = 0; i < kBlockSize; ++i) {
        left[i] = static_cast<float>(0.1 * std::sin(kTwoPi * 440.0 * phase / 48000.0));
        right[i] = -left[i];
        ++phase;
    }
    constexpr int kBlocks = 512;
    const auto start = std::chrono::steady_clock::now();
    for (int block = 0; block < kBlocks; ++block) {
        for (EqInsert& insert : inserts) insert.process(left, right, kBlockSize);
    }
    const auto stop = std::chrono::steady_clock::now();
    const double seconds =
        std::chrono::duration<double>(stop - start).count();
    const double nsPerBlock = seconds * 1e9 / kBlocks;
    const double nsPerFrame = nsPerBlock / kBlockSize;
    const double cyclesPerFrameAt2p4Ghz = nsPerFrame * 2.4;
    std::printf(
        "INFO eq cpu: 19 inserts x 4 bands, %d-frame block: %.1f us/block, "
        "%.2f ns/frame (~%.1f cycles/frame at 2.4 GHz)\n",
        kBlockSize, nsPerBlock / 1000.0, nsPerFrame, cyclesPerFrameAt2p4Ghz);

    // Integration cost: the whole engine with all 18 strip inserts and the
    // main insert active. Printed, not asserted (timing is host-dependent).
    Engine engine;
    for (int track = 0; track < kMaxInputs; ++track) {
        engine.setTrackEqBand(track, 0, band(1, 80.0f, 0.0f, 0.707f));
        engine.setTrackEqBand(track, 1, band(2, 120.0f, 3.0f, 0.707f));
        engine.setTrackEqBand(track, 2, band(3, 2000.0f, -4.0f, 1.0f));
        engine.setTrackEqBand(track, 3, band(5, 9000.0f, 0.0f, 0.707f));
    }
    engine.setMainEqBand(0, band(1, 80.0f, 0.0f, 0.707f));
    engine.setMainEqBand(1, band(2, 120.0f, 3.0f, 0.707f));
    engine.setMainEqBand(2, band(3, 2000.0f, -4.0f, 1.0f));
    engine.setMainEqBand(3, band(5, 9000.0f, 0.0f, 0.707f));
    const float* inputs[kMaxInputs] = {};
    for (int track = 0; track < kMaxInputs; ++track) inputs[track] = left;
    float outL[kBlockSize] = {};
    float outR[kBlockSize] = {};
    const auto engineStart = std::chrono::steady_clock::now();
    for (int block = 0; block < kBlocks; ++block) {
        engine.process(inputs, kMaxInputs, kBlockSize, outL, outR);
    }
    const auto engineStop = std::chrono::steady_clock::now();
    const double engineSeconds =
        std::chrono::duration<double>(engineStop - engineStart).count();
    const double engineNsPerBlock = engineSeconds * 1e9 / kBlocks;
    std::printf(
        "INFO eq cpu: engine with 19 active inserts: %.1f us/block, "
        "%.2f ns/frame (~%.1f cycles/frame at 2.4 GHz)\n",
        engineNsPerBlock / 1000.0, engineNsPerBlock / kBlockSize,
        engineNsPerBlock / kBlockSize * 2.4);
    CHECK(engineNsPerBlock > 0.0);

    std::printf("PASS eq: CPU cost measured (not a manufacturing gate)\n");
}

}  // namespace

int main() {
    testBandShapes();
    testNoneTransparent();
    testTrackPlacement();
    testMainPlacement();
    testClamps();
    testSampleRate();
    testCpuCost();
    if (gFailures != 0) {
        std::printf("FAIL eq: %d assertion(s)\n", gFailures);
        return 1;
    }
    std::printf("PASS eq: all checks\n");
    return 0;
}
