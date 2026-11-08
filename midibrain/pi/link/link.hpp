#pragma once
// JUCE-free Pico <-> Pi SPI link core. The 32-byte wire format, CRC, registry
// and payload layouts are single-sourced from the firmware codec; this module
// only transports those frames (docs/protocol.md is the contract). Pi-only
// code (spidev, GPIO character device) is behind #ifdef __linux__ with
// throwing stubs elsewhere, so the core builds and tests on macOS.
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <utility>
#include <vector>

#include "../../firmware/pico/protocol.hpp"
#include "../../firmware/pico/midi.hpp"

namespace mnky::link {

// Pi BCM 24 = READY_IRQ in tscircuit/lib/pin-map.ts (driven by Pico GP20).
inline constexpr unsigned kReadyBcm = 24;
// SPI0 mode 0, MSB first, 1 MHz nominal on both ends (docs/protocol.md).
inline constexpr std::uint32_t kSpiClockHz = 1'000'000;
// One transaction carries exactly 32 bytes; anything less hangs the slave.
inline constexpr std::size_t kFrameBytes = mb::frame_size;
// 0x19 (EV_MIDI_ROUTE_PIPE) carries the per-pipe channel scope (chMask) after
// the positional int16 params, so a TRS->TRS chain's scope reaches the
// firmware and the shared pipe core applies it. The mask rides the offset-0
// chunk; continuation chunks keep the established scope. A legacy frame
// without the trailing mask still decodes as all-channels (backward
// tolerance), see firmware/pico/protocol.hpp.
inline constexpr bool kMidiRoutePipeCarriesChannelScope = true;
// Pacing budget for PicoLink::poll: >= 1 ms between transactions so the Pico
// main loop drains each received frame before the next one lands.
inline constexpr std::uint64_t kMinTransactionUs = 1'000;
// Firmware clamps SET_TEMPO to the same window (firmware/pico/main.cpp).
inline constexpr std::uint16_t kTempoMinX100 = 2'000;
inline constexpr std::uint16_t kTempoMaxX100 = 30'000;
inline constexpr std::uint16_t kTempoDefaultX100 = 12'000;

// One full-duplex exchange: out is sent while in is received. Returns false
// when the transfer did not complete (link core retries and counts it).
class ISpiTransport {
 public:
  virtual ~ISpiTransport() = default;
  virtual bool transfer(const std::uint8_t out[32], std::uint8_t in[32]) = 0;
};

// Scripted Pico for headless use: answers HELLO with ACK, echoes tempo and
// transport state, emits tick frames from its own device clock, and records
// what the Pi asked it to send. Mirrors firmware/pico/main.cpp behaviour,
// including idle fill (repeat of the last frame) and (session, sequence)
// dedup. Device time advances by kTransactionUs per exchange, matching the
// PicoLink pacing, so tick counts are reproducible in tests.
class EmulatorTransport : public ISpiTransport {
 public:
  struct SentMidi {
    std::uint8_t port{};
    std::uint8_t size{};
    std::uint8_t data[3]{};
  };
  // Emulator device time per SPI exchange (PicoLink paces >= 1 ms).
  static constexpr std::uint64_t kTransactionUs = kMinTransactionUs;

  EmulatorTransport();
  bool transfer(const std::uint8_t out[32], std::uint8_t in[32]) override;

