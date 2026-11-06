#include "link_client.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <exception>
#include <utility>

#include "logging.hpp"

namespace mnky::app {
namespace {
// Cap the queued commands so a stuck link thread cannot grow memory without
// bound; the newest request wins for tempo/LED state anyway.
constexpr std::size_t kMaxCommands = 64;
constexpr std::uint64_t kReadyPollMs = 100;

// No-Pico fine scheduler re-check intervals (microseconds). A due phase is
// targeted exactly; these apply only when the phase has no known deadline:
// stopped (nothing to advance, so wait for a command wake) or rolling with the
// USB clock gate closed (advanceFreeRun() must keep the Pi tick moving, but
// UsbClock::nextDueUs() deliberately reports nothing while gated).
constexpr std::uint64_t kNoPicoGatedPollUs = 250;
constexpr std::uint64_t kNoPicoStoppedPollUs = 50'000;

std::uint64_t monotonicMs() {
  using namespace std::chrono;
  return static_cast<std::uint64_t>(
      duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

// Physical TRS index used by the 0x18/0x19/0x1A wire: 0 = IN1/OUT1,
// 1 = IN2/OUT2. USB and the merged Pi-side ports have no wire index.
bool physicalTrs(midi::Port port, std::uint8_t& index) {
  switch (port) {
    case midi::Port::TrsIn1:
    case midi::Port::TrsOut1:
      index = 0;
      return true;
    case midi::Port::TrsIn2:
    case midi::Port::TrsOut2:
      index = 1;
      return true;
    default:
      return false;
  }
}
}  // namespace

LinkClient::LinkClient(MidiGlue& midi, AppState& state, bool emulated, bool noPico)
    : midi_(midi), state_(state), emulated_(emulated && !noPico), noPico_(noPico) {
  if (noPico_) {
    status_.noPico = true;
    status_.tempoX100 = state_.tempoX100.load();
    status_.manual = true;  // the Pi owns the transport; there is no Pico panel
    clock_.setFreeRun(true);
    clock_.setTempo(status_.tempoX100);
    log("link: NO PICO mode - no SPI transport, no PicoLink; the Pi owns the clock");
    return;
  }
  if (emulated_) {
    transport_ = std::make_unique<link::EmulatorTransport>();
    log("link: emulator transport (scripted Pico, no SPI hardware)");
  } else {
    try {
      transport_ = std::make_unique<link::LinuxSpidevTransport>();
      log("link: /dev/spidev0.0 at 1 MHz, mode 0");
    } catch (const std::exception& error) {
      log(std::string("link: SPI transport unavailable (") + error.what() +
          "); falling back to the emulator transport");
      transport_ = std::make_unique<link::EmulatorTransport>();
      emulated_ = true;
    }
  }

#if defined(__linux__)
  try {
    ready_ = std::make_unique<link::ReadyGpio>();
  } catch (const std::exception& error) {
    log(std::string("gpio: READY_IRQ (BCM 24) unavailable: ") + error.what());
  }
#endif

  link_ = std::make_unique<link::PicoLink>(*transport_);
  link_->onTick([this](std::uint32_t tick, std::uint32_t picoStampUs,
                       std::uint64_t piNowUs) { feedClock(tick, picoStampUs, piNowUs); });

  status_.emulated = emulated_;
}

LinkClient::~LinkClient() {
  stop();
}

void LinkClient::start() {
  if (thread_.joinable()) return;
  exit_.store(false);
  thread_ = std::thread([this] {
    if (noPico_) {
      runNoPico();
    } else {
      run();
    }
  });
#if defined(__linux__)
  // No-Pico only: the pump already emits the clock on other hosts.
  if (noPico_) clockThread_ = std::thread([this] { runNoPicoClockThread(); });
#endif
}

void LinkClient::stop() {
  exit_.store(true);
  wakeNoPicoClock();
  if (thread_.joinable()) thread_.join();
  if (clockThread_.joinable()) clockThread_.join();
}

LinkStatus LinkClient::status() const {
  const std::lock_guard<std::mutex> lock(statusLock_);
  return status_;
}

void LinkClient::requestTempo(std::uint16_t bpmX100) {
  Command command;
  command.kind = Command::Kind::Tempo;
  command.value16 = bpmX100;
  const std::lock_guard<std::mutex> lock(commandLock_);
  if (commands_.size() >= kMaxCommands) commands_.pop_front();
  commands_.push_back(command);
}

void LinkClient::requestTransport(std::uint8_t transportCommand) {
  Command command;
  command.kind = Command::Kind::Transport;
  command.value8 = transportCommand;
  const std::lock_guard<std::mutex> lock(commandLock_);
  if (commands_.size() >= kMaxCommands) commands_.pop_front();
  commands_.push_back(command);
}

void LinkClient::requestLeds(std::uint8_t mask, std::uint8_t states) {
  Command command;
  command.kind = Command::Kind::Leds;
  command.value8 = mask;
  command.extra8 = states;
  const std::lock_guard<std::mutex> lock(commandLock_);
  if (commands_.size() >= kMaxCommands) commands_.pop_front();
  commands_.push_back(command);
}

void LinkClient::requestManual(bool manual) {
  Command command;
  command.kind = Command::Kind::Manual;
  command.value8 = manual ? 1 : 0;
  const std::lock_guard<std::mutex> lock(commandLock_);
  if (commands_.size() >= kMaxCommands) commands_.pop_front();
  commands_.push_back(command);
}

void LinkClient::requestClockRoute(std::uint8_t output, std::uint8_t flags) {
  Command command;
  command.kind = Command::Kind::ClockRoute;
  command.value8 = output;
  command.extra8 = flags;
  const std::lock_guard<std::mutex> lock(commandLock_);
  if (commands_.size() >= kMaxCommands) commands_.pop_front();
  commands_.push_back(command);
}

void LinkClient::requestUsbClockGates(bool clock, bool transport) {
  Command command;
  command.kind = Command::Kind::UsbClockGates;
  command.value8 = static_cast<std::uint8_t>((clock ? 1 : 0) | (transport ? 2 : 0));
  const std::lock_guard<std::mutex> lock(commandLock_);
  if (commands_.size() >= kMaxCommands) commands_.pop_front();
  commands_.push_back(command);
}

void LinkClient::requestMidiRoutePipes(
    midi::Port source, midi::Port destination,
    const std::vector<session::MidiPipe>& pipes, bool enabled) {
  std::uint8_t src = 0;
  std::uint8_t dst = 0;
  if (!midi::isInput(source) || !midi::isOutput(destination) ||
      !physicalTrs(source, src) || !physicalTrs(destination, dst)) {
    return;  // USB-touching routes stay Pi-owned
  }
  Command command;
  command.kind = Command::Kind::MidiRoutePipes;
  command.value8 = src;
  command.extra8 = dst;
  command.value16 = enabled ? 1 : 0;
  command.pipes = pipes;
  const std::lock_guard<std::mutex> lock(commandLock_);
  if (commands_.size() >= kMaxCommands) commands_.pop_front();
  commands_.push_back(std::move(command));
}

void LinkClient::run() {
  const MidiGlue::TrsSink trsSink = [this](midi::Port destination,
                                           const midi::MidiEvent& event) {
    std::uint8_t dst = 0;
    std::uint8_t src = 0;
    if (!midi::isOutput(destination) || !physicalTrs(destination, dst)) return false;
    // A TRS->TRS route with a chain must go out as EV_MIDI_ROUTE_SEND (0x1A) so
    // the Pico evaluates it. The Pi already applied the channel mask and its
    // source transpose before queueing this delivery, so the effective order on
    // that route is mask -> transpose -> Pico chain. Every other destination
    // (USB in -> TRS out) uses the unprocessed 0x15 path: no Pico chain exists
    // for a USB source, so 0x1A would drop it.
    if (midi::isInput(event.source) && physicalTrs(event.source, src) &&
        midi_.routeHasPipes(event.source, destination)) {
      if (!loggedRouteSend_) {
        loggedRouteSend_ = true;
        log("link: TRS route sends use 0x1A (mask -> transpose -> Pico pipe chain)");
      }
      return link_->sendMidiRoute(src, dst, event.data, event.size);
    }
    return link_->sendMidi(dst, event.data, event.size);
  };

  while (!exit_.load(std::memory_order_relaxed)) {
    applyCommands();
    link_->poll();
    drainEvents();
    midi_.pump(trsSink);
    pumpClock();
    readReady();

    {
      const std::lock_guard<std::mutex> lock(statusLock_);
      status_.stats = link_->stats();
      status_.linkUp = link_->linkUp();
      status_.picoSession = link_->session();
      status_.piSession = link_->piSession();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

void LinkClient::applyCommands() {
  for (;;) {
    Command command;
    {
      const std::lock_guard<std::mutex> lock(commandLock_);
      if (commands_.empty()) break;
      command = commands_.front();
      commands_.pop_front();
    }

    switch (command.kind) {
      case Command::Kind::Tempo:
        link_->setTempo(command.value16);
        {
          const std::lock_guard<std::mutex> lock(statusLock_);
          clock_.setTempo(command.value16);
          status_.tempoX100 = command.value16;
        }
        state_.tempoX100.store(command.value16);
        break;

      case Command::Kind::Transport:
        link_->transport(command.value8);
        // The USB clock mirror follows the transport: START/CONTINUE/STOP are
        // single bytes and must not wait for the next Pico tick to be mirrored.
        // The transport gate closed for the USB output suppresses the mirror.
        if (command.value8 == 1) {
          if (usbTransportGate()) midi_.sendUsbClock(midi::kStartByte);
        } else if (command.value8 == 2) {
          if (usbTransportGate()) midi_.sendUsbClock(midi::kContinueByte);
        } else if (usbTransportGate()) {
          midi_.sendUsbClock(midi::kStopByte);
        }
        break;

      case Command::Kind::Leds:
        link_->ledSet(command.value8, command.extra8);
        break;

      case Command::Kind::Manual:
        link_->setManual(command.value8 != 0);
        {
          const std::lock_guard<std::mutex> lock(statusLock_);
          status_.manual = command.value8 != 0;
        }
        break;

      case Command::Kind::ClockRoute:
        // 0x14 EV_CLOCK_ROUTE: bit0 clock, bit1 transport for one TRS output.
        link_->clockRoute(command.value8, command.extra8);
        {
          const std::lock_guard<std::mutex> lock(statusLock_);
          if (command.value8 < 2) {
            status_.clockRouteFlags[command.value8] = command.extra8;
          }
        }
        break;

      case Command::Kind::UsbClockGates:
        // UsbClock::popDue already refuses clock bytes while the clock gate is
        // closed; the transport gate is read by usbTransportGate().
        {
          const std::lock_guard<std::mutex> lock(statusLock_);
          clock_.setClockEnabled((command.value8 & 1) != 0);
          clock_.setTransportEnabled((command.value8 & 2) != 0);
        }
        break;

      case Command::Kind::MidiRoutePipes: {
        // 0x18 begins a fresh chain (clears the Pico runtime state), then 0x19
        // carries each pipe in order; parameters go in chunks of the wire limit.
        if (!link_->midiRoute(command.value8, command.extra8, command.value16 != 0)) {
          break;
        }
        const std::size_t count = std::min(command.pipes.size(), mb::route_pipe_count_max);
        for (std::size_t index = 0; index < count; ++index) {
          const auto& pipe = command.pipes[index];
          std::array<std::int16_t, mb::route_pipe_param_max> params{};
          const std::size_t paramCount =
              std::min(pipe.params.size(), params.size());
          for (std::size_t param = 0; param < paramCount; ++param) {
            params[param] = static_cast<std::int16_t>(
                std::clamp(pipe.params[param], -32768, 32767));
          }
          const auto type = static_cast<std::uint8_t>(pipe.type);
          const auto pipeIndex = static_cast<std::uint8_t>(index);
          const auto chMask = static_cast<std::uint16_t>(pipe.chMask);
          if (paramCount == 0) {
            (void)link_->midiRoutePipe(command.value8, command.extra8, pipeIndex, type,
                                       pipe.bypass, 0, nullptr, 0, chMask);
            continue;
          }
          for (std::size_t offset = 0; offset < paramCount;
               offset += mb::route_pipe_param_frame_max) {
            const std::size_t remaining = paramCount - offset;
            const std::size_t chunk =
                std::min(remaining, mb::route_pipe_param_frame_max);
            (void)link_->midiRoutePipe(
                command.value8, command.extra8, pipeIndex, type, pipe.bypass,
                static_cast<std::uint8_t>(offset), &params[offset], chunk, chMask);
          }
        }
        break;
      }
    }
  }
}

void LinkClient::runNoPico() {
  // The Pico hardware owns every TRS output. There is nothing on the other end
  // of a routed TRS delivery in no-Pico mode, so the sink refuses it (the monitor
  // then has no TRS OUT row to show) and the first refusal explains why once.
  const MidiGlue::TrsSink trsSink = [this](midi::Port, const midi::MidiEvent&) {
    if (!loggedNoPicoTrsSend_) {
      loggedNoPicoTrsSend_ = true;
      log("no-pico: TRS MIDI output unavailable - 0x15/0x1A route send suppressed (no Pico)");
    }
    return false;
  };

#if defined(__linux__)
  log("link: no-Pico pump running (1 ms MIDI tick; clock bytes on a fine phase-timed thread)");
#else
  log("link: no-Pico pump running (1 ms clock/MIDI tick, USB output only; no fine timer on this host)");
#endif
  while (!exit_.load(std::memory_order_relaxed)) {
    applyNoPicoCommands();
    {
      // RtMidi's ALSA output backend keeps one encoder/buffer per port and is
      // not safe for concurrent sendMessage(), so routed USB sends and the
      // fine thread's clock bytes share the emission lock.
      const std::lock_guard<std::mutex> emitLock(noPicoEmitLock_);
      midi_.pump(trsSink);
    }
#if !defined(__linux__)
    // Non-Linux hosts have no fine clock thread: the pump stays the clock
    // emitter and keeps today's 1 ms scheduling granularity.
    pumpNoPicoClock();
#endif
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

void LinkClient::applyNoPicoCommands() {
  bool applied = false;
  for (;;) {
    Command command;
    {
      const std::lock_guard<std::mutex> lock(commandLock_);
      if (commands_.empty()) break;
      command = commands_.front();
      commands_.pop_front();
    }
    applied = true;

    switch (command.kind) {
      case Command::Kind::Tempo:
        {
          // The fine clock thread reads tempo/phase under statusLock_.
          const std::lock_guard<std::mutex> lock(statusLock_);
          clock_.setTempo(command.value16);
          status_.tempoX100 = command.value16;
        }
        state_.tempoX100.store(command.value16);
        break;

      case Command::Kind::Transport: {
        // State frames come from the Pico in hardware mode; no-Pico mode has no
        // Pico, so the command is the transport state change itself. START
        // resets the Pi tick exactly like the firmware's transport_cmd().
        // The fine clock thread emits under this same lock, so the phase change
        // and its transport byte are one step relative to clock emission.
        const std::lock_guard<std::mutex> emitLock(noPicoEmitLock_);
        const auto now = midi::piNowUs();
        bool transportGate = true;
        std::uint32_t tick = 0;
        {
          const std::lock_guard<std::mutex> lock(statusLock_);
          if (command.value8 == 1) noPicoTick_ = 0;
          if (command.value8 != 0) {
            clock_.setRolling(true);
            clock_.startFreeRun(now);
          } else {
            clock_.setRolling(false);
          }
          status_.rolling = command.value8 != 0;
          status_.tick = noPicoTick_;
          status_.clockPrimed = clock_.primed();
          status_.lastClockTick = noPicoTick_;
          transportGate = clock_.transportEnabled();
          tick = noPicoTick_;
        }
        state_.rolling.store(command.value8 != 0);
        state_.tick.store(tick);
        if (!transportGate) break;
        if (command.value8 == 1) {
          midi_.sendUsbClock(midi::kStartByte);
        } else if (command.value8 == 2) {
          midi_.sendUsbClock(midi::kContinueByte);
        } else {
          midi_.sendUsbClock(midi::kStopByte);
        }
        break;
      }

      case Command::Kind::Leds:
        // No panel LEDs exist on a bare Pi; the command is accepted and dropped.
        break;

      case Command::Kind::Manual:
        {
          const std::lock_guard<std::mutex> lock(statusLock_);
          status_.manual = command.value8 != 0;
        }
        break;

      case Command::Kind::ClockRoute:
        // TRS OUT clock/transport is generated by the Pico's UARTs. Keep the
        // gate for the status snapshot but never pretend a wire received it.
        {
          const std::lock_guard<std::mutex> lock(statusLock_);
          if (command.value8 < 2) {
            status_.clockRouteFlags[command.value8] = command.extra8;
          }
        }
        if (!loggedNoPicoClockRoute_) {
          loggedNoPicoClockRoute_ = true;
          log("no-pico: TRS clock/transport gate ignored (Pico UARTs absent); USB clock free-runs");
        }
        break;

      case Command::Kind::UsbClockGates:
        {
          // Same lock order as the fine thread (emit, then status): a clock
          // byte decided before this command lands before the gate closes, and
          // nothing decided afterwards can slip out.
          const std::lock_guard<std::mutex> emitLock(noPicoEmitLock_);
          const std::lock_guard<std::mutex> lock(statusLock_);
          clock_.setClockEnabled((command.value8 & 1) != 0);
          clock_.setTransportEnabled((command.value8 & 2) != 0);
        }
        break;

      case Command::Kind::MidiRoutePipes:
        // 0x18/0x19 configure the Pico's chain; with no Pico the TRS->TRS
        // chain stays Pi-side metadata and its route is inert in no-Pico mode.
        if (!loggedNoPicoPipeChain_) {
          loggedNoPicoPipeChain_ = true;
          log("no-pico: TRS pipe chain not sent (0x18/0x19 need a Pico); TRS routes are inert");
        }
        break;
    }
  }
  if (applied) wakeNoPicoClock();
}

void LinkClient::pumpNoPicoClock() { (void)serviceNoPicoClock(); }

std::uint64_t LinkClient::serviceNoPicoClock() {
  const auto now = midi::piNowUs();
  // Held across the phase decision and the byte: the pump's START/CONTINUE/
  // STOP and gate commands take this lock too, so a clock byte is ordered
  // against them even though it is no longer sent by the pump thread.
  const std::lock_guard<std::mutex> emitLock(noPicoEmitLock_);
  bool emit = false;
  std::uint64_t waitUs = kNoPicoStoppedPollUs;
  {
    const std::lock_guard<std::mutex> lock(statusLock_);
    clock_.setRolling(status_.rolling);
    // advanceFreeRun() is gate-independent on purpose: the Pi generator drives
    // the timeline, the USB gate only mutes its output bytes.
    if (clock_.advanceFreeRun(now)) {
      ++noPicoTick_;
      status_.tick = noPicoTick_;
      status_.lastClockTick = noPicoTick_;
      state_.tick.store(noPicoTick_);
      emit = clock_.clockEnabled();
    }
    status_.clockPrimed = clock_.primed();
    // Free-run has no Pico stamp, so the discipline diagnostics have no value.
    status_.offsetUs = 0.0;
    status_.latencyUs = 0.0;
    status_.clockSkipped = clock_.skipped();
    status_.clockReordered = clock_.reordered();
    if (status_.rolling) {
      // While rolling, nextDueUs() is the phase itself; nullopt means the USB
      // clock gate is closed, so poll at the fine cadence to keep the Pi tick
      // advancing (the phase still moves by exactly one interval per pass).
      const auto due = clock_.nextDueUs(now);
      if (!due) {
        waitUs = kNoPicoGatedPollUs;
      } else if (*due <= now) {
        waitUs = 1;  // due at/just after `now`: look again immediately
      } else {
        waitUs = *due - now;
      }
    }
  }
  if (emit) midi_.sendUsbClock(midi::kClockByte);
  return waitUs;
}

void LinkClient::runNoPicoClockThread() {
  // Wait for the exact phase deadline instead of polling on a fixed grid. The
  // wake generation closes the race where a command lands between reading the
  // state and blocking: if it already changed, wait_until returns at once and
  // the next pass sees the new tempo/rolling/gate state.
  for (;;) {
    std::uint64_t generation = 0;
    {
      const std::lock_guard<std::mutex> lock(clockWakeLock_);
      generation = clockWakeGeneration_;
    }
    if (exit_.load(std::memory_order_relaxed)) return;
    const std::uint64_t waitUs = serviceNoPicoClock();
    std::unique_lock<std::mutex> lock(clockWakeLock_);
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::microseconds(waitUs);
    clockWake_.wait_until(lock, deadline, [this, generation] {
      return exit_.load(std::memory_order_relaxed) ||
             clockWakeGeneration_ != generation;
    });
    if (exit_.load(std::memory_order_relaxed)) return;
  }
}

void LinkClient::wakeNoPicoClock() {
  {
    const std::lock_guard<std::mutex> lock(clockWakeLock_);
    ++clockWakeGeneration_;
  }
  clockWake_.notify_all();
}

void LinkClient::drainEvents() {
  link::PicoLink::Event event;
  while (link_->nextEvent(event)) {
    switch (event.kind) {
      case link::PicoLink::Event::MidiIn: {
        if (event.n < 1 || event.n > 3) break;
        midi::MidiEvent message;
        message.source = event.u32a == 0 ? midi::Port::TrsIn1 : midi::Port::TrsIn2;
        message.size = event.n;
        for (int i = 0; i < 3; ++i) message.data[i] = event.a[i];
        message.stampUs = midi::piNowUs();
        {
          // The Pico stamp is authoritative once the clock is warm; mapping it
          // keeps MIDI timing and the mirrored USB clock on one timeline.
          const std::lock_guard<std::mutex> lock(statusLock_);
          if (clock_.primed()) message.stampUs = clock_.mapStamp(event.u32b);
        }
        midi_.handleInput(message);
        break;
      }

      case link::PicoLink::Event::Control: {
        const std::uint8_t id = event.a[0];
        const std::uint8_t value = event.a[1];
        bool shift = false;
        {
          const std::lock_guard<std::mutex> lock(statusLock_);
          // CTL_SHIFT_EXIT is a modifier: it never becomes an action itself and
          // every other control is dispatched with the held state.
          if (id == mb::CTL_SHIFT_EXIT) {
            shiftHeld_ = value != 0;
            status_.shift = shiftHeld_;
          }
          shift = shiftHeld_;
        }
        if (id == mb::CTL_SHIFT_EXIT) break;

        const PanelState panel{state_.currentTab.load(std::memory_order_relaxed),
                               state_.selectedStrip.load(std::memory_order_relaxed)};
        const auto intent = intentForControl(id, value, shift, panel);
        if (intent.kind != Intent::Kind::None) state_.postIntent(intent);
        break;
      }

      case link::PicoLink::Event::Tempo: {
        const auto tempo = static_cast<std::uint16_t>(event.u32a);
        {
          const std::lock_guard<std::mutex> lock(statusLock_);
          clock_.setTempo(tempo);
          status_.tempoX100 = tempo;
        }
        state_.tempoX100.store(tempo);
        break;
      }

      case link::PicoLink::Event::Tick: {
        const std::lock_guard<std::mutex> lock(statusLock_);
        status_.tick = event.u32a;
        state_.tick.store(event.u32a);
        break;
      }

      case link::PicoLink::Event::State: {
        const bool rolling = event.a[0] != 0;
        {
          const std::lock_guard<std::mutex> lock(statusLock_);
          status_.rolling = rolling;
          status_.tick = event.u32a;
        }
        state_.rolling.store(rolling);
        state_.tick.store(event.u32a);
        break;
      }

      case link::PicoLink::Event::Linked: {
        {
          const std::lock_guard<std::mutex> lock(statusLock_);
          status_.linkUp = true;
          status_.picoSession = event.u32a;
          status_.piSession = link_->piSession();
        }
        log("link: up, Pico session 0x" + std::to_string(event.u32a));
        break;
      }

      case link::PicoLink::Event::LinkLost: {
        {
          const std::lock_guard<std::mutex> lock(statusLock_);
          status_.linkUp = false;
        }
        log("link: lost (Pico rebooted)");
        break;
      }
    }
  }
}

void LinkClient::pumpClock() {
  const auto now = midi::piNowUs();
  int due = 0;
  {
    const std::lock_guard<std::mutex> lock(statusLock_);
    clock_.setRolling(status_.rolling);
    while (due < 4 && clock_.popDue(now)) ++due;
    status_.clockPrimed = clock_.primed();
    status_.offsetUs = clock_.offsetUs();
    status_.latencyUs = clock_.latencyUs();
    status_.clockSkipped = clock_.skipped();
    status_.clockReordered = clock_.reordered();
    status_.lastClockTick = clock_.lastTick();
  }
  for (int i = 0; i < due; ++i) midi_.sendUsbClock(midi::kClockByte);
}

void LinkClient::feedClock(std::uint32_t tick, std::uint32_t picoStampUs,
                           std::uint64_t piNowUs) {
  const std::lock_guard<std::mutex> lock(statusLock_);
  clock_.addTick(tick, picoStampUs, piNowUs);
  clock_.setRolling(status_.rolling);
  status_.tick = tick;
  state_.tick.store(tick);
  status_.clockPrimed = clock_.primed();
  status_.offsetUs = clock_.offsetUs();
  status_.latencyUs = clock_.latencyUs();
  status_.clockSkipped = clock_.skipped();
  status_.clockReordered = clock_.reordered();
  status_.lastClockTick = clock_.lastTick();
}

bool LinkClient::usbTransportGate() const {
  const std::lock_guard<std::mutex> lock(statusLock_);
  return clock_.transportEnabled();
}

void LinkClient::readReady() {
  if (ready_ == nullptr) return;
  const auto now = monotonicMs();
  if (now - lastReadyPollMs_ < kReadyPollMs) return;
  lastReadyPollMs_ = now;
  const bool high = ready_->ready();
  const std::lock_guard<std::mutex> lock(statusLock_);
  status_.ready = high;
  status_.readyValid = true;
}

}  // namespace mnky::app
