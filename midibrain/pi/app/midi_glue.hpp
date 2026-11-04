#pragma once
// USB MIDI + routing-matrix glue. Inputs arrive on RtMidi's callback thread or
// on the Pico link thread, so the matrix and the pending-delivery queue are
// guarded. Deliveries are flushed by the link thread, which owns the SPI
// transport and therefore the TRS outputs.
#include <cstdint>
#include <deque>
#include <functional>

#include <juce_core/juce_core.h>

#include "../midi/router.hpp"
#include "../midi/usb.hpp"

namespace mnky::app {

class MidiGlue {
 public:
  // Sends one routed message to a TRS destination; false when the link rejected
  // it. Called from the link thread only.
  using TrsSink = std::function<bool(midi::Port, const midi::MidiEvent&)>;

  explicit MidiGlue(bool openVirtualPorts);
  ~MidiGlue();
  MidiGlue(const MidiGlue&) = delete;
  MidiGlue& operator=(const MidiGlue&) = delete;

  bool open();
  void close();
  juce::String describe() const;

  void handleInput(const midi::MidiEvent& event);  // any thread
  void pump(const TrsSink& trsSink);               // link thread
  void sendUsbClock(std::uint8_t byte);            // link thread (clock mirror)

  void setRoute(midi::Port source, midi::Port destination, bool enabled);
  bool routeEnabled(midi::Port source, midi::Port destination) const;
  void setChannelMask(midi::Port source, std::uint16_t mask);
  std::uint16_t channelMask(midi::Port source) const;
  void setTranspose(midi::Port source, int semitones);
  int transpose(midi::Port source) const;

  // Session capture/apply seam: copy the guarded matrix, mutate the copy, and
  // replace it in one lock so the MIDI input thread never sees a half-applied
  // project.
  midi::Router routerSnapshot() const {
    const juce::ScopedLock lock(lock_);
    return router_;
  }
  void replaceRouter(const midi::Router& router) {
    const juce::ScopedLock lock(lock_);
    router_ = router;
  }

  std::size_t inputCount() const;
  std::size_t outputCount() const;
  std::uint64_t routedCount() const;

 private:
  struct Delivery {
    midi::Port destination;
    midi::MidiEvent event;
  };

  mutable juce::CriticalSection lock_;
  midi::Router router_;
  midi::UsbMidi usb_;
  std::deque<Delivery> outbox_;
  const bool virtualPorts_;
  std::uint64_t routed_ = 0;
  juce::String description_;
};

}  // namespace mnky::app
