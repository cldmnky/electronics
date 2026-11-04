#include "link_client.hpp"

#include <exception>

#include "logging.hpp"

namespace mnky::app {
namespace {
// Cap the queued commands so a stuck link thread cannot grow memory without
// bound; the newest request wins for tempo/LED state anyway.
constexpr std::size_t kMaxCommands = 64;
constexpr std::uint64_t kReadyPollMs = 100;
}  // namespace

LinkClient::LinkClient(MidiGlue& midi, AppState& state, bool emulated)
    : juce::Thread("mnkybox link"),
      midi_(midi),
      state_(state),
      emulated_(emulated) {
  if (emulated_) {
    transport_ = std::make_unique<link::EmulatorTransport>();
    log("link: emulator transport (scripted Pico, no SPI hardware)");
  } else {
    try {
      transport_ = std::make_unique<link::LinuxSpidevTransport>();
      log("link: /dev/spidev0.0 at 1 MHz, mode 0");
    } catch (const std::exception& error) {
      log(juce::String("link: SPI transport unavailable (") + error.what() +
          "); falling back to the emulator transport");
      transport_ = std::make_unique<link::EmulatorTransport>();
      emulated_ = true;
    }
  }

#if defined(__linux__)
  try {
    ready_ = std::make_unique<link::ReadyGpio>();
  } catch (const std::exception& error) {
    log(juce::String("gpio: READY_IRQ (BCM 24) unavailable: ") + error.what());
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
  startThread(juce::Thread::Priority::normal);
}

void LinkClient::stop() {
  signalThreadShouldExit();
  stopThread(3000);
}

LinkStatus LinkClient::status() const {
  const juce::ScopedLock lock(statusLock_);
  return status_;
}

void LinkClient::requestTempo(std::uint16_t bpmX100) {
  Command command;
  command.kind = Command::Kind::Tempo;
  command.value16 = bpmX100;
  const juce::ScopedLock lock(commandLock_);
  if (commands_.size() >= kMaxCommands) commands_.pop_front();
  commands_.push_back(command);
}

void LinkClient::requestTransport(std::uint8_t transportCommand) {
  Command command;
  command.kind = Command::Kind::Transport;
  command.value8 = transportCommand;
  const juce::ScopedLock lock(commandLock_);
  if (commands_.size() >= kMaxCommands) commands_.pop_front();
  commands_.push_back(command);
}

void LinkClient::requestLeds(std::uint8_t mask, std::uint8_t states) {
  Command command;
  command.kind = Command::Kind::Leds;
  command.value8 = mask;
  command.extra8 = states;
  const juce::ScopedLock lock(commandLock_);
  if (commands_.size() >= kMaxCommands) commands_.pop_front();
  commands_.push_back(command);
}

void LinkClient::requestManual(bool manual) {
  Command command;
  command.kind = Command::Kind::Manual;
  command.value8 = manual ? 1 : 0;
  const juce::ScopedLock lock(commandLock_);
  if (commands_.size() >= kMaxCommands) commands_.pop_front();
  commands_.push_back(command);
}

void LinkClient::run() {
  const MidiGlue::TrsSink trsSink = [this](midi::Port destination,
                                           const midi::MidiEvent& event) {
    const std::uint8_t port = destination == midi::Port::TrsOut1
                                  ? 0
                                  : (destination == midi::Port::TrsOut2 ? 1 : 0xFF);
    if (port > 1) return false;
    return link_->sendMidi(port, event.data, event.size);
  };

  while (!threadShouldExit()) {
    applyCommands();
    link_->poll();
    drainEvents();
    midi_.pump(trsSink);
    pumpClock();
    readReady();

    {
      const juce::ScopedLock lock(statusLock_);
      status_.stats = link_->stats();
      status_.linkUp = link_->linkUp();
      status_.picoSession = link_->session();
      status_.piSession = link_->piSession();
    }

    juce::Thread::sleep(1);
  }
}

void LinkClient::applyCommands() {
  for (;;) {
    Command command;
    {
      const juce::ScopedLock lock(commandLock_);
      if (commands_.empty()) break;
      command = commands_.front();
      commands_.pop_front();
    }

    switch (command.kind) {
      case Command::Kind::Tempo:
        link_->setTempo(command.value16);
        {
          const juce::ScopedLock lock(statusLock_);
          clock_.setTempo(command.value16);
          status_.tempoX100 = command.value16;
        }
        state_.tempoX100.store(command.value16);
        break;

      case Command::Kind::Transport:
        link_->transport(command.value8);
        // The USB clock mirror follows the transport: START/CONTINUE/STOP are
        // single bytes and must not wait for the next Pico tick to be mirrored.
        if (command.value8 == 1) {
          midi_.sendUsbClock(midi::kStartByte);
        } else if (command.value8 == 2) {
          midi_.sendUsbClock(midi::kContinueByte);
        } else {
          midi_.sendUsbClock(midi::kStopByte);
        }
        break;

      case Command::Kind::Leds:
        link_->ledSet(command.value8, command.extra8);
        break;

      case Command::Kind::Manual:
        link_->setManual(command.value8 != 0);
        {
          const juce::ScopedLock lock(statusLock_);
          status_.manual = command.value8 != 0;
        }
        break;
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
          const juce::ScopedLock lock(statusLock_);
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
          const juce::ScopedLock lock(statusLock_);
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
          const juce::ScopedLock lock(statusLock_);
          clock_.setTempo(tempo);
          status_.tempoX100 = tempo;
        }
        state_.tempoX100.store(tempo);
        break;
      }

      case link::PicoLink::Event::Tick: {
        const juce::ScopedLock lock(statusLock_);
        status_.tick = event.u32a;
        break;
      }

      case link::PicoLink::Event::State: {
        const bool rolling = event.a[0] != 0;
        {
          const juce::ScopedLock lock(statusLock_);
          status_.rolling = rolling;
          status_.tick = event.u32a;
        }
        state_.rolling.store(rolling);
        break;
      }

      case link::PicoLink::Event::Linked: {
        {
          const juce::ScopedLock lock(statusLock_);
          status_.linkUp = true;
          status_.picoSession = event.u32a;
          status_.piSession = link_->piSession();
        }
        log("link: up, Pico session 0x" + juce::String::toHexString(
                                             static_cast<juce::int64>(event.u32a)));
        break;
      }

      case link::PicoLink::Event::LinkLost: {
        {
          const juce::ScopedLock lock(statusLock_);
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
    const juce::ScopedLock lock(statusLock_);
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
  const juce::ScopedLock lock(statusLock_);
  clock_.addTick(tick, picoStampUs, piNowUs);
  clock_.setRolling(status_.rolling);
  status_.tick = tick;
  status_.clockPrimed = clock_.primed();
  status_.offsetUs = clock_.offsetUs();
  status_.latencyUs = clock_.latencyUs();
  status_.clockSkipped = clock_.skipped();
  status_.clockReordered = clock_.reordered();
  status_.lastClockTick = clock_.lastTick();
}

void LinkClient::readReady() {
  if (ready_ == nullptr) return;
  const auto now = juce::Time::getMillisecondCounter();
  if (now - lastReadyPollMs_ < kReadyPollMs) return;
  lastReadyPollMs_ = now;
  const bool high = ready_->ready();
  const juce::ScopedLock lock(statusLock_);
  status_.ready = high;
  status_.readyValid = true;
}

}  // namespace mnky::app
