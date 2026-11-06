#include "fx_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#include "../audio/fx/delay.hpp"
#include "../audio/fx/reverb.hpp"

namespace mnky::app {
namespace {

// Stable-index tables. Every row's `index` names the constant from the effect
// header, so a reordered or removed index breaks this file instead of silently
// moving a UI control onto another parameter.
constexpr std::array<FxParamMeta, audio::fx::Delay::kParamCount> kDelayParams{{
    {audio::fx::Delay::kTimeMs, "time_ms", "TIME", " ms", 1.0,
     audio::fx::Delay::kMaxTimeMs, 250.0, FxParamFormat::Number, 0, false},
    {audio::fx::Delay::kFeedback, "feedback", "FEEDBACK", "", 0.0, 0.95, 0.35,
     FxParamFormat::Percent, 0, false},
    {audio::fx::Delay::kMix, "mix", "MIX", "", 0.0, 1.0, 0.3,
     FxParamFormat::Percent, 0, false},
    {audio::fx::Delay::kBeatMode, "beat_mode", "BEAT", "", 0.0, 1.0, 0.0,
     FxParamFormat::OnOff, 0, true},
    {audio::fx::Delay::kDivision, "division", "DIVISION", "", 0.0,
     static_cast<double>(audio::fx::Delay::kDivCount - 1),
     static_cast<double>(audio::fx::Delay::kDiv1_4), FxParamFormat::Division, 0,
     true},
    {audio::fx::Delay::kPingPong, "ping_pong", "PING-PONG", "", 0.0, 1.0, 0.0,
     FxParamFormat::OnOff, 0, true},
    {audio::fx::Delay::kBpCutoffHz, "bp_cutoff_hz", "CUTOFF", " Hz", 20.0,
     20000.0, 1000.0, FxParamFormat::Hertz, 0, false},
    {audio::fx::Delay::kBpWidthOct, "bp_width_oct", "WIDTH", " oct", 0.1, 4.0,
     1.0, FxParamFormat::Number, 2, false},
}};

constexpr std::array<FxParamMeta, audio::fx::Reverb::kParamCount> kReverbParams{{
    {audio::fx::Reverb::kPredelayMs, "predelay_ms", "PREDELAY", " ms", 0.0, 200.0,
     20.0, FxParamFormat::Number, 0, false},
    {audio::fx::Reverb::kDecay, "decay", "DECAY", "", 0.0, 0.98, 0.7,
     FxParamFormat::Percent, 0, false},
    {audio::fx::Reverb::kDamping, "damping", "DAMPING", "", 0.0, 1.0, 0.5,
     FxParamFormat::Percent, 0, false},
    {audio::fx::Reverb::kMix, "mix", "MIX", "", 0.0, 1.0, 0.25,
     FxParamFormat::Percent, 0, false},
    {audio::fx::Reverb::kModel, "model", "MODEL", "", 0.0,
     static_cast<double>(audio::fx::Reverb::kModelCount - 1), 0.0,
     FxParamFormat::Model, 0, true},
    {audio::fx::Reverb::kSize, "size", "SIZE", "", 0.25, 2.0, 1.0,
     FxParamFormat::Number, 2, false},
}};

// The table's position is the parameter's stable index: a row inserted out of
// order would make every positional read (session, engine, UI) disagree.
static_assert(kDelayParams.size() == audio::fx::Delay::kParamCount);
static_assert(kReverbParams.size() == audio::fx::Reverb::kParamCount);
static_assert(kDelayParams[audio::fx::Delay::kBpCutoffHz].index ==
              audio::fx::Delay::kBpCutoffHz);
static_assert(kDelayParams[audio::fx::Delay::kBpWidthOct].index ==
              audio::fx::Delay::kBpWidthOct);
static_assert(kReverbParams[audio::fx::Reverb::kSize].index ==
              audio::fx::Reverb::kSize);
static_assert(kReverbParams[audio::fx::Reverb::kMix].index ==
              audio::fx::Reverb::kMix);

constexpr const char* kDivisionNames[audio::fx::Delay::kDivCount] = {
    "1/32", "1/16", "1/8", "1/4", "1/2", "1/1", "2/1", "4/1"};

std::string trimNumber(double value, int decimals) {
  char buffer[48];
  std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
  std::string text = buffer;
  // Large values (Hz) keep their digits; a fixed-point readout only trims the
  // fraction it was asked for.
  if (decimals > 0) {
    while (!text.empty() && text.back() == '0') text.pop_back();
    if (!text.empty() && text.back() == '.') text.pop_back();
  }
  return text;
}

}  // namespace

std::span<const FxParamMeta> fxParamTable(const std::string& effectId) {
  if (effectId == kFxIdDelay) return kDelayParams;
  if (effectId == kFxIdReverb) return kReverbParams;
  return {};
}

int fxParamIndex(const std::string& effectId, const char* paramId) {
  if (paramId == nullptr) return -1;
  const auto table = fxParamTable(effectId);
  for (const auto& meta : table) {
    if (std::string(meta.id) == paramId) return meta.index;
  }
  return -1;
}

std::vector<double> fxDefaultParams(const std::string& effectId) {
  const auto table = fxParamTable(effectId);
  std::vector<double> params;
  params.reserve(table.size());
  for (const auto& meta : table) params.push_back(meta.def);
  return params;
}

void fxSetParam(std::vector<double>& params, const std::string& effectId,
                int index, double value) {
  if (!std::isfinite(value)) return;
  const auto table = fxParamTable(effectId);
  if (index < 0 || index >= static_cast<int>(table.size())) return;
  const FxParamMeta& meta = table[static_cast<std::size_t>(index)];
  double clamped = std::clamp(value, meta.min, meta.max);
  if (meta.stepped) clamped = std::round(clamped);
  if (params.size() != table.size()) {
    // A slot vector of the wrong length is the effect's own defaults plus the
    // write: the positional contract needs every index present once.
    params = fxDefaultParams(effectId);
  }
  params[static_cast<std::size_t>(index)] = clamped;
}

std::string fxParamValueText(const std::string& effectId, int index,
                             double value) {
  const auto table = fxParamTable(effectId);
  if (index < 0 || index >= static_cast<int>(table.size())) return {};
  const FxParamMeta& meta = table[static_cast<std::size_t>(index)];
  switch (meta.format) {
    case FxParamFormat::Percent:
      return std::to_string(static_cast<int>(std::lround(value * 100.0)));
    case FxParamFormat::OnOff:
      return value >= 0.5 ? "ON" : "OFF";
    case FxParamFormat::Division: {
      const int division = std::clamp(static_cast<int>(std::lround(value)), 0,
                                      audio::fx::Delay::kDivCount - 1);
      return kDivisionNames[division];
    }
    case FxParamFormat::Model: {
      // The card labels are ASCII upper case; the effect's own model names are
      // capitalised ("Hall"), so the readout matches the table above it.
      std::string name = audio::fx::Reverb::modelName(
          std::clamp(static_cast<int>(std::lround(value)), 0,
                     audio::fx::Reverb::kModelCount - 1));
      for (char& character : name) {
        if (character >= 'a' && character <= 'z') {
          character = static_cast<char>(character - 'a' + 'A');
        }
      }
      return name;
    }
    case FxParamFormat::Hertz:
      return std::to_string(static_cast<long long>(std::lround(value)));
    case FxParamFormat::Number:
    default:
      return trimNumber(value, meta.decimals);
  }
}

int fxGridXParam(const std::string& effectId) {
  if (effectId == kFxIdDelay) return audio::fx::Delay::kBpCutoffHz;
  if (effectId == kFxIdReverb) return audio::fx::Reverb::kSize;
  return -1;
}

int fxGridYParam(const std::string& effectId) {
  if (effectId == kFxIdDelay) return audio::fx::Delay::kBpWidthOct;
  if (effectId == kFxIdReverb) return audio::fx::Reverb::kMix;
  return -1;
}

double fxGridPosition(const std::string& effectId, int index, double value) {
  const auto table = fxParamTable(effectId);
  if (index < 0 || index >= static_cast<int>(table.size())) return 0.0;
  const FxParamMeta& meta = table[static_cast<std::size_t>(index)];
  if (!(meta.max > meta.min)) return 0.0;
  return std::clamp((value - meta.min) / (meta.max - meta.min), 0.0, 1.0);
}

double fxGridValue(const std::string& effectId, int index, double position) {
  const auto table = fxParamTable(effectId);
  if (index < 0 || index >= static_cast<int>(table.size())) return 0.0;
  const FxParamMeta& meta = table[static_cast<std::size_t>(index)];
  const double normalized = std::clamp(position, 0.0, 1.0);
  return meta.min + normalized * (meta.max - meta.min);
}

std::string fxEffectName(const std::string& effectId) {
  if (effectId.empty()) return "NONE";
  if (effectId == kFxIdDelay) return "DELAY";
  if (effectId == kFxIdReverb) return "REVERB";
  return "PLUGIN";
}

bool fxIsBuiltin(const std::string& effectId) {
  return effectId == kFxIdDelay || effectId == kFxIdReverb;
}

}  // namespace mnky::app
