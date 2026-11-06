// Assert-style acceptance tests for mnky::session (no external framework).
// Build: clang++ -std=c++20 -Wall -Wextra -Werror -I. -I.. test_session.cpp
//        session.cpp ../audio/engine.cpp ../midi/router.cpp -o /tmp/t_session
//
// The real-file tests run in a fresh mkdtemp() directory. A failing check
// prints FAIL and the process exits non-zero, so `test_session` itself is the
// gate.
#include <array>
#include <cstdint>
#include <utility>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

// mkdtemp() is POSIX: this macOS SDK declares it in <unistd.h>, glibc in
// <stdlib.h>. Including both keeps the global prototype visible on both.
#include <stdlib.h>
#include <unistd.h>

#include "session.hpp"

namespace {

using mnky::audio::Channel;
using mnky::audio::Engine;
using mnky::midi::Port;
using mnky::midi::Router;
using mnky::session::FxSlotTarget;
using mnky::session::LoadResult;
using mnky::session::MidiPipe;
using mnky::session::MidiPipeType;
using mnky::session::RouteState;
using mnky::session::Snapshot;

int gFailures = 0;

#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
      ++gFailures;                                                      \
    }                                                                   \
  } while (0)

void pass(const char* name) { std::printf("PASS %s\n", name); }

// mkdtemp() gives a private, collision-free directory; it is removed when the
// test object leaves scope (a failed run leaves nothing behind either).
class TempDir {
 public:
  TempDir() {
    char pattern[] = "/tmp/mnkybox-session-test.XXXXXX";
    char* made = ::mkdtemp(pattern);
    if (made != nullptr) path_ = made;
  }

  ~TempDir() {
    if (path_.empty()) return;
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;

  bool ok() const { return !path_.empty(); }

  std::string file(const std::string& name) const {
    return (std::filesystem::path(path_) / name).string();
  }

 private:
  std::string path_;
};

bool writeFile(const std::string& path, const std::string& text) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out.write(text.data(), static_cast<std::streamsize>(text.size()));
  out.flush();
  return out.good();
}

std::string readFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return {};
  return std::string(std::istreambuf_iterator<char>(in),
                     std::istreambuf_iterator<char>());
}

// ---------------------------------------------------------------- round trip

void testRoundTrip() {
  Snapshot original;
  original.router.routes = {
      {static_cast<int>(Port::TrsIn1), static_cast<int>(Port::UsbOut), true, {}},
      {static_cast<int>(Port::UsbIn), static_cast<int>(Port::TrsOut2), true, {}},
      {static_cast<int>(Port::TrsIn2), static_cast<int>(Port::TrsOut1), false, {}}};
  original.router.channels[0] = 0x0003;
  original.router.channels[2] = 0xFFFF;
  original.router.channels[5] = 0x00F0;
  original.router.transpose[0] = -12;
  original.router.transpose[2] = 7;

  // The last strip index comes from the capacity constant, never a literal.
  const auto lastStrip = static_cast<std::size_t>(mnky::audio::kMaxInputs - 1);
  original.mixer.channels[0].gain = 0.125f;
  original.mixer.channels[0].pan = 0.0f;
  original.mixer.channels[0].mute = true;
  original.mixer.channels[0].send = {0.25f, 0.75f};
  original.mixer.channels[lastStrip].gain = 1.5f;
  original.mixer.channels[lastStrip].pan = 1.0f;
  original.mixer.channels[lastStrip].solo = true;
  original.mixer.channels[lastStrip].arm = true;
  original.mixer.channels[lastStrip].send = {1.0f, 0.0f};
  original.mixer.master = 0.8f;

  original.fx.bus[0].id = "com.mnkygain";
  original.fx.bus[0].params = {0.1, -2.5, 3.0, 1e-9};
  original.fx.bus[0].bypass = true;
  original.fx.bus[1].id = "builtin.delay \"slap\"\\tape\ncaf" "\xC3\xA9";
  original.fx.bus[1].params = {0.0, 1000.5};

  original.transport.bpmX100 = 13333;
  original.fx.eq.tracks[2][1].type = 3;
  original.fx.eq.tracks[2][1].gainDb = -3.0f;
  original.fx.eq.tracks[2][1].freqHz = 800.0f;
  original.fx.eq.tracks[2][1].q = 2.0f;
  original.fx.eq.main[0].type = 1;
  original.fx.eq.main[0].freqHz = 40.0f;
  original.fx.xy.delayX = 0.25f;
  original.fx.xy.delayY = 0.75f;
  original.fx.xy.reverbX = 0.8f;
  original.fx.xy.reverbY = 0.2f;
  original.click.mode = 2;
  original.click.output = 1;
  original.click.gainDb = -6.0f;
  original.click.bars = 4;
  original.midimap = {
      {74, 1, "mixer.ch3.gain", 3},
      {7, 16, "fx.0.mix", 0},
      {0, 1, "future.unknown.param", 17}};
  original.isTemplate = true;
  original.takes.active[1] = "02-drums-003.wav";
  original.takes.loop[1].enabled = true;
  original.takes.loop[1].startBeats = 8.0;
  original.takes.loop[1].endBeats = 16.0;
  mnky::session::TakeRegion region;
  region.take = "02-drums-003.wav";
  region.offsetBeats = 4.0;
  region.lengthBeats = 8.0;
  region.gainDb = -3.0f;
  region.fadeInBeats = 0.5;
  region.fadeOutBeats = 1.0;
  region.normalize = true;
  original.takes.edit[1].push_back(region);
  original.punch.playFrom.enabled = true;
  original.punch.playFrom.measure = 5;
  original.punch.in.enabled = true;
  original.punch.in.measure = 9;
  original.punch.out.enabled = true;
  original.punch.out.measure = 13;
  original.cue.phonesSrc = 1;
  original.cue.style = 1;
  original.sends.cue = 0;
  original.sends.rec = 0;
  original.mainComp.on = true;
  original.mainComp.ratio = 4.0f;
  original.mainComp.mode = 1;
  original.mixer.channels[2].cueSend = 0.75f;
  original.mixer.channels[2].out34Send = 0.5f;
  original.mixer.channels[2].monitor = 2;
  original.mixer.channels[3].monitor = 0;
  original.mixer.channels[4].monitor = 3;
  original.mixer.channels[2].inCh = 5;
  original.mixer.channels[2].inPair = true;
  original.mixer.channels[2].outDst = 1;
  original.mixer.out34Mode = 1;
  original.clock.trs[0].clock = false;
  original.clock.trs[1].transport = false;
  original.clock.usb.clock = false;
  original.clock.source = 1;
  original.clock.transportFollow[0] = true;
  original.clock.transportFollow[2] = true;
  original.clock.trs[0].tempoDiv = 2;
  original.clock.trs[0].syncDelayMs = 12;
  original.deviceOut.main = {4, 5};
  original.deviceOut.out34 = {6, 7};
  original.deviceOut.phones = {0, 1};
  original.router.devices = {{6, "usb:Arturia KeyStep 32"},
                              {7, "usb:Elektron Digitakt"}};
  original.router.channels[6] = 0x000F;
  original.router.transpose[7] = -2;
  RouteState usbRoute;
  usbRoute.src = 6;
  usbRoute.dst = 7;
  usbRoute.on = true;
  original.router.routes.push_back(std::move(usbRoute));
  RouteState split;
  split.src = 2;
  split.dst = 3;
  split.on = true;
  MidiPipe range;
  range.type = MidiPipeType::NoteRange;
  range.params = {36, 72, 1, 127};
  split.pipes.push_back(range);
  MidiPipe chance;
  chance.type = MidiPipeType::Chance;
  chance.bypass = true;
  chance.chMask = 0x000F;  // channels 1-4 only
  chance.params = {25};
  split.pipes.push_back(chance);
  original.router.routes.push_back(std::move(split));

  TempDir dir;
  CHECK(dir.ok());
  if (!dir.ok()) return;
  const std::string path = dir.file("session.json");
  CHECK(mnky::session::save(path, original));

  const std::string text = readFile(path);
  CHECK(!text.empty());
  CHECK(text.find("\"app\":\"mnkybox\"") != std::string::npos);
  CHECK(text.find("\"session\":2") != std::string::npos);
  CHECK(text.find("\"template\":true") != std::string::npos);
  CHECK(text.find("\"routes\":[") != std::string::npos);
  CHECK(text.find("\"channels\":{\"0\":") != std::string::npos);
  CHECK(text.find("\"transpose\":{\"0\":") != std::string::npos);
  CHECK(text.find("\"master\":") != std::string::npos);
  CHECK(text.find("\"bpm_x100\":13333") != std::string::npos);
  CHECK(text.find("\"click\":{\"mode\":2,\"output\":1") != std::string::npos);
  CHECK(text.find("\"takes\":{\"active\":{\"1\":\"02-drums-003.wav\"},\"loop\":{"
                "\"1\":{\"enabled\":true,\"start\":8,\"end\":16}},\"edit\":{"
                "\"1\":[{\"take\":\"02-drums-003.wav\",\"offset\":4,\"length\":8,"
                "\"gainDb\":-3,\"fadeIn\":0.5,\"fadeOut\":1,\"normalize\":true}]}}") !=
        std::string::npos);
  CHECK(text.find("\"punch\":{\"playFrom\":{\"enabled\":true,\"measure\":5}") !=
        std::string::npos);
  CHECK(text.find("\"cue\":{\"phonesSrc\":1,\"style\":1}") != std::string::npos);
  CHECK(text.find("\"sends\":{\"cue\":0") != std::string::npos);
  CHECK(text.find("\"mainComp\":{\"on\":true") != std::string::npos);
  CHECK(text.find("\"cue\":0.75") != std::string::npos);
  CHECK(text.find("\"out34mode\":1") != std::string::npos);
  CHECK(text.find("\"midimap\":[") != std::string::npos);
  CHECK(text.find("\"target\":\"mixer.ch3.gain\"") != std::string::npos);
  CHECK(text.find("\\\"slap\\\"") != std::string::npos);
  // A snapshot that never touched the track list saves the legacy layout: all
  // 18 slots in order, and every channel carries an empty (none) insert.
  CHECK(text.find("\"tracks\":[{\"slot\":0,\"name\":\"\"},"
                  "{\"slot\":1,\"name\":\"\"}") != std::string::npos);
  CHECK(text.find("\"insert\":{\"id\":\"\",\"bypass\":false,"
                  "\"params\":[]}") != std::string::npos);

  const LoadResult loaded = mnky::session::load(path);
  if (!loaded.ok) std::printf("FAIL load: %s\n", loaded.error.c_str());
  CHECK(loaded.ok);
  CHECK(loaded.snapshot == original);
}

