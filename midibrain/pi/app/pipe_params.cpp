#include "pipe_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace mnky::app {
namespace {

using session::MidiPipeType;

constexpr const char* kFilterChoices[] = {"NOTE ON", "NOTE OFF", "CC",     "PC",
                                          "PRESS",   "BEND",     "CLOCK",  "SYSEX"};
constexpr const char* kScaleChoices[] = {"OFF",  "MAJOR", "MINOR", "PENTA MAJ",
                                         "PENTA MIN", "DORIAN", "USER"};
constexpr const char* kRootChoices[] = {"C",  "C#", "D",  "D#", "E",  "F",
                                        "F#", "G",  "G#", "A",  "A#", "B"};
constexpr const char* kTransformKindChoices[] = {"NOTE", "CC", "PC", "BEND", "PRESS"};
constexpr const char* kDivisionChoices[] = {"1/4", "1/8", "1/16", "1/32"};
constexpr const char* kArpTypeChoices[] = {"UP", "DOWN", "UPDOWN", "RANDOM", "CHORD"};
constexpr const char* kLfoWaveChoices[] = {"SINE", "TRI", "SQUARE", "SAW"};

// Positional parameter order is frozen by pi/session/session.hpp; every row is
// annotated with the same index the session core documents.
constexpr PipeParamSpec kFilter[] = {
    {"PASS NOTE ON", 0, 1, 1, PipeParamEditor::Toggle, kFilterChoices, 8},
    {"PASS NOTE OFF", 0, 1, 1, PipeParamEditor::Toggle},
    {"PASS CC", 0, 1, 1, PipeParamEditor::Toggle},
    {"PASS PC", 0, 1, 1, PipeParamEditor::Toggle},
    {"PASS PRESS", 0, 1, 1, PipeParamEditor::Toggle},
    {"PASS BEND", 0, 1, 1, PipeParamEditor::Toggle},
    {"PASS CLOCK", 0, 1, 1, PipeParamEditor::Toggle},
    {"PASS SYSEX", 0, 1, 1, PipeParamEditor::Toggle},
};
constexpr PipeParamSpec kNoteRange[] = {
    {"LOW NOTE", 0, 127, 0, PipeParamEditor::Note},
    {"HIGH NOTE", 0, 127, 127, PipeParamEditor::Note},
    {"LOW VELOCITY", 0, 127, 0, PipeParamEditor::Stepper},
    {"HIGH VELOCITY", 0, 127, 127, PipeParamEditor::Stepper},
};
constexpr PipeParamSpec kCcRemap[] = {
    {"IN LOW", 0, 127, 0, PipeParamEditor::Stepper},
    {"IN HIGH", 0, 127, 127, PipeParamEditor::Stepper},
    {"OUT LOW", 0, 127, 0, PipeParamEditor::Stepper},
    {"OUT HIGH", 0, 127, 127, PipeParamEditor::Stepper},
};
constexpr PipeParamSpec kChannelRemap[] = {
    {"IN CHANNEL", 0, 16, 0, PipeParamEditor::Stepper},   // 0 = any
    {"OUT CHANNEL", 1, 16, 1, PipeParamEditor::Stepper},
};
constexpr PipeParamSpec kScaleQuantize[] = {
    {"SCALE", 0, 6, 0, PipeParamEditor::Choice, kScaleChoices, 7},
    {"ROOT", 0, 11, 0, PipeParamEditor::Choice, kRootChoices, 12},
    {"USER MASK", 0, 4095, 4095, PipeParamEditor::Stepper},  // 12 pitch-class bits
};
constexpr PipeParamSpec kChance[] = {
    {"PASS CHANCE", 0, 100, 0, PipeParamEditor::Level},
};
constexpr PipeParamSpec kVelocityRandom[] = {
    {"HUMANIZE", 0, 127, 0, PipeParamEditor::Stepper},
    {"MIN VELOCITY", 0, 127, 0, PipeParamEditor::Stepper},
};
constexpr PipeParamSpec kDispatcher[] = {
    {"CHANNELS", 2, 16, 2, PipeParamEditor::Stepper},
    {"START CHANNEL", 1, 16, 1, PipeParamEditor::Stepper},
};
constexpr PipeParamSpec kSustain[] = {
    {"LATCH", 0, 1, 0, PipeParamEditor::Toggle},
};
constexpr PipeParamSpec kNoteLength[] = {
    {"LENGTH", 0, 5000, 0, PipeParamEditor::Milliseconds},  // 0 = off
};
constexpr PipeParamSpec kTransform[] = {
    {"MATCH KIND", 0, 4, 0, PipeParamEditor::Choice, kTransformKindChoices, 5},
    {"MATCH LOW", 0, 127, 0, PipeParamEditor::Stepper},
    {"MATCH HIGH", 0, 127, 127, PipeParamEditor::Stepper},
    {"OUT KIND", 0, 4, 0, PipeParamEditor::Choice, kTransformKindChoices, 5},
    {"OUT VALUE", 0, 16383, 0, PipeParamEditor::Stepper},
};
constexpr PipeParamSpec kArp[] = {
    {"DIVISION", 0, 3, 1, PipeParamEditor::Choice, kDivisionChoices, 4},
    {"TYPE", 0, 4, 0, PipeParamEditor::Choice, kArpTypeChoices, 5},
    {"OCTAVES", 1, 4, 1, PipeParamEditor::Stepper},
    {"SWING", 0, 100, 0, PipeParamEditor::Level},
    {"ACCENT", 0, 100, 0, PipeParamEditor::Level},
    {"GATE", 1, 100, 100, PipeParamEditor::Level},
};
constexpr PipeParamSpec kRepeater[] = {
    {"DIVISION", 0, 3, 1, PipeParamEditor::Choice, kDivisionChoices, 4},
    {"REPEATS", 1, 8, 1, PipeParamEditor::Stepper},
    {"FALLOFF", 0, 100, 0, PipeParamEditor::Level},
};
constexpr PipeParamSpec kCcLfo[] = {
    {"CC", 0, 127, 1, PipeParamEditor::Stepper},
    {"WAVE", 0, 3, 0, PipeParamEditor::Choice, kLfoWaveChoices, 4},
    {"DIVISION", 0, 3, 1, PipeParamEditor::Choice, kDivisionChoices, 4},
    {"DEPTH", 0, 127, 0, PipeParamEditor::Stepper},
    {"CENTER", 0, 127, 64, PipeParamEditor::Stepper},
};
constexpr PipeParamSpec kHarmonizer[] = {
    {"VOICES", 0, 4, 0, PipeParamEditor::Stepper},
    {"INTERVAL 1", -48, 48, 0, PipeParamEditor::Signed},
    {"INTERVAL 2", -48, 48, 4, PipeParamEditor::Signed},
    {"INTERVAL 3", -48, 48, 7, PipeParamEditor::Signed},
    {"INTERVAL 4", -48, 48, 12, PipeParamEditor::Signed},
};
constexpr PipeParamSpec kMidiDelay[] = {
    {"DELAY", 0, 2000, 250, PipeParamEditor::Milliseconds},
    {"REPEATS", 0, 8, 1, PipeParamEditor::Stepper},
    {"FALLOFF", 0, 100, 50, PipeParamEditor::Level},
};

constexpr PipeTypeSpec kTypes[] = {
    {MidiPipeType::Filter, "FILTER", "FILT", false, kFilter},
    {MidiPipeType::NoteRange, "NOTE RANGE", "NOTE", false, kNoteRange},
    {MidiPipeType::CcRemap, "CC REMAP", "CC", false, kCcRemap},
    {MidiPipeType::ChannelRemap, "CH REMAP", "CH", false, kChannelRemap},
    {MidiPipeType::ScaleQuantize, "SCALE", "SCAL", false, kScaleQuantize},
    {MidiPipeType::Chance, "CHANCE", "CHAN", false, kChance},
    {MidiPipeType::VelocityRandom, "VELOCITY", "VEL", false, kVelocityRandom},
    {MidiPipeType::Dispatcher, "DISPATCH", "DISP", false, kDispatcher},
    {MidiPipeType::Sustain, "SUSTAIN", "SUS", false, kSustain},
    {MidiPipeType::NoteLength, "NOTE LEN", "LEN", false, kNoteLength},
    {MidiPipeType::Transform, "TRANSFORM", "XF", false, kTransform},
    {MidiPipeType::Arp, "ARP", "ARP", true, kArp},
    {MidiPipeType::Repeater, "REPEAT", "REP", true, kRepeater},
    {MidiPipeType::CcLfo, "CC LFO", "LFO", true, kCcLfo},
    {MidiPipeType::Harmonizer, "HARMONY", "HARM", true, kHarmonizer},
    {MidiPipeType::MidiDelay, "MIDI DELAY", "DLY", true, kMidiDelay},
};

}  // namespace

