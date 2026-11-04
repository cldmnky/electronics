// Assert-style acceptance tests for mnky::audio::ClickSource (no framework).
// Build: clang++ -std=c++20 -Wall -Wextra -Werror -I. test_click.cpp click.cpp -o /tmp/t_click && /tmp/t_click

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

#include "click.hpp"

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

// 1600 frames: longer than the 1440-frame (30 ms) pip at 48 kHz.
constexpr int kBlock = 1600;

void checkNear(const char* what, float actual, float expected, float eps = 1e-6f) {
    if (std::fabs(actual - expected) <= eps) return;
    std::printf("FAIL %s: actual %.9g expected %.9g\n", what, actual, expected);
    ++gFailures;
}

int zeroCrossings(const std::vector<float>& samples) {
    int crossings = 0;
    int lastSign = 0;
    for (float sample : samples) {
        const int sign = sample > 0.0f ? 1 : (sample < 0.0f ? -1 : 0);
        if (sign == 0) continue;
        if (lastSign != 0 && sign != lastSign) ++crossings;
        lastSign = sign;
    }
    return crossings;
}

bool allZero(const std::vector<float>& samples, size_t from, size_t to) {
    for (size_t i = from; i < to && i < samples.size(); ++i) {
        if (samples[i] != 0.0f) return false;
    }
    return true;
}

bool anyNonZero(const std::vector<float>& samples, size_t from, size_t to) {
    for (size_t i = from; i < to && i < samples.size(); ++i) {
        if (samples[i] != 0.0f) return true;
    }
    return false;
}

std::vector<float> renderAt(uint32_t tick, uint32_t ticksPerBeat) {
    ClickSource click;
    click.setEnabled(true);
    click.setLevel(1.0f);
    std::vector<float> out(static_cast<size_t>(kBlock), 123.0f);
    click.render(out.data(), kBlock, tick, ticksPerBeat, true);
    return out;
}

// ------------------------------------------------- accent / downbeat placement

void testAccentAndDownbeatPlacement() {
    constexpr uint32_t kTicksPerBeat = 24;  // midi::kTicksPerQuarter

    // Bar 1 beat 0 is the accent: 2 kHz, so ~120 zero crossings in 30 ms.
    const std::vector<float> downbeat = renderAt(0, kTicksPerBeat);
    CHECK(downbeat[0] == 0.0f);  // the sine starts at phase 0
    CHECK(anyNonZero(downbeat, 0, 1440));
    CHECK(allZero(downbeat, 1440, kBlock));  // the 30 ms pip ends hard
    const int accentCrossings = zeroCrossings(downbeat);
    CHECK(accentCrossings >= 118 && accentCrossings <= 121);

    // Beats 1 and 2 are plain 1 kHz pips: ~60 crossings.
    for (uint32_t beat : {1u, 2u}) {
        const std::vector<float> plain = renderAt(beat * kTicksPerBeat, kTicksPerBeat);
        CHECK(anyNonZero(plain, 0, 1440));
        CHECK(allZero(plain, 1440, kBlock));
        const int crossings = zeroCrossings(plain);
        CHECK(crossings >= 58 && crossings <= 61);
    }

    // Bar 2 beat 0 accents again (tick 96), and beat 3 of bar 2 stays plain.
    const std::vector<float> secondDownbeat = renderAt(4 * kTicksPerBeat, kTicksPerBeat);
    const int secondAccentCrossings = zeroCrossings(secondDownbeat);
    CHECK(secondAccentCrossings >= 118 && secondAccentCrossings <= 121);
    const std::vector<float> beat3 = renderAt(7 * kTicksPerBeat, kTicksPerBeat);
    const int beat3Crossings = zeroCrossings(beat3);
    CHECK(beat3Crossings >= 58 && beat3Crossings <= 61);

    // The exponential decay is steep: the tail is far below the onset.
    float onsetPeak = 0.0f;
    float tailPeak = 0.0f;
    for (int i = 0; i < 100; ++i) onsetPeak = std::max(onsetPeak, std::fabs(downbeat[i]));
    for (int i = 1340; i < 1440; ++i) tailPeak = std::max(tailPeak, std::fabs(downbeat[i]));
    CHECK(onsetPeak > 0.9f);
    CHECK(tailPeak < 0.02f);

    std::printf("PASS click: accent/downbeat placement and pip length\n");
}

// ------------------------------------------------------------- silence cases

