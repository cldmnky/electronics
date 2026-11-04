#include "midi_glue.hpp"

#include <cstddef>
#include <utility>

#include "logging.hpp"

namespace mnky::app {
namespace {
// Bounded so a stalled link thread cannot grow the queue without limit; the
// oldest deliveries are discarded first, which is what a MIDI hub has to do.
constexpr std::size_t kMaxPendingDeliveries = 2048;
}  // namespace

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
  {
    const std::lock_guard<std::mutex> lock(lock_);
    router_.route(event, [this](midi::Port destination, const midi::MidiEvent& routed) {
      if (outbox_.size() >= kMaxPendingDeliveries) outbox_.pop_front();
      outbox_.push_back(Delivery{destination, routed});
      ++routed_;
    });
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
    pending.swap(outbox_);
  }
  for (const auto& delivery : pending) {
    if (delivery.destination == midi::Port::UsbOut) {
      usb_.send(delivery.event);
    } else if (trsSink) {
      trsSink(delivery.destination, delivery.event);
    }
  }
}

void MidiGlue::sendUsbClock(std::uint8_t byte) {
  midi::MidiEvent event;
  event.source = midi::Port::UsbOut;
  event.size = 1;
  event.data[0] = byte;
  event.stampUs = midi::piNowUs();
  usb_.send(event);
}

void MidiGlue::setRoute(midi::Port source, midi::Port destination, bool enabled) {
  const std::lock_guard<std::mutex> lock(lock_);
  router_.setRoute(source, destination, enabled);
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
