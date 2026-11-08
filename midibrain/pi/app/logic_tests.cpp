// Non-visual checks for the Qt app. Run by `mnkybox --unittests` and CTest
// `mnkybox_app_logic`; no window, audio device, MIDI port or QML engine needed.
// The QML Style singleton is read from the compiled resource so its contrast
// floors stay a measured property of the shipped theme.
#include <QColor>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QVariantMap>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "../../session/session.hpp"
#include "app_context.hpp"
#include "app_controller.hpp"
#include "app_state.hpp"
#include "arrange_logic.hpp"
#include "audio/engine.hpp"
#include "audio_backend.hpp"
#include "audio_glue.hpp"
#include "config_popup_model.hpp"
#include "eq_model.hpp"
#include "eq_view_model.hpp"
#include "fx_params.hpp"
#include "fx_view_model.hpp"
#include "learn_view_model.hpp"
#include "link_client.hpp"
#include "logging.hpp"
#include "logic.hpp"
#include "midi/learn.hpp"
#include "midi_glue.hpp"
#include "midi_monitor_view_model.hpp"
#include "mixer_model.hpp"
#include "mixer_view_model.hpp"
#include "performance_model.hpp"
#include "pipe_preset.hpp"
#include "pipe_ui.hpp"
#include "project_io.hpp"
#include "project_logic.hpp"
#include "recorder_view_model.hpp"
#include "routing_model.hpp"
#include "routing_view_model.hpp"
#include "session_settings.hpp"
#include "session_view_model.hpp"
#include "system_view_model.hpp"
#include "text_buffer.hpp"
#include "timeline_model.hpp"
#include "timeline_view_model.hpp"
#include "wav_info.hpp"
#include "xy_view_model.hpp"

