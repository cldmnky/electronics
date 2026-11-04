#include "midi_glue.hpp"

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
  if (virtualPorts_) {
    // Emulator mode: our own virtual port is how another app reaches us.
    usb_.setInputCallback([this](const midi::MidiEvent& event) { handleInput(event); });
    const auto inputs = usb_.openAll(true);
    const auto outputs = usb_.outputAll(true);
    const auto summary = "USB " + juce::String(static_cast<int>(inputs)) + " in / " +
                         juce::String(static_cast<int>(outputs)) + " out (virtual \"" +
                         juce::String(midi::UsbMidi::kVirtualPortName) + "\")";
    {
      const juce::ScopedLock lock(lock_);
      description_ = summary;
    }
    log("midi: " + summary);
    return inputs > 0 || outputs > 0;
  }

  usb_.setInputCallback([this](const midi::MidiEvent& event) { handleInput(event); });
  const auto inputs = usb_.openAll(false);
  const auto outputs = usb_.outputAll(false);
  auto summary = "USB " + juce::String(static_cast<int>(inputs)) + " in / " +
                 juce::String(static_cast<int>(outputs)) + " out";
  if (!usb_.lastError().empty()) summary += " (" + juce::String(usb_.lastError()) + ")";
  {
    const juce::ScopedLock lock(lock_);
    description_ = summary;
  }
  log("midi: " + summary);
  return inputs > 0 || outputs > 0;
}

void MidiGlue::close() {
  usb_.closeAll();
}

juce::String MidiGlue::describe() const {
  const juce::ScopedLock lock(lock_);
  return description_;
}

void MidiGlue::handleInput(const midi::MidiEvent& event) {
  const juce::ScopedLock lock(lock_);
  router_.route(event, [this](midi::Port destination, const midi::MidiEvent& routed) {
    if (outbox_.size() >= kMaxPendingDeliveries) outbox_.pop_front();
    outbox_.push_back(Delivery{destination, routed});
    ++routed_;
  });
}

void MidiGlue::pump(const TrsSink& trsSink) {
  std::deque<Delivery> pending;
  {
    const juce::ScopedLock lock(lock_);
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
  const juce::ScopedLock lock(lock_);
  router_.setRoute(source, destination, enabled);
}

bool MidiGlue::routeEnabled(midi::Port source, midi::Port destination) const {
  const juce::ScopedLock lock(lock_);
  return router_.routeEnabled(source, destination);
}

void MidiGlue::setChannelMask(midi::Port source, std::uint16_t mask) {
  const juce::ScopedLock lock(lock_);
  router_.setChannelMask(source, mask);
}

std::uint16_t MidiGlue::channelMask(midi::Port source) const {
  const juce::ScopedLock lock(lock_);
  return router_.channelMask(source);
}

void MidiGlue::setTranspose(midi::Port source, int semitones) {
  const juce::ScopedLock lock(lock_);
  router_.setTranspose(source, semitones);
}

int MidiGlue::transpose(midi::Port source) const {
  const juce::ScopedLock lock(lock_);
  return router_.transpose(source);
}

std::size_t MidiGlue::inputCount() const {
  return usb_.inputCount();
}

std::size_t MidiGlue::outputCount() const {
  return usb_.outputCount();
}

std::uint64_t MidiGlue::routedCount() const {
  const juce::ScopedLock lock(lock_);
  return routed_;
}

}  // namespace mnky::app
