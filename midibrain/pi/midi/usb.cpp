#include "usb.hpp"

// The backend macro must be set before RtMidi.h is parsed. It is also passed on
// the command line (see CMakeLists.txt / NOTES.md); the guard keeps a second
// definition harmless.
#if defined(__APPLE__) && !defined(__MACOSX_CORE__)
#define __MACOSX_CORE__ 1
#endif

#include <rtmidi/RtMidi.h>

#include <chrono>
#include <mutex>
#include <utility>

namespace mnky::midi {
namespace {

// RtMidi client name (also the server-side port name we publish).
constexpr const char* kClientName = "mnkybox";

std::string describe(const std::exception& e) { return e.what(); }

}  // namespace

std::uint64_t piNowUs() {
  using SteadyClock = std::chrono::steady_clock;
  const auto now = SteadyClock::now().time_since_epoch();
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(now).count());
}

struct UsbMidi::Impl {
  // Stable callback user data: RtMidi keeps the pointer until the port closes,
  // so tags outlive their ports and are never moved.
  struct Tag {
    Impl* owner = nullptr;
    std::uint8_t device = 0;
  };

  // Declaration order matters: ports (which hold the userData pointers) are
  // destroyed before the tags they point at.
  std::vector<std::unique_ptr<Tag>> tags;
  std::vector<std::unique_ptr<RtMidiIn>> inputs;
  std::vector<std::unique_ptr<RtMidiOut>> outputs;
  std::unique_ptr<RtMidiIn> inputProbe;
  std::unique_ptr<RtMidiOut> outputProbe;
  std::mutex mutex;
  std::function<void(const MidiEvent&)> callback;
  std::string lastError;

  RtMidiIn& inputProbeRef() {
    if (!inputProbe) inputProbe = std::make_unique<RtMidiIn>(RtMidi::UNSPECIFIED, kClientName, 0);
    return *inputProbe;
  }
  RtMidiOut& outputProbeRef() {
    if (!outputProbe) outputProbe = std::make_unique<RtMidiOut>(RtMidi::UNSPECIFIED, kClientName);
    return *outputProbe;
  }

  // Enumerated input: the device index is the enumeration index.
  bool openInput(unsigned int index) {
    auto port = std::make_unique<RtMidiIn>(RtMidi::UNSPECIFIED, kClientName, 0);
    auto tag = prepare(*port, static_cast<std::uint8_t>(index));
    port->openPort(index, kClientName);  // may throw before anything is kept
    tags.push_back(std::move(tag));
    inputs.push_back(std::move(port));
    return true;
  }

  bool openVirtualInput(const std::string& name) {
    auto port = std::make_unique<RtMidiIn>(RtMidi::UNSPECIFIED, kClientName, 0);
    auto tag = prepare(*port, UsbMidi::kVirtualDevice);
    port->openVirtualPort(name);
    tags.push_back(std::move(tag));
    inputs.push_back(std::move(port));
    return true;
  }

  std::unique_ptr<Tag> prepare(RtMidiIn& port, std::uint8_t device) {
    auto tag = std::make_unique<Tag>();
    tag->owner = this;
    tag->device = device;
    // SysEx is dropped (v1 forwards 1..3 byte messages only); active sensing is
    // dropped; timing/transport bytes stay visible to the app. The callback is
    // installed before the port opens so no message races into a queue we
    // never drain (the RtMidi input queue is disabled on purpose).
    port.ignoreTypes(true, false, true);
    port.setCallback(&UsbMidi::onInput, tag.get());
    return tag;
  }

  bool send(const std::uint8_t* data, std::size_t size) {
    if (data == nullptr || size == 0) return false;
    if (outputs.empty()) return false;
    const std::vector<unsigned char> message(data, data + size);
    bool ok = true;
    for (const auto& out : outputs) {
      try {
        out->sendMessage(&message);
      } catch (const std::exception& e) {
        lastError = describe(e);
        ok = false;
      }
    }
    return ok;
  }
};

UsbMidi::UsbMidi() : impl_(std::make_unique<Impl>()) {}

// Impl destroys the open ports before the callback tags they point at.
UsbMidi::~UsbMidi() = default;

