#pragma once
// Single source of the RBJ-style biquad math used by the EQ. The FX-suite
// `fx::Eq` effect and the engine's per-strip/main four-band inserts both build
// their coefficients here; do not fork these formulas into another copy.
//
// `makeBand(type, ...)` maps the stable band-type integer used by the session
// EQ contract (0 none, 1 low cut, 2 low shelf, 3 parametric, 4 high shelf,
// 5 high cut). A none band is the identity biquad.

#include <cmath>

#include "params.hpp"

namespace mnky::audio::fx {

inline constexpr double kBiquadPi = 3.14159265358979323846;

struct Biquad {
    double b0 = 1.0;
    double b1 = 0.0;
    double b2 = 0.0;
    double a1 = 0.0;
    double a2 = 0.0;
};

struct BiquadState {
    double x1 = 0.0;
    double x2 = 0.0;
    double y1 = 0.0;
    double y2 = 0.0;
};

inline int bandTypeIndex(int type) {
    if (type < 0) return 0;
    return type > 5 ? 5 : type;
}

inline Biquad normalized(double b0, double b1, double b2, double a0, double a1, double a2) {
    const double inverse = 1.0 / a0;
    Biquad result;
    result.b0 = b0 * inverse;
    result.b1 = b1 * inverse;
    result.b2 = b2 * inverse;
    result.a1 = a1 * inverse;
    result.a2 = a2 * inverse;
    return result;
}

inline Biquad lowCut(double sampleRate, double freq, double q) {
    const double w0 = 2.0 * kBiquadPi * freq / sampleRate;
    const double cosw = std::cos(w0);
    const double sinw = std::sin(w0);
    const double alpha = sinw / (2.0 * (q < 0.1 ? 0.1 : q));
    const double b0 = (1.0 + cosw) * 0.5;
    const double b1 = -(1.0 + cosw);
    const double b2 = (1.0 + cosw) * 0.5;
    const double a0 = 1.0 + alpha;
    const double a1 = -2.0 * cosw;
    const double a2 = 1.0 - alpha;
    return normalized(b0, b1, b2, a0, a1, a2);
}

inline Biquad highCut(double sampleRate, double freq, double q) {
    const double w0 = 2.0 * kBiquadPi * freq / sampleRate;
    const double cosw = std::cos(w0);
    const double sinw = std::sin(w0);
    const double alpha = sinw / (2.0 * (q < 0.1 ? 0.1 : q));
    const double b0 = (1.0 - cosw) * 0.5;
    const double b1 = 1.0 - cosw;
    const double b2 = (1.0 - cosw) * 0.5;
    const double a0 = 1.0 + alpha;
    const double a1 = -2.0 * cosw;
    const double a2 = 1.0 - alpha;
    return normalized(b0, b1, b2, a0, a1, a2);
}

inline double clampBiquad(double value, double min, double max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

inline Biquad lowShelf(double sampleRate, double freq, double gainDb, double q) {
    const double a = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * kBiquadPi * freq / sampleRate;
    const double cosw = std::cos(w0);
    const double sinw = std::sin(w0);
    const double slope = clampBiquad(q, 0.1, 1.0);
    const double alpha = sinw * 0.5 * std::sqrt((a + 1.0 / a) * (1.0 / slope - 1.0) + 2.0);
    const double beta = 2.0 * std::sqrt(a) * alpha;
    const double b0 = a * ((a + 1.0) - (a - 1.0) * cosw + beta);
    const double b1 = 2.0 * a * ((a - 1.0) - (a + 1.0) * cosw);
    const double b2 = a * ((a + 1.0) - (a - 1.0) * cosw - beta);
    const double a0 = (a + 1.0) + (a - 1.0) * cosw + beta;
    const double a1 = -2.0 * ((a - 1.0) + (a + 1.0) * cosw);
    const double a2 = (a + 1.0) + (a - 1.0) * cosw - beta;
    return normalized(b0, b1, b2, a0, a1, a2);
}

inline Biquad highShelf(double sampleRate, double freq, double gainDb, double q) {
    const double a = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * kBiquadPi * freq / sampleRate;
    const double cosw = std::cos(w0);
    const double sinw = std::sin(w0);
    const double slope = clampBiquad(q, 0.1, 1.0);
    const double alpha = sinw * 0.5 * std::sqrt((a + 1.0 / a) * (1.0 / slope - 1.0) + 2.0);
    const double beta = 2.0 * std::sqrt(a) * alpha;
    const double b0 = a * ((a + 1.0) + (a - 1.0) * cosw + beta);
    const double b1 = -2.0 * a * ((a - 1.0) + (a + 1.0) * cosw);
    const double b2 = a * ((a + 1.0) + (a - 1.0) * cosw - beta);
    const double a0 = (a + 1.0) - (a - 1.0) * cosw + beta;
    const double a1 = 2.0 * ((a - 1.0) - (a + 1.0) * cosw);
    const double a2 = (a + 1.0) - (a - 1.0) * cosw - beta;
    return normalized(b0, b1, b2, a0, a1, a2);
}

inline Biquad peaking(double sampleRate, double freq, double q, double gainDb) {
    const double a = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * kBiquadPi * freq / sampleRate;
    const double cosw = std::cos(w0);
    const double sinw = std::sin(w0);
    const double alpha = sinw / (2.0 * (q < 0.1 ? 0.1 : q));
    const double b0 = 1.0 + alpha * a;
    const double b1 = -2.0 * cosw;
    const double b2 = 1.0 - alpha * a;
    const double a0 = 1.0 + alpha / a;
    const double a1 = -2.0 * cosw;
    const double a2 = 1.0 - alpha / a;
    return normalized(b0, b1, b2, a0, a1, a2);
}

inline Biquad makeBand(int type, double sampleRate, double freq, double gainDb, double q) {
    const double limit = sampleRate * 0.45;
    const double f = freq < 10.0 ? 10.0 : (freq > limit ? limit : freq);
    switch (bandTypeIndex(type)) {
        case 1: return lowCut(sampleRate, f, q);
        case 2: return lowShelf(sampleRate, f, gainDb, q);
        case 3: return peaking(sampleRate, f, q, gainDb);
        case 4: return highShelf(sampleRate, f, gainDb, q);
        case 5: return highCut(sampleRate, f, q);
        default: return Biquad{};
    }
}

inline double run(const Biquad& biquad, BiquadState& state, double x) {
    const double y = biquad.b0 * x + biquad.b1 * state.x1 + biquad.b2 * state.x2 - biquad.a1 * state.y1 -
                     biquad.a2 * state.y2;
    state.x2 = state.x1;
    state.x1 = x;
    state.y2 = state.y1;
    state.y1 = y;
    return y;
}

inline void flushState(BiquadState& state) {
    state.x1 = flushDenormal(state.x1);
    state.x2 = flushDenormal(state.x2);
    state.y1 = flushDenormal(state.y1);
    state.y2 = flushDenormal(state.y2);
}

}  // namespace mnky::audio::fx