namespace mnky::app {
namespace {

int gChecks = 0;
int gFailures = 0;

void check(bool condition, const std::string& name) {
  ++gChecks;
  if (condition) return;
  ++gFailures;
  std::printf("FAIL %s\n", name.c_str());
}

void checkNear(double actual, double expected, double tolerance, const std::string& name) {
  check(std::abs(actual - expected) <= tolerance, name);
}

void appClapActivationAudioTests();
void newTrackFromTakeTests();

void checkEqual(const std::string& actual, const std::string& expected,
                const std::string& name) {
  check(actual == expected, name + " (got '" + actual + "', wanted '" + expected + "')");
}

void appendLe16(std::vector<unsigned char>& out, std::uint16_t value) {
  out.push_back(static_cast<unsigned char>(value & 0xFFu));
  out.push_back(static_cast<unsigned char>((value >> 8) & 0xFFu));
}

void appendLe32(std::vector<unsigned char>& out, std::uint32_t value) {
  out.push_back(static_cast<unsigned char>(value & 0xFFu));
  out.push_back(static_cast<unsigned char>((value >> 8) & 0xFFu));
  out.push_back(static_cast<unsigned char>((value >> 16) & 0xFFu));
  out.push_back(static_cast<unsigned char>((value >> 24) & 0xFFu));
}

void appendTag(std::vector<unsigned char>& out, const char* tag) {
  for (int i = 0; i < 4; ++i) out.push_back(static_cast<unsigned char>(tag[i]));
}

// WCAG 2.x relative luminance and contrast ratio.
double luminance(const QColor& colour) {
  const auto channel = [](int value) {
    const double c = static_cast<double>(value) / 255.0;
    return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
  };
  return 0.2126 * channel(colour.red()) + 0.7152 * channel(colour.green()) +
         0.0722 * channel(colour.blue());
}

double contrast(const QColor& a, const QColor& b) {
  const double first = luminance(a);
  const double second = luminance(b);
  return (std::max(first, second) + 0.05) / (std::min(first, second) + 0.05);
}

void logicTests() {
  check(intentForAction(midi::Action::Play, {}).kind == Intent::Kind::Play,
        "Play action maps to Play intent");
  check(intentForAction(midi::Action::Stop, {}).kind == Intent::Kind::Stop,
        "Stop action maps to Stop intent");
  check(intentForAction(midi::Action::NavUp, {}).kind == Intent::Kind::NavUp,
        "NavUp maps to the focus ring");
  check(intentForAction(midi::Action::NavPress, {}).kind == Intent::Kind::NavPress,
        "NavPress maps to focus activation");

  const auto encoder = intentForControl(mb::CTL_ENCODER_TURN, static_cast<std::uint8_t>(2),
                                        false, {});
  check(encoder.kind == Intent::Kind::EncoderTurn && encoder.amount == 2,
        "encoder turn carries a signed detent delta");
  const auto release = intentForControl(mb::CTL_NAV_UP, 0, false, {});
  check(release.kind == Intent::Kind::None, "button release never produces an intent");
  const auto shift = intentForControl(mb::CTL_SHIFT_EXIT, 1, true, {});
  check(shift.kind == Intent::Kind::None, "SHIFT/EXIT is a modifier, not an action");

  checkNear(clampTempoX100(1), 2000, 0, "tempo clamps to the firmware minimum");
  checkNear(clampTempoX100(99999), 30000, 0, "tempo clamps to the firmware maximum");
  checkEqual(formatTempo(12000), "120.00 BPM", "tempo formatting");
  checkEqual(formatBarBeatTick(0), "001:1:00", "bar:beat:tick at the start");
  checkEqual(formatBarBeatTick(kTicksPerBar + kTicksPerBeat), "002:2:00",
             "bar:beat:tick after one bar and one beat");
  checkNear(tickSeconds(kTicksPerBar, 12000), 2.0, 1e-9, "one 4/4 bar at 120 BPM is 2 s");
  checkNear(meterFraction(0.0f), 0.0, 1e-6, "meter floor is silence");
  checkNear(meterFraction(1.0f), 1.0, 1e-6, "meter full scale");
  check(moveIndex(0, -1, 6) == 5, "moveIndex wraps backwards");
  check(moveIndex(5, 2, 6) == 1, "moveIndex wraps forwards");
  checkEqual(formatDuration(65.4), "01:05", "duration formatting");
  checkEqual(formatBytes(2048), "2.0 KiB", "byte formatting");
}

void textBufferTests() {
  TextBuffer buffer;
  check(buffer.insert("hello world"), "text buffer inserts text");
  checkEqual(buffer.text(), "hello world", "text buffer text");
  check(buffer.backspace(), "text buffer backspace");
  checkEqual(buffer.text(), "hello worl", "text buffer after backspace");
  buffer.moveLeft();
  buffer.moveLeft();
  check(buffer.insert('X'), "text buffer inserts at the cursor");
  checkEqual(buffer.text(), "hello woXrl", "cursor insert position");
  buffer.setCursor(0);
  check(buffer.deleteForward(), "text buffer deletes forward");
  checkEqual(buffer.text(), "ello woXrl", "forward delete position");
  buffer.clear();
  check(buffer.empty(), "text buffer clears");
  buffer.setText("caf\u00e9");
  checkEqual(buffer.text(), "caf", "non-ASCII input is dropped");
  std::string overLong(60, 'a');
  buffer.setText(overLong);
  check(static_cast<int>(buffer.text().size()) == TextBuffer::kDefaultMaxLength,
        "text buffer enforces max length");
  check(!buffer.insert('b'), "insert at max length is refused");
}

void projectTests() {
  LoopWrapPulse wrapPulse;
  wrapPulse.note(1000);
  check(wrapPulse.count() == 1 && wrapPulse.lastMs() == 1000,
        "loop wrap pulse records the counter and timestamp");
  check(wrapPulse.active(1000) && wrapPulse.active(1279),
        "loop wrap pulse stays active for its ~280 ms window");
  check(!wrapPulse.active(1280) && !wrapPulse.active(999),
        "loop wrap pulse expires after 280 ms and ignores clock skew");
  wrapPulse.note(2000);
  check(wrapPulse.count() == 2 && wrapPulse.lastMs() == 2000,
        "a second loop wrap advances the pulse");
  check(validateProjectName("Project-20260101-120000").empty(), "valid project name");
  check(!validateProjectName("").empty(), "empty project name rejected");
  check(!validateProjectName("bad/name").empty(), "path separator rejected");
  check(!validateProjectName(" leading").empty(), "leading space rejected");
  check(!validateTakeName("session-1").empty(), "take name keeps the take- prefix");
  check(validateTakeName("take-20260101T120000Z").empty(), "valid take name");
  checkEqual(defaultProjectName(0), "Project-19700101-000000",
             "default project name is UTC");

  DirtyTracker tracker;
  check(!tracker.poll("a"), "first dirty poll establishes the baseline");
  check(!tracker.poll("a"), "unchanged fingerprint stays clean");
  check(tracker.poll("b"), "changed fingerprint becomes dirty");
  check(tracker.poll("a"), "manual undo stays dirty until clear");
  tracker.clear("a");
  check(!tracker.poll("a"), "clear resets the dirty baseline");

  ChordDetector chord;
  check(!chord.onPress(ChordDetector::Button::Rec, 100), "REC alone does not complete");
  check(chord.onPress(ChordDetector::Button::Play, 250), "REC then PLAY inside 300 ms");
  chord.reset();
  check(!chord.onPress(ChordDetector::Button::Play, 100), "PLAY alone does not complete");
  check(!chord.onPress(ChordDetector::Button::Rec, 401), "the 300 ms window is strict");
  check(chord.onPress(ChordDetector::Button::Play, 402), "a later edge can still pair");
}

void audioBackendTests() {
  check(audioBackendFromKey("default") == AudioBackend::Default, "default backend key");
  check(audioBackendFromKey("device") == AudioBackend::Device, "device backend key");
  check(audioBackendFromKey("emulator") == AudioBackend::Emulator, "emulator backend key");
  check(audioBackendFromKey("jack") == AudioBackend::Default,
        "the dropped JACK key resolves to the default backend");

  AudioBackendAvailability availability;
  availability.defaultOutput = true;
  availability.outputDeviceIds = {"a", "b"};
  auto selection = resolveAudioBackend(AudioBackend::Device, "b", availability);
  check(selection.effective == AudioBackend::Device && !selection.fellBack,
        "a listed device is used as requested");
  selection = resolveAudioBackend(AudioBackend::Device, "gone", availability);
  check(selection.effective == AudioBackend::Default && selection.fellBack,
        "a missing device falls back to the default output");
  selection = resolveAudioBackend(AudioBackend::Default, "", availability);
  check(selection.effective == AudioBackend::Default && !selection.fellBack,
        "the default output is used when present");
  availability.defaultOutput = false;
  selection = resolveAudioBackend(AudioBackend::Default, "", availability);
  check(selection.effective == AudioBackend::Emulator && selection.fellBack,
        "no output at all falls back to the emulator generator");
  selection = resolveAudioBackend(AudioBackend::Emulator, "", {});
  check(selection.effective == AudioBackend::Emulator && !selection.fellBack,
        "the emulator backend is always available");

  check(clampAudioBufferSize(100) == 128, "buffer size rounds to the nearest");
  check(clampAudioBufferSize(200) == 256, "buffer size rounds up when nearer");
  check(audioBufferSizeLabel(64) == "64 frames", "buffer size label");
  check(resolveAudioDeviceName("b", {"a", "b"}) == "b", "stored device still listed");
  check(resolveAudioDeviceName("x", {"a", "b"}).empty(),
        "unplugged device resolves to the type default");
}

void routingTests() {
  const auto layout = routing::layoutFor(788.0f, 140.0f);
  check(layout.nodeHeight > 0.0f, "patchbay layout has nodes");
  check(routing::hitInputNode(layout, {10.0f, layout.inputs[1].centreY()}) == 1,
        "input node hit test");
  check(routing::hitOutputNode(layout, {780.0f, layout.outputs[2].centreY()}) == 2,
        "output node hit test");
  check(routing::distanceToCubic(layout.wires[2][1].start, layout.wires[2][1]) < 1.0f,
        "a wire contains its own start point");
  int source = -1;
  int destination = -1;
  check(routing::hitWire(layout, layout.wires[2][1].end, source, destination),
        "a wire endpoint is within the touch slop");
  check(routing::kDefaultRoutes[0][2] && routing::kDefaultRoutes[1][2] &&
            !routing::kDefaultRoutes[2][2],
        "documented default routes: TRS IN 1+2 -> USB OUT only");
  check(routing::kInputs[2] == midi::Port::UsbIn &&
            routing::kOutputs[2] == midi::Port::UsbOut,
        "USB ports keep their documented patchbay position");

  // Per-device nodes: ids 6..31 are separate nodes, both directions of one
  // device share the stable id, and a dormant reservation keeps its identity.
  std::vector<MidiGlue::UsbPortInfo> ports = {
      {6, "Studio Keys", "usb:Studio Keys", true, true, true},
      {7, "Pad Controller", "usb:Pad Controller", true, false, false},
      {8, "Synth Out", "usb:Synth Out", false, true, true}};
  const auto inputs = routingInputNodes(ports);
  const auto outputs = routingOutputNodes(ports);
  check(inputs.size() == 6 && inputs[0].port == 0 && inputs[1].port == 1 &&
            inputs[2].port == 2,
        "routing input nodes keep the three fixed ports first");
  check(inputs[3].port == midi::kSrcGenerator &&
            inputs[3].name == QStringLiteral("GENERATOR") && !inputs[3].dynamic,
        "GENERATOR is a fixed source-less node, listed with the fixed sources");
  check(inputs[4].port == 6 && inputs[4].name == QStringLiteral("Studio Keys") &&
            inputs[4].connected,
        "a connected per-device USB input becomes its own node");
  check(inputs[5].port == 7 && !inputs[5].connected &&
            inputs[5].dynamic,
        "a dormant per-device reservation stays visible");
  check(outputs.size() == 5 && outputs[3].port == 6 && outputs[4].port == 8,
        "per-device output nodes use the same stable registry id");
  check(inputs[4].input && outputs[3].dynamic,
        "a bidirectional device appears once on each side");

  const auto wide = routing::layoutFor(788.0f, 220.0f, 5, 5);
  check(wide.inputs.size() == 5 && wide.outputs.size() == 5 &&
            wide.wires.size() == 5 && wide.wires[4].size() == 5,
        "generic patchbay layout follows the dynamic node counts");
  check(std::abs(wide.nodeHeight - 44.0f) < 1e-4f,
        "five rows on 220 px keep the 44 px touch height");
  check(routing::hitInputNode(wide, {10.0f, wide.inputs[4].centreY()}) == 4,
        "a dynamic input node hit test is index-correct");
  check(routing::hitOutputNode(wide, {780.0f, wide.outputs[4].centreY()}) == 4,
        "a dynamic output node hit test is index-correct");

  // The pipe-scope ConfigPopup writes session::MidiPipe::chMask through
  // RoutingViewModel/MidiGlue; the draft is what the 16 toggles edit and OK
  // commits, so the popup and the engine cannot disagree.
  AppState state;
  SessionSettings settings;
  MixerModel mixer;
  EqModel eq;
  PerformanceModel performance;
  MidiGlue scopeMidi(false);
  RoutingViewModel scopeRouting(scopeMidi, true);
  scopeRouting.setPipeSource(0);
  scopeRouting.setPipeDestination(2);
  scopeRouting.addPipe();
  check(scopeRouting.pipeCount() == 1, "pipe scope test has a selected pipe");
  ConfigPopupModel popup(state, mixer, settings, eq);
  popup.setRouting(&scopeRouting);
  check(popup.openKind(QStringLiteral("pipechannels")),
        "the pipe channel-scope popup opens for a selected pipe");
  popup.setPipeChannelPreset(1);
  check(popup.pipeChannelText() == QStringLiteral("CH 1-4"),
        "the pipe scope readout is live ('CH 1-4')");
  popup.accept();
  check(scopeRouting.selectedPipeChannelMask() == 0x000F,
        "OK writes the draft chMask through to MidiGlue");

  // The parameter choice popup: a bool parameter's list is OFF/ON, an enum's
  // list is its schema choices, a tap writes the positional parameter live and
  // CANCEL restores what the popup opened with.
  check(scopeRouting.pipeParams().size() == 8,
        "the selected pipe exposes every schema parameter (no paging)");
  popup.setPipeParamTarget(0, 0);
  check(popup.openKind(QStringLiteral("pipeparam")),
        "the pipe parameter choice popup opens for a bool parameter");
  check(popup.pipeParamChoices() == QStringList({QStringLiteral("OFF"), QStringLiteral("ON")}),
        "a bool parameter offers OFF and ON");
  check(popup.pipeParamChoice() == 1, "the bool parameter opens on its value (ON)");
  popup.setPipeParamChoice(0);
  check(scopeRouting.pipeParams().value(0).toMap().value(QStringLiteral("value")).toInt() == 0,
        "a choice tap writes the pipe parameter immediately");
  popup.cancel();
  check(scopeRouting.pipeParams().value(0).toMap().value(QStringLiteral("value")).toInt() == 1,
        "CANCEL restores the pipe parameter the popup opened with");
  // ARP's DIVISION is a schema enum: four divisions, the first one selected.
  scopeRouting.setAddTypeIndex(11);
  scopeRouting.addPipe();
  scopeRouting.selectPipe(1);
  popup.setPipeParamTarget(1, 0);
  check(popup.openKind(QStringLiteral("pipeparam")),
        "the pipe parameter choice popup opens for an enum parameter");
  check(popup.pipeParamChoices().size() == 4, "an enum offers its schema choices");
  check(popup.pipeParamChoice() == 0, "the enum opens on its stored choice");
  popup.setPipeParamChoice(2);
  checkEqual(scopeRouting.pipeParamValueText(0, scopeRouting.pipeParams()
                                                    .value(0).toMap()
                                                    .value(QStringLiteral("value")).toInt())
                 .toStdString(),
             "1/16",
             "the enum pick reaches the same formatter the card shows");
  popup.accept();

  // The type picker carries the schema role and doc of each type, so the list
  // can state what a type does instead of only its name.
  check(popup.openKind(QStringLiteral("pipetype")), "the pipe type list opens");
  check(popup.pipeTypeRoles().size() == popup.pipeTypeLabels().size(),
        "every pipe type row has a schema role");
  popup.setPipeTypeIndex(11);
  check(popup.pipeTypeDocs().startsWith(QStringLiteral("Arpeggiator")),
        "the type list shows the selected type's schema doc");
  popup.cancel();
}

void mixerTests() {
  MixerModel model;
  // Slots must exist as tracks for the routing state and the arm mask to reach
  // the engine; a removed track is silent by contract (dynamicMixerTests).
  model.addTrack(3, "");
  model.addTrack(4, "");
  model.addTrack(5, "");
  model.addTrack(6, "");
  model.setGainDb(3, -6.0f);
  model.setPan(3, 0.25f);
  model.setSend(3, 1, 0.5f);
  model.setArm(3, true);
  checkNear(gainToDb(dbToGain(-6.0f)), -6.0, 1e-4, "dB/gain round trip");
  audio::Engine engine;
  model.applyTo(engine);
  checkNear(engine.channel(3).gain, dbToGain(-6.0f), 1e-6, "mixer snapshot gain");
  checkNear(engine.channel(3).pan, 0.25, 1e-6, "mixer snapshot pan");
  checkNear(engine.channel(3).send[1], 0.5, 1e-6, "mixer snapshot send");
  // Track modes: 0 PLAY, 1 ARM, 2 OVERDUB, 3 LIVE. The
  // number is the engine's MonitorMode value 1:1, so 3 binds to InputOnly.
  model.setMode(3, 3);
  check(model.mode(3) == 3, "LIVE mode is stored unclamped");
  check(model.channel(3).monitor == audio::MonitorMode::InputOnly,
        "LIVE mode reaches the engine snapshot as InputOnly");
  model.setMode(3, 9);
  check(model.mode(3) == 3, "track mode clamps to 0..3");
  model.setMode(3, 1);
  check(static_cast<int>(model.channel(3).monitor) == 1,
        "ARM mode stays Auto");
  check(model.arm(3), "ARM records (derived arm)");
  check(model.channel(3).arm, "ARM arms the engine strip so its take stays muted");
  model.setMode(3, 2);
  check(model.arm(3), "OVERDUB records too (W2b: ARM and OVERDUB record)");
  check(!model.channel(3).arm,
        "OVERDUB does not arm the engine strip, so the take keeps playing");
  model.setMode(3, 0);
  check(!model.arm(3), "PLAY never records");
  model.setMode(3, 1);
  // Input source, pair flag and output destination are separate engine routing
  // state (Engine::setChannel leaves them alone), so the same snapshot carries
  // them and a strip picker is live on the next block.
  check(engine.inputSource(4) == 4 && engine.inputSource(6) == 6,
        "a new strip listens to its own device channel (identity)");
  model.setInCh(4, 9);
  model.setInPair(5, true);
  model.setOutDst(6, 2);
  model.applyTo(engine);
  check(engine.inputSource(4) == 9 && engine.inputPair(5) &&
            engine.outputDestination(6) == audio::OutputDestination::Phones,
        "the mixer snapshot carries input source, pair and output destination");
  model.setMode(3, 2);
  model.applyTo(engine);
  check(engine.channel(3).monitor == audio::MonitorMode::On,
        "mixer snapshot carries OVERDUB as the On monitor mode");
  model.setMode(3, 0);
  model.applyTo(engine);
  check(engine.channel(3).monitor == audio::MonitorMode::Off,
        "PLAY reaches the engine as Off (take only)");
  check((model.armMask() & (1u << 3)) == 0,
        "a PLAY track drops out of the arm mask");
  model.setMode(3, 1);
  check((model.armMask() & (1u << 3)) != 0, "arm mask carries the ARM track");
  model.setPan(3, 2.0f);
  checkNear(model.pan(3), 1.0, 1e-6, "pan clamps at the top");
  model.setPan(3, 0.49f);
  checkNear(model.pan(3), 0.5, 1e-9, "pan snaps to centre inside the detent");
  model.setPan(3, 0.40f);
  checkNear(model.pan(3), 0.40, 1e-6, "pan outside the detent is unchanged");
}

// Dynamic track list (owner contract): the mixer starts empty, ADD TRACK
// appends a slot in session order, REMOVE TRACK silences the slot while its
// configuration, name and takes stay, and a re-add restores them. The per-slot
// store is fixed, so nothing is ever reallocated by the audio path.
void dynamicMixerTests() {
  MixerModel model;
  check(model.trackCount() == 0, "a new project starts with an empty mixer");
  check(model.firstInactiveSlot() == 0, "the first add uses slot 0");
  model.setGainDb(2, -3.0f);
  model.setOutDst(2, 1);
  check(model.addTrack(2, "keys"), "ADD TRACK appends the slot");
  check(!model.addTrack(2, "again"), "a slot that is already a track cannot be re-added");
  check(model.trackCount() == 1 && model.trackName(2) == "keys",
        "the added track keeps its name");
  check(model.positionOfSlot(2) == 0 && model.slotAt(0) == 2,
        "the list maps positions to slots");

  audio::Engine engine;
  model.applyTo(engine);
  check(engine.inputSource(2) == 2 && engine.outputDestination(2) ==
            audio::OutputDestination::Out34,
        "an active track forwards its input source and output destination");
  model.setArm(2, true);
  check((model.armMask() & (1u << 2)) != 0, "an active armed slot is in the arm mask");

  check(model.removeTrackAt(0), "REMOVE TRACK removes the strip");
  check(model.trackCount() == 0 && !model.trackActive(2),
        "a removed track is no longer in the list");
  model.applyTo(engine);
  check(engine.inputSource(2) == -1 &&
            engine.outputDestination(2) == audio::OutputDestination::None,
        "a removed track is silent (no input, no output destination)");
  check((model.armMask() & (1u << 2)) == 0, "a removed track never records");
  checkNear(model.gainDb(2), -3.0, 1e-6, "REMOVE TRACK keeps the slot's settings");
  check(model.outDst(2) == 1, "REMOVE TRACK keeps the slot's output destination");

  check(model.addTrack(2, ""), "a removed slot can be re-added");
  check(model.trackName(2) == "keys", "a re-add restores the stored name");
  checkNear(model.gainDb(2), -3.0, 1e-6, "a re-add restores the stored settings");

  check(model.addTrack(0, ""), "a nameless add succeeds");
  check(model.slotAt(0) == 2 && model.slotAt(1) == 0,
        "the list keeps session order (a re-added track goes last)");
  check(model.trackName(0) == defaultTrackName(0),
        "a nameless track uses the take-name default");

  MixerModel::FxSlot rack;
  rack.id = "builtin.delay";
  rack.bypass = true;
  rack.wet = 0.4f;
  model.setFxSlot(2, 1, rack);
  const auto stored = model.fxSlot(2, 1);
  check(stored.id == "builtin.delay" && stored.bypass,
        "a rack slot holds id, bypass and wet");
  checkNear(stored.wet, 0.4, 1e-6, "the rack slot stores the wet crossfade");
  check(stored.params.size() == fxParamTable("builtin.delay").size(),
        "a selected effect starts from its published parameter table");
  model.setFxWet(2, 1, 2.5f);
  checkNear(model.fxSlot(2, 1).wet, 1.0, 1e-6, "wet is clamped to 0..1");
  model.setFxSlot(2, 1, {});
  check(model.fxSlot(2, 1).id.empty() && model.fxSlot(2, 1).params.empty(),
        "an empty slot holds no effect");

  // Legacy sessions: a default session::MixerState carries all 18 slots in
  // order, so the app shows the full-width 18-strip mixer for a file without a
  // "tracks" key (the session reader keeps that default).
  session::MixerState legacy;
  const int legacySlots = static_cast<int>(legacy.tracks.size());
  check(legacySlots > 0 && legacySlots <= audio::kMaxTracks,
        "a legacy session state lists every slot its store carries");
  std::vector<MixerModel::Track> legacyTracks;
  for (const auto& entry : legacy.tracks) {
    legacyTracks.push_back(MixerModel::Track{entry.slot, entry.name});
  }
  model.setTracks(legacyTracks);
  check(model.trackCount() == legacySlots && model.trackActive(0) &&
            model.trackActive(legacySlots - 1),
        "a legacy session shows every slot as an active strip");

  // Session load replaces the whole ordered list and the active set.
  model.setTracks({MixerModel::Track{5, "lead"}, MixerModel::Track{1, ""}});
  check(model.trackCount() == 2 && model.slotAt(0) == 5 && model.slotAt(1) == 1 &&
            model.trackActive(5) && !model.trackActive(2),
        "a loaded track list replaces the active set in order");
  check(model.trackName(5) == "lead" && model.trackName(1) == defaultTrackName(1),
        "loaded names are kept and empty names fall back to the default");
}

// Capacity: the mixer's slot store and the arm mask follow the engine's track
// capacity (audio::kMaxTracks, 64), not the device capture width (18). Nothing
// in the app may assume a literal slot count, so a last-slot track is a normal
// add/remove/arm, and the mask carries bit 63.
void capacityTests() {
  check(audio::kMaxTracks >= 64, "the engine track capacity is at least 64");
  check(audio::kMaxChannels == 2 * audio::kMaxTracks,
        "each track owns at most two internal channels");
  check(sizeof(MixerModel::TrackMask) == sizeof(std::uint64_t),
        "the app arm mask is 64-bit, matching Recorder::startTake");
  check(MixerModel::kMaskBits >= audio::kMaxTracks,
        "the mask covers every track slot");

  MixerModel model;
  const int lastSlot = audio::kMaxTracks - 1;
  check(model.firstInactiveSlot() == 0, "the first add still uses slot 0");
  check(model.addTrack(lastSlot, "last"), "a track can occupy the last slot");
  check(model.trackActive(lastSlot) && model.positionOfSlot(lastSlot) == 0,
        "the last slot is a normal track");
  model.setArm(lastSlot, true);
  const auto mask = model.armMask();
  check((mask & (MixerModel::TrackMask{1} << lastSlot)) != 0,
        "the arm mask carries the last slot");
  check(model.addTrack(lastSlot, "again") == false,
        "the last slot cannot be added twice");
  model.setInCh(lastSlot, 5);
  audio::Engine engine;
  model.applyTo(engine);
  check(engine.inputSource(lastSlot) == 5,
        "the last slot carries its own device input selection");
  check(model.removeTrackAt(0), "the last slot can be removed");
  check(!model.trackActive(lastSlot), "the removed last slot is silent");

  // A full 64-track list: the page count follows the list, three per page.
  MixerModel full;
  for (int slot = 0; slot < audio::kMaxTracks; ++slot) {
    check(full.addTrack(slot, ""), "every capacity slot can be added");
  }
  SessionSettings settings;
  EqModel eq;
  MixerViewModel view(full, settings, eq);
  check(view.stripCount() == audio::kMaxTracks, "the mixer shows every track");
  check(view.pageCount() == (audio::kMaxTracks + kStripsPerBank - 1) / kStripsPerBank,
        "the page count is ceil(tracks / 3), never a constant");
  view.setBank(view.pageCount() - 1);
  check(view.bank() == view.pageCount() - 1, "the last page is reachable");
  view.setBank(view.pageCount());
  check(view.bank() == view.pageCount() - 1, "the bank clamps at the last page");
}

// The QML model over the dynamic list: rows carry slots (not positions), the
// bank pages three strips at a time, the selectors are absolute list writes and
// the channel popout reads the same slot the strip was opened on.
void dynamicMixerViewModelTests() {
  SessionSettings settings;
  MixerModel model;
  FxManager fx;
  EqModel eq;
  MixerViewModel view(model, settings, eq);
  view.setClapHost(&fx);

  check(view.rowCount() == 0 && view.empty() && view.pageCount() == 1,
        "an empty project has no strip rows and one empty page");
  check(view.bankText() == "NO TRACKS", "the page readout names the empty mixer");

  check(view.addTrack(0, "drums", 0, false, 0), "the view adds slot 0");
  check(view.addTrack(1, "bass", 1, false, 1), "the view adds slot 1");
  check(view.addTrack(2, "keys", 2, true, 3), "the view adds slot 2 as a pair");
  check(view.rowCount() == 3 && !view.empty(), "the list has three strips");
  check(view.data(view.index(1, 0), MixerViewModel::SlotRole).toInt() == 1,
        "a row carries its slot, not just its position");
  check(view.data(view.index(1, 0), MixerViewModel::NameRole).toString() == "bass",
        "a row carries the track name");
  check(view.data(view.index(2, 0), MixerViewModel::InputTextRole).toString() ==
            "IN 03/04",
        "the pair input reads back as a stereo pair");
  check(view.data(view.index(2, 0), MixerViewModel::OutputTextRole).toString() ==
            "NONE",
        "the output destination reads back in the row");
  check(view.stripsPerBank() == 3, "the stage-2 bank shows three strips");
  check(view.bankText() == "TRACKS 1-3 / 3", "the page readout counts the tracks");

  check(view.addTrack(3, "gtr", 3, false, 0), "the view adds slot 3");
  check(view.addTrack(4, "vox", 4, false, 0), "the view adds slot 4");
  check(view.pageCount() == 2 && view.bankText() == "TRACKS 1-3 / 5",
        "three strips fit one page and the fourth opens a second page");
  view.stepBank(1);
  check(view.bank() == 1 && view.bankText() == "TRACKS 4-5 / 5",
        "stepping the bank reaches the second page");
  view.stepBank(1);
  check(view.bank() == 1, "the bank cannot step past the last page");
  // the pager grid is a fixed grid over the capacity; pages past the
  // track list stay visible but empty, and the current page is marked.
  const auto pages = view.bankPages();
  check(pages.size() == (audio::kMaxTracks + kStripsPerBank - 1) / kStripsPerBank,
        "the pager grid covers the 64-track capacity");
  check(pages.value(0).toMap().value(QStringLiteral("label")).toString() == "1-3",
        "the first page reads 1-3");
  check(!pages.value(1).toMap().value(QStringLiteral("empty")).toBool(),
        "a page with tracks is selectable");
  check(pages.value(2).toMap().value(QStringLiteral("empty")).toBool(),
        "a page past the track list is empty");
  check(pages.value(1).toMap().value(QStringLiteral("current")).toBool(),
        "the grid marks the current page");
  view.stepBank(-1);
  // a tap clears one meter's latch without ending the demo fixture.
  const auto clipAt = [&view](int row) {
    return view.data(view.index(row, 0), MixerViewModel::ClipRole).toBool();
  };
  view.showClipDemo();
  check(clipAt(0) && clipAt(1), "the clip demo latches crowns");
  view.clearClip(view.data(view.index(0, 0), MixerViewModel::SlotRole).toInt());
  check(!clipAt(0) && clipAt(1),
        "clearClip drops one crown and leaves its neighbour");
  view.clearClipDemo();

  view.setGainDb(1, -6.0);
  view.removeTrack(1);
  check(view.rowCount() == 4, "REMOVE TRACK drops one row");
  checkNear(model.gainDb(1), -6.0, 1e-6, "REMOVE TRACK keeps the removed settings");
  check(view.data(view.index(1, 0), MixerViewModel::SlotRole).toInt() == 2,
        "the rows below a removed track keep their slots");

  const int inputChannels = MixerViewModel::inputChannelCount();
  check(view.inSourceState(2) == 1 + inputChannels + 1,
        "slot 2 is the pair IN 03/04");
  check(MixerViewModel::inputSourceLabel(1 + inputChannels + 1) == "PAIR 03/04",
        "the selector list names the pair");
  view.setInSourceState(2, 0);
  check(model.inCh(2) == -1 && !model.inPair(2), "NONE clears the input source");
  view.setInSourceState(2, 1 + 4);
  check(model.inCh(2) == 4 && !model.inPair(2), "a mono pick writes the device channel");
  view.setOutDstOf(2, 2);
  check(model.outDst(2) == 2, "the output selector writes the destination");

  check(MixerViewModel::inputSourceLabels().size() ==
            1 + MixerViewModel::inputChannelCount() +
                MixerViewModel::inputChannelCount() / 2,
        "the input list covers none, mono device channels and every pair");
  check(MixerViewModel::outputLabels().size() == kOutDstCount,
        "the output list covers the four destinations");

  view.setFxChoiceAt(2, 0, 1);
  check(model.fxSlot(2, 0).id == "builtin.delay",
        "the slot selector writes the effect id");
  check(view.fxNameAt(2, 0) == "DELAY", "the slot reads back by name");
  check(model.fxSlot(2, 1).id.empty(), "FX1 and FX2 are independent slots");
  if (!fx.plugins().empty()) {
    const auto plugin = fx.plugins().front();
    check(view.setFxChoiceAt(2, 0, MixerViewModel::kFxChoicePlugin),
          "a discovered plugin is a selectable rack choice");
    check(model.fxSlot(2, 0).id == plugin.id,
          "the plugin choice writes the plugin's CLAP id");
    check(model.fxSlot(2, 0).params.size() == plugin.params.size(),
          "a new plugin slot starts from the plugin's own parameter defaults");
    check(view.fxParams(2, 0).size() == static_cast<int>(plugin.params.size()),
          "the card list comes from the plugin's clap.params table");
    check(view.setFxChoiceAt(2, 0, 0),
          "clearing the plugin slot is a built-in NONE selection");
    check(model.fxSlot(2, 0).id.empty(), "NONE clears the CLAP plugin id");
    check(view.setFxChoiceAt(2, 0, 1), "the built-in selector still works");
  } else {
    check(!view.setFxChoiceAt(2, 0, MixerViewModel::kFxChoicePlugin),
          "without a discovered plugin the choice is refused");
  }
  view.setFxBypassAt(2, 0, true);
  check(model.fxSlot(2, 0).bypass, "the slot bypass is stored per track");
  view.setFxWetAt(2, 0, 0.65);
  checkNear(model.fxSlot(2, 0).wet, 0.65, 1e-6,
            "the FX1 knob writes the slot wet (not a send)");
  check(model.fxSlot(2, 0).params.size() == fxParamTable("builtin.delay").size(),
        "the rack holds one parameter per effect parameter");
  view.setFxParamAt(2, 0, audio::fx::Delay::kBpCutoffHz, 1234.0);
  checkNear(model.fxSlot(2, 0).params[audio::fx::Delay::kBpCutoffHz], 1234.0, 1e-9,
            "a rack parameter write lands positionally");
  view.setFxParamAt(2, 0, audio::fx::Delay::kBpCutoffHz, 1.0e9);
  checkNear(model.fxSlot(2, 0).params[audio::fx::Delay::kBpCutoffHz], 20000.0, 1e-9,
            "a rack parameter write is clamped to the effect range");
  check(view.fxParams(2, 0).size() ==
            static_cast<int>(fxParamTable("builtin.delay").size()),
        "the popup gets one card per parameter");
  check(view.fxParams(2, 1).isEmpty(), "an empty slot has no parameter cards");
  view.setFxChoiceAt(2, 0, 0);
  check(model.fxSlot(2, 0).id.empty() && view.fxNameAt(2, 0) == "NONE",
        "NONE clears the slot");
  checkNear(model.fxSlot(2, 0).wet, 0.65, 1e-6,
            "an empty slot keeps the wet knob position");

  view.openChannel(2);
  check(view.channelOpen() && view.channelSlot() == 2,
        "the channel popout opens on the strip's slot");
  check(view.channelEqSummary().contains("1 NONE"),
        "the popout summarises the channel's EQ bands");
  check(view.channelFxSummary().contains("FX1") &&
            view.channelFxSummary().contains("FX2"),
        "the popout summarises both rack slots");
  view.setTrackFxWet1(0.25);
  checkNear(model.fxSlot(2, 0).wet, 0.25, 1e-6,
            "the popout's FX1 knob writes that track's slot wet");
  view.closeChannel();
  check(!view.channelOpen(), "CLOSE closes the channel popout");
}

// Every config popup field is live: the change lands on the model at the tap
// (no apply button) and CANCEL restores what the popup opened with. ADD TRACK
// is the one kind that creates anything, and only on OK.
void configLiveTests() {
  AppState state;
  SessionSettings settings;
  MixerModel mixer;
  FxManager fx;
  EqModel eq;
  PerformanceModel performance;
  ConfigPopupModel popup(state, mixer, settings, eq);
  MixerViewModel view(mixer, settings, eq);
  view.setClapHost(&fx);
  popup.setMixer(&view);

  const int clickModeBefore = state.clickMode.load();
  check(popup.openKind(QStringLiteral("metronome")), "the metronome popup opens");
  popup.setClickMode(2);
  check(state.clickMode.load() == 2,
        "a metronome change reaches the model immediately (no OK)");
  popup.cancel();
  check(state.clickMode.load() == clickModeBefore,
        "CANCEL restores the metronome value the popup opened with");

  check(popup.openKind(QStringLiteral("sends")), "the send-source popup opens");
  popup.setSendCue(0);
  check(settings.sends.cue == 0, "a send-source change is live");
  popup.accept();
  check(settings.sends.cue == 0, "OK keeps the live value");

  // ADD TRACK: name plus input (none / mono / pair) and output.
  check(popup.openKind(QStringLiteral("addtrack")), "the add-track popup opens");
  popup.setAddTrackName(QStringLiteral("lead"));
  popup.setAddTrackInputMode(2);
  popup.setAddTrackChannel(5);
  popup.setAddTrackOutput(1);
  check(popup.addTrackChannel() == 4,
        "a stereo pair clamps the input to an even first channel");
  popup.accept();
  check(view.stripCount() == 1, "OK ADDS the track");
  check(mixer.trackName(0) == "lead", "the new track carries the popup name");
  check(mixer.inCh(0) == 4 && mixer.inPair(0),
        "the new track carries the popup's stereo input");
  check(mixer.outDst(0) == 1, "the new track carries the popup's output");

  check(popup.openKind(QStringLiteral("addtrack")), "the add-track popup reopens");
  popup.setAddTrackName(QStringLiteral("discarded"));
  popup.cancel();
  check(view.stripCount() == 1, "CANCEL adds nothing");

  // Selector popups: input, output, FX slot, delay division, reverb model.
  popup.setInputTarget(0);
  check(popup.openKind(QStringLiteral("inputsource")), "the input list opens");
  popup.setInputSourceIndex(1 + 7);
  check(mixer.inCh(0) == 7 && !mixer.inPair(0),
        "an input pick is written to the strip immediately");
  popup.cancel();
  check(mixer.inCh(0) == 4 && mixer.inPair(0),
        "CANCEL restores the strip's previous input source");
  popup.setOutputTarget(0);
  check(popup.openKind(QStringLiteral("outputdest")), "the output list opens");
  popup.setOutputIndex(3);
  check(mixer.outDst(0) == 3, "an output pick is live");
  popup.cancel();
  check(mixer.outDst(0) == 1, "CANCEL restores the previous output");
  popup.setFxTarget(0, 1);
  check(popup.openKind(QStringLiteral("fxslot")), "the FX slot list opens");
  popup.setFxSlotChoice(2);
  check(mixer.fxSlot(0, 1).id == "builtin.reverb", "a slot pick is live");
  check(mixer.fxSlot(0, 0).id.empty(), "the pick writes only the targeted slot");
  if (!fx.plugins().empty()) {
    const auto plugin = fx.plugins().front();
    popup.setFxSlotChoice(MixerViewModel::kFxChoicePlugin);
    check(mixer.fxSlot(0, 1).id == plugin.id,
          "the plugin choice loads into the targeted rack slot");
    check(popup.fxSlotChoice() == MixerViewModel::kFxChoicePlugin,
          "the selector shows the selected plugin");
  }
  popup.cancel();
  check(mixer.fxSlot(0, 1).id.empty(), "CANCEL restores the previous slot");

  // The X/Y parameter kinds edit the rack slot they are pointed at.
  mixer.setFxSlot(0, 0, MixerModel::FxSlot{"builtin.delay", false, 1.0f,
                                           fxDefaultParams("builtin.delay")});
  popup.setFxTarget(0, 0);
  const int divisionBefore = static_cast<int>(std::lround(
      mixer.fxSlot(0, 0).params[audio::fx::Delay::kDivision]));
  check(popup.openKind(QStringLiteral("delaydivision")), "the delay division list opens");
  popup.setDelayDivision(1);
  check(static_cast<int>(std::lround(
            mixer.fxSlot(0, 0).params[audio::fx::Delay::kDivision])) == 1,
        "a delay division pick is live on the track's slot");
  popup.cancel();
  check(static_cast<int>(std::lround(
            mixer.fxSlot(0, 0).params[audio::fx::Delay::kDivision])) == divisionBefore,
        "CANCEL restores the previous delay division");
  mixer.setFxSlot(1, 1, MixerModel::FxSlot{"builtin.reverb", false, 1.0f,
                                           fxDefaultParams("builtin.reverb")});
  popup.setFxTarget(1, 1);
  check(popup.openKind(QStringLiteral("reverbmodel")), "the reverb model list opens");
  popup.setReverbModel(2);
  check(static_cast<int>(std::lround(
            mixer.fxSlot(1, 1).params[audio::fx::Reverb::kModel])) == 2,
        "a reverb model pick is live on the track's slot");
  popup.cancel();
}

void valueFormatTests() {
  checkEqual(formatPanValue(0.5f), "Center", "pan formatting centre");
  checkEqual(formatPanValue(0.25f), "L50", "pan formatting left");
  checkEqual(formatPanValue(0.75f), "R50", "pan formatting right");
  checkEqual(formatDbValue(-6.0f), "-6.0 dB", "dB formatting");
  checkEqual(formatDbValue(3.0f), "+3.0 dB", "positive dB formatting");
  checkEqual(formatPercentValue(0.5f), "50 %", "percent formatting");
  checkEqual(formatRatioValue(4.0f), "4.0:1", "ratio formatting");
  checkEqual(formatMillisecondsValue(10.0f), "10 ms", "millisecond formatting");
  checkEqual(formatGenericValue(-1.25), "-1.25", "generic FX parameter formatting");
  checkEqual(formatHzValue(80.0), "80 Hz", "Hz formatting below 1 kHz");
  checkEqual(formatHzValue(1200.0), "1.20 kHz", "kHz formatting above 1 kHz");
  checkEqual(formatQValue(1.5), "Q 1.50", "Q formatting");
  checkEqual(formatChannelMask(0xFFFF), "CH ALL", "channel mask all");
  checkEqual(formatChannelMask(0x0000), "CH NONE", "channel mask none");
  checkEqual(formatChannelMask(0x000F), "CH 1-4", "channel mask 1-4");
  checkEqual(formatChannelMask(0x00F0), "CH 5-8", "channel mask 5-8");
  checkEqual(formatChannelMask(0x0F0F), "CH 1-4, 9-12", "channel mask ranges");
  checkEqual(formatChannelMask(0x8001), "CH 1, 16", "channel mask reads bit order");
  checkEqual(formatChannelMask(0x8003), "CH 1-2, 16", "channel mask mixed");
}

void learnTests() {
  midi::LearnEngine engine;
  check(engine.addBinding(74, 1, "mixer.ch01.gain", 0), "learn binding added");
  check(!engine.addBinding(74, 1, "mixer.ch02.gain", 0),
        "one binding per (cc, channel)");
  double value = -1.0;
  engine.registerTarget("mixer.ch01.gain",
                        [&value](double next) { value = next; });
  midi::MidiEvent event;
  event.size = 3;
  event.data[0] = 0xB0;
  event.data[1] = 74;
  event.data[2] = 64;
  check(engine.dispatch(event), "learn dispatch finds the binding");
  checkNear(value, 64.0 / 127.0, 1e-6, "learn value is scaled to 0..1");
  event.data[1] = 75;
  check(!engine.dispatch(event), "unbound control is ignored");
  check(engine.removeBinding(74, 1), "learn binding removed");
}

void wavTests() {
  const auto path = std::filesystem::temp_directory_path() /
                    ("mnkybox-wav-test-" +
                     std::to_string(std::chrono::steady_clock::now()
                                        .time_since_epoch()
                                        .count()) +
                     ".wav");
  constexpr std::uint32_t frames = 480;
  constexpr std::uint16_t channels = 2;
  std::vector<unsigned char> bytes;
  appendTag(bytes, "RIFF");
  appendLe32(bytes, 36 + frames * channels * 4);
  appendTag(bytes, "WAVE");
  appendTag(bytes, "fmt ");
  appendLe32(bytes, 16);
  appendLe16(bytes, 3);  // IEEE float
  appendLe16(bytes, channels);
  appendLe32(bytes, 48000);
  appendLe32(bytes, 48000 * channels * 4);
  appendLe16(bytes, static_cast<std::uint16_t>(channels * 4));
  appendLe16(bytes, 32);
  appendTag(bytes, "data");
  appendLe32(bytes, frames * channels * 4);
  bytes.resize(bytes.size() + frames * channels * 4, 0);
  {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  }
  const auto info = readWavInfo(path);
  check(info.ok, "WAV header parses");
  check(info.channels == channels && info.sampleRate == 48000 && info.format == 3,
        "WAV format fields");
  check(info.frames == frames, "WAV frame count");
  checkNear(wavDurationSeconds(path), 0.01, 1e-9, "WAV duration");
  // the import rule is a header check, and its refusal names the rate.
  check(importSampleRateError(48000).empty(),
        "48 kHz is the import rate");
  check(importSampleRateError(44100) ==
            "import refused: 44.1 kHz file - convert to 48 kHz first",
        "a 44.1 kHz file is refused with the conversion hint");
  check(importSampleRateError(0).find("no readable WAV header") != std::string::npos,
        "an unreadable header is refused before the rate rule");
  std::error_code error;
  std::filesystem::remove(path, error);
}

void writeMinimalWav(const std::filesystem::path& path) {
  constexpr std::uint32_t frames = 480;
  constexpr std::uint16_t channels = 1;
  std::vector<unsigned char> bytes;
  appendTag(bytes, "RIFF");
  appendLe32(bytes, 36 + frames * channels * 4);
  appendTag(bytes, "WAVE");
  appendTag(bytes, "fmt ");
  appendLe32(bytes, 16);
  appendLe16(bytes, 3);
  appendLe16(bytes, channels);
  appendLe32(bytes, 48000);
  appendLe32(bytes, 48000 * channels * 4);
  appendLe16(bytes, static_cast<std::uint16_t>(channels * 4));
  appendLe16(bytes, 32);
  appendTag(bytes, "data");
  appendLe32(bytes, frames * channels * 4);
  bytes.resize(bytes.size() + frames * channels * 4, 0);
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  std::ofstream file(path, std::ios::binary);
  file.write(reinterpret_cast<const char*>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
}

// A deterministic tone so the EDIT popup's waveform assertions have a real
// peak to compare across an APPLY (writeMinimalWav is silent by design).
void writeToneWav(const std::filesystem::path& path, std::uint32_t frames,
                  std::uint16_t channels, double amplitude) {
  std::vector<unsigned char> bytes;
  appendTag(bytes, "RIFF");
  appendLe32(bytes, 36 + frames * channels * 4);
  appendTag(bytes, "WAVE");
  appendTag(bytes, "fmt ");
  appendLe32(bytes, 16);
  appendLe16(bytes, 3);  // IEEE float
  appendLe16(bytes, channels);
  appendLe32(bytes, 48000);
  appendLe32(bytes, 48000 * channels * 4);
  appendLe16(bytes, static_cast<std::uint16_t>(channels * 4));
  appendLe16(bytes, 32);
  appendTag(bytes, "data");
  appendLe32(bytes, frames * channels * 4);
  const std::size_t start = bytes.size();
  bytes.resize(start + static_cast<std::size_t>(frames) * channels * 4, 0);
  for (std::uint32_t frame = 0; frame < frames; ++frame) {
    const double t = static_cast<double>(frame) / 48000.0;
    const float value = static_cast<float>(
        amplitude * std::sin(2.0 * 3.14159265358979323846 * 220.0 * t));
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof bits);
    for (std::uint16_t channel = 0; channel < channels; ++channel) {
      const std::size_t offset = start +
                                 static_cast<std::size_t>(frame) * channels * 4 +
                                 static_cast<std::size_t>(channel) * 4;
      for (int shift = 0; shift < 32; shift += 8) {
        bytes[offset + static_cast<std::size_t>(shift / 8)] =
            static_cast<unsigned char>((bits >> shift) & 0xFFu);
      }
    }
  }
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  std::ofstream file(path, std::ios::binary);
  file.write(reinterpret_cast<const char*>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
}

void takeAdoptionTests() {
  AppState state;
  SessionSettings settings;
  MixerModel mixer;
  FxManager fx;
  PerformanceModel performance;
  MidiGlue midi(false);
  AudioGlue audio(mixer, fx, performance, state, true);
  LinkClient link(midi, state, true);
  const auto base = std::filesystem::temp_directory_path() /
                    ("mnkybox-take-adopt-" +
                     std::to_string(std::chrono::steady_clock::now()
                                        .time_since_epoch()
                                        .count()));
  std::error_code error;
  std::filesystem::create_directories(base, error);
  state.setTakeDirectory(base);
  RecorderViewModel recorder(state, audio, mixer, link, settings);
  const auto first = base / "take-20260101T000000Z";
  writeMinimalWav(first / "ch00.wav");
  writeMinimalWav(first / "ch01.wav");
  check(recorder.adoptRecordedTake(first.string(), 0x3u) == 2,
        "recorded channel files are adopted");
  check(std::filesystem::exists(first / "01-ch01-001.wav"),
        "track 1 take uses nn-trackname-take#");
  check(std::filesystem::exists(first / "02-ch02-001.wav"),
        "track 2 take uses nn-trackname-take#");
  check(settings.activeTake(0) == "01-ch01-001.wav",
        "adopted take becomes the active pointer");

  const auto second = base / "take-20260101T000001Z";
  writeMinimalWav(second / "ch01.wav");
  check(recorder.adoptRecordedTake(second.string(), 0x2u) == 1,
        "second recorded take is adopted");
  check(std::filesystem::exists(second / "02-ch02-002.wav"),
        "take counter continues per track");
  std::filesystem::remove_all(base, error);
}

// Edit chain proof: the popup's APPLY has to reach TimelineViewModel's region
// engine, change the region list and change what the header/waveform report.
// GAIN changes the selected peak; TRIM and DELETE change the edited
// duration/region count.
void takeEditViewModelTests() {
  AppState state;
  SessionSettings settings;
  MixerModel mixer;
  FxManager fx;
  PerformanceModel performance;
  MidiGlue midi(false);
  AudioGlue audio(mixer, fx, performance, state, true);
  LinkClient link(midi, state, true);
  const auto base = std::filesystem::temp_directory_path() /
                    ("mnkybox-take-edit-" +
                     std::to_string(std::chrono::steady_clock::now()
                                        .time_since_epoch()
                                        .count()));
  std::error_code error;
  std::filesystem::create_directories(base, error);
  state.setTakeDirectory(base);
  writeToneWav(base / "01-tone-001.wav", 4u * 48000u, 1, 0.8);
  for (int track = 0; track < 3; ++track) {
    settings.setActiveTake(track, "01-tone-001.wav");
  }
  RecorderViewModel recorder(state, audio, mixer, link, settings);
  TimelineViewModel timeline(state, audio, recorder, link, settings);
  timeline.preparePeaksForDemo();

  const auto columnPeak = [](const QVariantList& wave, int column) {
    const int index = column * 2;
    if (index + 1 >= wave.size()) return 0.0;
    return std::max(std::abs(wave[index].toDouble()),
                    std::abs(wave[index + 1].toDouble()));
  };

  // GAIN on IN..OUT only: the region list is cut at both edges first, the
  // middle region carries -6 dB, and the waveform peak at that span halves.
  timeline.openEdit(0);
  checkNear(timeline.takeBeats(), 8.0, 0.01,
            "the 4 s EDIT fixture is 8 beats at 120 BPM");
  const QVariantList gainBefore = timeline.editWave();
  check(gainBefore.size() > 0, "the EDIT popup has a waveform to apply to");
  timeline.beginSelection(0, 2.0);
  timeline.updateSelection(6.0);
  timeline.endSelection();
  timeline.selectEditOp(2);  // GAIN
  timeline.setEditGainDb(-6.0);
  check(timeline.applyEditOp().isEmpty(), "APPLY GAIN reports success");
  const auto& gained = settings.takeEdit[0];
  check(gained.size() == 3,
        "GAIN cuts the take at IN and OUT, so only the selection changes");
  if (gained.size() == 3) {
    check(std::abs(gained[0].offsetBeats - 0.0) < 1e-9 &&
              std::abs(gained[0].lengthBeats - 2.0) < 1e-9 &&
              std::abs(gained[0].gainDb) < 1e-6,
          "GAIN leaves the head of the take untouched");
    check(std::abs(gained[1].offsetBeats - 2.0) < 1e-9 &&
              std::abs(gained[1].lengthBeats - 4.0) < 1e-9 &&
              std::abs(gained[1].gainDb + 6.0) < 1e-6,
          "GAIN applies -6 dB to the selected span only");
    // The tail keeps the "to the end of the take" marker (lengthBeats 0).
    check(std::abs(gained[2].offsetBeats - 6.0) < 1e-9 &&
              std::abs(gained[2].lengthBeats) < 1e-9 &&
              std::abs(gained[2].gainDb) < 1e-6,
          "GAIN leaves the tail of the take untouched");
  }
  const QVariantList gainAfter = timeline.editWave();
  check(gainAfter.size() == gainBefore.size(),
        "the applied waveform keeps the popup's column count");
  // Column 400 is beat 4, inside the selection; column 100 is beat 1, outside.
  const double selectedBefore = columnPeak(gainBefore, 400);
  const double selectedAfter = columnPeak(gainAfter, 400);
  const double untouchedBefore = columnPeak(gainBefore, 100);
  const double untouchedAfter = columnPeak(gainAfter, 100);
  check(selectedBefore > 0.05 &&
            std::abs(selectedAfter / selectedBefore - 0.5012) < 0.02,
        "GAIN's waveform peak is 6 dB lower inside the selection");
  check(std::abs(untouchedAfter - untouchedBefore) < 1e-6,
        "GAIN's waveform outside the selection is unchanged");
  check(timeline.canUndo() && timeline.undo(),
        "the applied GAIN is on the undo stack");
  check(settings.takeEdit[0].empty(),
        "UNDO returns the take to no committed regions");

  // TRIM on IN..OUT: one region remains, the edited duration is the selection
  // and the header says so (the meta property emits editChanged).
  timeline.openEdit(1);
  timeline.beginSelection(1, 2.0);
  timeline.updateSelection(6.0);
  timeline.endSelection();
  timeline.selectEditOp(0);  // TRIM
  check(timeline.applyEditOp().isEmpty(), "APPLY TRIM reports success");
  check(settings.takeEdit[1].size() == 1,
        "TRIM keeps one region for a single-region take");
  if (settings.takeEdit[1].size() == 1) {
    check(std::abs(settings.takeEdit[1][0].offsetBeats - 2.0) < 1e-9 &&
              std::abs(settings.takeEdit[1][0].lengthBeats - 4.0) < 1e-9,
          "TRIM keeps exactly IN..OUT");
  }
  checkNear(timeline.editedBeats(), 4.0, 1e-9,
            "TRIM's edited duration is the 4-beat selection");
  check(timeline.editMetaText().contains(QStringLiteral("4.0 edited")),
        "the EDIT header states the trimmed duration");
  const QVariantList trimWave = timeline.editWave();
  check(columnPeak(trimWave, 100) == 0.0,
        "TRIM silences the waveform before IN");
  check(columnPeak(trimWave, 400) > 0.05,
        "TRIM keeps the waveform inside IN..OUT");
  check(columnPeak(trimWave, 700) == 0.0,
        "TRIM silences the waveform after OUT");

  // DELETE on IN..OUT: the selected span is cut out, the two remaining parts
  // stay as regions, and both the region count and the edited duration change.
  timeline.openEdit(2);
  timeline.beginSelection(2, 2.0);
  timeline.updateSelection(6.0);
  timeline.endSelection();
  timeline.selectEditOp(6);  // DELETE REGION
  check(timeline.applyEditOp().isEmpty(), "APPLY DELETE reports success");
  const auto& deleted = settings.takeEdit[2];
  check(deleted.size() == 2,
        "DELETE removes the selected span and keeps the two sides");
  if (deleted.size() == 2) {
    // The tail keeps the "to the end of the take" marker (lengthBeats 0).
    check(std::abs(deleted[0].offsetBeats - 0.0) < 1e-9 &&
              std::abs(deleted[0].lengthBeats - 2.0) < 1e-9 &&
              std::abs(deleted[1].offsetBeats - 6.0) < 1e-9 &&
              std::abs(deleted[1].lengthBeats) < 1e-9,
          "DELETE's regions are the head and the tail of the take");
  }
  checkNear(timeline.editedBeats(), 4.0, 1e-9,
            "DELETE shortens the edited duration by the selection");
  checkNear(timeline.takeBeats(), 8.0, 1e-9,
            "DELETE leaves the source take length alone");
  check(timeline.editMetaText().contains(QStringLiteral("4.0 edited")),
        "the EDIT header states the post-delete duration");
  check(timeline.lastOpText().startsWith(QStringLiteral("DELETE")) &&
            timeline.regionCount() == 2,
        "DELETE names itself and the new region count");
  const QVariantList deleteWave = timeline.editWave();
  check(columnPeak(deleteWave, 400) == 0.0,
        "DELETE clears the waveform across the removed span");
  check(columnPeak(deleteWave, 100) > 0.05 && columnPeak(deleteWave, 700) > 0.05,
        "DELETE keeps the waveform on both sides");
  // A delete that would empty the list is refused: an empty region list means
  // CLEAR (unedited take) to the engine, which would silently undo the edit.
  timeline.beginSelection(2, 0.0);
  timeline.updateSelection(8.0);
  timeline.endSelection();
  const int beforeRefusal = timeline.regionCount();
  check(!timeline.applyEditOp().isEmpty(),
        "DELETE that would empty the region list is refused");
  check(timeline.regionCount() == beforeRefusal,
        "a refused DELETE leaves the region list untouched");

  // ---- UNDO model -----------------------------------------------------
  // The stack stores the region list as it was BEFORE each APPLY, so UNDO is an
  // exact restore of the previous take state and not a reverse arithmetic. The
  // checks below walk the whole op selector, the LIFO order, the bottom of the
  // stack, the >=16 level depth and the popup/track lifetime.
  using Regions = std::vector<session::TakeRegion>;
  const auto sameRegions = [](const Regions& a, const Regions& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
      const auto& x = a[i];
      const auto& y = b[i];
      if (x.take != y.take || std::abs(x.offsetBeats - y.offsetBeats) > 1e-9 ||
          std::abs(x.lengthBeats - y.lengthBeats) > 1e-9 ||
          std::abs(x.gainDb - y.gainDb) > 1e-6 ||
          std::abs(x.fadeInBeats - y.fadeInBeats) > 1e-9 ||
          std::abs(x.fadeOutBeats - y.fadeOutBeats) > 1e-9 ||
          x.normalize != y.normalize) {
        return false;
      }
    }
    return true;
  };
  const auto undoAll = [&timeline](int track) {
    timeline.openEdit(track);
    for (int guard = 0; guard < 64 && timeline.undo(); ++guard) {
    }
  };
  check(TimelineViewModel::kUndoDepth >= 16,
        "the per-take undo stack holds at least the 16 levels the owner asked for");

  // Every op card is reversible: apply it, then UNDO restores the previous
  // region list exactly. GAIN and the two fades need their drafted value; SPLIT
  // cuts at the cursor and ignores the selection.
  struct OpCase {
    int op;
    const char* name;
  };
  const OpCase opCases[] = {{0, "TRIM"},      {1, "SPLIT"},   {2, "GAIN"},
                            {3, "FADE IN"},   {4, "FADE OUT"}, {5, "NORMALIZE"},
                            {6, "DELETE"}};
  for (const auto& opCase : opCases) {
    undoAll(0);
    if (opCase.op == 1) {
      timeline.clearSelection();
      timeline.setCursorBeats(4.0);
    } else {
      timeline.beginSelection(0, 2.0);
      timeline.updateSelection(6.0);
      timeline.endSelection();
    }
    timeline.selectEditOp(opCase.op);
    if (opCase.op == 2) timeline.setEditGainDb(-6.0);
    if (opCase.op == 3) timeline.setEditFadeIn(1.0);
    if (opCase.op == 4) timeline.setEditFadeOut(1.0);
    const Regions before = settings.takeEdit[0];
    const QVariantList waveBefore = timeline.editWave();
    check(timeline.applyEditOp().isEmpty(),
          std::string("APPLY ") + opCase.name + " reports success");
    check(!sameRegions(settings.takeEdit[0], before),
          std::string("APPLY ") + opCase.name + " changes the take state");
    const Regions applied = settings.takeEdit[0];
    check(!applied.empty(),
          std::string("APPLY ") + opCase.name + " leaves committed regions");
    check(timeline.canUndo(),
          std::string("APPLY ") + opCase.name + " pushes one undo level");
    check(timeline.undo(),
          std::string("UNDO after ") + opCase.name + " succeeds");
    check(sameRegions(settings.takeEdit[0], before),
          std::string("UNDO restores the exact state before ") + opCase.name);
    check(!timeline.canUndo(),
          std::string("UNDO after ") + opCase.name + " pops the only level");
    if (opCase.op == 2) {
      check(timeline.editWave() == waveBefore,
            "UNDO repaints the waveform back to the pre-GAIN peaks");
    }
  }

  // LIFO order: three stacked edits unwind newest first, and each level returns
  // exactly the state the previous level pushed.
  undoAll(0);
  timeline.beginSelection(0, 2.0);
  timeline.updateSelection(6.0);
  timeline.endSelection();
  timeline.selectEditOp(2);
  timeline.setEditGainDb(-6.0);
  check(timeline.applyEditOp().isEmpty(), "stacked op 1 (GAIN) applies");
  const Regions afterGain = settings.takeEdit[0];
  timeline.beginSelection(0, 3.0);
  timeline.updateSelection(5.0);
  timeline.endSelection();
  timeline.selectEditOp(3);
  timeline.setEditFadeIn(1.0);
  check(timeline.applyEditOp().isEmpty(), "stacked op 2 (FADE IN) applies");
  const Regions afterFade = settings.takeEdit[0];
  timeline.beginSelection(0, 7.0);
  timeline.updateSelection(8.0);
  timeline.endSelection();
  timeline.selectEditOp(6);  // DELETE REGION
  check(timeline.applyEditOp().isEmpty(), "stacked op 3 (DELETE) applies");
  check(timeline.undo() && sameRegions(settings.takeEdit[0], afterFade),
        "the first UNDO pops the newest APPLY (DELETE restores FADE's state)");
  check(timeline.undo() && sameRegions(settings.takeEdit[0], afterGain),
        "the second UNDO restores the state after the first op");
  check(timeline.undo() && settings.takeEdit[0].empty(),
        "the third UNDO restores the unedited take");
  check(!timeline.canUndo(), "the stack reports empty at the bottom");
  check(!timeline.undo(), "UNDO at the bottom of the stack is a no-op");
  check(settings.takeEdit[0].empty(),
        "the bottom-of-stack no-op leaves the take untouched");

  // Depth: 17 stacked GAIN applies (over the 16-level floor) all unwind.
  undoAll(0);
  timeline.beginSelection(0, 2.0);
  timeline.updateSelection(6.0);
  timeline.endSelection();
  timeline.selectEditOp(2);
  bool allApplied = true;
  for (int step = 0; step < 17; ++step) {
    timeline.setEditGainDb(-1.0 - static_cast<double>(step));
    allApplied = allApplied && timeline.applyEditOp().isEmpty();
  }
  check(allApplied, "17 stacked GAIN applies all report success");
  int undoneLevels = 0;
  while (undoneLevels < 64 && timeline.undo()) ++undoneLevels;
  check(undoneLevels == 17,
        "17 UNDO presses walk the whole stack back (>= 16 levels kept)");
  check(settings.takeEdit[0].empty() && !timeline.canUndo(),
        "the 17-level walk ends on the unedited take with an empty stack");

  // Lifetime: the stack belongs to the session take, so closing and reopening
  // the popup keeps it; a different track has its own stack.
  timeline.selectEditOp(2);
  timeline.setEditGainDb(-3.0);
  check(timeline.applyEditOp().isEmpty(), "lifetime GAIN applies");
  timeline.closeEdit();
  check(timeline.canUndo(), "the undo stack survives closing the EDIT popup");
  timeline.openEdit(0);
  check(timeline.canUndo(), "the undo stack survives reopening the EDIT popup");
  check(timeline.undo() && settings.takeEdit[0].empty(),
        "UNDO after reopening restores the pre-edit take");
  // Track 1 still carries the TRIM from above: its stack is independent of
  // track 0's empty stack, and undoing it restores that track only.
  timeline.openEdit(1);
  check(timeline.canUndo(), "a different track keeps its own undo level");
  check(timeline.undo(), "track 1's own level pops on its own stack");
  check(settings.takeEdit[1].empty() && settings.takeEdit[0].empty(),
        "track 1's UNDO restores only track 1 (track 0 stays popped)");

  std::filesystem::remove_all(base, error);
}

void sessionClickTests() {
  session::Snapshot snapshot;
  snapshot.click.mode = 2;
  snapshot.click.output = 1;
  snapshot.click.gainDb = -12.0f;
  snapshot.click.bars = 2;
  snapshot.transport.bpmX100 = 13300;
  const auto path = std::filesystem::temp_directory_path() /
                    ("mnkybox-click-" +
                     std::to_string(std::chrono::steady_clock::now()
                                        .time_since_epoch()
                                        .count()) +
                     ".mnkybox");
  check(session::save(path.string(), snapshot), "session file writes");
  const auto result = session::load(path.string());
  check(result.ok, "session file reads back");
  check(result.snapshot.click.mode == 2, "click.mode round-trips");
  check(result.snapshot.click.output == 1, "click.output round-trips");
  checkNear(result.snapshot.click.gainDb, -12.0, 1e-6, "click.gainDb round-trips");
  check(result.snapshot.click.bars == 2, "click.bars round-trips");
  check(result.snapshot.transport.bpmX100 == 13300, "tempo round-trips");
  std::error_code error;
  std::filesystem::remove(path, error);
}

void takeNameTests() {
  check(parseTakeFileName("02-drums-003.wav").valid, "per-track take name parses");
  const auto parts = parseTakeFileName("02-drums-003");
  check(parts.valid && parts.track == 1 && parts.name == "drums" && parts.number == 3,
        "per-track take name fields");
  checkEqual(takeFileName(1, "Drums", 3), "02-drums-003.wav",
             "take file name uses nn-trackname-take#");
  check(sanitizeTrackName("Drum Kit!") == "drum-kit", "track name sanitizes to ASCII");
  check(validateTakeName("02-drums-003.wav").empty(), "per-track name is a valid take name");
  check(validateTakeName("take-20260101T120000Z").empty(), "legacy take name stays valid");
  check(!validateTakeName("session-1").empty(), "unprefixed take name is rejected");
  check(parseTakeFileName("2-drums-3.wav").valid == false, "short take name is rejected");
}

void meterAndSpaceTests() {
  ClipHold hold;
  check(!hold.clipped(), "clip hold starts clear");
  check(!hold.update(0.5f), "below full scale does not clip");
  check(hold.update(1.0f), "full scale latches the clip");
  for (int tick = 0; tick < ClipHold::kHoldTicks - 1; ++tick) (void)hold.tick();
  check(hold.clipped(), "clip hold survives the hold window");
  (void)hold.tick();
  check(!hold.clipped(), "clip hold expires after the window");
  check(isLowDiskSpace(1ull * 1024 * 1024 * 1024, 100ull * 1024 * 1024 * 1024),
        "under 2 GiB is low space");
  check(!isLowDiskSpace(20ull * 1024 * 1024 * 1024, 100ull * 1024 * 1024 * 1024),
        "20 GiB free is not low space");
  check(!isLowDiskSpace(0, 0), "unknown capacity is not low space");
}


void learnCaptureTests() {
  MidiGlue midi(false);
  LearnViewModel learn(midi);
  learn.setTargetIndex(0);
  learn.startCapture();
  check(learn.capturing(), "learn capture mode is armed");
  midi::MidiEvent event;
  event.size = 3;
  event.data[0] = 0xB2;
  event.data[1] = 74;
  event.data[2] = 100;
  midi.handleInput(event);
  learn.poll();
  check(!learn.capturing(), "one control change completes the capture");
  check(learn.bindings().size() == 1, "captured CC creates one learn binding");
  check(learn.bindings().value(0).contains(QStringLiteral("Mixer 01 GAIN")),
        "captured CC binds the selected target under its human name");
  check(!learn.bindings().value(0).contains(QStringLiteral("mixer.ch01.gain")),
        "binding rows never show the internal learn path");
  learn.removeBinding(0);
  check(learn.bindings().isEmpty(), "learn binding removal");
}

void learnTargetCaptureTests() {
  MidiGlue midi(false);
  LearnViewModel learn(midi);
  // Arm-then-touch: ARM CAPTURE with no target waits for a touch, and the
  // touched control becomes the capture target before any CC arrives.
  learn.armTargetCapture();
  check(learn.targetArmed(), "ARM CAPTURE without a target arms the touch pick");
  check(!learn.capturing(), "the CC capture does not start before a target");
  learn.setTargetByPath(QStringLiteral("mixer.ch02.gain"));
  check(!learn.targetArmed(), "picking a target ends the touch pick");
  check(learn.capturing(), "the CC capture starts once the target is picked");
  check(learn.captureTargetLabel() == QStringLiteral("Mixer 02 GAIN"),
        "the picked target reads as a human name");
  midi::MidiEvent event;
  event.size = 3;
  event.data[0] = 0xB0;  // channel 1
  event.data[1] = 21;
  event.data[2] = 64;
  midi.handleInput(event);
  learn.poll();
  check(learn.bindings().size() == 1, "the touched target binds the next CC");
  check(learn.bindings().value(0).contains(QStringLiteral("CC 21 ch1 -> Mixer 02 GAIN")),
        "the binding row names the CC and the human target");
  learn.removeBinding(0);
}

void songPersistenceTests() {
  AppState state;
  SessionSettings settings;
  MixerModel mixer;
  FxManager fx;
  PerformanceModel performance;
  EqModel eq;
  MidiGlue midi(false);
  AudioGlue audio(mixer, fx, performance, state, true);
  LinkClient link(midi, state, true);
  AppContext context{state, mixer, fx, midi, audio, link, settings, eq, performance};

  const auto directory = std::filesystem::temp_directory_path() /
                         ("mnkybox-song-test-" +
                          std::to_string(std::chrono::steady_clock::now()
                                             .time_since_epoch()
                                             .count()));
  std::string error;
  settings.songMode = false;
  settings.songLengthBeats = 48.0;
  settings.loopOn = false;
  settings.loopStartBeats = 8.0;
  settings.loopEndBeats = 24.0;
  settings.song.sections.push_back(session::SongSection{"INTRO", 4});
  settings.song.sections.push_back(session::SongSection{"VERSE", 8});
  session::SongClip clip;
  clip.track = 1;
  clip.startBeats = 16.0;
  clip.take = "02-drums-003.wav";
  clip.offsetBeats = 1.0;
  clip.lengthBeats = 8.0;
  clip.gainDb = -2.5f;
  clip.fadeInBeats = 0.25;
  clip.fadeOutBeats = 0.5;
  clip.normalize = true;
  clip.loop = true;
  clip.muted = false;
  settings.song.clips.push_back(clip);
  settings.song.grid = 2;

  check(project::save(context, directory, error),
        "song state: project save writes the session file");
  check(!std::filesystem::exists(directory / project::kLegacyArrangeFileName),
        "song state: save no longer writes the legacy arrange sidecar");
  settings.reset();
  check(project::load(context, directory, error),
        "song state: project load reads the session file");
  check(!settings.songMode && std::abs(settings.songLengthBeats - 48.0) < 1e-9 &&
            !settings.loopOn && std::abs(settings.loopStartBeats - 8.0) < 1e-9 &&
            std::abs(settings.loopEndBeats - 24.0) < 1e-9,
        "song state: mode/length/loop round-trip through the session song");
  check(settings.song.sections.size() == 2 &&
            settings.song.sections[0].name == "INTRO" &&
            settings.song.sections[1].bars == 8,
        "song state: sections round-trip through the session song");
  check(settings.song.clips.size() == 1 &&
            settings.song.clips[0].track == 1 &&
            settings.song.clips[0].take == "02-drums-003.wav" &&
            std::abs(settings.song.clips[0].lengthBeats - 8.0) < 1e-9 &&
            settings.song.clips[0].loop && settings.song.clips[0].normalize,
        "song state: clips round-trip through the session song");
  check(!settings.song.mode && std::abs(settings.song.lengthBeats - 48.0) < 1e-9 &&
            !settings.song.loop.on &&
            std::abs(settings.song.loop.startBeats - 8.0) < 1e-9 &&
            std::abs(settings.song.loop.endBeats - 24.0) < 1e-9,
        "song state: load mirrors the timing fields into the session object");

  // Rewrite project.mnkybox as the old writer left it: song sections/clips and
  // the deprecated lengthBars, but none of the timing keys. The legacy sidecar
  // must fold its mode/length/loop into that project and then vanish.
  const auto projectFile = directory / project::kProjectFileName;
  {
    QFile file(QString::fromStdString(projectFile.string()));
    check(file.open(QIODevice::ReadOnly), "song migration: project file readable");
    auto document = QJsonDocument::fromJson(file.readAll());
    file.close();
    auto root = document.object();
    auto song = root.value(QStringLiteral("song")).toObject();
    song.remove(QStringLiteral("mode"));
    song.remove(QStringLiteral("lengthBeats"));
    song.remove(QStringLiteral("loop"));
    song.insert(QStringLiteral("lengthBars"), 12);
    root.insert(QStringLiteral("song"), song);
    check(file.open(QIODevice::WriteOnly | QIODevice::Truncate),
          "song migration: legacy project file writable");
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
  }
  {
    const auto sidecar = directory / project::kLegacyArrangeFileName;
    QFile file(QString::fromStdString(sidecar.string()));
    check(file.open(QIODevice::WriteOnly | QIODevice::Truncate),
          "song migration: legacy sidecar writable");
    file.write(R"({"song":{"mode":false,"lengthBeats":40,"loop":{"on":false,"startBeats":4,"endBeats":20}}})");
    file.close();
    check(std::filesystem::exists(sidecar), "song migration: sidecar fixture exists");
  }
  settings.reset();
  check(project::load(context, directory, error),
        "song migration: legacy project still loads");
  check(!settings.songMode && std::abs(settings.songLengthBeats - 40.0) < 1e-9 &&
            !settings.loopOn && std::abs(settings.loopStartBeats - 4.0) < 1e-9 &&
            std::abs(settings.loopEndBeats - 20.0) < 1e-9,
        "song migration: sidecar wins for the timing fields it carries");
  check(settings.song.sections.size() == 2 && settings.song.clips.size() == 1,
        "song migration: project.mnkybox stays authoritative for sections/clips");
  check(!std::filesystem::exists(directory / project::kLegacyArrangeFileName),
        "song migration: the folded sidecar is removed");

  // The folded state lives in project.mnkybox. A stale sidecar left by an
  // interrupted migration must not overwrite it.
  check(project::save(context, directory, error),
        "song migration: migrated project saves the session song");
  {
    const auto sidecar = directory / project::kLegacyArrangeFileName;
    QFile file(QString::fromStdString(sidecar.string()));
    check(file.open(QIODevice::WriteOnly | QIODevice::Truncate),
          "song migration: stale sidecar writable");
    file.write(R"({"song":{"mode":true,"lengthBeats":64,"loop":{"on":true,"startBeats":0,"endBeats":64}}})");
    file.close();
  }
  settings.reset();
  check(project::load(context, directory, error),
        "song migration: the project with session song data loads");
  check(!settings.songMode && std::abs(settings.songLengthBeats - 40.0) < 1e-9 &&
            !settings.loopOn && std::abs(settings.loopStartBeats - 4.0) < 1e-9 &&
            std::abs(settings.loopEndBeats - 20.0) < 1e-9,
        "song migration: a stale sidecar never overrides the session song");
  check(!std::filesystem::exists(directory / project::kLegacyArrangeFileName),
        "song migration: the stale sidecar is removed after it is ignored");

  // Shutdown flush: nothing to do when no project is open and nothing is
  // dirty; dirty state without a folder fails loudly; an open project saves
  // through the same session snapshot path.
  state.clearDirty();
  state.setCurrentProject({});
  check(project::flushCurrentProject(context, error),
        "shutdown flush: a clean session with no project needs no write");
  state.markDirty();
  check(!project::flushCurrentProject(context, error) &&
            error.find("no project folder") != std::string::npos,
        "shutdown flush: dirty state without a project fails loudly");
  state.setCurrentProject(directory);
  check(project::flushCurrentProject(context, error),
        "shutdown flush: the open project saves through the session path");
  check(std::filesystem::exists(directory / project::kProjectFileName),
        "shutdown flush: the session file is on disk after the flush");
  state.clearDirty();

  std::error_code removeError;
  std::filesystem::remove_all(directory, removeError);
}

void projectRoundTripTests() {
  AppState state;
  SessionSettings settings;
  MixerModel mixer;
  FxManager fx;
  PerformanceModel performance;
  EqModel eq;
  MidiGlue midi(false);
  AudioGlue audio(mixer, fx, performance, state, true);
  LinkClient link(midi, state, true);
  AppContext context{state, mixer, fx, midi, audio, link, settings, eq, performance};

  mixer.setGainDb(2, -6.0f);
  mixer.setPan(2, 0.25f);
  state.tempoX100.store(13300);
  state.clickEnabled.store(true);
  state.clickMode.store(2);
  state.clickLevel.store(0.3f);
  state.clickGainDb.store(-10.45f);
  state.clickOutput.store(1);
  state.clickBars.store(2);
  mixer.setCueSend(2, 0.4f);
  mixer.setOut34Send(2, 0.75f);
  mixer.setOut34Mode(1);
  mixer.setMode(2, 0);
  mixer.setMode(3, 2);
  mixer.setMode(4, 3);
  settings.punch.playFrom = session::PunchPoint{true, 1};
  settings.punch.in = session::PunchPoint{true, 5};
  settings.punch.out = session::PunchPoint{true, 9};
  settings.cue.phonesSrc = 1;
  settings.cue.style = 1;
  settings.sends.cue = 0;
  settings.sends.fx1 = 1;
  settings.sends.fx2 = 0;
  settings.sends.out34 = 1;
  settings.sends.rec = 0;
  settings.mainComp.on = true;
  settings.mainComp.threshDb = -18.0f;
  settings.mainComp.ratio = 4.0f;
  settings.mainComp.attackMs = 12.0f;
  settings.mainComp.releaseMs = 120.0f;
  settings.mainComp.makeupDb = 3.0f;
  settings.mainComp.mode = 1;
  settings.setActiveTake(2, "02-drums-003.wav");
  settings.takeLoop[2] = session::LoopState{true, 1.5, 7.25};
  settings.takeEdit[2].push_back(session::TakeRegion{
      "02-drums-003.wav", 1.0, 4.0, -3.0f, 0.5, 0.25, true});
  // One Wave-1 and one clocked Wave-2 pipe on a Pi-owned route: the pipe lists
  // are app-owned in MidiGlue, so the round trip must carry them even though
  // session::apply() ignores RouteState::pipes.
  std::vector<session::MidiPipe> pipeChain;
  session::MidiPipe filter;
  filter.type = session::MidiPipeType::Filter;
  filter.params = {1, 1, 1, 1, 1, 1, 1, 0};
  pipeChain.push_back(filter);
  session::MidiPipe arp;
  arp.type = session::MidiPipeType::Arp;
  arp.bypass = true;
  arp.params = {1, 0, 2, 0, 20, 90};
  pipeChain.push_back(arp);
  check(midi.setRoutePipes(midi::Port::TrsIn1, midi::Port::UsbOut, pipeChain),
        "project test stores a pipe chain");
  settings.clock.trs[0].clock = true;
  settings.clock.trs[0].transport = false;
  settings.clock.trs[1].clock = false;
  settings.clock.trs[1].transport = true;
  settings.clock.usb.clock = true;
  settings.clock.usb.transport = false;
  settings.deviceOut.main = {6, 7};
  settings.deviceOut.out34 = {0, 1};
  settings.deviceOut.phones = {8, 9};
  mixer.setInCh(2, 4);
  mixer.setInPair(2, true);
  mixer.setOutDst(2, 1);
  mixer.setInCh(3, -1);
  mixer.setOutDst(3, 3);
  // Dynamic track list and the per-track FX rack: the ordered slots and their
  // names are session v2 mixer.tracks, the rack is mixer.channels[].fxSlots
  // (the deprecated "insert" key is written as the FX1 view).
  mixer.addTrack(2, "keys");
  mixer.addTrack(0, "drums");
  mixer.addTrack(4, "");
  MixerModel::FxSlot projectDelay;
  projectDelay.id = "builtin.delay";
  projectDelay.bypass = true;
  projectDelay.wet = 0.5f;
  projectDelay.params = {250.0, 0.55, 0.3, 0.0, 3.0, 0.0, 1200.0, 1.5};
  mixer.setFxSlot(2, 0, projectDelay);
  MixerModel::FxSlot projectReverb;
  projectReverb.id = "builtin.reverb";
  projectReverb.wet = 0.25f;
  projectReverb.params = {20.0, 0.8, 0.4, 0.6, 2.0, 1.4};
  mixer.setFxSlot(2, 1, projectReverb);
  // A CLAP slot round-trips by plugin id plus the plugin's positional
  // parameter vector. Track 4 slot 0 keeps the built-in slots at tracks 2/3
  // independent.
  bool clapProject = !fx.plugins().empty();
  MixerModel::FxSlot projectClap;
  double clapFirstParam = 0.0;
  if (clapProject) {
    const auto& plugin = fx.plugins().front();
    projectClap.id = plugin.id;
    projectClap.wet = 0.8f;
    for (const auto& param : plugin.params) projectClap.params.push_back(param.value);
    if (!projectClap.params.empty()) {
      clapFirstParam = std::clamp(3.5, plugin.params.front().min,
                                  plugin.params.front().max);
      projectClap.params.front() = clapFirstParam;
    }
    mixer.setFxSlot(4, 0, projectClap);
    check(fx.trackEffectLoaded(4, 0),
          "selecting a CLAP slot creates the host instance before save");
  }
  midi.setRoute(midi::Port::TrsIn1, midi::Port::UsbOut, true);
  // Per-device route and its scoped pipe chain: id 6 is a dynamic USB id, so
  // this proves the project capture/load path covers ids 6..31, not only the
  // fixed 3x3 matrix.
  midi.setRoute(midi::dynamicPort(6), midi::Port::UsbOut, true);
  check(midi.routeEnabled(midi::dynamicPort(6), midi::Port::UsbOut),
        "project test stores a per-device route");
  check(midi.setRoutePipes(midi::dynamicPort(6), midi::Port::UsbOut, pipeChain),
        "project test stores a per-device pipe chain");
  check(midi.setRoutePipeChannelMask(midi::dynamicPort(6), midi::Port::UsbOut, 0,
                                     0x000F),
        "project test scopes the per-device pipe to channels 1-4");
  check(midi.addLearnBinding(74, 2, "mixer.ch03.gain", 0),
        "project test learn binding");

  // EQ bands (one track + Main) and the X/Y performance positions ride in the
  // session fx section.
  eq.setSubject(1);
  eq.setType(0, 3);
  eq.setFreqHz(0, 800.0);
  eq.setGainDb(0, -3.0);
  eq.setQ(0, 2.0);
  eq.setType(1, 1);
  eq.setSubject(EqModel::kMainSubject);
  eq.setType(3, 4);
  eq.setFreqHz(3, 9000.0);
  eq.setGainDb(3, 2.5);
  performance.setPoint(PerformanceModel::kDelayGrid, 0.25, 0.75);
  performance.setPoint(PerformanceModel::kReverbGrid, 0.8, 0.2);

  const auto directory = std::filesystem::temp_directory_path() /
                         ("mnkybox-project-test-" +
                          std::to_string(std::chrono::steady_clock::now()
                                             .time_since_epoch()
                                             .count()));
  std::string error;
  check(project::save(context, directory, error), "project save writes a session file");

  mixer.setGainDb(2, 0.0f);
  mixer.setPan(2, 0.5f);
  state.tempoX100.store(12000);
  state.clickEnabled.store(false);
  state.clickMode.store(0);
  state.clickBars.store(1);
  mixer.setCueSend(2, 0.0f);
  mixer.setOut34Send(2, 0.0f);
  mixer.setOut34Mode(0);
  mixer.setMode(2, 1);
  mixer.setMode(3, 1);
  mixer.setMode(4, 1);
  settings.reset();
  eq.setSubject(1);
  eq.setType(0, 0);
  eq.setFreqHz(0, 1000.0);
  eq.setGainDb(0, 0.0);
  eq.setQ(0, 1.0);
  eq.setType(1, 0);
  eq.setSubject(EqModel::kMainSubject);
  eq.setType(3, 0);
  performance.setPoint(PerformanceModel::kDelayGrid, 0.5, 0.5);
  performance.setPoint(PerformanceModel::kReverbGrid, 0.5, 0.5);
  midi.setRoute(midi::Port::TrsIn1, midi::Port::UsbOut, false);
  midi.setRoute(midi::dynamicPort(6), midi::Port::UsbOut, false);
  midi.clearLearnBindings();
  if (clapProject) {
    mixer.setFxSlot(4, 0, {});
    check(!fx.trackEffectLoaded(4, 0),
          "clearing the CLAP slot destroys the instance before load");
  }

  check(project::load(context, directory, error), "project load applies the session file");
  checkNear(mixer.gainDb(2), -6.0, 0.01, "project load restores strip gain");
  checkNear(mixer.pan(2), 0.25, 1e-6, "project load restores strip pan");
  check(state.tempoX100.load() == 13300, "project load restores tempo");
  check(state.clickMode.load() == 2 && state.clickOutput.load() == 1 &&
            state.clickBars.load() == 2,
        "project load restores the click mode/output/bars");
  checkNear(state.clickGainDb.load(), -10.45, 1e-4,
            "project load restores the click gain");
  checkNear(mixer.cueSend(2), 0.4, 1e-6, "project load restores the cue send");
  checkNear(mixer.out34Send(2), 0.75, 1e-6, "project load restores the OUT3&4 send");
  check(mixer.out34Mode() == 1, "project load restores the OUT3&4 mode");
  check(mixer.mode(2) == 0 && mixer.mode(3) == 2 && mixer.mode(4) == 3,
        "project load restores per-track modes");
  check(settings.punch.in.enabled && settings.punch.in.measure == 5 &&
            settings.punch.out.measure == 9,
        "project load restores the punch section");
  check(settings.cue.phonesSrc == 1 && settings.cue.style == 1,
        "project load restores the cue monitor config");
  check(settings.sends.cue == 0 && settings.sends.fx2 == 0 && settings.sends.rec == 0,
        "project load restores the send source matrix");
  check(settings.mainComp.on && settings.mainComp.mode == 1 &&
            settings.mainComp.ratio == 4.0f,
        "project load restores the main compressor");
  check(settings.activeTake(2) == "02-drums-003.wav",
        "project load restores the active take pointer");
  check(settings.takeLoop[2].enabled &&
            std::abs(settings.takeLoop[2].startBeats - 1.5) < 1e-9 &&
            std::abs(settings.takeLoop[2].endBeats - 7.25) < 1e-9,
        "project load restores the per-track take loop");
  check(settings.clock.trs[0].clock && !settings.clock.trs[0].transport &&
            !settings.clock.trs[1].clock && settings.clock.trs[1].transport &&
            settings.clock.usb.clock && !settings.clock.usb.transport,
        "project load restores the per-output clock gates");
  check(settings.deviceOut.main == std::array<int, 2>{6, 7} &&
            settings.deviceOut.out34 == std::array<int, 2>{0, 1} &&
            settings.deviceOut.phones == std::array<int, 2>{8, 9},
        "project load restores the device output channel pairs");
  check(mixer.inCh(2) == 4 && mixer.inPair(2) && mixer.outDst(2) == 1,
        "project load restores the per-track input source and output destination");
  check(mixer.inCh(3) == -1 && mixer.outDst(3) == 3,
        "project load restores a disabled input source and the NONE destination");
  check(mixer.trackCount() == 3 && mixer.slotAt(0) == 2 && mixer.slotAt(1) == 0 &&
            mixer.slotAt(2) == 4,
        "project load restores the ordered dynamic track list");
  check(mixer.trackName(2) == "keys" && mixer.trackName(0) == "drums" &&
            mixer.trackName(4) == defaultTrackName(4),
        "project load restores the track names (empty names keep the default)");
  const auto loadedFx1 = mixer.fxSlot(2, 0);
  check(loadedFx1.id == "builtin.delay" && loadedFx1.bypass &&
            loadedFx1.params.size() == fxParamTable("builtin.delay").size(),
        "project load restores the per-track FX1 slot");
  checkNear(loadedFx1.wet, 0.5, 1e-6, "project load restores the FX1 wet");
  checkNear(loadedFx1.params[audio::fx::Delay::kBpCutoffHz], 1200.0, 1e-9,
            "project load restores a rack parameter");
  const auto loadedFx2 = mixer.fxSlot(2, 1);
  check(loadedFx2.id == "builtin.reverb" && loadedFx2.params.size() ==
                                                 fxParamTable("builtin.reverb").size(),
        "project load restores the per-track FX2 slot");
  checkNear(loadedFx2.wet, 0.25, 1e-6, "project load restores the FX2 wet");
  check(mixer.fxSlot(2, 0).id == "builtin.delay" && mixer.fxSlot(3, 0).id.empty(),
        "a rack slot loads for its own track only");
  if (clapProject) {
    check(mixer.fxSlot(4, 0).id == projectClap.id &&
              mixer.fxSlot(4, 0).params == projectClap.params,
          "project load restores the CLAP slot id and parameter vector");
    check(fx.trackEffectLoaded(4, 0),
          "project load recreates the CLAP host instance lazily");
    if (clapFirstParam != 0.0) {
      check(std::abs(fx.trackParamValue(4, 0, 0) - clapFirstParam) < 1e-9,
            "the restored session parameters reach the plugin's param API");
    }
  }
  check(midi.routeEnabled(midi::Port::TrsIn1, midi::Port::UsbOut),
        "project load restores the routing matrix");
  check(midi.routeEnabled(midi::dynamicPort(6), midi::Port::UsbOut),
        "project load restores a per-device route");
  check(midi.routePipeChannelMask(midi::dynamicPort(6), midi::Port::UsbOut, 0) ==
            0x000F,
        "project load restores the per-device pipe channel scope");
  check(midi.learnBindings().size() == 1 &&
            midi.learnBindings().front().target == "mixer.ch03.gain",
        "project load restores the MIDI-learn map");
  eq.setSubject(1);
  check(eq.type(0) == 3 && std::abs(eq.freqHz(0) - 800.0) < 0.01 &&
            std::abs(eq.gainDb(0) + 3.0) < 0.01 && std::abs(eq.q(0) - 2.0) < 0.01,
        "project load restores a track's EQ bands");
  check(eq.type(1) == 1, "project load restores a cut band type");
  eq.setSubject(EqModel::kMainSubject);
  check(eq.type(3) == 4 && std::abs(eq.gainDb(3) - 2.5) < 0.01,
        "project load restores the Main EQ bands");
  checkNear(eq.freqHz(3), 9000.0, 0.01, "project load restores the Main band frequency");
  checkNear(performance.x(PerformanceModel::kDelayGrid), 0.25, 1e-6,
            "project load restores the delay X/Y position");
  checkNear(performance.y(PerformanceModel::kDelayGrid), 0.75, 1e-6,
            "project load restores the delay Y position");
  checkNear(performance.x(PerformanceModel::kReverbGrid), 0.8, 1e-6,
            "project load restores the reverb X/Y position");
  checkNear(performance.y(PerformanceModel::kReverbGrid), 0.2, 1e-6,
            "project load restores the reverb Y position");

  // Capacity seam: when the session store is narrower than the mixer's track
  // capacity, a slot beyond it must fail the save closed (the session reader
  // rejects such a slot, so a written file could never be loaded again). The
  // guard follows the session store width, so this test is a no-op once the
  // store reaches audio::kMaxTracks.
  const int sessionSlots =
      static_cast<int>(std::tuple_size_v<decltype(session::MixerState::channels)>);
  if (sessionSlots < audio::kMaxTracks) {
    const int overflowSlot = audio::kMaxTracks - 1;
    mixer.addTrack(overflowSlot, "overflow");
    std::string capacityError;
    check(!project::save(context, directory, capacityError) &&
              capacityError.find("session store") != std::string::npos,
          "a track beyond the session store fails the save closed");
    mixer.removeTrackAt(mixer.positionOfSlot(overflowSlot));
    std::string retryError;
    check(project::save(context, directory, retryError),
          "the same project saves once the slot is removed");
  }

  std::error_code removeError;
  std::filesystem::remove_all(directory, removeError);
}

// EQ grid: log-frequency/gain mapping, band types and the summed response, plus
// the X/Y performance mapping and the freeze macro. The mode values come from
// the shipped audio::fx effects, so these checks measure the real DSP.
void eqAndXyTests() {
  EqModel eq;
  check(eq.subject() == EqModel::kMainSubject, "EQ defaults to the Main subject");
  check(eq.band() == 0, "EQ defaults to band 1");
  check(eq.type(0) == 0, "EQ bands default to None");
  checkNear(eq.freqHz(0), 1000.0, 0.5, "EQ bands default to 1 kHz");

  // an untouched subject's four None bands share one frequency, which
  // stacked all four nodes on one spot. The screen's first load spreads them.
  EqModel spread;
  check(spread.ensureVisibleBandDefaults() == 1,
        "an untouched EQ subject gets spread default bands");
  for (int band = 0; band < EqModel::kBandCount; ++band) {
    checkNear(spread.freqHz(band),
              EqModel::kDefaultFreqHz[static_cast<std::size_t>(band)], 0.01,
              "the default bands are spread 100/400/1k/4k");
    check(spread.type(band) == 0, "spread default bands stay None");
  }
  check(spread.ensureVisibleBandDefaults() == 0,
        "a spread EQ is left alone on the next screen load");
  spread.setSubject(0);
  check(spread.ensureVisibleBandDefaults() == 1,
        "stepping to an untouched track spreads that subject");
  checkNear(spread.freqHz(0), EqModel::kDefaultFreqHz[0], 0.01,
            "the untouched track's first node moves to its default");
  spread.setSubject(EqModel::kMainSubject);
  spread.setType(1, 3);
  spread.setFreqHz(1, 5000.0);
  check(spread.ensureVisibleBandDefaults() == 0,
        "an edited EQ is never re-spread");
  checkNear(spread.freqHz(1), 5000.0, 0.01, "an edited band keeps its frequency");

  checkNear(EqModel::freqToX(20.0), 0.0, 1e-9, "20 Hz is the left grid edge");
  checkNear(EqModel::freqToX(20000.0), 1.0, 1e-9, "20 kHz is the right grid edge");
  for (const double hz : {20.0, 100.0, 1000.0, 10000.0, 20000.0}) {
    checkNear(EqModel::xToFreq(EqModel::freqToX(hz)), hz, hz * 1e-9,
              "log-frequency grid round trip");
  }
  checkNear(EqModel::yToGain(EqModel::gainToY(6.0)), 6.0, 1e-9,
            "gain/grid-y round trip");
  checkNear(EqModel::yToGain(0.0), -24.0, 1e-9, "the grid bottom is -24 dB");
  checkNear(EqModel::yToGain(1.0), 24.0, 1e-9, "the grid top is +24 dB");

  // every band must produce a finite grid coordinate for every type and
  // for degenerate stored values (0 Hz, non-finite, far out of range).
  for (int type = 0; type < EqModel::kTypeCount; ++type) {
    EqModel band;
    band.setType(0, type);
    for (const double hz : {0.0, -1.0, 20.0, 1000.0, 20000.0, 1.0e9}) {
      band.setFreqHz(0, hz);
      const double x = band.bandX(0);
      check(std::isfinite(x) && x >= 0.0 && x <= 1.0,
            "every EQ band type maps to a finite x on the grid");
    }
    for (const double db : {-1.0e9, -24.0, 0.0, 24.0, 1.0e9}) {
      band.setGainDb(0, db);
      const double y = band.bandY(0);
      check(std::isfinite(y) && y >= 0.0 && y <= 1.0,
            "every EQ band type maps to a finite y on the grid");
    }
  }
  checkNear(EqModel::freqToX(0.0), 0.0, 1e-9, "0 Hz clamps to the left grid edge");
  checkNear(EqModel::freqToX(std::nan("")), 0.5, 1e-9,
            "a non-finite frequency maps to the grid middle");
  checkNear(EqModel::freqToX(std::numeric_limits<double>::infinity()), 0.5, 1e-9,
            "an infinite frequency maps to the grid middle");
  checkNear(EqModel::gainToY(std::nan("")), 0.5, 1e-9,
            "a non-finite gain maps to the grid middle");

  // A loaded session with all-None bands at 0 Hz still yields finite nodes
  // (the effect clamps the stored value), and the screen load then spreads it.
  EqModel degenerate;
  session::EqState emptyState;
  for (auto& entry : emptyState.main) entry = session::EqBand{0, 0.0f, 0.0f, 0.0f};
  degenerate.apply(emptyState);
  for (int band = 0; band < EqModel::kBandCount; ++band) {
    const double x = degenerate.bandX(band);
    const double y = degenerate.bandY(band);
    check(std::isfinite(x) && x >= 0.0 && x <= 1.0,
          "a degenerate loaded EQ band keeps a finite x");
    check(std::isfinite(y) && y >= 0.0 && y <= 1.0,
          "a degenerate loaded EQ band keeps a finite y");
  }
  degenerate.ensureVisibleBandDefaults();
  for (int band = 0; band < EqModel::kBandCount; ++band) {
    checkNear(degenerate.freqHz(band),
              EqModel::kDefaultFreqHz[static_cast<std::size_t>(band)], 0.01,
              "a degenerate loaded EQ spreads to the default nodes");
  }

  // The QML data source (EqViewModel::nodes) reports four finite grid points
  // for all type states, so the Repeater can never place a node off-grid.
  {
    EqModel viewModel;
    MixerModel mixerModel;
    EqViewModel view(viewModel, mixerModel);
    // nodes() is the QML Repeater's data source: reading it spreads the
    // untouched default, so the grid gets four distinct x positions instead of
    // one stacked point.
    {
      std::vector<double> xs;
      for (const QVariant& entry : view.nodes()) {
        xs.push_back(entry.toMap().value(QStringLiteral("x")).toDouble());
      }
      std::sort(xs.begin(), xs.end());
      check(xs.size() == EqModel::kBandCount && xs[0] + 0.01 < xs[1] &&
                xs[1] + 0.01 < xs[2] && xs[2] + 0.01 < xs[3],
            "the default EQ exposes four distinct node positions");
    }
    checkEqual(view.freqText().toStdString(), "100 Hz",
               "the default band-1 readout matches its spread node");
    for (int type = 0; type < EqModel::kTypeCount; ++type) {
      viewModel.setType(0, type);
      viewModel.setType(1, type);
      viewModel.setType(2, type);
      viewModel.setType(3, type);
      const QVariantList nodes = view.nodes();
      check(nodes.size() == EqModel::kBandCount,
            "the EQ grid always exposes four nodes");
      for (const QVariant& entry : nodes) {
        const QVariantMap node = entry.toMap();
        const double x = node.value(QStringLiteral("x")).toDouble();
        const double y = node.value(QStringLiteral("y")).toDouble();
        check(std::isfinite(x) && x >= 0.0 && x <= 1.0,
              "every EQ node x is a finite normalized coordinate");
        check(std::isfinite(y) && y >= 0.0 && y <= 1.0,
              "every EQ node y is a finite normalized coordinate");
      }
    }
  }

  // Types: None/LCut/LShelf/Param/HShelf/HCut, and cuts carry no gain.
  eq.setType(0, 1);
  check(eq.type(0) == 1 && !EqModel::typeHasGain(1), "a low cut has no gain axis");
  eq.setGainDb(0, 6.0);
  checkNear(eq.gainDb(0), 0.0, 1e-9, "a cut band ignores gain writes");
  eq.setType(0, 3);
  eq.setGainDb(0, 6.0);
  checkNear(eq.gainDb(0), 6.0, 1e-9, "a parametric band accepts gain");
  eq.setType(0, 1);
  checkNear(eq.gainDb(0), 0.0, 1e-9, "switching to a cut neutralises the stored gain");
  eq.setType(0, 5);
  eq.cycleType(0, 1);
  check(eq.type(0) == 0, "type cycling wraps High Cut -> None");
  eq.cycleType(0, -1);
  check(eq.type(0) == 5, "type cycling wraps None -> High Cut");
  checkEqual(EqModel::typeShortName(1) + "/" + EqModel::typeShortName(2) + "/" +
                 EqModel::typeShortName(3) + "/" + EqModel::typeShortName(4),
             "LCut/LShelf/Param/HShelf", "band type short names");
  checkEqual(EqModel::typeLongName(2), "Low Shelf", "band type long name from the effect");

  // Ranges clamp to the effect's own parameter limits.
  eq.setType(0, 3);
  eq.setFreqHz(0, 5.0);
  checkNear(eq.freqHz(0), 20.0, 1e-9, "frequency clamps at 20 Hz");
  eq.setFreqHz(0, 50000.0);
  checkNear(eq.freqHz(0), 20000.0, 1e-9, "frequency clamps at 20 kHz");
  eq.setGainDb(0, 99.0);
  checkNear(eq.gainDb(0), 24.0, 1e-9, "gain clamps at +24 dB");
  eq.setQ(0, 0.0);
  checkNear(eq.q(0), 0.1, 1e-9, "Q clamps at 0.1");

  // Touch drag / panel steps.
  eq.setFreqHz(0, 1000.0);
  eq.setGainDb(0, 0.0);
  check(eq.nodeAt(EqModel::freqToX(1000.0), 0.5) == 0, "the touch hit test finds a node");
  check(eq.nodeAt(0.0, 1.0) == -1, "an empty corner has no node");
  eq.dragBand(0, 0.25, 0.75);
  checkNear(eq.freqHz(0), EqModel::xToFreq(0.25), 1e-6, "drag maps x to log frequency");
  checkNear(eq.gainDb(0), EqModel::yToGain(0.75), 1e-6, "drag maps y to gain");
  const double before = eq.freqHz(0);
  eq.nudgeBand(0, 1, 0, false);
  checkNear(eq.freqHz(0), before * std::pow(2.0, 1.0 / 12.0), 1e-6,
            "one panel step is a semitone");
  eq.nudgeBand(0, 0, 1, false);
  checkNear(eq.gainDb(0), EqModel::yToGain(0.75) + 0.5, 1e-6,
            "one panel step is +0.5 dB");
  eq.setType(0, 1);
  const double cutQ = eq.q(0);
  eq.nudgeBand(0, 0, 1, false);
  check(eq.q(0) > cutQ, "a cut band's up/down adjusts Q");

  // Summed response: silence-free defaults and a real +12 dB parametric peak.
  EqModel flat;
  const auto& flatCurve = flat.curve();
  double flatMax = 0.0;
  for (const float db : flatCurve) flatMax = std::max(flatMax, std::abs(double(db)));
  check(static_cast<int>(flatCurve.size()) == EqModel::kCurvePoints,
        "the curve has one point per display column");
  check(flatMax < 0.01, "four None bands measure as a flat 0 dB response");

  EqModel peak;
  peak.setType(0, 3);
  peak.setFreqHz(0, 1000.0);
  peak.setGainDb(0, 12.0);
  peak.setQ(0, 1.0);
  const auto& peakCurve = peak.curve();
  int loudest = 0;
  for (int i = 1; i < static_cast<int>(peakCurve.size()); ++i) {
    if (peakCurve[static_cast<std::size_t>(i)] > peakCurve[static_cast<std::size_t>(loudest)]) {
      loudest = i;
    }
  }
  checkNear(EqModel::xToFreq(double(loudest) / (EqModel::kCurvePoints - 1)), 1000.0, 120.0,
            "the +12 dB parametric band peaks at its 1 kHz frequency");
  checkNear(peakCurve[static_cast<std::size_t>(loudest)], 12.0, 0.3,
            "the peak reads the band gain");

  // Subject and band selection wrap through Main / all four bands.
  EqModel select;
  select.setSubject(0);
  select.stepSubject(-1);
  check(select.subject() == EqModel::kMainSubject, "subject wrapping reaches Main");
  select.stepSubject(1);
  check(select.subject() == 0, "subject wrapping reaches track 1");
  checkEqual(EqModel::subjectName(EqModel::kMainSubject), "MAIN", "Main subject name");
  checkEqual(EqModel::subjectName(4), "IN 05", "track subject name");
  select.setBand(3);
  select.stepBand(1);
  check(select.band() == 0, "band stepping wraps");

  // Session round trip through the model's own capture/apply.
  EqModel source;
  source.setType(2, 3);
  source.setFreqHz(2, 640.0);
  source.setGainDb(2, -4.5);
  source.setQ(2, 2.25);
  source.setType(EqModel::kBandCount - 1, 1);
  EqModel restored;
  restored.apply(source.capture());
  checkNear(restored.freqHz(2), 640.0, 0.01, "EQ capture/apply keeps frequency");
  checkNear(restored.gainDb(2), -4.5, 0.01, "EQ capture/apply keeps gain");
  checkNear(restored.q(2), 2.25, 0.01, "EQ capture/apply keeps Q");
  check(restored.type(2) == 3 && restored.type(3) == 1, "EQ capture/apply keeps types");

  // X/Y performance: the frozen mapping, panel steps, freeze and persistence.
  PerformanceModel xy;
  using Model = PerformanceModel;
  const int dg = PerformanceModel::kDelayGrid;
  const int rg = PerformanceModel::kReverbGrid;
  checkNear(xy.param(dg, audio::fx::Delay::kBpCutoffHz), 20.0 + 0.5 * (20000.0 - 20.0),
            1e-6, "delay X starts at the middle of the cutoff range");
  xy.setPoint(Model::kDelayGrid, 0.0, 0.0);
  checkNear(xy.param(dg, audio::fx::Delay::kBpCutoffHz), 20.0, 1e-6,
            "delay X = 0 is the lowest cutoff");
  checkNear(xy.param(dg, audio::fx::Delay::kBpWidthOct), 0.1, 1e-6,
            "delay Y = 0 is the narrowest width");
  xy.setPoint(Model::kDelayGrid, 1.0, 1.0);
  checkNear(xy.param(dg, audio::fx::Delay::kBpCutoffHz), 20000.0, 1e-6,
            "delay X = 1 is the highest cutoff");
  checkNear(xy.param(dg, audio::fx::Delay::kBpWidthOct), 4.0, 1e-6,
            "delay Y = 1 is the widest width");
  xy.setPoint(Model::kReverbGrid, 0.0, 0.0);
  checkNear(xy.param(rg, audio::fx::Reverb::kSize), 0.25, 1e-6,
            "reverb X = 0 is the smallest size");
  checkNear(xy.param(rg, audio::fx::Reverb::kMix), 0.0, 1e-6,
            "reverb Y = 0 is the lowest level");
  xy.setPoint(Model::kReverbGrid, 1.0, 1.0);
  checkNear(xy.param(rg, audio::fx::Reverb::kSize), 2.0, 1e-6,
            "reverb X = 1 is the largest size");
  checkNear(xy.param(rg, audio::fx::Reverb::kMix), 1.0, 1e-6,
            "reverb Y = 1 is full level");
  check(xy.setPoint(Model::kDelayGrid, 5.0, -2.0), "an out-of-range point is clamped");
  checkNear(xy.x(Model::kDelayGrid), 1.0, 1e-9, "clamped x");
  checkNear(xy.y(Model::kDelayGrid), 0.0, 1e-9, "clamped y");
  xy.setPoint(Model::kDelayGrid, 0.5, 0.5);
  xy.nudge(Model::kDelayGrid, 1, 0, false);
  checkNear(xy.x(Model::kDelayGrid), 0.52, 1e-9, "a fine panel step is 0.02");
  xy.nudge(Model::kDelayGrid, 1, 0, true);
  checkNear(xy.x(Model::kDelayGrid), 0.62, 1e-9, "a SHIFT panel step is 0.10");

  xy.setPoint(Model::kReverbGrid, 0.4, 0.4);
  const double decay = xy.param(rg, audio::fx::Reverb::kDecay);
  const double damping = xy.param(rg, audio::fx::Reverb::kDamping);
  const double size = xy.param(rg, audio::fx::Reverb::kSize);
  const double mix = xy.param(rg, audio::fx::Reverb::kMix);
  xy.setFrozen(true);
  check(xy.frozen(), "freeze latches");
  checkNear(xy.param(rg, audio::fx::Reverb::kDecay), 0.98, 1e-9,
            "freeze sets the longest decay");
  checkNear(xy.param(rg, audio::fx::Reverb::kDamping), 0.0, 1e-9,
            "freeze removes the damping");
  checkNear(xy.param(rg, audio::fx::Reverb::kMix), 1.0, 1e-9, "freeze is fully wet");
  checkNear(xy.param(rg, audio::fx::Reverb::kSize), 2.0, 1e-9,
            "freeze uses the largest size");
  check(!xy.setPoint(Model::kReverbGrid, 0.1, 0.1), "the reverb grid is locked while frozen");
  check(xy.setPoint(Model::kDelayGrid, 0.3, 0.7), "the delay grid stays live while frozen");
  xy.setFrozen(false);
  check(!xy.frozen(), "freeze releases");
  checkNear(xy.param(rg, audio::fx::Reverb::kDecay), decay, 1e-9,
            "release restores the decay");
  checkNear(xy.param(rg, audio::fx::Reverb::kDamping), damping, 1e-9,
            "release restores the damping");
  checkNear(xy.param(rg, audio::fx::Reverb::kSize), size, 1e-9,
            "release restores the size");
  checkNear(xy.param(rg, audio::fx::Reverb::kMix), mix, 1e-9,
            "release restores the level");
  checkNear(xy.x(Model::kReverbGrid), 0.4, 1e-9, "release restores the reverb point");

  const auto positions = xy.capture();
  PerformanceModel restoredXy;
  restoredXy.apply(positions);
  checkNear(restoredXy.x(Model::kDelayGrid), positions.delayX, 1e-6,
            "X/Y capture/apply keeps the delay X position");
  checkNear(restoredXy.y(Model::kReverbGrid), positions.reverbY, 1e-6,
            "X/Y capture/apply keeps the reverb Y position");
  checkEqual(xy.gridName(Model::kDelayGrid) + "/" + xy.gridName(Model::kReverbGrid),
             "DELAY/REVERB", "grid names");
}

// The per-track X/Y grid and the rack popup: both bind the SELECTED track's
// FX1/FX2 slots, so a grid move and a popup knob are the same parameter write
// (and the slot's wet stays a separate control).
void trackFxRackTests() {
  MixerModel model;
  SessionSettings settings;
  FxManager fxHost;
  EqModel eq;
  MixerViewModel mixer(model, settings, eq);
  mixer.setClapHost(&fxHost);
  model.addTrack(0, "drums");
  model.addTrack(1, "bass");
  mixer.setSelectedStrip(0);

  XyViewModel xy(model, mixer);
  TrackFxViewModel fx(model, mixer);

  check(!xy.fx1()->bound() && !xy.fx2()->bound(),
        "an empty rack has no X/Y grid");
  check(xy.fx1()->note().contains("NONE"),
        "an empty slot says NONE and asks for DELAY or REVERB");

  // Track 0: FX1 delay, FX2 reverb.
  mixer.setFxChoiceAt(0, 0, 1);
  mixer.setFxChoiceAt(0, 1, 2);
  check(xy.fx1()->bound() && xy.fx2()->bound(),
        "a built-in slot binds its grid");
  check(xy.fx1()->axisX() == "X CUTOFF" && xy.fx1()->axisY() == "Y WIDTH",
        "the delay grid is cutoff / width");
  check(xy.fx2()->axisX() == "X SIZE" && xy.fx2()->axisY() == "Y LEVEL",
        "the reverb grid is size / level");

  xy.setPoint(0, 0.0, 0.0);
  checkNear(model.fxSlot(0, 0).params[audio::fx::Delay::kBpCutoffHz], 20.0, 1e-6,
            "delay X = 0 writes the lowest cutoff");
  checkNear(model.fxSlot(0, 0).params[audio::fx::Delay::kBpWidthOct], 0.1, 1e-6,
            "delay Y = 0 writes the narrowest width");
  xy.setPoint(0, 1.0, 1.0);
  checkNear(model.fxSlot(0, 0).params[audio::fx::Delay::kBpCutoffHz], 20000.0, 1e-6,
            "delay X = 1 writes the highest cutoff");
  checkNear(xy.fx1()->x(), 1.0, 1e-6, "the grid position reads back from the slot");
  xy.nudge(0, -1, 0, false);
  checkNear(xy.fx1()->x(), 0.98, 1e-6, "a fine panel step is 0.02 on the slot value");
  xy.nudge(0, -1, 0, true);
  checkNear(xy.fx1()->x(), 0.88, 1e-6, "a SHIFT panel step is 0.10 on the slot value");

  xy.setPoint(1, 0.0, 0.5);
  checkNear(model.fxSlot(0, 1).params[audio::fx::Reverb::kSize], 0.25, 1e-6,
            "reverb X = 0 writes the smallest size");
  checkNear(model.fxSlot(0, 1).params[audio::fx::Reverb::kMix], 0.5, 1e-6,
            "reverb Y writes the level");

  // Freeze is the reverb macro on the selected track's reverb slot.
  const double decayBefore = model.fxSlot(0, 1).params[audio::fx::Reverb::kDecay];
  xy.toggleFreeze();
  check(xy.frozen(), "freeze latches on a reverb slot");
  checkNear(model.fxSlot(0, 1).params[audio::fx::Reverb::kDecay], 0.98, 1e-9,
            "freeze sets the longest decay");
  checkNear(model.fxSlot(0, 1).params[audio::fx::Reverb::kSize], 2.0, 1e-9,
            "freeze uses the largest size");
  xy.toggleFreeze();
  check(!xy.frozen(), "freeze releases");
  checkNear(model.fxSlot(0, 1).params[audio::fx::Reverb::kDecay], decayBefore, 1e-9,
            "freeze release restores the decay");

  // Slot writes are not wet writes: the strip knob stays where it was.
  mixer.setFxWetAt(0, 0, 0.4);
  xy.setPoint(0, 0.5, 0.5);
  checkNear(model.fxSlot(0, 0).wet, 0.4, 1e-6,
            "a grid move leaves the slot wet alone");

  // Selecting another track rebinds both grids; a NONE slot is named, not
  // silently left showing the previous track's position.
  mixer.setSelectedStrip(1);
  check(!xy.fx1()->bound() && !xy.fx2()->bound(),
        "the grids follow the selected track");
  check(xy.fx1()->note().contains("NONE"), "the unbound slot names its state");
  if (!fxHost.plugins().empty()) {
    check(mixer.setFxChoiceAt(1, 1, MixerViewModel::kFxChoicePlugin),
          "the rack selector reaches a discovered plugin");
    check(model.fxSlot(1, 1).id == fxHost.plugins().front().id,
          "the plugin choice writes its CLAP id");
    check(mixer.setFxChoiceAt(1, 1, 0), "NONE clears the plugin slot again");
  }

  // TRACK FX popup: it follows the selected track and edits the same rack.
  fx.openFor(1, 1);
  check(fx.open() && fx.track() == 1 && fx.focusSlot() == 1,
        "TRACK FX opens on the requested track and slot");
  check(fx.fx1()->title() == "FX1" && fx.fx2()->title() == "FX2",
        "the two cards are FX1 and FX2");
  check(fx.fx1()->choice() == mixer.fxChoiceOf(""),
        "the closed selector reads the slot it shows");
  fx.setChoice(0, 1);
  check(model.fxSlot(1, 0).id == "builtin.delay",
        "the popup selector writes that track's FX1");
  fx.setWet(0, 0.3);
  checkNear(model.fxSlot(1, 0).wet, 0.3, 1e-6, "the popup wet writes the slot");
  fx.toggleBypass(0);
  check(model.fxSlot(1, 0).bypass, "the popup bypass writes the slot");
  fx.setParam(0, audio::fx::Delay::kFeedback, 0.75);
  checkNear(model.fxSlot(1, 0).params[audio::fx::Delay::kFeedback], 0.75, 1e-9,
            "a popup parameter card writes the effect parameter");
  check(fx.fx1()->paramCount() ==
            static_cast<int>(fxParamTable("builtin.delay").size()),
        "the card count follows the slot's effect");
  fx.close();
  check(!fx.open(), "CLOSE closes the TRACK FX popup");

  // Reverb model is a preset in the effect contract: the rack expands it.
  MixerModel presetModel;
  presetModel.setFxSlot(0, 0, MixerModel::FxSlot{"builtin.reverb", false, 1.0f,
                                                 fxDefaultParams("builtin.reverb")});
  presetModel.setFxParam(0, 0, audio::fx::Reverb::kModel,
                         static_cast<double>(audio::fx::Reverb::kModelHall));
  checkNear(presetModel.fxSlot(0, 0).params[audio::fx::Reverb::kDecay],
            audio::fx::Reverb::modelPreset(audio::fx::Reverb::kModelHall).decay, 1e-9,
            "a reverb model writes its preset decay");
  checkNear(presetModel.fxSlot(0, 0).params[audio::fx::Reverb::kSize],
            audio::fx::Reverb::modelPreset(audio::fx::Reverb::kModelHall).size, 1e-9,
            "a reverb model writes its preset size");
}

// Per-track CLAP hosting: the first plugin selection creates the host instance,
// the plugin's own clap.params table drives the cards, NONE destroys the
// instance, and the measured 16-instance cap refuses a 17th selection with the
// exact UI message.
void clapTrackHostTests() {
  AppState state;
  SessionSettings settings;
  MixerModel mixer;
  FxManager fx;
  EqModel eq;
  PerformanceModel performance;
  AudioGlue audio(mixer, fx, performance, state, true);
  MixerViewModel view(mixer, settings, eq);
  view.setClapHost(&fx);

  const auto& plugins = fx.plugins();
  if (plugins.empty()) {
    check(!view.setFxChoiceAt(0, 0, MixerViewModel::kFxChoicePlugin),
          "no discovered plugin refuses the CLAP choice");
    return;
  }
  const auto plugin = plugins.front();
  check(fx.trackInstanceCount() == 0,
        "empty and built-in slots allocate no CLAP instance");
  check(view.setFxChoiceAt(0, 1, MixerViewModel::kFxChoicePlugin),
        "the first discovered plugin is selectable");
  check(mixer.fxSlot(0, 1).id == plugin.id,
        "the slot stores the plugin's CLAP id (session-fied)");
  check(fx.trackEffectLoaded(0, 1) && fx.trackInstanceCount() == 1,
        "selecting the plugin creates exactly one host instance");
  check(fx.trackEffectName(0, 1) == plugin.name,
        "the host reports the plugin's own name");
  const auto cards = view.fxParams(0, 1);
  check(cards.size() == static_cast<int>(plugin.params.size()),
        "the card list is the plugin's clap.params table");
  if (!cards.isEmpty() && !plugin.params.empty()) {
    check(cards.first().toMap().value(QStringLiteral("name")).toString() ==
              QString::fromStdString(plugin.params.front().name).toUpper(),
          "a card carries the plugin's parameter name");
    const auto& first = plugin.params.front();
    const double requested = std::clamp(3.5, first.min, first.max);
    view.setFxParamAt(0, 1, 0, requested);
    checkNear(mixer.fxSlot(0, 1).params[0], requested, 1e-9,
              "a plugin card writes the session parameter vector");
    check(std::abs(fx.trackParamValue(0, 1, 0) - requested) < 1e-9,
          "the parameter write reaches the plugin's own clap.params API");
  }

  // Fill the host to the measured admission cap: track 0's second slot plus
  // one slot on tracks 1..15 is 16 live instances.
  int loaded = fx.trackInstanceCount();
  for (int track = 1; track < FxManager::kMaxTrackInstances && loaded <
                                                              FxManager::kMaxTrackInstances;
       ++track) {
    if (!view.setFxChoiceAt(track, 0, MixerViewModel::kFxChoicePlugin)) break;
    loaded = fx.trackInstanceCount();
  }
  check(fx.trackInstanceCount() == FxManager::kMaxTrackInstances,
        "the host reaches the 16 simultaneous instance cap");
  const int refusedTrack = 16;
  mixer.setFxSlot(refusedTrack, 0, {});
  check(!view.setFxChoiceAt(refusedTrack, 0, MixerViewModel::kFxChoicePlugin),
        "a 17th selection at the cap is refused");
  check(view.fxChoiceRefusal(refusedTrack, 0) ==
            "PLUGIN LIMIT REACHED - unload one first",
        "the refusal carries the exact owner-facing message");
  check(mixer.fxSlot(refusedTrack, 0).id.empty(),
        "a refused selection leaves the slot untouched");
  ConfigPopupModel popup(state, mixer, settings, eq);
  popup.setMixer(&view);
  popup.setFxTarget(refusedTrack, 0);
  check(popup.openKind(QStringLiteral("fxslot")), "the FX slot popup opens at the cap");
  popup.setFxSlotChoice(MixerViewModel::kFxChoicePlugin);
  check(popup.fxSlotNote() == "PLUGIN LIMIT REACHED - unload one first",
        "the popup shows the exact cap message");
  check(popup.fxSlotChoice() == 0,
        "the popup selector stays on the slot's actual value");
  popup.cancel();

  check(view.setFxChoiceAt(0, 1, 0), "NONE unloads the plugin slot");
  check(!fx.trackEffectLoaded(0, 1) && fx.trackInstanceCount() == 15,
        "clearing a slot destroys its instance");
  check(view.setFxChoiceAt(refusedTrack, 0, MixerViewModel::kFxChoicePlugin),
        "the freed slot admits the next selection");
  check(fx.trackInstanceCount() == FxManager::kMaxTrackInstances,
        "the host is at the cap again");

  // Teardown path: every remaining track slot is cleared through the same
  // UI path, and no plugin instance stays loaded.
  for (int track = 0; track < audio::kMaxTracks; ++track) {
    for (int slot = 0; slot < MixerModel::kFxSlotCount; ++slot) {
      if (mixer.fxSlot(track, slot).id.empty()) continue;
      (void)view.setFxChoiceAt(track, slot, 0);
    }
  }
  check(fx.trackInstanceCount() == 0, "every CLAP instance is torn down");
}

// CLAP audio path: a selected plugin is not merely an instance count. The
// engine's external branch must actually process the strip, and the slot wet
// must still crossfade around it. The build's reference plugin (mnkygain) is
// the probe: gain -24 dB must attenuate a steady input; a dry slot must not.
void clapSlotAudioTests() {
  FxManager fx;
  const auto& plugins = fx.plugins();
  if (plugins.empty()) return;
  const auto plugin = plugins.front();
  if (plugin.id != "com.cldmnky.mnkygain") return;  // needs the reference gain DSP

  check(fx.loadTrackEffect(0, 0, plugin.id, 48000.0, audio::kBlockSize),
        "the discovered plugin instantiates for a track slot");
  check(fx.trackEffect(0, 0) != nullptr, "the loaded slot publishes its effect seam");

  audio::Engine engine;
  audio::Channel channel;
  channel.gain = 1.0f;
  channel.vol = 1.0f;
  channel.pan = 0.0f;  // hard left: Main L is the strip at unity
  channel.monitor = audio::MonitorMode::On;
  engine.setChannel(0, channel);

  audio::TrackFxSlot slot;
  slot.id = plugin.id;
  slot.wet = 1.0f;
  for (const auto& param : plugin.params) slot.params.push_back(param.value);
  engine.setTrackFxSlot(0, 0, slot);
  engine.setTrackFxSlotEffect(0, 0, fx.trackEffect(0, 0));
  // mnkygain's first parameter is Gain in dB; -24 dB is ~0.063x.
  check(fx.setTrackParam(0, 0, 0, -24.0),
        "a CLAP parameter write reaches the live instance");

  std::vector<float> input(static_cast<std::size_t>(audio::kBlockSize), 0.25f);
  std::vector<float> outL(static_cast<std::size_t>(audio::kBlockSize), 0.0f);
  std::vector<float> outR(static_cast<std::size_t>(audio::kBlockSize), 0.0f);
  const float* inputs[1] = {input.data()};
  const auto render = [&]() {
    for (int block = 0; block < 4; ++block) {
      engine.process(inputs, 1, audio::kBlockSize, outL.data(), outR.data());
    }
    return outL[audio::kBlockSize - 1];
  };

  const float wet = render();
  check(wet > 0.001f && wet < 0.05f,
        "a wet CLAP slot processes the strip (gain -24 dB is audible)");

  audio::TrackFxSlot dry = slot;
  dry.wet = 0.0f;
  engine.setTrackFxSlot(0, 0, dry);
  checkNear(render(), 0.25, 1e-6,
            "wet 0 leaves the CLAP slot bit-transparent (dry path)");
}

// A two-slot series probe with deterministic effects. FX1 runs first, then
// FX2, and each slot's wet crossfade is `dry + wet * (processed - dry)`, so
// the measured output is a direct proof of the documented mix math.
void trackFxSeriesMathTests() {
  struct AddOne final : audio::IEffect {
    void process(float* const* ch, int nch, int n) override {
      for (int channel = 0; channel < nch; ++channel) {
        if (ch[channel] == nullptr) continue;
        for (int i = 0; i < n; ++i) ch[channel][i] += 1.0f;
      }
    }
    const char* name() const override { return "probe-add"; }
  };
  struct TimesTwo final : audio::IEffect {
    void process(float* const* ch, int nch, int n) override {
      for (int channel = 0; channel < nch; ++channel) {
        if (ch[channel] == nullptr) continue;
        for (int i = 0; i < n; ++i) ch[channel][i] *= 2.0f;
      }
    }
    const char* name() const override { return "probe-mul"; }
  };

  AddOne add;
  TimesTwo times;
  audio::Engine engine;
  audio::Channel channel;
  channel.gain = 1.0f;
  channel.vol = 1.0f;
  channel.pan = 0.0f;
  channel.monitor = audio::MonitorMode::On;
  engine.setChannel(0, channel);

  audio::TrackFxSlot first;
  first.id = "test.add";
  first.wet = 1.0f;
  audio::TrackFxSlot second;
  second.id = "test.times";
  second.wet = 1.0f;
  engine.setTrackFxSlot(0, 0, first);
  engine.setTrackFxSlotEffect(0, 0, &add);
  engine.setTrackFxSlot(0, 1, second);
  engine.setTrackFxSlotEffect(0, 1, &times);

  std::vector<float> input(static_cast<std::size_t>(audio::kBlockSize), 0.1f);
  std::vector<float> outL(static_cast<std::size_t>(audio::kBlockSize), 0.0f);
  std::vector<float> outR(static_cast<std::size_t>(audio::kBlockSize), 0.0f);
  const float* inputs[1] = {input.data()};
  const auto render = [&]() {
    engine.process(inputs, 1, audio::kBlockSize, outL.data(), outR.data());
    return outL[audio::kBlockSize - 1];
  };

  checkNear(render(), 2.2, 1e-5,
            "FX1 (add) then FX2 (multiply) both process at wet 1");

  // FX1 at 50%: dry + 0.5 * (processed - dry) = 0.1 + 0.5 * 1.0 = 0.6,
  // then FX2 wet 1 doubles it.
  audio::TrackFxSlot half = first;
  half.wet = 0.5f;
  engine.setTrackFxSlot(0, 0, half);
  checkNear(render(), 1.2, 1e-5,
            "a slot wet 0.5 crossfades before the next slot processes");

  // FX1 bypassed: only FX2 runs -> 0.2. FX2 bypassed: only the half-wet FX1
  // output survives -> 0.6.
  audio::TrackFxSlot bypassFirst = half;
  bypassFirst.bypass = true;
  engine.setTrackFxSlot(0, 0, bypassFirst);
  checkNear(render(), 0.2, 1e-5, "bypassing FX1 leaves FX2 in the chain");

  engine.setTrackFxSlot(0, 0, half);
  audio::TrackFxSlot bypassSecond = second;
  bypassSecond.bypass = true;
  engine.setTrackFxSlot(0, 1, bypassSecond);
  checkNear(render(), 0.6, 1e-5, "bypassing FX2 leaves FX1's wet output");
}

// The Pi may discover zero CLAP modules (the honest "no CLAP plugins found"
// state). The rack's builtin slots are engine-owned, so DELAY and REVERB
// must stay fully functional there: a wet-1 slot changes the strip, wet 0 is
// bit-transparent, and FX1 -> FX2 runs both slots in series.
void builtinTrackFxRackAudioTests() {
  audio::Engine engine;
  audio::Channel channel;
  channel.gain = 1.0f;
  channel.vol = 1.0f;
  channel.pan = 0.0f;
  channel.monitor = audio::MonitorMode::On;
  engine.setChannel(0, channel);

  std::vector<float> input(static_cast<std::size_t>(audio::kBlockSize), 0.25f);
  std::vector<float> outL(static_cast<std::size_t>(audio::kBlockSize), 0.0f);
  std::vector<float> outR(static_cast<std::size_t>(audio::kBlockSize), 0.0f);
  const float* inputs[1] = {input.data()};
  // 24 blocks = 64 ms at the engine's 48 kHz start rate: short of the delay's
  // 250 ms default time, so the measured sample is the slot's own mix stage and
  // not a note-feedback artefact.
  const auto render = [&]() {
    for (int block = 0; block < 24; ++block) {
      engine.process(inputs, 1, audio::kBlockSize, outL.data(), outR.data());
    }
    return outL[audio::kBlockSize - 1];
  };

  checkNear(render(), 0.25, 1e-6, "an empty rack passes the strip unchanged");

  audio::TrackFxSlot delay;
  delay.id = kFxIdDelay;
  delay.wet = 1.0f;
  delay.params = fxDefaultParams(kFxIdDelay);
  engine.setTrackFxSlot(0, 0, delay);
  const float delayOnly = render();
  check(std::isfinite(delayOnly) && std::abs(delayOnly - 0.25) > 1e-3,
        "a wet-1 DELAY slot processes the strip without any CLAP module");

  audio::TrackFxSlot quiet = delay;
  quiet.wet = 0.0f;
  engine.setTrackFxSlot(0, 0, quiet);
  checkNear(render(), 0.25, 1e-6, "wet 0 leaves the DELAY slot bit-transparent");

  audio::TrackFxSlot reverb;
  reverb.id = kFxIdReverb;
  reverb.wet = 1.0f;
  reverb.params = fxDefaultParams(kFxIdReverb);
  engine.setTrackFxSlot(0, 0, delay);
  engine.setTrackFxSlot(0, 1, reverb);
  const float series = render();
  check(std::isfinite(series) && std::abs(series - delayOnly) > 1e-3,
        "FX2's REVERB also processes FX1's DELAY output (series rack)");
}

// A device with no discoverable CLAP module is a state, not a broken popup: the
// slot list must name it instead of showing an empty plugin section, and the
// built-in choices must stay selectable. This is the studio Pi's state when
// its plugin search finds nothing.
void fxSlotZeroClapStateTests() {
  AppState state;
  SessionSettings settings;
  MixerModel mixer;
  EqModel eq;
  MixerViewModel view(mixer, settings, eq);  // no CLAP host at all
  check(mixer.addTrack(0, "honest"), "the zero-CLAP probe track exists");
  check(view.fxChoiceLabels() ==
            QStringList{QStringLiteral("NONE"), QStringLiteral("DELAY"),
                        QStringLiteral("REVERB")},
        "zero discovered CLAPs leaves exactly the built-in choices");

  ConfigPopupModel popup(state, mixer, settings, eq);
  popup.setMixer(&view);
  popup.setFxTarget(0, 0);
  check(popup.openKind(QStringLiteral("fxslot")),
        "the FX-slot popup opens without a plugin host");
  check(popup.fxSlotNote().contains("no CLAP plugins found"),
        "the FX-slot popup names the zero-CLAP state");
  check(view.setFxChoiceAt(0, 0, 1) && mixer.fxSlot(0, 0).id == kFxIdDelay,
        "DELAY stays selectable on a zero-CLAP device");
  check(view.setFxChoiceAt(0, 0, 2) && mixer.fxSlot(0, 0).id == kFxIdReverb,
        "REVERB stays selectable on a zero-CLAP device");
  popup.cancel();
}

// Slot kind switching through the same UI path the FX popup uses: NONE /
// DELAY / REVERB never allocate a CLAP host, a CLAP pick creates exactly one
// instance, and each switch destroys the old instance before the new one is
// selected. The instantiate-failure path must publish its reason (the popup
// note) instead of leaving a silently dry slot.
void trackFxKindSwitchTests() {
  AppState state;
  SessionSettings settings;
  MixerModel mixer;
  FxManager fx;
  EqModel eq;
  PerformanceModel performance;
  AudioGlue audio(mixer, fx, performance, state, true);
  MixerViewModel view(mixer, settings, eq);
  view.setClapHost(&fx);
  check(mixer.addTrack(0, "probe"), "the kind-switch probe track exists");

  check(mixer.fxSlot(0, 0).id.empty(), "a new slot starts as NONE");
  check(view.setFxChoiceAt(0, 0, 1), "NONE -> DELAY is accepted");
  check(mixer.fxSlot(0, 0).id == "builtin.delay", "DELAY stores the built-in id");
  check(mixer.fxSlot(0, 0).params.size() == fxParamTable("builtin.delay").size(),
        "DELAY starts from its published parameter defaults");
  check(fx.trackInstanceCount() == 0,
        "a built-in slot never allocates a CLAP host instance");

  check(view.setFxChoiceAt(0, 0, 2), "DELAY -> REVERB is accepted");
  check(mixer.fxSlot(0, 0).id == "builtin.reverb", "REVERB replaces DELAY");
  check(mixer.fxSlot(0, 0).params.size() == fxParamTable("builtin.reverb").size(),
        "REVERB starts from its own defaults, not DELAY's vector");
  check(view.setFxChoiceAt(0, 0, 0), "REVERB -> NONE is accepted");
  check(mixer.fxSlot(0, 0).id.empty(), "NONE clears the built-in id");

  const auto& plugins = fx.plugins();
  if (!plugins.empty()) {
    const auto plugin = plugins.front();
    check(view.setFxChoiceAt(0, 0, MixerViewModel::kFxChoicePlugin),
          "NONE -> CLAP is accepted");
    check(mixer.fxSlot(0, 0).id == plugin.id, "the CLAP id is stored");
    check(fx.trackEffectLoaded(0, 0) && fx.trackInstanceCount() == 1,
          "the CLAP pick creates one live instance");

    check(view.setFxChoiceAt(0, 0, 1), "CLAP -> DELAY is accepted");
    check(mixer.fxSlot(0, 0).id == "builtin.delay", "DELAY replaces the CLAP id");
    check(!fx.trackEffectLoaded(0, 0) && fx.trackInstanceCount() == 0,
          "switching away from CLAP destroys its instance");

    check(view.setFxChoiceAt(0, 0, MixerViewModel::kFxChoicePlugin),
          "DELAY -> CLAP is accepted");
    check(fx.trackEffectLoaded(0, 0) && fx.trackInstanceCount() == 1,
          "the second CLAP pick recreates one instance");
    check(!view.setFxChoiceAt(0, 0, 3 + static_cast<int>(plugins.size())),
          "an out-of-range plugin choice is refused");

    // The selection is accepted, but the instance cannot be created: the
    // engine must fall back dry AND the popup must name the failure.
    check(!fx.loadTrackEffect(0, 0, "test.missing", 48000.0, audio::kBlockSize),
          "an unknown plugin id fails to instantiate");
    check(!fx.trackEffectLoaded(0, 0), "a failed load leaves no live instance");
    check(!fx.trackEffectError(0, 0).empty(),
          "a failed load publishes its exact reason");
    MixerModel::FxSlot failed;
    failed.id = "test.missing";
    failed.wet = 1.0f;
    mixer.setFxSlot(0, 0, failed);
    check(view.fxSlotNote(0, 0).contains("NOT LOADED"),
          "the TRACK FX card note names the failed load");
    ConfigPopupModel popup(state, mixer, settings, eq);
    popup.setMixer(&view);
    popup.setFxTarget(0, 0);
    check(popup.openKind(QStringLiteral("fxslot")), "the FX-slot popup opens");
    check(popup.fxSlotNote().contains("NOT LOADED"),
          "the FX-slot popup shows the load failure while it is visible");
    popup.cancel();
    mixer.setFxSlot(0, 0, {});
  }
}

// Output inserts: selecting USB OUT exposes an ordered chain list; add/remove/
// reorder and the chain's pipes round-trip through the session router, and a
// TRS output reports that inserts are Pi-owned only.
void outputInsertChainTests() {
  MidiGlue midi(false);
  RoutingViewModel routing(midi, false);
  int usbOut = -1;
  int trsOut = -1;
  for (const auto& row : routing.outputNodes()) {
    const auto map = row.toMap();
    if (map.value(QStringLiteral("port")).toInt() ==
        midi::portId(midi::Port::UsbOut)) {
      usbOut = map.value(QStringLiteral("nodeIndex")).toInt();
    }
    if (map.value(QStringLiteral("port")).toInt() ==
        midi::portId(midi::Port::TrsOut1)) {
      trsOut = map.value(QStringLiteral("nodeIndex")).toInt();
    }
  }
  check(usbOut >= 0 && trsOut >= 0, "the routing model lists USB and TRS outputs");
  routing.setPipeDestination(usbOut);
  check(routing.outputInsertsAvailable(),
        "a USB output exposes output inserts");
  routing.addInsertChain();
  check(routing.insertSlot() == 0 && routing.insertRows().size() == 1,
        "ADD appends the first output insert chain");
  routing.setAddTypeIndex(0);  // FILTER
  routing.addPipe();
  check(routing.pipeCount() == 1, "the insert chain takes ordinary modifiers");
  const QStringList insertTypes = routing.addTypeLabels();
  check(!insertTypes.contains(QStringLiteral("CC LFO")),
        "a generator-role head is not offered as an insert modifier");
  check(!midi.addOutputInsertPipe(midi::Port::UsbOut, 0,
                                  session::MidiPipeType::CcLfo),
        "the glue refuses a generator head in an output insert chain");
  check(routing.pipeStatusText().contains(QStringLiteral("PI ENGINE")),
        "the insert chain reports the Pi engine");
  const auto routerSnapshot = midi.routerSnapshot();
  check(routerSnapshot.outputInsertEnabled(midi::Port::UsbOut, 0) &&
            routerSnapshot.outputInsertPipeCount(midi::Port::UsbOut, 0) == 1,
        "the glue applies the insert chain to the live Router slot");
  routing.addInsertChain();
  routing.setAddTypeIndex(11);  // ARP
  routing.addPipe();
  check(midi.outputInsertChains(midi::Port::UsbOut).size() == 2,
        "a second insert chain is stored in order");
  check(midi.outputInsertPipes(midi::Port::UsbOut, 1).size() == 1,
        "the second chain holds its own pipe");
  routing.moveInsertChain(1, -1);
  check(midi.outputInsertPipes(midi::Port::UsbOut, 0).size() == 1 &&
            static_cast<int>(midi.outputInsertPipes(midi::Port::UsbOut, 0)
                                 .front()
                                 .type) ==
                static_cast<int>(session::MidiPipeType::Arp),
        "moving a chain swaps the ordered slots");
  routing.removeInsertChain(1);
  check(midi.outputInsertChains(midi::Port::UsbOut).size() == 1,
        "removing a chain compacts the list");
  routing.toggleInsertChain(0);
  check(!midi.outputInsertEnabled(midi::Port::UsbOut, 0),
        "the insert chain has its own ON gate");

  // Session round-trip keeps the ordered chains, their gates and pipes.
  const auto captured = midi.captureSessionRouter();
  int insertRoutes = 0;
  for (const auto& route : captured.routes) {
    if (route.src == session::kSrcOutputInsert &&
        route.dst == midi::portId(midi::Port::UsbOut)) {
      ++insertRoutes;
      check(!route.pipes.empty(), "capture keeps the insert chain's pipes");
    }
  }
  check(insertRoutes == 1, "capture writes the output insert pseudo route");
  MidiGlue restored(false);
  restored.applySessionRouter(captured);
  const auto restoredChains = restored.outputInsertChains(midi::Port::UsbOut);
  check(restoredChains.size() == 1 && restoredChains.front().pipes.size() == 1 &&
            !restoredChains.front().enabled,
        "project load restores the ordered insert chain and its gate");

  // The ordered list is capped at the Router's four slots.
  while (routing.insertRows().size() <
         static_cast<int>(midi::Router::kMaxOutputInserts)) {
    routing.addInsertChain();
  }
  check(routing.insertRows().size() ==
            static_cast<int>(midi::Router::kMaxOutputInserts),
        "an output holds at most four insert chains");
  routing.addInsertChain();
  check(routing.insertHint() == "OUTPUT INSERT LIMIT (4) REACHED",
        "a fifth insert chain is refused with a named limit");
  routing.setPipeDestination(trsOut);
  check(!routing.outputInsertsAvailable(),
        "a TRS output is Pico-owned, so it exposes no insert chain");
  routing.addInsertChain();
  check(routing.insertHint().contains(QStringLiteral("USB-ONLY")),
        "ADD on a TRS output names the actual engine boundary");
  routing.reset();
  check(midi.outputInsertChains(midi::Port::UsbOut).empty(),
        "matrix RESET clears output inserts");
}

// The send chain behind the X/Y grids: the built-in delay/reverb process the
// FX1/FX2 send bus ahead of the CLAP slot, and the grid positions reach the DSP.
class HalvingEffect final : public audio::IEffect {
 public:
  void process(float* const* ch, int nch, int n) override {
    ++calls;
    for (int channel = 0; channel < nch; ++channel) {
      for (int i = 0; i < n; ++i) ch[channel][i] *= 0.5f;
    }
  }
  const char* name() const override { return "halving-test-slot"; }
  int calls = 0;
};

struct SendChainResult {
  double tail = 0.0;
  int peakFrame = -1;
  float peak = 0.0f;
  int blocks = 0;
};

// One impulse through the send chain, then 140 blocks of silence: the tail is
// the energy after the first 100 frames, the peak frame shows where the echo or
// the reverb build-up lands.
SendChainResult runSendChain(PerformanceModel& model, int grid,
                             audio::IEffect* clap = nullptr) {
  if (clap != nullptr) model.setClapEffect(grid, clap);
  model.setSampleRate(48000.0);
  audio::IEffect& effect = model.effect(grid);
  std::vector<float> left(audio::kBlockSize, 0.0f);
  std::vector<float> right(audio::kBlockSize, 0.0f);
  left[0] = 1.0f;
  right[0] = 1.0f;
  SendChainResult result;
  int frame = 0;
  for (int block = 0; block < 140; ++block) {
    float* channels[2] = {left.data(), right.data()};
    effect.process(channels, 2, audio::kBlockSize);
    ++result.blocks;
    for (int i = 0; i < audio::kBlockSize; ++i, ++frame) {
      const float value = std::abs(left[static_cast<std::size_t>(i)]);
      if (value > result.peak) {
        result.peak = value;
        result.peakFrame = frame;
      }
      if (frame > 100) {
        result.tail += double(left[static_cast<std::size_t>(i)]) *
                       left[static_cast<std::size_t>(i)];
      }
    }
    std::fill(left.begin(), left.end(), 0.0f);
    std::fill(right.begin(), right.end(), 0.0f);
  }
  return result;
}

void sendChainTests() {
  const int delayGrid = PerformanceModel::kDelayGrid;
  const int reverbGrid = PerformanceModel::kReverbGrid;

  PerformanceModel delaySend;
  delaySend.setPoint(delayGrid, 0.5, 0.5);
  HalvingEffect clap;
  const auto delay = runSendChain(delaySend, delayGrid, &clap);
  check(delay.peakFrame > 11800 && delay.peakFrame < 12200,
        "the built-in delay repeats the FX1 send impulse at its 250 ms time");
  check(delay.peak > 0.0f, "the built-in delay produces audio on the send bus");
  check(clap.calls > 0, "the chain forwards the send bus to the CLAP slot");
  check(clap.calls == delay.blocks, "the chain reaches the CLAP slot every block");

  PerformanceModel dryReverb;
  dryReverb.setPoint(reverbGrid, 0.5, 0.0);
  const auto dry = runSendChain(dryReverb, reverbGrid);
  check(dry.tail < 1e-9, "reverb level 0 leaves the send bus dry");
  PerformanceModel wetReverb;
  wetReverb.setPoint(reverbGrid, 0.5, 1.0);
  const auto wet = runSendChain(wetReverb, reverbGrid);
  check(wet.tail > 1e-3, "reverb level 1 adds the wet tail");

  PerformanceModel smallReverb;
  smallReverb.setPoint(reverbGrid, 0.02, 1.0);
  const auto small = runSendChain(smallReverb, reverbGrid);
  PerformanceModel largeReverb;
  largeReverb.setPoint(reverbGrid, 0.98, 1.0);
  const auto large = runSendChain(largeReverb, reverbGrid);
  check(small.peakFrame > 0 && large.peakFrame > small.peakFrame + 1000,
        "reverb size (grid X) moves the tail later in the DSP");
}

// Take-loop beat field, I/O assignment and clock-gate value rules: the pure
// mapping the Recorder/Routing/Mixer SHIFT faces and the session sections share.
void loopAndIoTests() {
  check(loopBeats(-1.0) == 0.0 && loopBeats(0.0) == 0.0, "loop beats floor at 0");
  checkNear(loopBeats(0.1), 0.0, 1e-9, "loop beats snap down to the 0.25 grid");
  checkNear(loopBeats(1.24), 1.25, 1e-9, "loop beats snap to the nearest 0.25");
  checkNear(loopBeats(1.1), 1.0, 1e-9, "loop beats snap to the nearest step");
  check(loopBeats(1.0e9) == kLoopMaxBeats, "loop beats clamp at kLoopMaxBeats");
  check(loopBeats(std::nan("")) == 0.0, "a NaN loop field reads as 0");

  check(clampInCh(-5) == -1 && clampInCh(0) == 0 && clampInCh(17) == 17 &&
            clampInCh(99) == audio::kMaxDeviceInputs - 1,
        "input source clamps to none or a real channel");
  check(evenInCh(4) == 4 && evenInCh(5) == 4 && evenInCh(-1) == -1,
        "a stereo pair keeps the first channel even");
  check(clampOutDst(-1) == 0 && clampOutDst(1) == 1 && clampOutDst(9) == 3,
        "output destination clamps to Main/OUT3&4/phones/none");

  MixerModel mixer;
  mixer.setInCh(0, 5);
  check(mixer.inCh(0) == 5 && !mixer.inPair(0), "a mono input keeps its channel");
  mixer.setInPair(0, true);
  check(mixer.inCh(0) == 4 && mixer.inPair(0),
        "enabling a pair clamps an odd channel down to even");
  mixer.setInCh(0, 7);
  check(mixer.inCh(0) == 6, "an odd channel cannot be selected while pairing");
  mixer.setInPair(0, false);
  mixer.setInCh(0, 7);
  check(mixer.inCh(0) == 7, "odd channels are selectable again in mono");
  mixer.setInCh(0, -1);
  check(mixer.inCh(0) == -1, "input source NONE is stored as -1");
  mixer.setInPair(0, true);
  check(mixer.inCh(0) == 0, "a pair cannot be NONE; it falls back to channel 1");
  mixer.setOutDst(0, 2);
  check(mixer.outDst(0) == 2, "output destination stores phones");
  mixer.setOutDst(0, 9);
  check(mixer.outDst(0) == 3, "output destination clamps to NONE");

  check(MixerViewModel::inputText(-1, false) == "NONE", "input text for NONE");
  check(MixerViewModel::inputText(2, false) == "IN 03", "input text for one channel");
  check(MixerViewModel::inputText(4, true) == "IN 05/06", "input text for a stereo pair");
  check(MixerViewModel::outputText(0) == "MAIN 1&2" && MixerViewModel::outputText(1) == "OUT3&4" &&
            MixerViewModel::outputText(2) == "PHONES" && MixerViewModel::outputText(3) == "NONE",
        "output destination labels");
}

// MIDI monitor: the ring records every message the Pi handles, classifies the
// counters and renders the log line; the screen is a pure view of that.
void midiMonitorTests() {
  std::uint8_t noteOn[3] = {0x90, 60, 100};
  std::uint8_t cc[3] = {0xB3, 74, 64};
  std::uint8_t clock[1] = {midi::kClockByte};
  check(midiMessageText(3, noteOn) == "N ON 01 3C v100", "monitor renders a note on");
  std::uint8_t noteOff[3] = {0x90, 60, 0};
  check(midiMessageText(3, noteOff) == "N OFF 01 3C", "a zero-velocity note on is an off");
  check(midiMessageText(3, cc) == "CC 04 74 64", "monitor renders a control change");
  check(midiMessageText(1, clock) == "CLOCK", "monitor renders a clock byte");
  std::uint8_t start[1] = {midi::kStartByte};
  check(midiMessageText(1, start) == "START", "monitor renders a transport byte");
  check(midiMessageText(0, nullptr) == "-", "an empty message renders as a dash");
  check(midiPortName(0) == "TRS IN1" && midiPortName(5) == "USB OUT",
        "monitor port names match the router port order");
  check(midiAgeText(500) == "500 us" && midiAgeText(2500) == "2.5 ms",
        "monitor age text switches units");

  MidiGlue midi(false);
  midi::MidiEvent event;
  event.source = midi::Port::TrsIn1;
  event.size = 3;
  for (int i = 0; i < 3; ++i) event.data[i] = noteOn[i];
  midi.handleInput(event);
  event.data[0] = 0xB0;
  midi.handleInput(event);
  event.source = midi::Port::TrsIn2;
  event.size = 1;
  event.data[0] = midi::kClockByte;
  midi.handleInput(event);

  auto snapshot = midi.monitorSnapshot();
  check(snapshot.total == 3 && snapshot.count == 3, "the monitor ring records every event");
  check(snapshot.counts[0].notes == 1 && snapshot.counts[0].cc == 1,
        "note and CC counters are per port");
  check(snapshot.counts[1].clock == 1 && snapshot.counts[0].clock == 0,
        "clock bytes are counted on their own port");
  check(snapshot.events[0].size == 3 && snapshot.events[2].size == 1,
        "the ring keeps arrival order");

  midi.clearMonitor();
  snapshot = midi.monitorSnapshot();
  check(snapshot.total == 0 && snapshot.count == 0 && snapshot.counts[0].notes == 0,
        "clearing the monitor drops the ring and the counters");
}

// Clock gates: the SessionViewModel writes the session clock section and asks
// the link for the matching wire action. LinkClient queues the command; the
// wire behaviour itself is covered by link/midi CTest.
void clockGateTests() {
  AppState state;
  SessionSettings settings;
  MixerModel mixer;
  MidiGlue midi(false);
  LinkClient link(midi, state, true);
  SessionViewModel session(state, mixer, link, settings);

  check(session.trs1Clock() && session.trs1Transport() && session.usbClock() &&
            session.usbTransport(),
        "clock gates default on, as the Pico boot state");
  session.setTrs1Clock(false);
  check(!settings.clock.trs[0].clock && settings.clock.trs[0].transport,
        "a TRS gate edit writes the session clock section");
  session.setTrs2Transport(false);
  session.setUsbClock(false);
  check(!settings.clock.trs[1].transport && !settings.clock.usb.clock,
        "TRS OUT2 and USB gates write their own fields");
  check(session.clockText().contains("TRS1 off/on"),
        "the clock summary reports the current gates");

  settings.clock.trs[0].clock = true;
  settings.clock.trs[0].transport = true;
  session.pushClockToLink();
  check(session.trs1Clock() && session.trs1Transport(),
        "pushClockToLink re-reads the session gates");

  // the CLOCK SOURCE choice writes session clock.source + transportFollow.
  check(session.clockSourceChoice() == 0 && session.clockSourceText() == "INTERNAL",
        "the clock source defaults to the internal generator");
  session.setClockSourceChoice(1);
  check(settings.clock.source == 1 && settings.clock.transportFollow[0] &&
            !settings.clock.transportFollow[1] &&
            !settings.clock.transportFollow[2],
        "FOLLOW TRS selects TRS IN1 and its transport follow");
  check(session.clockSourceText().contains("TRS IN1"),
        "the choice readout names the followed input");
  session.setClockSourceChoice(2);
  check(settings.clock.source == 0 && settings.clock.transportFollow[2] &&
            !settings.clock.transportFollow[0],
        "FOLLOW USB keeps the generator internal and follows USB transport");
  check(session.clockSourceText().contains("USB"),
        "the USB choice is named as transport, not as a clock source");
  session.setClockSourceChoice(0);
  check(settings.clock.source == 0 && !settings.clock.transportFollow[0] &&
            !settings.clock.transportFollow[2],
        "INTERNAL clears every transport follow");
}

// Device output routing (session v2 deviceOut): the System pickers write the
// pairs the session carries, the labels cover the channel range the format
// accepts, and refresh() re-reads a loaded session.
void deviceOutTests() {
  AppState state;
  SessionSettings settings;
  MixerModel mixer;
  MidiGlue midi(false);
  LinkClient link(midi, state, true);
  SessionViewModel session(state, mixer, link, settings);

  check(session.deviceOutMain() == 0 && session.deviceOutOut34() == 1 &&
            session.deviceOutPhones() == 2,
        "device outputs default to Main 1&2, OUT3&4 and phones 5&6");
  check(session.deviceOutPairLabels().size() == kDeviceOutPairs &&
            session.deviceOutPairLabels().value(0) == "1&2" &&
            session.deviceOutPairLabels().value(1) == "3&4" &&
            session.deviceOutPairLabels().value(kDeviceOutPairs - 1) == "31&32",
        "the pair labels cover the session device channel range");

  session.setDeviceOutMain(3);
  check(settings.deviceOut.main[0] == 6 && settings.deviceOut.main[1] == 7 &&
            session.deviceOutMain() == 3,
        "the MAIN picker writes both device channels of the pair");
  session.setDeviceOutOut34(0);
  check(settings.deviceOut.out34[0] == 0 && settings.deviceOut.out34[1] == 1,
        "the OUT3&4 picker writes its own pair");
  session.setDeviceOutPhones(kDeviceOutPairs + 5);
  check(settings.deviceOut.phones[0] == 2 * (kDeviceOutPairs - 1) &&
            settings.deviceOut.phones[1] == 2 * (kDeviceOutPairs - 1) + 1,
        "a pair index past the picker list clamps to the last pair");

  settings.deviceOut.out34 = {4, 5};
  session.refresh();
  check(session.deviceOutOut34() == 2, "refresh re-reads a loaded session pair");
}

// Compact input source on every MIXER strip: one list of NONE, mono IN 01..18
// and the nine stereo pairs, wrapping in both directions. The panel encoder
// steps the same list (adjustFocused "mixer.in.N").
void mixerInputSourceTests() {
  SessionSettings settings;
  MixerModel model;
  EqModel eq;
  // The mixer list is dynamic: seed the two slots this test addresses so the
  // model rows exist, exactly as ADD TRACK would.
  model.addTrack(0, "");
  model.addTrack(7, "");
  MixerViewModel view(model, settings, eq);

  check(view.inSourceText(0) == "IN 01", "a strip starts on its own input");
  check(view.data(view.index(0, 0), MixerViewModel::InputTextRole).toString() ==
            QStringLiteral("IN 01"),
        "the strip role carries the input source text");
  check(view.cycleInSource(0, 1) == "IN 02", "the strip selector steps to the next input");
  check(view.cycleInSource(0, -1) == "IN 01", "and steps back");
  check(view.cycleInSource(0, -1) == "NONE", "below IN 01 is NONE");
  const int lastPairChannel = MixerViewModel::inputChannelCount() - 2;
  check(view.cycleInSource(0, -1) ==
            MixerViewModel::inputText(lastPairChannel, true),
        "below NONE the selector wraps to the last stereo pair");
  check(model.inCh(0) == lastPairChannel && model.inPair(0),
        "the last pair starts on the last even device channel");
  check(view.cycleInSource(0, 1) == "NONE", "above the last pair is NONE again");
  check(view.cycleInSource(0, 1) == "IN 01", "and the list starts over");
  model.setInCh(0, 4);
  check(view.inSourceText(0) == "IN 05", "a mono pick reads back through the selector");
  check(view.toggleInPair(0) == "IN 05/06", "PAIR binds the next device channel");
  check(model.inCh(0) == 4 && model.inPair(0), "the pair keeps an even first channel");
  check(view.toggleInPair(0) == "IN 05", "PAIR releases the second channel");
  check(view.cycleInSource(0, 1) == "IN 06", "a mono step moves one channel");
  check(view.cycleInSource(7, 1) == "IN 09",
        "a strip's selector writes that strip only");
  check(view.inSourceText(0) == "IN 06" && model.inCh(1) == 1,
        "other strips keep their source");
}

// System device list columns. The name is the backend's own string (a generic
// repeated name is displayed as-is, never replaced), and the row carries the
// backend it came from and the channel count next to it.
void systemDeviceRowTests() {
  std::vector<AudioDeviceOption> devices(2);
  devices[0].id = "pipewire:alsa_output.usb-umc1820";
  devices[0].name = "USB";
  devices[0].isDefault = true;
  devices[1].id = "alsa:hw:2";
  devices[1].name = "USB";

  const auto rows = SystemViewModel::deviceRows(devices);
  check(rows.size() == 2, "one row per enumerated device");
  const auto first = rows.value(0).toMap();
  const auto second = rows.value(1).toMap();
  check(first.value(QStringLiteral("name")).toString() == QStringLiteral("USB (default)") &&
            second.value(QStringLiteral("name")).toString() == QStringLiteral("USB"),
        "rows keep the backend's device names and mark the default");
  check(first.value(QStringLiteral("backend")).toString() == QStringLiteral("pipewire") &&
            second.value(QStringLiteral("backend")).toString() == QStringLiteral("alsa"),
        "the row names the backend the device came from");
  check(first.contains(QStringLiteral("channels")) &&
            second.value(QStringLiteral("channels")).toString() == QStringLiteral("?"),
        "an unknown channel count is shown as unknown, not guessed");
  check(first.value(QStringLiteral("label")).toString().contains(
            QStringLiteral("pipewire")) &&
            first.value(QStringLiteral("label")).toString().contains(QStringLiteral("USB")),
        "the closed field carries the same three columns");
}

void timelineAndPipeTests() {
  using namespace mnky::app::timeline;
  check(measureBeat(1) == 0.0 && measureBeat(5) == 16.0,
        "punch measure maps to session beats");
  checkNear(takeLengthBeats(2.0, 12000), 4.0, 1e-9,
            "take seconds map to beats at the transport tempo");
  checkEqual(beatText(4.0), "002:1:00", "beat text is bar:beat:tick");

  const TakeLengthFn length = [](const std::string&) { return 16.0; };
  std::vector<session::TakeRegion> region{session::TakeRegion{
      "take.wav", 0.0, 0.0, 0.0f, 0.0, 0.0, false}};
  BeatRange range;
  range.active = true;
  range.in = 2.0;
  range.out = 6.0;
  auto trimmed = trimRegions(region, range, length);
  check(trimmed.size() == 1 && std::abs(trimmed[0].offsetBeats - 2.0) < 1e-9 &&
            std::abs(trimmed[0].lengthBeats - 4.0) < 1e-9,
        "TRIM clips the region list to IN..OUT");
  auto split = splitRegionsAt(trimmed, 4.0, length);
  check(split.size() == 2 && std::abs(split[0].lengthBeats - 2.0) < 1e-9 &&
            std::abs(split[1].offsetBeats - 4.0) < 1e-9,
        "SPLIT cuts the region containing the cursor");
  BeatRange firstHalf;
  firstHalf.active = true;
  firstHalf.in = 2.0;
  firstHalf.out = 4.0;
  auto gained = gainRegions(split, firstHalf, -3.0f, length);
  checkNear(gained[0].gainDb, -3.0, 1e-9, "GAIN targets the overlapping region");
  checkNear(gained[1].gainDb, 0.0, 1e-9, "GAIN leaves the other region alone");
  auto faded = fadeRegions(gained, firstHalf, 0.5, 0.25, length);
  checkNear(faded[0].fadeInBeats, 0.5, 1e-9, "FADES sets the region fade in");
  auto normalized = normalizeRegions(faded, firstHalf, length);
  check(normalized[0].normalize && !normalized[1].normalize,
        "NORMALIZE flags the targeted region");
  auto deleted = deleteRegions(normalized, firstHalf, length);
  check(deleted.size() == 1 && std::abs(deleted[0].offsetBeats - 4.0) < 1e-9,
        "DELETE removes overlapping regions");
  BeatRange inactive;
  check(trimRegions(region, inactive, length) == region,
        "an inactive range is not a trim");

  audio::PeakSummary summary;
  summary.sourceFrames = 1024;
  summary.bucketFrames = 256;
  summary.buckets = {audio::PeakRange{0.0f, 0.25f}, audio::PeakRange{-0.5f, 0.5f},
                     audio::PeakRange{-0.75f, 0.75f}, audio::PeakRange{0.0f, 1.0f}};
  const auto wave = reducePeaks(summary, 0.0, 4.0, 4.0, 2);
  check(wave.size() == 2 && std::abs(wave[0] - 0.5f) < 1e-6 &&
            std::abs(wave[1] - 1.0f) < 1e-6,
        "peaks reduce to the requested waveform columns");

  // EDIT popup wave: it keeps polarity (lo, hi per column) and
  // switches to the folded coarse level once the fine buckets are denser than
  // the columns.
  const auto pairs = reducePeakPairs(summary, 0.0, 4.0, 4.0, 2);
  check(pairs.size() == 4 && std::abs(pairs[0] - 0.5f) < 1e-6 &&
            std::abs(pairs[1] - 0.5f) < 1e-6 &&
            std::abs(pairs[2] - 0.75f) < 1e-6 &&
            std::abs(pairs[3] - 1.0f) < 1e-6,
        "popup peaks keep the low/high pair for every column");
  audio::PeakSummary coarseSummary;
  coarseSummary.sourceFrames = 4096;
  coarseSummary.bucketFrames = 256;
  coarseSummary.coarseFactor = 4;
  coarseSummary.buckets.assign(16, audio::PeakRange{0.0f, 0.1f});
  coarseSummary.buckets[15] = audio::PeakRange{-0.9f, 0.9f};
  coarseSummary.coarse = {audio::PeakRange{0.0f, 0.1f},
                          audio::PeakRange{0.0f, 0.1f},
                          audio::PeakRange{0.0f, 0.1f},
                          audio::PeakRange{-0.9f, 0.9f}};
  const auto coarsePairs = reducePeakPairs(coarseSummary, 0.0, 4.0, 4.0, 1);
  check(coarsePairs.size() == 2 && std::abs(coarsePairs[0] - 0.9f) < 1e-6 &&
            std::abs(coarsePairs[1] - 0.9f) < 1e-6,
        "zoom plus coarse level reaches the take's own peak");
  checkEqual(beatTextCompact(0.0), "1:1:00",
             "popup footer beats are bar:beat:tick without padding");
  checkEqual(beatTextCompact(4.5), "2:1:12",
             "popup footer carries the sub-beat tick");
  checkEqual(editOpName(EditOp::DeleteRegion), "DELETE REGION",
             "the ops strip names the destructive verb");
  check(editOpHasValue(EditOp::Gain) && editOpHasValue(EditOp::FadeOut) &&
            !editOpHasValue(EditOp::Normalize),
        "only GAIN and the fades carry a value knob");
  check(editOpNeedsRange(EditOp::Trim) && editOpNeedsRange(EditOp::DeleteRegion) &&
            !editOpNeedsRange(EditOp::Normalize),
        "TRIM and DELETE REGION need IN..OUT");
  EditOpRequest request;
  check(editOpRequest(EditOp::FadeOut, 0.0f, 1.5, 0.75, &request) &&
            request.op == TimelineOp::Fades && request.fadeInBeats == 0.0 &&
            std::abs(request.fadeOutBeats - 0.75) < 1e-9,
        "the FADE OUT card runs FADES with only the out ramp");
  check(!editOpRequest(EditOp::Undo, 0.0f, 0.0, 0.0, &request),
        "UNDO is an action, not a region op");

  check(pipeui::isKnown(1) && pipeui::isKnown(16) && !pipeui::isKnown(0) &&
            !pipeui::isKnown(17),
        "the generated schema defines the 16 pipe types");
  check(pipeui::paramCount(1) == 8 && pipeui::paramCount(12) == 6,
        "positional pipe parameter counts come from the schema");
  check(!pipeui::isClocked(11) && pipeui::isClocked(12) && pipeui::isClocked(16),
        "the schema marks the clocked (Wave 2) types");
  checkEqual(pipeui::pipeRoleName(1).toStdString(), "MODIFIER",
             "a stateless pipe reports its schema role");
  checkEqual(pipeui::pipeRoleName(12).toStdString(), "MODIFIER",
             "the arp is a modifier: it transforms incoming notes");
  check(pipeui::pipeDocs(1).startsWith(QStringLiteral("Per-message-kind pass gate")),
        "the pipe type list carries the schema doc line");
  checkEqual(pipeui::valueText(1, 0, 1).toStdString(), "ON",
             "toggle parameter formatting");
  checkEqual(pipeui::valueText(2, 0, 60).toStdString(), "C4",
             "note parameter formatting uses the schema format hint");
  checkEqual(pipeui::valueText(12, 3, 50).toStdString(), "50%",
             "percent parameter carries its unit");
  checkEqual(pipeui::valueText(16, 0, 250).toStdString(), "250 ms",
             "millisecond parameter carries its unit");
  checkEqual(pipeui::valueText(15, 1, 4).toStdString(), "+4 st",
             "semitone parameter is signed with its unit");
  checkEqual(pipeui::noteName(60).toStdString(), "C4", "MIDI note naming");
  checkEqual(pipeui::learnTargetPath(2, 1, 3, 4), "routing.pipe.2.1.3.4",
             "pipe parameter dotted learn path");
  // Schema-driven editors: every type renders exactly as many editor rows as the
  // schema declares parameters, and the editor kind follows the schema format.
  for (int typeId = 1; typeId <= 16; ++typeId) {
    const int count = pipeui::paramCount(typeId);
    const auto rows = pipeui::editorRows(typeId, {}, 0, count);
    check(rows.size() == count, "every schema parameter renders one card");
    for (const auto& row : rows) {
      const auto map = row.toMap();
      check(!map.value(QStringLiteral("name")).toString().isEmpty() &&
                !map.value(QStringLiteral("editor")).toString().isEmpty() &&
                !map.value(QStringLiteral("valueText")).toString().isEmpty(),
            "each editor row has a schema label, editor kind and value text");
      const QString editor = map.value(QStringLiteral("editor")).toString();
      const int minimum = map.value(QStringLiteral("minimum")).toInt();
      const int maximum = map.value(QStringLiteral("maximum")).toInt();
      check(minimum <= map.value(QStringLiteral("value")).toInt() &&
                map.value(QStringLiteral("value")).toInt() <= maximum,
            "the row default is inside its schema range");
      check(editor == "toggle" || editor == "choice" || editor == "knob" ||
                editor == "stepper",
            "the editor kind is one the generic row implements");
    }
  }

  // The throwaway codegen fixture type (midifx/tests/fixtures/test-pipe-type.json,
  // never shipped): five parameters, one of every editor format. The same
  // generic row builder that serves the real schema renders its five editors,
  // which is the phase-3 rule "N parameters = N editors, no per-type QML".
  {
    namespace generated = mnky::midifx::generated;
    namespace ui = mnky::midifx::ui;
    static constexpr const char* kTestShapeChoices[] = {"RAMP", "STEP", "SINE"};
    static const generated::ParamSpec kTestParams[] = {
        {"on", "TEST ON", generated::ParamKind::Bool, 0, 1, 1, "", nullptr, 0, 0},
        {"shape", "TEST SHAPE", generated::ParamKind::Enum, 0, 2, 1, "",
         kTestShapeChoices, 3, 1},
        {"amount", "TEST AMOUNT", generated::ParamKind::Int, 0, 100, 25, "%",
         nullptr, 0, 2},
        {"offset", "TEST OFFSET", generated::ParamKind::Int, -24, 24, -2, "st",
         nullptr, 0, 3},
        {"rootNote", "TEST NOTE", generated::ParamKind::Int, 0, 127, 60, "",
         nullptr, 0, 4},
    };
    static const ui::ParamUiSpec kTestUi[] = {
        {&kTestParams[0], ui::ValueFormat::Bool, ui::EditorKind::Toggle},
        {&kTestParams[1], ui::ValueFormat::Choice, ui::EditorKind::Choice},
        {&kTestParams[2], ui::ValueFormat::Percent, ui::EditorKind::Knob},
        {&kTestParams[3], ui::ValueFormat::Semitones, ui::EditorKind::Knob},
        {&kTestParams[4], ui::ValueFormat::Note, ui::EditorKind::Knob},
    };
    const auto rows = pipeui::editorRowsFor(kTestParams, kTestUi, 5, {}, 0, 5);
    check(rows.size() == 5, "a new five-parameter type renders five editors");
    const auto editor = [&rows](int index) {
      return rows[index].toMap().value(QStringLiteral("editor")).toString().toStdString();
    };
    const auto text = [&rows](int index) {
      return rows[index].toMap().value(QStringLiteral("valueText")).toString().toStdString();
    };
    checkEqual(editor(0), "toggle", "the fixture bool is a toggle");
    checkEqual(editor(1), "choice", "the fixture enum is a choice");
    checkEqual(editor(2) + editor(3) + editor(4), "knobknobknob",
               "the fixture unit values are arc knobs");
    checkEqual(text(0), "ON", "the fixture bool formats as ON");
    checkEqual(text(1), "STEP", "the fixture enum formats as its choice name");
    checkEqual(text(2), "25%", "the fixture percent keeps its unit");
    checkEqual(text(3), "-2 st", "the fixture offset keeps its sign and unit");
    checkEqual(text(4), "C4", "the fixture note formats as a note name");
    const auto tail = pipeui::editorRowsFor(kTestParams, kTestUi, 5, {}, 4, 5);
    check(tail.size() == 1 &&
              tail[0].toMap().value(QStringLiteral("param")).toInt() == 4,
          "a clipped range renders only the remaining positional parameter");
  }

  // Chain preset files (phase 4): export/import round-trip, schema version and
  // the refusal reasons the keypad shows.
  {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path directory = fs::temp_directory_path(ec) / "mnkybox-logic-chain";
    fs::create_directories(directory, ec);
    std::vector<session::MidiPipe> chain;
    session::MidiPipe arp;
    arp.type = session::MidiPipeType::Arp;
    arp.chMask = 0x00FF;
    arp.params = {1, 2, 3, 40, 20, 75};
    chain.push_back(arp);
    session::MidiPipe noteRange;
    noteRange.type = session::MidiPipeType::NoteRange;
    noteRange.bypass = true;
    noteRange.params = {36, 96, 1, 127};
    chain.push_back(noteRange);

    pipe_preset::ChainFile exported;
    exported.name = QStringLiteral("chain-0-2");
    exported.pipes = chain;
    const fs::path file = directory / "chain-0-2.json";
    check(pipe_preset::write(file, exported).isEmpty(), "a chain exports to JSON");
    pipe_preset::ChainFile imported;
    check(pipe_preset::read(file, imported).isEmpty(), "an exported chain reads back");
    check(imported.pipes == chain && imported.name == exported.name,
          "the chain round-trips type, bypass, channel scope and params");

    const fs::path missing = directory / "missing.json";
    check(!pipe_preset::read(missing, imported).isEmpty(),
          "a missing chain file is refused with a reason");

    pipe_preset::ChainFile unknown;
    QByteArray body = pipe_preset::encode(exported);
    const int marker = body.indexOf("\"t\": 12");
    check(marker > 0, "the exported type id is visible in the file");
    body.replace(marker, 7, "\"t\": 99");
    check(pipe_preset::decode(body, unknown).contains(QStringLiteral("unknown type id 99")),
          "an unknown pipe type is refused by id");

    pipe_preset::ChainFile newer;
    QByteArray futureSchema = pipe_preset::encode(exported);
    const QByteArray schemaField = "\"schema\": 1";
    const int schemaAt = futureSchema.indexOf(schemaField);
    check(schemaAt >= 0, "the exported schema version is visible in the file");
    futureSchema.replace(schemaAt, schemaField.size(), "\"schema\": 9");
    check(pipe_preset::decode(futureSchema, newer).contains(QStringLiteral("newer")),
          "a chain file from a newer schema is refused");

    pipe_preset::ChainFile clamped;
    QByteArray outOfRange = pipe_preset::encode(exported);
    check(pipe_preset::decode(outOfRange, clamped).isEmpty(),
          "the exported body parses without a reason");
    std::vector<session::MidiPipe> tooLong(9, session::MidiPipe{});
    pipe_preset::ChainFile oversized;
    oversized.pipes = tooLong;
    check(pipe_preset::decode(pipe_preset::encode(oversized), clamped)
              .contains(QStringLiteral("limit is 8")),
          "a chain longer than the wire limit is refused");

    fs::path resolved;
    check(pipe_preset::resolvePath(QStringLiteral("take-style"),
                                   QString::fromStdString(directory.string()), resolved)
                  .isEmpty() &&
              resolved == directory / "take-style.json",
          "a bare preset name resolves inside the project folder");
    check(!pipe_preset::resolvePath(QStringLiteral("take-style"), QString(), resolved)
               .isEmpty(),
          "a bare name without a project is refused instead of writing elsewhere");
    check(!pipe_preset::validateName(QStringLiteral("../escape")).isEmpty(),
          "a preset name may not contain a path separator");
    fs::remove_all(directory, ec);
  }

  MidiGlue midi(false);
  std::vector<session::MidiPipe> chain;
  session::MidiPipe filterPipe;
  filterPipe.type = session::MidiPipeType::Filter;
  filterPipe.params = {1, 1, 0, 1, 1, 1, 1, 0};
  chain.push_back(filterPipe);
  check(midi.setRoutePipes(midi::Port::TrsIn1, midi::Port::UsbOut, chain),
        "MidiGlue stores a pipe chain");
  check(midi.routePipes(midi::Port::TrsIn1, midi::Port::UsbOut).size() == 1,
        "stored pipe chain reads back");
  check(midi.setRoutePipeParam(midi::Port::TrsIn1, midi::Port::UsbOut, 0, 2, 0),
        "pipe parameter write succeeds");
  check(midi.routePipes(midi::Port::TrsIn1, midi::Port::UsbOut)[0].params[2] == 0,
        "pipe parameter write is visible");
  check(midi.routePipeLearnTargets().size() == 8,
        "each configured pipe parameter is a MIDI-learn target");
  check(midi.addRoutePipe(midi::Port::TrsIn1, midi::Port::UsbOut,
                          session::MidiPipeType::Arp),
        "pipe add succeeds");
  check(midi.moveRoutePipe(midi::Port::TrsIn1, midi::Port::UsbOut, 1, -1),
        "pipe reorder succeeds");
  check(midi.routePipes(midi::Port::TrsIn1, midi::Port::UsbOut)[0].type ==
            session::MidiPipeType::Arp,
        "pipe reorder changes the ordered list");
  check(midi.removeRoutePipe(midi::Port::TrsIn1, midi::Port::UsbOut, 0),
        "pipe remove succeeds");

  // The engine-split readout states what the code does. The shared core
  // implements all 16 types, the 24-PPQN grid pass is missing Pi-side
  // (MidiGlue::pump calls Router::tick, not Router::clockTick), and the readout
  // says exactly that.
  {
    MidiGlue sweepMidi(false);
    RoutingViewModel sweep(sweepMidi, false);
    sweep.setPipeSource(0);
    sweep.setPipeDestination(2);  // TRS IN 1 -> USB OUT: Pi-owned
    sweep.setAddTypeIndex(11);    // ARP (clocked, schema id 12)
    sweep.addPipe();
    const QString status = sweep.pipeStatusText();
    check(!status.contains(QStringLiteral("rejects")),
          "the retired Wave-2 rejection is not claimed any more");
    check(status.contains(QStringLiteral("PI ENGINE")),
          "a USB-touching chain reports the Pi engine");
    check(status.contains(QStringLiteral("NO 24-PPQN TICK")),
          "a clocked pipe on a Pi-owned route names the missing grid pass");
    const auto statusRow = sweep.pipeRows().value(0).toMap();
    checkEqual(statusRow.value(QStringLiteral("status")).toString().toStdString(),
               "NO PI CLOCK TICK",
               "the clocked chain row carries the warning chip");
    check(statusRow.value(QStringLiteral("warning")).toBool(),
          "the clocked row is flagged for the warning tone");

    // The same pipe on a TRS->TRS route belongs to the Pico, whose route engine
    // drives clockTick(): there the clock is live.
    sweep.setPipeDestination(0);
    sweep.setAddTypeIndex(11);
    sweep.addPipe();
    check(sweep.pipeStatusText().contains(QStringLiteral("PICO 0x1A CHAIN")),
          "a TRS->TRS chain reports the Pico chain");
    check(sweep.pipeStatusText().contains(QStringLiteral("CLOCKED PIPE LIVE")),
          "the Pico chain runs the clocked pipe on its own tick grid");
  }
}

// The app half of the 0x1A split: only a TRS->TRS pair is mirrored to the Pico,
// a Pi-owned pair never is, and Router::tick is pumped so NoteLength offs are
// flushed without waiting for another input event.
// A generator route consumes no input. Its
// chain is stored per output, its head must carry the schema's generator role,
// building the chain opens the route gate, and the session round-trip keeps it.
void generatorRouteTests() {
  MidiGlue midi(false);
  session::MidiPipe ccLfo;
  ccLfo.type = session::MidiPipeType::CcLfo;
  ccLfo.params = {74, 0, 0, 0, 64, 64};
  session::MidiPipe filter;
  filter.type = session::MidiPipeType::Filter;

  check(!midi.addGeneratorPipe(midi::Port::UsbOut, session::MidiPipeType::Filter),
        "a modifier cannot head a generator chain (needs incoming notes)");
  check(midi.generatorPipes(midi::Port::UsbOut).empty(),
        "a refused head stores nothing");
  check(midi.addGeneratorPipe(midi::Port::UsbOut, session::MidiPipeType::CcLfo),
        "CC LFO heads a generator chain with no input set");
  check(midi.generatorPipes(midi::Port::UsbOut).size() == 1,
        "the generator chain stores without any input port");
  check(!midi.generatorRouteEnabled(midi::Port::UsbOut),
        "adding a chain does not silently enable the route in the glue");

  // Session round-trip: capture includes src=-1 even with the route disabled.
  auto captured = midi.captureSessionRouter();
  bool sawGenerator = false;
  for (const auto& route : captured.routes) {
    if (route.src == session::kSrcGenerator &&
        route.dst == midi::portId(midi::Port::UsbOut)) {
      sawGenerator = route.pipes.size() == 1 && !route.on;
    }
  }
  check(sawGenerator, "capture writes the generator route as src = -1");
  MidiGlue restored(false);
  restored.applySessionRouter(captured);
  check(restored.generatorPipes(midi::Port::UsbOut).size() == 1 &&
            !restored.generatorRouteEnabled(midi::Port::UsbOut),
        "project load restores the generator chain and its gate");

  // A TRS destination is Pico-owned: the chain is stored for the session and
  // reports honestly that this build does not send it.
  check(midi.setGeneratorPipes(midi::Port::TrsOut1, {ccLfo, filter}),
        "a TRS generator chain stores (Pico-owned)");
  check(midi.generatorPipes(midi::Port::TrsOut1).size() == 2,
        "the TRS generator chain is kept for the session");
  midi.setGeneratorRoute(midi::Port::TrsOut1, true);
  check(midi.generatorRouteEnabled(midi::Port::TrsOut1),
        "the TRS generator route gate is stored");
  midi.clearGeneratorRoutes();
  check(midi.generatorPipes(midi::Port::UsbOut).empty() &&
            midi.generatorPipes(midi::Port::TrsOut1).empty() &&
            !midi.generatorRouteEnabled(midi::Port::TrsOut1),
        "matrix RESET clears every generator route");

  // The pipe page itself: FROM = GENERATOR needs no selected input, the ADD
  // list is the generator-role head only while the chain is empty, and the
  // first ADD opens the gate.
  MidiGlue page(false);
  RoutingViewModel routing(page, false);
  int generatorNode = -1;
  int usbOutNode = -1;
  for (const auto& row : routing.inputNodes()) {
    if (row.toMap().value(QStringLiteral("port")).toInt() == midi::kSrcGenerator) {
      generatorNode = row.toMap().value(QStringLiteral("nodeIndex")).toInt();
    }
  }
  for (const auto& row : routing.outputNodes()) {
    if (row.toMap().value(QStringLiteral("port")).toInt() ==
        midi::portId(midi::Port::UsbOut)) {
      usbOutNode = row.toMap().value(QStringLiteral("nodeIndex")).toInt();
    }
  }
  check(generatorNode >= 0 && usbOutNode >= 0,
        "GENERATOR and USB OUT are selectable pipe endpoints");
  routing.setPipeSource(generatorNode);
  routing.setPipeDestination(usbOutNode);
  check(routing.generatorSelected() && routing.generatorHeadPending(),
        "GENERATOR is selected with no input and no head");
  check(routing.addTypeLabels().size() == 1 &&
            routing.addTypeLabels().first().contains(QStringLiteral("LFO")),
        "a pending generator head lists only the generator role");
  routing.setAddTypeIndex(0);
  routing.addPipe();
  check(routing.pipeCount() == 1,
        "ADD PIPE works on a generator route without any input source");
  check(routing.routeEnabled(generatorNode, usbOutNode),
        "building the generator chain opens its own route gate");
  routing.addPipe();
  check(routing.pipeCount() == 2 && routing.addTypeLabels().size() > 1,
        "after the head the full modifier list returns");
  check(routing.pipeStatusText().contains(QStringLiteral("GENERATOR")),
        "the status names the source-less route");
  routing.removePipe(1);
  routing.removePipe(0);
  check(routing.pipeCount() == 0, "the generator chain can be emptied");
  check(routing.pipeStatusText().contains(QStringLiteral("NEEDS NO INPUT")),
        "an empty generator route still says it needs no input");
}

void pipeLinkTests() {
  MidiGlue midi(false);
  struct Sync {
    midi::Port source{};
    midi::Port destination{};
    bool enabled = false;
    std::size_t pipes = 0;
  };
  std::vector<Sync> syncs;
  midi.setPipeSync([&syncs](midi::Port source, midi::Port destination,
                            const std::vector<session::MidiPipe>& pipes,
                            bool enabled) {
    syncs.push_back(Sync{source, destination, enabled, pipes.size()});
  });
  session::MidiPipe filter;
  filter.type = session::MidiPipeType::Filter;
  filter.params = {1, 1, 1, 1, 1, 1, 1, 0};
  check(midi.setRoutePipes(midi::Port::TrsIn1, midi::Port::UsbOut, {filter}),
        "Pi-owned chain stores");
  check(syncs.empty(), "a Pi-owned chain is never mirrored to the Pico");
  check(midi.routeHasPipes(midi::Port::TrsIn1, midi::Port::UsbOut),
        "routeHasPipes sees the Pi-owned chain");

  check(midi.setRoutePipes(midi::Port::TrsIn1, midi::Port::TrsOut1, {filter}),
        "TRS->TRS chain stores");
  check(syncs.size() == 1 && syncs[0].source == midi::Port::TrsIn1 &&
            syncs[0].destination == midi::Port::TrsOut1 && !syncs[0].enabled &&
            syncs[0].pipes == 1,
        "TRS->TRS chain is mirrored with the route enable flag");
  midi.setRoute(midi::Port::TrsIn1, midi::Port::TrsOut1, true);
  check(syncs.size() == 2 && syncs[1].enabled,
        "enabling a TRS route re-sends its chain with flags on");

  // Project load replaces every stored list; every TRS pair is re-pushed,
  // including empty ones so a removed chain cannot stay live on the Pico.
  syncs.clear();
  midi.setRoutePipeLists({});
  check(syncs.size() == 4, "project load mirrors all four TRS->TRS pairs");
  bool cleared = true;
  for (const auto& sync : syncs) cleared = cleared && sync.pipes == 0;
  check(cleared, "an empty project chain clears the Pico mirror");

  MidiGlue ticked(false);
  ticked.setRoute(midi::Port::TrsIn1, midi::Port::UsbOut, true);
  session::MidiPipe noteLength;
  noteLength.type = session::MidiPipeType::NoteLength;
  noteLength.params = {20};  // 20 ms fixed length
  check(ticked.setRoutePipes(midi::Port::TrsIn1, midi::Port::UsbOut, {noteLength}),
        "NoteLength chain stores on a Pi-owned route");
  midi::MidiEvent noteOn;
  noteOn.source = midi::Port::TrsIn1;
  noteOn.size = 3;
  noteOn.data[0] = 0x90;
  noteOn.data[1] = 60;
  noteOn.data[2] = 100;
  noteOn.stampUs = midi::piNowUs();
  ticked.handleInput(noteOn);
  std::this_thread::sleep_for(std::chrono::milliseconds(40));
  ticked.pump({});
  const auto snapshot = ticked.monitorSnapshot();
  bool sawOff = false;
  for (std::size_t i = 0; i < snapshot.count; ++i) {
    const auto& event = snapshot.events[i];
    if (event.outbound && event.port == midi::Port::UsbOut && event.size == 3 &&
        (event.data[0] & 0xF0u) == 0x80u) {
      sawOff = true;
    }
  }
  check(sawOff, "MidiGlue::pump drives Router::tick and flushes a due NoteLength off");
}

// EQ forwarding: the grid state reaches the engine's track/main inserts, and a
// loaded session Q above the UI/CLAP cap is normalized with a counted report.
void eqForwardingTests() {
  AppState state;
  MixerModel mixer;
  FxManager fx;
  PerformanceModel performance;
  AudioGlue audio(mixer, fx, performance, state, true);

  session::EqState eq;
  eq.tracks[0][0] = session::EqBand{3, 6.0f, 1200.0f, 2.5f};
  eq.tracks[17][3] = session::EqBand{1, 0.0f, 80.0f, 0.7f};
  eq.main[2] = session::EqBand{2, -3.0f, 12000.0f, 0.9f};
  audio.applyEq(eq);
  const auto applied = audio.appliedEq();
  check(applied.tracks[0][0].type == 3 &&
            std::abs(applied.tracks[0][0].freqHz - 1200.0f) < 1e-3f &&
            std::abs(applied.tracks[0][0].q - 2.5f) < 1e-4f,
        "EQ forwarding writes a track band to Engine::setTrackEqBand");
  check(applied.tracks[17][3].type == 1,
        "EQ forwarding reaches the last track strip");
  check(applied.main[2].type == 2 &&
            std::abs(applied.main[2].gainDb + 3.0f) < 1e-4f,
        "EQ forwarding writes the main bus through Engine::setMainEqBand");

  EqModel model;
  session::EqState loaded;
  loaded.tracks[4][1] = session::EqBand{3, 0.0f, 800.0f, 18.0f};
  loaded.main[0] = session::EqBand{1, 0.0f, 60.0f, 12.0f};
  const int normalized = model.apply(loaded);
  check(normalized == 2, "a loaded Q above 10 is counted so the loader can log it");
  const auto capture = model.capture();
  check(std::abs(capture.tracks[4][1].q - 10.0f) < 1e-4f &&
            std::abs(capture.main[0].q - 10.0f) < 1e-4f,
        "loaded Q above the UI/CLAP cap is normalized to 10");
}

// No-Pico mode: a bare Pi with no Pico. The link has no SPI transport or
// PicoLink, transport commands drive rolling directly, the Pi clock free-runs
// and TRS surfaces report UNAVAILABLE instead of a zero that looks idle.
void noPicoModeTests() {
  const auto waitFor = [](const std::function<bool()>& predicate) {
    for (int i = 0; i < 600; ++i) {
      if (predicate()) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
  };

  AppState state;
  MidiGlue midi(false);
  LinkClient link(midi, state, false, true);
  check(link.noPico(), "LinkClient reports no-Pico mode");
  check(!link.emulated(), "no-Pico is not the scripted-Pico emulator transport");
  auto status = link.status();
  check(status.noPico && !status.linkUp, "no-Pico status is NO PICO, not link down");
  check(status.tempoX100 == midi::UsbClock::kDefaultBpmX100,
        "no-Pico clock starts at the default tempo");
  check(!status.rolling, "no-Pico starts stopped");

  link.start();
  link.requestTempo(static_cast<std::uint16_t>(midi::UsbClock::kMinBpmX100));
  check(waitFor([&] { return link.status().tempoX100 == midi::UsbClock::kMinBpmX100; }),
        "no-Pico tempo edit reaches the Pi clock without a Pico");
  check(waitFor([&] { return state.tempoX100.load() == midi::UsbClock::kMinBpmX100; }),
        "no-Pico tempo edit writes AppState for the UI");

  // Slow tempo keeps the START tick-reset check race-free (one interval is
  // 125 ms at 20 bpm); the clock then runs at 128 bpm for the emission check.
  link.requestTransport(1);
  check(waitFor([&] { return link.status().rolling; }),
        "no-Pico START drives rolling directly (no EV_TRANSPORT frame)");
  check(waitFor([&] { return state.rolling.load(); }),
        "no-Pico START writes AppState::rolling");
  check(link.status().tick <= 1, "no-Pico START resets the Pi tick");

  link.requestTempo(12800);
  check(waitFor([&] { return link.status().tempoX100 == 12800; }),
        "no-Pico tempo changes while rolling");
  const std::uint32_t startTick = link.status().tick;
  check(waitFor([&] { return link.status().tick >= startTick + 3; }),
        "no-Pico clock free-runs and advances the Pi tick");
  check(state.tick.load() > 0, "no-Pico tick reaches AppState");

  // The USB clock gate mutes the output bytes but must not stall the timeline:
  // the Pico generator keeps ticking under its per-output gates too.
  const auto usbClockCount = [&] {
    return midi.monitorSnapshot()
        .counts[static_cast<std::size_t>(midi::Port::UsbOut)]
        .clock;
  };
  link.requestUsbClockGates(false, true);
  std::this_thread::sleep_for(std::chrono::milliseconds(60));
  const auto gatedCount = usbClockCount();
  const auto gatedTick = link.status().tick;
  std::this_thread::sleep_for(std::chrono::milliseconds(60));
  check(link.status().tick > gatedTick,
        "no-Pico tick keeps running with the USB clock gate closed");
  check(usbClockCount() == gatedCount,
        "no-Pico USB clock gate suppresses the emitted clock bytes");
  link.requestUsbClockGates(true, true);

  link.requestTransport(0);
  check(waitFor([&] { return !link.status().rolling; }),
        "no-Pico STOP clears rolling directly");
  const std::uint32_t stoppedTick = link.status().tick;
  std::this_thread::sleep_for(std::chrono::milliseconds(60));
  check(link.status().tick == stoppedTick, "no-Pico clock stops emitting while stopped");
  const auto noPicoStats = link.status().stats;
  check(noPicoStats.tx == 0 && noPicoStats.rx == 0 && !link.status().linkUp,
        "no-Pico link performs no SPI transactions and never reports a lost link");

  // A TRS route is refused by the no-Pico sink: nothing can receive 0x1A, the
  // Monitor records no TRS OUT send and the reason is logged once.
  midi.setRoute(midi::Port::TrsIn1, midi::Port::TrsOut1, true);
  check(midi.routeEnabled(midi::Port::TrsIn1, midi::Port::TrsOut1),
        "no-Pico still stores TRS matrix metadata");
  midi::MidiEvent note;
  note.source = midi::Port::TrsIn1;
  note.size = 3;
  note.data[0] = 0x90;
  note.data[1] = 60;
  note.data[2] = 100;
  midi.handleInput(note);
  check(waitFor([&] {
          return AppLogger::instance().tail(400).find(
                     "no-pico: TRS MIDI output unavailable") != std::string::npos;
        }),
        "no-Pico logs the suppressed 0x1A TRS route send");
  const auto snapshot = midi.monitorSnapshot();
  check(snapshot.counts[static_cast<std::size_t>(midi::Port::TrsOut1)].notes == 0,
        "no-Pico monitor records no TRS OUT send");
  // USB destinations still go through the Pi router; only their delivery needs
  // a USB output port, which this headless check does not open.
  midi.setRoute(midi::Port::UsbIn, midi::Port::UsbOut, true);
  check(midi.routeEnabled(midi::Port::UsbIn, midi::Port::UsbOut),
        "no-Pico keeps the USB routing matrix live");
  midi::MidiEvent usbNote = note;
  usbNote.source = midi::Port::UsbIn;
  midi.handleInput(usbNote);
  check(midi.routedCount() >= 2, "no-Pico routes USB messages in the Pi engine");
  link.stop();

  // TRS rows are honest UNAVAILABLE in the Routing and Monitor models.
  RoutingViewModel routing(midi, true);
  check(!routing.inputAvailable(0) && !routing.inputAvailable(1) &&
            routing.inputAvailable(2),
        "routing marks both TRS inputs UNAVAILABLE in no-Pico mode");
  check(!routing.outputAvailable(0) && routing.outputAvailable(2),
        "routing marks the TRS outputs UNAVAILABLE in no-Pico mode");
  routing.setShiftPage(1);
  routing.setPipeSource(0);
  routing.setPipeDestination(0);
  session::MidiPipe pipe;
  pipe.type = session::MidiPipeType::Filter;
  pipe.params = {1, 1, 1, 1, 1, 1, 1, 1};
  check(midi.setRoutePipes(midi::Port::TrsIn1, midi::Port::TrsOut1, {pipe}),
        "no-Pico stores a TRS pipe chain as project metadata");
  routing.refresh();
  check(routing.pipeStatusText().contains(QStringLiteral("INERT (NO PICO")),
        "pipe UI labels a TRS->TRS chain inert in no-Pico mode");
  check(routing.pipeRows().size() == 1 &&
            routing.pipeRows().first().toMap().value(QStringLiteral("status"))
                    .toString() == QStringLiteral("INERT (NO PICO)"),
        "pipe rows report INERT (NO PICO)");

  MidiMonitorViewModel monitor(midi, true);
  check(monitor.headerText().contains(QStringLiteral("NO PICO")),
        "monitor header says NO PICO");
  check(monitor.portSummary(0).contains(QStringLiteral("UNAVAILABLE")),
        "monitor TRS summary is UNAVAILABLE, not a zero counter");
  const auto trsRow = monitor.ports()->index(0, 0);
  const auto usbRow = monitor.ports()->index(2, 0);
  check(!monitor.ports()
             ->data(trsRow, MidiMonitorPortModel::AvailableRole)
             .toBool(),
        "monitor TRS row is not available in no-Pico mode");
  check(monitor.ports()->data(usbRow, MidiMonitorPortModel::AvailableRole).toBool(),
        "monitor USB row stays available in no-Pico mode");

  MidiMonitorViewModel hardwareMonitor(midi, false);
  check(hardwareMonitor.ports()
            ->data(trsRow, MidiMonitorPortModel::AvailableRole)
            .toBool(),
        "hardware mode keeps the TRS monitor rows available");
}

// The mode words, the record-source names and the scheduled-recording status
// strings are the UI contract, so they are asserted verbatim.
void trackModeTests() {
  check(trackModeWord(0) == "PLAY" && trackModeWord(1) == "ARM" &&
            trackModeWord(2) == "OVERDUB" && trackModeWord(3) == "LIVE",
        "mode words are LIVE/PLAY/ARM/OVERDUB");
  check(trackModeWord(-4) == "ARM" && trackModeWord(99) == "LIVE",
        "an out-of-range mode clamps to the 0..3 store");
  // The 7-tap first recording needs PLAY -> ARM in one tap.
  check(nextTrackMode(kTrackModePlay) == kTrackModeArm,
        "one tap on a PLAY track selects ARM");
  check(nextTrackMode(kTrackModeLive) == kTrackModePlay,
        "the cycle wraps LIVE -> PLAY");
  check(trackModeRecords(kTrackModeArm) && trackModeRecords(kTrackModeOverdub) &&
            !trackModeRecords(kTrackModePlay) && !trackModeRecords(kTrackModeLive),
        "ARM and OVERDUB record, PLAY and LIVE do not (W8)");

  check(recSourceText(0) == "PRE" && recSourceText(1) == "POST" &&
            recSourceText(2) == "POST+FX",
        "REC SOURCE names PRE / POST / POST+FX");
  check(recSourceText(clampRecSource(-1)) == "PRE" &&
            recSourceText(clampRecSource(9)) == "POST+FX",
        "REC SOURCE clamps to 0..2");

  MixerModel modeModel;
  modeModel.addTrack(0, "drums");
  modeModel.addTrack(1, "bass");
  check(modeModel.recSource(0) == kRecSourcePre, "a new track records PRE");
  modeModel.setRecSource(0, 2);
  check(modeModel.recSource(0) == kRecSourcePostFx,
        "the per-track REC SOURCE is stored");
  modeModel.setMode(0, kTrackModeArm);
  modeModel.setMode(1, kTrackModeOverdub);
  auto mask = modeModel.armMask();
  check((mask & 1u) != 0 && (mask & 2u) != 0,
        "the arm mask carries ARM and OVERDUB tracks");
  modeModel.setMode(0, kTrackModePlay);
  mask = modeModel.armMask();
  check((mask & 1u) == 0, "a PLAY track is never in the arm mask");
  modeModel.setArm(0, true);
  check(modeModel.mode(0) == kTrackModeArm, "ARM (the button) maps onto the ARM mode");
  modeModel.setArm(0, false);
  check(modeModel.mode(0) == kTrackModePlay, "disarming maps onto PLAY");

  // Schedule status strings: exactly what will happen and when.
  check(recScheduleStatus(kRecModeCountIn, 2, true, 1, 9, 13) ==
            "COUNT-IN 2 bars - recording at bar 1",
        "the count-in status names the bars and the engage bar");
  check(recScheduleStatus(kRecModePunch, 2, true, 1, 9, 13) ==
            "PUNCH - recording at bar 9 of 13",
        "the punch status names PUNCH IN of PUNCH OUT");
  check(recScheduleStatus(kRecModeNow, 2, false, 1, 9, 13).empty(),
        "NOW needs no schedule line");
  check(recModeText(kRecModeNow, 2) == "NOW" &&
            recModeText(kRecModeCountIn, 2) == "COUNT-IN 2" &&
            recModeText(kRecModePunch, 2) == "PUNCH",
        "the transport strip readout names the armed RECMODE");
  check(clampCountInBars(0) == 1 && clampCountInBars(99) == 8,
        "the count-in is 1..8 bars");
  check(barOfTick(0) == 1 && barOfTick(kTicksPerBar) == 2 &&
            barOfTick(kTicksPerBar * 8) == 9,
        "the measure boundary maps ticks to 1-based bars");
}

// Plugin activation, physical path: selecting a discovered plugin in the FX
// popup must instantiate it through the app's own rack sink and change the
// sound. The probe renders through AudioGlue's emulator driver (the same Engine
// the device runs) rather than attaching an effect to a bare Engine, so a
// silent app slot cannot pass.
void appClapActivationAudioTests() {
  AppState state;
  SessionSettings settings;
  MixerModel mixer;
  FxManager fx;
  PerformanceModel performance;
  EqModel eq;
  AudioGlue audio(mixer, fx, performance, state, true);
  MixerViewModel view(mixer, settings, eq);
  view.setClapHost(&fx);

  const auto plugins = fx.plugins();
  if (plugins.empty()) {
    check(view.fxSlotNote(0, 0).isEmpty() ||
              !view.fxSlotNote(0, 0).contains(QStringLiteral("NOT LOADED")),
          "a zero-CLAP device offers no dead plugin row");
    return;
  }
  const auto plugin = plugins.front();
  if (plugin.id != "com.cldmnky.mnkygain") return;  // needs the reference gain DSP

  check(view.addTrack(0, "drums", 0, false, 0), "the probe track exists");
  mixer.setMode(0, kTrackModeLive);
  mixer.setGainDb(0, 0.0f);
  check(audio.start(), "the emulator backend starts");

  // The peak meter is a read-and-decay hold. Drain the previous phase's hold
  // until it falls under the current block level before measuring, or the dry
  // peak would be read back as the wet one.
  const auto settlePeak = [&audio] {
    for (int i = 0; i < 24; ++i) {
      (void)audio.peak(0);
      std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }
  };
  const auto samplePeak = [&audio] {
    float peak = 0.0f;
    for (int i = 0; i < 40; ++i) {
      peak = std::max(peak, audio.peak(0));
      std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }
    return peak;
  };
  const float dry = samplePeak();
  check(dry > 0.01f, "the emulator input reaches the main bus before the FX");

  ConfigPopupModel popup(state, mixer, settings, eq);
  popup.setMixer(&view);
  popup.setFxTarget(0, 0);
  check(popup.openKind(QStringLiteral("fxslot")), "the FX slot list opens");
  popup.setFxSlotChoice(MixerViewModel::kFxChoicePlugin);
  check(view.fxSlotNote(0, 0).contains(QStringLiteral("active")),
        "the plugin is instantiated when it is selected (not only at load)");
  view.setFxParamAt(0, 0, 0, -24.0);
  check(!view.fxParams(0, 0).isEmpty(),
        "the reference plugin's own parameter cards are present");

  settlePeak();
  const float wet = samplePeak();
  check(wet < dry * 0.25f,
        "a selected plugin audibly processes the strip (gain -24 dB)");

  view.setFxBypassAt(0, 0, true);
  settlePeak();
  const float bypassed = samplePeak();
  check(bypassed > dry * 0.5f,
        "BYPASS restores the dry strip while the instance stays loaded");
  view.setFxBypassAt(0, 0, false);
  settlePeak();
  const float enabled = samplePeak();
  check(enabled < dry * 0.25f,
        "un-bypassing the slot re-engages the same plugin instance");
  // A slot that names a plugin but holds no live instance (a failed earlier
  // load, a host teardown) must retry when the same entry is selected again;
  // otherwise it reads as selected but stays dead.
  fx.unloadTrackEffect(0, 0);
  check(!fx.trackEffectLoaded(0, 0), "the probe slot lost its instance");
  const int choice = view.fxChoiceAt(0, 0);
  check(choice >= MixerViewModel::kFxChoicePlugin,
        "the slot still names the discovered plugin");
  check(view.setFxChoiceAt(0, 0, choice),
        "re-selecting the same plugin succeeds (it is not a silent no-op)");
  check(fx.trackEffectLoaded(0, 0),
        "re-selecting a dead plugin entry retries the host instance");
  popup.cancel();
  audio.stop();
}

// NEW TRACK FROM TAKE must grow the mixer's track list, land the bank on the
// new strip and keep the take ACTIVE on it. Drives the same public
// controller/model calls the recorder's "..." menu and MOVE button use.
void newTrackFromTakeTests() {
  AppController controller(true, true);
  MixerViewModel* mixer = controller.mixer();
  const char* names[] = {"drums", "bass", "keys", "gtr", "vox", "perc"};
  for (int slot = 0; slot < 6; ++slot) {
    mixer->addTrack(slot, QString::fromLatin1(names[slot]), slot, false, 0);
  }
  mixer->setBank(0);
  const int before = mixer->stripCount();
  check(before == 6, "the fixture mixer has six tracks");

  const auto base = std::filesystem::temp_directory_path() /
                    ("mnkybox-new-track-take-" +
                     std::to_string(std::chrono::steady_clock::now()
                                        .time_since_epoch()
                                        .count()));
  std::error_code error;
  std::filesystem::create_directories(base, error);
  const auto wav = base / "beat.wav";
  writeMinimalWav(wav);
  check(controller.recorder()->importTake(QString::fromStdString(wav.string())).isEmpty(),
        "the take fixture imports");
  int row = -1;
  for (int candidate = 0; candidate < controller.recorder()->takes()->count();
       ++candidate) {
    if (controller.recorder()->takeNameAt(candidate) == QStringLiteral("beat.wav")) {
      row = candidate;
      break;
    }
  }
  check(row >= 0, "the imported take is listed");
  if (row < 0) {
    std::filesystem::remove_all(base, error);
    return;
  }

  controller.openTakeActions(row);
  controller.config()->chooseTakeAction(6);
  check(controller.keypadOpen(), "NEW TRACK FROM THIS TAKE asks for a name");
  controller.keypadAccept(QStringLiteral("beat"));
  check(controller.config()->kind() == QStringLiteral("trackfromtake"),
        "the MOVE / KEEP ON SOURCE step opens");
  controller.config()->chooseTrackFromTake(0);

  check(mixer->stripCount() == before + 1,
        "NEW TRACK FROM TAKE creates a real track");
  check(mixer->trackIndex() == 6, "the mixer selects the new track's slot");
  check(mixer->bank() == 2,
        "the mixer bank lands on the page that shows the new track");
  check(mixer->index(0, 0).isValid() &&
            mixer->data(mixer->index(6, 0), MixerViewModel::NameRole).toString() ==
                QStringLiteral("beat"),
        "the new track carries the keypad name");
  const QString active = controller.recorder()->activeTakePath(6);
  check(active.endsWith(QStringLiteral("beat.wav")),
        "the new track plays the take (ACTIVE)");
  check(controller.recorder()->activeTakePath(0).isEmpty(),
        "MOVE leaves the source track without that take");
  std::filesystem::remove_all(base, error);
}

void styleTests() {
  QFile file(QStringLiteral(":/qml/Style.qml"));
  check(file.open(QIODevice::ReadOnly | QIODevice::Text), "QML Style resource opens");
  const QString qml = QString::fromUtf8(file.readAll());

  std::map<QString, QColor> colours;
  QRegularExpression colourPattern(
      QStringLiteral("readonly\\s+property\\s+color\\s+(\\w+)\\s*:\\s*\"(#[0-9a-fA-F]{6})\""));
  auto matches = colourPattern.globalMatch(qml);
  while (matches.hasNext()) {
    const auto match = matches.next();
    colours.insert_or_assign(match.captured(1), QColor(match.captured(2)));
  }
  check(colours.count("background") == 1 && colours.count("panel") == 1 &&
            colours.count("raised") == 1 && colours.count("text") == 1 &&
            colours.count("textDim") == 1 && colours.count("accent") == 1 &&
            colours.count("record") == 1,
        "Style.qml defines the measured colour tokens");

  const auto background = colours["background"];
  const auto panel = colours["panel"];
  const auto raised = colours["raised"];
  const auto text = colours["text"];
  const auto textDim = colours["textDim"];
  const auto accent = colours["accent"];
  const auto record = colours["record"];
  check(luminance(background) <= 0.03, "surfaces stay near-black (luminance <= 0.03)");
  check(luminance(panel) <= 0.03, "panel surface stays near-black");
  check(luminance(raised) <= 0.03, "raised surface stays near-black");
  check(contrast(text, background) >= 7.0, "primary text contrast >= 7:1");
  check(contrast(text, raised) >= 7.0, "primary text on raised >= 7:1");
  check(contrast(textDim, background) >= 4.5, "dim text contrast >= 4.5:1");
  check(contrast(textDim, raised) >= 4.5, "dim text on raised >= 4.5:1");
  check(contrast(accent, background) >= 4.5, "accent contrast >= 4.5:1");
  check(contrast(accent, raised) >= 4.5, "accent on raised >= 4.5:1");
  check(contrast(record, raised) >= 4.5, "danger text on raised >= 4.5:1");
  check(contrast(accent, panel) >= 3.0, "selected accent vs idle panel >= 3:1");

  QRegularExpression touchPattern(QStringLiteral("property\\s+int\\s+touch\\s*:\\s*(\\d+)"));
  const auto touch = touchPattern.match(qml);
  check(touch.hasMatch() && touch.captured(1).toInt() >= 44,
        "Style.touch keeps the 44 px minimum");
  QRegularExpression transportPattern(
      QStringLiteral("property\\s+int\\s+transportHeight\\s*:\\s*(\\d+)"));
  const auto transport = transportPattern.match(qml);
  check(transport.hasMatch() && transport.captured(1).toInt() >= 44,
        "the transport strip is a 44 px touch row");
}

void arrangeTests() {
  using namespace mnky::app::arrange;
  check(transportTick(10, 5) == 15, "song seek offset adds to the link tick");
  check(transportTick(10, -20) == 0,
        "song seek offset never wraps the transport tick below zero");
  check(std::abs(gridStepBeats(kGridBar) - 4.0) < 1e-9,
        "arrange grid bar = 4 beats");
  check(std::abs(gridStepBeats(kGridHalf) - 2.0) < 1e-9,
        "arrange grid half = 2 beats");
  check(std::abs(gridStepBeats(kGridQuarter) - 1.0) < 1e-9,
        "arrange grid quarter = 1 beat");
  check(std::abs(gridStepBeats(kGridSixteenth) - 0.25) < 1e-9,
        "arrange grid sixteenth = 0.25 beat");
  check(std::abs(snapBeats(5.1, kGridBar) - 4.0) < 1e-9,
        "arrange snap rounds to the nearest bar");
  check(std::abs(snapLength(0.0, kGridQuarter) - 1.0) < 1e-9,
        "arrange length never collapses below one grid step");

  session::SongState song;
  song.sections.push_back(session::SongSection{"INTRO", 4});
  song.sections.push_back(session::SongSection{"VERSE", 8});
  const auto spans = sectionSpans(song.sections);
  check(spans.size() == 2 && std::abs(spans[0].startBeats) < 1e-9 &&
            std::abs(spans[0].endBeats - 16.0) < 1e-9,
        "arrange section spans are sequential bars");
  check(std::abs(spans[1].startBeats - 16.0) < 1e-9 &&
            std::abs(spans[1].endBeats - 48.0) < 1e-9,
        "arrange second section starts at the first section end");
  check(sectionAtBeat(song.sections, 17.0) == 1,
        "arrange section lookup finds the second section");
  check(std::abs(sectionsEndBeats(song.sections) - 48.0) < 1e-9,
        "arrange section end is bars * 4");

  const auto takeLength = [](const session::SongClip&) { return 16.0; };
  session::SongClip introClip;
  introClip.track = 1;
  introClip.startBeats = 0.0;
  introClip.take = "01-drums-001.wav";
  std::string error;
  check(addClip(song, introClip, takeLength, &error),
        "arrange adds a clip to an empty lane");
  check(!addClip(song, introClip, takeLength, &error),
        "arrange refuses an overlapping clip");
  session::SongClip verseClip = introClip;
  verseClip.startBeats = 32.0;
  check(addClip(song, verseClip, takeLength, &error),
        "arrange adds a non-overlapping clip on the same track");
  check(clipAtBeat(song.clips, 1, 10.0, takeLength) == 0,
        "arrange clip lookup uses the clip span");
  check(moveClip(song, 0, 4.0, takeLength, &error),
        "arrange moves a clip inside an empty region");
  check(!moveClip(song, 0, 20.0, takeLength, &error),
        "arrange refuses a move that would overlap");
  check(trimClipStart(song, 0, 12.0, takeLength, &error),
        "arrange trims a clip start");
  check(std::abs(song.clips[0].lengthBeats - 8.0) < 1e-9,
        "arrange trim start materializes the clip length");
  check(trimClipEnd(song, 0, 16.0, takeLength, &error),
        "arrange trims a clip end");
  check(std::abs(song.clips[0].lengthBeats - 4.0) < 1e-9,
        "arrange trim end updates the length");
  const int duplicate = duplicateClip(song, 0, kGridBar, takeLength, &error);
  check(duplicate >= 0, "arrange duplicates a clip into free space");

  session::SongState dupSong;
  dupSong.sections = song.sections;
  session::SongClip sectionClip;
  sectionClip.track = 2;
  sectionClip.startBeats = 4.0;
  sectionClip.take = "02-bass-001.wav";
  session::SongClip laterClip;
  laterClip.track = 3;
  laterClip.startBeats = 20.0;
  laterClip.take = "03-keys-001.wav";
  check(addClip(dupSong, sectionClip, takeLength, &error), "arrange section copy source placed");
  check(addClip(dupSong, laterClip, takeLength, &error), "arrange later clip placed");
  const int dupIndex = duplicateSection(dupSong, 0, takeLength);
  check(dupIndex == 1 && dupSong.sections.size() == 3,
        "arrange duplicate section inserts after the source");
  check(dupSong.sections[1].name == "INTRO" && dupSong.sections[1].bars == 4,
        "arrange duplicate section copies name and bars");
  check(dupSong.clips.size() == 3, "arrange duplicate section copies its clips");
  check(std::abs(dupSong.clips[1].startBeats - 36.0) < 1e-9,
        "arrange duplicate section shifts later clips right");
  check(std::abs(dupSong.clips[2].startBeats - 20.0) < 1e-9,
        "arrange duplicate section shifts the copy by the section length");

  session::SongState lengthSong;
  session::SongClip lengthClip;
  lengthClip.track = 0;
  lengthClip.take = "01-drums-001.wav";
  lengthClip.lengthBeats = 32.0;
  const auto longTake = [](const session::SongClip&) { return 64.0; };
  check(addClip(lengthSong, lengthClip, longTake, &error),
        "arrange length fixture placed");
  check(std::abs(effectiveLengthBeats(lengthSong, 0.0, longTake) - 32.0) < 1e-9,
        "arrange length 0 derives from the last clip end");
  check(std::abs(effectiveLengthBeats(lengthSong, 32.0, longTake) - 32.0) < 1e-9,
        "arrange explicit length wins");
  check(std::abs(loopEndBeats(0.0, 0.0, 32.0) - 32.0) < 1e-9,
        "arrange loop end 0 follows the song length");
  check(loopIsWholeSong(0.0, 0.0, 32.0),
        "arrange empty loop window is the whole song");
  check(!loopIsWholeSong(8.0, 16.0, 32.0),
        "arrange section loop is not the whole song");
  check(lengthText(32.0) == "8:0", "arrange length text is bars:beats");
  check(barOfBeat(punchStartBeats(9, 0.0)) == 9,
        "arrange punch bar 9 maps to song bar 9");
  check(barOfBeat(punchStartBeats(3, 16.0)) == 7,
        "arrange punch markers are loop-relative");
  check(deleteClip(song, 0), "arrange deletes a clip");
  check(!deleteClip(song, 99), "arrange refuses an out-of-range delete");
}

int runAll() {
  appClapActivationAudioTests();
  newTrackFromTakeTests();
  logicTests();
  textBufferTests();
  projectTests();
  audioBackendTests();
  routingTests();
  mixerTests();
  valueFormatTests();
  learnTests();
  wavTests();
  takeAdoptionTests();
  takeEditViewModelTests();
  sessionClickTests();
  takeNameTests();
  meterAndSpaceTests();
  learnCaptureTests();
  learnTargetCaptureTests();
  projectRoundTripTests();
  songPersistenceTests();
  loopAndIoTests();
  eqAndXyTests();
  trackFxRackTests();
  clapTrackHostTests();
  clapSlotAudioTests();
  trackFxSeriesMathTests();
  builtinTrackFxRackAudioTests();
  fxSlotZeroClapStateTests();
  trackFxKindSwitchTests();
  outputInsertChainTests();
  sendChainTests();
  midiMonitorTests();
  clockGateTests();
  deviceOutTests();
  mixerInputSourceTests();
  dynamicMixerTests();
  dynamicMixerViewModelTests();
  capacityTests();
  configLiveTests();
  systemDeviceRowTests();
  timelineAndPipeTests();
  generatorRouteTests();
  pipeLinkTests();
  eqForwardingTests();
  noPicoModeTests();
  trackModeTests();
  styleTests();
  arrangeTests();
  std::printf("%s mnkybox app logic: %d checks, %d failures\n",
              gFailures == 0 ? "PASS" : "FAIL", gChecks, gFailures);
  return gFailures == 0 ? 0 : 1;
}

}  // namespace

int runLogicUnitTests() {
  return runAll();
}

}  // namespace mnky::app
