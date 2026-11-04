// Assert-style acceptance tests for the per-strip input source and output
// destination (no framework).
// Build: clang++ -std=c++20 -Wall -Wextra -Werror -I. test_audio_routing.cpp
//        engine.cpp take_player.cpp recorder.cpp click.cpp -o /tmp/t_routing
//        -pthread
//
// Covers the identity default, a remapped strip (2 <- device 5), the silent
// (-1) source, stereo-pair binding including the odd clamp-down and the balance
// law, the four output destinations (Main, OUT3&4 stereo and dual mono,
// Phones = CUE, None) and the rule that the recorder tap and the take voice
// follow the strip rather than the device index.

#include <algorithm>
#include <cmath>
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

// cos(pi/4): the constant-power pan gain of a centred strip.
constexpr double kCenterGain = 0.70710678118654752440;

void checkNear(const char* what, float actual, float expected, float eps = 1e-6f) {
    if (std::fabs(actual - expected) <= eps) return;
    std::printf("FAIL %s: actual %.9g expected %.9g\n", what, actual, expected);
    ++gFailures;
}

// All 18 device channels as constant buffers, so a test can say "device 5
// carries 0.25" without building 18 arrays by hand.
struct Devices {
    explicit Devices(int frames)
        : data(static_cast<std::size_t>(kMaxInputs),
               std::vector<float>(static_cast<std::size_t>(frames), 0.0f)) {
        for (int c = 0; c < kMaxInputs; ++c) {
            ptrs[c] = data[static_cast<std::size_t>(c)].data();
        }
    }
    void set(int channel, float value) {
        std::fill(data[static_cast<std::size_t>(channel)].begin(),
                  data[static_cast<std::size_t>(channel)].end(), value);
    }
    std::vector<std::vector<float>> data;
    const float* ptrs[kMaxInputs] = {};
};

// One rendered block on every output bus the engine renders.
struct Buses {
    float mainL[kBlockSize] = {};
    float mainR[kBlockSize] = {};
    float phonesL[kBlockSize] = {};
    float phonesR[kBlockSize] = {};
    float out34L[kBlockSize] = {};
    float out34R[kBlockSize] = {};
};

void runBlock(Engine& engine, const Devices& devices, Buses& buses) {
    OutputBuses outputs;
    outputs.mainL = buses.mainL;
    outputs.mainR = buses.mainR;
    outputs.phonesL = buses.phonesL;
    outputs.phonesR = buses.phonesR;
    outputs.out34L = buses.out34L;
    outputs.out34R = buses.out34R;
    engine.process(devices.ptrs, kMaxInputs, kBlockSize, outputs);
}

// Mutes every strip except `keep` and gives that one `strip`. Routing state
// (source/pair/destination) is deliberately not touched here: setChannel must
// not reset it.
void onlyStrip(Engine& engine, int keep, const Channel& strip) {
    for (int c = 0; c < kMaxInputs; ++c) {
        Channel ch;
        ch.mute = c != keep;
        engine.setChannel(c, ch);
    }
    engine.setChannel(keep, strip);
}

Channel unityStrip() {
    Channel strip;
    strip.gain = 1.0f;
    strip.vol = 1.0f;
    strip.pan = 0.5f;
    return strip;
}

// --------------------------------------------------------- identity defaults

void testIdentityDefault() {
    Engine engine;
    for (int c = 0; c < kMaxInputs; ++c) {
        CHECK(engine.inputSource(c) == c);  // strip N <- device N
        CHECK(!engine.inputPair(c));
        CHECK(engine.outputDestination(c) == OutputDestination::Main);
    }
    // Out-of-range strips read as "no source" and Main instead of reading past
    // the end of the arrays.
    CHECK(engine.inputSource(-1) == kNoInputSource);
    CHECK(engine.inputSource(kMaxInputs) == kNoInputSource);
    CHECK(!engine.inputPair(kMaxInputs));
    CHECK(engine.outputDestination(kMaxInputs) == OutputDestination::Main);

    Devices devices(kBlockSize);
    devices.set(5, 0.25f);
    Buses buses;
    runBlock(engine, devices, buses);
    checkNear("identity: device 5 reaches strip 5 (L)", buses.mainL[0],
              static_cast<float>(0.25 * kCenterGain));
    checkNear("identity: device 5 reaches strip 5 (R)", buses.mainR[0],
              static_cast<float>(0.25 * kCenterGain));

    std::printf("PASS routing: identity default mapping and getters\n");
}