  // Scripted panel input, queued as CONTROL_EVENT frames.
  void injectButton(std::uint8_t id, std::uint8_t value);
  bool injectMidi(std::uint8_t port, const std::uint8_t* data, std::uint8_t n);
  // MIDI FX chain the Pi configured on this emulated Pico (0x18/0x19). flags
  // bit0 = route on; pipe params are positional int16 like the wire and
  // chMask is the per-pipe channel scope carried on the offset-0 chunk. src is
  // the wire route-source byte: 0/1 = TRS, mb::route_src_generator, or
  // mb::route_src_insert(slot).
  struct RoutePipeState {
    std::uint8_t type{};
    bool bypass{};
    std::uint16_t chMask{mb::route_pipe_channel_mask_default};
    std::uint8_t param_count{};
    std::array<std::int16_t, mb::route_pipe_param_max> params{};
  };
  struct MidiRouteState {
    std::uint8_t flags{};
    std::uint8_t pipe_count{};
    std::array<RoutePipeState, mb::route_pipe_count_max> pipes{};
  };
  // Missing chains read as the boot state (off, empty).
  const MidiRouteState& midiRoute(std::uint8_t src, std::uint8_t dst) const {
    static const MidiRouteState empty;
    const auto found = midi_routes_.find(midiRouteKey(src, dst));
    return found == midi_routes_.end() ? empty : found->second;
  }
  // Local tempo change (encoder path): affects tick rate, queues EV_TEMPO.
  void setTempo(std::uint16_t bpm_x100);
  // Power-cycle: new session id, handshake state cleared.
  void reboot();
  // 0x23 RESET_LINK: new session id and state/tempo re-announce, but tempo,
  // transport, tick count and LED state survive (the firmware does not reboot).
  void resetLink();
  // Scripted 0x21 STATUS_RESP values. When unset the emulator derives the
  // snapshot from its live state; tests use this for exact round trips.
  void scriptStatus(const mb::LinkStatus& status) {
    status_script_ = status;
    status_scripted_ = true;
  }
  bool rebootRequested() const { return reboot_requested_; }
  bool usbBootRequested() const { return usb_boot_requested_; }

  std::uint32_t session() const { return pico_session_; }
  std::uint16_t tempoX100() const { return bpm_x100_; }
  bool rolling() const { return rolling_; }
  bool manual() const { return manual_; }
  bool clockEnableSeen() const { return clock_enable_seen_; }
  // Last EV_CLOCK_SOURCE applied (0 internal, 1 TRS IN1, 2 TRS IN2) and the
  // transport-follow mask (bits 0/1). Both default off, as at Pico boot;
  // reboot() resets them, resetLink() keeps them.
  std::uint8_t clockSource() const { return clock_source_; }
  std::uint8_t transportFollowMask() const { return transport_follow_mask_; }
  // Last EV_CLOCK_ROUTE flags applied for output 0/1 (defaults on, as the
  // firmware's boot state; reboot() resets them, resetLink() keeps them).
  std::uint8_t clockRouteFlags(std::uint8_t port) const {
    return clock_route_flags_[port];
  }
  std::uint8_t ledMask() const { return led_mask_; }
  std::uint8_t ledStates() const { return led_states_; }
  std::uint32_t ticks() const { return tick_; }
  const std::vector<SentMidi>& sentMidi() const { return sent_midi_; }
  void clearSentMidi() { sent_midi_.clear(); }

 private:
  void handle(const mb::Frame& f);
  void advance(std::uint64_t us);
  void resetLinkSession();
  bool queue(const mb::Frame& f);
  std::uint16_t allocSeq() { return next_seq_++; }
  std::uint32_t tickIntervalUs() const;

