// Assert-style acceptance tests for mnky::audio (no external framework).
// Build: c++ -std=c++20 -Wall -Wextra -Werror -I. test_audio.cpp engine.cpp recorder.cpp -o /tmp/t_audio -pthread

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
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

constexpr float kEps = 1e-6f;

bool approx(float actual, float expected, float eps = kEps) {
    return std::fabs(actual - expected) <= eps;
}

void checkNear(const char* what, float actual, float expected, float eps = kEps) {
    if (approx(actual, expected, eps)) return;
    std::printf("FAIL %s: actual %.9g expected %.9g\n", what, actual, expected);
    ++gFailures;
}

void checkSamples(const char* what, const std::vector<float>& actual, const std::vector<float>& expected,
                  float eps = kEps) {
    if (actual.size() != expected.size()) {
        std::printf("FAIL %s: %zu samples, expected %zu\n", what, actual.size(), expected.size());
        ++gFailures;
        return;
    }
    for (size_t i = 0; i < actual.size(); ++i) {
        if (approx(actual[i], expected[i], eps)) continue;
        std::printf("FAIL %s[%zu]: actual %.9g expected %.9g\n", what, i, actual[i], expected[i]);
        ++gFailures;
        return;
    }
}

// Centre-pan mono sum: constant-power L/R gain at pan 0.5.
constexpr float kCentre = 0.70710678f;

void fill(float* buffer, float value) {
    for (int i = 0; i < kBlockSize; ++i) buffer[i] = value;
}

// ---------------------------------------------------------------- gain / pan

void testGainPan() {
    Engine engine;
    float in[kBlockSize];
    fill(in, 1.0f);
    const float* inputs[kMaxInputs] = {&in[0]};
    float outL[kBlockSize];
    float outR[kBlockSize];

    // Default strip: unity gain, centre pan.
    engine.process(inputs, 1, kBlockSize, outL, outR);
    checkNear("default centre L", outL[0], kCentre);
    checkNear("default centre R", outR[0], kCentre);
    checkNear("default centre L last frame", outL[kBlockSize - 1], kCentre);

    // Constant-power law: L^2 + R^2 == gain^2 at every position.
    const float pans[] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
    for (const float pan : pans) {
        Channel ch;
        ch.gain = 0.5f;
        ch.pan = pan;
        engine.setChannel(0, ch);
        engine.process(inputs, 1, kBlockSize, outL, outR);
        checkNear("pan law power", outL[0] * outL[0] + outR[0] * outR[0], 0.25f);
    }

    // Hard left / hard right.
    Channel ch;
    ch.pan = 0.0f;
    engine.setChannel(0, ch);
    engine.process(inputs, 1, kBlockSize, outL, outR);
    checkNear("hard left L", outL[0], 1.0f);
    checkNear("hard left R", outR[0], 0.0f);

    ch.pan = 1.0f;
    engine.setChannel(0, ch);
    engine.process(inputs, 1, kBlockSize, outL, outR);
    checkNear("hard right L", outL[0], 0.0f);
    checkNear("hard right R", outR[0], 1.0f);

    // Gain trim and master gain multiply through.
    ch.pan = 0.5f;
    ch.gain = 0.5f;
    engine.setChannel(0, ch);
    engine.setMaster(0.5f);
    engine.process(inputs, 1, kBlockSize, outL, outR);
    checkNear("gain*pan*master L", outL[0], 0.5f * kCentre * 0.5f);

    // Master clamp: negative/NaN master is silent.
    engine.setMaster(-1.0f);
    engine.process(inputs, 1, kBlockSize, outL, outR);
    checkNear("negative master quiet", outL[0], 0.0f);
    engine.setMaster(std::nanf(""));
    engine.process(inputs, 1, kBlockSize, outL, outR);
    checkNear("nan master quiet", outL[0], 0.0f);

    std::printf("PASS audio engine: gain and constant-power pan\n");
}

// ------------------------------------------------------------- mute / solo

