#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mnky::audio::fx {

/// Per-block linear ramp used by every built-in FX parameter. `next(n)` is
/// called once at the top of a block and returns the block-constant value
/// applied for those `n` samples (the midpoint of the advanced segment), so
/// consecutive blocks differ by at most `delta * n / rampSamples`.
struct Ramp {
    double current = 0.0;
    double target = 0.0;
    /// Per-sample increment while `remaining > 0`.
    double step = 0.0;
    /// Samples left before `current` reaches `target`.
    int remaining = 0;

    /// Jump to `value` with no ramp (construction, reset, state restore).
    void snap(double value);

    /// Start or retarget a linear ramp lasting `rampSamples` samples. A
    /// non-positive length, or a target equal to `current`, snaps instead.
    void setTarget(double value, int rampSamples);

    /// Advance by `n` samples and return the value a block-constant stage
    /// should use for those samples. `n <= 0` does not advance.
    double next(int n);
};

/// Parameter metadata plus the control-thread shadow value. `def` is the
/// default (the word `default` is reserved); `value` is always within
/// [min, max] after construction and every accepted `setParam()`.
struct Param {
    std::string id;
    std::string name;
    double min = 0.0;
    double max = 1.0;
    double def = 0.0;
    double value = 0.0;
    /// Stepped parameters snap to whole numbers (type/model/division selectors).
    bool stepped = false;
};

/// One effect's serialized state. The emitted JSON is exactly:
///
///   {"id":"<effect-id>","v":<uint>,"bypass":<bool>,"params":[<double>,...]}
///
/// Key order is fixed, there is no whitespace and `params` is positional;
/// each effect header documents its stable indices. Appending a parameter is a
/// version bump, reordering an existing index is forbidden.
struct EffectState {
    std::string id;
    uint32_t version = 0;
    bool bypass = false;
    std::vector<double> params;

    /// Emits the fixed-order object above. Non-finite parameter values are
    /// invalid state and must be rejected by the caller before saving.
    std::string toJson() const;

    /// Parses exactly that shape. Whitespace and arbitrary key order are
    /// accepted; malformed JSON, unknown/missing/duplicate keys, non-finite
    /// numbers and trailing bytes are rejected. `out` is untouched on failure.
    static bool fromJson(const std::string& json, EffectState& out, std::string* error = nullptr);
};

/// Flush values near the denormal range so feedback tails never fall into slow
/// subnormal arithmetic. Both thresholds are far below any audible level
/// (-400 dBFS and below); NaN passes through unchanged.
inline float flushDenormal(float value) {
    return value > -1.0e-20f && value < 1.0e-20f ? 0.0f : value;
}

inline double flushDenormal(double value) {
    return value > -1.0e-30 && value < 1.0e-30 ? 0.0 : value;
}

}  // namespace mnky::audio::fx
