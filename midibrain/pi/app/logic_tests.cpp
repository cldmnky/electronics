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
#include <map>
#include <string>
#include <system_error>
#include <vector>

#include "../../session/session.hpp"
#include "app_context.hpp"
#include "app_state.hpp"
#include "audio/engine.hpp"
#include "audio_backend.hpp"
#include "audio_glue.hpp"
#include "learn_view_model.hpp"
#include "link_client.hpp"
#include "logic.hpp"
#include "midi/learn.hpp"
#include "midi_glue.hpp"
#include "midi_monitor_view_model.hpp"
#include "mixer_model.hpp"
#include "mixer_view_model.hpp"
#include "project_io.hpp"
#include "project_logic.hpp"
#include "recorder_view_model.hpp"
#include "routing_model.hpp"
#include "session_settings.hpp"
#include "session_view_model.hpp"
#include "text_buffer.hpp"
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
}

void mixerTests() {
  MixerModel model;
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
  MidiGlue midi(false);
  AudioGlue audio(mixer, fx, state, true);
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
  check(learn.bindings().value(0).contains(QStringLiteral("mixer.ch01.gain")),
        "captured CC binds the selected target");
  learn.removeBinding(0);
  check(learn.bindings().isEmpty(), "learn binding removal");
}

void projectRoundTripTests() {
  AppState state;
  SessionSettings settings;
  MixerModel mixer;
  FxManager fx;
  MidiGlue midi(false);
  AudioGlue audio(mixer, fx, state, true);
  LinkClient link(midi, state, true);
  AppContext context{state, mixer, fx, midi, audio, link, settings};

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
  settings.clock.trs[0].clock = true;
  settings.clock.trs[0].transport = false;
  settings.clock.trs[1].clock = false;
  settings.clock.trs[1].transport = true;
  settings.clock.usb.clock = true;
  settings.clock.usb.transport = false;
  mixer.setInCh(2, 4);
  mixer.setInPair(2, true);
  mixer.setOutDst(2, 1);
  mixer.setInCh(3, -1);
  mixer.setOutDst(3, 3);
  midi.setRoute(midi::Port::TrsIn1, midi::Port::UsbOut, true);
  check(midi.addLearnBinding(74, 2, "mixer.ch03.gain", 0),
        "project test learn binding");

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
  settings.reset();
  midi.setRoute(midi::Port::TrsIn1, midi::Port::UsbOut, false);
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
  check(mixer.monitor(2) == 0 && mixer.monitor(3) == 2,
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
  check(mixer.inCh(2) == 4 && mixer.inPair(2) && mixer.outDst(2) == 1,
        "project load restores the per-track input source and output destination");
  check(mixer.inCh(3) == -1 && mixer.outDst(3) == 3,
        "project load restores a disabled input source and the NONE destination");
  check(midi.routeEnabled(midi::Port::TrsIn1, midi::Port::UsbOut),
        "project load restores the routing matrix");
  check(midi.learnBindings().size() == 1 &&
            midi.learnBindings().front().target == "mixer.ch03.gain",
        "project load restores the MIDI-learn map");

  std::error_code removeError;
  std::filesystem::remove_all(directory, removeError);
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
            clampInCh(99) == kNumStrips - 1,
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
            colours.count("text") == 1 && colours.count("textDim") == 1 &&
            colours.count("accent") == 1,
        "Style.qml defines the measured colour tokens");

  const auto background = colours["background"];
  const auto panel = colours["panel"];
  const auto text = colours["text"];
  const auto textDim = colours["textDim"];
  const auto accent = colours["accent"];
  check(luminance(background) <= 0.03, "surfaces stay near-black (luminance <= 0.03)");
  check(luminance(panel) <= 0.03, "panel surface stays near-black");
  check(contrast(text, background) >= 7.0, "primary text contrast >= 7:1");
  check(contrast(textDim, background) >= 4.5, "dim text contrast >= 4.5:1");
  check(contrast(accent, background) >= 4.5, "accent contrast >= 4.5:1");
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
  projectRoundTripTests();
  loopAndIoTests();
  midiMonitorTests();
  clockGateTests();
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