void testMuteSolo() {
    Engine engine;
    float in0[kBlockSize];
    float in1[kBlockSize];
    float in2[kBlockSize];
    fill(in0, 1.0f);
    fill(in1, 2.0f);
    fill(in2, 3.0f);
    const float* inputs[kMaxInputs] = {&in0[0], &in1[0], &in2[0]};
    float outL[kBlockSize];
    float outR[kBlockSize];

    Channel strips[3];
    for (Channel& ch : strips) {
        ch.gain = 1.0f;
        ch.pan = 0.5f;
    }

    auto run = [&](const char* what, float expectedSum) {
        for (int c = 0; c < 3; ++c) engine.setChannel(c, strips[c]);
        engine.process(inputs, 3, kBlockSize, outL, outR);
        checkNear(what, outL[0], expectedSum * kCentre);
        checkNear("centre pan keeps L==R", outR[0], outL[0]);
    };

    run("no mute/solo sums all", 6.0f);

    strips[0].mute = true;
    run("mute removes strip 0", 5.0f);

    strips[0].mute = false;
    strips[1].solo = true;
    run("any-solo keeps only the solo", 2.0f);

    strips[2].solo = true;
    run("two solos both audible", 5.0f);

    strips[1].mute = true;
    run("mute wins over own solo", 3.0f);

    strips[1].mute = false;
    strips[2].solo = false;
    run("back to a single solo", 2.0f);

    strips[1].solo = false;
    strips[2].solo = false;
    for (Channel& ch : strips) ch.mute = true;
    run("all muted is silent", 0.0f);
    for (Channel& ch : strips) ch.mute = false;

    strips[1].solo = true;
    strips[1].gain = 0.0f;
    run("soloed strip still obeys its gain", 0.0f);

    std::printf("PASS audio engine: mute/solo matrix\n");
}

// ------------------------------------------------- retired sends and FX

class GainFx : public IEffect {
public:
    GainFx(float gain, const char* name) : mGain(gain), mName(name) {}

    void process(float* const* ch, int nch, int n) override {
        ++mCalls;
        mLastNch = nch;
        mLastN = n;
        if (nch > 0 && n > 0) mLastInput0 = ch[0][0];
        for (int c = 0; c < nch; ++c) {
            for (int i = 0; i < n; ++i) ch[c][i] *= mGain;
        }
    }

    const char* name() const override { return mName; }

    int calls() const { return mCalls; }
    int lastNch() const { return mLastNch; }
    int lastN() const { return mLastN; }
    float lastInput0() const { return mLastInput0; }

private:
    float mGain;
    const char* mName;
    int mCalls = 0;
    int mLastNch = 0;
    int mLastN = 0;
    float mLastInput0 = 0.0f;
};

void testSendsAndFx() {
    // The global FX1/FX2 send buses are RETIRED from the signal path. The
    // Channel::send levels and setFx()/setFxReturn() stay in the API for the
    // app's refactor, but neither the bus effect nor its return may reach Main.
    Engine engine;
    float in[kBlockSize];
    fill(in, 1.0f);
    const float* inputs[kMaxInputs] = {&in[0]};
    float outL[kBlockSize];
    float outR[kBlockSize];

    Channel ch;
    ch.pan = 0.5f;
    ch.gain = 1.0f;
    ch.send[0] = 0.5f;
    ch.send[1] = 0.0f;
    engine.setChannel(0, ch);

    GainFx gainFx(2.0f, "gain");
    engine.setFx(0, &gainFx);
    engine.setFxReturn(0, 0.25f);
    engine.setFxReturn(1, 0.0f);

    engine.process(inputs, 1, kBlockSize, outL, outR);
    CHECK(gainFx.calls() == 0);  // no bus process call at all
    checkNear("retired FX send leaves dry L", outL[0], kCentre);
    checkNear("retired FX send leaves dry R", outR[0], kCentre);

    // Send levels, the send source and every return level are inert.
    ch.send[1] = 1.0f;
    engine.setChannel(0, ch);
    engine.setFxReturn(1, 0.5f);
    engine.setSendSource(SendDestination::Fx1, SendSource::PreFader);
    engine.process(inputs, 1, kBlockSize, outL, outR);
    checkNear("retired FX send level is inert", outL[0], kCentre);
    checkNear("retired FX return is inert", outR[0], kCentre);

    // Mute still closes the dry path.
    ch.mute = true;
    engine.setChannel(0, ch);
    engine.process(inputs, 1, kBlockSize, outL, outR);
    checkNear("muted strip sends nothing", outL[0], 0.0f);
    CHECK(gainFx.calls() == 0);

    // Detaching or reattaching the bus effect cannot change the output.
    ch.mute = false;
    engine.setChannel(0, ch);
    engine.setFx(0, nullptr);
    engine.setFxReturn(1, 0.0f);
    engine.process(inputs, 1, kBlockSize, outL, outR);
    checkNear("detached FX is still dry", outL[0], kCentre);
    engine.setFx(0, &gainFx);
    engine.setFxReturn(0, 1.0f);
    engine.process(inputs, 1, kBlockSize, outL, outR);
    checkNear("full FX return is still dry", outL[0], kCentre);

    std::printf("PASS audio engine: retired FX send buses are inert\n");
}

