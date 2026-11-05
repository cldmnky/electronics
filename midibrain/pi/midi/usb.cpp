#include "usb.hpp"

// The backend macro must be set before RtMidi.h is parsed. It is also passed on
// the command line (see CMakeLists.txt / NOTES.md); the guard keeps a second
// definition harmless.
#if defined(__APPLE__) && !defined(__MACOSX_CORE__)
#define __MACOSX_CORE__ 1
#endif

#include <rtmidi/RtMidi.h>

#include <chrono>
#include <memory>
#include <mutex>
#include <utility>

namespace mnky::midi {
namespace {

// RtMidi client name (also the server-side port name we publish).
constexpr const char* kClientName = "mnkybox";

std::string describe(const std::exception& e) { return e.what(); }

bool isDynamicDevice(std::uint8_t device) {
  return device >= kFirstDynamicPort && device < kMaxPorts;
}

}  // namespace

std::uint64_t piNowUs() {
  using SteadyClock = std::chrono::steady_clock;
  const auto now = SteadyClock::now().time_since_epoch();
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(now).count());
}

struct UsbMidi::Impl {
  // Stable callback user data: RtMidi keeps the pointer until the port closes.
  // Tags are retained for the Impl lifetime (even across closeAll/reopen) so a
  // backend thread that is still inside onInput never dereferences a freed one.
  struct Tag {
    Impl* owner = nullptr;
    Port source = Port::UsbIn;
    std::uint8_t device = 0;
  };

  struct OutputPort {
    unsigned int index = 0;
    std::uint8_t device = 0;
    std::unique_ptr<RtMidiOut> port;
  };

  // Declaration order matters: ports (which hold the userData pointers) are
  // destroyed before the tags they point at.
  std::vector<std::unique_ptr<Tag>> tags;
  std::vector<std::unique_ptr<RtMidiIn>> inputs;
  // shared_ptr so a send can snapshot the list and release the mutex before a
  // (possibly blocking) RtMidi call, while a hot-plug refresh closes ports.
  std::vector<std::shared_ptr<OutputPort>> outputs;
  std::unique_ptr<RtMidiIn> inputProbe;
  std::unique_ptr<RtMidiOut> outputProbe;
  std::mutex mutex;         // callback
  std::mutex outputsMutex;  // outputs vector
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

  std::unique_ptr<Tag> prepare(RtMidiIn& port, Port source, std::uint8_t device) {
    auto tag = std::make_unique<Tag>();
    tag->owner = this;
    tag->source = source;
    tag->device = device;
    // SysEx is dropped (v1 forwards 1..3 byte messages only); active sensing is
    // dropped; timing/transport bytes stay visible to the app. The callback is
    // installed before the port opens so no message races into a queue we
    // never drain (the RtMidi input queue is disabled on purpose).
    port.ignoreTypes(true, false, true);
    port.setCallback(&UsbMidi::onInput, tag.get());
    return tag;
  }

  bool openInput(unsigned int index, Port source, std::uint8_t device) {
    auto port = std::make_unique<RtMidiIn>(RtMidi::UNSPECIFIED, kClientName, 0);
    auto tag = prepare(*port, source, device);
    port->openPort(index, kClientName);  // may throw before anything is kept
    tags.push_back(std::move(tag));
    inputs.push_back(std::move(port));
    return true;
  }

  bool openVirtualInput(const std::string& name) {
    auto port = std::make_unique<RtMidiIn>(RtMidi::UNSPECIFIED, kClientName, 0);
    auto tag = prepare(*port, Port::UsbIn, UsbMidi::kVirtualDevice);
    port->openVirtualPort(name);
    tags.push_back(std::move(tag));
    inputs.push_back(std::move(port));
    return true;
  }

  bool openOutput(unsigned int index, std::uint8_t device) {
    auto port = std::make_unique<RtMidiOut>(RtMidi::UNSPECIFIED, kClientName);
    port->openPort(index, kClientName);
    auto entry = std::make_shared<OutputPort>();
    entry->index = index;
    entry->device = device;
    entry->port = std::move(port);
    const std::lock_guard<std::mutex> lock(outputsMutex);
    outputs.push_back(std::move(entry));
    return true;
  }

  bool openVirtualOutput(const std::string& name) {
    auto port = std::make_unique<RtMidiOut>(RtMidi::UNSPECIFIED, kClientName);
    port->openVirtualPort(name);
    auto entry = std::make_shared<OutputPort>();
    entry->index = 0;
    entry->device = UsbMidi::kVirtualDevice;
    entry->port = std::move(port);
    const std::lock_guard<std::mutex> lock(outputsMutex);
    outputs.push_back(std::move(entry));
    return true;
  }

  bool send(const std::uint8_t* data, std::size_t size) {
    if (data == nullptr || size == 0) return false;
    std::vector<std::shared_ptr<OutputPort>> snapshot;
    {
      const std::lock_guard<std::mutex> lock(outputsMutex);
      snapshot = outputs;
    }
    if (snapshot.empty()) return false;
    const std::vector<unsigned char> message(data, data + size);
    bool ok = true;
    for (const auto& out : snapshot) {
      try {
        out->port->sendMessage(&message);
      } catch (const std::exception& e) {
        lastError = describe(e);
        ok = false;
      }
    }
    return ok;
  }