void UsbMidi::onInput(double timeStamp, std::vector<unsigned char>* message, void* userData) {
  (void)timeStamp;  // backend arrival stamp; the Pi timeline comes from piNowUs()
  auto* tag = static_cast<Impl::Tag*>(userData);
  if (tag == nullptr || message == nullptr) return;
  const MidiEvent ev =
      makeEvent(message->data(), message->size(), tag->device, piNowUs());
  if (ev.size == 0) return;
  std::function<void(const MidiEvent&)> fn;
  {
    const std::lock_guard<std::mutex> lock(tag->owner->mutex);
    fn = tag->owner->callback;
  }
  if (fn) fn(ev);
}

std::vector<std::string> UsbMidi::listInputs() {
  std::vector<std::string> names;
  try {
    auto& probe = impl_->inputProbeRef();
    const unsigned int count = probe.getPortCount();
    names.reserve(count);
    for (unsigned int i = 0; i < count; ++i) names.push_back(probe.getPortName(i));
  } catch (const std::exception& e) {
    impl_->lastError = describe(e);
    names.clear();
  }
  return names;
}

std::vector<std::string> UsbMidi::listOutputs() {
  std::vector<std::string> names;
  try {
    auto& probe = impl_->outputProbeRef();
    const unsigned int count = probe.getPortCount();
    names.reserve(count);
    for (unsigned int i = 0; i < count; ++i) names.push_back(probe.getPortName(i));
  } catch (const std::exception& e) {
    impl_->lastError = describe(e);
    names.clear();
  }
  return names;
}

std::size_t UsbMidi::openAll(bool withVirtual, const std::string& virtualName) {
  impl_->lastError.clear();
  std::size_t opened = 0;
  unsigned int count = 0;
  try {
    count = impl_->inputProbeRef().getPortCount();
  } catch (const std::exception& e) {
    impl_->lastError = describe(e);
  }
  for (unsigned int i = 0; i < count; ++i) {
    try {
      impl_->openInput(i);
      ++opened;
    } catch (const std::exception& e) {
      impl_->lastError = describe(e);
    }
  }
  if (withVirtual) {
    try {
      impl_->openVirtualInput(virtualName);
      ++opened;
    } catch (const std::exception& e) {
      impl_->lastError = describe(e);
    }
  }
  return opened;
}

std::size_t UsbMidi::outputAll(bool withVirtual, const std::string& virtualName) {
  impl_->lastError.clear();
  std::size_t opened = 0;
  unsigned int count = 0;
  try {
    count = impl_->outputProbeRef().getPortCount();
  } catch (const std::exception& e) {
    impl_->lastError = describe(e);
  }
  for (unsigned int i = 0; i < count; ++i) {
    try {
      auto port = std::make_unique<RtMidiOut>(RtMidi::UNSPECIFIED, kClientName);
      port->openPort(i, kClientName);
      impl_->outputs.push_back(std::move(port));
      ++opened;
    } catch (const std::exception& e) {
      impl_->lastError = describe(e);
    }
  }
  if (withVirtual) {
    try {
      auto port = std::make_unique<RtMidiOut>(RtMidi::UNSPECIFIED, kClientName);
      port->openVirtualPort(virtualName);
      impl_->outputs.push_back(std::move(port));
      ++opened;
    } catch (const std::exception& e) {
      impl_->lastError = describe(e);
    }
  }
  return opened;
}

void UsbMidi::closeAll() {
  impl_->inputs.clear();   // close ports first: they still reference the tags
  impl_->tags.clear();
  impl_->outputs.clear();
}

void UsbMidi::setInputCallback(std::function<void(const MidiEvent&)> fn) {
  const std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->callback = std::move(fn);
}

bool UsbMidi::send(const MidiEvent& ev) { return impl_->send(ev.data, ev.size); }

MidiEvent UsbMidi::makeEvent(const std::uint8_t* bytes, std::size_t size,
                             std::uint8_t device, std::uint64_t stampUs) {
  MidiEvent ev;
  ev.source = Port::UsbIn;
  ev.device = device;
  ev.stampUs = stampUs;
  if (bytes == nullptr || size < 1 || size > 3 || bytes[0] < 0x80) return ev;
  ev.size = static_cast<std::uint8_t>(size);
  for (std::size_t i = 0; i < size; ++i) ev.data[i] = bytes[i];
  return ev;
}

std::size_t UsbMidi::inputCount() const { return impl_->inputs.size(); }

std::size_t UsbMidi::outputCount() const { return impl_->outputs.size(); }

const std::string& UsbMidi::lastError() const { return impl_->lastError; }

}  // namespace mnky::midi