// ------------------------------------------------------------------- taps

void testTapProvenance() {
    Engine engine;
    float in0[kBlockSize];
    float in1[kBlockSize];
    for (int i = 0; i < kBlockSize; ++i) {
        in0[i] = 0.01f * static_cast<float>(i);
        in1[i] = 1.0f;
    }
    const float* inputs[kMaxInputs] = {&in0[0], &in1[0]};
    float outL[kBlockSize];
    float outR[kBlockSize];

    Channel ch0;
    ch0.gain = 0.5f;
    ch0.pan = 0.0f;
    Channel ch1;
    ch1.gain = 2.0f;
    ch1.mute = true;
    engine.setChannel(0, ch0);
    engine.setChannel(1, ch1);
    engine.process(inputs, 2, kBlockSize, outL, outR);

    const float* tap0 = engine.tap(0);
    const float* tap1 = engine.tap(1);
    const float* tap2 = engine.tap(2);
    CHECK(tap0 != nullptr);
    CHECK(tap1 != nullptr);
    CHECK(tap2 != nullptr);
    CHECK(engine.tap(-1) == nullptr);
    CHECK(engine.tap(kMaxTracks) == nullptr);
    if (tap0 != nullptr && tap1 != nullptr && tap2 != nullptr) {
        for (int i = 0; i < kBlockSize; ++i) {
            checkNear("tap 0 is post-gain", tap0[i], in0[i] * 0.5f);
            checkNear("tap 1 ignores mute", tap1[i], in1[i] * 2.0f);
            checkNear("tap beyond nch is silent", tap2[i], 0.0f);
        }
        checkNear("muted strip stays out of the master L", outL[0], 0.0f);
        checkNear("muted strip stays out of the master R", outR[0], 0.0f);
    }

    // Taps track the current block only.
    fill(in0, 0.25f);
    engine.process(inputs, 2, kBlockSize, outL, outR);
    if (tap0 != nullptr) {
        checkNear("tap follows the next block", tap0[0], 0.125f);
        checkNear("tap tracks last frame", tap0[kBlockSize - 1], 0.125f);
    }

    // A soloed strip still exposes its own post-gain tap, and non-soloed strips
    // keep their taps even though they are pulled from the mix.
    Channel soloed = ch0;
    soloed.solo = true;
    engine.setChannel(0, soloed);
    engine.process(inputs, 2, kBlockSize, outL, outR);
    if (tap0 != nullptr) checkNear("tap independent of solo", tap0[0], 0.125f);
    checkNear("soloed strip is in the master", outL[0], 0.125f);

    std::printf("PASS audio engine: post-gain tap provenance\n");
}

// --------------------------------------------------------------- recorder

struct WavInfo {
    bool ok = false;
    std::string error;
    uint16_t format = 0;
    uint16_t channels = 0;
    uint16_t bits = 0;
    uint32_t sampleRate = 0;
    uint32_t factFrames = 0;
    uint32_t dataBytes = 0;
    std::vector<float> samples;
};

uint32_t le32(const std::vector<unsigned char>& bytes, size_t at) {
    return static_cast<uint32_t>(bytes[at]) | (static_cast<uint32_t>(bytes[at + 1]) << 8) |
           (static_cast<uint32_t>(bytes[at + 2]) << 16) | (static_cast<uint32_t>(bytes[at + 3]) << 24);
}

uint16_t le16(const std::vector<unsigned char>& bytes, size_t at) {
    return static_cast<uint16_t>(static_cast<uint32_t>(bytes[at]) | (static_cast<uint32_t>(bytes[at + 1]) << 8));
}

