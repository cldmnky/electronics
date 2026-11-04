#include "midi_glue.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

#include "logging.hpp"

namespace mnky::app {
namespace {
// Bounded so a stalled link thread cannot grow the queue without limit; the
// oldest deliveries are discarded first, which is what a MIDI hub has to do.
constexpr std::size_t kMaxPendingDeliveries = 2048;
// Same 3x3 slot order as midi::Router::pipeIndex().
int routeSlot(midi::Port source, midi::Port destination) {
  if (!midi::isInput(source) || !midi::isOutput(destination)) return -1;
  const int in = source == midi::Port::TrsIn1 ? 0 : (source == midi::Port::TrsIn2 ? 1 : 2);
  const int out = destination == midi::Port::TrsOut1
                      ? 0
                      : (destination == midi::Port::TrsOut2 ? 1 : 2);
  return in * 3 + out;
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
    if (!isKnownPipeType(static_cast<int>(pipe.type))) return false;
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
    if (!isKnownPipeType(static_cast<int>(pipe.type))) return false;
    if (pipe.params.size() > midi::kMaxPipeParams) return false;
    auto& config = configs[index];
    config = midi::PipeConfig{};
    config.type = static_cast<midi::PipeType>(static_cast<int>(pipe.type));
    config.bypass = pipe.bypass;
    config.paramCount = static_cast<std::uint8_t>(pipe.params.size());
    for (std::size_t param = 0; param < pipe.params.size(); ++param) {
      config.params[param] = pipe.params[param];
    }
  }
  return true;
}

// Fills a stored pipe's positional parameters from the descriptor defaults so
// an editor write to a later parameter does not silently inherit a different
// fallback than the UI shows.
void fillDefaults(session::MidiPipe& pipe) {
  const auto specs = pipeParamSpecs(pipe.type);
  if (pipe.params.size() >= specs.size()) return;
  pipe.params.resize(specs.size());
  for (std::size_t index = 0; index < specs.size(); ++index) {
    if (index >= pipe.params.size()) break;
    if (pipe.params[index] == 0 && specs[index].defaultValue != 0) {
      // Only untouched trailing zeros are defaulted; an explicit 0 stays 0.
      // The stored vector has no "set" bitmap, so this mirrors how the session
      // reader treats missing trailing parameters and is documented in NOTES.
      pipe.params[index] = specs[index].defaultValue;
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
  const auto inputs = usb_.openAll(virtualPorts_);
  const auto outputs = usb_.outputAll(virtualPorts_);
  std::string summary = "USB " + std::to_string(inputs) + " in / " +
                        std::to_string(outputs) + " out";
  if (virtualPorts_) {
    summary += std::string(" (virtual \"") + midi::UsbMidi::kVirtualPortName + "\")";
  }
  if (!usb_.lastError().empty()) summary += " (" + usb_.lastError() + ")";
  {
    const std::lock_guard<std::mutex> lock(lock_);
    description_ = summary;
  }
  log("midi: " + summary);
  return inputs > 0 || outputs > 0;
}

void MidiGlue::close() {
  usb_.closeAll();
}

std::string MidiGlue::describe() const {
  const std::lock_guard<std::mutex> lock(lock_);
  return description_;
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
  if (slot < 0) return {};
  const std::lock_guard<std::mutex> lock(lock_);
  return routePipes_[static_cast<std::size_t>(slot)];
}

bool MidiGlue::routeHasPipes(midi::Port source, midi::Port destination) const {
  const int slot = routeSlot(source, destination);
  if (slot < 0) return false;
  const std::lock_guard<std::mutex> lock(lock_);
  return !routePipes_[static_cast<std::size_t>(slot)].empty();
}

bool MidiGlue::setRoutePipes(midi::Port source, midi::Port destination,
                             const std::vector<session::MidiPipe>& pipes) {
  const int slot = routeSlot(source, destination);
  if (slot < 0) return false;
  std::vector<session::MidiPipe> sanitized;
  if (!sanitizePipes(pipes, sanitized)) return false;
  std::array<midi::PipeConfig, midi::kMaxRoutePipes> configs;
  std::size_t count = 0;
  if (!routerPipes(sanitized, configs, count)) return false;
  {
    const std::lock_guard<std::mutex> lock(lock_);
    if (!router_.setPipes(source, destination, configs.data(), count)) return false;
    routePipes_[static_cast<std::size_t>(slot)] = std::move(sanitized);
  }
  notifyPipeSync(source, destination);
  return true;
}

bool MidiGlue::addRoutePipe(midi::Port source, midi::Port destination,
                            session::MidiPipeType type) {
  if (!isKnownPipeType(static_cast<int>(type))) return false;
  auto pipes = routePipes(source, destination);
  if (pipes.size() >= midi::kMaxRoutePipes) return false;
  session::MidiPipe pipe;
  pipe.type = type;
  const auto specs = pipeParamSpecs(type);
  pipe.params.reserve(specs.size());
  for (const auto& spec : specs) pipe.params.push_back(spec.defaultValue);
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
  const auto specs = pipeParamSpecs(pipe.type);
  if (param < 0 || param >= static_cast<int>(specs.size())) return false;
  const auto& spec = specs[static_cast<std::size_t>(param)];
  fillDefaults(pipe);
  pipe.params[static_cast<std::size_t>(param)] = clampPipeParam(spec, value);
  return setRoutePipes(source, destination, pipes);
}

void MidiGlue::setRoutePipeLists(const std::vector<session::RouteState>& routes) {
  {
    const std::lock_guard<std::mutex> lock(lock_);
    for (auto& list : routePipes_) list.clear();
    for (const auto& route : routes) {
      const auto source = static_cast<midi::Port>(route.src);
      const auto destination = static_cast<midi::Port>(route.dst);
      const int slot = routeSlot(source, destination);
      if (slot < 0) continue;
      std::vector<session::MidiPipe> sanitized;
      if (!sanitizePipes(route.pipes, sanitized)) continue;
      routePipes_[static_cast<std::size_t>(slot)] = std::move(sanitized);
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
        const auto specs = pipeParamSpecs(pipes[pipe].type);
        for (std::size_t param = 0; param < specs.size(); ++param) {
          targets.push_back(pipeLearnTargetPath(
              source, destination, static_cast<int>(pipe), static_cast<int>(param)));
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