  std::uint32_t pico_session_ = 0;
  std::uint32_t session_serial_ = 0;
  std::uint32_t pi_session_ = 0;
  bool have_last_rx_ = false;
  std::uint32_t last_rx_session_ = 0;
  std::uint16_t last_rx_seq_ = 0;
  std::uint16_t next_seq_ = 1;
  std::deque<mb::Bytes> txq_;
  mb::Bytes repeat_{};
  std::uint64_t now_us_ = 0;
  std::uint64_t next_tick_us_ = 0;
  std::uint32_t tick_ = 0;
  bool rolling_ = false;
  bool manual_ = false;
  bool clock_enable_seen_ = false;
  std::uint8_t clock_source_ = 0;          // 0 internal, 1 TRS IN1, 2 TRS IN2
  std::uint8_t transport_follow_mask_ = 0; // bit0 TRS IN1, bit1 TRS IN2
  std::uint16_t bpm_x100_ = kTempoDefaultX100;
  std::uint8_t clock_route_flags_[2] = {mb::CLOCK_ROUTE_DEFAULT,
                                        mb::CLOCK_ROUTE_DEFAULT};
  std::uint8_t led_mask_ = 0;
  std::uint8_t led_states_ = 0;
  // Wave-1 chain state per physical TRS route, [src][dst] (0 = IN1/OUT1),
  // mirroring the firmware's route_pipes[2][2]: a begin clears it and pipe
  // chunks fill it in order. reboot() resets it to the boot default.
  static constexpr int midiRouteKey(std::uint8_t src, std::uint8_t dst) {
    return (static_cast<int>(src) << 8) | dst;
  }
  std::map<int, MidiRouteState> midi_routes_{};
  std::uint16_t tx_overflow_ = 0;
  std::uint16_t rx_overflow_ = 0;
  bool reboot_requested_ = false;
  bool usb_boot_requested_ = false;
  mb::LinkStatus status_script_{};
  bool status_scripted_ = false;
  std::vector<SentMidi> sent_midi_;
};

#ifdef __linux__
// /dev/spidev0.0 at 1 MHz, mode 0. Every call is one CSn assertion with
// exactly 32 bytes or it reports failure (a short exchange hangs the Pico
// until its watchdog). Not yet validated on Pi hardware.
class LinuxSpidevTransport : public ISpiTransport {
 public:
  explicit LinuxSpidevTransport(const char* device = "/dev/spidev0.0",
                                std::uint32_t clock_hz = kSpiClockHz);
  ~LinuxSpidevTransport() override;
  LinuxSpidevTransport(const LinuxSpidevTransport&) = delete;
  LinuxSpidevTransport& operator=(const LinuxSpidevTransport&) = delete;
  bool transfer(const std::uint8_t out[32], std::uint8_t in[32]) override;

 private:
  int fd_ = -1;
  std::uint32_t clock_hz_ = kSpiClockHz;
};

// READY_IRQ (BCM 24) read through /dev/gpiochip0, polled. The link core does
// not need it (transactions are paced by time), so it is an optional hint.
class ReadyGpio {
 public:
  explicit ReadyGpio(unsigned bcm = kReadyBcm, const char* chip = "/dev/gpiochip0");
  ~ReadyGpio();
  ReadyGpio(const ReadyGpio&) = delete;
  ReadyGpio& operator=(const ReadyGpio&) = delete;
  bool ready() const;

 private:
  int fd_ = -1;
  int request_fd_ = -1;
};
#else
// Portable stand-ins: constructing them throws std::runtime_error so callers
// on macOS fail loudly instead of silently running an unconnected link.
class LinuxSpidevTransport : public ISpiTransport {
 public:
  explicit LinuxSpidevTransport(const char* device = "/dev/spidev0.0",
                                std::uint32_t clock_hz = kSpiClockHz);
  bool transfer(const std::uint8_t out[32], std::uint8_t in[32]) override;
};

class ReadyGpio {
 public:
  explicit ReadyGpio(unsigned bcm = kReadyBcm, const char* chip = "/dev/gpiochip0");
  bool ready() const;
};
#endif

// Pi side of the protocol: owns handshake, session, sequence allocation,
// remote dedup, the outbound frame queue and the drained event queue. Any
// ISpiTransport can drive it (emulator here, spidev on the Pi).
class PicoLink {
 public:
  struct Event {
    enum Kind { MidiIn, Control, Tempo, Tick, State, Linked, LinkLost };
    Kind kind{};
    // Layout per kind (docs/protocol.md control registry):
    //  MidiIn   a[0..2] = message bytes, n = size, u32a = port, u32b = stamp_us
    //  Control  a[0] = CTL_* id, a[1] = state / int8 encoder delta, n = 2
    //  Tempo    u32a = bpm x 100, n = 0
    //  Tick     u32a = tick count, u32b = Pico stamp_us, n = 0
    //  State    a[0] = rolling, n = 1, u32a = tick count at the transition
    //  Linked / LinkLost  u32a = Pico session, n = 0
    // STATUS_RESP is a query answer, not an asynchronous event: it is cached
    // and read with lastStatus() so exhaustive event switches stay unchanged.
    std::uint8_t a[10]{};
    std::uint8_t n{};
    std::uint32_t u32a{};
    std::uint32_t u32b{};
  };
  // tick, Pico stamp_us, Pi monotonic time at delivery (never the tick time).
  using TickFn = std::function<void(std::uint32_t tick, std::uint32_t picoStampUs,
                                    std::uint64_t piNowUs)>;
  struct Stats {
    std::uint64_t tx{};
    std::uint64_t rx{};
    std::uint64_t bad{};
    std::uint64_t dup{};
    std::uint64_t overruns{};
    // App-level reliability accounting (control frames only): retries sent
    // after a missing ack, per-frame acks consumed, Note Offs that had to be
    // refused because the queue held only Note Offs, and safe frames evicted
    // to make room for a Note Off.
    std::uint64_t retries{};
    std::uint64_t acks{};
    std::uint64_t noteOffDrops{};
    std::uint64_t evicted{};
  };