WavInfo readWav(const std::string& path) {
    WavInfo info;
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        info.error = "cannot open " + path;
        return info;
    }
    const std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    if (bytes.size() < 56) {
        info.error = "too short";
        return info;
    }
    if (std::memcmp(bytes.data(), "RIFF", 4) != 0 || std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
        info.error = "not RIFF/WAVE";
        return info;
    }
    if (le32(bytes, 4) != bytes.size() - 8) {
        info.error = "RIFF size mismatch";
        return info;
    }

    bool haveFmt = false;
    bool haveData = false;
    size_t at = 12;
    while (at + 8 <= bytes.size()) {
        const uint32_t size = le32(bytes, at + 4);
        const size_t payload = at + 8;
        if (payload + size > bytes.size()) {
            info.error = "chunk overruns file";
            return info;
        }
        if (std::memcmp(bytes.data() + at, "fmt ", 4) == 0) {
            if (size < 16) {
                info.error = "short fmt chunk";
                return info;
            }
            info.format = le16(bytes, payload);
            info.channels = le16(bytes, payload + 2);
            info.sampleRate = le32(bytes, payload + 4);
            info.bits = le16(bytes, payload + 14);
            haveFmt = true;
        } else if (std::memcmp(bytes.data() + at, "fact", 4) == 0) {
            if (size < 4) {
                info.error = "short fact chunk";
                return info;
            }
            info.factFrames = le32(bytes, payload);
        } else if (std::memcmp(bytes.data() + at, "data", 4) == 0) {
            info.dataBytes = size;
            info.samples.resize(size / sizeof(float));
            std::memcpy(info.samples.data(), bytes.data() + payload, info.samples.size() * sizeof(float));
            haveData = true;
        }
        at = payload + size + (size % 2u);
    }

    if (!haveFmt || !haveData) {
        info.error = "missing chunk";
        return info;
    }
    info.ok = true;
    return info;
}

