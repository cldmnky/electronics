// mnkybox Pico firmware (RP2350 / Pico 2): MIDI UARTs, panel controls, SPI link to Pi.
//
// Owns, in real time: both MIDI UARTs (31250 baud), debounced panel inputs, the
// detentless value-knob quadrature decoder, three LEDs, the local MIDI-clock
// scheduler, the external-clock follower, and the SPI-slave side of the
// 32-byte framed link
// (protocol.hpp / midi.hpp / clock_follow.hpp). The Pico is the single clock
// master: every tick also queues a timestamped tick frame so the Pi can
// discipline its USB clock and map Pico timestamps into its own timeline. The
// Pi owns recording, routing policy, the UI and tempo overrides. External
// clock follow only retunes the local generator (tempo); the clock stream
// never stops, and a lost source coasts at the last estimate.
//
// Link contract (see docs/protocol.md): the Pi asserts CSn, waits >= 10 us,
// clocks exactly 32 bytes, deasserts; idle fillers are repeats deduped by
// (session, sequence). SPI rate is 1 MHz nominal on both ends.
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/spi.h"
#include "hardware/structs/rosc.h"
#include "hardware/sync.h"
#include "hardware/timer.h"
#include "hardware/uart.h"
#include "hardware/watchdog.h"
#include "pico/bootrom.h"
#include "pico/stdlib.h"

#include "clock_follow.hpp"
#include "clock_output.hpp"
#include "clock_scheduler.hpp"
#include "encoder_input.hpp"
#include "midi.hpp"
#include "midi_tx_scheduler.hpp"
#include "panel_input.hpp"
#include "pipe_event.hpp"
#include "pins.h"
#include "route_chain.hpp"

