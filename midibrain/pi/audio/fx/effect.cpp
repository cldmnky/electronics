#include "effect.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace mnky::audio::fx {
namespace {

double clampValue(double value, double min, double max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

// Stepped selectors (filter type, note division, reverb model, ...) are stored
// as whole numbers so saved state cannot carry an impossible 2.7 type.
double storedValue(const Param& param, double value) {
    double result = clampValue(value, param.min, param.max);
    if (param.stepped) result = std::round(result);
    return result;
}

bool finiteValue(double value) {
    return std::isfinite(value);
}

}  // namespace

Effect::Effect(std::string id, std::string name, uint32_t version, std::vector<Param> params)
    : mId(std::move(id)), mName(std::move(name)), mVersion(version), mParams(std::move(params)) {
    mRamps.resize(mParams.size());
    for (size_t i = 0; i < mParams.size(); ++i) {
        Param& param = mParams[i];
        if (!(param.min <= param.max)) std::swap(param.min, param.max);
        param.value = storedValue(param, param.def);
        mRamps[i].snap(param.value);
    }
}

double Effect::param(int index) const {
    if (index < 0 || index >= static_cast<int>(mParams.size())) return 0.0;
    return mParams[static_cast<size_t>(index)].value;
}

bool Effect::setParam(int index, double value) {
    if (index < 0 || index >= static_cast<int>(mParams.size())) return false;
    if (!finiteValue(value)) return false;
    Param& param = mParams[static_cast<size_t>(index)];
    const double clamped = storedValue(param, value);
    param.value = clamped;
    if (mStarted) {
        mRamps[static_cast<size_t>(index)].setTarget(clamped, rampSamples());
    } else {
        mRamps[static_cast<size_t>(index)].snap(clamped);
    }
    return true;
}

void Effect::setSampleRate(double sampleRate) {
    if (!finiteValue(sampleRate) || sampleRate < 8000.0 || sampleRate > 192000.0) return;
    if (sampleRate == mSampleRate) return;
    mSampleRate = sampleRate;
    for (size_t i = 0; i < mRamps.size(); ++i) mRamps[i].snap(mParams[i].value);
    onSampleRateChanged();
}

void Effect::reset() {
    for (size_t i = 0; i < mRamps.size(); ++i) mRamps[i].snap(mParams[i].value);
    onReset();
}

EffectState Effect::state() const {
    EffectState result;
    result.id = mId;
    result.version = mVersion;
    result.bypass = mBypass;
    result.params.reserve(mParams.size());
    for (const Param& param : mParams) result.params.push_back(param.value);
    return result;
}

bool Effect::applyState(const EffectState& state, std::string* error) {
    const auto fail = [error](const char* message) {
        if (error != nullptr) *error = message;
        return false;
    };
    if (state.id != mId) return fail("effect id mismatch");
    if (state.version != mVersion) return fail("effect state version mismatch");
    if (state.params.size() != mParams.size()) return fail("effect parameter count mismatch");
    for (const double value : state.params) {
        if (!finiteValue(value)) return fail("non-finite effect parameter");
    }

    for (size_t i = 0; i < mParams.size(); ++i) {
        Param& param = mParams[i];
        param.value = storedValue(param, state.params[i]);
        mRamps[i].snap(param.value);
    }
    mBypass = state.bypass;
    return true;
}

int Effect::paramIndex(const std::string& id) const {
    for (size_t i = 0; i < mParams.size(); ++i) {
        if (mParams[i].id == id) return static_cast<int>(i);
    }
    return -1;
}

Effect::XYMap Effect::xy(int slot) const {
    if (slot < 0 || slot >= kXYSlots) return XYMap{};
    return mXY[slot];
}

bool Effect::setXY(int slot, std::string xParamId, std::string yParamId) {
    if (slot < 0 || slot >= kXYSlots) return false;
    if (paramIndex(xParamId) < 0 || paramIndex(yParamId) < 0) return false;
    mXY[slot].x = std::move(xParamId);
    mXY[slot].y = std::move(yParamId);
    return true;
}

bool Effect::clearXY(int slot) {
    if (slot < 0 || slot >= kXYSlots) return false;
    mXY[slot] = XYMap{};
    return true;
}

bool Effect::setXYNormalized(int slot, double x, double y) {
    if (slot < 0 || slot >= kXYSlots) return false;
    if (!finiteValue(x) || !finiteValue(y)) return false;
    const int xIndex = paramIndex(mXY[slot].x);
    const int yIndex = paramIndex(mXY[slot].y);
    if (xIndex < 0 || yIndex < 0) return false;
    const double nx = x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x);
    const double ny = y < 0.0 ? 0.0 : (y > 1.0 ? 1.0 : y);
    const Param& xParam = mParams[static_cast<size_t>(xIndex)];
    const Param& yParam = mParams[static_cast<size_t>(yIndex)];
    const double xValue = xParam.min + nx * (xParam.max - xParam.min);
    const double yValue = yParam.min + ny * (yParam.max - yParam.min);
    const bool xOk = setParam(xIndex, xValue);
    const bool yOk = setParam(yIndex, yValue);
    return xOk && yOk;
}

void Effect::process(float* const* ch, int nch, int n) {
    mStarted = true;
    if (mBypass || ch == nullptr || nch <= 0 || n <= 0) return;
    processBlock(ch, nch, n);
}

double Effect::nextParam(int index, int frames) {
    if (index < 0 || index >= static_cast<int>(mRamps.size())) return 0.0;
    return mRamps[static_cast<size_t>(index)].next(frames);
}

int Effect::rampSamples() const {
    const int samples = static_cast<int>(std::lround(mSampleRate * kSmoothingMs * 0.001));
    return samples > 0 ? samples : 1;
}

}  // namespace mnky::audio::fx
