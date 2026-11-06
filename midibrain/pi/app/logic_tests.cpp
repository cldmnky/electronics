// Non-visual checks for the Qt app. Run by `mnkybox --unittests` and CTest
// `mnkybox_app_logic`; no window, audio device, MIDI port or QML engine needed.
// The QML Style singleton is read from the compiled resource so its contrast
// floors stay a measured property of the shipped theme.
#include <QColor>
#include <QFile>
#include <QRegularExpression>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "../../session/session.hpp"
#include "app_context.hpp"
#include "app_state.hpp"
#include "audio/engine.hpp"
#include "audio_backend.hpp"
#include "audio_glue.hpp"
#include "config_popup_model.hpp"
#include "eq_model.hpp"
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
#include "wav_info.hpp"

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
  check(inputs.size() == 5 && inputs[0].port == 0 && inputs[1].port == 1 &&
            inputs[2].port == 2,
        "routing input nodes keep the three fixed ports first");
  check(inputs[3].port == 6 && inputs[3].name == QStringLiteral("Studio Keys") &&
            inputs[3].connected,
        "a connected per-device USB input becomes its own node");
  check(inputs[4].port == 7 && !inputs[4].connected &&
            inputs[4].dynamic,
        "a dormant per-device reservation stays visible");
  check(outputs.size() == 5 && outputs[3].port == 6 && outputs[4].port == 8,
        "per-device output nodes use the same stable registry id");
  check(inputs[3].input && outputs[3].dynamic,
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
  ConfigPopupModel popup(state, mixer, settings, eq, performance);
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
  // MPC parity monitor: 0 PLAY, 1 AUTO, 2 BOTH, 3 MON. The engine enum binds
  // 3 to audio::MonitorMode::InputOnly.
  model.setMonitor(3, 3);
  check(model.monitor(3) == 3, "monitor-only mode is stored unclamped");
  check(model.channel(3).monitor == audio::MonitorMode::InputOnly,
        "monitor-only mode reaches the engine snapshot as InputOnly");
  model.setMonitor(3, 9);
  check(model.monitor(3) == 3, "monitor mode clamps to 0..3");
  model.setMonitor(3, 1);
  check(static_cast<int>(model.channel(3).monitor) == 1,
        "auto monitor mode stays Auto");
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
  model.setMonitor(3, 2);
  model.applyTo(engine);
  check(engine.channel(3).monitor == audio::MonitorMode::On,
        "mixer snapshot carries per-track monitor mode");
  model.setMonitor(3, 0);
  model.applyTo(engine);
  check(engine.channel(3).monitor == audio::MonitorMode::Off,
        "per-track monitor mode off reaches the engine");
  check((model.armMask() & (1u << 3)) != 0, "arm mask carries the armed strip");
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

  MixerModel::Insert insert;
  insert.id = "builtin.delay";
  insert.bypass = true;
  insert.params = {0.4, 0.25};
  model.setInsert(2, insert);
  const auto stored = model.insert(2);
  check(stored.id == "builtin.delay" && stored.bypass && stored.params.size() == 2,
        "the per-track FX insert holds id, bypass and params");

  // Legacy sessions: a default session::MixerState carries all 18 slots in
  // order, so the app shows the legacy 18-strip mixer for a file without a
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

  // A full 64-track list: the page count follows the list, four per page.
  MixerModel full;
  for (int slot = 0; slot < audio::kMaxTracks; ++slot) {
    check(full.addTrack(slot, ""), "every capacity slot can be added");
  }
  SessionSettings settings;
  EqModel eq;
  MixerViewModel view(full, settings, eq);
  check(view.stripCount() == audio::kMaxTracks, "the mixer shows every track");
  check(view.pageCount() == (audio::kMaxTracks + kStripsPerBank - 1) / kStripsPerBank,
        "the page count is ceil(tracks / 4), never a constant");
  view.setBank(view.pageCount() - 1);
  check(view.bank() == view.pageCount() - 1, "the last page is reachable");
  view.setBank(view.pageCount());
  check(view.bank() == view.pageCount() - 1, "the bank clamps at the last page");
}