// ----------------------------------------------------- source remap / silence

void testSourceRemap() {
    Engine engine;
    Channel strip = unityStrip();
    strip.gain = 0.5f;
    onlyStrip(engine, 2, strip);
    engine.setInputSource(2, 5);
    CHECK(engine.inputSource(2) == 5);

    // A blind strip refresh keeps the routing: source/pair/destination are not
    // Channel state.
    engine.setChannel(2, strip);
    CHECK(engine.inputSource(2) == 5);

    Devices devices(kBlockSize);
    devices.set(2, 0.75f);   // the old device channel is now ignored
    devices.set(5, 0.25f);
    Buses buses;
    runBlock(engine, devices, buses);
    // Strip 2 is the only unmuted strip and it is fed by device 5, gain 0.5.
    checkNear("remap: strip 2 plays device 5 (L)", buses.mainL[0],
              static_cast<float>(0.125 * kCenterGain));
    checkNear("remap: strip 2 plays device 5 (R)", buses.mainR[0],
              static_cast<float>(0.125 * kCenterGain));

    // The recorder tap follows the selection, at the strip's tapPoint.
    const float* tap = engine.tap(2);
    CHECK(tap != nullptr);
    if (tap != nullptr) checkNear("remap: tap carries device 5 * gain", tap[0], 0.125f);

    strip.tapPoint = TapPoint::PreGain;
    engine.setChannel(2, strip);
    runBlock(engine, devices, buses);
    tap = engine.tap(2);
    CHECK(tap != nullptr);
    if (tap != nullptr) checkNear("remap: PreGain tap carries device 5", tap[0], 0.25f);

    // A device channel that is not supplied reads as silence, not as the strip
    // index's old channel.
    engine.setInputSource(2, 17);
    Buses shortBus;
    OutputBuses outputs;
    outputs.mainL = shortBus.mainL;
    outputs.mainR = shortBus.mainR;
    engine.process(devices.ptrs, 6, kBlockSize, outputs);
    tap = engine.tap(2);
    CHECK(tap != nullptr);
    if (tap != nullptr) checkNear("remap: unsupplied source taps silence", tap[0], 0.0f);

    // Selection is clamped into 0..17, and -1 is the documented silent source.
    engine.setInputSource(2, 99);
    CHECK(engine.inputSource(2) == kMaxInputs - 1);
    engine.setInputSource(2, kNoInputSource);
    CHECK(engine.inputSource(2) == kNoInputSource);

    std::printf("PASS routing: strip remap 2 <- device 5\n");
}

void testNoneSilence() {
    Engine engine;
    Channel strip = unityStrip();
    strip.monitor = MonitorMode::On;  // even a forced monitor cannot invent audio
    strip.cueSend = 1.0f;
    strip.out34Send = 1.0f;
    strip.send[0] = 1.0f;
    onlyStrip(engine, 0, strip);
    engine.setInputSource(0, kNoInputSource);

    Devices devices(kBlockSize);
    devices.set(0, 1.0f);
    Buses buses;
    runBlock(engine, devices, buses);
    checkNear("none: Main stays silent (L)", buses.mainL[0], 0.0f);
    checkNear("none: Main stays silent (R)", buses.mainR[0], 0.0f);
    checkNear("none: phones/CUE stays silent", buses.phonesL[0], 0.0f);
    checkNear("none: OUT3&4 stays silent", buses.out34L[0], 0.0f);
    const float* tap = engine.tap(0);
    CHECK(tap != nullptr);
    if (tap != nullptr) checkNear("none: tap is silent", tap[0], 0.0f);

    // Re-selecting the identity channel restores the strip (with the sends
    // cleared, so Main carries the dry signal only).
    strip.cueSend = 0.0f;
    strip.out34Send = 0.0f;
    strip.send[0] = 0.0f;
    strip.monitor = MonitorMode::Auto;
    engine.setChannel(0, strip);
    engine.setInputSource(0, 0);
    runBlock(engine, devices, buses);
    checkNear("none: identity selection restores the strip", buses.mainL[0],
              static_cast<float>(kCenterGain));

    std::printf("PASS routing: silent source silences the live path\n");
}

// ------------------------------------------------------------ stereo pairs