namespace {
// The shared MIDI FX core (midibrain/midifx/core) is the only pipe
// implementation; the firmware supplies its tick alarm as the engine clock and
// owns the 0x18/0x19 wire decode. These asserts keep the wire limits and the
// core capacity identical.
using mnky::midifx::PipeChain;
using mnky::midifx::PipeOutput;
static_assert(mnky::midifx::kMaxRoutePipes == mb::route_pipe_count_max);
static_assert(mnky::midifx::kMaxPipeParams == mb::route_pipe_param_max);
static_assert(mnky::midifx::generated::kMaxParamsPerPipe <=
              mnky::midifx::kMaxPipeParams);
static_assert(mnky::midifx::generated::kChannelScope.defaultValue ==
              mb::route_pipe_channel_mask_default);

// ---------------------------------------------------------------------------
// Local parameters (prototype values, see docs/protocol.md)
// ---------------------------------------------------------------------------
constexpr uint32_t kSpiBaud = 1000 * 1000;
constexpr uint32_t kMidiBaud = 31250;
constexpr uint32_t kTxDepth = 16;     // outbound SPI frames
constexpr uint32_t kRxDepth = 128;    // stamped UART bytes per port
constexpr int32_t kTempoDefault = 12000;  // bpm * 100
constexpr int32_t kTempoMin = 2000, kTempoMax = 30000;
// EV_TEMPO announcements while following use a 0.1 bpm deadband so residual
// jitter on the interval estimate does not flood the link; the generator
// itself always runs at the unquantized estimate.
constexpr int32_t kFollowTempoAnnounceX100 = 10;

// ---------------------------------------------------------------------------
// Outbound SPI frame queue (main loop only; CSn path only peeks)
// ---------------------------------------------------------------------------
// Push/pop mask IRQs: the clock alarm callback (IRQ context) enqueues tick
// frames while the main loop pops. Head is main-only, tail is IRQ-or-main,
// count is shared: every mutation runs masked. peek() only reads head, which
// the callback never touches, so it stays lock-free.
struct TxQueue {
  mb::Bytes buf[kTxDepth]{};
  uint32_t head = 0, tail = 0, count = 0, overflows = 0;
  bool push(const mb::Bytes& b) {
    const uint32_t saved = save_and_disable_interrupts();
    bool ok = false;
    if (count >= kTxDepth) {
      ++overflows;
    } else {
      buf[tail] = b;
      tail = (tail + 1) % kTxDepth;
      ++count;
      ok = true;
    }
    restore_interrupts(saved);
    return ok;
  }
  const mb::Bytes& peek() const { return buf[head]; }
  void pop() {
    const uint32_t saved = save_and_disable_interrupts();
    if (count) {
      head = (head + 1) % kTxDepth;
      --count;
    }
    restore_interrupts(saved);
  }
};

// Stamped UART RX rings (IRQ producer, main-loop consumer).
struct RxRing {
  struct Entry {
    uint8_t byte;
    uint32_t stamp;
  };
  Entry buf[kRxDepth]{};
  volatile uint32_t head = 0, tail = 0;
  uint32_t overruns = 0;
  void push(uint8_t b, uint32_t t) {
    const uint32_t next = (tail + 1) % kRxDepth;
    if (next == head) {
      ++overruns;
      return;
    }
    buf[tail].byte = b;
    buf[tail].stamp = t;
    tail = next;
  }
  bool pop(Entry& e) {
    if (head == tail) return false;
    e = buf[head];
    head = (head + 1) % kRxDepth;
    return true;
  }
  bool pending() const { return head != tail; }
};

TxQueue txq;
RxRing rxring[2];
// Per-output non-blocking MIDI TX queues. The clock alarm and transport path
// only enqueue; the UART TX IRQ drains them, realtime bytes first.
mb::MidiTxScheduler midi_tx[2];
volatile bool spi_rx_ready = false;
mb::Bytes spi_tx{}, spi_rx{};
// True while spi_tx holds a queued (unpopped) frame.
bool tx_holds_queued = false;

// ---------------------------------------------------------------------------
// Link + musical state
// ---------------------------------------------------------------------------
uint32_t pico_session = 0, pi_session = 0;
uint16_t next_seq = 1;
bool hello_seen = false;
uint32_t last_rx_session = 0;
uint16_t last_rx_seq = 0;
bool last_rx_valid = false;
// The clock runs from boot at the current tempo on every output whose 0x14
// clock gate is on (default on): downstream gear always has clock, and
// transport commands only move the transport state.
int32_t bpm_x100 = kTempoDefault;
bool transport_running = false;
// Local (default): panel PLAY/PLAY_START/STOP drive transport immediately so
// buttons stay responsive and work headless. Manual: buttons send EV_CONTROL
// only and the Pico waits for EV_TRANSPORT (count-ins, conditional starts).
// Resets to local on boot and on Pi (re)connect; the Pi re-sends MODE after
// HELLO when its policy needs manual.
bool transport_local = true;
// Per-output EV_CLOCK_ROUTE gates (RAM only): index 0 = OUT1/uart0, 1 =
// OUT2/uart1. Defaults preserve the legacy always-on stream; the Pi re-sends
// its last-known route after HELLO, so only boot state lives here.
mb::ClockGates clock_gates;
// Per-output clock delay line (EV_CLOCK_ROUTE_EXT): only 0xF8 bytes enter it,
// transport bytes are emitted directly. The alarm pushes, the main loop pops.
mb::ClockDelayLine clock_delay[2];
volatile uint32_t clock_delay_drops = 0;
// Every physical TRS route plus the generator and output-insert pseudo-source
// route kinds (see route_chain.hpp). Configured by EV_MIDI_ROUTE* and
// evaluated for EV_MIDI_ROUTE_SEND; plain EV_MIDI_SEND stays the unprocessed
// immediate path. The clocked pipes (arp, repeater, LFO) and the generator
// chains are stepped from clock_ticks in drain_route_engines(), so the Pico
// tick alarm is their only clock.
mb::RouteChains route_chains;
// External clock follow (RAM only). Default: internal generator, no transport
// follow; the Pi re-sends its last-known config after HELLO. Follow only
// retunes the absolute grid period; it never gates or re-phases the generator.
mb::ClockFollow clock_follow;
// Absolute 24-PPQN grid. The alarm always re-arms at nextDeadlineUs(), never
// "now + interval"; a missed deadline skips whole ticks instead of bursting.
mb::ClockScheduler clock_scheduler;
volatile uint32_t clock_skipped = 0;
uint32_t clock_ticks = 0;
uint8_t led_mask = 0;
// Record-arm state, toggled by REC/OVERDUB press edges (a rule the Pi shares:
// both sides toggle on the same deduped press edge).
bool rec_armed = false, overdub_armed = false;
// Pi LED_SET writes luminous state directly and suppresses the local semantic
// refresh until the next panel-driven change (arm toggle or panel transport).
bool led_override = false;
uint64_t bad_frames = 0, ignored_kinds = 0, dup_frames = 0;
bool shift_held = false;

mb::MidiParser midi_parse[2] = {mb::MidiParser(0), mb::MidiParser(1)};

uint32_t session_from_rosc() {
  uint32_t s = 0;
  for (int i = 0; i < 32; ++i) {
    // ROSC RANDOMBIT is the documented Pico entropy trick; each bit needs a
    // few ROSC cycles to re-randomise, hence the crude delay.
    busy_wait_us_32(5);
    s = (s << 1) | (rosc_hw->randombit & 1u);
  }
  if (!s) s = time_us_32() | 1u;
  return s;
}

// READY stays high while frames wait, and until the Pi's first HELLO: the
// boot HELLO lives in spi_tx (not the queue), so the queue alone cannot
// drive the pin.
void ready_update() { gpio_put(MB_PIN_READY_IRQ, txq.count > 0 || !hello_seen); }

// Single sequence space shared by main loop and tick callback: allocate
// masked so concurrent allocators cannot hand out the same number twice.
uint16_t alloc_seq() {
  const uint32_t saved = save_and_disable_interrupts();
  const uint16_t s = next_seq++;
  restore_interrupts(saved);
  return s;
}

bool queue_frame(const mb::Frame& f) {
  mb::Bytes b{};
  if (!mb::encode(f, b)) return false;
  const bool ok = txq.push(b);
  ready_update();
  return ok;
}

// Tick notification for the Pi USB-clock mirror. Called from the alarm
// callback: stamp first (tick time), bytes out, frame queued. Delivery
// delay is irrelevant — the Pi disciplines off the stamp, not arrival.
void queue_tick(uint32_t tick, uint32_t stamp_us) {
  mb::Frame f{};
  if (!mb::pack_tick(f, alloc_seq(), pico_session, tick, stamp_us)) return;
  queue_frame(f);  // drop-newest + counter on overflow (documented)
}

bool queue_midi(const mb::MidiMessage& m) {
  mb::Frame f{};
  if (!mb::pack_midi(f, alloc_seq(), pico_session, m)) return false;
  return queue_frame(f);
}

bool queue_control(uint8_t id, uint8_t value) {
  mb::Frame f{};
  if (!mb::pack_control(f, alloc_seq(), pico_session, id, value)) return false;
  return queue_frame(f);
}

bool queue_tempo() {
  mb::Frame f{};
  if (!mb::pack_tempo(f, alloc_seq(), pico_session, static_cast<uint16_t>(bpm_x100))) return false;
  return queue_frame(f);
}

void apply_leds() {
  gpio_put(MB_PIN_REC_LED, (led_mask & mb::LED_REC) != 0);
  gpio_put(MB_PIN_OVERDUB_LED, (led_mask & mb::LED_OVERDUB) != 0);
  gpio_put(MB_PIN_PLAY_LED, (led_mask & mb::LED_PLAY) != 0);
}

// Recompute the semantic indicator mask (solid armed+rolling, blinking
// armed+stopped at ~1 Hz, play follows transport) and write on change.
// Skipped while a Pi LED_SET override is latched.
void refresh_indicators() {
  if (led_override) return;
  const uint32_t now_ms = to_ms_since_boot(get_absolute_time());
  const mb::IndicatorState s{rec_armed, overdub_armed, transport_running,
                             (now_ms / 500) % 2 == 0};
  const uint8_t m = mb::indicator_mask(s);
  if (m != led_mask) {
    led_mask = m;
    apply_leds();
  }
}

// UART TX interrupt half: drain the per-output scheduler while the hardware
// has FIFO space, then disable the TX interrupt so an idle UART cannot storm.
// Only this function writes MIDI bytes to a UART.
void midi_tx_drain(uint8_t port) {
  uart_inst_t* uart = (port == 0) ? uart0 : uart1;
  mb::MidiTxScheduler& queue = midi_tx[port];
  while (uart_is_writable(uart)) {
    const int byte = queue.nextByte();
    if (byte < 0) break;
    // Direct data-register write after the writable check: the ISR must never
    // call a UART helper that can wait for FIFO space.
    uart_get_hw(uart)->dr = static_cast<std::uint8_t>(byte);
  }
  if (!queue.pending()) uart_set_irq_enables(uart, true, false);
}

// Enqueue-only MIDI TX. Safe from the clock alarm: it never waits for UART
// FIFO space and realtime bytes jump ahead of normal traffic. The mask keeps
// the queue mutation + TX-interrupt enable atomic against the UART handler:
// otherwise the handler could drain between them and leave the TX interrupt
// enabled on an empty FIFO (a spurious interrupt storm).
void midi_emit(uint8_t port, const uint8_t* data, uint32_t n) {
  if (port > 1) return;
  const uint32_t saved = save_and_disable_interrupts();
  midi_tx[port].enqueue(data, n);
  if (midi_tx[port].pending()) {
    uart_set_irq_enables((port == 0) ? uart0 : uart1, true, true);
  }
  restore_interrupts(saved);
}

// Emits the ordered result of one route evaluation to the physical output.
void emit_pipe_output(uint8_t dst, const PipeOutput& out) {
  for (std::size_t i = 0; i < out.count; ++i) {
    if (out.events[i].size < 1 || out.events[i].size > 3) continue;
    midi_emit(dst, out.events[i].data, out.events[i].size);
  }
}

// Fixed-length note-offs, delay echoes and the clocked Wave-2 pipes are
// serviced in the main loop, never in the clock IRQ: the route engines are not
// IRQ-safe, so their output goes through the same non-blocking TX scheduler as
// any other routed message. clock_ticks is read once per pass; a missed tick is caught up by the
// engine (bounded to kMaxStepsPerCall steps, then it resyncs to the grid).
void drain_route_engines() {
  const uint32_t now_ms = to_ms_since_boot(get_absolute_time());
  const uint32_t ticks = clock_ticks;
  route_chains.service(ticks, now_ms, [](uint8_t dst, const PipeOutput& out) {
    emit_pipe_output(dst, out);
  });
}

// Main-loop half of the clock delay line. The alarm callback pushes; each pop
// masks interrupts so a push cannot race the FIFO cursor update.
void clock_delay_service() {
  const uint32_t now_us = time_us_32();
  const uint8_t tick = 0xF8;
  for (uint8_t port = 0; port < 2; ++port) {
    if (!clock_gates.clockOn(port)) continue;
    for (;;) {
      const uint32_t saved = save_and_disable_interrupts();
      const bool due = clock_delay[port].popDue(now_us);
      restore_interrupts(saved);
      if (!due) break;
      midi_emit(port, &tick, 1);
    }
  }
}

int64_t clock_tick(alarm_id_t, void*) {
  // Clock bytes are per-output gated and divided; the tick count and Pi tick
  // frame stay unconditional because they also drive the Pi timeline and USB
  // mirror. Only the clock byte enters the delay line; transport bytes are
  // emitted directly by transport_cmd.
  const uint32_t stamp_us = static_cast<uint32_t>(clock_scheduler.nextDeadlineUs());
  const uint8_t tick = 0xF8;
  for (uint8_t port = 0; port < 2; ++port) {
    if (!clock_gates.clockOn(port)) continue;
    if (!mb::clock_div_due(clock_ticks, clock_gates.tempoDiv(port))) continue;
    if (clock_gates.syncDelayMs(port) == 0) {
      midi_emit(port, &tick, 1);  // enqueue realtime; no blocking UART call
    } else if (!clock_delay[port].push(stamp_us)) {
      clock_delay_drops = clock_delay_drops + 1u;
    }
  }
  ++clock_ticks;
  queue_tick(clock_ticks, stamp_us);  // Pi USB-mirror + timeline mapping
  // The next pulse is the previous scheduled deadline + one exact period.
  // If the callback ran past that point, skip whole missed ticks (never a
  // burst) and re-arm at the next absolute grid point. Sample `now` after the
  // callback's enqueue work so a slow callback is measured as slow.
  clock_scheduler.advance();
  clock_skipped = clock_skipped + clock_scheduler.skipToAfter(time_us_64());
  add_alarm_at(from_us_since_boot(clock_scheduler.nextDeadlineUs()), clock_tick, nullptr,
               true);
  return 0;
}

// Retune the absolute grid. The setters mask interrupts because the clock
// alarm advances the same scheduler from IRQ context. Recompute the period
// from the smoothed follow estimate when one exists (including while
// coasting), otherwise from the internal tempo. `announce` queues EV_TEMPO
// when the followed tempo moves past the deadband; the main loop owns this
// state, and clock_tick only advances the grid.
void clock_period_set_ns(uint64_t period_ns) {
  const uint32_t saved = save_and_disable_interrupts();
  clock_scheduler.setPeriodNs(period_ns);
  restore_interrupts(saved);
}

void clock_period_set_tempo(int32_t bpm) {
  const uint32_t saved = save_and_disable_interrupts();
  clock_scheduler.setTempoX100(bpm);
  restore_interrupts(saved);
}

void clock_interval_refresh(bool announce) {
  if (!clock_follow.hasEstimate()) {
    clock_period_set_tempo(bpm_x100);
    return;
  }
  // Follow supplies a measured integer-microsecond estimate; it enters the
  // same absolute grid, so the generator period never accumulates its rounding.
  clock_period_set_ns(static_cast<uint64_t>(clock_follow.intervalUsRounded()) * 1000ull);
  const int32_t followed = clock_follow.bpmX100();
  if (followed >= bpm_x100 + kFollowTempoAnnounceX100 ||
      followed <= bpm_x100 - kFollowTempoAnnounceX100) {
    bpm_x100 = followed;
    if (announce) queue_tempo();
  }
}

void clock_arm() {
  clock_scheduler.reset(bpm_x100, time_us_64());
  add_alarm_at(from_us_since_boot(clock_scheduler.nextDeadlineUs()), clock_tick, nullptr, true);
}

// Transport state for the Pi USB mirror (ticks flow regardless).
void announce_state() {
  mb::Frame f{};
  if (!mb::pack_transport_state(f, alloc_seq(), pico_session, transport_running, clock_ticks, rec_armed,
                                overdub_armed))
    return;
  queue_frame(f);
}

std::uint16_t saturate_u16(std::uint64_t v) {
  return v > 0xffffu ? 0xffffu : static_cast<std::uint16_t>(v);
}

// 0x21 STATUS_RESP snapshot. Counters are read with IRQs masked because the
// clock alarm and UART RX handlers mutate them; rxov is the sum of both UART
// rings (the console keeps the per-port split).
mb::LinkStatus link_status() {
  mb::LinkStatus s{};
  s.uptime_s = static_cast<std::uint32_t>(time_us_64() / 1'000'000ull);
  const std::uint32_t saved = save_and_disable_interrupts();
  s.ticks = clock_ticks;
  s.txq_depth = static_cast<std::uint8_t>(txq.count);
  s.txov = saturate_u16(txq.overflows);
  s.rxov = saturate_u16(static_cast<std::uint64_t>(rxring[0].overruns) + rxring[1].overruns);
  s.flags = static_cast<std::uint8_t>((hello_seen ? mb::STATUS_LINK_UP : 0) |
                                      (transport_running ? mb::STATUS_ROLLING : 0));
  restore_interrupts(saved);
  return s;
}

// 0x23 RESET_LINK: start a fresh link session without rebooting. Tempo,
// transport state and the tick count survive; the Pi sees a new session and
// re-handshakes, then the current state/tempo are announced again.
void reset_link_session() {
  const std::uint32_t fresh = session_from_rosc();
  mb::Frame hello{};
  hello.type = mb::Type::hello;
  hello.sequence = 0;
  hello.session = fresh;
  hello.length = 0;
  mb::Bytes wire{};
  if (!mb::encode(hello, wire)) return;
  const std::uint32_t saved = save_and_disable_interrupts();
  pico_session = fresh;
  pi_session = 0;
  next_seq = 1;
  hello_seen = false;
  // Keep the Pi-frame dedup state: a repeated RESET_LINK (same session and
  // sequence) must not mint another session while the Pi is still switching
  // to its post-reset HELLO.
  transport_local = true;
  while (txq.count > 0) txq.pop();  // stale frames carry the old session
  txq.push(wire);
  restore_interrupts(saved);
  ready_update();
  announce_state();
  queue_tempo();
  printf("[link] RESET_LINK session=%08lx\n", static_cast<unsigned long>(fresh));
}

void transport_cmd(uint8_t cmd) {
  // Transport state is global; only the emitted bytes are per-output gated.
  // A transport command is an event, not a clock tick: its bytes go out at the
  // event instant on the realtime TX class (never through the clock divider or
  // sync-delay line), and START resets the tick index. This preserves the
  // pre-scheduler semantics.
  uint8_t b = 0xFC;
  if (cmd == 1) {
    b = 0xFA;
    clock_ticks = 0;
    transport_running = true;
  } else if (cmd == 2) {
    b = 0xFB;
    transport_running = true;
  } else {
    transport_running = false;
  }
  if (clock_gates.transportOn(0)) midi_emit(0, &b, 1);
  if (clock_gates.transportOn(1)) midi_emit(1, &b, 1);
  announce_state();
}

void set_tempo(int32_t v, bool echo) {
  if (v < kTempoMin) v = kTempoMin;
  if (v > kTempoMax) v = kTempoMax;
  bpm_x100 = v;
  clock_interval_refresh(false);  // a followed source wins over local tempo
  if (echo) queue_tempo();
}

// One handler per UART covers both directions. Drain TX first so a busy RX
// stream cannot delay a queued clock byte.
void on_uart0_irq() {
  midi_tx_drain(0);
  while (uart_is_readable(uart0)) rxring[0].push(uart_getc(uart0), time_us_32());
}
void on_uart1_irq() {
  midi_tx_drain(1);
  while (uart_is_readable(uart1)) rxring[1].push(uart_getc(uart1), time_us_32());
}

void handle_frame(const mb::Frame& f) {
  if (f.type == mb::Type::hello) {
    if (pi_session != f.session) {
      pi_session = f.session;
      last_rx_valid = false;
      printf("[link] Pi HELLO session=%08lx\n", static_cast<unsigned long>(f.session));
    }
    hello_seen = true;
    transport_local = true;  // reconnect = local default; Pi re-sends MODE if needed
    mb::Frame ack{};
    ack.type = mb::Type::ack;
    ack.sequence = alloc_seq();
    ack.session = pico_session;
    ack.length = 0;
    queue_frame(ack);
    announce_state();  // resync the Pi mirror after its (re)connect
    queue_tempo();
    return;
  }
  if (f.type == mb::Type::ack) return;  // link liveness only in this revision
  if (f.type != mb::Type::control_event) {
    ++ignored_kinds;
    return;
  }
  if (f.length < 1) {
    ++bad_frames;
    return;
  }
  switch (f.payload[0]) {
    case mb::EV_SET_TEMPO:
      if (f.length < 3) {
        ++bad_frames;
        break;
      }
      set_tempo(f.payload[1] | (f.payload[2] << 8), true);
      printf("[link] SET_TEMPO %ld.%02ld bpm\n", static_cast<long>(bpm_x100 / 100),
             static_cast<long>(bpm_x100 % 100));
      break;
    case mb::EV_TRANSPORT:
      if (f.length < 2 || f.payload[1] > 2) {
        ++bad_frames;
        break;
      }
      transport_cmd(f.payload[1]);
      if (!led_override) refresh_indicators();
      break;
    case mb::EV_CLOCK_ENABLE:
      // Deprecated: the clock is always on. Still validated + accepted so
      // older Pi software keeps working; the state frame answers instead.
      if (f.length < 2 || f.payload[1] > 1) {
        ++bad_frames;
        break;
      }
      printf("[link] CLOCK_ENABLE ignored (clock always on)\n");
      announce_state();
      break;
    case mb::EV_CLOCK_ROUTE:
      if (f.length < 3 || !clock_gates.apply(f.payload[1], f.payload[2])) {
        ++bad_frames;
        break;
      }
      if (!clock_gates.clockOn(f.payload[1])) {
        const uint32_t saved = save_and_disable_interrupts();
        clock_delay[f.payload[1]].clear();
        restore_interrupts(saved);
      }
      printf("[link] CLOCK_ROUTE out%u clock=%d transport=%d\n",
             static_cast<unsigned>(f.payload[1]), clock_gates.clockOn(f.payload[1]) ? 1 : 0,
             clock_gates.transportOn(f.payload[1]) ? 1 : 0);
      break;
    case mb::EV_CLOCK_ROUTE_EXT: {
      std::uint8_t port = 0, tempo_div = 1;
      std::uint16_t sync_delay_ms = 0;
      if (!mb::unpack_clock_route_ext(f, port, tempo_div, sync_delay_ms)) {
        ++bad_frames;
        break;
      }
      const uint32_t saved = save_and_disable_interrupts();
      const bool ok = clock_gates.applyExtras(port, tempo_div, sync_delay_ms);
      if (ok) clock_delay[port].setDelayMs(clock_gates.syncDelayMs(port));
      restore_interrupts(saved);
      if (!ok) {
        ++bad_frames;
        break;
      }
      printf("[link] CLOCK_ROUTE_EXT out%u div=%u delay=%ums\n",
             static_cast<unsigned>(port), static_cast<unsigned>(clock_gates.tempoDiv(port)),
             static_cast<unsigned>(clock_gates.syncDelayMs(port)));
      break;
    }
    case mb::EV_MIDI_ROUTE: {
      std::uint8_t src = 0, dst = 0, flags = 0;
      if (!mb::unpack_midi_route(f, src, dst, flags)) {
        ++bad_frames;
        break;
      }
      route_chains.begin(src, dst, (flags & mb::ROUTE_FLAG_ON) != 0);
      printf("[link] MIDI_ROUTE in%u -> out%u %s\n", static_cast<unsigned>(src),
             static_cast<unsigned>(dst), (flags & mb::ROUTE_FLAG_ON) ? "on" : "off");
      break;
    }
    case mb::EV_MIDI_ROUTE_PIPE: {
      std::uint8_t src = 0, dst = 0, index = 0, type = 0, offset = 0;
      bool bypass = false;
      std::uint16_t ch_mask = 0;
      // Legacy frames (no scope) carry up to five params; the decoder keeps
      // accepting them, so the array holds the legacy maximum.
      std::int16_t raw[mb::route_pipe_param_frame_max_legacy]{};
      std::size_t count = 0;
      if (!mb::unpack_midi_route_pipe(f, src, dst, index, type, bypass, offset, ch_mask,
                                      raw, mb::route_pipe_param_frame_max_legacy,
                                      count)) {
        ++bad_frames;
        break;
      }
      std::int32_t params[mb::route_pipe_param_frame_max_legacy]{};
      for (std::size_t i = 0; i < count; ++i) params[i] = raw[i];
      if (!route_chains.setPipeChunk(src, dst, index, type, bypass, offset, params,
                                     count, ch_mask)) {
        ++bad_frames;
        break;
      }
      printf("[link] MIDI_ROUTE_PIPE in%u -> out%u [%u] type=%u bypass=%d "
             "mask=%04x params=%u+%u\n",
             static_cast<unsigned>(src), static_cast<unsigned>(dst),
             static_cast<unsigned>(index), static_cast<unsigned>(type), bypass ? 1 : 0,
             static_cast<unsigned>(ch_mask), static_cast<unsigned>(offset),
             static_cast<unsigned>(count));
      break;
    }
    case mb::EV_MIDI_ROUTE_SEND: {
      std::uint8_t src = 0, dst = 0, size = 0;
      std::uint8_t data[3]{};
      if (!mb::unpack_midi_route_send(f, src, dst, data, size)) {
        ++bad_frames;
        break;
      }
      mb::MidiMessage m{};
      m.size = size;
      for (std::uint8_t i = 0; i < size; ++i) m.data[i] = data[i];
      const PipeOutput out = route_chains.processRoute(
          src, dst, mb::pipeEvent(m), to_ms_since_boot(get_absolute_time()));
      emit_pipe_output(dst, out);
      break;
    }
    case mb::EV_CLOCK_SOURCE:
      if (f.length < 3 || !clock_follow.apply(f.payload[1], f.payload[2])) {
        ++bad_frames;
        break;
      }
      clock_interval_refresh(false);  // source change restarts acquisition
      printf("[link] CLOCK_SOURCE %s transport=%02x\n",
             clock_follow.source() == mb::CLOCK_SOURCE_INTERNAL
                 ? "internal"
                 : (clock_follow.source() == mb::CLOCK_SOURCE_TRS1 ? "TRS IN1" : "TRS IN2"),
             static_cast<unsigned>(clock_follow.transportMask()));
      break;
    case mb::EV_LED_SET:
      if (f.length < 3) {
        ++bad_frames;
        break;
      } {
        const mb::LedCommand lc = mb::interpret_led_set(f.payload[1], f.payload[2]);
        if (lc.sync_arms) {
          rec_armed = lc.rec_arm;
          overdub_armed = lc.overdub_arm;
          led_override = false;
          refresh_indicators();
        } else {
          led_override = true;
          led_mask = (led_mask & ~f.payload[1]) | (f.payload[2] & f.payload[1]);
          apply_leds();
        }
      }
      break;
    case mb::EV_TRANSPORT_MODE:
      if (f.length < 2 || f.payload[1] > 1) {
        ++bad_frames;
        break;
      }
      transport_local = f.payload[1] == 0;
      printf("[link] transport mode %s\n", transport_local ? "local" : "manual");
      break;
    case mb::EV_MIDI_SEND:
      if (f.length < 3 || f.payload[1] > 1 || f.payload[2] < 1 || f.payload[2] > 3 ||
          f.length < static_cast<uint8_t>(3 + f.payload[2])) {
        ++bad_frames;
        break;
      }
      midi_emit(f.payload[1], &f.payload[3], f.payload[2]);
      break;
    case mb::EV_STATUS_REQ: {
      if (f.length != 1) {
        ++bad_frames;
        break;
      }
      mb::Frame resp{};
      if (mb::pack_status_resp(resp, alloc_seq(), pico_session, link_status())) queue_frame(resp);
      break;
    }
    case mb::EV_REBOOT:
      if (f.length != 2 || f.payload[1] > 1) {
        ++bad_frames;
        break;
      }
      printf("[link] REBOOT mode=%u\n", static_cast<unsigned>(f.payload[1]));
      if (f.payload[1] == 0) {
        watchdog_reboot(0, 0, 0);  // standard boot from flash
      } else {
        reset_usb_boot(0, 0);  // BOOTSEL/USB boot for the firmware-update flow
      }
      break;
    case mb::EV_RESET_LINK:
      if (f.length != 1) {
        ++bad_frames;
        break;
      }
      reset_link_session();
      break;
    default:
      ++ignored_kinds;  // Pico->Pi kinds echoed back, or future commands
      break;
  }
}

struct Button {
  uint gpio;
  mb::PanelControl control;
};

Button buttons[] = {
    {MB_PIN_NAV_UP, mb::PanelControl(mb::CTL_NAV_UP)},
    {MB_PIN_NAV_DOWN, mb::PanelControl(mb::CTL_NAV_DOWN)},
    {MB_PIN_NAV_LEFT, mb::PanelControl(mb::CTL_NAV_LEFT)},
    {MB_PIN_NAV_RIGHT, mb::PanelControl(mb::CTL_NAV_RIGHT)},
    {MB_PIN_NAV_PRESS, mb::PanelControl(mb::CTL_NAV_PRESS)},
    {MB_PIN_ENCODER_SW, mb::PanelControl(mb::CTL_ENCODER_SW)},
    {MB_PIN_REC, mb::PanelControl(mb::CTL_REC)},
    {MB_PIN_OVER_DUB, mb::PanelControl(mb::CTL_OVER_DUB)},
    {MB_PIN_STOP, mb::PanelControl(mb::CTL_STOP)},
    {MB_PIN_PLAY, mb::PanelControl(mb::CTL_PLAY)},
    {MB_PIN_PLAY_START, mb::PanelControl(mb::CTL_PLAY_START)},
    {MB_PIN_SHIFT_EXIT, mb::PanelControl(mb::CTL_SHIFT_EXIT)},
};

// The front knob is the context-aware value knob (Bourns PEC11R-4015F-S0024:
// detentless, 24 pulses/rev). It only reports CTL_ENCODER_TURN deltas; the Pi
// owns the mapping to the focused control and sends EV_SET_TEMPO back when
// that control is tempo.
mb::ValueEncoder value_encoder;

void scan_controls(uint32_t now_ms) {
  for (auto& b : buttons) {
    const bool raw_pressed = !gpio_get(b.gpio);  // active-low against GND
    const mb::PanelControl::Result r = b.control.update(raw_pressed, now_ms);
    const uint8_t id = b.control.id();
    const bool pressed = b.control.pressed();
    if (id == mb::CTL_SHIFT_EXIT) {
      // SHIFT/EXIT held is the knob's fine mode (no acceleration); the event
      // stream carries press/release plus the single long-hold (2) that
      // enters shift mode live. Short-press exit and mode policy live on
      // the Pi (see docs/protocol.md).
      shift_held = pressed;
    }
    if (r.event) queue_control(id, r.value);
    if (r.changed) {
      // Panel transport drives locally (no Pi round-trip); the Pi learns it
      // from the state frame + tick stream and must not echo it back (see
      // docs/protocol.md). EV_CONTROL still goes out for UI/record policy.
      // In manual mode the mapping is suppressed: events only, Pi decides.
      if (pressed && transport_local) {
        const int cmd = mb::transport_for_press(id, transport_running);
        if (cmd >= 0) {
          transport_cmd(static_cast<uint8_t>(cmd));
          led_override = false;
          refresh_indicators();
        }
      }
      // REC/OVERDUB presses toggle record arming (a rule the Pi shares:
      // both sides toggle on the same deduped press edge). Announcing here
      // keeps the Pi's mirror in step without waiting for a transition.
      if (pressed && (id == mb::CTL_REC || id == mb::CTL_OVER_DUB)) {
        if (id == mb::CTL_REC)
          rec_armed = !rec_armed;
        else
          overdub_armed = !overdub_armed;
        led_override = false;
        refresh_indicators();
        announce_state();
      }
    }
  }
  // Encoder turns only leave the Pico as CTL_ENCODER_TURN: an accelerated
  // int8 delta, unaccelerated while SHIFT/EXIT is held (the Pi's fine step).
  // Stepping the local tempo here as well would move tempo in every context.
  const mb::ValueEncoder::Result turn = value_encoder.update(
      gpio_get(MB_PIN_ENCODER_A), gpio_get(MB_PIN_ENCODER_B), shift_held, time_us_32());
  if (turn.turned) queue_control(mb::CTL_ENCODER_TURN, static_cast<uint8_t>(turn.delta));
}

void drain_midi() {
  for (uint8_t port = 0; port < 2; ++port) {
    RxRing::Entry e{};
    while (rxring[port].pop(e)) {
      if (!midi_parse[port].feed(e.byte, e.stamp)) continue;
      const mb::MidiMessage& m = midi_parse[port].last();
      if (m.size == 1 && m.data[0] == 0xF8) {
        // The selected source's ticks drive the generator estimate; they are
        // consumed instead of routed so the Pico regenerates rather than
        // mirrors them. Other ports' clock stays ordinary MIDI input.
        if (clock_follow.following() && clock_follow.source() == port + 1) {
          clock_follow.onTick(m.stamp_us);
          clock_interval_refresh(true);
        } else {
          queue_midi(m);
        }
        continue;
      }
      const int transport = mb::transport_command_for_byte(m.size == 1 ? m.data[0] : 0);
      if (transport >= 0) {
        // FA/FB/FC are consumed on every TRS input and never enter the MIDI
        // stream; only a transport-followed input drives local transport.
        if (clock_follow.followsTransport(port)) {
          transport_cmd(static_cast<uint8_t>(transport));
          if (!led_override) refresh_indicators();
        }
        continue;
      }
      queue_midi(m);
    }
  }
}

// One 32-byte exchange per CSn assertion. Polled (sub-microsecond latency,
// no IRQ race with the master's first clocks). The master must hold CS low
// across all 32 clocks; an aborted transaction hangs here until the watchdog
// reboots us, so the master side must guarantee full transfers.
void spi_service(bool csn_level) {
  static bool was_high = true;
  if (was_high && !csn_level) {
    was_high = false;
    spi_write_read_blocking(spi0, spi_tx.data(), spi_rx.data(), spi_tx.size());
    spi_rx_ready = true;
  } else if (!was_high && csn_level) {
    was_high = true;
  }
}
}  // namespace

