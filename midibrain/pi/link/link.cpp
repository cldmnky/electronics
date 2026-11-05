// Implementation of the Pico <-> Pi link core (see link.hpp for the contract).
#include "link.hpp"

#include "../../firmware/pico/clock_follow.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <utility>

#ifdef __linux__
#include <cstring>
#include <fcntl.h>
#include <linux/gpio.h>
#include <linux/spi/spidev.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace mnky::link {
namespace {

// Firmware queue depth (firmware/pico/main.cpp kTxDepth): outbound frames
// beyond it are dropped-newest and counted, never blocked on.
constexpr std::size_t kMaxTxQueue = 16;
// Draining capacity between app polls; overflow is counted in Stats::overruns.
constexpr std::size_t kMaxEvents = 64;
// Emulator session base ("PICO"); reboot() increments it so each power cycle
// looks like a new Pico to the host, as on hardware.
constexpr std::uint32_t kEmulatorSessionBase = 0x5049434fu;

std::uint64_t nowUs() {
  using namespace std::chrono;
  return static_cast<std::uint64_t>(
      duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
}

std::uint32_t mintSession() {
  static std::uint32_t serial = 0;
  const std::uint64_t count = nowUs() * 1000ull + (++serial);
  const std::uint32_t mixed = static_cast<std::uint32_t>(count) ^
                              static_cast<std::uint32_t>(count >> 32) ^ 0x4d4e4b59u;
  return mixed ? mixed : 1u;
}

std::uint32_t readLe32(const std::uint8_t* p) {
  return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
         (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint16_t readLe16(const std::uint8_t* p) {
  return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

std::uint16_t clampTempo(std::uint16_t bpm_x100) {
  if (bpm_x100 < kTempoMinX100) return kTempoMinX100;
  if (bpm_x100 > kTempoMaxX100) return kTempoMaxX100;
  return bpm_x100;
}

}  // namespace

// ---------------------------------------------------------------------------
// EmulatorTransport: scripted Pico
// ---------------------------------------------------------------------------
EmulatorTransport::EmulatorTransport() {
  // Boot: HELLO is the first frame out, then the boot transport state, exactly
  // like the firmware preload (firmware/pico/main.cpp main()).
  pico_session_ = kEmulatorSessionBase + session_serial_;
  mb::Frame hello{};
  hello.type = mb::Type::hello;
  hello.sequence = 0;
  hello.session = pico_session_;
  hello.length = 0;
  mb::Bytes wire{};
  mb::encode(hello, wire);
  repeat_ = wire;
  txq_.push_back(wire);
  next_seq_ = 1;
  mb::Frame state{};
  mb::pack_transport_state(state, allocSeq(), pico_session_, rolling_, tick_);
  queue(state);
  next_tick_us_ = tickIntervalUs();
}

std::uint32_t EmulatorTransport::tickIntervalUs() const {
  // Same formula as the firmware: 60e6 / (bpm * 24) us per 24-PPQN tick.
  return static_cast<std::uint32_t>((60'000'000ull * 100ull) /
                                    (static_cast<std::uint64_t>(bpm_x100_) * 24ull));
}

bool EmulatorTransport::queue(const mb::Frame& f) {
  mb::Bytes wire{};
  if (!mb::encode(f, wire)) return false;
  if (txq_.size() >= kMaxTxQueue) {
    // Drop-newest, as the Pico does; the counter feeds STATUS_RESP txov.
    if (tx_overflow_ != 0xffffu) ++tx_overflow_;
    return false;
  }
  txq_.push_back(wire);
  return true;
}

bool EmulatorTransport::transfer(const std::uint8_t out[32], std::uint8_t in[32]) {
  std::array<std::uint8_t, kFrameBytes> wire{};
  std::copy(out, out + kFrameBytes, wire.begin());
  mb::Frame f{};
  if (!mb::decode(wire, f)) {
    // Bad frame: the Pi miscounted its 32 bytes. Nothing to apply.
  } else if (have_last_rx_ && f.session == last_rx_session_ && f.sequence == last_rx_seq_) {
    // Idle-fill repeat or retried transfer: deduped, never re-applied.
  } else {
    have_last_rx_ = true;
    last_rx_session_ = f.session;
    last_rx_seq_ = f.sequence;
    handle(f);
  }
  advance(kTransactionUs);
  if (!txq_.empty()) {
    repeat_ = txq_.front();
    txq_.pop_front();
  }
  std::copy(repeat_.begin(), repeat_.end(), in);
  return true;
}

void EmulatorTransport::handle(const mb::Frame& f) {
  if (f.type == mb::Type::hello) {
    if (pi_session_ != f.session) {
      pi_session_ = f.session;
      // Pi reboot: its sequence restarts. Like the firmware, the next idle
      // repeat of this HELLO is re-handled once, so the IDEMPOTENT
      // ACK/state/tempo announcements go out twice at connect.
      have_last_rx_ = false;
    }
    manual_ = false;  // reconnect resets panel authority to local
    mb::Frame ack{};
    ack.type = mb::Type::ack;
    ack.sequence = allocSeq();
    ack.session = pico_session_;
    ack.length = 0;
    queue(ack);
    mb::Frame state{};
    mb::pack_transport_state(state, allocSeq(), pico_session_, rolling_, tick_);
    queue(state);
    mb::Frame tempo{};
    mb::pack_tempo(tempo, allocSeq(), pico_session_, bpm_x100_);
    queue(tempo);
    return;
  }
  if (f.type == mb::Type::ack) return;
  if (f.type != mb::Type::control_event || f.length < 1) return;
  switch (f.payload[0]) {
    case mb::EV_SET_TEMPO: {
      if (f.length < 3) return;
      setTempo(readLe16(&f.payload[1]));
      break;
    }
    case mb::EV_TRANSPORT: {
      if (f.length < 2) return;
      const std::uint8_t cmd = f.payload[1];
      if (cmd > 2) return;
      if (cmd == 1) {
        tick_ = 0;
        rolling_ = true;
      } else if (cmd == 2) {
        rolling_ = true;
      } else {
        rolling_ = false;
      }
      mb::Frame state{};
      mb::pack_transport_state(state, allocSeq(), pico_session_, rolling_, tick_);
      queue(state);
      break;
    }
    case mb::EV_CLOCK_ENABLE:
      // Deprecated upstream: the clock is always on. Recorded for tests.
      if (f.length < 2) return;
      clock_enable_seen_ = true;
      break;
    case mb::EV_CLOCK_ROUTE: {
      if (f.length < 3) return;
      const std::uint8_t port = f.payload[1];
      const std::uint8_t flags = f.payload[2];
      if (port > 1 || (flags & ~mb::CLOCK_ROUTE_MASK) != 0) return;
      clock_route_flags_[port] = flags;
      break;
    }
    case mb::EV_CLOCK_SOURCE: {
      if (f.length < 3) return;
      const std::uint8_t source = f.payload[1];
      const std::uint8_t mask = f.payload[2];
      if (source > mb::CLOCK_SOURCE_TRS2 || (mask & ~mb::CLOCK_FOLLOW_MASK) != 0) return;
      clock_source_ = source;
      transport_follow_mask_ = mask;
      break;
    }
    case mb::EV_MIDI_ROUTE: {
      std::uint8_t src = 0, dst = 0, flags = 0;
      if (!mb::unpack_midi_route(f, src, dst, flags)) return;
      // begin(): fresh chain, runtime state cleared, flags applied.
      midi_routes_[src][dst] = MidiRouteState{};
      midi_routes_[src][dst].flags = flags;
      break;
    }
    case mb::EV_MIDI_ROUTE_PIPE: {
      std::uint8_t src = 0, dst = 0, index = 0, type = 0, offset = 0;
      bool bypass = false;
      std::uint16_t ch_mask = 0;
      std::int16_t raw[mb::route_pipe_param_frame_max_legacy]{};
      std::size_t count = 0;
      if (!mb::unpack_midi_route_pipe(f, src, dst, index, type, bypass, offset,
                                      ch_mask, raw,
                                      mb::route_pipe_param_frame_max_legacy, count))
        return;
      MidiRouteState& chain = midi_routes_[src][dst];
      // Same shape rules as the firmware's MidiPipeChain::setPipeChunk: an
      // invalid index/type mismatch is a bad frame, not a silent append.
      if (offset == 0) {
        if (index > chain.pipe_count) return;
      } else {
        if (index >= chain.pipe_count) return;
        // setPipeChunk compares the type only; bypass and chMask on a
        // continuation chunk are ignored by the firmware, not identity fields.
        if (chain.pipes[index].type != type) return;
      }
      RoutePipeState& pipe = chain.pipes[index];
      if (offset == 0) {
        pipe = RoutePipeState{};
        pipe.type = type;
        pipe.bypass = bypass;
        pipe.chMask = ch_mask;
        if (index == chain.pipe_count) ++chain.pipe_count;
      }
      for (std::size_t i = 0; i < count; ++i) pipe.params[offset + i] = raw[i];
      if (offset + count > pipe.param_count)
        pipe.param_count = static_cast<std::uint8_t>(offset + count);
      break;
    }
    case mb::EV_LED_SET:
      if (f.length < 3) return;
      led_mask_ = f.payload[1];
      led_states_ = f.payload[2];
      break;
    case mb::EV_TRANSPORT_MODE:
      if (f.length < 2) return;
      manual_ = f.payload[1] != 0;
      break;
    case mb::EV_MIDI_SEND: {
      if (f.length < 3) return;
      const std::uint8_t port = f.payload[1];
      const std::uint8_t size = f.payload[2];
      if (port > 1 || size < 1 || size > 3 || f.length < 3 + size) return;
      SentMidi m{};
      m.port = port;
      m.size = size;
      m.data[0] = f.payload[3];
      m.data[1] = size > 1 ? f.payload[4] : 0;
      m.data[2] = size > 2 ? f.payload[5] : 0;
      sent_midi_.push_back(m);
      break;
    }
    case mb::EV_STATUS_REQ: {
      if (f.length != 1) return;
      mb::LinkStatus status{};
      if (status_scripted_) {
        status = status_script_;
      } else {
        status.uptime_s = static_cast<std::uint32_t>(now_us_ / 1'000'000ull);
        status.ticks = tick_;
        status.txq_depth = static_cast<std::uint8_t>(txq_.size());
        status.txov = tx_overflow_;
        status.rxov = rx_overflow_;
        status.flags = static_cast<std::uint8_t>(
            (pi_session_ != 0 ? mb::STATUS_LINK_UP : 0) | (rolling_ ? mb::STATUS_ROLLING : 0));
      }
      mb::Frame resp{};
      mb::pack_status_resp(resp, allocSeq(), pico_session_, status);
      queue(resp);
      break;
    }
    case mb::EV_REBOOT:
      if (f.length != 2 || f.payload[1] > 1) return;
      reboot_requested_ = true;
      if (f.payload[1] == 0) {
        reboot();  // normal firmware reboot: new session, link re-handshakes
      } else {
        usb_boot_requested_ = true;  // BOOTSEL: the application link goes away
      }
      break;
    case mb::EV_RESET_LINK:
      if (f.length != 1) return;
      resetLinkSession();
      break;
    default:
      break;  // Pi->Pico unknown kind, or a Pico->Pi kind echoed back
  }
}

void EmulatorTransport::advance(std::uint64_t us) {
  now_us_ += us;
  // The Pico tick stream is unconditional (firmware clock_tick), so frames are
  // emitted regardless of transport state; 0x06 state frames carry rolling.
  while (now_us_ >= next_tick_us_) {
    const std::uint32_t stamp = static_cast<std::uint32_t>(next_tick_us_);
    ++tick_;
    mb::Frame f{};
    mb::pack_tick(f, allocSeq(), pico_session_, tick_, stamp);
    queue(f);
    next_tick_us_ += tickIntervalUs();
  }
}

void EmulatorTransport::setTempo(std::uint16_t bpm_x100) {
  bpm_x100_ = clampTempo(bpm_x100);
  mb::Frame f{};
  mb::pack_tempo(f, allocSeq(), pico_session_, bpm_x100_);
  queue(f);
}

void EmulatorTransport::injectButton(std::uint8_t id, std::uint8_t value) {
  mb::Frame f{};
  mb::pack_control(f, allocSeq(), pico_session_, id, value);
  queue(f);
}

bool EmulatorTransport::injectMidi(std::uint8_t port, const std::uint8_t* data, std::uint8_t n) {
  if (port > 1 || n < 1 || n > 3 || !data) return false;
  mb::MidiMessage m{};
  m.port = port;
  m.size = n;
  for (std::uint8_t i = 0; i < n; ++i) m.data[i] = data[i];
  m.stamp_us = static_cast<std::uint32_t>(now_us_);
  mb::Frame f{};
  if (!mb::pack_midi(f, allocSeq(), pico_session_, m)) return false;
  return queue(f);
}

void EmulatorTransport::reboot() {
  ++session_serial_;
  pico_session_ = kEmulatorSessionBase + session_serial_;
  pi_session_ = 0;
  have_last_rx_ = false;
  last_rx_session_ = 0;
  last_rx_seq_ = 0;
  next_seq_ = 0;
  txq_.clear();
  tick_ = 0;
  rolling_ = false;
  manual_ = false;
  clock_enable_seen_ = false;
  clock_route_flags_[0] = mb::CLOCK_ROUTE_DEFAULT;
  clock_route_flags_[1] = mb::CLOCK_ROUTE_DEFAULT;
  clock_source_ = mb::CLOCK_SOURCE_INTERNAL;
  transport_follow_mask_ = 0;
  // The Pico's route_pipes are RAM: a power cycle starts them off and empty.
  midi_routes_ = {};
  led_mask_ = 0;
  led_states_ = 0;
  sent_midi_.clear();
  mb::Frame hello{};
  hello.type = mb::Type::hello;
  hello.sequence = allocSeq();
  hello.session = pico_session_;
  hello.length = 0;
  mb::Bytes wire{};
  mb::encode(hello, wire);
  repeat_ = wire;
  txq_.push_back(wire);
  next_tick_us_ = now_us_ + tickIntervalUs();
}

void EmulatorTransport::resetLink() { resetLinkSession(); }

void EmulatorTransport::resetLinkSession() {
  // Same new-session + state/tempo announcement as boot, but musical and LED
  // state survive; only the link handshake restarts.
  ++session_serial_;
  pico_session_ = kEmulatorSessionBase + session_serial_;
  pi_session_ = 0;
  // Keep the Pi-frame dedup state so a repeated RESET_LINK frame cannot mint
  // a second session while the Pi switches to its post-reset HELLO.
  next_seq_ = 0;
  txq_.clear();
  manual_ = false;  // reconnect defaults to local, as after HELLO
  mb::Frame hello{};
  hello.type = mb::Type::hello;
  hello.sequence = allocSeq();
  hello.session = pico_session_;
  hello.length = 0;
  mb::Bytes wire{};
  mb::encode(hello, wire);
  repeat_ = wire;
  txq_.push_back(wire);
  mb::Frame state{};
  mb::pack_transport_state(state, allocSeq(), pico_session_, rolling_, tick_);
  queue(state);
  mb::Frame tempo{};
  mb::pack_tempo(tempo, allocSeq(), pico_session_, bpm_x100_);
  queue(tempo);
  next_tick_us_ = now_us_ + tickIntervalUs();
}

// ---------------------------------------------------------------------------
// Linux transports (unvalidated on hardware; see NOTES.md)
// ---------------------------------------------------------------------------
#ifdef __linux__
LinuxSpidevTransport::LinuxSpidevTransport(const char* device, std::uint32_t clock_hz)
    : clock_hz_(clock_hz) {
  fd_ = ::open(device, O_RDWR | O_CLOEXEC);
  if (fd_ < 0) throw std::runtime_error("mnky::link: cannot open spidev device");
  std::uint8_t mode = SPI_MODE_0;
  std::uint8_t bits = 8;
  if (ioctl(fd_, SPI_IOC_WR_MODE, &mode) < 0 || ioctl(fd_, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0 ||
      ioctl(fd_, SPI_IOC_WR_MAX_SPEED_HZ, &clock_hz_) < 0) {
    ::close(fd_);
    fd_ = -1;
    throw std::runtime_error("mnky::link: spidev mode/speed setup failed");
  }
}

LinuxSpidevTransport::~LinuxSpidevTransport() {
  if (fd_ >= 0) ::close(fd_);
}

bool LinuxSpidevTransport::transfer(const std::uint8_t out[32], std::uint8_t in[32]) {
  if (fd_ < 0) return false;
  std::array<std::uint8_t, kFrameBytes> tx{};
  std::array<std::uint8_t, kFrameBytes> rx{};
  std::copy(out, out + kFrameBytes, tx.begin());
  struct spi_ioc_transfer tr {};
  tr.tx_buf = reinterpret_cast<unsigned long>(tx.data());
  tr.rx_buf = reinterpret_cast<unsigned long>(rx.data());
  tr.len = static_cast<std::uint32_t>(kFrameBytes);
  tr.speed_hz = clock_hz_;
  tr.bits_per_word = 8;
  tr.cs_change = 0;
  tr.delay_usecs = 0;
  // spidev asserts CSn for exactly one transfer; the kernel's minimum
  // CS-to-first-clock delay already exceeds the Pico's 10 us arming window, so
  // no extra delay is inserted here. Any short result is a failed exchange.
  if (ioctl(fd_, SPI_IOC_MESSAGE(1), &tr) < static_cast<int>(kFrameBytes)) return false;
  std::copy(rx.begin(), rx.end(), in);
  return true;
}

ReadyGpio::ReadyGpio(unsigned bcm, const char* chip) {
  fd_ = ::open(chip, O_RDWR | O_CLOEXEC);
  if (fd_ < 0) throw std::runtime_error("mnky::link: cannot open gpiochip");
  struct gpio_v2_line_request req {};
  req.offsets[0] = bcm;
  req.num_lines = 1;
  req.config.flags = GPIO_V2_LINE_FLAG_INPUT;
  std::strncpy(req.consumer, "mnkybox-link", sizeof(req.consumer) - 1);
  if (ioctl(fd_, GPIO_V2_GET_LINE_IOCTL, &req) < 0) {
    ::close(fd_);
    fd_ = -1;
    throw std::runtime_error("mnky::link: gpio line request failed");
  }
  request_fd_ = req.fd;
}

ReadyGpio::~ReadyGpio() {
  if (request_fd_ >= 0) ::close(request_fd_);
  if (fd_ >= 0) ::close(fd_);
}

bool ReadyGpio::ready() const {
  if (request_fd_ < 0) return false;
  struct gpio_v2_line_values values {};
  values.mask = 1;
  if (ioctl(request_fd_, GPIO_V2_LINE_GET_VALUES_IOCTL, &values) < 0) return false;
  return (values.bits & 1u) != 0;
}
#else
LinuxSpidevTransport::LinuxSpidevTransport(const char* device, std::uint32_t clock_hz) {
  (void)device;
  (void)clock_hz;
  throw std::runtime_error("mnky::link: LinuxSpidevTransport requires Linux spidev");
}

bool LinuxSpidevTransport::transfer(const std::uint8_t out[32], std::uint8_t in[32]) {
  (void)out;
  (void)in;
  return false;
}

ReadyGpio::ReadyGpio(unsigned bcm, const char* chip) {
  (void)bcm;
  (void)chip;
  throw std::runtime_error("mnky::link: ReadyGpio requires the Linux GPIO chardev");
}

bool ReadyGpio::ready() const { return false; }
#endif

// ---------------------------------------------------------------------------
// PicoLink: Pi side of the protocol
// ---------------------------------------------------------------------------
PicoLink::PicoLink(ISpiTransport& transport) : transport_(transport) {
  pi_session_ = mintSession();
  enqueueHello();  // the boot HELLO: repeated as idle fill until new frames
}

bool PicoLink::enqueueHello() {
  mb::Frame f{};
  f.type = mb::Type::hello;
  f.sequence = next_seq_++;
  f.session = pi_session_;
  f.length = 0;
  mb::Bytes wire{};
  if (!mb::encode(f, wire)) return false;
  // Not queued: the HELLO is the idle filler, so it keeps going out until the
  // Pico's ACK proves the link is up and real frames take over.
  repeat_ = wire;
  return true;
}

bool PicoLink::queueFrame(mb::Frame f) {
  f.session = pi_session_;
  f.sequence = next_seq_++;
  mb::Bytes wire{};
  if (!mb::encode(f, wire)) return false;
  if (txq_.size() >= kMaxTxQueue) {
    ++stats_.overruns;
    return false;
  }
  txq_.push_back(wire);
  return true;
}

void PicoLink::push(const Event& e) {
  if (events_.size() >= kMaxEvents) {
    ++stats_.overruns;
    return;
  }
  events_.push_back(e);
}

void PicoLink::poll() {
  const std::uint64_t now = nowUs();
  if (last_xfer_us_ != 0 && now - last_xfer_us_ < kMinTransactionUs) return;
  last_xfer_us_ = now;
  const mb::Bytes& out = txq_.empty() ? repeat_ : txq_.front();
  std::array<std::uint8_t, kFrameBytes> in{};
  if (!transport_.transfer(out.data(), in.data())) {
    ++stats_.bad;  // aborted exchange: the queue head stays for the retry
    return;
  }
  if (!txq_.empty()) {
    repeat_ = txq_.front();
    txq_.pop_front();
  }
  ++stats_.tx;
  mb::Frame f{};
  if (!mb::decode(in, f)) {
    ++stats_.bad;
    return;
  }
  ++stats_.rx;
  handle(f, now);
}

void PicoLink::handle(const mb::Frame& f, std::uint64_t now_us) {
  if (f.session != pico_session_) {
    // A different Pico session id means it rebooted: drop the stale outbound
    // queue, restart the handshake and tell the app the link went away.
    const bool reconnect = pico_session_ != 0;
    if (reconnect && link_up_) {
      Event lost{};
      lost.kind = Event::LinkLost;
      lost.u32a = f.session;
      push(lost);
    }
    pico_session_ = f.session;
    have_remote_seq_ = false;
    link_up_ = false;
    have_status_ = false;  // cached snapshot belonged to the old session
    txq_.clear();
    enqueueHello();
  }
  if (have_remote_seq_ && f.sequence == remote_seq_) {
    ++stats_.dup;  // idle-fill repeat or retried transfer: never re-applied
    return;
  }
  have_remote_seq_ = true;
  remote_seq_ = f.sequence;
  switch (f.type) {
    case mb::Type::hello:
      break;  // our repeated HELLO answers it; the ACK confirms link-up
    case mb::Type::ack:
      if (!link_up_) {
        link_up_ = true;
        Event up{};
        up.kind = Event::Linked;
        up.u32a = pico_session_;
        push(up);
        // The Pico reset its RAM gates at boot; re-assert the last-known
        // per-output route now that the session is confirmed.
        resendClockRoutes();
        resendClockSource();
        resendMidiRoutes();
      }
      break;
    case mb::Type::control_event:
      handleControl(f, now_us);
      break;
  }
}

void PicoLink::handleControl(const mb::Frame& f, std::uint64_t now_us) {
  if (f.length < 1) {
    ++stats_.bad;
    return;
  }
  Event e{};
  switch (f.payload[0]) {
    case mb::EV_MIDI_MSG: {
      mb::MidiMessage m{};
      if (!mb::unpack_midi(f, m)) {
        ++stats_.bad;
        return;
      }
      e.kind = Event::MidiIn;
      e.a[0] = m.data[0];
      e.a[1] = m.data[1];
      e.a[2] = m.data[2];
      e.n = m.size;
      e.u32a = m.port;
      e.u32b = m.stamp_us;
      break;
    }
    case mb::EV_CONTROL:
      if (f.length < 3) {
        ++stats_.bad;
        return;
      }
      e.kind = Event::Control;
      e.a[0] = f.payload[1];
      e.a[1] = f.payload[2];
      e.n = 2;
      break;
    case mb::EV_TEMPO:
      if (f.length < 3) {
        ++stats_.bad;
        return;
      }
      e.kind = Event::Tempo;
      e.u32a = readLe16(&f.payload[1]);
      break;
    case mb::EV_CLOCK_TICK: {
      if (f.length < 9) {
        ++stats_.bad;
        return;
      }
      const std::uint32_t tick = readLe32(&f.payload[1]);
      const std::uint32_t stamp = readLe32(&f.payload[5]);
      e.kind = Event::Tick;
      e.u32a = tick;
      e.u32b = stamp;
      // Callback first: it is the low-latency path for the USB clock mirror
      // and must not wait for the app to drain the event queue.
      if (tick_fn_) tick_fn_(tick, stamp, now_us);
      break;
    }
    case mb::EV_TRANSPORT_STATE:
      if (f.length < 6) {
        ++stats_.bad;
        return;
      }
      e.kind = Event::State;
      e.a[0] = f.payload[1];
      e.n = 1;
      e.u32a = readLe32(&f.payload[2]);
      break;
    case mb::EV_STATUS_RESP: {
      mb::LinkStatus s{};
      if (!mb::unpack_status_resp(f, s)) {
        ++stats_.bad;
        return;
      }
      status_ = s;
      have_status_ = true;
      return;  // query answer, not an asynchronous event
    }
    default:
      return;  // Pi->Pico kinds echoed back, or future commands: ignored
  }
  push(e);
}

bool PicoLink::nextEvent(Event& out) {
  if (events_.empty()) return false;
  out = events_.front();
  events_.pop_front();
  return true;
}

bool PicoLink::sendMidi(std::uint8_t port, const std::uint8_t* data, std::uint8_t n) {
  if (port > 1 || n < 1 || n > 3 || !data) return false;
  mb::Frame f{};
  f.type = mb::Type::control_event;
  f.length = static_cast<std::uint8_t>(3 + n);
  f.payload[0] = mb::EV_MIDI_SEND;
  f.payload[1] = port;
  f.payload[2] = n;
  for (std::uint8_t i = 0; i < n; ++i) f.payload[3 + i] = data[i];
  return queueFrame(f);
}

bool PicoLink::sendMidiRoute(std::uint8_t src, std::uint8_t dst,
                             const std::uint8_t* data, std::uint8_t n) {
  if (src > 1 || dst > 1 || n < 1 || n > 3 || !data) return false;
  mb::Frame f{};
  if (!mb::pack_midi_route_send(f, 0, 0, src, dst, data, n)) return false;
  return queueFrame(f);
}

bool PicoLink::setTempo(std::uint16_t bpm_x100) {
  const std::uint16_t clamped = clampTempo(bpm_x100);
  mb::Frame f{};
  f.type = mb::Type::control_event;
  f.length = 3;
  f.payload[0] = mb::EV_SET_TEMPO;
  f.payload[1] = static_cast<std::uint8_t>(clamped);
  f.payload[2] = static_cast<std::uint8_t>(clamped >> 8);
  return queueFrame(f);
}

bool PicoLink::transport(std::uint8_t cmd) {
  if (cmd > 2) return false;
  mb::Frame f{};
  f.type = mb::Type::control_event;
  f.length = 2;
  f.payload[0] = mb::EV_TRANSPORT;
  f.payload[1] = cmd;
  return queueFrame(f);
}

bool PicoLink::clockRoute(std::uint8_t port, std::uint8_t flags) {
  mb::Frame f{};
  if (!mb::pack_clock_route(f, 0, 0, port, flags)) return false;
  if (!queueFrame(f)) return false;
  // Remember only what actually went out: the cache is the last-known route
  // that a reconnect must re-assert, not an unqueued caller intent.
  clock_route_flags_[port] = flags;
  clock_route_known_[port] = true;
  return true;
}

void PicoLink::resendClockRoutes() {
  for (std::uint8_t port = 0; port < 2; ++port) {
    if (!clock_route_known_[port]) continue;
    mb::Frame f{};
    if (!mb::pack_clock_route(f, 0, 0, port, clock_route_flags_[port])) continue;
    queueFrame(f);
  }
}

bool PicoLink::clockSource(std::uint8_t source, std::uint8_t transport_mask) {
  mb::Frame f{};
  if (!mb::pack_clock_source(f, 0, 0, source, transport_mask)) return false;
  if (!queueFrame(f)) return false;
  clock_source_ = source;
  clock_source_mask_ = transport_mask;
  clock_source_known_ = true;
  return true;
}

void PicoLink::resendClockSource() {
  if (!clock_source_known_) return;
  mb::Frame f{};
  if (!mb::pack_clock_source(f, 0, 0, clock_source_, clock_source_mask_)) return;
  queueFrame(f);
}

bool PicoLink::midiRoute(std::uint8_t src, std::uint8_t dst, bool on) {
  const std::uint8_t flags = on ? mb::ROUTE_FLAG_ON : 0;
  mb::Frame f{};
  if (!mb::pack_midi_route(f, 0, 0, src, dst, flags)) return false;
  if (!queueFrame(f)) return false;
  // 0x18 starts a fresh chain on the Pico: the cache mirrors that, so a
  // reconnect re-sends the begin before any pipe chunk of this route.
  CachedMidiRoute& cache = midi_routes_[src][dst];
  cache = CachedMidiRoute{};
  cache.known = true;
  cache.flags = flags;
  return true;
}

bool PicoLink::midiRoutePipe(std::uint8_t src, std::uint8_t dst, std::uint8_t index,
                             std::uint8_t type, bool bypass,
                             std::uint8_t param_offset, const std::int16_t* params,
                             std::size_t count, std::uint16_t chMask) {
  if (src > 1 || dst > 1) return false;  // only the two physical TRS routes
  if (count != 0 && params == nullptr) return false;
  CachedMidiRoute& cache = midi_routes_[src][dst];
  // Mirror the firmware's setPipeChunk acceptance before queueing, so the
  // cache can never describe a chain the Pico refused.
  if (param_offset == 0) {
    if (index > cache.pipe_count) return false;
  } else {
    if (index >= cache.pipe_count) return false;
    // setPipeChunk's continuation identity is the type; bypass is ignored on
    // an offset > 0 chunk, so mirror that instead of rejecting the frame.
    if (cache.pipes[index].type != type) return false;
  }
  mb::Frame f{};
  if (!mb::pack_midi_route_pipe(f, 0, 0, src, dst, index, type, bypass,
                                param_offset, params, count, chMask))
    return false;
  if (!queueFrame(f)) return false;
  CachedRoutePipe& pipe = cache.pipes[index];
  if (param_offset == 0) {
    pipe = CachedRoutePipe{};
    pipe.type = type;
    pipe.bypass = bypass;
    pipe.chMask = chMask;
    if (index == cache.pipe_count) ++cache.pipe_count;
  }
  for (std::size_t i = 0; i < count; ++i) pipe.params[param_offset + i] = params[i];
  if (param_offset + count > pipe.param_count)
    pipe.param_count = static_cast<std::uint8_t>(param_offset + count);
  // A pipe before its 0x18 still describes a real (off) chain; remember it so
  // a reconnect reproduces the Pico state instead of dropping the config.
  cache.known = true;
  return true;
}

void PicoLink::resendMidiRoutes() {
  for (std::uint8_t src = 0; src < 2; ++src) {
    for (std::uint8_t dst = 0; dst < 2; ++dst) {
      const CachedMidiRoute& cache = midi_routes_[src][dst];
      if (!cache.known) continue;
      mb::Frame begin{};
      if (!mb::pack_midi_route(begin, 0, 0, src, dst, cache.flags)) continue;
      queueFrame(begin);
      for (std::uint8_t index = 0; index < cache.pipe_count; ++index) {
        const CachedRoutePipe& pipe = cache.pipes[index];
        for (std::uint8_t offset = 0; offset < pipe.param_count;
             offset = static_cast<std::uint8_t>(
                 offset + mb::route_pipe_param_frame_max)) {
          const std::uint8_t remaining =
              static_cast<std::uint8_t>(pipe.param_count - offset);
          const std::uint8_t chunk = static_cast<std::uint8_t>(
              remaining < mb::route_pipe_param_frame_max
                  ? remaining
                  : mb::route_pipe_param_frame_max);
          mb::Frame f{};
          if (!mb::pack_midi_route_pipe(f, 0, 0, src, dst, index, pipe.type,
                                        pipe.bypass, offset,
                                        &pipe.params[offset], chunk, pipe.chMask))
            continue;
          queueFrame(f);
        }
      }
    }
  }
}

bool PicoLink::clockEnable(bool on) {
  mb::Frame f{};
  f.type = mb::Type::control_event;
  f.length = 2;
  f.payload[0] = mb::EV_CLOCK_ENABLE;
  f.payload[1] = on ? 1 : 0;
  return queueFrame(f);
}

bool PicoLink::ledSet(std::uint8_t mask, std::uint8_t states) {
  mb::Frame f{};
  f.type = mb::Type::control_event;
  f.length = 3;
  f.payload[0] = mb::EV_LED_SET;
  f.payload[1] = mask;
  f.payload[2] = states;
  return queueFrame(f);
}

bool PicoLink::setManual(bool manual) {
  mb::Frame f{};
  f.type = mb::Type::control_event;
  f.length = 2;
  f.payload[0] = mb::EV_TRANSPORT_MODE;
  f.payload[1] = manual ? 1 : 0;
  return queueFrame(f);
}

bool PicoLink::statusRequest() {
  mb::Frame f{};
  if (!mb::pack_status_req(f, 0, 0)) return false;
  return queueFrame(f);
}

bool PicoLink::rebootPico(bool usbBoot) {
  mb::Frame f{};
  if (!mb::pack_reboot(f, 0, 0, usbBoot ? 1 : 0)) return false;
  return queueFrame(f);
}

bool PicoLink::resetLink() {
  mb::Frame f{};
  if (!mb::pack_reset_link(f, 0, 0)) return false;
  return queueFrame(f);
}

bool PicoLink::lastStatus(mb::LinkStatus& out) const {
  if (!have_status_) return false;
  out = status_;
  return true;
}

}  // namespace mnky::link