// The QML model over the dynamic list: rows carry slots (not positions), the
// bank pages four strips at a time, the selectors are absolute list writes and
// the channel popout reads the same slot the strip was opened on.
void dynamicMixerViewModelTests() {
  SessionSettings settings;
  MixerModel model;
  EqModel eq;
  MixerViewModel view(model, settings, eq);

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
  check(view.bankText() == "TRACKS 1-3 / 3", "the page readout counts the tracks");

  check(view.addTrack(3, "gtr", 3, false, 0), "the view adds slot 3");
  check(view.addTrack(4, "vox", 4, false, 0), "the view adds slot 4");
  check(view.pageCount() == 2 && view.bankText() == "TRACKS 1-4 / 5",
        "four strips fit one page and the fifth opens a second page");
  view.stepBank(1);
  check(view.bank() == 1 && view.bankText() == "TRACKS 5-5 / 5",
        "stepping the bank reaches the second page");
  view.stepBank(1);
  check(view.bank() == 1, "the bank cannot step past the last page");
  view.stepBank(-1);

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

  view.setInsertChoiceAt(2, 1);
  check(model.insert(2).id == "builtin.delay", "the insert selector writes the id");
  check(view.trackInsertNameAt(2) == "DELAY", "the insert reads back by name");
  view.setInsertBypassAt(2, true);
  check(model.insert(2).bypass, "the insert bypass is stored per track");
  view.setInsertChoiceAt(2, 0);
  check(model.insert(2).id.empty() && view.trackInsertNameAt(2) == "NONE",
        "NONE clears the insert");

  view.openChannel(2);
  check(view.channelOpen() && view.channelSlot() == 2,
        "the channel popout opens on the strip's slot");
  check(view.channelEqSummary().contains("1 NONE"),
        "the popout summarises the channel's EQ bands");
  view.setChannelInsertBypass(true);
  check(model.insert(2).bypass, "the popout bypass writes the slot's insert");
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
  EqModel eq;
  PerformanceModel performance;
  ConfigPopupModel popup(state, mixer, settings, eq, performance);
  MixerViewModel view(mixer, settings, eq);
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

  // Selector popups: input, output, insert, delay division, reverb model.
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
  popup.setInsertTarget(0);
  check(popup.openKind(QStringLiteral("insert")), "the insert list opens");
  popup.setInsertIndex(2);
  check(mixer.insert(0).id == "builtin.reverb", "an insert pick is live");
  popup.cancel();
  check(mixer.insert(0).id.empty(), "CANCEL restores the previous insert");

  const int divisionBefore = static_cast<int>(std::lround(
      performance.param(PerformanceModel::kDelayGrid, audio::fx::Delay::kDivision)));
  check(popup.openKind(QStringLiteral("delaydivision")), "the delay division list opens");
  popup.setDelayDivision(1);
  check(static_cast<int>(std::lround(
            performance.param(PerformanceModel::kDelayGrid,
                              audio::fx::Delay::kDivision))) == 1,
        "a delay division pick is live");
  popup.cancel();
  check(static_cast<int>(std::lround(
            performance.param(PerformanceModel::kDelayGrid,
                              audio::fx::Delay::kDivision))) == divisionBefore,
        "CANCEL restores the previous delay division");
  check(popup.openKind(QStringLiteral("reverbmodel")), "the reverb model list opens");
  popup.setReverbModel(2);
  check(static_cast<int>(std::lround(
            performance.param(PerformanceModel::kReverbGrid,
                              audio::fx::Reverb::kModel))) == 2,
        "a reverb model pick is live");
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
  // A9 arm-then-touch: ARM CAPTURE with no target waits for a touch, and the
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
  mixer.setMonitor(2, 0);
  mixer.setMonitor(3, 2);
  mixer.setMonitor(4, 3);
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
  // Dynamic track list and one per-track insert: the ordered slots and their
  // names are session v2 mixer.tracks, the insert is mixer.channels[].insert.
  mixer.addTrack(2, "keys");
  mixer.addTrack(0, "drums");
  mixer.addTrack(4, "");
  MixerModel::Insert projectInsert;
  projectInsert.id = "builtin.delay";
  projectInsert.bypass = true;
  projectInsert.params = {0.4, 0.25};
  mixer.setInsert(2, projectInsert);
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
  mixer.setMonitor(2, 1);
  mixer.setMonitor(3, 1);
  mixer.setMonitor(4, 1);
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
  check(mixer.monitor(2) == 0 && mixer.monitor(3) == 2 && mixer.monitor(4) == 3,
        "project load restores per-track monitor modes");
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
  const auto loadedInsert = mixer.insert(2);
  check(loadedInsert.id == "builtin.delay" && loadedInsert.bypass &&
            loadedInsert.params.size() == 2,
        "project load restores the per-track FX insert");
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
  // implements all 16 types and no longer rejects the clocked ids; the missing
  // piece Pi-side is the 24-PPQN grid pass (MidiGlue::pump calls Router::tick,
  // not Router::clockTick), and the readout says exactly that.
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

int runAll() {
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
  sessionClickTests();
  takeNameTests();
  meterAndSpaceTests();
  learnCaptureTests();
  learnTargetCaptureTests();
  projectRoundTripTests();
  loopAndIoTests();
  eqAndXyTests();
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
  pipeLinkTests();
  eqForwardingTests();
  noPicoModeTests();
  styleTests();
  std::printf("%s mnkybox app logic: %d checks, %d failures\n",
              gFailures == 0 ? "PASS" : "FAIL", gChecks, gFailures);
  return gFailures == 0 ? 0 : 1;
}

}  // namespace

int runLogicUnitTests() {
  return runAll();
}

}  // namespace mnky::app