  explicit PicoLink(ISpiTransport& transport);

  // Runs at most one paced transaction (>= kMinTransactionUs apart); cheap to
  // call in a tight loop.
  void poll();
  bool linkUp() const { return link_up_; }
  bool nextEvent(Event& out);
  // Immediate UART emit on the Pico. port 0/1, 1..3 bytes.
  bool sendMidi(std::uint8_t port, const std::uint8_t* data, std::uint8_t n);
  // Routed UART emit (0x1A): the Pico runs the configured (src,dst) pipe chain
  // and writes its output on dst. src/dst are 0 = TRS IN1/OUT1, 1 = TRS
  // IN2/OUT2 and size is 1..3; a route whose 0x18 flags are off drops the
  // message. This is the path for bytes that must be processed by the TRS
  // chain; sendMidi() (0x15) stays the unprocessed immediate path. Per-message,
  // so it has no resend cache (the chain config already has one).
  bool sendMidiRoute(std::uint8_t src, std::uint8_t dst, const std::uint8_t* data,
                     std::uint8_t n);
  bool setTempo(std::uint16_t bpm_x100);
  bool transport(std::uint8_t cmd);  // 0 stop, 1 start, 2 continue
  // Per-output TRS clock/transport gate (0x14): port 0 = OUT1 (uart0), 1 =
  // OUT2 (uart1); flags bit0 = clock, bit1 = transport. The last successfully
  // queued route per output is re-sent after HELLO so a Pico reboot cannot
  // silently revert an output to the boot default while the Pi is connected.
  bool clockRoute(std::uint8_t port, std::uint8_t flags);
  // External clock follow (0x17): source 0 internal, 1 TRS IN1, 2 TRS IN2;
  // transport_mask bit0 TRS IN1, bit1 TRS IN2 (bit2 USB is Pi-side only and
  // rejected on the wire). Source 3+ or reserved mask bits are rejected. The
  // last successfully queued config is re-sent after HELLO; the Pico resets to
  // internal/no-follow at boot.
  bool clockSource(std::uint8_t source, std::uint8_t transport_mask);
  // MIDI FX chain for one TRS route or pseudo-source (0x18/0x19). src is the
  // wire route-source byte: 0 = TRS IN1, 1 = TRS IN2,
  // mb::route_src_generator, or mb::route_src_insert(slot); dst is 0/1. Routes
  // touching USB stay Pi-side and are rejected here. midiRoute() starts a
  // fresh chain (the firmware clears its pipes and runtime state), so send it
  // before that route's pipes. A generator chain's head must have the schema
  // role generator; the link mirrors the firmware's refusal before queueing.
  // midiRoutePipe() mirrors the firmware's setPipeChunk rules: offset 0 with
  // index == pipe count appends, index < count replaces; a larger offset
  // extends an existing pipe of the same type (the continuation chunk's
  // bypass byte and chMask are ignored, as in firmware). chMask takes effect
  // on the offset-0 chunk only; the default is all channels. The last
  // successfully queued chain per route is re-sent after HELLO, in wire order.
  bool midiRoute(std::uint8_t src, std::uint8_t dst, bool on);
  bool midiRoutePipe(std::uint8_t src, std::uint8_t dst, std::uint8_t index,
                     std::uint8_t type, bool bypass, std::uint8_t param_offset,
                     const std::int16_t* params, std::size_t count,
                     std::uint16_t chMask = mb::route_pipe_channel_mask_default);
  // Deprecated in the firmware (clock is always on); sent for compatibility.
  bool clockEnable(bool on);
  bool ledSet(std::uint8_t mask, std::uint8_t states);
  bool setManual(bool manual);
  // Control-plane (0x20-0x23): status probe, reboot (normal/USB boot), and a
  // link resync that mints a new Pico session without rebooting it.
  bool statusRequest();
  bool rebootPico(bool usbBoot);
  bool resetLink();
  // Latest 0x21 answer; false until a response arrives. Cleared when the Pico
  // session changes, so a cached snapshot never outlives its Pico boot.
  bool lastStatus(mb::LinkStatus& out) const;
  void onTick(TickFn fn) { tick_fn_ = std::move(fn); }
  // Pico session currently in use; piSession() is the local one we minted.
  std::uint32_t session() const { return pico_session_; }
  std::uint32_t piSession() const { return pi_session_; }
  Stats stats() const { return stats_; }