// ------------------------------------------------------------ version reject

void testVersionOneLoads() {
  // A v1 file (legacy click enabled/level, no v2 sections) loads with the
  // click mapped onto v2 fields and v2 sections at defaults.
  TempDir dir;
  CHECK(dir.ok());
  if (!dir.ok()) return;
  const std::string path = dir.file("v1.json");
  CHECK(writeFile(path, R"({"app":"mnkybox","session":1,"click":{"enabled":true,"level":0.65,"bars":4},"mixer":{"channels":[{"cue":0.5}],"master":0.8}})"));
  LoadResult result = mnky::session::load(path);
  if (!result.ok) std::printf("FAIL v1 load: %s\n", result.error.c_str());
  CHECK(result.ok);
  CHECK(result.snapshot.click.mode == 2);
  CHECK(result.snapshot.click.output == 0);
  CHECK(result.snapshot.click.gainDb < 0.0f);
  CHECK(result.snapshot.click.bars == 4);
  CHECK(result.snapshot.mixer.channels[0].cueSend == 0.5f);
  CHECK(!result.snapshot.isTemplate);
  CHECK(result.snapshot.punch.in.measure == 1);

  CHECK(writeFile(path, R"({"app":"mnkybox","session":1,"click":{"enabled":false,"level":0.0}})"));
  result = mnky::session::load(path);
  CHECK(result.ok);
  CHECK(result.snapshot.click.mode == 0);
}

void testVersionReject() {
  TempDir dir;
  CHECK(dir.ok());
  if (!dir.ok()) return;
  const std::string path = dir.file("version.json");

  CHECK(writeFile(path, R"({"app":"mnkybox","session":3})"));
  LoadResult result = mnky::session::load(path);
  CHECK(!result.ok);
  CHECK(result.error.find("version") != std::string::npos);

  CHECK(writeFile(path, R"({"app":"mnkybox","session":0})"));
  result = mnky::session::load(path);
  CHECK(!result.ok);
  CHECK(result.error.find("version") != std::string::npos);

  CHECK(writeFile(path, R"({"app":"mnkybox"})"));
  result = mnky::session::load(path);
  CHECK(!result.ok);
  CHECK(result.error.find("session") != std::string::npos);
}

// ------------------------------------------------------------- corrupt reject

void testCorruptReject() {
  const char* corrupt[] = {
      "",
      "not json",
      R"({"app":"mnkybox","session":1)",
      R"({"app":"mnkybox","session":1} trailing)",
      R"([{"app":"mnkybox","session":1}])",
      R"({"session":1})",
      R"({"app":7,"session":1})",
      R"({"app":"other","session":1})",
      R"({"app":"mnkybox","session":"one"})",
      R"({"app":"mnkybox","session":1,"router":{"routes":{}}})",
      R"({"app":"mnkybox","session":1,"router":{"routes":[{"src":0,"dst":5,"on":"yes"}]}})",
      R"({"app":"mnkybox","session":1,"mixer":{"channels":[{"gain":"loud"}]}})",
      R"({"app":"mnkybox","session":1,"mixer":{"channels":[{"send":"wet"}]}})",
      R"({"app":"mnkybox","session":1,"fx":{"bus":[{"id":7}]}})",
      R"({"app":"mnkybox","session":1,"fx":{"bus":[{"params":[1,"two"]}]}})",
      R"({"app":"mnkybox","session":1,"transport":{"bpm_x100":"fast"}})",
      R"({"app":"mnkybox","session":1,"click":[]})",
      R"({"app":"mnkybox","session":1,"click":{"enabled":"yes"}})",
      R"({"app":"mnkybox","session":1,"click":{"level":"loud"}})",
      R"({"app":"mnkybox","session":1,"midimap":{}})",
      R"({"app":"mnkybox","session":1,"midimap":[1]})",
      R"({"app":"mnkybox","session":1,"midimap":[{"cc":1,"ch":1,"target":"mixer.ch1.gain"}]})",
      R"({"app":"mnkybox","session":1,"midimap":[{"cc":128,"ch":1,"target":"mixer.ch1.gain","track":0}]})",
      R"({"app":"mnkybox","session":1,"midimap":[{"cc":1,"ch":17,"target":"mixer.ch1.gain","track":0}]})",
      R"({"app":"mnkybox","session":1,"midimap":[{"cc":1,"ch":1,"target":7,"track":0}]})",
      "{\"app\":\"mnkybox\",\"session\":1,\"fx\":{\"bus\":[{\"id\":\"bad\\q\"}]}}",
      R"({"app":"mnkybox","session":1,"mixer":{"master":1.5.2}})",
      R"({"app":"mnkybox","session":1,"router":{"routes":[1]}})",
  };

  TempDir dir;
  CHECK(dir.ok());
  if (!dir.ok()) return;
  const std::string path = dir.file("corrupt.json");
  for (const char* text : corrupt) {
    CHECK(writeFile(path, text));
    const LoadResult result = mnky::session::load(path);
    if (result.ok) std::printf("FAIL corrupt accepted: %s\n", text);
    CHECK(!result.ok);
    CHECK(!result.error.empty());
  }

  const LoadResult missing = mnky::session::load(dir.file("does-not-exist.json"));
  CHECK(!missing.ok);
  CHECK(missing.error.find("cannot open") != std::string::npos);
}

// -------------------------------------------------------- missing fields

