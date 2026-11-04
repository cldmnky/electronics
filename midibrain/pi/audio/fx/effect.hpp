#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "engine.hpp"
#include "params.hpp"

namespace mnky::audio::fx {

/// Common control surface for the built-in effects: stable parameter list,
/// block-rate smoothing, bypass and JSON-ready state. `process()` is final so
/// bypass always leaves the caller's buffers bit-exact; subclasses implement
/// `processBlock()`.
///
/// Threading matches the engine: parameter/state calls, setSampleRate() and
/// reset() are control-thread operations; process() is the audio thread. No
/// call allocates or locks once constructed.
class Effect : public IEffect {
public:
    /// Default smoothing time for every parameter.
    static constexpr double kSmoothingMs = 20.0;

    Effect(const Effect&) = delete;
    Effect& operator=(const Effect&) = delete;

    const char* name() const override { return mName.c_str(); }

    /// Stable registry id; this is the `id` written into EffectState JSON.
    const std::string& id() const { return mId; }
    /// State schema version; a mismatch is rejected by applyState().
    uint32_t version() const { return mVersion; }

    /// Metadata and current shadow values, in stable index order.
    const std::vector<Param>& params() const { return mParams; }

    /// Stable string id -> parameter index, or -1 when unknown.
    int paramIndex(const std::string& id) const;

    /// One X/Y performance pair. Both axes are stable parameter ids; an empty
    /// string means the axis is unassigned. The grid/gesture comes from the UI.
    struct XYMap {
        std::string x;
        std::string y;
        bool assigned() const { return !x.empty() && !y.empty(); }
    };
    static constexpr int kXYSlots = 2;

    /// Copy of an X/Y slot; an out-of-range slot yields an empty pair.
    XYMap xy(int slot) const;
    /// Assigns both axes by stable parameter id. Rejects a bad slot, an empty
    /// id, or an unknown id; the previous assignment is unchanged on failure.
    bool setXY(int slot, std::string xParamId, std::string yParamId);
    bool clearXY(int slot);
    /// Live-control entry point: each axis is normalized 0..1 and mapped to its
    /// parameter's [min, max] (stepped parameters are rounded). Returns false
    /// for a bad/unassigned slot or non-finite normalized values.
    bool setXYNormalized(int slot, double x, double y);

    /// Shadow value for `index`, or 0.0 for an out-of-range index.
    double param(int index) const;

    /// Clamps and stores a control value. The DSP follows through a 20 ms
    /// ramp once process() has run; before the first block it snaps. Returns
    /// false for an out-of-range index or a non-finite value.
    bool setParam(int index, double value);

    bool bypass() const { return mBypass; }
    /// Hard dry switch (bit-exact bypass, no ramp). Ramps for bypass
    /// transitions are a documented follow-up.
    void setBypass(bool bypass) { mBypass = bypass; }

    double sampleRate() const { return mSampleRate; }
    /// Control thread only. Re-allocates time-based buffers and resets DSP
    /// state; the accepted range is 8 kHz .. 192 kHz (outside is ignored).
    void setSampleRate(double sampleRate) override;

    /// Clears DSP state and snaps every parameter ramp to its shadow value.
    void reset();

    EffectState state() const;
    /// Restores id/version/params/bypass. Rejects a mismatched id, version or
    /// parameter count, and non-finite values; ranges are clamped. On success
    /// every ramp snaps, so a restored session does not sweep from defaults.
    bool applyState(const EffectState& state, std::string* error = nullptr);

    void process(float* const* ch, int nch, int n) final;

protected:
    Effect(std::string id, std::string name, uint32_t version, std::vector<Param> params);

    virtual void processBlock(float* const* ch, int nch, int n) = 0;
    /// Re-allocate/reset for the new sample rate. Called by setSampleRate().
    virtual void onSampleRateChanged() {}
    /// Clear delay lines and filter state. Called by reset().
    virtual void onReset() {}

    /// Advance one parameter by `frames` samples and return this block's value.
    double nextParam(int index, int frames);

private:
    int rampSamples() const;

    std::string mId;
    std::string mName;
    uint32_t mVersion = 1;
    std::vector<Param> mParams;
    std::vector<Ramp> mRamps;
    XYMap mXY[kXYSlots];
    double mSampleRate = 48000.0;
    bool mBypass = false;
    bool mStarted = false;
};

}  // namespace mnky::audio::fx