  bool sendTo(std::uint8_t device, const std::uint8_t* data, std::size_t size) {
    if (data == nullptr || size == 0) return false;
    std::vector<std::shared_ptr<OutputPort>> snapshot;
    {
      const std::lock_guard<std::mutex> lock(outputsMutex);
      for (const auto& out : outputs) {
        if (out->device == device) snapshot.push_back(out);
      }
    }
    if (snapshot.empty()) return false;
    const std::vector<unsigned char> message(data, data + size);
    bool ok = true;
    for (const auto& out : snapshot) {
      try {
        out->port->sendMessage(&message);
      } catch (const std::exception& e) {
        lastError = describe(e);
        ok = false;
      }
    }
    return ok;
  }

  std::vector<std::string> list(bool input) {
    std::vector<std::string> names;
    try {
      if (input) {
        auto& probe = inputProbeRef();
        const unsigned int count = probe.getPortCount();
        names.reserve(count);
        for (unsigned int i = 0; i < count; ++i) names.push_back(probe.getPortName(i));
      } else {
        auto& probe = outputProbeRef();
        const unsigned int count = probe.getPortCount();
        names.reserve(count);
        for (unsigned int i = 0; i < count; ++i) names.push_back(probe.getPortName(i));
      }
    } catch (const std::exception& e) {
      lastError = describe(e);
      names.clear();
    }
    return names;
  }
};

UsbMidi::UsbMidi() : impl_(std::make_unique<Impl>()) {}

// Impl destroys the open ports before the callback tags they point at.
UsbMidi::~UsbMidi() = default;

void UsbMidi::onInput(double timeStamp, std::vector<unsigned char>* message, void* userData) {
  (void)timeStamp;  // backend arrival stamp; the Pi timeline comes from piNowUs()
  auto* tag = static_cast<Impl::Tag*>(userData);
  if (tag == nullptr || message == nullptr) return;
  MidiEvent ev = makeEvent(message->data(), message->size(), tag->device, piNowUs());
  if (ev.size == 0) return;
  ev.source = tag->source;
  std::function<void(const MidiEvent&)> fn;
  {
    const std::lock_guard<std::mutex> lock(tag->owner->mutex);
    fn = tag->owner->callback;
  }
  if (fn) fn(ev);
}

std::vector<std::string> UsbMidi::listInputs() { return impl_->list(true); }

std::vector<std::string> UsbMidi::listOutputs() { return impl_->list(false); }

std::size_t UsbMidi::openAll(bool withVirtual, const std::string& virtualName) {
  impl_->lastError.clear();
  std::size_t opened = 0;
  const auto names = listInputs();
  for (unsigned int i = 0; i < names.size(); ++i) {
    try {
      impl_->openInput(i, Port::UsbIn, static_cast<std::uint8_t>(i));
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
  const auto names = listOutputs();
  for (unsigned int i = 0; i < names.size(); ++i) {
    try {
      impl_->openOutput(i, static_cast<std::uint8_t>(i));
      ++opened;
    } catch (const std::exception& e) {
      impl_->lastError = describe(e);
    }
  }
  if (withVirtual) {
    try {
      impl_->openVirtualOutput(virtualName);
      ++opened;
    } catch (const std::exception& e) {
      impl_->lastError = describe(e);
    }
  }
  return opened;
}

bool UsbMidi::openDeviceInput(unsigned int index, std::uint8_t device) {
  if (!isDynamicDevice(device)) return false;
  try {
    return impl_->openInput(index, static_cast<Port>(device), device);
  } catch (const std::exception& e) {
    impl_->lastError = describe(e);
    return false;
  }
}

bool UsbMidi::openDeviceOutput(unsigned int index, std::uint8_t device) {
  if (!isDynamicDevice(device)) return false;
  try {
    return impl_->openOutput(index, device);
  } catch (const std::exception& e) {
    impl_->lastError = describe(e);
    return false;
  }
}

bool UsbMidi::sendToDevice(std::uint8_t device, const MidiEvent& ev) {
  if (!isDynamicDevice(device)) return false;
  return impl_->sendTo(device, ev.data, ev.size);
}

bool UsbMidi::openVirtualPorts(const std::string& virtualName) {
  bool ok = true;
  try {
    impl_->openVirtualInput(virtualName);
  } catch (const std::exception& e) {
    impl_->lastError = describe(e);
    ok = false;
  }
  try {
    impl_->openVirtualOutput(virtualName);
  } catch (const std::exception& e) {
    impl_->lastError = describe(e);
    ok = false;
  }
  return ok;
}

void UsbMidi::closeAll() {
  impl_->inputs.clear();  // close ports first: they still reference the tags
  const std::lock_guard<std::mutex> lock(impl_->outputsMutex);
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

std::size_t UsbMidi::outputCount() const {
  const std::lock_guard<std::mutex> lock(impl_->outputsMutex);
  return impl_->outputs.size();
}

const std::string& UsbMidi::lastError() const { return impl_->lastError; }

}  // namespace mnky::midi
