// Engine capacity benchmark (audio wave 10): the real-time budget for 64
// mixer tracks at 48 kHz. Measurement tool, not a test: it prints a table and
// exits 0. Run it on the target (studio Pi 5, Cortex-A76) before quoting any
// number; the numbers on a dev machine are not the product budget.
//
// Scenarios cover the mixing path the owner asked about (gain + pan + 4-band
// EQ + sends), the skip-inactive fast path, and the per-track insert limit in
// the 64-track context. Block sizes are the engine's supported 64 and 128
// frames; the owner's 64-frame question is answered by the first column.
//
// Build: part of the audio module (mnky_audio_bench). Usage:
//   mnky_audio_bench [blocks-per-row] [warmup-blocks]

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "engine.hpp"
#include "fx/delay.hpp"
#include "fx/reverb.hpp"

namespace {

using namespace mnky::audio;

constexpr double kSampleRate = 48000.0;

class Noise {
public:
    explicit Noise(std::uint32_t seed = 1u) : mState(seed) {}

    float next() {
        mState = mState * 1664525u + 1013904223u;
        return static_cast<float>(
            static_cast<double>(mState >> 8) / 8388608.0 - 1.0);
    }

private:
    std::uint32_t mState;
};

struct Scenario {
    const char* name;
    int tracks = kMaxTracks;
    bool stereo = true;
    bool eq = true;
    bool sends = true;
    bool active = true;
    int reverbs = 0;
    int delays = 0;
};

struct Timing {
    double usPerBlock = 0.0;
};

TrackInsert delayInsert() {
    TrackInsert insert;
    insert.id = "builtin.delay";
    insert.params = {250.0, 0.35, 0.3, 0.0, 3.0, 0.0, 2000.0, 1.0};
    return insert;
}

TrackInsert reverbInsert() {
    TrackInsert insert;
    insert.id = "builtin.reverb";
    insert.params = {20.0, 0.7, 0.5, 0.25, 0.0, 1.0};
    return insert;
}

void configureEq(Engine& engine, int track) {
    EqBand band;
    band.q = 0.707f;
    band.type = 1;  // low cut
    band.freqHz = 80.0f;
    engine.setTrackEqBand(track, 0, band);
    band.type = 2;  // low shelf
    band.freqHz = 200.0f;
    band.gainDb = 3.0f;
    engine.setTrackEqBand(track, 1, band);
    band.type = 3;  // parametric
    band.freqHz = 1000.0f;
    band.gainDb = -2.0f;
    engine.setTrackEqBand(track, 2, band);
    band.type = 4;  // high shelf
    band.freqHz = 8000.0f;
    band.gainDb = 3.0f;
    engine.setTrackEqBand(track, 3, band);
}

Timing measure(const Scenario& scenario, int blockFrames, int blocks,
               int warmup) {
    Engine engine;
    std::vector<std::vector<float>> deviceInput(
        static_cast<std::size_t>(kMaxDeviceInputs),
        std::vector<float>(static_cast<std::size_t>(blockFrames), 0.0f));
    Noise noise(7u);
    for (auto& buffer : deviceInput) {
        for (float& value : buffer) value = 0.2f * noise.next();
    }

    for (int track = 0; track < scenario.tracks; ++track) {
        Channel channel;
        channel.gain = 1.0f;
        channel.vol = 0.8f;
        channel.pan = 0.5f;
        channel.monitor = MonitorMode::On;
        if (scenario.sends) {
            channel.send[0] = 0.5f;
            channel.send[1] = 0.25f;
            channel.cueSend = 0.25f;
            channel.out34Send = 0.25f;
        }
        engine.setChannel(track, channel);
        if (scenario.active) {
            const int pairCount = kMaxDeviceInputs / 2;
            const int source = scenario.stereo
                                   ? 2 * (track % pairCount)
                                   : track % kMaxDeviceInputs;
            engine.setInputSource(track, source);
            engine.setInputPair(track, scenario.stereo);
        } else {
            engine.setInputSource(track, kNoInputSource);
            engine.setInputPair(track, false);
        }
        engine.setOutputDestination(
            track, track % 3 == 0
                       ? OutputDestination::Out34
                       : (track % 3 == 1 ? OutputDestination::Phones
                                         : OutputDestination::Main));
        if (scenario.eq) configureEq(engine, track);
        if (track < scenario.reverbs) {
            engine.setTrackInsert(track, reverbInsert());
        } else if (track < scenario.reverbs + scenario.delays) {
            engine.setTrackInsert(track, delayInsert());
        }
    }

    const float* inputs[kMaxDeviceInputs] = {};
    for (int device = 0; device < kMaxDeviceInputs; ++device) {
        inputs[device] = deviceInput[static_cast<std::size_t>(device)].data();
    }
    std::vector<float> mainL(static_cast<std::size_t>(blockFrames));
    std::vector<float> mainR(static_cast<std::size_t>(blockFrames));
    std::vector<float> phonesL(static_cast<std::size_t>(blockFrames));
    std::vector<float> phonesR(static_cast<std::size_t>(blockFrames));
    std::vector<float> out34L(static_cast<std::size_t>(blockFrames));
    std::vector<float> out34R(static_cast<std::size_t>(blockFrames));
    OutputBuses buses;
    buses.mainL = mainL.data();
    buses.mainR = mainR.data();
    buses.phonesL = phonesL.data();
    buses.phonesR = phonesR.data();
    buses.out34L = out34L.data();
    buses.out34R = out34R.data();

    for (int block = 0; block < warmup; ++block) {
        engine.process(inputs, kMaxDeviceInputs, blockFrames, buses);
    }
    const auto start = std::chrono::steady_clock::now();
    for (int block = 0; block < blocks; ++block) {
        engine.process(inputs, kMaxDeviceInputs, blockFrames, buses);
    }
    const auto stop = std::chrono::steady_clock::now();
    Timing timing;
    timing.usPerBlock =
        std::chrono::duration<double>(stop - start).count() * 1e6 /
        static_cast<double>(blocks);
    return timing;
}

void printRow(const Scenario& scenario, int blocks, int warmup) {
    const double budget64 = 64.0 / kSampleRate * 1e6;
    const double budget128 = 128.0 / kSampleRate * 1e6;
    const Timing small = measure(scenario, 64, blocks, warmup);
    const Timing large = measure(scenario, 128, blocks, warmup);
    std::printf("%-44s %8.1f us %5.1f%%   %8.1f us %5.1f%%\n", scenario.name,
                small.usPerBlock, 100.0 * small.usPerBlock / budget64,
                large.usPerBlock, 100.0 * large.usPerBlock / budget128);
    std::fflush(stdout);
}

}  // namespace

