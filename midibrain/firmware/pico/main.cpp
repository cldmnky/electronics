// mnkybox Pico firmware (RP2350 / Pico 2): MIDI UARTs, panel controls, SPI link to Pi.
//
// Owns, in real time: both MIDI UARTs (31250 baud), debounced panel inputs,
// quadrature tempo encoder, three LEDs, the local MIDI-clock scheduler, and
// the SPI-slave side of the 32-byte framed link (protocol.hpp / midi.hpp).
// The Pico is the single clock master: every tick also queues a timestamped
// tick frame so the Pi can discipline its USB clock and map Pico timestamps
// into its own timeline. The Pi owns recording, routing policy, the UI and
// tempo overrides.
//
// Link contract (see docs/protocol.md): the Pi asserts CSn, waits >= 10 us,
// clocks exactly 32 bytes, deasserts; idle fillers are repeats deduped by
// (session, sequence). SPI rate is 1 MHz nominal on both ends.
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
#include "pico/stdlib.h"

#include "midi.hpp"
#include "pins.h"

namespace {
// ---------------------------------------------------------------------------
// Local parameters (prototype values, see docs/protocol.md)
// ---------------------------------------------------------------------------
constexpr uint32_t kSpiBaud = 1000 * 1000;
constexpr uint32_t kMidiBaud = 31250;
constexpr uint32_t kDebounceMs = 10;
constexpr uint32_t kTxDepth = 16;     // outbound SPI frames
constexpr uint32_t kRxDepth = 128;    // stamped UART bytes per port
constexpr int32_t kTempoDefault = 12000;  // bpm * 100
constexpr int32_t kTempoMin = 2000, kTempoMax = 30000;
constexpr int32_t kTempoStep = 100, kTempoFineStep = 10;
constexpr int kQuartersPerDetent = 4;

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
// The clock runs unconditionally from boot at the current tempo: downstream
// gear always has clock, and transport commands only move the transport state.
int32_t bpm_x100 = kTempoDefault;
bool transport_running = false;
// Local (default): panel PLAY/PLAY_START/STOP drive transport immediately so
// buttons stay responsive and work headless. Manual: buttons send EV_CONTROL
// only and the Pico waits for EV_TRANSPORT (count-ins, conditional starts).
// Resets to local on boot and on Pi (re)connect; the Pi re-sends MODE after
// HELLO when its policy needs manual.
bool transport_local = true;
uint32_t clock_ticks = 0;
int clock_alarm = -1;
uint8_t led_mask = 0;
uint64_t bad_frames = 0, ignored_kinds = 0, dup_frames = 0;
bool shift_held = false;

mb::Debouncer deb_nav_up(kDebounceMs), deb_nav_down(kDebounceMs), deb_nav_left(kDebounceMs),
    deb_nav_right(kDebounceMs), deb_nav_press(kDebounceMs), deb_enc_sw(kDebounceMs),
    deb_rec(kDebounceMs), deb_overdub(kDebounceMs), deb_stop(kDebounceMs), deb_play(kDebounceMs),
    deb_play_start(kDebounceMs), deb_shift(kDebounceMs);
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

// UART TX from the main loop runs opposite the clock alarm: guard multi-byte
// writes so clock bytes cannot interleave a MIDI_SEND.
void uart_write_guarded(uart_inst_t* uart, const uint8_t* data, uint32_t n) {
  const uint32_t saved = save_and_disable_interrupts();
  for (uint32_t i = 0; i < n; ++i) uart_putc(uart, data[i]);
  restore_interrupts(saved);
}

void midi_emit(uint8_t port, const uint8_t* data, uint32_t n) {
  uart_inst_t* uart = (port == 0) ? uart0 : uart1;
  uart_write_guarded(uart, data, n);
}

int64_t clock_tick(alarm_id_t, void*) {
  // Unconditional: the tick stream never stops (see transport_cmd for state).
  const uint32_t stamp_us = time_us_32();
  const uint8_t tick = 0xF8;
  uart_putc(uart0, tick);
  uart_putc(uart1, tick);
  ++clock_ticks;
  queue_tick(clock_ticks, stamp_us);  // Pi USB-mirror + timeline mapping
  const int64_t interval = (60'000'000LL * 100) / (static_cast<int64_t>(bpm_x100) * 24);
  return interval;
}

void clock_arm() {
  if (clock_alarm < 0) {
    const int64_t interval = (60'000'000LL * 100) / (static_cast<int64_t>(bpm_x100) * 24);
    clock_alarm = add_alarm_in_us(static_cast<uint64_t>(interval), clock_tick, nullptr, true);
  }
}

// Transport state for the Pi USB mirror (ticks flow regardless).
void announce_state() {
  mb::Frame f{};
  if (!mb::pack_transport_state(f, alloc_seq(), pico_session, transport_running, clock_ticks)) return;
  queue_frame(f);
}

void transport_cmd(uint8_t cmd) {
  // Transport bytes always go out; the 0xF8 stream never stops.
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
  midi_emit(0, &b, 1);
  midi_emit(1, &b, 1);
  announce_state();
}

void set_tempo(int32_t v, bool echo) {
  if (v < kTempoMin) v = kTempoMin;
  if (v > kTempoMax) v = kTempoMax;
  bpm_x100 = v;
  if (echo) queue_tempo();
}

void on_uart0_rx() {
  while (uart_is_readable(uart0)) rxring[0].push(uart_getc(uart0), time_us_32());
}
void on_uart1_rx() {
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
    case mb::EV_LED_SET:
      if (f.length < 3) {
        ++bad_frames;
        break;
      }
      led_mask = (led_mask & ~f.payload[1]) | (f.payload[2] & f.payload[1]);
      apply_leds();
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
    default:
      ++ignored_kinds;  // Pico->Pi kinds echoed back, or future commands
      break;
  }
}