std::span<const PipeParamSpec> pipeParamSpecs(session::MidiPipeType type) {
  const auto* spec = pipeTypeSpec(type);
  return spec == nullptr ? std::span<const PipeParamSpec>{} : spec->params;
}

std::span<const PipeTypeSpec> pipeTypeSpecs() { return kTypes; }

const PipeTypeSpec* pipeTypeSpec(int typeId) {
  for (const auto& spec : kTypes) {
    if (static_cast<int>(spec.type) == typeId) return &spec;
  }
  return nullptr;
}

const PipeTypeSpec* pipeTypeSpec(session::MidiPipeType type) {
  return pipeTypeSpec(static_cast<int>(type));
}

bool isClockedPipeType(session::MidiPipeType type) {
  const auto* spec = pipeTypeSpec(type);
  return spec != nullptr && spec->clocked;
}

bool isKnownPipeType(int typeId) { return pipeTypeSpec(typeId) != nullptr; }

int pipeParamCount(session::MidiPipeType type) {
  return static_cast<int>(pipeParamSpecs(type).size());
}

int clampPipeParam(const PipeParamSpec& spec, int value) {
  return std::clamp(value, spec.minimum, spec.maximum);
}

std::string pipeNoteName(int note) {
  static constexpr const char* kNames[] = {"C",  "C#", "D",  "D#", "E",  "F",
                                           "F#", "G",  "G#", "A",  "A#", "B"};
  const int clamped = std::clamp(note, 0, 127);
  const int octave = clamped / 12 - 1;
  char buffer[16] = {};
  std::snprintf(buffer, sizeof buffer, "%s%d", kNames[clamped % 12], octave);
  return buffer;
}

std::string pipeParamText(const PipeParamSpec& spec, int value) {
  const int clamped = clampPipeParam(spec, value);
  if (spec.editor == PipeParamEditor::Note) return pipeNoteName(clamped);
  if (spec.editor == PipeParamEditor::Choice && spec.choices != nullptr &&
      spec.choiceCount > 0) {
    return spec.choices[std::clamp(clamped, 0, spec.choiceCount - 1)];
  }
  if (spec.editor == PipeParamEditor::Toggle) return clamped != 0 ? "ON" : "OFF";
  if (spec.editor == PipeParamEditor::Level) return std::to_string(clamped) + "%";
  if (spec.editor == PipeParamEditor::Milliseconds) {
    return std::to_string(clamped) + " ms";
  }
  if (spec.editor == PipeParamEditor::Signed) {
    return (clamped > 0 ? "+" : "") + std::to_string(clamped) + " st";
  }
  return std::to_string(clamped);
}

std::string pipeLearnTargetPath(int source, int destination, int pipe, int param) {
  char buffer[64] = {};
  std::snprintf(buffer, sizeof buffer, "routing.pipe.%d.%d.%d.%d", source,
                destination, pipe, param);
  return buffer;
}

}  // namespace mnky::app