int main() {
  stdio_init_all();
  sleep_ms(200);

  pico_session = session_from_rosc();
  printf("\n[mnkybox] Pico firmware " __DATE__
         " " __TIME__ " session=%08lx midi-fx-schema=v%u\n",
         static_cast<unsigned long>(pico_session),
         static_cast<unsigned>(mnky::midifx::generated::kSchemaVersion));
  // Seed the FX RNGs from the session/time so Chance and VelocityRandom do not
  // repeat the same sequence after every boot. The route config stays off until
  // the Pi sends EV_MIDI_ROUTE.
  route_chains.setSeeds(pico_session, time_us_32());

  // Panel inputs: active-low with pull-ups. Encoder/nav/transport alike.
  for (const auto& b : buttons) {
    gpio_init(b.gpio);
    gpio_set_dir(b.gpio, GPIO_IN);
    gpio_pull_up(b.gpio);
  }
  for (uint gpio : {MB_PIN_ENCODER_A, MB_PIN_ENCODER_B}) {
    gpio_init(gpio);
    gpio_set_dir(gpio, GPIO_IN);
    gpio_pull_up(gpio);
  }
  value_encoder.reset(gpio_get(MB_PIN_ENCODER_A), gpio_get(MB_PIN_ENCODER_B));

  // LEDs: high-side drive through 330R, low = off.
  for (uint gpio : {MB_PIN_PLAY_LED, MB_PIN_REC_LED, MB_PIN_OVERDUB_LED}) {
    gpio_init(gpio);
    gpio_set_dir(gpio, GPIO_OUT);
    gpio_put(gpio, 0);
  }

  // MIDI UARTs at the standard 31250 baud, 8N1. USB carries the console;
  // UART0 must not be stdio (see CMake: stdio_uart disabled).
  uart_init(uart0, kMidiBaud);
  gpio_set_function(MB_PIN_MIDI_OUT_1_UART0_TX, GPIO_FUNC_UART);
  gpio_set_function(MB_PIN_MIDI_IN_1_UART0_RX, GPIO_FUNC_UART);
  uart_init(uart1, kMidiBaud);
  gpio_set_function(MB_PIN_MIDI_OUT_2_UART1_TX, GPIO_FUNC_UART);
  gpio_set_function(MB_PIN_MIDI_IN_2_UART1_RX, GPIO_FUNC_UART);
  uart_set_irq_enables(uart0, true, false);
  uart_set_irq_enables(uart1, true, false);
  irq_set_exclusive_handler(UART0_IRQ, on_uart0_irq);
  irq_set_exclusive_handler(UART1_IRQ, on_uart1_irq);
  irq_set_enabled(UART0_IRQ, true);
  irq_set_enabled(UART1_IRQ, true);

  // SPI0 slave for the Pi link. MOSI/SCK/MISO on the SPI function; CSn stays
  // a plain GPIO input so the main loop sees the exact assertion edge.
  spi_init(spi0, kSpiBaud);
  spi_set_slave(spi0, true);
  spi_set_format(spi0, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
  gpio_set_function(MB_PIN_SPI0_RX_MOSI, GPIO_FUNC_SPI);
  gpio_set_function(MB_PIN_SPI0_SCK, GPIO_FUNC_SPI);
  gpio_set_function(MB_PIN_SPI0_TX_MISO, GPIO_FUNC_SPI);
  gpio_init(MB_PIN_SPI0_CSN);
  gpio_set_dir(MB_PIN_SPI0_CSN, GPIO_IN);
  gpio_pull_up(MB_PIN_SPI0_CSN);
  gpio_init(MB_PIN_READY_IRQ);
  gpio_set_dir(MB_PIN_READY_IRQ, GPIO_OUT);

  // First HELLO goes out in the very first exchange; repeats (same sequence)
  // are the idle filler until the Pi answers.
  {
    mb::Frame hello{};
    hello.type = mb::Type::hello;
    hello.sequence = 0;
    hello.session = pico_session;
    hello.length = 0;
    mb::encode(hello, spi_tx);
  }
  next_seq = 1;
  ready_update();  // alive + HELLO pending
  announce_state();  // boot state: stopped, tick 0 (ticks already flowing)
  clock_arm();  // first tick one interval after boot

  watchdog_enable(8000, 1);
  uint32_t last_ms = to_ms_since_boot(get_absolute_time());
  uint32_t last_stats = last_ms;
  printf("[link] waiting for Pi HELLO (SPI0 slave, 1 MHz, CS-gated 32B)\n");

  while (true) {
    watchdog_update();
    spi_service(gpio_get(MB_PIN_SPI0_CSN));
    if (spi_rx_ready) {
      spi_rx_ready = false;
      if (tx_holds_queued) {
        txq.pop();
        tx_holds_queued = false;
      }
      mb::Frame f{};
      if (!mb::decode(spi_rx, f)) {
        ++bad_frames;
      } else if (last_rx_valid && f.session == last_rx_session && f.sequence == last_rx_seq) {
        ++dup_frames;  // idle-fill repeat or retried transfer: no re-apply
      } else {
        last_rx_valid = true;
        last_rx_session = f.session;
        last_rx_seq = f.sequence;
        handle_frame(f);
      }
      // Preload the next exchange: queued frame, else repeat (Pi dedups).
      if (txq.count > 0) {
        spi_tx = txq.peek();
        tx_holds_queued = true;
      } else {
        tx_holds_queued = false;  // spi_tx keeps the last frame: idle repeat
      }
      ready_update();
    }
    drain_midi();
    clock_delay_service();
    drain_route_engines();
    const uint32_t now_ms = to_ms_since_boot(get_absolute_time());
    if (now_ms - last_ms >= 1) {
      last_ms = now_ms;
      scan_controls(now_ms);
      refresh_indicators();  // blink phase + any missed edge
      ready_update();
    }
    if (now_ms - last_stats >= 1000) {
      last_stats = now_ms;
      uint32_t mtx[4];
      uint64_t mtx_drop, mtx_reject, mtx_rt_drop;
      uint32_t skipped;
      {
        const uint32_t saved = save_and_disable_interrupts();
        mtx[0] = midi_tx[0].normalDepth();
        mtx[1] = midi_tx[0].realtimeDepth();
        mtx[2] = midi_tx[1].normalDepth();
        mtx[3] = midi_tx[1].realtimeDepth();
        mtx_drop = midi_tx[0].droppedNormal() + midi_tx[1].droppedNormal();
        mtx_reject = midi_tx[0].rejectedNormal() + midi_tx[1].rejectedNormal();
        mtx_rt_drop = midi_tx[0].droppedRealtime() + midi_tx[1].droppedRealtime();
        skipped = clock_skipped;
        restore_interrupts(saved);
      }
      printf("[stat] link=%d bpm=%ld.%02ld run=%d ticks=%lu txq=%lu rxpend=%d/%d bad=%llu dup=%llu txov=%lu rxov=%lu/%lu mtx=%lu+%lu/%lu+%lu mtxdrop=%llu mtxrej=%llu mtxrt=%llu miss=%lu\n",
             hello_seen ? 1 : 0, static_cast<long>(bpm_x100 / 100), static_cast<long>(bpm_x100 % 100),
             transport_running ? 1 : 0, static_cast<unsigned long>(clock_ticks),
             static_cast<unsigned long>(txq.count), rxring[0].pending() ? 1 : 0,
             rxring[1].pending() ? 1 : 0, static_cast<unsigned long long>(bad_frames),
             static_cast<unsigned long long>(dup_frames), static_cast<unsigned long>(txq.overflows),
             static_cast<unsigned long>(rxring[0].overruns), static_cast<unsigned long>(rxring[1].overruns),
             static_cast<unsigned long>(mtx[0]), static_cast<unsigned long>(mtx[1]),
             static_cast<unsigned long>(mtx[2]), static_cast<unsigned long>(mtx[3]),
             static_cast<unsigned long long>(mtx_drop),
             static_cast<unsigned long long>(mtx_reject),
             static_cast<unsigned long long>(mtx_rt_drop),
             static_cast<unsigned long>(skipped));
    }
  }
}
