#pragma once
// App-side descriptor table for the MIDI FX pipe parameters frozen in
// pi/session/session.hpp (MidiPipeType, positional int parameters). One
// descriptor per positional parameter gives the QML generator its editor kind,
// range, startup default and (for enumerated values) the choice names. The
// table is the single source for the pipe editor, the value formatting, the
// MIDI-learn range mapping and the non-visual tests; the DSP semantics stay in
// pi/midi/midifx.cpp and are not duplicated here.
//
// Editor mapping (documented in pi/app/NOTES.md, wave 6):
//   Toggle      -> one ON/OFF button (one positional 0/1 flag)
//   Choice      -> previous / name / next stepper over `choices`
//   Stepper     -> signed integer stepper over [minimum, maximum]
//   Note        -> MIDI note stepper shown as a note name (C-1..G9)
//   Level       -> 0..100 TouchSlider (percent)
//   Milliseconds-> millisecond stepper
//   Signed      -> signed semitone stepper
// Every value is an integer on the wire (session::MidiPipe::params).
#include <cstddef>
#include <span>
#include <string>

#include "../session/session.hpp"

namespace mnky::app {

enum class PipeParamEditor : std::uint8_t {
  Toggle = 0,
  Choice = 1,
  Stepper = 2,
  Note = 3,
  Level = 4,
  Milliseconds = 5,
  Signed = 6,
};

struct PipeParamSpec {
  const char* name = "";
  int minimum = 0;
  int maximum = 0;
  int defaultValue = 0;
  PipeParamEditor editor = PipeParamEditor::Stepper;
  const char* const* choices = nullptr;
  int choiceCount = 0;
};

struct PipeTypeSpec {
  session::MidiPipeType type = session::MidiPipeType::Filter;
  const char* name = "";
  const char* shortName = "";
  bool clocked = false;  // Wave 2: configure-only until the clocked engine lands
  std::span<const PipeParamSpec> params;
};

// The parameter table for one pipe type. Unknown types return an empty span.
std::span<const PipeParamSpec> pipeParamSpecs(session::MidiPipeType type);

// The full type table in stable id order (Filter .. MidiDelay).
std::span<const PipeTypeSpec> pipeTypeSpecs();

// Stable type id (1..16) to descriptor; null for an unknown id.
const PipeTypeSpec* pipeTypeSpec(int typeId);
const PipeTypeSpec* pipeTypeSpec(session::MidiPipeType type);

bool isClockedPipeType(session::MidiPipeType type);
bool isKnownPipeType(int typeId);
int pipeParamCount(session::MidiPipeType type);

int clampPipeParam(const PipeParamSpec& spec, int value);
// Value text for the editor/readout: note names, choice names or plain ints.
std::string pipeParamText(const PipeParamSpec& spec, int value);
// MIDI note name for 0..127 (C-1 .. G9).
std::string pipeNoteName(int note);

// Dotted MIDI-learn path for one positional parameter. This is the path the
// binding table stores and the LearnEngine setter map registers. The route
// indices are 0..2 (TRS IN1/TRS IN2/USB IN -> TRS OUT1/TRS OUT2/USB OUT).
std::string pipeLearnTargetPath(int source, int destination, int pipe, int param);

}  // namespace mnky::app