void testMissingFields() {
  TempDir dir;
  CHECK(dir.ok());
  if (!dir.ok()) return;
  const std::string path = dir.file("minimal.json");

  CHECK(writeFile(path, R"({"app":"mnkybox","session":1})"));
  LoadResult result = mnky::session::load(path);
  CHECK(result.ok);
  const Snapshot defaults;
  CHECK(result.snapshot == defaults);
  CHECK(!result.snapshot.click.enabled);
  CHECK(result.snapshot.click.level == 0.5f);
  CHECK(result.snapshot.click.bars == 1);
  CHECK(result.snapshot.midimap.empty());

  CHECK(writeFile(
      path,
      R"({"app":"mnkybox","session":1,"mixer":{"master":0.5},"transport":{}})"));
  result = mnky::session::load(path);
  CHECK(result.ok);
  Snapshot expected;
  expected.mixer.master = 0.5f;
  CHECK(result.snapshot == expected);

  CHECK(writeFile(path,
                  R"({"app":"mnkybox","session":1,"mixer":{"channels":[{},{"gain":0.25}]}})"));
  result = mnky::session::load(path);
  CHECK(result.ok);
  CHECK(result.snapshot.mixer.channels[0].gain == 1.0f);
  CHECK(result.snapshot.mixer.channels[1].gain == 0.25f);
  CHECK(result.snapshot.mixer.channels[1].pan == 0.5f);
  CHECK(result.snapshot.mixer.channels[static_cast<std::size_t>(
            mnky::audio::kMaxInputs - 1)]
            .gain == 1.0f);

  CHECK(writeFile(path,
                  R"({"app":"mnkybox","session":1,"click":{"enabled":true}})"));
  result = mnky::session::load(path);
  CHECK(result.ok);
  CHECK(result.snapshot.click.enabled);
  CHECK(result.snapshot.click.level == 0.5f);
  CHECK(result.snapshot.click.bars == 1);

  CHECK(writeFile(
      path,
      R"({"app":"mnkybox","session":1,"click":{"enabled":true,"level":2.5,"bars":4}})"));
  result = mnky::session::load(path);
  CHECK(result.ok);
  CHECK(result.snapshot.click.level == 1.0f);
  CHECK(result.snapshot.click.bars == 4);
}

// -------------------------------------------------------- unknown fields

void testUnknownFields() {
  TempDir dir;
  CHECK(dir.ok());
  if (!dir.ok()) return;
  const std::string path = dir.file("unknown.json");

  CHECK(writeFile(path, R"({
    "app":"mnkybox",
    "session":1,
    "future":{"nested":[1,2,3]},
    "router":{
      "routes":[{"src":0,"dst":5,"on":true,"color":"red"}],
      "channels":{"0":3,"99":7},
      "transpose":{"2":-5},
      "extra":true
    },
    "mixer":{
      "channels":[{"gain":0.5,"name":"kick"},{"gain":0.25}],
      "master":0.9,
      "extra":[null]
    },
    "fx":{
      "bus":[{"id":"builtin.gain","params":[1,2],"preset":9},
             {"id":"builtin.caf\u00e9\uD83C\uDFB9","params":[]},
             {"id":"ignored"}],
      "extra":{}
    },
    "transport":{"bpm_x100":14000,"swing":0.1},
    "click":{"enabled":true,"level":0.75,"bars":2,"accent":0.9},
    "midimap":[
      {"cc":74,"ch":1,"target":"mixer.ch3.gain","track":3,"label":"gain"},
      {"cc":7,"ch":16,"target":"future.unknown.param","track":0}
    ]
  })"));

  const LoadResult result = mnky::session::load(path);
  if (!result.ok) std::printf("FAIL load: %s\n", result.error.c_str());
  CHECK(result.ok);
  CHECK(result.snapshot.router.routes.size() == 1);
  CHECK(result.snapshot.router.routes[0].src == 0);
  CHECK(result.snapshot.router.routes[0].dst == 5);
  CHECK(result.snapshot.router.routes[0].on);
  CHECK(result.snapshot.router.channels[0] == 3);
  CHECK(result.snapshot.router.channels[1] == Router::kAllChannels);
  CHECK(result.snapshot.router.transpose[2] == -5);
  CHECK(result.snapshot.mixer.channels[0].gain == 0.5f);
  CHECK(result.snapshot.mixer.channels[1].gain == 0.25f);
  CHECK(result.snapshot.mixer.channels[2].gain == 1.0f);
  CHECK(result.snapshot.mixer.master == 0.9f);
  CHECK(result.snapshot.fx.bus[0].id == "builtin.gain");
  CHECK(result.snapshot.fx.bus[0].params.size() == 2);
  CHECK(result.snapshot.fx.bus[0].params[0] == 1.0);
  CHECK(result.snapshot.fx.bus[0].params[1] == 2.0);
  CHECK(result.snapshot.fx.bus[1].id ==
        "builtin.caf" "\xC3\xA9" "\xF0\x9F\x8E\xB9");
  CHECK(result.snapshot.fx.bus[1].params.empty());
  CHECK(result.snapshot.transport.bpmX100 == 14000);
}

// ------------------------------------------------------ apply / capture core

void testApplyAndCapture() {
  Router router;
  Engine engine;
  // Live state that the snapshot must replace, not merge into.
  CHECK(router.setRoute(Port::TrsIn1, Port::TrsOut1, true));
  router.setChannelMask(Port::TrsIn1, 0x0001);
  router.setTranspose(Port::TrsIn1, 3);
  Channel dirty;
  dirty.gain = 0.2f;
  dirty.mute = true;
  engine.setChannel(0, dirty);
  engine.setMaster(0.1f);

  Snapshot snapshot;
  snapshot.router.routes = {
      {static_cast<int>(Port::TrsIn1), static_cast<int>(Port::UsbOut), true, {}},
      {static_cast<int>(Port::UsbIn), static_cast<int>(Port::TrsOut2), true, {}}};
  snapshot.router.channels[0] = 0x0003;
  snapshot.router.channels[1] = Router::kAllChannels;
  snapshot.router.transpose[0] = -12;
  snapshot.mixer.channels[0].gain = 0.75f;
  snapshot.mixer.channels[0].pan = 0.25f;
  snapshot.mixer.channels[0].mute = true;
  snapshot.mixer.channels[0].arm = true;
  snapshot.mixer.channels[0].send = {0.3f, 0.6f};
  snapshot.mixer.channels[7].gain = 1.2f;
  snapshot.mixer.channels[7].solo = true;
  snapshot.mixer.master = 0.7f;

  mnky::session::apply(snapshot, router, engine);

  CHECK(!router.routeEnabled(Port::TrsIn1, Port::TrsOut1));
  CHECK(router.routeEnabled(Port::TrsIn1, Port::UsbOut));
  CHECK(router.routeEnabled(Port::UsbIn, Port::TrsOut2));
  CHECK(router.channelMask(Port::TrsIn1) == 0x0003);
  CHECK(router.channelMask(Port::TrsIn2) == Router::kAllChannels);
  CHECK(router.transpose(Port::TrsIn1) == -12);
  CHECK(router.transpose(Port::TrsIn2) == 0);

  const Channel channel0 = engine.channel(0);
  CHECK(channel0.gain == 0.75f);
  CHECK(channel0.pan == 0.25f);
  CHECK(channel0.mute);
  CHECK(!channel0.solo);
  CHECK(channel0.arm);
  CHECK(channel0.send[0] == 0.3f);
  CHECK(channel0.send[1] == 0.6f);
  const Channel channel7 = engine.channel(7);
  CHECK(channel7.gain == 1.2f);
  CHECK(channel7.solo);
  const Channel untouched = engine.channel(mnky::audio::kMaxInputs - 1);
  CHECK(untouched.gain == 1.0f);
  CHECK(untouched.pan == 0.5f);
  CHECK(engine.masterGain() == 0.7f);

  // Out-of-range getters return a default strip instead of reading past the end.
  CHECK(engine.channel(-1).gain == 1.0f);
  CHECK(engine.channel(mnky::audio::kMaxInputs).gain == 1.0f);

  const Snapshot captured = mnky::session::capture(router, engine);
  CHECK(captured == snapshot);
}

// --------------------------------------------------------------- FX slots

class FakeSlot final : public FxSlotTarget {
 public:
  std::string id;
  std::vector<double> params;
  bool bypass = false;
  bool loadOk = true;
  int loads = 0;
  int clears = 0;

  std::string effectId() const override { return id; }

  bool loadEffect(const std::string& requested) override {
    ++loads;
    if (!loadOk) return false;
    id = requested;
    params.assign(4, 0.0);  // the effect's stable parameter order
    return true;
  }

  void clearEffect() override {
    ++clears;
    id.clear();
    params.clear();
  }

  std::size_t paramCount() const override { return params.size(); }

  double paramValue(std::size_t index) const override {
    return index < params.size() ? params[index] : 0.0;
  }

