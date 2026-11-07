#pragma once
// Deterministic host-side clock-emulation harness (phase 1: hardware models).
//
// The expert MIDI-clock review (2026-11-07) is made CI-verifiable here: every
// model runs on virtual time, with no wall-clock sleeps, and the acceptance
// matrix prints the table the bench run later reproduces on the logic analyser.
// Phase 2 wires the real firmware scheduler through the seam interfaces below
// and re-runs the same scenarios.
//
// Time base: integer 1/9 ps ticks. Every 24-PPQN interval at the acceptance
// tempos (20/60/120/180/300 bpm) is an exact integer in this unit, so the
// deadline path can be asserted against k*interval with zero rounding drift.
// Resolution is 0.11 ps and a 2 h run uses 6.5e16 ticks, well inside int64.
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <queue>
#include <span>
#include <string>
#include <vector>

namespace mnky::emulator::clocksim {

using Ps = std::int64_t;
inline constexpr Ps kUnitsPerPs = 9;
constexpr Ps ps(std::int64_t value) { return value * kUnitsPerPs; }
constexpr Ps us(std::int64_t value) { return value * 9'000; }
constexpr Ps ms(std::int64_t value) { return value * 9'000'000; }
constexpr Ps seconds(std::int64_t value) { return value * 9'000'000'000; }

// MIDI / RP2350 constants (datasheet + MIDI 1.0).
inline constexpr Ps kMidiByteTime = us(320);  // 31250 baud, 10 bits per byte
inline constexpr std::size_t kUartFifoDepth = 32;
inline constexpr std::size_t kRealtimeQueueDepth = 64;
inline constexpr int kTicksPerQuarter = 24;
inline constexpr std::size_t kSpiFrameSize = 32;
inline constexpr std::size_t kSpiPayloadSize = 18;
inline constexpr std::size_t kSpiQueueSlots = 16;
inline constexpr Ps kSpiWatchdog = seconds(8);

// ---------------------------------------------------------------------------
// Phase-2 seam: the firmware scheduler implements these; the harness only
// calls them. Units are 1/9 ps here, the firmware adapter quantises to the
// RP2350 1 MHz timer tick.
// ---------------------------------------------------------------------------
namespace seam {
using Ps = clocksim::Ps;

class TimeSource {
 public:
  virtual ~TimeSource() = default;
  virtual Ps now() const = 0;
};

// Mirrors add_alarm_in_us semantics exactly:
//   return > 0  -> next alarm at the CALLBACK'S END + return
//                  (execution time + interrupt latency accumulate: the drift bug)
//   return < 0  -> next alarm at the PREVIOUS SCHEDULED DEADLINE + |return|
//   return == 0 -> stop
class Alarm {
 public:
  using Callback = std::function<Ps(Ps now)>;
  virtual ~Alarm() = default;
  virtual void scheduleAfter(Ps delay, Callback callback) = 0;
  virtual bool armed() const = 0;
};

class UartTx {
 public:
  virtual ~UartTx() = default;
  // Realtime F8/FA/FB/FC: jumps ahead of queued note/CC, may interleave between
  // the bytes of a multi-byte message (MIDI 1.0). False on queue overflow.
  virtual bool sendRealtime(std::uint8_t byte) = 0;
  // Whole non-realtime message, queued atomically (never emitted partially).
  virtual bool sendMessage(std::span<const std::uint8_t> message) = 0;
  // Start time of the last byte put on the wire.
  virtual Ps wireTime() const = 0;
};

class SpiLink {
 public:
  virtual ~SpiLink() = default;
  // One CSn transaction. A short transfer (validBytes < 32) models the Linux
  // master aborting mid-frame; the slave must recover without a watchdog reset.
  virtual bool transfer(std::span<const std::uint8_t> mosi,
                        std::span<std::uint8_t> miso) = 0;
};

// Implemented by the firmware follow/PLL policy. `phaseReference` is the
// source's jitter-free musical phase (the ideal tick time).
class ExternalClock {
 public:
  virtual ~ExternalClock() = default;
  virtual void onPulse(Ps capture, Ps phaseReference) = 0;
};
}  // namespace seam

// ---------------------------------------------------------------------------
// Deterministic discrete-event scheduler. Each dispatch advances virtual time
// by the modelled interrupt latency plus the callback's execution cost, so a
// callback that re-arms from "now" accumulates both (the documented bug).
// ---------------------------------------------------------------------------
class Scheduler {
 public:
  using Task = std::function<void()>;

  explicit Scheduler(Ps start = 0, Ps interruptLatency = 0, Ps jitterPeak = 0,
                     std::uint32_t seed = 1);

  void scheduleAt(Ps due, Task task, Ps execCost = 0);
  void scheduleAfter(Ps delay, Task task, Ps execCost = 0) {
    scheduleAt(now_ + delay, std::move(task), execCost);
  }
  bool runNext();
  // Dispatches every event due at or before `limit`.
  void runUntil(Ps limit);
  // Blocking work (uart_putc waiting for FIFO space): moves time forward
  // without dispatching; due events run afterwards, as an ISR stall would.
  void advanceTo(Ps time);

  Ps now() const { return now_; }
  std::uint64_t dispatched() const { return dispatched_; }
  bool empty() const { return queue_.empty(); }
  Ps interruptLatency() const { return interruptLatency_; }
  Ps jitterPeak() const { return jitterPeak_; }

 private:
  struct Event {
    Ps due = 0;
    std::uint64_t seq = 0;
    Ps execCost = 0;
    Task task;
  };
  struct Later {
    bool operator()(const Event& a, const Event& b) const {
      if (a.due != b.due) return a.due > b.due;
      return a.seq > b.seq;
    }
  };
  Ps nextLatency();

  std::priority_queue<Event, std::vector<Event>, Later> queue_;
  Ps now_ = 0;
  Ps interruptLatency_ = 0;
  Ps jitterPeak_ = 0;
  std::uint32_t rng_ = 1;
  std::uint64_t seq_ = 0;
  std::uint64_t dispatched_ = 0;
};

// Exact 24-PPQN period as a rational in 1/9 ps units. deadlineAt(epoch, k) is
// floor(k*period) from the epoch with no accumulated rounding.
struct ClockRate {
  std::uint16_t bpmX100 = 12000;
  Ps base = 0;  // floor(period)
  Ps rem = 0;   // period fractional remainder
  Ps den = 1;

  static ClockRate forTempo(std::uint16_t bpmX100);
  Ps deadlineAt(Ps epoch, std::uint64_t index) const {
    return epoch + static_cast<Ps>(index) * base +
           (static_cast<Ps>(index) * rem) / den;
  }
  Ps periodAt(std::uint64_t index) const {
    return deadlineAt(0, index + 1) - deadlineAt(0, index);
  }
  double bpm() const { return static_cast<double>(bpmX100) / 100.0; }
  double periodUs() const {
    return (static_cast<double>(base) +
            static_cast<double>(rem) / static_cast<double>(den)) / 9'000.0;
  }
};

// RP2350 add_alarm_in_us model, including the positive/negative return split.
class AlarmModel final : public seam::Alarm {
 public:
  AlarmModel(Scheduler& scheduler, Ps execCost);
  void scheduleAfter(Ps delay, Callback callback) override;
  bool armed() const override { return armed_; }
  std::uint64_t fires() const { return fires_; }

 private:
  void fire(Ps scheduledDeadline);
  Scheduler& scheduler_;
  Ps execCost_;
  Callback callback_;
  bool armed_ = false;
  Ps deadline_ = 0;
  std::uint64_t fires_ = 0;
};

struct TxByte {
  Ps at = 0;  // start of the byte on the wire
  std::uint8_t byte = 0;
  bool realtime = false;
};

// 31250-baud UART transmitter with a 32-byte TX FIFO.
//  - Blocking mode is the legacy ISR path: uart_putc() waits for FIFO space and
//    a clock byte queues behind every pending note byte (measurable latency).
//  - Priority mode is the new path: realtime bytes jump the queue at the next
//    byte boundary and may interleave inside a multi-byte message; non-realtime
//    messages are queued whole, and overflow evicts a Note On/CC before any
//    queued Note Off (a dropped Note Off is a hung note).
class UartTxModel final : public seam::UartTx {
 public:
  enum class Mode { Blocking, Priority };

  UartTxModel(Scheduler& scheduler, std::uint8_t, Mode mode,
              std::size_t messageCapacityBytes = 64);
  ~UartTxModel() override = default;

  // Legacy blocking putc. Returns the byte's wire start time; may advance
  // virtual time when the FIFO is full.
  Ps putcBlocking(std::uint8_t byte);
  bool sendRealtime(std::uint8_t byte) override;
  bool sendMessage(std::span<const std::uint8_t> message) override;
  Ps wireTime() const override { return lastWireTime_; }

  // Records every byte whose transmission has started at or before `time`.
  void emitUpTo(Ps time);
  const std::vector<TxByte>& trace() const { return trace_; }

  std::uint64_t droppedRealtime() const { return droppedRealtime_; }
  std::uint64_t droppedMessages() const { return droppedMessages_; }
  std::uint64_t droppedNoteOffs() const { return droppedNoteOffs_; }
  std::uint64_t evictedMessages() const { return evictedMessages_; }
  std::size_t queuedBytes() const;

 private:
  static bool isNoteOff(std::uint8_t status) { return status >= 0x80 && status < 0x90; }
  void record(std::uint8_t byte, Ps at, bool realtime);

  Scheduler& scheduler_;
  Mode mode_;
  std::size_t capacity_;
  std::vector<TxByte> trace_;
  Ps lastWireTime_ = 0;
  bool haveWire_ = false;
  Ps nextStart_ = 0;
  bool haveNext_ = false;
  Ps busyUntil_ = 0;

  // Blocking FIFO: queued bytes with their wire start time.
  std::deque<std::pair<Ps, std::uint8_t>> fifo_;

  // Priority queues: realtime bytes and whole non-realtime messages.
  std::deque<std::uint8_t> realtime_;
  std::deque<std::vector<std::uint8_t>> messages_;
  std::size_t messageBytes_ = 0;
  std::size_t cursor_ = 0;

  std::uint64_t droppedRealtime_ = 0;
  std::uint64_t droppedMessages_ = 0;
  std::uint64_t droppedNoteOffs_ = 0;
  std::uint64_t evictedMessages_ = 0;
};

// ---------------------------------------------------------------------------
// External clock input model: jitter, slow wander, hard dropout, phase
// reference. The follower below is the reference policy phase 2's firmware
// implementation must match; the harness measures phase/frequency error.
// ---------------------------------------------------------------------------
struct ExternalClockConfig {
  std::uint16_t bpmX100 = 12000;
  Ps jitterPeak = 0;          // uniform +/- arrival jitter
  double wanderBpm = 0.0;     // slow sinusoidal tempo wander, +/- bpm
  Ps wanderPeriod = seconds(8);
  Ps dropoutStart = -1;       // -1 = no dropout
  Ps dropoutEnd = -1;
  std::uint16_t stepBpmX100 = 0;  // 0 = no tempo step
  Ps stepAt = 0;
  Ps duration = seconds(10);
  std::uint32_t seed = 7;
};

class ExternalClockSource {
 public:
  ExternalClockSource(Scheduler& scheduler, ExternalClockConfig config,
                      seam::ExternalClock& sink);
  void start(Ps epoch);
  Ps idealPulse(std::uint64_t index) const;
  std::uint64_t emitted() const { return emitted_; }
  std::uint64_t dropped() const { return dropped_; }

 private:
  void schedulePulse(std::uint64_t index);
  Ps actualPulse(std::uint64_t index);

  Scheduler& scheduler_;
  ExternalClockConfig config_;
  seam::ExternalClock& sink_;
  ClockRate rate_;
  Ps epoch_ = 0;
  std::uint32_t rng_ = 1;
  std::uint64_t emitted_ = 0;
  std::uint64_t dropped_ = 0;
};

// Reference follow/PLL policy: EMA interval estimate (like the firmware
// ClockFollow) plus bounded per-tick phase slew, so resync never jumps.
class ClockFollower final : public seam::ExternalClock {
 public:
  struct Config {
    double emaDiv = 8.0;
    double phaseGain = 0.25;
    Ps maxSlew = us(200);
  };

  explicit ClockFollower(Ps nominalPeriod);
  ClockFollower(Ps nominalPeriod, Config config);
  void onPulse(Ps capture, Ps phaseReference) override;
  // Records generated pulses up to `time` (the free-running generator keeps
  // emitting during a dropout; only its period is retuned).
  void flushTo(Ps time);
  const std::vector<Ps>& generated() const { return generated_; }
  Ps periodEstimate() const { return period_; }
  std::uint64_t outliers() const { return outliers_; }
  std::uint64_t pulses() const { return accepted_; }
  bool haveEstimate() const { return haveEstimate_; }

 private:
  void emitUntil(Ps time);
  Ps nextGen_ = 0;
  Ps period_ = 0;
  Ps lastCapture_ = 0;
  bool haveLast_ = false;
  bool haveEstimate_ = false;
  Config config_;
  std::vector<Ps> generated_;
  std::uint64_t outliers_ = 0;
  std::uint64_t accepted_ = 0;
};

// ---------------------------------------------------------------------------
// SPI link model: 32-byte frame codec, transaction-level aborts, 16-slot
// outgoing queue and app-message Note-Off priority.
// ---------------------------------------------------------------------------
struct SpiFrame {
  std::uint8_t type = 0;
  std::uint16_t sequence = 0;
  std::uint32_t session = 0;
  std::uint8_t length = 0;
  std::array<std::uint8_t, kSpiPayloadSize> payload{};
};
using SpiBytes = std::array<std::uint8_t, kSpiFrameSize>;

std::uint16_t spiCrc16(const std::uint8_t* data, std::size_t size);
bool spiEncode(const SpiFrame& frame, SpiBytes& out);
bool spiDecode(const SpiBytes& in, SpiFrame& frame);

class SpiLinkModel final : public seam::SpiLink {
 public:
  explicit SpiLinkModel(std::uint32_t session);
  bool transfer(std::span<const std::uint8_t> mosi,
                std::span<std::uint8_t> miso) override;
  // Short transaction (CSn deasserted after `validBytes`): models a killed
  // Linux transfer. The partial frame is discarded; the next full transfer
  // must recover without the 8 s watchdog.
  bool transferShort(std::span<const std::uint8_t> mosi,
                     std::span<std::uint8_t> miso, std::size_t validBytes, Ps now);
  // Advances the watchdog: an aborted transaction with no follow-up within
  // 8 s restarts the slave. Returns true when a restart happened.
  bool tick(Ps now);

  bool queueFrame(const SpiFrame& frame);
  std::size_t queueDepth() const { return queue_.size(); }
  std::uint64_t queueOverflows() const { return queueOverflows_; }
  std::uint64_t duplicatesDropped() const { return duplicatesDropped_; }
  std::uint64_t badFrames() const { return badFrames_; }
  std::uint64_t aborts() const { return aborts_; }
  std::uint64_t abortRecoveries() const { return abortRecoveries_; }
  std::uint64_t watchdogRestarts() const { return watchdogRestarts_; }
  std::uint32_t session() const { return session_; }

 private:
  bool acceptFrame(const SpiBytes& bytes);
  std::uint32_t session_;
  std::uint16_t nextSequence_ = 1;
  std::deque<SpiBytes> queue_;
  bool aborted_ = false;
  Ps abortAt_ = 0;
  std::uint32_t lastRxSession_ = 0;
  std::uint16_t lastRxSequence_ = 0;
  bool haveLastRx_ = false;
  std::uint64_t queueOverflows_ = 0;
  std::uint64_t duplicatesDropped_ = 0;
  std::uint64_t badFrames_ = 0;
  std::uint64_t aborts_ = 0;
  std::uint64_t abortRecoveries_ = 0;
  std::uint64_t watchdogRestarts_ = 0;
};

// Pi -> Pico application send queue: 16 whole-message slots. On overflow the
// newest Note On/CC is evicted; a queued Note Off is never dropped first.
class AppSendQueue {
 public:
  explicit AppSendQueue(std::size_t slots = kSpiQueueSlots);
  bool push(std::span<const std::uint8_t> message);
  bool containsNoteOff() const;
  std::size_t depth() const { return messages_.size(); }
  std::uint64_t evicted() const { return evicted_; }
  std::uint64_t droppedNoteOffs() const { return droppedNoteOffs_; }
  std::uint64_t accepted() const { return accepted_; }
  const std::deque<std::vector<std::uint8_t>>& messages() const { return messages_; }

 private:
  static bool isNoteOff(const std::vector<std::uint8_t>& message);
  std::size_t slots_;
  std::deque<std::vector<std::uint8_t>> messages_;
  std::uint64_t evicted_ = 0;
  std::uint64_t droppedNoteOffs_ = 0;
  std::uint64_t accepted_ = 0;
};

// ---------------------------------------------------------------------------
// Acceptance matrix + scenario report.
// ---------------------------------------------------------------------------
struct ClockSimStage {
  std::string name;
  bool pass = false;
  std::string detail;
};

struct ClockSimResult {
  std::vector<ClockSimStage> stages;
  std::string acceptanceTable;
  bool passed() const;
};

// Runs every model stage and the review's acceptance matrix (2 h per tempo,
// 20/60/120/180/300 bpm, both TRS outputs, note/CC + SPI + host churn load).
ClockSimResult runClockSimulation();

}  // namespace mnky::emulator::clocksim
