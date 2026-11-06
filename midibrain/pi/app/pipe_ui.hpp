#pragma once
// Adapters between the generated MIDI FX schema metadata
// (midifx/gen/generated/midifx_ui.hpp, emitted from midifx/pipes.json) and the
// pipe editor. There is no hand-written per-type parameter table: labels,
// ranges, units, choices, editor kinds and the learn-target set all come from
// the schema, so a new pipe type is a pipes.json entry plus one regenerated
// header - no C++ table and no QML.
//
// Editor mapping (one card per positional parameter):
//   ValueFormat::Bool        -> "toggle"  label + ON/OFF chip (choice popup)
//   ValueFormat::Choice      -> "choice"  label + choice-name chip (choice popup)
//   value with a schema unit -> "knob"    arc dial + unit value popup
//   ValueFormat::Plain       -> "stepper" arc dial over the schema range
//
// Inline @p values are the stored positional parameters; indices past the stored
// list use the generated schema default, exactly like the 0x19 wire reader.
#include <QVariantList>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "../../midifx/gen/generated/midifx_ui.hpp"

namespace mnky::app::pipeui {

using ParamSpec = mnky::midifx::generated::ParamSpec;
using PipeSpec = mnky::midifx::generated::PipeSpec;
using ParamUiSpec = mnky::midifx::ui::ParamUiSpec;

// Stable type id (1..16) to schema row; null for an unknown id.
const PipeSpec* pipeSpec(int typeId);
int paramCount(int typeId);
bool isKnown(int typeId);
bool isClocked(int typeId);
QString pipeName(int typeId);
QString pipeShortName(int typeId);
// Schema role of the type, uppercase for the type list: "MODIFIER" (transforms
// messages that already exist) or "GENERATOR" (emits on the clock grid).
QString pipeRoleName(int typeId);
// One-line schema doc of the type ("" when the id is unknown).
QString pipeDocs(int typeId);

int clamp(const ParamSpec& param, int value);
// MIDI note name for 0..127 (C-1 .. G9).
QString noteName(int note);
// Value text with the schema unit: "ON", "MAJOR", "50%", "250 ms", "+4 st", "C4".
QString valueText(const ParamUiSpec& spec, int value);
// Same, for one positional parameter of a type; empty when id/index is unknown.
QString valueText(int typeId, int index, int value);
// "toggle" | "choice" | "knob" | "stepper".
QString editorKind(const ParamUiSpec& ui);

// QML editor rows for a positional range of a pipe's parameters. `first` and
// `last` clip the range, so the caller asks for the whole schema (the parameter
// grid shows every card) and the tests can ask for a slice.
QVariantList editorRows(int typeId, const std::vector<int>& values, int first,
                        int last);
// The same builder over an explicit spec pair. Tests use it with a synthetic
// type to prove that N schema parameters render N editors.
QVariantList editorRowsFor(const ParamSpec* params, const ParamUiSpec* ui,
                           std::size_t count, const std::vector<int>& values,
                           int first, int last);

// Dotted MIDI-learn path of one positional pipe parameter. The path stays
// positional on purpose: a learned control must survive a chain edit, and the
// LearnEngine's setter table is registered once, not rebuilt per type change.
// The parameter *set* is the generated schema one (midi_glue walks it).
std::string learnTargetPath(int source, int destination, int pipe, int param);

}  // namespace mnky::app::pipeui