void testPairBinding() {
    Engine engine;
    Channel strip = unityStrip();
    onlyStrip(engine, 3, strip);
    engine.setInputSource(3, 4);
    engine.setInputPair(3, true);
    CHECK(engine.inputSource(3) == 4);
    CHECK(engine.inputPair(3));

    Devices devices(kBlockSize);
    devices.set(3, 1.0f);  // not part of the pair, must be ignored
    devices.set(4, 0.25f);
    devices.set(5, -0.5f);
    Buses buses;
    runBlock(engine, devices, buses);
    // One strip, two device channels: centre balance keeps both at unity, so a
    // centred pair passes through unchanged instead of being panned by 0.707.
    checkNear("pair: L is device 4", buses.mainL[0], 0.25f);
    checkNear("pair: R is device 5", buses.mainR[0], -0.5f);

    const float* tap = engine.tap(3);
    CHECK(tap != nullptr);
    if (tap != nullptr) {
        checkNear("pair: the mono recorder tap is the pair average", tap[0],
                  0.5f * (0.25f - 0.5f));
    }

    // Odd start clamps down: 5 binds 4/5, not 5/6.
    engine.setInputSource(3, 5);
    Devices odd(kBlockSize);
    odd.set(4, 0.25f);
    odd.set(5, -0.5f);
    odd.set(6, 1.0f);
    runBlock(engine, odd, buses);
    checkNear("pair: odd start clamps down to device 4", buses.mainL[0], 0.25f);
    checkNear("pair: odd start clamps down to device 5", buses.mainR[0], -0.5f);
    // The stored selection stays exactly what was applied, so a session
    // round-trip does not rewrite it.
    CHECK(engine.inputSource(3) == 5);

    // The top channel pairs with the one below it instead of reading past 17.
    engine.setInputSource(3, kMaxInputs - 1);
    Devices top(kBlockSize);
    top.set(16, 0.125f);
    top.set(17, 0.25f);
    runBlock(engine, top, buses);
    checkNear("pair: 17/18 clamps to devices 16/17 (L)", buses.mainL[0], 0.125f);
    checkNear("pair: 17/18 clamps to devices 16/17 (R)", buses.mainR[0], 0.25f);

    // A silent source cannot be paired: -1 stays silent.
    engine.setInputSource(3, kNoInputSource);
    runBlock(engine, odd, buses);
    checkNear("pair: a silent source stays silent", buses.mainL[0], 0.0f);
    checkNear("pair: a silent source stays silent (R)", buses.mainR[0], 0.0f);

    std::printf("PASS routing: stereo-pair binding and odd clamp-down\n");
}

void testPairBalance() {
    Engine engine;
    Channel strip = unityStrip();
    onlyStrip(engine, 3, strip);
    engine.setInputSource(3, 4);
    engine.setInputPair(3, true);

    Devices devices(kBlockSize);
    devices.set(4, 0.5f);
    devices.set(5, 0.5f);

    Buses buses;
    strip.pan = 0.0f;
    engine.setChannel(3, strip);
    runBlock(engine, devices, buses);
    checkNear("balance: hard left keeps L", buses.mainL[0], 0.5f);
    checkNear("balance: hard left kills R", buses.mainR[0], 0.0f);

    strip.pan = 1.0f;
    engine.setChannel(3, strip);
    runBlock(engine, devices, buses);
    checkNear("balance: hard right kills L", buses.mainL[0], 0.0f);
    checkNear("balance: hard right keeps R", buses.mainR[0], 0.5f);

    strip.pan = 0.25f;
    engine.setChannel(3, strip);
    runBlock(engine, devices, buses);
    checkNear("balance: 0.25 keeps L at unity", buses.mainL[0], 0.5f);
    checkNear("balance: 0.25 fades R to half", buses.mainR[0], 0.25f);

    // A mono strip keeps the historical constant-power pan.
    Channel mono = unityStrip();
    mono.pan = 0.25f;
    onlyStrip(engine, 2, mono);
    Devices one(kBlockSize);
    one.set(2, 0.5f);
    runBlock(engine, one, buses);
    checkNear("balance: a mono strip keeps constant-power pan (L)",
              buses.mainL[0], static_cast<float>(0.5 * std::cos(0.25 * 1.57079632679489661923)));
    checkNear("balance: a mono strip keeps constant-power pan (R)",
              buses.mainR[0], static_cast<float>(0.5 * std::sin(0.25 * 1.57079632679489661923)));

    std::printf("PASS routing: stereo-pair balance law\n");
}

// ------------------------------------------------------- output destinations

