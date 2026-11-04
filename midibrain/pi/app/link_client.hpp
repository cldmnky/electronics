#pragma once
// Pico link glue: owns the SPI transport, PicoLink and the single thread that
// touches them. Every other thread (UI, RtMidi input) talks to the link through
// the command queue here, because PicoLink is single-threaded by contract.
#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>

#include <juce_core/juce_core.h>

#include "../link/link.hpp"
#include "../midi/clock.hpp"
#include "app_state.hpp"
#include "midi_glue.hpp"

namespace mnky::app {

// Snapshot the UI can read at any time. Written only by the link thread.
struct LinkStatus {
  bool emulated = false;
  bool linkUp = false;
  bool ready = false;
  bool readyValid = false;
  std::uint32_t picoSession = 0;
  std::uint32_t piSession = 0;
  std::uint32_t tick = 0;
  std::uint16_t tempoX100 = midi::UsbClock::kDefaultBpmX100;
  bool rolling = false;
  bool manual = false;
  bool shift = false;
  bool clockPrimed = false;
  double offsetUs = 0.0;
  double latencyUs = 0.0;
  std::uint64_t clockSkipped = 0;
  std::uint64_t clockReordered = 0;
  std::uint32_t lastClockTick = 0;
  link::PicoLink::Stats stats{};
};

class LinkClient final : private juce::Thread {
 public:
  LinkClient(MidiGlue& midi, AppState& state, bool emulated);
  ~LinkClient() override;
  LinkClient(const LinkClient&) = delete;
  LinkClient& operator=(const LinkClient&) = delete;

  void start();
  void stop();
  bool emulated() const { return emulated_; }
  LinkStatus status() const;

  // Queued to the link thread; safe from the message thread.
  void requestTempo(std::uint16_t bpmX100);
  void requestTransport(std::uint8_t command);  // 0 stop, 1 start, 2 continue
  void requestLeds(std::uint8_t mask, std::uint8_t states);
  void requestManual(bool manual);

 private:
  struct Command {
    enum class Kind : std::uint8_t { Tempo, Transport, Leds, Manual };
    Kind kind = Kind::Tempo;
    std::uint16_t value16 = 0;
    std::uint8_t value8 = 0;
    std::uint8_t extra8 = 0;
  };

  void run() override;
  void applyCommands();
  void drainEvents();
  void pumpClock();
  void readReady();
  void feedClock(std::uint32_t tick, std::uint32_t picoStampUs, std::uint64_t piNowUs);

  MidiGlue& midi_;
  AppState& state_;
  bool emulated_;
  std::unique_ptr<link::ISpiTransport> transport_;
  std::unique_ptr<link::PicoLink> link_;
  std::unique_ptr<link::ReadyGpio> ready_;

  mutable juce::CriticalSection statusLock_;
  LinkStatus status_;
  midi::UsbClock clock_;
  bool shiftHeld_ = false;

  mutable juce::CriticalSection commandLock_;
  std::deque<Command> commands_;
  std::uint64_t lastReadyPollMs_ = 0;
};

}  // namespace mnky::app