  bool setParamValue(std::size_t index, double value) override {
    if (index >= params.size()) return false;
    params[index] = value;
    return true;
  }

  bool bypassed() const override { return bypass; }
  void setBypassed(bool value) override { bypass = value; }
};

void testFxSlots() {
  Router router;
  Engine engine;
  FakeSlot slotA;
  FakeSlot slotB;
  std::array<FxSlotTarget*, 2> slots{&slotA, &slotB};

  Snapshot snapshot;
  snapshot.fx.bus[0].id = "com.mnkygain";
  snapshot.fx.bus[0].params = {0.5, 1.0, 2.0, 3.0, 4.0};  // 5th is extra
  snapshot.fx.bus[0].bypass = true;
  snapshot.fx.bus[1].id = "builtin.echo";
  snapshot.fx.bus[1].params = {0.25};

  mnky::session::apply(snapshot, router, engine, slots);
  CHECK(slotA.id == "com.mnkygain");
  CHECK(slotA.params.size() == 4);
  CHECK(slotA.params[0] == 0.5);
  CHECK(slotA.params[1] == 1.0);
  CHECK(slotA.params[2] == 2.0);
  CHECK(slotA.params[3] == 3.0);
  CHECK(slotA.bypass);
  CHECK(slotB.id == "builtin.echo");
  CHECK(slotB.params.size() == 4);
  CHECK(slotB.params[0] == 0.25);
  CHECK(slotB.params[1] == 0.0);
  CHECK(!slotB.bypass);

  // capture() reads the slots back; the 5th saved param has no stable index,
  // so the canonical capture is the first four. FakeSlot always reports four
  // parameters, so the second bus also captures its full stable order.
  Snapshot canonical = snapshot;
  canonical.fx.bus[0].params.resize(4);
  canonical.fx.bus[1].params = {0.25, 0.0, 0.0, 0.0};
  const Snapshot captured = mnky::session::capture(router, engine, slots);
  CHECK(captured == canonical);

  // An unavailable effect leaves the slot untouched.
  const std::string previousId = slotA.id;
  const std::vector<double> previousParams = slotA.params;
  slotA.bypass = false;
  Snapshot unavailable;
  unavailable.fx.bus[0].id = "com.not-installed";
  unavailable.fx.bus[0].params = {9.0};
  unavailable.fx.bus[0].bypass = true;
  slotA.loadOk = false;
  mnky::session::apply(unavailable, router, engine, slots);
  CHECK(slotA.id == previousId);
  CHECK(slotA.params == previousParams);
  CHECK(!slotA.bypass);

  // An empty id clears the slot; bypass is not applied to an empty bus.
  slotA.loadOk = true;
  Snapshot emptyFx;
  mnky::session::apply(emptyFx, router, engine, slots);
  CHECK(slotA.id.empty());
  CHECK(slotA.params.empty());
  CHECK(slotA.clears == 1);
}

// ------------------------------------------------------------- file errors

void testFileErrors() {
  TempDir dir;
  CHECK(dir.ok());
  if (!dir.ok()) return;
  const Snapshot snapshot;
  CHECK(!mnky::session::save(dir.file("missing/session.json"), snapshot));
  CHECK(!mnky::session::save(dir.file(""), snapshot));
}

// --------------------------------------------------------- dynamic track list

void testTrackListRoundTrip() {
  TempDir dir;
  CHECK(dir.ok());
  if (!dir.ok()) return;
  const std::string path = dir.file("tracks.json");

  // Custom order + names: the list defines which slots exist and their order.
  // The last slot index comes from the capacity constant, never a literal.
  const int lastSlot = mnky::audio::kMaxInputs - 1;
  Snapshot original;
  original.mixer.tracks = {{2, "Kick"}, {0, "Snare top"}, {lastSlot, "Vox \xC3\xA9"}};
  original.mixer.channels[static_cast<std::size_t>(lastSlot)].gain = 0.25f;
  CHECK(mnky::session::save(path, original));
  const std::string text = readFile(path);
  CHECK(text.find("\"tracks\":[{\"slot\":2,\"name\":\"Kick\"},"
                  "{\"slot\":0,\"name\":\"Snare top\"},"
                  "{\"slot\":" + std::to_string(lastSlot) +
                  ",\"name\":\"Vox \xC3\xA9\"}]") != std::string::npos);
  LoadResult loaded = mnky::session::load(path);
  if (!loaded.ok) std::printf("FAIL tracks load: %s\n", loaded.error.c_str());
  CHECK(loaded.ok);
  CHECK(loaded.snapshot == original);
  CHECK(loaded.snapshot.mixer.tracks.size() == 3);
  CHECK(loaded.snapshot.mixer.tracks[0].slot == 2);
  // The list is visibility/order only: a slot keeps its channel settings.
  CHECK(loaded.snapshot.mixer.channels[static_cast<std::size_t>(lastSlot)].gain ==
        0.25f);

  // An empty list is a valid empty project, not the legacy default.
  Snapshot empty;
  empty.mixer.tracks.clear();
  CHECK(mnky::session::save(path, empty));
  CHECK(readFile(path).find("\"tracks\":[]") != std::string::npos);
  loaded = mnky::session::load(path);
  if (!loaded.ok) std::printf("FAIL empty tracks load: %s\n", loaded.error.c_str());
  CHECK(loaded.ok);
  CHECK(loaded.snapshot == empty);
  CHECK(loaded.snapshot.mixer.tracks.empty());

  // The default list (all slots) round-trips too.
  Snapshot legacy;
  CHECK(mnky::session::save(path, legacy));
  loaded = mnky::session::load(path);
  CHECK(loaded.ok);
  CHECK(loaded.snapshot == legacy);
  CHECK(loaded.snapshot.mixer.tracks.size() ==
        static_cast<std::size_t>(mnky::audio::kMaxInputs));
}

void testTrackListLegacyDefault() {
  TempDir dir;
  CHECK(dir.ok());
  if (!dir.ok()) return;
  const std::string path = dir.file("legacy-tracks.json");

  // A v1 and a v2 file without a "tracks" key load as all 18 slots in order.
  const char* const files[] = {
      R"({"app":"mnkybox","session":1,"mixer":{"master":0.5}})",
      R"({"app":"mnkybox","session":2,"mixer":{"master":0.5}})"};
  for (const char* text : files) {
    CHECK(writeFile(path, text));
    const LoadResult result = mnky::session::load(path);
    if (!result.ok) {
      std::printf("FAIL legacy tracks load: %s\n", result.error.c_str());
    }
    CHECK(result.ok);
    CHECK(result.snapshot.mixer.tracks.size() ==
          static_cast<std::size_t>(mnky::audio::kMaxInputs));
    for (int slot = 0; slot < mnky::audio::kMaxInputs; ++slot) {
      const auto index = static_cast<std::size_t>(slot);
      CHECK(result.snapshot.mixer.tracks[index].slot == slot);
      CHECK(result.snapshot.mixer.tracks[index].name.empty());
    }
    CHECK(result.snapshot.mixer.master == 0.5f);
  }
}