 private:
  void handle(const mb::Frame& f, std::uint64_t now_us);
  void handleControl(const mb::Frame& f, std::uint64_t now_us);
  bool queueFrame(mb::Frame f);
  // Retires the inflight slot whose Pi sequence the Pico acked.
  void noteAck(std::uint16_t sequence);
  bool enqueueHello();
  void resendClockRoutes();
  void resendClockSource();
  void resendMidiRoutes();
  void push(const Event& e);

  ISpiTransport& transport_;
  std::uint32_t pi_session_ = 0;
  std::uint32_t pico_session_ = 0;
  std::uint16_t next_seq_ = 0;
  bool have_remote_seq_ = false;
  std::uint16_t remote_seq_ = 0;
  bool link_up_ = false;
  // Set by handle() when a Pico session change cleared the outbound queue; the
  // poll that observed it must not overwrite the fresh HELLO idle filler.
  bool resynced_ = false;
  std::uint64_t last_xfer_us_ = 0;
  std::deque<mb::Bytes> txq_;
  // Unacked control frames: a lost ack replays the SAME (session, sequence),
  // which the Pico dedups and acks again, so the message is applied exactly
  // once. Note Offs are never evicted from txq_ to make room.
  struct Inflight {
    mb::Bytes wire{};
    std::uint16_t sequence = 0;
    std::uint64_t sent_us = 0;
    std::uint8_t attempts = 0;
    bool acked = false;
  };
  std::deque<Inflight> inflight_;
  mb::Bytes repeat_{};
  std::deque<Event> events_;
  mb::LinkStatus status_{};
  bool have_status_ = false;
  // Last-known EV_CLOCK_ROUTE flags per output (index 0 = OUT1, 1 = OUT2);
  // only known entries are re-sent after HELLO.
  std::uint8_t clock_route_flags_[2] = {mb::CLOCK_ROUTE_DEFAULT,
                                        mb::CLOCK_ROUTE_DEFAULT};
  bool clock_route_known_[2] = {};
  // Last-known EV_CLOCK_SOURCE config; re-sent after HELLO when known.
  std::uint8_t clock_source_ = 0;
  std::uint8_t clock_source_mask_ = 0;
  bool clock_source_known_ = false;
  // Last-known chain per route source/destination, re-sent after HELLO in the
  // same order the firmware applied it (0x18, then pipe chunks). The key is
  // (src << 8) | dst so the pseudo-sources fit the same cache.
  static constexpr int midiRouteKey(std::uint8_t src, std::uint8_t dst) {
    return (static_cast<int>(src) << 8) | dst;
  }
  struct CachedRoutePipe {
    std::uint8_t type = 0;
    bool bypass = false;
    std::uint16_t chMask = mb::route_pipe_channel_mask_default;
    std::uint8_t param_count = 0;
    std::array<std::int16_t, mb::route_pipe_param_max> params{};
  };
  struct CachedMidiRoute {
    bool known = false;
    std::uint8_t flags = 0;
    std::uint8_t pipe_count = 0;
    std::array<CachedRoutePipe, mb::route_pipe_count_max> pipes{};
  };
  std::map<int, CachedMidiRoute> midi_routes_;
  TickFn tick_fn_;
  Stats stats_;
};

}  // namespace mnky::link
