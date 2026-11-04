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

LinkClient::LinkClient(MidiGlue& midi, AppState& state, bool emulated)
    : midi_(midi), state_(state), emulated_(emulated) {
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
  thread_ = std::thread([this] { run(); });
}

void LinkClient::stop() {
  exit_.store(true);
  if (thread_.joinable()) thread_.join();
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
          if (paramCount == 0) {
            (void)link_->midiRoutePipe(command.value8, command.extra8, pipeIndex, type,
                                       pipe.bypass, 0, nullptr, 0);
            continue;
          }
          for (std::size_t offset = 0; offset < paramCount;
               offset += mb::route_pipe_param_frame_max) {
            const std::size_t remaining = paramCount - offset;
            const std::size_t chunk =
                std::min(remaining, mb::route_pipe_param_frame_max);
            (void)link_->midiRoutePipe(
                command.value8, command.extra8, pipeIndex, type, pipe.bypass,
                static_cast<std::uint8_t>(offset), &params[offset], chunk);
          }
        }
        break;
      }
    }
  }
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
