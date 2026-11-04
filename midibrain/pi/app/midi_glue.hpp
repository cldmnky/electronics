#pragma once
// USB MIDI + routing-matrix glue. Inputs arrive on RtMidi's callback thread or
// on the Pico link thread, so the matrix, the MIDI-learn table and the
// pending-delivery queue are guarded. Deliveries are flushed by the link
// thread, which owns the SPI transport and therefore the TRS outputs.
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "../midi/learn.hpp"
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
  std::string describe() const;

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
    const std::lock_guard<std::mutex> lock(lock_);
    return router_;
  }
  void replaceRouter(const midi::Router& router) {
    const std::lock_guard<std::mutex> lock(lock_);
    router_ = router;
  }

  std::size_t inputCount() const;
  std::size_t outputCount() const;
  std::uint64_t routedCount() const;

  // --- MIDI learn ----------------------------------------------------------
  // Setters run on the MIDI input thread, so registered targets must be safe
  // there (the app registers MixerModel atomics and the click level only).
  void registerLearnTarget(const std::string& path, midi::LearnEngine::Setter setter);
  std::vector<midi::LearnBinding> learnBindings() const;
  bool addLearnBinding(int cc, int ch, std::string target, int track = 0);
  bool removeLearnBinding(int cc, int ch);
  void clearLearnBindings();
  void setLearnBindings(const std::vector<midi::LearnBinding>& bindings);

  // Capture mode: while armed, every control-change updates a lock-free
  // snapshot the GUI thread polls; the sequence number changes per event.
  struct LearnCapture {
    bool armed = false;
    std::uint64_t seq = 0;
    int cc = 0;
    int ch = 1;
    int value = 0;
  };
  void armLearnCapture(bool armed);
  LearnCapture learnCapture() const;

  // CC123 on all 16 channels of every USB + TRS output. Queued as ordinary
  // deliveries so only the link thread touches the transport.
  void panic();

 private:
  struct Delivery {
    midi::Port destination;
    midi::MidiEvent event;
  };

  mutable std::mutex lock_;
  midi::Router router_;
  midi::UsbMidi usb_;
  std::deque<Delivery> outbox_;
  const bool virtualPorts_;
  std::uint64_t routed_ = 0;
  std::string description_;

  mutable std::mutex learnLock_;
  midi::LearnEngine learn_;

  std::atomic<bool> learnCaptureArmed_{false};
  std::atomic<std::uint64_t> learnCaptureSeq_{0};
  std::atomic<int> learnCaptureCc_{0};
  std::atomic<int> learnCaptureCh_{1};
  std::atomic<int> learnCaptureValue_{0};
};

}  // namespace mnky::app