void testRecorderRoundTrip() {
    Engine engine;
    Channel left;
    left.gain = 0.5f;
    left.pan = 0.0f;
    Channel right;
    right.gain = 1.0f;
    right.pan = 1.0f;
    engine.setChannel(0, left);
    engine.setChannel(2, right);
    engine.setMaster(0.8f);

    std::error_code ec;
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "mnky_audio_test_take";
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    CHECK(!ec);

    const uint32_t armMask = (1u << 0) | (1u << 2);
    Recorder recorder;
    CHECK(recorder.startTake(root.string(), armMask, 48000));
    CHECK(recorder.recording());
    CHECK(recorder.drops() == 0);

    // A take cannot be restarted, and an empty arm mask is rejected.
    CHECK(!recorder.startTake((root / "second").string(), armMask, 48000));

    const int blocks = 40;
    std::vector<float> expectCh0;
    std::vector<float> expectCh2;
    std::vector<float> expectL;
    std::vector<float> expectR;
    for (int b = 0; b < blocks; ++b) {
        float in0[kBlockSize];
        float in2[kBlockSize];
        float silence[kBlockSize] = {};
        const float ramp = 0.25f + 0.001f * static_cast<float>(b);
        const float dip = -0.5f + 0.002f * static_cast<float>(b);
        fill(in0, ramp);
        fill(in2, dip);
        const float* inputs[kMaxInputs] = {};
        inputs[0] = &in0[0];
        inputs[1] = &silence[0];
        inputs[2] = &in2[0];
        float outL[kBlockSize];
        float outR[kBlockSize];
        engine.process(inputs, 3, kBlockSize, outL, outR);

        const float* taps[kMaxInputs];
        for (int c = 0; c < kMaxInputs; ++c) taps[c] = engine.tap(c);
        recorder.writeBlock(taps, outL, outR, kBlockSize);

        for (int i = 0; i < kBlockSize; ++i) {
            expectCh0.push_back(in0[i] * 0.5f);
            expectCh2.push_back(in2[i] * 1.0f);
            expectL.push_back(in0[i] * 0.5f * 0.8f);
            expectR.push_back(in2[i] * 1.0f * 0.8f);
        }
    }

    const Recorder::Take take = recorder.stopTake();
    CHECK(!recorder.recording());
    CHECK(take.drops == 0);
    CHECK(recorder.drops() == 0);
    CHECK(take.frames == static_cast<uint64_t>(blocks) * kBlockSize);
    CHECK(take.files == 3);

    const std::filesystem::path takeDir(take.path);
    CHECK(!take.path.empty());
    CHECK(takeDir.parent_path() == root);
    CHECK(takeDir.filename().string().rfind("take-", 0) == 0);
    CHECK(std::filesystem::exists(takeDir / "ch00.wav"));
    CHECK(std::filesystem::exists(takeDir / "ch02.wav"));
    CHECK(std::filesystem::exists(takeDir / "master.wav"));
    CHECK(!std::filesystem::exists(takeDir / "ch01.wav"));

    int wavFiles = 0;
    for (const auto& entry : std::filesystem::directory_iterator(takeDir)) {
        if (entry.path().extension() == ".wav") ++wavFiles;
    }
    CHECK(wavFiles == 3);

    const uint32_t frames = static_cast<uint32_t>(blocks) * static_cast<uint32_t>(kBlockSize);
    const WavInfo ch0 = readWav((takeDir / "ch00.wav").string());
    CHECK(ch0.ok);
    if (ch0.ok) {
        CHECK(ch0.format == 3);
        CHECK(ch0.channels == 1);
        CHECK(ch0.bits == 32);
        CHECK(ch0.sampleRate == 48000);
        CHECK(ch0.factFrames == frames);
        CHECK(ch0.dataBytes == frames * 4u);
        checkSamples("ch00 samples", ch0.samples, expectCh0);
    } else {
        std::printf("FAIL ch00.wav: %s\n", ch0.error.c_str());
        ++gFailures;
    }

    const WavInfo ch2 = readWav((takeDir / "ch02.wav").string());
    CHECK(ch2.ok);
    if (ch2.ok) {
        CHECK(ch2.format == 3);
        CHECK(ch2.channels == 1);
        CHECK(ch2.bits == 32);
        CHECK(ch2.sampleRate == 48000);
        CHECK(ch2.factFrames == frames);
        checkSamples("ch02 samples", ch2.samples, expectCh2);
    } else {
        std::printf("FAIL ch02.wav: %s\n", ch2.error.c_str());
        ++gFailures;
    }

    std::vector<float> expectInterleaved;
    expectInterleaved.reserve(expectL.size() * 2);
    for (size_t i = 0; i < expectL.size(); ++i) {
        expectInterleaved.push_back(expectL[i]);
        expectInterleaved.push_back(expectR[i]);
    }
    const WavInfo master = readWav((takeDir / "master.wav").string());
    CHECK(master.ok);
    if (master.ok) {
        CHECK(master.format == 3);
        CHECK(master.channels == 2);
        CHECK(master.bits == 32);
        CHECK(master.sampleRate == 48000);
        CHECK(master.factFrames == frames);
        CHECK(master.dataBytes == frames * 2u * 4u);
        checkSamples("master interleaved samples", master.samples, expectInterleaved);
    } else {
        std::printf("FAIL master.wav: %s\n", master.error.c_str());
        ++gFailures;
    }

    // The recorder is reusable after the take closes.
    Recorder fresh;
    std::filesystem::remove_all(root / "again", ec);
    CHECK(fresh.startTake((root / "again").string(), 1u << 1, 44100));
    std::vector<float> silence0(kBlockSize, 0.0f);
    std::vector<float> silence1(kBlockSize, 0.0f);
    const float* taps[kMaxInputs] = {};
    taps[1] = silence1.data();
    fresh.writeBlock(taps, silence0.data(), silence0.data(), kBlockSize);
    const Recorder::Take second = fresh.stopTake();
    CHECK(second.files == 2);
    CHECK(second.frames == static_cast<uint64_t>(kBlockSize));
    const WavInfo soloCh1 = readWav((std::filesystem::path(second.path) / "ch01.wav").string());
    CHECK(soloCh1.ok);
    if (soloCh1.ok) {
        CHECK(soloCh1.channels == 1);
        CHECK(soloCh1.sampleRate == 44100);
        CHECK(soloCh1.factFrames == static_cast<uint32_t>(kBlockSize));
    }

    // Empty arm mask is rejected outright.
    Recorder rejected;
    CHECK(!rejected.startTake(root.string(), 0u, 48000));
    CHECK(!rejected.recording());

    std::filesystem::remove_all(root, ec);
    std::printf("PASS audio recorder: float32 WAV take round-trip\n");
}

}  // namespace

int main() {
    testGainPan();
    testMuteSolo();
    testSendsAndFx();
    testTapProvenance();
    testRecorderRoundTrip();

    if (gFailures != 0) {
        std::printf("FAIL audio: %d check(s) failed\n", gFailures);
        return 1;
    }
    std::printf("PASS\n");
    return 0;
}
