// Assert-style acceptance tests for mnky::session (no external framework).
// Build: clang++ -std=c++20 -Wall -Wextra -Werror -I. -I.. test_session.cpp
//        session.cpp ../audio/engine.cpp ../midi/router.cpp -o /tmp/t_session
//
// The real-file tests run in a fresh mkdtemp() directory. A failing check
// prints FAIL and the process exits non-zero, so `test_session` itself is the
// gate.
#include <array>
#include <cstdint>
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
      {static_cast<int>(Port::TrsIn1), static_cast<int>(Port::UsbOut), true},
      {static_cast<int>(Port::UsbIn), static_cast<int>(Port::TrsOut2), true},
      {static_cast<int>(Port::TrsIn2), static_cast<int>(Port::TrsOut1), false}};
  original.router.channels[0] = 0x0003;
  original.router.channels[2] = 0xFFFF;
  original.router.channels[5] = 0x00F0;
  original.router.transpose[0] = -12;
  original.router.transpose[2] = 7;

  original.mixer.channels[0].gain = 0.125f;
  original.mixer.channels[0].pan = 0.0f;
  original.mixer.channels[0].mute = true;
  original.mixer.channels[0].send = {0.25f, 0.75f};
  original.mixer.channels[17].gain = 1.5f;
  original.mixer.channels[17].pan = 1.0f;
  original.mixer.channels[17].solo = true;
  original.mixer.channels[17].arm = true;
  original.mixer.channels[17].send = {1.0f, 0.0f};
  original.mixer.master = 0.8f;

  original.fx.bus[0].id = "com.mnkygain";
  original.fx.bus[0].params = {0.1, -2.5, 3.0, 1e-9};
  original.fx.bus[0].bypass = true;
  original.fx.bus[1].id = "builtin.delay \"slap\"\\tape\ncaf" "\xC3\xA9";
  original.fx.bus[1].params = {0.0, 1000.5};

  original.transport.bpmX100 = 13333;
  original.click.enabled = true;
  original.click.level = 0.65f;
  original.click.bars = 4;
  original.midimap = {
      {74, 1, "mixer.ch3.gain", 3},
      {7, 16, "fx.0.mix", 0},
      {0, 1, "future.unknown.param", 17}};

  TempDir dir;
  CHECK(dir.ok());
  if (!dir.ok()) return;
  const std::string path = dir.file("session.json");
  CHECK(mnky::session::save(path, original));

  const std::string text = readFile(path);
  CHECK(!text.empty());
  CHECK(text.find("\"app\":\"mnkybox\"") != std::string::npos);
  CHECK(text.find("\"session\":1") != std::string::npos);
  CHECK(text.find("\"routes\":[") != std::string::npos);
  CHECK(text.find("\"channels\":{\"0\":") != std::string::npos);
  CHECK(text.find("\"transpose\":{\"0\":") != std::string::npos);
  CHECK(text.find("\"master\":") != std::string::npos);
  CHECK(text.find("\"bpm_x100\":13333") != std::string::npos);
  CHECK(text.find("\"click\":{\"enabled\":true") != std::string::npos);
  CHECK(text.find("\"midimap\":[") != std::string::npos);
  CHECK(text.find("\"target\":\"mixer.ch3.gain\"") != std::string::npos);
  CHECK(text.find("\\\"slap\\\"") != std::string::npos);

  const LoadResult loaded = mnky::session::load(path);
  if (!loaded.ok) std::printf("FAIL load: %s\n", loaded.error.c_str());
  CHECK(loaded.ok);
  CHECK(loaded.snapshot == original);
}

// ------------------------------------------------------------ version reject

void testVersionReject() {
  TempDir dir;
  CHECK(dir.ok());
  if (!dir.ok()) return;
  const std::string path = dir.file("version.json");

  CHECK(writeFile(path, R"({"app":"mnkybox","session":2})"));
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
  CHECK(result.snapshot.mixer.channels[17].gain == 1.0f);

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
      {static_cast<int>(Port::TrsIn1), static_cast<int>(Port::UsbOut), true},
      {static_cast<int>(Port::UsbIn), static_cast<int>(Port::TrsOut2), true}};
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
  const Channel untouched = engine.channel(17);
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
  run("corrupt reject with reason", testCorruptReject);
  run("missing fields default", testMissingFields);
  run("unknown fields tolerated", testUnknownFields);
  run("apply/capture router+mixer", testApplyAndCapture);
  run("apply/capture FX slots", testFxSlots);
  run("save/load file errors", testFileErrors);
  if (gFailures != 0) {
    std::printf("FAIL %d check(s)\n", gFailures);
    return 1;
  }
  std::printf("PASS test_session\n");
  return 0;
}