int main(int argc, char** argv) {
    int blocks = 1000;
    int warmup = 50;
    if (argc > 1) blocks = std::max(1, std::atoi(argv[1]));
    if (argc > 2) warmup = std::max(0, std::atoi(argv[2]));

    std::printf(
        "mnky audio engine bench: 48 kHz, %d blocks/row, %d warmup; budget "
        "64 frames = 1333 us, 128 frames = 2667 us\n",
        blocks, warmup);
    std::printf("%-44s %16s %16s\n", "scenario", "64-frame block",
                "128-frame block");

    Scenario reference;
    reference.name = "18 stereo active, gain+pan+EQ+sends";
    reference.tracks = kMaxDeviceInputs;
    printRow(reference, blocks, warmup);

    Scenario full;
    full.name = "64 stereo active, gain+pan+EQ+sends";
    printRow(full, blocks, warmup);

    Scenario noEq;
    noEq.name = "64 stereo active, gain+pan+sends (no EQ)";
    noEq.eq = false;
    printRow(noEq, blocks, warmup);

    Scenario plain;
    plain.name = "64 stereo active, gain+pan only";
    plain.eq = false;
    plain.sends = false;
    printRow(plain, blocks, warmup);

    Scenario idle;
    idle.name = "64 stereo inactive (skip path)";
    idle.active = false;
    idle.eq = false;
    idle.sends = false;
    printRow(idle, blocks, warmup);

    Scenario idleConfigured;
    idleConfigured.name = "64 inactive, EQ+sends configured (skip)";
    idleConfigured.active = false;
    printRow(idleConfigured, blocks, warmup);

    Scenario idleInserts;
    idleInserts.name = "64 inactive + 64 reverb inserts (skip)";
    idleInserts.active = false;
    idleInserts.reverbs = kMaxTracks;
    printRow(idleInserts, blocks, warmup);

    Scenario mono;
    mono.name = "64 mono active, gain+pan+EQ+sends";
    mono.stereo = false;
    printRow(mono, blocks, warmup);

    Scenario reverbs;
    reverbs.name = "64 stereo active + 64 reverb inserts";
    reverbs.reverbs = kMaxTracks;
    printRow(reverbs, blocks, warmup);

    Scenario reverbs32;
    reverbs32.name = "64 stereo active + 32 reverb inserts";
    reverbs32.reverbs = 32;
    printRow(reverbs32, blocks, warmup);

    Scenario delays64;
    delays64.name = "64 stereo active + 64 delay inserts";
    delays64.delays = kMaxTracks;
    printRow(delays64, blocks, warmup);

    return 0;
}
