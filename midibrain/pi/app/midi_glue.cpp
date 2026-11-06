#include "midi_glue.hpp"

#include "pipe_ui.hpp"

#include <algorithm>
#include <cstddef>
#include <map>
#include <utility>

#include "audio_backend.hpp"
#include "logging.hpp"

namespace mnky::app {
namespace {
// Bounded so a stalled link thread cannot grow the queue without limit; the
// oldest deliveries are discarded first, which is what a MIDI hub has to do.
constexpr std::size_t kMaxPendingDeliveries = 2048;
// One RtMidi port name -> the per-device name the System audio inventory uses
// (audio_backend.hpp). Calling the shared helper with a single name reuses its
// grouping rule instead of forking a second copy of it here.
std::string portDeviceName(const std::string& portName) {
  const auto names = midiDeviceNamesFromPortNames({portName});
  return names.empty() ? std::string() : names.front();
}

// Same 3x3 slot order as midi::Router::pipeIndex(); -1 when either side is a
// dynamic USB id (those chains live in dynamicRoutePipes_).
int routeSlot(midi::Port source, midi::Port destination) {
  if (!midi::isFixedInput(source) || !midi::isFixedOutput(destination)) return -1;
  const int in = source == midi::Port::TrsIn1 ? 0 : (source == midi::Port::TrsIn2 ? 1 : 2);
  const int out = destination == midi::Port::TrsOut1
                      ? 0
                      : (destination == midi::Port::TrsOut2 ? 1 : 2);
  return in * 3 + out;
}

// Sparse-chain key for a valid route that is not one of the fixed 3x3 pairs,
// matching midi::Router's dynamic key. -1 when the pair is not routable.
int dynamicRouteKey(midi::Port source, midi::Port destination) {
  if (!midi::isInput(source) || !midi::isOutput(destination)) return -1;
  if (routeSlot(source, destination) >= 0) return -1;
  return midi::portId(source) * midi::kMaxPorts + midi::portId(destination);
}

// The wire limits are midifx.hpp's (8 pipes, 10 positional parameters). A chain
// outside them is refused by Router::setPipes, so refuse it here too instead of
// storing a list the engine never accepted.
bool sanitizePipes(const std::vector<session::MidiPipe>& input,
                   std::vector<session::MidiPipe>& output) {
  if (input.size() > midi::kMaxRoutePipes) return false;
  output.clear();
  output.reserve(input.size());
  for (const auto& pipe : input) {
    if (!pipeui::isKnown(static_cast<int>(pipe.type))) return false;
    if (pipe.params.size() > midi::kMaxPipeParams) return false;
    output.push_back(pipe);
  }
  return true;
}

bool routerPipes(const std::vector<session::MidiPipe>& pipes,
                 std::array<midi::PipeConfig, midi::kMaxRoutePipes>& configs,
                 std::size_t& count) {
  count = pipes.size();
  for (std::size_t index = 0; index < pipes.size(); ++index) {
    const auto& pipe = pipes[index];
    if (!pipeui::isKnown(static_cast<int>(pipe.type))) return false;
    if (pipe.params.size() > midi::kMaxPipeParams) return false;
    auto& config = configs[index];
    config = midi::PipeConfig{};
    config.type = static_cast<midi::PipeType>(static_cast<int>(pipe.type));
    config.bypass = pipe.bypass;
    config.chMask = pipe.chMask;
    config.paramCount = static_cast<std::uint8_t>(pipe.params.size());
    for (std::size_t param = 0; param < pipe.params.size(); ++param) {
      config.params[param] = pipe.params[param];
    }
  }
  return true;
}

// A generator chain's head must have the schema role generator; Router enforces
// the same rule, so refuse it here before storing a chain the engine rejects.
bool generatorHeadAllowed(session::MidiPipeType type) {
  return mnky::midifx::generated::pipeIsGenerator(
      static_cast<std::uint8_t>(static_cast<int>(type)));
}

// Fills a stored pipe's positional parameters from the descriptor defaults so
// an editor write to a later parameter does not silently inherit a different
// fallback than the UI shows.
void fillDefaults(session::MidiPipe& pipe) {
  const auto* spec = pipeui::pipeSpec(static_cast<int>(pipe.type));
  if (spec == nullptr) return;
  const std::size_t count = spec->paramCount;
  if (pipe.params.size() >= count) return;
  pipe.params.resize(count);
  for (std::size_t index = 0; index < count; ++index) {
    if (pipe.params[index] == 0 && spec->params[index].defaultValue != 0) {
      // Only untouched trailing zeros are defaulted; an explicit 0 stays 0.
      // The stored vector has no "set" bitmap, so this mirrors how the session
      // reader treats missing trailing parameters and is documented in NOTES.
      pipe.params[index] = spec->params[index].defaultValue;
    }
  }
}
}  // namespace

bool applyRoutePipesToRouter(midi::Router& router, midi::Port source,
                             midi::Port destination,
                             const std::vector<session::MidiPipe>& pipes) {
  std::vector<session::MidiPipe> sanitized;
  if (!sanitizePipes(pipes, sanitized)) return false;
  std::array<midi::PipeConfig, midi::kMaxRoutePipes> configs;
  std::size_t count = 0;
  if (!routerPipes(sanitized, configs, count)) return false;
  return router.setPipes(source, destination, configs.data(), count);
}

MidiGlue::MidiGlue(bool openVirtualPorts) : virtualPorts_(openVirtualPorts) {}

MidiGlue::~MidiGlue() {
  close();
}

bool MidiGlue::open() {
  usb_.setInputCallback([this](const midi::MidiEvent& event) { handleInput(event); });
  return refreshDevices();
}

void MidiGlue::close() {
  usb_.closeAll();
  const std::lock_guard<std::mutex> lock(lock_);
  devicesOpen_ = false;
}

bool MidiGlue::refreshDevices() {
  struct PortRef {
    unsigned int index;
    std::string identity;
  };
  const auto collect = [](const std::vector<std::string>& names,
                          std::vector<PortRef>& ports,
                          std::vector<std::string>& identities) {
    for (unsigned int index = 0; index < names.size(); ++index) {
      const std::string device = portDeviceName(names[index]);
      if (device.empty()) continue;
      // Never register our own published virtual port as a device: a refresh
      // enumerates while that port is open, and a dynamic loopback would feed
      // UsbIn back into the matrix.
      if (device == midi::UsbMidi::kVirtualPortName) continue;
      const std::string identity = midi::usbIdentity(device);
      ports.push_back(PortRef{index, identity});
      if (std::find(identities.begin(), identities.end(), identity) ==
          identities.end()) {
        identities.push_back(identity);
      }
    }
  };

  std::vector<PortRef> inputPorts;
  std::vector<PortRef> outputPorts;
  std::vector<std::string> inputIdentities;
  std::vector<std::string> outputIdentities;
  collect(usb_.listInputs(), inputPorts, inputIdentities);
  collect(usb_.listOutputs(), outputPorts, outputIdentities);

  // The inventory signature is the RtMidi index + grouped device name in both
  // directions: a hot-plug changes it, an unchanged poll does no port churn.
  std::vector<std::string> inventory;
  inventory.reserve(inputPorts.size() + outputPorts.size());
  for (const auto& port : inputPorts) {
    inventory.push_back("in:" + std::to_string(port.index) + ":" + port.identity);
  }
  for (const auto& port : outputPorts) {
    inventory.push_back("out:" + std::to_string(port.index) + ":" + port.identity);
  }

  bool changed = false;
  bool wasOpen = false;
  std::map<std::string, int> idByIdentity;
  {
    const std::lock_guard<std::mutex> lock(lock_);
    changed = inventory != deviceInventory_;
    wasOpen = devicesOpen_;
    registry_.syncDevices(inputIdentities, outputIdentities);
    deviceInventory_ = inventory;
    for (const auto& entry : registry_.entries()) {
      idByIdentity[entry.identity] = entry.id;
    }
  }

  if (changed || !wasOpen) {
    usb_.closeAll();
    for (const auto& port : inputPorts) {
      const auto found = idByIdentity.find(port.identity);
      if (found == idByIdentity.end()) continue;
      (void)usb_.openDeviceInput(port.index,
                                 static_cast<std::uint8_t>(found->second));
    }
    for (const auto& port : outputPorts) {
      const auto found = idByIdentity.find(port.identity);
      if (found == idByIdentity.end()) continue;
      (void)usb_.openDeviceOutput(port.index,
                                  static_cast<std::uint8_t>(found->second));
    }
    if (virtualPorts_) (void)usb_.openVirtualPorts(midi::UsbMidi::kVirtualPortName);
    const std::lock_guard<std::mutex> lock(lock_);
    devicesOpen_ = true;
  }

  std::size_t connected = 0;
  {
    const std::lock_guard<std::mutex> lock(lock_);
    connected = registry_.connectedCount();
  }
  std::string summary = "USB " + std::to_string(usb_.inputCount()) + " in / " +
                        std::to_string(usb_.outputCount()) + " out, " +
                        std::to_string(connected) + " device(s)";
  if (virtualPorts_) {
    summary += std::string(" (virtual \"") + midi::UsbMidi::kVirtualPortName + "\")";
  }
  if (!usb_.lastError().empty()) summary += " (" + usb_.lastError() + ")";
  {
    const std::lock_guard<std::mutex> lock(lock_);
    description_ = summary;
  }
  if (changed || !wasOpen) log("midi: " + summary);
  return usb_.inputCount() > 0 || usb_.outputCount() > 0;
}

std::string MidiGlue::describe() const {
  const std::lock_guard<std::mutex> lock(lock_);
  return description_;
}

std::vector<MidiGlue::UsbPortInfo> MidiGlue::usbPorts() const {
  std::vector<UsbPortInfo> ports;
  const std::lock_guard<std::mutex> lock(lock_);
  for (const auto& entry : registry_.entries()) {
    UsbPortInfo info;
    info.id = entry.id;
    info.identity = entry.identity;
    info.name = midi::usbDeviceName(entry.identity);
    info.input = entry.input;
    info.output = entry.output;
    info.connected = entry.connected;
    ports.push_back(std::move(info));
  }
  return ports;
}

std::size_t MidiGlue::usbPortCount() const {
  const std::lock_guard<std::mutex> lock(lock_);
  return registry_.connectedCount();
}

std::size_t MidiGlue::bindSessionRouter(session::RouterState& state) {
  midi::PortRegistry::SessionBinding binding;
  {
    const std::lock_guard<std::mutex> lock(lock_);
    binding = registry_.bindSession(state.devices);
  }
  midi::remapRouteIds(binding.ids, state.routes);
  midi::remapSessionPorts(binding.ids, state.channels, state.transpose);
  std::map<int, std::string> live;
  for (const auto& [storedId, identity] : state.devices) {
    const auto mapped = binding.ids.find(storedId);
    live[mapped == binding.ids.end() ? storedId : mapped->second] = identity;
  }
  state.devices = std::move(live);
  return binding.ids.size();
}

void MidiGlue::captureSessionDevices(session::RouterState& state) const {
  const std::lock_guard<std::mutex> lock(lock_);
  state.devices = registry_.sessionDevices();
}

session::RouterState MidiGlue::captureSessionRouter() const {
  session::RouterState state;
  const std::lock_guard<std::mutex> lock(lock_);
  for (int source = 0; source < midi::kMaxPorts; ++source) {
    for (int destination = 0; destination < midi::kMaxPorts; ++destination) {
      const auto src = static_cast<midi::Port>(source);
      const auto dst = static_cast<midi::Port>(destination);
      if (!midi::isInput(src) || !midi::isOutput(dst)) continue;
      if (!router_.routeEnabled(src, dst)) continue;
      std::vector<session::MidiPipe> pipes;
      const int slot = routeSlot(src, dst);
      if (slot >= 0) {
        pipes = routePipes_[static_cast<std::size_t>(slot)];
      } else {
        const auto found = dynamicRoutePipes_.find(dynamicRouteKey(src, dst));
        if (found != dynamicRoutePipes_.end()) pipes = found->second;
      }
      state.routes.push_back(
          session::RouteState{source, destination, true, std::move(pipes)});
    }
  }
  for (int port = 0; port < midi::kMaxPorts; ++port) {
    const auto id = static_cast<midi::Port>(port);
    state.channels[static_cast<std::size_t>(port)] = router_.channelMask(id);
    state.transpose[static_cast<std::size_t>(port)] = router_.transpose(id);
  }
  // Generator routes carry no input, so they are captured straight from the
  // pseudo-source store (the loop above only sees real input->output pairs).
  for (int destination = 0; destination < midi::kMaxPorts; ++destination) {
    const auto dst = static_cast<midi::Port>(destination);
    if (!midi::isOutput(dst)) continue;
    if (!generatorEnabled_[static_cast<std::size_t>(destination)] &&
        generatorPipes_[static_cast<std::size_t>(destination)].empty()) {
      continue;
    }
    state.routes.push_back(session::RouteState{
        session::kSrcGenerator, destination,
        generatorEnabled_[static_cast<std::size_t>(destination)],
        generatorPipes_[static_cast<std::size_t>(destination)]});
  }
  state.devices = registry_.sessionDevices();
  return state;
}

void MidiGlue::applySessionRouter(const session::RouterState& input) {
  session::RouterState state = input;
  (void)bindSessionRouter(state);
  {
    const std::lock_guard<std::mutex> lock(lock_);
    for (int source = 0; source < midi::kMaxPorts; ++source) {
      for (int destination = 0; destination < midi::kMaxPorts; ++destination) {
        (void)router_.setRoute(static_cast<midi::Port>(source),
                               static_cast<midi::Port>(destination), false);
      }
    }
    router_.clearPipes();
    generatorPipes_ = {};
    generatorEnabled_ = {};
    for (const auto& route : state.routes) {
      if (route.src == session::kSrcGenerator) {
        // Generator routes have no input port. Store the chain for both engine
        // owners; only a Pi-owned destination can be applied to Router here.
        const auto destination = static_cast<midi::Port>(route.dst);
        if (!midi::isOutput(destination)) continue;
        const auto index = static_cast<std::size_t>(route.dst);
        std::vector<session::MidiPipe> pipes;
        if (!sanitizePipes(route.pipes, pipes)) pipes.clear();
        generatorPipes_[index] = std::move(pipes);
        generatorEnabled_[index] = route.on;
        if (midi::piOwnsPipeChain(session::kSrcGenerator, destination)) {
          (void)router_.setGenerator(destination, route.on);
          std::array<midi::PipeConfig, midi::kMaxRoutePipes> configs;
          std::size_t count = 0;
          if (routerPipes(generatorPipes_[index], configs, count)) {
            (void)router_.setGeneratorPipes(destination, configs.data(), count);
          }
        }
        continue;
      }
      if (route.src == session::kSrcOutputInsert) continue;  // not exposed yet
      const auto source = static_cast<midi::Port>(route.src);
      const auto destination = static_cast<midi::Port>(route.dst);
      (void)router_.setRoute(source, destination, route.on);
      if (!route.pipes.empty())
        (void)applyRoutePipesToRouter(router_, source, destination, route.pipes);
    }
    for (int port = 0; port < midi::kMaxPorts; ++port) {
      const auto id = static_cast<midi::Port>(port);
      router_.setChannelMask(id, state.channels[static_cast<std::size_t>(port)]);
      router_.setTranspose(id, state.transpose[static_cast<std::size_t>(port)]);
    }
  }
  setRoutePipeLists(state.routes);
}

void MidiGlue::handleInput(const midi::MidiEvent& event) {
  midi::Router::PipeDiagnostics rejected;
  {
    const std::lock_guard<std::mutex> lock(lock_);
    recordMonitor(event.source, false, event.data, event.size);
    router_.route(event, [this](midi::Port destination, const midi::MidiEvent& routed) {
      if (outbox_.size() >= kMaxPendingDeliveries) outbox_.pop_front();
      outbox_.push_back(Delivery{destination, routed});
      ++routed_;
    });
    rejected = router_.pipeDiagnostics();
  }
  if (rejected.rejected != 0 && rejected.rejected != loggedPipeRejections_) {
    loggedPipeRejections_ = rejected.rejected;
    log(std::string("midi: pipe chain rejected at pipe ") +
        std::to_string(rejected.lastRejectPipe) + ": " +
        (rejected.lastRejectReason != nullptr ? rejected.lastRejectReason
                                              : "unknown reason"));
  }

  {
    const std::lock_guard<std::mutex> lock(learnLock_);
    (void)learn_.dispatch(event);
  }

  if (learnCaptureArmed_.load(std::memory_order_relaxed) && event.size == 3 &&
      (event.data[0] & 0xF0u) == 0xB0u) {
    learnCaptureCc_.store(static_cast<int>(event.data[1]), std::memory_order_relaxed);
    learnCaptureCh_.store(static_cast<int>(event.data[0] & 0x0Fu) + 1,
                          std::memory_order_relaxed);
    learnCaptureValue_.store(static_cast<int>(event.data[2] & 0x7Fu),
                             std::memory_order_relaxed);
    learnCaptureSeq_.fetch_add(1, std::memory_order_relaxed);
  }
}

void MidiGlue::pump(const TrsSink& trsSink) {
  std::deque<Delivery> pending;
  {
    const std::lock_guard<std::mutex> lock(lock_);
    // NoteLength (Wave 1) offs are due on the time line, not on an input
    // event; flush them before the queue swap so the link thread emits them in
    // the same pass as every routed message. The Router's time base is the
    // same millisecond clock as MidiEvent::stampUs.
    const auto nowMs = static_cast<std::uint32_t>(midi::piNowUs() / 1000u);
    router_.tick(nowMs, [this](midi::Port destination, const midi::MidiEvent& routed) {
      if (outbox_.size() >= kMaxPendingDeliveries) outbox_.pop_front();
      outbox_.push_back(Delivery{destination, routed});
      ++routed_;
    });
    pending.swap(outbox_);
  }
  for (const auto& delivery : pending) {
    bool sent = false;
    if (delivery.destination == midi::Port::UsbOut) {
      usb_.send(delivery.event);
      sent = true;
    } else if (midi::isDynamicPort(delivery.destination)) {
      // Per-device USB output: only that device's ports receive the message.
      sent = usb_.sendToDevice(
          static_cast<std::uint8_t>(midi::portId(delivery.destination)),
          delivery.event);
    } else if (trsSink) {
      sent = trsSink(delivery.destination, delivery.event);
    }
    if (!sent) continue;
    const std::lock_guard<std::mutex> lock(lock_);
    recordMonitor(delivery.destination, true, delivery.event.data, delivery.event.size);
  }
}

void MidiGlue::sendUsbClock(std::uint8_t byte) {
  midi::MidiEvent event;
  event.source = midi::Port::UsbOut;
  event.size = 1;
  event.data[0] = byte;
  event.stampUs = midi::piNowUs();
  usb_.send(event);
  const std::lock_guard<std::mutex> lock(lock_);
  recordMonitor(midi::Port::UsbOut, true, event.data, event.size);
}

void MidiGlue::recordMonitor(midi::Port port, bool outbound, const std::uint8_t* data,
                             std::uint8_t size) {
  const auto portIndex = static_cast<std::size_t>(port);
  if (portIndex >= static_cast<std::size_t>(midi::kPortCount)) return;
  MonitorEvent event;
  event.port = port;
  event.outbound = outbound;
  event.size = size > 3 ? 3 : size;
  for (std::uint8_t i = 0; i < event.size; ++i) event.data[i] = data[i];
  event.stampUs = midi::piNowUs();
  monitorEvents_[monitorNext_] = event;
  monitorNext_ = (monitorNext_ + 1) % kMonitorEvents;
  if (monitorCount_ < kMonitorEvents) ++monitorCount_;
  ++monitorTotal_;

  auto& counts = monitorCounts_[portIndex];
  if (event.size == 0) {
    ++counts.other;
  } else if (event.size == 1 && (event.data[0] == 0xF8)) {
    ++counts.clock;
  } else if ((event.data[0] & 0xF0u) == 0x90u ||
             (event.data[0] & 0xF0u) == 0x80u) {
    ++counts.notes;
  } else if ((event.data[0] & 0xF0u) == 0xB0u) {
    ++counts.cc;
  } else {
    ++counts.other;
  }
}

void MidiGlue::clearMonitor() {
  const std::lock_guard<std::mutex> lock(lock_);
  monitorNext_ = 0;
  monitorCount_ = 0;
  monitorTotal_ = 0;
  monitorCounts_ = {};
}

MidiGlue::MonitorSnapshot MidiGlue::monitorSnapshot() const {
  MonitorSnapshot snapshot;
  const std::lock_guard<std::mutex> lock(lock_);
  snapshot.count = monitorCount_;
  snapshot.counts = monitorCounts_;
  snapshot.total = monitorTotal_;
  for (std::size_t i = 0; i < monitorCount_; ++i) {
    // Oldest first: the ring writes at monitorNext_ and wraps.
    const std::size_t index =
        (monitorNext_ + kMonitorEvents - monitorCount_ + i) % kMonitorEvents;
    snapshot.events[i] = monitorEvents_[index];
  }
  return snapshot;
}

void MidiGlue::setRoute(midi::Port source, midi::Port destination, bool enabled) {
  {
    const std::lock_guard<std::mutex> lock(lock_);
    router_.setRoute(source, destination, enabled);
  }
  // The Pico evaluates a TRS->TRS chain only for an enabled 0x18 route, so a
  // matrix change re-sends that route's begin/flags (the chain itself is
  // unchanged but the enable bit is not; re-sending the whole chain is the one
  // command shape LinkClient has).
  notifyPipeSync(source, destination);
}

bool MidiGlue::routeEnabled(midi::Port source, midi::Port destination) const {
  const std::lock_guard<std::mutex> lock(lock_);
  return router_.routeEnabled(source, destination);
}

void MidiGlue::setChannelMask(midi::Port source, std::uint16_t mask) {
  const std::lock_guard<std::mutex> lock(lock_);
  router_.setChannelMask(source, mask);
}

std::uint16_t MidiGlue::channelMask(midi::Port source) const {
  const std::lock_guard<std::mutex> lock(lock_);
  return router_.channelMask(source);
}

void MidiGlue::setTranspose(midi::Port source, int semitones) {
  const std::lock_guard<std::mutex> lock(lock_);
  router_.setTranspose(source, semitones);
}

int MidiGlue::transpose(midi::Port source) const {
  const std::lock_guard<std::mutex> lock(lock_);
  return router_.transpose(source);
}

std::vector<session::MidiPipe> MidiGlue::routePipes(midi::Port source,
                                                    midi::Port destination) const {
  const int slot = routeSlot(source, destination);
  const int key = dynamicRouteKey(source, destination);
  if (slot < 0 && key < 0) return {};
  const std::lock_guard<std::mutex> lock(lock_);
  if (slot >= 0) return routePipes_[static_cast<std::size_t>(slot)];
  const auto found = dynamicRoutePipes_.find(key);
  return found == dynamicRoutePipes_.end() ? std::vector<session::MidiPipe>{}
                                           : found->second;
}

bool MidiGlue::routeHasPipes(midi::Port source, midi::Port destination) const {
  const int slot = routeSlot(source, destination);
  const int key = dynamicRouteKey(source, destination);
  if (slot < 0 && key < 0) return false;
  const std::lock_guard<std::mutex> lock(lock_);
  if (slot >= 0) return !routePipes_[static_cast<std::size_t>(slot)].empty();
  const auto found = dynamicRoutePipes_.find(key);
  return found != dynamicRoutePipes_.end() && !found->second.empty();
}

bool MidiGlue::setRoutePipes(midi::Port source, midi::Port destination,
                             const std::vector<session::MidiPipe>& pipes) {
  const int slot = routeSlot(source, destination);
  const int key = dynamicRouteKey(source, destination);
  if (slot < 0 && key < 0) return false;
  std::vector<session::MidiPipe> sanitized;
  if (!sanitizePipes(pipes, sanitized)) return false;
  std::array<midi::PipeConfig, midi::kMaxRoutePipes> configs;
  std::size_t count = 0;
  if (!routerPipes(sanitized, configs, count)) return false;
  {
    const std::lock_guard<std::mutex> lock(lock_);
    if (!router_.setPipes(source, destination, configs.data(), count)) return false;
    if (slot >= 0) {
      routePipes_[static_cast<std::size_t>(slot)] = std::move(sanitized);
    } else {
      dynamicRoutePipes_[key] = std::move(sanitized);
    }
  }
  notifyPipeSync(source, destination);
  return true;
}

bool MidiGlue::addRoutePipe(midi::Port source, midi::Port destination,
                            session::MidiPipeType type) {
  const auto* spec = pipeui::pipeSpec(static_cast<int>(type));
  if (spec == nullptr) return false;
  auto pipes = routePipes(source, destination);
  if (pipes.size() >= midi::kMaxRoutePipes) return false;
  session::MidiPipe pipe;
  pipe.type = type;
  pipe.params.reserve(spec->paramCount);
  for (std::size_t index = 0; index < spec->paramCount; ++index) {
    pipe.params.push_back(spec->params[index].defaultValue);
  }
  pipes.push_back(std::move(pipe));
  return setRoutePipes(source, destination, pipes);
}

bool MidiGlue::removeRoutePipe(midi::Port source, midi::Port destination,
                               int index) {
  auto pipes = routePipes(source, destination);
  if (index < 0 || index >= static_cast<int>(pipes.size())) return false;
  pipes.erase(pipes.begin() + index);
  return setRoutePipes(source, destination, pipes);
}

bool MidiGlue::moveRoutePipe(midi::Port source, midi::Port destination, int index,
                             int delta) {
  auto pipes = routePipes(source, destination);
  if (index < 0 || index >= static_cast<int>(pipes.size())) return false;
  const int target = index + (delta < 0 ? -1 : 1);
  if (target < 0 || target >= static_cast<int>(pipes.size())) return false;
  std::swap(pipes[static_cast<std::size_t>(index)],
            pipes[static_cast<std::size_t>(target)]);
  return setRoutePipes(source, destination, pipes);
}

bool MidiGlue::setRoutePipeBypass(midi::Port source, midi::Port destination,
                                  int index, bool bypass) {
  auto pipes = routePipes(source, destination);
  if (index < 0 || index >= static_cast<int>(pipes.size())) return false;
  pipes[static_cast<std::size_t>(index)].bypass = bypass;
  return setRoutePipes(source, destination, pipes);
}

bool MidiGlue::setRoutePipeParam(midi::Port source, midi::Port destination,
                                 int index, int param, int value) {
  auto pipes = routePipes(source, destination);
  if (index < 0 || index >= static_cast<int>(pipes.size())) return false;
  auto& pipe = pipes[static_cast<std::size_t>(index)];
  const auto* spec = pipeui::pipeSpec(static_cast<int>(pipe.type));
  if (spec == nullptr || param < 0 || param >= static_cast<int>(spec->paramCount)) {
    return false;
  }
  fillDefaults(pipe);
  pipe.params[static_cast<std::size_t>(param)] =
      pipeui::clamp(spec->params[static_cast<std::size_t>(param)], value);
  return setRoutePipes(source, destination, pipes);
}

bool MidiGlue::setRoutePipeChannelMask(midi::Port source, midi::Port destination,
                                       int index, std::uint16_t mask) {
  auto pipes = routePipes(source, destination);
  if (index < 0 || index >= static_cast<int>(pipes.size())) return false;
  pipes[static_cast<std::size_t>(index)].chMask = mask;
  return setRoutePipes(source, destination, pipes);
}

std::uint16_t MidiGlue::routePipeChannelMask(midi::Port source,
                                             midi::Port destination,
                                             int index) const {
  const auto pipes = routePipes(source, destination);
  if (index < 0 || index >= static_cast<int>(pipes.size())) return 0xFFFF;
  return pipes[static_cast<std::size_t>(index)].chMask;
}

bool MidiGlue::generatorRouteEnabled(midi::Port destination) const {
  if (!midi::isOutput(destination)) return false;
  const std::lock_guard<std::mutex> lock(lock_);
  return generatorEnabled_[static_cast<std::size_t>(midi::portId(destination))];
}

bool MidiGlue::setGeneratorRoute(midi::Port destination, bool enabled) {
  if (!midi::isOutput(destination)) return false;
  const auto index = static_cast<std::size_t>(midi::portId(destination));
  {
    const std::lock_guard<std::mutex> lock(lock_);
    generatorEnabled_[index] = enabled;
    if (midi::piOwnsPipeChain(session::kSrcGenerator, destination)) {
      (void)router_.setGenerator(destination, enabled);
    }
  }
  return true;
}

std::vector<session::MidiPipe> MidiGlue::generatorPipes(
    midi::Port destination) const {
  if (!midi::isOutput(destination)) return {};
  const std::lock_guard<std::mutex> lock(lock_);
  return generatorPipes_[static_cast<std::size_t>(midi::portId(destination))];
}

bool MidiGlue::setGeneratorPipes(midi::Port destination,
                                 const std::vector<session::MidiPipe>& pipes) {
  if (!midi::isOutput(destination)) return false;
  std::vector<session::MidiPipe> sanitized;
  if (!sanitizePipes(pipes, sanitized)) return false;
  if (!sanitized.empty() && !generatorHeadAllowed(sanitized.front().type)) {
    return false;
  }
  std::array<midi::PipeConfig, midi::kMaxRoutePipes> configs;
  std::size_t count = 0;
  if (!routerPipes(sanitized, configs, count)) return false;
  {
    const std::lock_guard<std::mutex> lock(lock_);
    if (midi::piOwnsPipeChain(session::kSrcGenerator, destination)) {
      if (!router_.setGeneratorPipes(destination, configs.data(), count)) {
        return false;
      }
    }
    // A Pico-owned destination keeps the chain in the session store only; the
    // link seam does not carry pseudo-source chains in this build.
    generatorPipes_[static_cast<std::size_t>(midi::portId(destination))] =
        std::move(sanitized);
  }
  return true;
}

bool MidiGlue::addGeneratorPipe(midi::Port destination,
                                session::MidiPipeType type) {
  const auto* spec = pipeui::pipeSpec(static_cast<int>(type));
  if (spec == nullptr) return false;
  auto pipes = generatorPipes(destination);
  if (pipes.size() >= midi::kMaxRoutePipes) return false;
  if (pipes.empty() && !generatorHeadAllowed(type)) return false;
  session::MidiPipe pipe;
  pipe.type = type;
  pipe.params.reserve(spec->paramCount);
  for (std::size_t index = 0; index < spec->paramCount; ++index) {
    pipe.params.push_back(spec->params[index].defaultValue);
  }
  pipes.push_back(std::move(pipe));
  return setGeneratorPipes(destination, pipes);
}

bool MidiGlue::removeGeneratorPipe(midi::Port destination, int index) {
  auto pipes = generatorPipes(destination);
  if (index < 0 || index >= static_cast<int>(pipes.size())) return false;
  pipes.erase(pipes.begin() + index);
  return setGeneratorPipes(destination, pipes);
}

bool MidiGlue::moveGeneratorPipe(midi::Port destination, int index, int delta) {
  auto pipes = generatorPipes(destination);
  if (index < 0 || index >= static_cast<int>(pipes.size())) return false;
  const int target = index + (delta < 0 ? -1 : 1);
  if (target < 0 || target >= static_cast<int>(pipes.size())) return false;
  std::swap(pipes[static_cast<std::size_t>(index)],
            pipes[static_cast<std::size_t>(target)]);
  return setGeneratorPipes(destination, pipes);
}

bool MidiGlue::setGeneratorPipeBypass(midi::Port destination, int index,
                                      bool bypass) {
  auto pipes = generatorPipes(destination);
  if (index < 0 || index >= static_cast<int>(pipes.size())) return false;
  pipes[static_cast<std::size_t>(index)].bypass = bypass;
  return setGeneratorPipes(destination, pipes);
}

bool MidiGlue::setGeneratorPipeParam(midi::Port destination, int index,
                                     int param, int value) {
  auto pipes = generatorPipes(destination);
  if (index < 0 || index >= static_cast<int>(pipes.size())) return false;
  auto& pipe = pipes[static_cast<std::size_t>(index)];
  const auto* spec = pipeui::pipeSpec(static_cast<int>(pipe.type));
  if (spec == nullptr || param < 0 ||
      param >= static_cast<int>(spec->paramCount)) {
    return false;
  }
  fillDefaults(pipe);
  pipe.params[static_cast<std::size_t>(param)] =
      pipeui::clamp(spec->params[static_cast<std::size_t>(param)], value);
  return setGeneratorPipes(destination, pipes);
}

bool MidiGlue::setGeneratorPipeChannelMask(midi::Port destination, int index,
                                           std::uint16_t mask) {
  auto pipes = generatorPipes(destination);
  if (index < 0 || index >= static_cast<int>(pipes.size())) return false;
  pipes[static_cast<std::size_t>(index)].chMask = mask;
  return setGeneratorPipes(destination, pipes);
}

std::uint16_t MidiGlue::generatorPipeChannelMask(midi::Port destination,
                                                 int index) const {
  const auto pipes = generatorPipes(destination);
  if (index < 0 || index >= static_cast<int>(pipes.size())) return 0xFFFF;
  return pipes[static_cast<std::size_t>(index)].chMask;
}

void MidiGlue::clearGeneratorRoutes() {
  {
    const std::lock_guard<std::mutex> lock(lock_);
    generatorPipes_ = {};
    generatorEnabled_ = {};
    router_.clearPseudoPipes();
  }
  log("midi: generator routes cleared");
}

void MidiGlue::setRoutePipeLists(const std::vector<session::RouteState>& routes) {
  {
    const std::lock_guard<std::mutex> lock(lock_);
    for (auto& list : routePipes_) list.clear();
    dynamicRoutePipes_.clear();
    generatorPipes_ = {};
    generatorEnabled_ = {};
    for (const auto& route : routes) {
      if (route.src == session::kSrcGenerator) {
        if (!midi::isOutput(static_cast<midi::Port>(route.dst))) continue;
        std::vector<session::MidiPipe> sanitized;
        if (!sanitizePipes(route.pipes, sanitized)) continue;
        const auto index = static_cast<std::size_t>(route.dst);
        generatorPipes_[index] = std::move(sanitized);
        generatorEnabled_[index] = route.on;
        continue;
      }
      if (route.src == session::kSrcOutputInsert) continue;
      const auto source = static_cast<midi::Port>(route.src);
      const auto destination = static_cast<midi::Port>(route.dst);
      const int slot = routeSlot(source, destination);
      const int key = dynamicRouteKey(source, destination);
      if (slot < 0 && key < 0) continue;
      std::vector<session::MidiPipe> sanitized;
      if (!sanitizePipes(route.pipes, sanitized)) continue;
      if (slot >= 0) {
        routePipes_[static_cast<std::size_t>(slot)] = std::move(sanitized);
      } else {
        dynamicRoutePipes_[key] = std::move(sanitized);
      }
    }
  }
  // A project load can remove a chain; re-sending every TRS->TRS pair (including
  // the empty ones) clears it on the Pico instead of leaving stale pipes.
  syncTrsPipes();
}

void MidiGlue::setPipeSync(PipeSyncFn fn) {
  const std::lock_guard<std::mutex> lock(lock_);
  pipeSync_ = std::move(fn);
}

void MidiGlue::syncTrsPipes() {
  for (int source = 0; source < midi::kPortCount; ++source) {
    for (int destination = 0; destination < midi::kPortCount; ++destination) {
      const auto from = static_cast<midi::Port>(source);
      const auto to = static_cast<midi::Port>(destination);
      if (!midi::isInput(from) || !midi::isOutput(to)) continue;
      if (midi::piOwnsPipeChain(from, to)) continue;
      notifyPipeSync(from, to);
    }
  }
}

void MidiGlue::notifyPipeSync(midi::Port source, midi::Port destination) {
  if (midi::piOwnsPipeChain(source, destination)) return;
  const int slot = routeSlot(source, destination);
  if (slot < 0) return;
  PipeSyncFn fn;
  std::vector<session::MidiPipe> pipes;
  bool enabled = false;
  {
    const std::lock_guard<std::mutex> lock(lock_);
    fn = pipeSync_;
    pipes = routePipes_[static_cast<std::size_t>(slot)];
    enabled = router_.routeEnabled(source, destination);
  }
  if (fn) fn(source, destination, pipes, enabled);
}

std::vector<std::string> MidiGlue::routePipeLearnTargets() const {
  const std::lock_guard<std::mutex> lock(lock_);
  std::vector<std::string> targets;
  for (int source = 0; source < 3; ++source) {
    for (int destination = 0; destination < 3; ++destination) {
      const auto& pipes = routePipes_[static_cast<std::size_t>(source * 3 + destination)];
      for (std::size_t pipe = 0; pipe < pipes.size(); ++pipe) {
        const int count =
            pipeui::paramCount(static_cast<int>(pipes[pipe].type));
        for (int param = 0; param < count; ++param) {
          targets.push_back(pipeui::learnTargetPath(
              source, destination, static_cast<int>(pipe), param));
        }
      }
    }
  }
  return targets;
}

std::size_t MidiGlue::inputCount() const {
  return usb_.inputCount();
}

std::size_t MidiGlue::outputCount() const {
  return usb_.outputCount();
}

std::uint64_t MidiGlue::routedCount() const {
  const std::lock_guard<std::mutex> lock(lock_);
  return routed_;
}

void MidiGlue::registerLearnTarget(const std::string& path,
                                   midi::LearnEngine::Setter setter) {
  const std::lock_guard<std::mutex> lock(learnLock_);
  learn_.registerTarget(path, std::move(setter));
}

std::vector<midi::LearnBinding> MidiGlue::learnBindings() const {
  const std::lock_guard<std::mutex> lock(learnLock_);
  return learn_.listBindings();
}

bool MidiGlue::addLearnBinding(int cc, int ch, std::string target, int track) {
  const std::lock_guard<std::mutex> lock(learnLock_);
  return learn_.addBinding(cc, ch, std::move(target), track);
}

bool MidiGlue::removeLearnBinding(int cc, int ch) {
  const std::lock_guard<std::mutex> lock(learnLock_);
  return learn_.removeBinding(cc, ch);
}

void MidiGlue::clearLearnBindings() {
  const std::lock_guard<std::mutex> lock(learnLock_);
  learn_.clear();
}

void MidiGlue::setLearnBindings(const std::vector<midi::LearnBinding>& bindings) {
  const std::lock_guard<std::mutex> lock(learnLock_);
  learn_.clear();
  for (const auto& binding : bindings) {
    (void)learn_.addBinding(binding.cc, binding.ch, binding.target, binding.track);
  }
}

void MidiGlue::armLearnCapture(bool armed) {
  if (armed) learnCaptureSeq_.fetch_add(1, std::memory_order_relaxed);
  learnCaptureArmed_.store(armed, std::memory_order_relaxed);
}

MidiGlue::LearnCapture MidiGlue::learnCapture() const {
  LearnCapture capture;
  capture.armed = learnCaptureArmed_.load(std::memory_order_relaxed);
  capture.seq = learnCaptureSeq_.load(std::memory_order_relaxed);
  capture.cc = learnCaptureCc_.load(std::memory_order_relaxed);
  capture.ch = learnCaptureCh_.load(std::memory_order_relaxed);
  capture.value = learnCaptureValue_.load(std::memory_order_relaxed);
  return capture;
}

void MidiGlue::panic() {
  const std::lock_guard<std::mutex> lock(lock_);
  for (int channel = 1; channel <= 16; ++channel) {
    midi::MidiEvent event;
    event.source = midi::Port::UsbOut;
    event.size = 3;
    event.data[0] = static_cast<std::uint8_t>(0xB0u | static_cast<unsigned>(channel - 1));
    event.data[1] = 123;
    event.data[2] = 0;
    event.stampUs = midi::piNowUs();
    for (const auto destination :
         {midi::Port::UsbOut, midi::Port::TrsOut1, midi::Port::TrsOut2}) {
      if (outbox_.size() >= kMaxPendingDeliveries) outbox_.pop_front();
      event.source = destination;
      outbox_.push_back(Delivery{destination, event});
    }
  }
  log("midi: panic sent (CC123, 16 channels, USB + TRS)");
}

}  // namespace mnky::app
