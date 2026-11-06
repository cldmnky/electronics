#pragma once
// Built-in effect metadata for the per-track FX rack: the app's single table of
// what a `builtin.delay` / `builtin.reverb` slot holds.
//
// The wire contract is positional (session.hpp: "params are doubles in the
// effect's stable order"), so this table mirrors the stable indices of
// audio/fx/delay.hpp and audio/fx/reverb.hpp. The index constants come from
// those headers (a reordered index is a compile error here, not a silent
// remap), while the ranges and defaults restate what the effect constructors
// publish; the effect still clamps every accepted value, so this table is the
// UI's range, never the DSP's authority. Units are display strings only.
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace mnky::app {

// Effect ids of the per-track rack (session.hpp FxInsertState::id).
inline constexpr const char* kFxIdNone = "";
inline constexpr const char* kFxIdDelay = "builtin.delay";
inline constexpr const char* kFxIdReverb = "builtin.reverb";

// How a parameter value reads out. Number/Hertz/Percent are continuous;
// OnOff/Division/Model are stepped and display their name instead of a number.
enum class FxParamFormat : std::uint8_t {
  Number = 0,
  Hertz,
  Percent,
  OnOff,
  Division,
  Model,
};

struct FxParamMeta {
  int index = 0;             // stable positional index in the slot's params
  const char* id = "";       // effect's own parameter id
  const char* label = "";    // short card label (ASCII)
  const char* unit = "";     // readout suffix (" ms", " Hz", "" when none)
  double min = 0.0;
  double max = 1.0;
  double def = 0.0;
  FxParamFormat format = FxParamFormat::Number;
  int decimals = 0;
  bool stepped = false;
};

// Empty for "none" and for a CLAP id (the plugin owns its table).
std::span<const FxParamMeta> fxParamTable(const std::string& effectId);
// Parameter index for an effect's own id, or -1.
int fxParamIndex(const std::string& effectId, const char* paramId);
// The slot's parameter vector at the effect's published defaults: 8 values for
// the delay, 6 for the reverb, empty for none/CLAP. A slot that selects an
// effect starts from this vector, so the rack, the session and the engine agree
// on what "just selected" means.
std::vector<double> fxDefaultParams(const std::string& effectId);
// Clamped to the parameter's range; a stepped parameter is rounded. A bad index
// or non-finite value leaves the vector untouched.
void fxSetParam(std::vector<double>& params, const std::string& effectId,
                int index, double value);
// Value readout without the unit: "65", "1.25", "ON", "1/8", "HALL".
std::string fxParamValueText(const std::string& effectId, int index, double value);

// The X/Y performance axes of an effect (X = delay cutoff / reverb size,
// Y = delay width / reverb level), or -1 when the effect has no grid.
int fxGridXParam(const std::string& effectId);
int fxGridYParam(const std::string& effectId);
// Normalized position of `value` on the axis (0..1), and the value a normalized
// position maps to. Both clamp; -1 for an effect without that axis.
double fxGridPosition(const std::string& effectId, int index, double value);
double fxGridValue(const std::string& effectId, int index, double position);

// ASCII display name of a slot id: NONE / DELAY / REVERB / PLUGIN, and the
// plugin id itself for an unknown (CLAP) id.
std::string fxEffectName(const std::string& effectId);
bool fxIsBuiltin(const std::string& effectId);

}  // namespace mnky::app
