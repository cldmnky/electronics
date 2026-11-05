// Host self-test for the dynamic USB-MIDI port registry and the session
// binding rules. No framework: a failing check prints FAIL and the process
// exits non-zero, so `test_registry` itself is the acceptance gate. No MIDI
// hardware is required; RtMidi only needs to enumerate an empty list.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "port_registry.hpp"
#include "router.hpp"

// The dynamic port bound must stay aligned with the session format. This test
// only uses the session structures (no session.cpp calls), so the header is
// enough and the standalone midi build stays independent of the audio engine.
#include "session/session.hpp"

#if defined(MNKY_MIDI_SESSION_TEST)
#include <filesystem>
#include <system_error>
#endif

namespace {

int g_failures = 0;

void check(bool ok, const char* what, const char* file, int line) {
  if (ok) return;
  std::fprintf(stderr, "FAIL %s (%s:%d)\n", what, file, line);
  ++g_failures;
}

#define CHECK(cond) check((cond), #cond, __FILE__, __LINE__)

void pass(const char* name) { std::printf("PASS %s\n", name); }

using mnky::midi::Port;
using mnky::midi::PortRegistry;
using mnky::midi::Router;

Port dynamicPort(int id) { return static_cast<Port>(id); }

// ---------------------------------------------------------------------------
// registry lifecycle: register / update / disconnect / remove / capacity
// ---------------------------------------------------------------------------
void registryLifecycle() {
  PortRegistry registry;
  CHECK(registry.size() == 0);
  CHECK(registry.connectedCount() == 0);
  CHECK(registry.entries().empty());

  const int a = registry.connect("usb:Alpha", true, false);
  CHECK(a == mnky::midi::kFirstDynamicPort);
  const int b = registry.connect("usb:Beta", false, true);
  CHECK(b == a + 1);
  CHECK(registry.size() == 2);
  CHECK(registry.connectedCount() == 2);
  CHECK(registry.find("usb:Alpha") == a);
  CHECK(registry.find("usb:Missing") == 0);

  // A device with both endpoints keeps one id and unions the direction flags.
  CHECK(registry.connect("usb:Alpha", false, true) == a);
  const auto* alpha = registry.entry(a);
  CHECK(alpha != nullptr && alpha->input && alpha->output && alpha->connected);
  CHECK(registry.connectedCount() == 2);

  // Hot-unplug keeps the id reserved for the route table.
  CHECK(registry.disconnect("usb:Alpha"));
  CHECK(registry.find("usb:Alpha") == a);
  CHECK(registry.entry(a) != nullptr && !registry.entry(a)->connected);
  CHECK(registry.connectedCount() == 1);
  CHECK(!registry.disconnect("usb:Missing"));

  // Replug wakes the same slot rather than allocating a new id.
  CHECK(registry.connect("usb:Alpha", true, false) == a);
  CHECK(registry.entry(a)->connected);

  // Explicit forget frees the slot; the next identity takes the lowest id.
  CHECK(registry.remove("usb:Beta"));
  CHECK(registry.find("usb:Beta") == 0);
  CHECK(registry.connect("usb:Gamma", true, false) == b);
  CHECK(registry.size() == 2);

  // Capacity is the session's dynamic range (6..31), never a fixed slot.
  int index = 0;
  while (registry.size() < static_cast<std::size_t>(mnky::midi::kMaxDynamicPorts)) {
    CHECK(registry.connect("usb:Fill" + std::to_string(index++), true, false) != 0);
  }
  CHECK(registry.connect("usb:Overflow", true, false) == 0);
  CHECK(registry.size() == static_cast<std::size_t>(mnky::midi::kMaxDynamicPorts));
  const auto entries = registry.entries();
  CHECK(entries.front().id == mnky::midi::kFirstDynamicPort);
  CHECK(entries.back().id == mnky::midi::kMaxPorts - 1);

  registry.clear();
  CHECK(registry.size() == 0 && registry.connectedCount() == 0);
  pass("registry lifecycle");
}

// ---------------------------------------------------------------------------
// hot-plug inventory: ids stay reserved while a device is away
// ---------------------------------------------------------------------------
void registryHotPlug() {
  PortRegistry registry;
  registry.syncDevices({"usb:Alpha", "usb:Beta"}, {"usb:Alpha"});
  CHECK(registry.connectedCount() == 2);
  const int alpha = registry.find("usb:Alpha");
  const int beta = registry.find("usb:Beta");
  CHECK(alpha == mnky::midi::kFirstDynamicPort);
  CHECK(beta == alpha + 1);
  CHECK(registry.entry(alpha)->input && registry.entry(alpha)->output);
  CHECK(registry.entry(beta)->input && !registry.entry(beta)->output);

  // Beta unplugged: its id and last known direction stay, only connected drops.
  registry.syncDevices({"usb:Alpha"}, {"usb:Alpha"});
  CHECK(registry.find("usb:Beta") == beta);
  CHECK(!registry.entry(beta)->connected);
  CHECK(registry.connectedCount() == 1);

  // A different device does not steal the dormant id.
  registry.syncDevices({"usb:Alpha", "usb:Gamma"}, {"usb:Alpha"});
  CHECK(registry.find("usb:Beta") == beta);
  CHECK(registry.find("usb:Gamma") == beta + 1);
  CHECK(!registry.entry(beta)->connected);

  // Beta returns in a different enumeration position: same id, routes intact.
  registry.syncDevices({"usb:Gamma", "usb:Alpha", "usb:Beta"}, {"usb:Alpha"});
  CHECK(registry.find("usb:Beta") == beta);
  CHECK(registry.entry(beta)->connected);
  CHECK(registry.connectedCount() == 3);
  pass("registry hot-plug");
}

// Route shape used by the remap tests: the same src/dst/on fields as
// session::RouteState, without pulling session.cpp into this test.
struct Route {
  int src = 0;
  int dst = 0;
  bool on = false;
};

// ---------------------------------------------------------------------------
// name-based rebind: enumeration order may change between runs
// ---------------------------------------------------------------------------
void nameRebind() {
  const std::map<int, std::string> stored = {
      {6, "usb:Arturia KeyStep"},
      {7, "usb:Elektron Digitakt"},
  };

  PortRegistry first;
  CHECK(first.connect("usb:Arturia KeyStep", true, false) == 6);
  CHECK(first.connect("usb:Elektron Digitakt", false, true) == 7);
  const auto identityBinding = first.bindSession(stored);
  CHECK(identityBinding.ids == (std::map<int, int>{{6, 6}, {7, 7}}));
  CHECK(identityBinding.unbound.empty());

  // Replug order is reversed: the same stored ids must follow the names.
  PortRegistry second;
  CHECK(second.connect("usb:Elektron Digitakt", false, true) == 6);
  CHECK(second.connect("usb:Arturia KeyStep", true, false) == 7);
  const auto swapped = second.bindSession(stored);
  CHECK(swapped.ids == (std::map<int, int>{{6, 7}, {7, 6}}));

  std::vector<Route> routes = {{6, 7, true}};
  mnky::midi::remapRouteIds(swapped.ids, routes);
  CHECK(routes.size() == 1 && routes[0].src == 7 && routes[0].dst == 6);

  std::array<std::uint16_t, mnky::midi::kMaxPorts> channels{};
  channels.fill(Router::kAllChannels);
  std::array<int, mnky::midi::kMaxPorts> transpose{};
  channels[6] = 0x000F;
  transpose[7] = -2;
  mnky::midi::remapSessionPorts(swapped.ids, channels, transpose);
  CHECK(channels[7] == 0x000F);          // KeyStep mask follows KeyStep (id 7)
  CHECK(channels[6] == Router::kAllChannels);  // Digitakt's old slot resets
  CHECK(transpose[6] == -2);             // Digitakt transpose follows its id
  CHECK(transpose[7] == 0);

  // An identity never seen this run gets a dormant reserved slot, so its
  // stored routes stay bound by name instead of being dropped.
  PortRegistry third;
  const auto reserved = third.bindSession({{9, "usb:Missing Device"}});
  CHECK(reserved.ids.size() == 1 && reserved.ids.at(9) == mnky::midi::kFirstDynamicPort);
  CHECK(third.entry(reserved.ids.at(9)) != nullptr);
  CHECK(!third.entry(reserved.ids.at(9))->connected);
  CHECK(third.connectedCount() == 0);
  pass("name-based rebind");
}

// ---------------------------------------------------------------------------
// missing device: dormant routes are intact and never reassigned
// ---------------------------------------------------------------------------
void missingDeviceDormancy() {
  PortRegistry registry;
  const int alpha = registry.connect("usb:Alpha", true, false);
  CHECK(alpha == mnky::midi::kFirstDynamicPort);
  registry.syncDevices({}, {});  // Alpha unplugged
  CHECK(!registry.entry(alpha)->connected);

  // Session was saved while Alpha was on id 9.
  const auto binding = registry.bindSession({{9, "usb:Alpha"}});
  CHECK(binding.ids.at(9) == alpha);

  std::vector<Route> routes = {{9, static_cast<int>(Port::TrsOut1), true}};
  mnky::midi::remapRouteIds(binding.ids, routes);
  Router router;
  CHECK(router.setRoute(dynamicPort(routes[0].src), dynamicPort(routes[0].dst),
                        routes[0].on));
  CHECK(router.routeEnabled(dynamicPort(alpha), Port::TrsOut1));

  // Another device appears while Alpha is away: it gets a different id, so the
  // dormant Alpha route is not silently pointed at the newcomer.
  registry.syncDevices({"usb:Beta"}, {"usb:Beta"});
  const int beta = registry.find("usb:Beta");
  CHECK(beta != 0 && beta != alpha);
  CHECK(router.routeEnabled(dynamicPort(alpha), Port::TrsOut1));
  CHECK(!router.routeEnabled(dynamicPort(beta), Port::TrsOut1));

  // Alpha returns: same id, route already live.
  registry.syncDevices({"usb:Alpha", "usb:Beta"}, {"usb:Alpha", "usb:Beta"});
  CHECK(registry.find("usb:Alpha") == alpha);
  CHECK(registry.entry(alpha)->connected);
  CHECK(router.routeEnabled(dynamicPort(alpha), Port::TrsOut1));
  pass("missing-device dormancy");
}

// ---------------------------------------------------------------------------
// router: dynamic ids behave exactly like fixed ids; masks/transpose apply
// ---------------------------------------------------------------------------
void dynamicRouter() {
  CHECK(mnky::midi::isDynamicPort(dynamicPort(mnky::midi::kFirstDynamicPort)));
  CHECK(!mnky::midi::isDynamicPort(Port::UsbIn));
  CHECK(mnky::midi::isInput(dynamicPort(6)) && mnky::midi::isOutput(dynamicPort(6)));
  CHECK(!mnky::midi::isInput(dynamicPort(mnky::midi::kMaxPorts)));
  CHECK(!mnky::midi::isOutput(dynamicPort(mnky::midi::kMaxPorts)));

  // Pipe ownership per id class: each dynamic USB endpoint is Pi-owned, while
  // TRS->TRS stays on the Pico.
  CHECK(!mnky::midi::piOwnsPipeChain(Port::TrsIn1, Port::TrsOut1));
  CHECK(!mnky::midi::piOwnsPipeChain(Port::TrsIn2, Port::TrsOut2));
  CHECK(mnky::midi::piOwnsPipeChain(Port::UsbIn, Port::TrsOut1));
  CHECK(mnky::midi::piOwnsPipeChain(Port::TrsIn1, Port::UsbOut));
  CHECK(mnky::midi::piOwnsPipeChain(dynamicPort(6), Port::TrsOut1));
  CHECK(mnky::midi::piOwnsPipeChain(Port::TrsIn1, dynamicPort(7)));
  CHECK(mnky::midi::piOwnsPipeChain(dynamicPort(6), dynamicPort(7)));
  CHECK(!mnky::midi::piOwnsPipeChain(Port::TrsIn2, Port::TrsOut1));

  Router router;
  const Port source = dynamicPort(6);
  const Port destination = dynamicPort(7);
  CHECK(router.setRoute(source, destination, true));
  CHECK(router.setRoute(source, Port::TrsOut1, true));
  router.setChannelMask(source, 0x0001);  // channel 1 only
  router.setTranspose(source, 12);

  std::vector<std::pair<Port, mnky::midi::MidiEvent>> log;
  const auto capture = [&log](Port dst, const mnky::midi::MidiEvent& ev) {
    log.push_back({dst, ev});
  };
  mnky::midi::MidiEvent note;
  note.source = source;
  note.size = 3;
  note.data[0] = 0x90;
  note.data[1] = 60;
  note.data[2] = 100;
  router.route(note, capture);
  CHECK(log.size() == 2);
  // Delivery runs in ascending port id: TrsOut1 (3) before dynamicOut (7).
  bool sawTrs = false;
  bool sawDynamic = false;
  for (const auto& delivery : log) {
    CHECK(delivery.second.data[1] == 72);  // source transpose applies to both
    if (delivery.first == Port::TrsOut1) sawTrs = true;
    if (delivery.first == destination) sawDynamic = true;
  }
  CHECK(sawTrs && sawDynamic);

  log.clear();
  note.data[0] = 0x91;  // channel 2: the source mask blocks it
  router.route(note, capture);
  CHECK(log.empty());

  // Dynamic routes carry chains too; a per-pipe channel scope gates them.
  CHECK(router.setRoute(source, Port::TrsOut1, false));
  mnky::midi::PipeConfig pipe;
  pipe.type = mnky::midi::PipeType::Filter;
  pipe.chMask = 0x0001;  // channel 1 only
  pipe.paramCount = 8;   // block every covered kind
  CHECK(router.setPipes(source, destination, &pipe, 1));
  CHECK(router.pipeCount(source, destination) == 1);
  log.clear();
  note.data[0] = 0x90;  // channel 1, in scope: the Filter drops it
  router.route(note, capture);
  CHECK(log.empty());
  // A scoped Chance on a dynamic->fixed route drops only matching channels.
  mnky::midi::PipeConfig chance;
  chance.type = mnky::midi::PipeType::Chance;
  chance.chMask = 0x0001;
  chance.paramCount = 1;
  chance.params[0] = 100;
  CHECK(router.setPipes(source, Port::TrsOut1, &chance, 1));
  CHECK(router.setRoute(source, Port::TrsOut1, true));
  CHECK(router.setRoute(source, destination, false));
  log.clear();
  router.route(note, capture);
  CHECK(log.empty());
  log.clear();
  note.data[0] = 0x91;  // channel 2, outside the Chance scope
  // The source channel mask still blocks channel 2, so lower it for this probe.
  router.setChannelMask(source, Router::kAllChannels);
  router.route(note, capture);
  CHECK(log.size() == 1 && log[0].first == Port::TrsOut1);

  // Out-of-range ids are rejected by every accessor.
  CHECK(!router.setRoute(dynamicPort(mnky::midi::kMaxPorts), Port::TrsOut1, true));
  CHECK(router.channelMask(dynamicPort(mnky::midi::kMaxPorts)) == Router::kAllChannels);
  CHECK(router.transpose(dynamicPort(mnky::midi::kMaxPorts)) == 0);
  pass("dynamic router");
}

// ---------------------------------------------------------------------------
// session shape: dynamic ids round-trip through RouterState/Router
// ---------------------------------------------------------------------------
void sessionDynamicRoundTrip() {
  mnky::session::RouterState state;
  CHECK(state.channels.size() == static_cast<std::size_t>(mnky::session::kSessionMaxPorts));
  CHECK(state.channels[mnky::midi::kFirstDynamicPort] == Router::kAllChannels);
  CHECK(mnky::midi::kMaxPorts == mnky::session::kSessionMaxPorts);

  state.devices = {{6, "usb:Arturia KeyStep"}, {7, "usb:Elektron Digitakt"}};
  state.channels[6] = 0x000F;
  state.transpose[7] = -2;
  state.routes.push_back(mnky::session::RouteState{6, 7, true, {}});
  state.routes.push_back(
      mnky::session::RouteState{static_cast<int>(Port::TrsIn1), 7, true, {}});

  // Identity binding is a name lookup: ids may move, the route survives.
  PortRegistry registry;
  CHECK(registry.connect("usb:Elektron Digitakt", false, true) == 6);
  CHECK(registry.connect("usb:Arturia KeyStep", true, false) == 7);
  const auto binding = registry.bindSession(state.devices);
  mnky::midi::remapRouteIds(binding.ids, state.routes);
  mnky::midi::remapSessionPorts(binding.ids, state.channels, state.transpose);
  CHECK(state.routes[0].src == 7 && state.routes[0].dst == 6);
  CHECK(state.channels[7] == 0x000F);
  CHECK(state.transpose[6] == -2);

  // Apply the bound snapshot to a Router, then capture it back into the same
  // session shape: dynamic src/dst are ordinary ints, format untouched.
  Router router;
  for (const auto& route : state.routes) {
    CHECK(router.setRoute(static_cast<Port>(route.src),
                          static_cast<Port>(route.dst), route.on));
  }
  for (int port = 0; port < mnky::midi::kMaxPorts; ++port) {
    const auto id = static_cast<Port>(port);
    router.setChannelMask(id, state.channels[static_cast<std::size_t>(port)]);
    router.setTranspose(id, state.transpose[static_cast<std::size_t>(port)]);
  }
  CHECK(router.routeEnabled(dynamicPort(7), dynamicPort(6)));
  CHECK(router.routeEnabled(Port::TrsIn1, dynamicPort(6)));
  CHECK(router.channelMask(dynamicPort(7)) == 0x000F);
  CHECK(router.transpose(dynamicPort(6)) == -2);

  mnky::session::RouterState captured;
  for (int source = 0; source < mnky::midi::kMaxPorts; ++source) {
    for (int destination = 0; destination < mnky::midi::kMaxPorts; ++destination) {
      const auto src = static_cast<Port>(source);
      const auto dst = static_cast<Port>(destination);
      if (!mnky::midi::isInput(src) || !mnky::midi::isOutput(dst)) continue;
      if (router.routeEnabled(src, dst)) {
        captured.routes.push_back(mnky::session::RouteState{source, destination, true, {}});
      }
    }
  }
  for (int port = 0; port < mnky::midi::kMaxPorts; ++port) {
    captured.channels[static_cast<std::size_t>(port)] =
        router.channelMask(static_cast<Port>(port));
    captured.transpose[static_cast<std::size_t>(port)] =
        router.transpose(static_cast<Port>(port));
  }
  const auto byRoute = [](const mnky::session::RouteState& a,
                          const mnky::session::RouteState& b) {
    if (a.src != b.src) return a.src < b.src;
    return a.dst < b.dst;
  };
  std::sort(captured.routes.begin(), captured.routes.end(), byRoute);
  std::sort(state.routes.begin(), state.routes.end(), byRoute);
  CHECK(captured.routes == state.routes);
  CHECK(captured.channels == state.channels);
  CHECK(captured.transpose == state.transpose);
  pass("session dynamic round-trip");
}

#if defined(MNKY_MIDI_SESSION_TEST)
// The real session writer/reader, built only by the standalone midi test (the
// root module test links mnky_midi alone). It proves the unchanged JSON format
// carries dynamic routes, masks, transpose and device identities.
void sessionFileRoundTrip() {
  mnky::session::Snapshot original;
  original.router.devices = {{6, "usb:Arturia KeyStep"}, {7, "usb:Elektron Digitakt"}};
  original.router.channels[6] = 0x000F;
  original.router.transpose[7] = -2;
  mnky::session::MidiPipe scopedPipe;
  scopedPipe.type = mnky::session::MidiPipeType::Chance;
  scopedPipe.chMask = 0x000F;  // channels 1-4 only
  scopedPipe.params = {50};
  original.router.routes.push_back(
      mnky::session::RouteState{6, 7, true, {scopedPipe}});
  original.router.routes.push_back(
      mnky::session::RouteState{static_cast<int>(Port::TrsIn1), 7, true, {}});

  const auto path = std::filesystem::temp_directory_path() /
                    "mnky-midi-registry-roundtrip.json";
  CHECK(mnky::session::save(path.string(), original));
  const auto loaded = mnky::session::load(path.string());
  CHECK(loaded.ok);
  CHECK(loaded.snapshot.router == original.router);
  // The frozen 'chm' field survives the real writer/reader unchanged.
  CHECK(loaded.snapshot.router.routes[0].pipes.size() == 1);
  CHECK(loaded.snapshot.router.routes[0].pipes[0].chMask == 0x000F);
  std::error_code error;
  std::filesystem::remove(path, error);

  // The loaded file is bound by name to a registry whose enumeration order
  // changed, then applied to a live Router.
  PortRegistry registry;
  CHECK(registry.connect("usb:Elektron Digitakt", false, true) == 6);
  CHECK(registry.connect("usb:Arturia KeyStep", true, false) == 7);
  mnky::session::RouterState state = loaded.snapshot.router;
  const auto binding = registry.bindSession(state.devices);
  mnky::midi::remapRouteIds(binding.ids, state.routes);
  mnky::midi::remapSessionPorts(binding.ids, state.channels, state.transpose);
  CHECK(state.routes[0].src == 7 && state.routes[0].dst == 6);
  CHECK(state.channels[7] == 0x000F && state.transpose[6] == -2);

  Router router;
  for (const auto& route : state.routes) {
    CHECK(router.setRoute(static_cast<Port>(route.src),
                          static_cast<Port>(route.dst), route.on));
  }
  CHECK(router.routeEnabled(dynamicPort(7), dynamicPort(6)));
  CHECK(router.routeEnabled(Port::TrsIn1, dynamicPort(6)));
  pass("session file round-trip");
}
#endif

}  // namespace

int main() {
  registryLifecycle();
  registryHotPlug();
  nameRebind();
  missingDeviceDormancy();
  dynamicRouter();
  sessionDynamicRoundTrip();
#if defined(MNKY_MIDI_SESSION_TEST)
  sessionFileRoundTrip();
#endif

  if (g_failures != 0) {
    std::fprintf(stderr, "FAILURES %d\n", g_failures);
    return 1;
  }
  std::printf("PASS test_registry\n");
  return 0;
}