void testTrackListCorrupt() {
  TempDir dir;
  CHECK(dir.ok());
  if (!dir.ok()) return;
  const std::string path = dir.file("tracks-corrupt.json");

  struct Case {
    std::string text;
    std::string reason;
  };
  // The out-of-range slot is built from the capacity constant, and so is the
  // reason it must produce: no test here depends on the current value of
  // audio::kMaxInputs.
  const std::string outsideReason =
      ".slot\" is outside 0.." + std::to_string(mnky::audio::kMaxInputs - 1);
  const Case cases[] = {
      {R"({"app":"mnkybox","session":2,"mixer":{"tracks":{}}})",
       "mixer.tracks\" is not an array"},
      {R"({"app":"mnkybox","session":2,"mixer":{"tracks":[1]}})",
       "mixer.tracks[0]\" is not an object"},
      {R"({"app":"mnkybox","session":2,"mixer":{"tracks":[{"name":"kick"}]}})",
       "needs slot"},
      {R"({"app":"mnkybox","session":2,"mixer":{"tracks":[{"slot":"3"}]}})",
       ".slot\" is not an integer"},
      {R"({"app":"mnkybox","session":2,"mixer":{"tracks":[{"slot":1.5}]}})",
       ".slot\" is not an integer"},
      {"{\"app\":\"mnkybox\",\"session\":2,\"mixer\":{\"tracks\":"
       "[{\"slot\":" +
           std::to_string(mnky::audio::kMaxInputs) + "}]}}",
       outsideReason},
      {R"({"app":"mnkybox","session":2,"mixer":{"tracks":[{"slot":-1}]}})",
       outsideReason},
      {R"({"app":"mnkybox","session":2,"mixer":{"tracks":[{"slot":0,"name":7}]}})",
       ".name\" is not a string"},
      // A repeated slot is corrupt: the file must not silently drop a track.
      {R"({"app":"mnkybox","session":2,"mixer":{"tracks":[{"slot":4,"name":"first"},{"slot":4,"name":"second"}]}})",
       ".slot\" repeats slot 4"},
  };
  for (const Case& testCase : cases) {
    CHECK(writeFile(path, testCase.text));
    const LoadResult result = mnky::session::load(path);
    if (result.ok) {
      std::printf("FAIL corrupt tracks accepted: %s\n", testCase.text.c_str());
    }
    CHECK(!result.ok);
    CHECK(result.error.find(testCase.reason) != std::string::npos);
  }

  // Names are clamped to kMaxTrackNameBytes without splitting a UTF-8
  // sequence: 63 'a' + a 2-byte e-acute + "tail" cuts back to the 63 'a's.
  std::string utf8Name(63, 'a');
  utf8Name += "\xC3\xA9";
  utf8Name += "tail";
  CHECK(writeFile(path, "{\"app\":\"mnkybox\",\"session\":2,\"mixer\":"
                        "{\"tracks\":[{\"slot\":0,\"name\":\"" +
                            utf8Name + "\"}]}}"));
  const LoadResult clampedUtf8 = mnky::session::load(path);
  CHECK(clampedUtf8.ok);
  CHECK(clampedUtf8.snapshot.mixer.tracks.size() == 1);
  CHECK(clampedUtf8.snapshot.mixer.tracks[0].name == std::string(63, 'a'));

  const std::string asciiName(100, 'b');
  CHECK(writeFile(path, "{\"app\":\"mnkybox\",\"session\":2,\"mixer\":"
                        "{\"tracks\":[{\"slot\":0,\"name\":\"" +
                            asciiName + "\"}]}}"));
  const LoadResult clampedAscii = mnky::session::load(path);
  CHECK(clampedAscii.ok);
  CHECK(clampedAscii.snapshot.mixer.tracks[0].name ==
        std::string(mnky::session::kMaxTrackNameBytes, 'b'));
}

// ------------------------------------------------------------- per-track FX

void testInsertRoundTrip() {
  TempDir dir;
  CHECK(dir.ok());
  if (!dir.ok()) return;
  const std::string path = dir.file("insert.json");

  Snapshot original;
  original.mixer.channels[0].insert.id = "builtin.delay";
  original.mixer.channels[0].insert.bypass = true;
  original.mixer.channels[0].insert.params = {0.25, 1200.0, -3.5, 1e-9};
  original.mixer.channels[4].insert.id = "com.example.reverb";
  original.mixer.channels[4].insert.params = {0.5};
  original.mixer.channels[4].gain = 0.5f;
  CHECK(mnky::session::save(path, original));
  const std::string text = readFile(path);
  CHECK(text.find("\"insert\":{\"id\":\"builtin.delay\",\"bypass\":true,"
                  "\"params\":[0.25,1200,-3.5,") != std::string::npos);
  // Channel 1 has no insert: the empty id is the "none" encoding.
  CHECK(text.find("\"insert\":{\"id\":\"\",\"bypass\":false,"
                  "\"params\":[]}") != std::string::npos);
  LoadResult loaded = mnky::session::load(path);
  if (!loaded.ok) std::printf("FAIL insert load: %s\n", loaded.error.c_str());
  CHECK(loaded.ok);
  CHECK(loaded.snapshot == original);
  CHECK(loaded.snapshot.mixer.channels[0].insert.id == "builtin.delay");
  CHECK(loaded.snapshot.mixer.channels[0].insert.bypass);
  CHECK(loaded.snapshot.mixer.channels[0].insert.params.size() == 4);
  CHECK(loaded.snapshot.mixer.channels[1].insert.id.empty());
  CHECK(loaded.snapshot.mixer.channels[1].insert.params.empty());

  // An absent "insert" object means no insert.
  CHECK(writeFile(
      path,
      R"({"app":"mnkybox","session":2,"mixer":{"channels":[{"gain":0.5}]}})"));
  loaded = mnky::session::load(path);
  CHECK(loaded.ok);
  CHECK(loaded.snapshot.mixer.channels[0].insert.id.empty());
  CHECK(!loaded.snapshot.mixer.channels[0].insert.bypass);
  CHECK(loaded.snapshot.mixer.channels[0].insert.params.empty());
}

void testInsertCorrupt() {
  TempDir dir;
  CHECK(dir.ok());
  if (!dir.ok()) return;
  const std::string path = dir.file("insert-corrupt.json");

  struct Case {
    const char* text;
    const char* reason;
  };
  const Case cases[] = {
      {R"({"app":"mnkybox","session":2,"mixer":{"channels":[{"insert":[]}]}})",
       ".insert\" is not an object"},
      {R"({"app":"mnkybox","session":2,"mixer":{"channels":[{"insert":{"id":7}}]}})",
       ".insert.id\" is not a string"},
      {R"({"app":"mnkybox","session":2,"mixer":{"channels":[{"insert":{"bypass":"yes"}}]}})",
       ".insert.bypass\" is not a boolean"},
      {R"({"app":"mnkybox","session":2,"mixer":{"channels":[{"insert":{"params":{}}}]}})",
       ".insert.params\" is not an array"},
      {R"({"app":"mnkybox","session":2,"mixer":{"channels":[{"insert":{"params":[1,"two"]}}]}})",
       ".insert.params[1]\" is not a number"},
  };
  for (const Case& testCase : cases) {
    CHECK(writeFile(path, testCase.text));
    const LoadResult result = mnky::session::load(path);
    if (result.ok) std::printf("FAIL corrupt insert accepted: %s\n", testCase.text);
    CHECK(!result.ok);
    CHECK(result.error.find(testCase.reason) != std::string::npos);
  }
}

// --------------------------------------------- per-track FX rack (FX1/FX2)

void testTrackFxRackRoundTrip() {
  TempDir dir;
  CHECK(dir.ok());
  if (!dir.ok()) return;
  const std::string path = dir.file("fx-rack.json");

  Snapshot original;
  original.mixer.channels[0].gain = 0.5f;
  original.mixer.channels[0].fxSlots[0].id = "builtin.delay";
  original.mixer.channels[0].fxSlots[0].bypass = true;
  original.mixer.channels[0].fxSlots[0].wet = 0.3f;
  original.mixer.channels[0].fxSlots[0].params = {0.25, 1200.0, -3.5, 1e-9};
  original.mixer.channels[0].fxSlots[1].id = "com.example.reverb";
  original.mixer.channels[0].fxSlots[1].wet = 0.75f;
  original.mixer.channels[0].fxSlots[1].params = {0.5, 2.0};
  // The deprecated send[] still round-trips; it does not feed the rack once
  // the file carries explicit fxSlots.
  original.mixer.channels[4].send = {0.2f, 0.8f};
  original.mixer.channels[4].fxSlots[1].id = "clap.example.space";
  original.mixer.channels[4].fxSlots[1].wet = 0.0f;

  CHECK(mnky::session::save(path, original));
  const std::string text = readFile(path);
  CHECK(text.find("\"fxSlots\":[{\"id\":\"builtin.delay\",\"bypass\":true,"
                  "\"wet\":0.3,\"params\":[0.25,1200,-3.5,") !=
        std::string::npos);
  CHECK(text.find("{\"id\":\"com.example.reverb\",\"bypass\":false,"
                  "\"wet\":0.75,\"params\":[0.5,2]}") != std::string::npos);
  // A channel that never touched the rack saves two empty slots at wet 1.
  CHECK(text.find("\"fxSlots\":[{\"id\":\"\",\"bypass\":false,\"wet\":1,"
                  "\"params\":[]},{\"id\":\"\",\"bypass\":false,\"wet\":1,"
                  "\"params\":[]}]") != std::string::npos);

  const LoadResult loaded = mnky::session::load(path);
  if (!loaded.ok) std::printf("FAIL fx rack load: %s\n", loaded.error.c_str());
  CHECK(loaded.ok);
  CHECK(loaded.snapshot == original);
  CHECK(loaded.snapshot.mixer.channels[0].fxSlots[0].id == "builtin.delay");
  CHECK(loaded.snapshot.mixer.channels[0].fxSlots[0].bypass);
  CHECK(loaded.snapshot.mixer.channels[0].fxSlots[0].wet == 0.3f);
  CHECK(loaded.snapshot.mixer.channels[0].fxSlots[0].params.size() == 4);
  CHECK(loaded.snapshot.mixer.channels[0].fxSlots[0].params[3] == 1e-9);
  CHECK(loaded.snapshot.mixer.channels[0].fxSlots[1].id ==
        "com.example.reverb");
  CHECK(loaded.snapshot.mixer.channels[0].fxSlots[1].wet == 0.75f);
  CHECK(loaded.snapshot.mixer.channels[4].send[1] == 0.8f);

  // wet is part of the value: a snapshot that differs only there is unequal.
  Snapshot different = original;
  different.mixer.channels[0].fxSlots[1].wet = 0.5f;
  CHECK(!(different == original));
}