void testOutputDestinations() {
    Engine engine;
    Channel strip = unityStrip();
    onlyStrip(engine, 0, strip);
    Devices devices(kBlockSize);
    devices.set(0, 0.5f);
    Buses buses;

    // Main 1&2: the historical behaviour.
    runBlock(engine, devices, buses);
    checkNear("destination Main: main L", buses.mainL[0],
              static_cast<float>(0.5 * kCenterGain));
    checkNear("destination Main: no OUT3&4", buses.out34L[0], 0.0f);
    checkNear("destination Main: no phones", buses.phonesL[0], 0.0f);

    // OUT3&4 stereo: the dry strip signal, uncompressed, never on Main.
    engine.setOutputDestination(0, OutputDestination::Out34);
    runBlock(engine, devices, buses);
    checkNear("destination OUT3&4: main is silent", buses.mainL[0], 0.0f);
    checkNear("destination OUT3&4: OUT3 = pan L", buses.out34L[0],
              static_cast<float>(0.5 * kCenterGain));
    checkNear("destination OUT3&4: OUT4 = pan R", buses.out34R[0],
              static_cast<float>(0.5 * kCenterGain));

    // OUT3&4 dual mono writes the unpanned mono signal to both jacks.
    engine.setOut34Mode(Out34Mode::DualMono);
    runBlock(engine, devices, buses);
    checkNear("destination OUT3&4 dual mono: OUT3", buses.out34L[0], 0.5f);
    checkNear("destination OUT3&4 dual mono: OUT4", buses.out34R[0], 0.5f);
    engine.setOut34Mode(Out34Mode::Stereo);

    // Phones is the CUE bus.
    engine.setOutputDestination(0, OutputDestination::Phones);
    runBlock(engine, devices, buses);
    checkNear("destination Phones: phones L", buses.phonesL[0],
              static_cast<float>(0.5 * kCenterGain));
    checkNear("destination Phones: phones R", buses.phonesR[0],
              static_cast<float>(0.5 * kCenterGain));
    checkNear("destination Phones: main is silent", buses.mainL[0], 0.0f);
    checkNear("destination Phones: no OUT3&4", buses.out34L[0], 0.0f);

    // Documented gap by name: the engine has no separate phones bus. With the
    // phones source set to Main the jack plays the compressed Main duplicate
    // and the CUE bus is replaced, so a Phones-routed strip is not audible
    // there.
    engine.setPhonesSource(PhonesSource::Main);
    runBlock(engine, devices, buses);
    checkNear("destination Phones, phones source Main: duplicate is Main (L)",
              buses.phonesL[0], 0.0f);
    engine.setPhonesSource(PhonesSource::Cue);

    // None: no dry destination at all, but the sends and the tap keep working.
    strip.cueSend = 0.5f;
    engine.setChannel(0, strip);
    engine.setOutputDestination(0, OutputDestination::None);
    runBlock(engine, devices, buses);
    checkNear("destination None: main is silent", buses.mainL[0], 0.0f);
    checkNear("destination None: OUT3&4 is silent", buses.out34L[0], 0.0f);
    checkNear("destination None: the CUE send still lands on phones",
              buses.phonesL[0], static_cast<float>(0.5 * kCenterGain * 0.5));
    const float* tap = engine.tap(0);
    CHECK(tap != nullptr);
    if (tap != nullptr) checkNear("destination None: the tap stays live", tap[0], 0.5f);

    // Out-of-range destinations fall back to Main instead of leaking into an
    // unnamed bus.
    engine.setOutputDestination(0, static_cast<OutputDestination>(9));
    CHECK(engine.outputDestination(0) == OutputDestination::Main);

    std::printf("PASS routing: output destination Main / OUT3&4 / Phones / None\n");
}

void testDestinationMuteGate() {
    Engine engine;
    Channel strip = unityStrip();
    strip.mute = true;
    onlyStrip(engine, 0, strip);
    engine.setOutputDestination(0, OutputDestination::Out34);

    Devices devices(kBlockSize);
    devices.set(0, 0.5f);
    Buses buses;
    runBlock(engine, devices, buses);
    checkNear("mute gates the OUT3&4 destination", buses.out34L[0], 0.0f);
    const float* tap = engine.tap(0);
    CHECK(tap != nullptr);
    if (tap != nullptr) checkNear("mute never gates the tap", tap[0], 0.5f);

    // Solo narrows the OUT3&4 destination the same way it narrows Main.
    strip.mute = false;
    engine.setChannel(0, strip);
    Channel other = unityStrip();
    other.solo = true;
    engine.setChannel(1, other);
    engine.setOutputDestination(1, OutputDestination::Out34);
    Devices both(kBlockSize);
    both.set(0, 0.5f);
    both.set(1, 0.25f);
    runBlock(engine, both, buses);
    checkNear("solo narrows the OUT3&4 destination", buses.out34L[0],
              static_cast<float>(0.25 * kCenterGain));

    std::printf("PASS routing: OUT3&4 dual mono, mute and solo gating\n");
}