void testSilenceCases() {
    std::vector<float> out(static_cast<size_t>(kBlock), 123.0f);

    // Disabled, even while rolling on a downbeat.
    ClickSource disabled;
    disabled.setEnabled(false);
    disabled.setLevel(1.0f);
    disabled.render(out.data(), kBlock, 0, 24, true);
    CHECK(allZero(out, 0, kBlock));

    // Enabled but the transport is stopped.
    out.assign(static_cast<size_t>(kBlock), 123.0f);
    ClickSource stopped;
    stopped.setEnabled(true);
    stopped.setLevel(1.0f);
    stopped.render(out.data(), kBlock, 0, 24, false);
    CHECK(allZero(out, 0, kBlock));

    // Enabled and rolling, but the first call is mid-beat: no click until the
    // next beat boundary.
    out.assign(static_cast<size_t>(kBlock), 123.0f);
    ClickSource midBeat;
    midBeat.setEnabled(true);
    midBeat.setLevel(1.0f);
    midBeat.render(out.data(), kBlock, 12, 24, true);
    CHECK(allZero(out, 0, kBlock));

    // A zero ticks-per-beat has no beat to place a pip on.
    out.assign(static_cast<size_t>(kBlock), 123.0f);
    ClickSource noBeat;
    noBeat.setEnabled(true);
    noBeat.setLevel(1.0f);
    noBeat.render(out.data(), kBlock, 0, 0, true);
    CHECK(allZero(out, 0, kBlock));

    // Stopping cuts the tail instead of leaving it ringing.
    ClickSource stopping;
    stopping.setEnabled(true);
    stopping.setLevel(1.0f);
    std::vector<float> head(static_cast<size_t>(kBlock), 0.0f);
    stopping.render(head.data(), 100, 0, 24, true);
    CHECK(anyNonZero(head, 0, 100));
    std::vector<float> afterStop(static_cast<size_t>(kBlock), 123.0f);
    stopping.render(afterStop.data(), kBlock, 0, 24, false);
    CHECK(allZero(afterStop, 0, kBlock));

    // Resuming on the same downbeat starts a fresh pip (the stop reset the
    // beat tracker).
    std::vector<float> resumed(static_cast<size_t>(kBlock), 0.0f);
    stopping.render(resumed.data(), kBlock, 0, 24, true);
    CHECK(anyNonZero(resumed, 0, 1440));

    // setEnabled(false) also clears the running pip.
    ClickSource disabling;
    disabling.setEnabled(true);
    disabling.setLevel(1.0f);
    std::vector<float> beforeDisable(static_cast<size_t>(kBlock), 0.0f);
    disabling.render(beforeDisable.data(), 100, 0, 24, true);
    CHECK(anyNonZero(beforeDisable, 0, 100));
    disabling.setEnabled(false);
    std::vector<float> afterDisable(static_cast<size_t>(kBlock), 123.0f);
    disabling.render(afterDisable.data(), kBlock, 0, 24, true);
    CHECK(allZero(afterDisable, 0, kBlock));

    // A repeated tick never retriggers: the first block already consumed the
    // whole pip.
    ClickSource repeated;
    repeated.setEnabled(true);
    repeated.setLevel(1.0f);
    std::vector<float> first(static_cast<size_t>(1440), 0.0f);
    repeated.render(first.data(), 1440, 0, 24, true);
    CHECK(anyNonZero(first, 0, 1440));
    std::vector<float> second(static_cast<size_t>(kBlock), 123.0f);
    repeated.render(second.data(), kBlock, 0, 24, true);
    CHECK(allZero(second, 0, kBlock));

    std::printf("PASS click: silence unless enabled and rolling\n");
}

// --------------------------------------------------------------- level scaling

void testLevelScaling() {
    auto renderLevel = [](float level) {
        ClickSource click;
        click.setEnabled(true);
        click.setLevel(level);
        std::vector<float> out(200, 0.0f);
        click.render(out.data(), 200, 0, 24, true);
        return out;
    };

    const std::vector<float> unity = renderLevel(1.0f);
    const std::vector<float> quarter = renderLevel(0.25f);
    CHECK(anyNonZero(unity, 0, 200));
    for (size_t i = 0; i < unity.size(); ++i) {
        checkNear("click level scales linearly", quarter[i], unity[i] * 0.25f, 1e-7f);
    }

    // Above 1 clamps to unity; zero, negative and NaN are silent.
    const std::vector<float> clamped = renderLevel(2.0f);
    for (size_t i = 0; i < unity.size(); ++i) {
        checkNear("click level clamps to 1", clamped[i], unity[i]);
    }
    const std::vector<float> zero = renderLevel(0.0f);
    CHECK(allZero(zero, 0, zero.size()));
    const std::vector<float> negative = renderLevel(-1.0f);
    CHECK(allZero(negative, 0, negative.size()));
    const std::vector<float> nan = renderLevel(std::numeric_limits<float>::quiet_NaN());
    CHECK(allZero(nan, 0, nan.size()));
    // A subnormal level must not leak denormals onto the bus.
    const std::vector<float> denormal = renderLevel(std::numeric_limits<float>::denorm_min());
    CHECK(allZero(denormal, 0, denormal.size()));

    std::printf("PASS click: level scaling and clamping\n");
}

}  // namespace

int main() {
    testAccentAndDownbeatPlacement();
    testSilenceCases();
    testLevelScaling();

    if (gFailures != 0) {
        std::printf("FAIL click: %d check(s) failed\n", gFailures);
        return 1;
    }
    std::printf("PASS\n");
    return 0;
}