void testTrackFxRackLegacyMigration() {
  TempDir dir;
  CHECK(dir.ok());
  if (!dir.ok()) return;
  const std::string path = dir.file("fx-rack-legacy.json");

  // An old file: one per-track insert plus the FX1/FX2 send levels, no
  // fxSlots key. The insert becomes FX1; the sends become each slot's wet.
  CHECK(writeFile(
      path,
      R"({"app":"mnkybox","session":2,"mixer":{"channels":[{"gain":0.5,"send":[0.25,0.75],"insert":{"id":"builtin.delay","bypass":true,"params":[0.5,250]}}]}})"));
  LoadResult loaded = mnky::session::load(path);
  if (!loaded.ok) std::printf("FAIL fx rack legacy load: %s\n",
                              loaded.error.c_str());
  CHECK(loaded.ok);
  const auto& legacy = loaded.snapshot.mixer.channels[0];
  CHECK(legacy.fxSlots[0].id == "builtin.delay");
  CHECK(legacy.fxSlots[0].bypass);
  CHECK(legacy.fxSlots[0].params.size() == 2);
  CHECK(legacy.fxSlots[0].params[0] == 0.5);
  CHECK(legacy.fxSlots[0].params[1] == 250.0);
  CHECK(legacy.fxSlots[0].wet == 0.25f);
  CHECK(legacy.fxSlots[1].id.empty());
  CHECK(!legacy.fxSlots[1].bypass);
  CHECK(legacy.fxSlots[1].params.empty());
  CHECK(legacy.fxSlots[1].wet == 0.75f);
  CHECK(legacy.send[0] == 0.25f);  // deprecated field still decodes
  CHECK(legacy.send[1] == 0.75f);
  CHECK(legacy.insert.id == "builtin.delay");

  // A migrated snapshot saves the rack explicitly, so the next load no longer
  // depends on the legacy fallbacks.
  const Snapshot migrated = loaded.snapshot;
  CHECK(mnky::session::save(path, migrated));
  loaded = mnky::session::load(path);
  CHECK(loaded.ok);
  CHECK(loaded.snapshot == migrated);

  // A legacy insert without a send array keeps the 1.0 wet default.
  CHECK(writeFile(path,
                  R"({"app":"mnkybox","session":2,"mixer":{"channels":[{"insert":{"id":"builtin.reverb"}}]}})"));
  loaded = mnky::session::load(path);
  CHECK(loaded.ok);
  CHECK(loaded.snapshot.mixer.channels[0].fxSlots[0].id == "builtin.reverb");
  CHECK(loaded.snapshot.mixer.channels[0].fxSlots[0].wet == 1.0f);
  CHECK(loaded.snapshot.mixer.channels[0].fxSlots[1].wet == 1.0f);

  // fxSlots present wins over a legacy insert in the same channel.
  CHECK(writeFile(
      path,
      R"({"app":"mnkybox","session":2,"mixer":{"channels":[{"insert":{"id":"builtin.delay"},"fxSlots":[{"id":"builtin.reverb","wet":0.4}]}]}})"));
  loaded = mnky::session::load(path);
  CHECK(loaded.ok);
  CHECK(loaded.snapshot.mixer.channels[0].fxSlots[0].id == "builtin.reverb");
  CHECK(loaded.snapshot.mixer.channels[0].fxSlots[0].wet == 0.4f);
  CHECK(loaded.snapshot.mixer.channels[0].fxSlots[1].id.empty());
  CHECK(loaded.snapshot.mixer.channels[0].fxSlots[1].wet == 1.0f);
  // The deprecated key is still decoded for old readers, never applied over
  // the rack.
  CHECK(loaded.snapshot.mixer.channels[0].insert.id == "builtin.delay");
}

void testTrackFxRackDefaults() {
  TempDir dir;
  CHECK(dir.ok());
  if (!dir.ok()) return;
  const std::string path = dir.file("fx-rack-defaults.json");

  CHECK(writeFile(
      path,
      R"({"app":"mnkybox","session":2,"mixer":{"channels":[{"gain":0.5}]}})"));
  LoadResult loaded = mnky::session::load(path);
  CHECK(loaded.ok);
  CHECK(loaded.snapshot.mixer.channels[0].gain == 0.5f);
  for (int slot = 0; slot < mnky::session::kTrackFxSlots; ++slot) {
    const auto& empty = loaded.snapshot.mixer.channels[0]
                            .fxSlots[static_cast<std::size_t>(slot)];
    CHECK(empty.id.empty());
    CHECK(!empty.bypass);
    CHECK(empty.wet == 1.0f);
    CHECK(empty.params.empty());
  }

  // wet clamps to 0..1 instead of rejecting, like the other mixer levels.
  CHECK(writeFile(
      path,
      R"({"app":"mnkybox","session":2,"mixer":{"channels":[{"fxSlots":[{"id":"builtin.delay","wet":1.5},{"wet":-0.5}]}]}})"));
  loaded = mnky::session::load(path);
  CHECK(loaded.ok);
  CHECK(loaded.snapshot.mixer.channels[0].fxSlots[0].wet == 1.0f);
  CHECK(loaded.snapshot.mixer.channels[0].fxSlots[1].wet == 0.0f);
}

void testTrackFxRackCorrupt() {
  TempDir dir;
  CHECK(dir.ok());
  if (!dir.ok()) return;
  const std::string path = dir.file("fx-rack-corrupt.json");

  struct Case {
    const char* text;
    const char* reason;
  };
  const Case cases[] = {
      {R"({"app":"mnkybox","session":2,"mixer":{"channels":[{"fxSlots":{}}]}})",
       ".fxSlots\" is not an array"},
      {R"({"app":"mnkybox","session":2,"mixer":{"channels":[{"fxSlots":[[]]}]}})",
       ".fxSlots[0]\" is not an object"},
      {R"({"app":"mnkybox","session":2,"mixer":{"channels":[{"fxSlots":[{"id":7}]}]}})",
       ".fxSlots[0].id\" is not a string"},
      {R"({"app":"mnkybox","session":2,"mixer":{"channels":[{"fxSlots":[{"bypass":"yes"}]}]}})",
       ".fxSlots[0].bypass\" is not a boolean"},
      {R"({"app":"mnkybox","session":2,"mixer":{"channels":[{"fxSlots":[{"wet":"half"}]}]}})",
       ".fxSlots[0].wet\" is not a number"},
      {R"({"app":"mnkybox","session":2,"mixer":{"channels":[{"fxSlots":[{"params":{}}]}]}})",
       ".fxSlots[0].params\" is not an array"},
      {R"({"app":"mnkybox","session":2,"mixer":{"channels":[{"fxSlots":[{"params":[1,"two"]}]}]}})",
       ".fxSlots[0].params[1]\" is not a number"},
  };
  for (const Case& testCase : cases) {
    CHECK(writeFile(path, testCase.text));
    const LoadResult result = mnky::session::load(path);
    if (result.ok) {
      std::printf("FAIL corrupt fx rack accepted: %s\n", testCase.text);
    }
    CHECK(!result.ok);
    CHECK(result.error.find(testCase.reason) != std::string::npos);
  }
}

// ------------------------------------- per-track FX rack engine seam