void testMonitorAndPair() {
    Engine engine;
    Channel strip = unityStrip();
    strip.monitor = MonitorMode::Off;
    onlyStrip(engine, 3, strip);
    engine.setInputSource(3, 4);
    engine.setInputPair(3, true);

    Devices devices(kBlockSize);
    devices.set(4, 0.5f);
    devices.set(5, 0.25f);
    Buses buses;
    runBlock(engine, devices, buses);
    checkNear("pair + monitor off: no live pair on Main", buses.mainL[0], 0.0f);
    const float* tap = engine.tap(3);
    CHECK(tap != nullptr);
    if (tap != nullptr) checkNear("pair + monitor off: the tap stays live", tap[0], 0.375f);

    strip.monitor = MonitorMode::On;
    engine.setChannel(3, strip);
    runBlock(engine, devices, buses);
    checkNear("pair + monitor on: L passes", buses.mainL[0], 0.5f);
    checkNear("pair + monitor on: R passes", buses.mainR[0], 0.25f);

    std::printf("PASS routing: monitor and pair composition\n");
}

// ------------------------------------------- take voice and recorder taps

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

void testTakeFollowsStrip() {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("mnky_audio_routing_" + std::to_string(static_cast<long>(::getpid())));
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    CHECK(!ec);

    const std::string take = (root / "take.wav").string();
    // Four blocks long: every render below gets take frames, so a take that
    // ran out cannot be mistaken for a routing result.
    CHECK(writeWav(take, 48000, 1,
                   std::vector<float>(
                       static_cast<std::size_t>(4 * kBlockSize), 0.5f)));

    Engine engine;
    Channel strip = unityStrip();
    strip.pan = 0.0f;  // hard left, so Main L is the take level itself
    onlyStrip(engine, 2, strip);
    engine.setInputSource(2, 5);
    engine.setTransport(true, 0, 24, 12000);
    std::string error;
    CHECK(engine.setActiveTake(2, take, &error));
    CHECK(error.empty());

    Devices devices(kBlockSize);
    devices.set(2, 0.1f);  // the strip's old device channel
    devices.set(5, 0.9f);  // the selected source
    Buses buses;
    runBlock(engine, devices, buses);
    // Disarmed strip with a playing take and monitor Auto: the take voice only,
    // through the strip's own pan. The source select does not disable it.
    checkNear("take: the strip's take voice plays", buses.mainL[0], 0.5f);
    const float* tap = engine.tap(2);
    CHECK(tap != nullptr);
    if (tap != nullptr) {
        checkNear("take: the tap stays live and follows the selected source",
                  tap[0], 0.9f);
    }

    // A silent (-1) source silences the live input only: the take keeps
    // playing, matching the monitor rule that silence is about live input.
    engine.setInputSource(2, kNoInputSource);
    runBlock(engine, devices, buses);
    checkNear("take: a silent source keeps the take voice", buses.mainL[0], 0.5f);
    tap = engine.tap(2);
    CHECK(tap != nullptr);
    if (tap != nullptr) checkNear("take: a silent source taps silence", tap[0], 0.0f);

    // The take voice also follows the strip's output destination.
    engine.setInputSource(2, 5);
    engine.setOutputDestination(2, OutputDestination::Out34);
    runBlock(engine, devices, buses);
    checkNear("take: the take follows the strip destination (OUT3&4 L)",
              buses.out34L[0], 0.5f);
    checkNear("take: Main is silent after the destination change",
              buses.mainL[0], 0.0f);

    engine.clearActiveTake(2);
    std::filesystem::remove_all(root, ec);
    std::printf("PASS routing: recorder tap and take voice follow the strip\n");
}

}  // namespace

int main() {
    testIdentityDefault();
    testSourceRemap();
    testNoneSilence();
    testPairBinding();
    testPairBalance();
    testOutputDestinations();
    testDestinationMuteGate();
    testMonitorAndPair();
    testTakeFollowsStrip();

    if (gFailures != 0) {
        std::printf("%d routing check(s) failed\n", gFailures);
        return 1;
    }
    std::printf("PASS audio routing: per-strip source and destination\n");
    return 0;
}