struct Button {
  uint gpio;
  mb::Debouncer* deb;
  uint8_t id;
  uint8_t led_bit;  // 0 = no LED follows this control
};

Button buttons[] = {
    {MB_PIN_NAV_UP, &deb_nav_up, mb::CTL_NAV_UP, 0},
    {MB_PIN_NAV_DOWN, &deb_nav_down, mb::CTL_NAV_DOWN, 0},
    {MB_PIN_NAV_LEFT, &deb_nav_left, mb::CTL_NAV_LEFT, 0},
    {MB_PIN_NAV_RIGHT, &deb_nav_right, mb::CTL_NAV_RIGHT, 0},
    {MB_PIN_NAV_PRESS, &deb_nav_press, mb::CTL_NAV_PRESS, 0},
    {MB_PIN_ENCODER_SW, &deb_enc_sw, mb::CTL_ENCODER_SW, 0},
    {MB_PIN_REC, &deb_rec, mb::CTL_REC, mb::LED_REC},
    {MB_PIN_OVER_DUB, &deb_overdub, mb::CTL_OVER_DUB, mb::LED_OVERDUB},
    {MB_PIN_STOP, &deb_stop, mb::CTL_STOP, 0},
    {MB_PIN_PLAY, &deb_play, mb::CTL_PLAY, mb::LED_PLAY},
    {MB_PIN_PLAY_START, &deb_play_start, mb::CTL_PLAY_START, 0},
    {MB_PIN_SHIFT_EXIT, &deb_shift, mb::CTL_SHIFT_EXIT, 0},
};

// Quadrature edges per poll; 4 edges make one detent (adjust if the fitted
// encoder reports otherwise).
int8_t quad_table[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};
int quad_prev = 0, quad_acc = 0;

void scan_controls(uint32_t now_ms) {
  for (const auto& b : buttons) {
    const bool pressed = !gpio_get(b.gpio);  // active-low against GND
    if (b.deb->update(pressed, now_ms)) {
      queue_control(b.id, pressed ? 1 : 0);
      // Panel transport drives locally (no Pi round-trip); the Pi learns it
      // from the state frame + tick stream and must not echo it back (see
      // docs/protocol.md). EV_CONTROL still goes out for UI/record policy.
      // In manual mode the mapping is suppressed: events only, Pi decides.
      if (pressed && transport_local) {
        const int cmd = mb::transport_for_press(b.id, transport_running);
        if (cmd >= 0) transport_cmd(static_cast<uint8_t>(cmd));
      }
      if (b.id == mb::CTL_SHIFT_EXIT) shift_held = pressed;
      if (b.led_bit) {
        if (pressed)
          led_mask |= b.led_bit;
        else
          led_mask &= ~b.led_bit;
        apply_leds();
      }
    }
  }
  const int quad = (gpio_get(MB_PIN_ENCODER_A) ? 2 : 0) | (gpio_get(MB_PIN_ENCODER_B) ? 1 : 0);
  quad_acc += quad_table[(quad_prev << 2) | quad];
  quad_prev = quad;
  const int detents = quad_acc / kQuartersPerDetent;
  if (detents != 0) {
    quad_acc -= detents * kQuartersPerDetent;
    set_tempo(bpm_x100 + detents * (shift_held ? kTempoFineStep : kTempoStep), true);
    queue_control(mb::CTL_ENCODER_TURN, static_cast<uint8_t>(detents));
  }
}

void drain_midi() {
  for (uint8_t port = 0; port < 2; ++port) {
    RxRing::Entry e{};
    while (rxring[port].pop(e)) {
      if (midi_parse[port].feed(e.byte, e.stamp)) queue_midi(midi_parse[port].last());
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
         " " __TIME__ " session=%08lx\n",
         static_cast<unsigned long>(pico_session));

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
  quad_prev = (gpio_get(MB_PIN_ENCODER_A) ? 2 : 0) | (gpio_get(MB_PIN_ENCODER_B) ? 1 : 0);

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
  irq_set_exclusive_handler(UART0_IRQ, on_uart0_rx);
  irq_set_exclusive_handler(UART1_IRQ, on_uart1_rx);
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
    const uint32_t now_ms = to_ms_since_boot(get_absolute_time());
    if (now_ms - last_ms >= 1) {
      last_ms = now_ms;
      scan_controls(now_ms);
      ready_update();
    }
    if (now_ms - last_stats >= 1000) {
      last_stats = now_ms;
      printf("[stat] link=%d bpm=%ld.%02ld run=%d ticks=%lu txq=%lu rxpend=%d/%d bad=%llu dup=%llu txov=%lu rxov=%lu/%lu\n",
             hello_seen ? 1 : 0, static_cast<long>(bpm_x100 / 100), static_cast<long>(bpm_x100 % 100),
             transport_running ? 1 : 0, static_cast<unsigned long>(clock_ticks),
             static_cast<unsigned long>(txq.count), rxring[0].pending() ? 1 : 0,
             rxring[1].pending() ? 1 : 0, static_cast<unsigned long long>(bad_frames),
             static_cast<unsigned long long>(dup_frames), static_cast<unsigned long>(txq.overflows),
             static_cast<unsigned long>(rxring[0].overruns), static_cast<unsigned long>(rxring[1].overruns));
    }
  }
}