void testTrackFxRackApplyCapture() {
  Router router;
  Engine engine;

  Snapshot snapshot;
  snapshot.mixer.channels[0].fxSlots[0].id = "builtin.delay";
  snapshot.mixer.channels[0].fxSlots[0].wet = 0.4f;
  snapshot.mixer.channels[0].fxSlots[0].params = {0.25, 250.0};
  snapshot.mixer.channels[0].fxSlots[1].id = "com.example.space";
  snapshot.mixer.channels[0].fxSlots[1].wet = 0.0f;
  snapshot.mixer.channels[0].fxSlots[1].params = {0.5};
  mnky::session::apply(snapshot, router, engine);

  const mnky::audio::TrackFxSlot fx1 = engine.trackFxSlot(0, 0);
  CHECK(fx1.id == "builtin.delay");
  CHECK(fx1.wet == 0.4f);
  CHECK(fx1.params.size() == 8);  // the effect's stable order
  const mnky::audio::TrackFxSlot fx2 = engine.trackFxSlot(0, 1);
  CHECK(fx2.id == "com.example.space");
  CHECK(fx2.wet == 0.0f);
  CHECK(fx2.params == std::vector<double>{0.5});

  const Snapshot captured = mnky::session::capture(router, engine);
  CHECK(captured.mixer.channels[0].fxSlots[0].id == "builtin.delay");
  CHECK(captured.mixer.channels[0].fxSlots[0].wet == 0.4f);
  CHECK(captured.mixer.channels[0].fxSlots[0].params == fx1.params);
  CHECK(captured.mixer.channels[0].fxSlots[1].id == "com.example.space");
  CHECK(captured.mixer.channels[0].fxSlots[1].wet == 0.0f);
  // The deprecated insert stays the legacy view of FX1.
  CHECK(captured.mixer.channels[0].insert.id == "builtin.delay");
  CHECK(captured.mixer.channels[0].insert.bypass == fx1.bypass);
  CHECK(captured.mixer.channels[0].insert.params == fx1.params);

  // apply() is wholesale: two empty slots clear the engine rack.
  const Snapshot empty;
  mnky::session::apply(empty, router, engine);
  CHECK(engine.trackFxSlot(0, 0).id.empty());
  CHECK(engine.trackFxSlot(0, 1).id.empty());
}

void testTrackFxRackLegacyEndToEnd() {
  TempDir dir;
  CHECK(dir.ok());
  if (!dir.ok()) return;
  const std::string path = dir.file("fx-rack-legacy-engine.json");

  // Old file: one insert plus FX1/FX2 send levels. The reader migrates the
  // insert to FX1 and the sends to the two slots' wet levels; apply() then
  // forwards the migrated rack to the engine and capture() reads it back.
  CHECK(writeFile(
      path,
      R"({"app":"mnkybox","session":2,"mixer":{"channels":[{"send":[0.25,0.75],"insert":{"id":"builtin.delay","bypass":true,"params":[0.5,250]}}]}})"));
  const LoadResult loaded = mnky::session::load(path);
  if (!loaded.ok) std::printf("FAIL legacy rack load: %s\n", loaded.error.c_str());
  CHECK(loaded.ok);

  Router router;
  Engine engine;
  mnky::session::apply(loaded.snapshot, router, engine);
  const mnky::audio::TrackFxSlot fx1 = engine.trackFxSlot(0, 0);
  CHECK(fx1.id == "builtin.delay");
  CHECK(fx1.bypass);
  CHECK(fx1.wet == 0.25f);
  CHECK(engine.trackFxSlot(0, 1).id.empty());
  CHECK(engine.trackFxSlot(0, 1).wet == 0.75f);

  const Snapshot captured = mnky::session::capture(router, engine);
  CHECK(captured.mixer.channels[0].fxSlots[0].id == "builtin.delay");
  CHECK(captured.mixer.channels[0].fxSlots[0].bypass);
  CHECK(captured.mixer.channels[0].fxSlots[0].wet == 0.25f);
  CHECK(captured.mixer.channels[0].fxSlots[1].wet == 0.75f);
}

// --------------------------------------------- per-track insert engine seam

void testInsertApplyCapture() {
  Router router;
  Engine engine;

  // A CLAP id is echoed verbatim (the plugin owns its parameter table), so the
  // session values survive an apply() -> capture() round trip.
  Snapshot snapshot;
  snapshot.mixer.channels[3].insert.id = "com.example.gain";
  snapshot.mixer.channels[3].insert.bypass = true;
  snapshot.mixer.channels[3].insert.params = {0.5, -2.0, 3.25};
  mnky::session::apply(snapshot, router, engine);
  CHECK(engine.trackInsert(3).id == "com.example.gain");
  CHECK(engine.trackInsert(3).bypass);
  CHECK(engine.trackInsert(3).params ==
        snapshot.mixer.channels[3].insert.params);
  CHECK(engine.trackInsert(4).id.empty());

  const Snapshot captured = mnky::session::capture(router, engine);
  CHECK(captured.mixer.channels[3].insert.id == "com.example.gain");
  CHECK(captured.mixer.channels[3].insert.bypass);
  CHECK(captured.mixer.channels[3].insert.params ==
        snapshot.mixer.channels[3].insert.params);
  CHECK(captured.mixer.channels[4].insert.id.empty());
  CHECK(captured.mixer.channels[4].insert.params.empty());

  // A built-in id is captured as the state the DSP holds: the id survives and
  // the params come back in the effect's stable order, clamped to its ranges.
  Snapshot builtin;
  builtin.mixer.channels[0].insert.id = "builtin.delay";
  builtin.mixer.channels[0].insert.params = {0.25, 300.0};
  mnky::session::apply(builtin, router, engine);
  const mnky::audio::TrackInsert delay = engine.trackInsert(0);
  CHECK(delay.id == "builtin.delay");
  CHECK(delay.params.size() == 8);  // Delay's stable order
  CHECK(delay.params[1] != 300.0);  // feedback is clamped by the effect
  const Snapshot recaptured = mnky::session::capture(router, engine);
  CHECK(recaptured.mixer.channels[0].insert.id == "builtin.delay");
  CHECK(recaptured.mixer.channels[0].insert.params == delay.params);

  // An empty id detaches every insert again.
  const Snapshot none;
  mnky::session::apply(none, router, engine);
  CHECK(engine.trackInsert(0).id.empty());
  CHECK(engine.trackInsert(3).id.empty());
}

// ------------------------------------------------------ pseudo-source routes

