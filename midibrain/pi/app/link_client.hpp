#pragma once
// Pico link glue: owns the SPI transport, PicoLink and the single thread that
// touches them. Every other thread (GUI, RtMidi input) talks to the link
// through the command queue here, because PicoLink is single-threaded by
// contract.
//
// No-Pico mode (LinkClient(midi, state, emulated, noPico = true)) is the bare-Pi
// variant: no SPI transport, no PicoLink and no READY GPIO are constructed, so
// there is no handshake, no reconnect state machine and no link-lost noise. It
// still runs one thread, but only as a 1 ms clock/MIDI pump: the Pi free-runs
// the USB clock (UsbClock free-run), the transport commands apply directly to
// the local rolling state (no EV_TRANSPORT frames exist) and MidiGlue keeps
// routing USB traffic. TRS sends and pipe chains are suppressed and logged
// once; the Pico UARTs and panel hardware that would receive them are absent.
#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "../link/link.hpp"
#include "../midi/clock.hpp"
#include "app_state.hpp"
#include "midi_glue.hpp"

namespace mnky::app {

// Snapshot the UI can read at any time. Written only by the link thread.
struct LinkStatus {
  bool emulated = false;
  // No-Pico mode: no Pico was constructed. `linkUp` stays false because nothing
  // is connected; the UI reads this flag first and says NO PICO instead
  // of reporting a down link.
  bool noPico = false;
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
  // Last EV_CLOCK_ROUTE flags the link queued per TRS output (index 0 = OUT1,
  // 1 = OUT2). Bit0 = clock, bit1 = transport (mb::CLOCK_ROUTE_*).
  std::uint8_t clockRouteFlags[2] = {mb::CLOCK_ROUTE_DEFAULT,
                                     mb::CLOCK_ROUTE_DEFAULT};
  link::PicoLink::Stats stats{};
};

class LinkClient {
 public:
  LinkClient(MidiGlue& midi, AppState& state, bool emulated, bool noPico = false);
  ~LinkClient();
  LinkClient(const LinkClient&) = delete;
  LinkClient& operator=(const LinkClient&) = delete;

  void start();
  void stop();
  bool emulated() const { return emulated_; }
  bool noPico() const { return noPico_; }
  LinkStatus status() const;

  // Queued to the link thread; safe from the GUI thread.
  void requestTempo(std::uint16_t bpmX100);
  void requestTransport(std::uint8_t command);  // 0 stop, 1 start, 2 continue
  void requestLeds(std::uint8_t mask, std::uint8_t states);
  void requestManual(bool manual);
  // Per-output clock/transport gate (session v2 clock.trs[]). output 0/1 =
  // TRS OUT1/OUT2, flags bit0 = clock, bit1 = transport; delivered to the Pico
  // as EV_CLOCK_ROUTE (PicoLink::clockRoute).
  void requestClockRoute(std::uint8_t output, std::uint8_t flags);
  // USB mirror gates (session v2 clock.usb). Applied to the Pi-side UsbClock,
  // which then suppresses its own 0xF8 / START / CONTINUE / STOP bytes.
  void requestUsbClockGates(bool clock, bool transport);
  // MIDI FX chain mirror for one TRS->TRS route: EV_MIDI_ROUTE (0x18) begins a
  // fresh chain with the route's enable flag, then EV_MIDI_ROUTE_PIPE (0x19)
  // chunks carry every pipe. Sent on every chain edit and after a project load;
  // routed TRS bytes then use EV_MIDI_ROUTE_SEND (0x1A) so the Pico evaluates
  // exactly this chain. Pairs touching USB are rejected (Pi-owned).
  void requestMidiRoutePipes(midi::Port source, midi::Port destination,
                             const std::vector<session::MidiPipe>& pipes,
                             bool enabled);

 private:
  struct Command {
    enum class Kind : std::uint8_t {
      Tempo,
      Transport,
      Leds,
      Manual,
      ClockRoute,
      UsbClockGates,
      MidiRoutePipes
    };
    Kind kind = Kind::Tempo;
    std::uint16_t value16 = 0;
    std::uint8_t value8 = 0;
    std::uint8_t extra8 = 0;
    std::vector<session::MidiPipe> pipes;
  };

  void run();
  void applyCommands();
  void drainEvents();
  void pumpClock();
  void readReady();
  bool usbTransportGate() const;
  void feedClock(std::uint32_t tick, std::uint32_t picoStampUs, std::uint64_t piNowUs);

  // No-Pico pump: no transport, no PicoLink, no READY sampling.
  void runNoPico();
  void applyNoPicoCommands();
  void pumpNoPicoClock();

  MidiGlue& midi_;
  AppState& state_;
  bool emulated_;
  bool noPico_;
  std::unique_ptr<link::ISpiTransport> transport_;
  std::unique_ptr<link::PicoLink> link_;
  std::unique_ptr<link::ReadyGpio> ready_;

  mutable std::mutex statusLock_;
  LinkStatus status_;
  midi::UsbClock clock_;
  bool shiftHeld_ = false;

  mutable std::mutex commandLock_;
  std::deque<Command> commands_;
  std::uint64_t lastReadyPollMs_ = 0;
  bool loggedRouteSend_ = false;
  // No-Pico: Pi-side tick count advanced by each emitted USB clock byte, and the
  // one-time log flags that keep the absent Pico from spamming the log.
  std::uint32_t noPicoTick_ = 0;
  bool loggedNoPicoTrsSend_ = false;
  bool loggedNoPicoPipeChain_ = false;
  bool loggedNoPicoClockRoute_ = false;

  std::atomic<bool> exit_{false};
  std::thread thread_;
};

}  // namespace mnky::app