void testPseudoSourceRoutes() {
  TempDir dir;
  CHECK(dir.ok());
  if (!dir.ok()) return;
  const std::string path = dir.file("pseudo-routes.json");

  RouteState generator;
  generator.src = mnky::session::kSrcGenerator;
  generator.dst = static_cast<int>(Port::UsbOut);
  generator.on = true;
  MidiPipe arp;
  arp.type = MidiPipeType::Arp;
  arp.params = {2, 0, 2, 50, 70, 80};
  MidiPipe repeater;
  repeater.type = MidiPipeType::Repeater;
  repeater.bypass = true;
  repeater.chMask = 0x000F;
  repeater.params = {1, 3, 40};
  generator.pipes = {arp, repeater};

  RouteState outputInsert;
  outputInsert.src = mnky::session::kSrcOutputInsert;
  outputInsert.dst = static_cast<int>(Port::TrsOut1);
  outputInsert.on = true;
  MidiPipe delay;
  delay.type = MidiPipeType::MidiDelay;
  delay.params = {120, 2, 50};
  outputInsert.pipes = {delay};

  // The highest legal endpoint pair, from the port constant (not a literal).
  const int lastPort = mnky::session::kSessionMaxPorts - 1;
  RouteState boundary;
  boundary.src = lastPort;
  boundary.dst = lastPort;
  boundary.on = true;

  Snapshot original;
  original.router.routes = {generator, outputInsert, boundary};
  CHECK(mnky::session::save(path, original));
  const std::string text = readFile(path);
  CHECK(text.find("\"src\":-1,") != std::string::npos);
  CHECK(text.find("\"src\":-2,") != std::string::npos);
  CHECK(text.find("\"src\":" + std::to_string(lastPort) + ",\"dst\":" +
                  std::to_string(lastPort)) != std::string::npos);

  const LoadResult loaded = mnky::session::load(path);
  if (!loaded.ok) {
    std::printf("FAIL pseudo route load: %s\n", loaded.error.c_str());
  }
  CHECK(loaded.ok);
  CHECK(loaded.snapshot == original);
  CHECK(loaded.snapshot.router.routes[0].src == mnky::session::kSrcGenerator);
  CHECK(loaded.snapshot.router.routes[0].pipes.size() == 2);
  CHECK(loaded.snapshot.router.routes[1].src ==
        mnky::session::kSrcOutputInsert);
  CHECK(loaded.snapshot.router.routes[1].pipes.size() == 1);

  // apply() never turns a pseudo-source chain into a port-matrix route.
  Snapshot chains;
  chains.router.routes = {generator, outputInsert};
  Router router;
  Engine engine;
  mnky::session::apply(chains, router, engine);
  for (int src = 0; src < mnky::midi::kMaxPorts; ++src) {
    for (int dst = 0; dst < mnky::midi::kMaxPorts; ++dst) {
      CHECK(!router.routeEnabled(static_cast<Port>(src),
                                 static_cast<Port>(dst)));
    }
  }

  struct Case {
    std::string text;
    std::string reason;
  };
  // The legal endpoints and the reason strings come from the constants, so a
  // port-capacity change updates the test instead of breaking it.
  const std::string srcOutside =
      ".src\" is outside " + std::to_string(mnky::session::kSrcOutputInsert) +
      ".." + std::to_string(mnky::session::kSessionMaxPorts - 1);
  const std::string dstOutside =
      ".dst\" is outside 0.." + std::to_string(mnky::session::kSessionMaxPorts - 1);
  const std::string tooLow = "{\"app\":\"mnkybox\",\"session\":2,\"router\":"
                             "{\"routes\":[{\"src\":-3,\"dst\":5}]}}";
  const std::string tooHigh =
      "{\"app\":\"mnkybox\",\"session\":2,\"router\":{\"routes\":"
      "[{\"src\":" + std::to_string(mnky::session::kSessionMaxPorts) +
      ",\"dst\":5}]}}";
  const Case cases[] = {
      {tooLow, srcOutside},
      {tooHigh, srcOutside},
      {R"({"app":"mnkybox","session":2,"router":{"routes":[{"src":0,"dst":-1}]}})",
       dstOutside},
      {"{\"app\":\"mnkybox\",\"session\":2,\"router\":{\"routes\":"
       "[{\"src\":0,\"dst\":" + std::to_string(mnky::session::kSessionMaxPorts) +
           "}]}}",
       dstOutside},
  };
  for (const Case& testCase : cases) {
    CHECK(writeFile(path, testCase.text));
    const LoadResult result = mnky::session::load(path);
    if (result.ok) {
      std::printf("FAIL bad route endpoint accepted: %s\n", testCase.text.c_str());
    }
    CHECK(!result.ok);
    CHECK(result.error.find(testCase.reason) != std::string::npos);
  }
}

// ------------------------------------------------- pseudo apply/capture seam

void testPseudoSourceApplyCapture() {
  Router router;
  Engine engine;

  RouteState generator;
  generator.src = mnky::session::kSrcGenerator;
  generator.dst = static_cast<int>(Port::UsbOut);
  generator.on = true;
  MidiPipe lfo;
  lfo.type = MidiPipeType::CcLfo;
  lfo.params = {1, 0, 2, 64, 64};  // CC1, 1/16 grid
  generator.pipes = {lfo};

  RouteState insert;
  insert.src = mnky::session::kSrcOutputInsert;
  insert.dst = static_cast<int>(Port::UsbOut);
  insert.on = true;
  MidiPipe transform;
  transform.type = MidiPipeType::Transform;
  transform.params = {1, 0, 127, 2, 5};  // CC -> PC5
  insert.pipes = {transform};

  Snapshot chains;
  chains.router.routes = {generator, insert};
  mnky::session::apply(chains, router, engine);

  // Pi-owned destinations receive the chains; the generator is enabled and
  // the insert takes slot 0 in route order.
  CHECK(router.generatorEnabled(Port::UsbOut));
  CHECK(router.generatorPipeCount(Port::UsbOut) == 1);
  CHECK(router.outputInsertEnabled(Port::UsbOut, 0));
  CHECK(router.outputInsertPipeCount(Port::UsbOut, 0) == 1);

  // The generator emits from the clock grid with no input, and its output
  // passes the output insert: CC1 becomes PC5 on UsbOut.
  std::vector<std::pair<Port, mnky::midi::MidiEvent>> log;
  const mnky::midi::DeliverFn capture = [&log](Port dst,
                                               const mnky::midi::MidiEvent& ev) {
    log.push_back({dst, ev});
  };
  router.clockTick(0, 1000, capture);
  CHECK(log.empty());
  router.clockTick(6, 1000, capture);
  CHECK(log.size() == 1);
  CHECK(log[0].first == Port::UsbOut);
  CHECK(log[0].second.data[0] == 0xC0 && log[0].second.data[1] == 5);

  // capture() reads the enabled Pi-side chains back, including their pipes.
  const Snapshot captured = mnky::session::capture(router, engine);
  bool saw_generator = false;
  bool saw_insert = false;
  for (const RouteState& route : captured.router.routes) {
    if (route.src == mnky::session::kSrcGenerator &&
        route.dst == static_cast<int>(Port::UsbOut)) {
      saw_generator = true;
      CHECK(route.on);
      CHECK(route.pipes.size() == 1);
      CHECK(route.pipes[0].type == MidiPipeType::CcLfo);
      CHECK(route.pipes[0].params.size() == 5);
    }
    if (route.src == mnky::session::kSrcOutputInsert &&
        route.dst == static_cast<int>(Port::UsbOut)) {
      saw_insert = true;
      CHECK(route.on);
      CHECK(route.pipes.size() == 1);
      CHECK(route.pipes[0].type == MidiPipeType::Transform);
    }
    CHECK(route.src >= mnky::session::kSrcOutputInsert);
  }
  CHECK(saw_generator);
  CHECK(saw_insert);

  // A TRS output belongs to the Pico: apply leaves it out of the Pi engine.
  RouteState trsInsert = insert;
  trsInsert.dst = static_cast<int>(Port::TrsOut1);
  Snapshot trs;
  trs.router.routes = {trsInsert};
  Router trsRouter;
  mnky::session::apply(trs, trsRouter, engine);
  CHECK(!trsRouter.outputInsertEnabled(Port::TrsOut1, 0));

}

void run(const char* name, void (*test)()) {
  const int before = gFailures;
  test();
  if (gFailures == before) pass(name);
}

}  // namespace

int main() {
  std::printf("mnkybox session test (versioned JSON save/load)\n");
  run("round-trip save/load in mkdtemp", testRoundTrip);
  run("version reject", testVersionReject);
  run("version 1 loads with click mapping", testVersionOneLoads);
  run("corrupt reject with reason", testCorruptReject);
  run("missing fields default", testMissingFields);
  run("unknown fields tolerated", testUnknownFields);
  run("apply/capture router+mixer", testApplyAndCapture);
  run("apply/capture FX slots", testFxSlots);
  run("dynamic track list round-trip", testTrackListRoundTrip);
  run("legacy files default to every slot", testTrackListLegacyDefault);
  run("corrupt track list rejects with reason", testTrackListCorrupt);
  run("per-track insert round-trip", testInsertRoundTrip);
  run("corrupt per-track insert rejects with reason", testInsertCorrupt);
  run("per-track insert apply/capture", testInsertApplyCapture);
  run("per-track FX rack round-trip", testTrackFxRackRoundTrip);
  run("per-track FX rack legacy migration", testTrackFxRackLegacyMigration);
  run("per-track FX rack defaults and clamps", testTrackFxRackDefaults);
  run("corrupt per-track FX rack rejects with reason", testTrackFxRackCorrupt);
  run("per-track FX rack apply/capture", testTrackFxRackApplyCapture);
  run("per-track FX rack legacy end-to-end", testTrackFxRackLegacyEndToEnd);
  run("pseudo-source routes round-trip", testPseudoSourceRoutes);
  run("pseudo-source apply/capture", testPseudoSourceApplyCapture);
  run("save/load file errors", testFileErrors);
  if (gFailures != 0) {
    std::printf("FAIL %d check(s)\n", gFailures);
    return 1;
  }
  std::printf("PASS test_session\n");
  return 0;
}
