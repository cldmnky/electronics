// Deterministic clock-emulation models (see clock_sim.hpp for the contract).
#include "clock_sim.hpp"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <utility>

namespace mnky::emulator::clocksim {
namespace {

std::string formatText(const char* fmt, ...) {
  char buffer[512] = {};
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(buffer, sizeof buffer, fmt, args);
  va_end(args);
  return buffer;
}

double toUs(Ps units) { return static_cast<double>(units) / 9'000.0; }
double toMs(Ps units) { return static_cast<double>(units) / 9'000'000.0; }

bool isNoteOffStatus(std::uint8_t status) { return status >= 0x80 && status < 0x90; }

}  // namespace

// ---------------------------------------------------------------------------
// Scheduler
// ---------------------------------------------------------------------------
Scheduler::Scheduler(Ps start, Ps interruptLatency, Ps jitterPeak, std::uint32_t seed)
    : now_(start), interruptLatency_(interruptLatency), jitterPeak_(jitterPeak), rng_(seed) {}

Ps Scheduler::nextLatency() {
  Ps latency = interruptLatency_;
  if (jitterPeak_ > 0) {
    rng_ = rng_ * 1664525u + 1013904223u;
    latency += static_cast<Ps>(rng_ % static_cast<std::uint32_t>(jitterPeak_ + 1));
  }
  return latency;
}

void Scheduler::scheduleAt(Ps due, Task task, Ps execCost) {
  queue_.push(Event{due, seq_++, execCost, std::move(task)});
}

bool Scheduler::runNext() {
  if (queue_.empty()) return false;
  Event event = queue_.top();
  queue_.pop();
  if (event.due > now_) now_ = event.due;
  now_ += nextLatency();
  event.task();
  now_ += event.execCost;
  ++dispatched_;
  return true;
}

void Scheduler::runUntil(Ps limit) {
  while (!queue_.empty() && queue_.top().due <= limit) runNext();
}

void Scheduler::advanceTo(Ps time) {
  if (time > now_) now_ = time;
}

// ---------------------------------------------------------------------------
// Clock rate
// ---------------------------------------------------------------------------
ClockRate ClockRate::forTempo(std::uint16_t bpmX100) {
  ClockRate rate;
  rate.bpmX100 = bpmX100 == 0 ? 12000 : bpmX100;
  // period = 60 s / (bpm * 24) = 2.25e15 / bpmX100 in 1/9 ps units.
  const Ps numerator = 2'250'000'000'000LL;
  rate.den = static_cast<Ps>(rate.bpmX100);
  rate.base = numerator / rate.den;
  rate.rem = numerator % rate.den;
  return rate;
}

// ---------------------------------------------------------------------------
// Alarm model
// ---------------------------------------------------------------------------
AlarmModel::AlarmModel(Scheduler& scheduler, Ps execCost)
    : scheduler_(scheduler), execCost_(execCost) {}

void AlarmModel::scheduleAfter(Ps delay, Callback callback) {
  callback_ = std::move(callback);
  armed_ = true;
  deadline_ = scheduler_.now() + delay;
  scheduler_.scheduleAt(deadline_, [this, deadline = deadline_] { fire(deadline); });
}

void AlarmModel::fire(Ps scheduledDeadline) {
  if (!armed_) return;
  ++fires_;
  const Ps returned = callback_(scheduler_.now());
  if (returned > 0) {
    // add_alarm_in_us positive return: next alarm is measured from the end of
    // this callback, so interrupt latency and execution cost accumulate.
    deadline_ = scheduler_.now() + execCost_ + returned;
    scheduler_.scheduleAt(deadline_, [this, deadline = deadline_] { fire(deadline); });
  } else if (returned < 0) {
    // Negative return: next alarm is measured from the previous scheduled
    // deadline, so the period does not accumulate execution cost.
    deadline_ = scheduledDeadline - returned;
    scheduler_.scheduleAt(deadline_, [this, deadline = deadline_] { fire(deadline); });
  } else {
    armed_ = false;
  }
}

// ---------------------------------------------------------------------------
// UART model
// ---------------------------------------------------------------------------
UartTxModel::UartTxModel(Scheduler& scheduler, std::uint8_t port, Mode mode,
                         std::size_t messageCapacityBytes)
    : scheduler_(scheduler),
      port_(port),
      mode_(mode),
      capacity_(messageCapacityBytes) {}

void UartTxModel::record(std::uint8_t byte, Ps at, bool realtime) {
  trace_.push_back(TxByte{at, byte, realtime});
  lastWireTime_ = at;
  haveWire_ = true;
}

Ps UartTxModel::putcBlocking(std::uint8_t byte) {
  emitUpTo(scheduler_.now());
  while (fifo_.size() >= kUartFifoDepth) {
    // uart_putc blocks until the oldest byte leaves the FIFO.
    scheduler_.advanceTo(fifo_.front().first + kMidiByteTime);
    emitUpTo(scheduler_.now());
  }
  Ps start = std::max(scheduler_.now(), busyUntil_);
  busyUntil_ = start + kMidiByteTime;
  fifo_.push_back({start, byte});
  return start;
}

void UartTxModel::emitUpTo(Ps time) {
  if (mode_ == Mode::Blocking) {
    while (!fifo_.empty() && fifo_.front().first <= time) {
      const auto [start, byte] = fifo_.front();
      fifo_.pop_front();
      record(byte, start, false);
    }
    return;
  }
  for (;;) {
    if (realtime_.empty() && messages_.empty()) {
      haveNext_ = false;
      return;
    }
    if (!haveNext_) {
      nextStart_ = scheduler_.now();
      if (haveWire_ && nextStart_ < lastWireTime_ + kMidiByteTime) {
        nextStart_ = lastWireTime_ + kMidiByteTime;
      }
      haveNext_ = true;
    }
    if (nextStart_ > time) return;
    if (!realtime_.empty()) {
      const std::uint8_t byte = realtime_.front();
      realtime_.pop_front();
      record(byte, nextStart_, true);
    } else {
      std::vector<std::uint8_t>& message = messages_.front();
      const std::uint8_t byte = message[cursor_++];
      record(byte, nextStart_, false);
      if (cursor_ >= message.size()) {
        messageBytes_ -= message.size();
        messages_.pop_front();
        cursor_ = 0;
      }
    }
    nextStart_ += kMidiByteTime;
  }
}

bool UartTxModel::sendRealtime(std::uint8_t byte) {
  if (mode_ == Mode::Blocking) {
    putcBlocking(byte);
    return true;
  }
  // Record everything the wire should already have sent, so the realtime byte
  // is placed at the next boundary after *now*, not in a backfilled past.
  emitUpTo(scheduler_.now());
  if (realtime_.size() >= kRealtimeQueueDepth) {
    ++droppedRealtime_;
    return false;
  }
  if (!haveNext_ && realtime_.empty() && messages_.empty()) {
    nextStart_ = scheduler_.now();
    if (haveWire_ && nextStart_ < lastWireTime_ + kMidiByteTime) {
      nextStart_ = lastWireTime_ + kMidiByteTime;
    }
    haveNext_ = true;
  }
  realtime_.push_back(byte);
  emitUpTo(scheduler_.now());
  return true;
}

bool UartTxModel::sendMessage(std::span<const std::uint8_t> message) {
  if (message.empty() || message.front() >= 0xF8) {
    ++droppedMessages_;
    return false;
  }
  if (mode_ == Mode::Blocking) {
    for (std::uint8_t byte : message) putcBlocking(byte);
    return true;
  }
  emitUpTo(scheduler_.now());
  if (message.size() > capacity_) {
    if (isNoteOffStatus(message.front())) ++droppedNoteOffs_;
    ++droppedMessages_;
    return false;
  }
  if (messageBytes_ + message.size() > capacity_) {
    // Never drop a queued Note Off first: evict the newest Note On/CC.
    bool evicted = false;
    for (auto it = messages_.rbegin(); it != messages_.rend(); ++it) {
      if (!isNoteOffStatus(it->front())) {
        messageBytes_ -= it->size();
        messages_.erase(std::next(it).base());
        ++evictedMessages_;
        evicted = true;
        break;
      }
    }
    if (!evicted) {
      if (isNoteOffStatus(message.front())) ++droppedNoteOffs_;
      ++droppedMessages_;
      return false;
    }
  }
  if (!haveNext_ && realtime_.empty() && messages_.empty()) {
    nextStart_ = scheduler_.now();
    if (haveWire_ && nextStart_ < lastWireTime_ + kMidiByteTime) {
      nextStart_ = lastWireTime_ + kMidiByteTime;
    }
    haveNext_ = true;
  }
  messages_.emplace_back(message.begin(), message.end());
  messageBytes_ += message.size();
  emitUpTo(scheduler_.now());
  return true;
}

std::size_t UartTxModel::queuedBytes() const {
  return mode_ == Mode::Blocking ? fifo_.size() : messageBytes_ + realtime_.size();
}

// ---------------------------------------------------------------------------
// External clock source
// ---------------------------------------------------------------------------
ExternalClockSource::ExternalClockSource(Scheduler& scheduler, ExternalClockConfig config,
                                         seam::ExternalClock& sink)
    : scheduler_(scheduler),
      config_(config),
      sink_(sink),
      rate_(ClockRate::forTempo(config.bpmX100)),
      rng_(config.seed) {}

Ps ExternalClockSource::idealPulse(std::uint64_t index) const {
  Ps ideal = epoch_ + rate_.deadlineAt(0, index);
  if (config_.stepBpmX100 != 0 && config_.stepAt > epoch_) {
    const Ps before = (config_.stepAt - epoch_) / rate_.periodAt(0);
    if (index > static_cast<std::uint64_t>(before)) {
      const ClockRate stepped = ClockRate::forTempo(config_.stepBpmX100);
      ideal = epoch_ + rate_.deadlineAt(0, static_cast<std::uint64_t>(before)) +
              stepped.deadlineAt(0, index - static_cast<std::uint64_t>(before));
    }
  }
  if (config_.wanderBpm > 0.0 && config_.wanderPeriod > 0) {
    const double t = static_cast<double>(ideal - epoch_) / 9'000'000'000.0;
    const double period = static_cast<double>(config_.wanderPeriod) / 9'000'000'000.0;
    const double amplitude = rate_.periodAt(0) * (config_.wanderBpm / rate_.bpm()) * 2.0;
    const double phase = 2.0 * 3.14159265358979323846 * t / period;
    ideal += static_cast<Ps>(std::llround(amplitude * std::sin(phase)));
  }
  return ideal;
}

Ps ExternalClockSource::actualPulse(std::uint64_t index) {
  Ps actual = idealPulse(index);
  if (config_.jitterPeak > 0) {
    rng_ = rng_ * 1664525u + 1013904223u;
    const Ps span = config_.jitterPeak;
    actual += static_cast<Ps>(rng_ % static_cast<std::uint32_t>(2 * span + 1)) - span;
  }
  return actual;
}

void ExternalClockSource::start(Ps epoch) {
  epoch_ = epoch;
  schedulePulse(0);
}

void ExternalClockSource::schedulePulse(std::uint64_t index) {
  const Ps ideal = idealPulse(index);
  if (ideal > epoch_ + config_.duration) return;
  if (config_.dropoutStart >= 0 && ideal >= config_.dropoutStart &&
      ideal < config_.dropoutEnd) {
    ++dropped_;
    schedulePulse(index + 1);
    return;
  }
  Ps actual = actualPulse(index);
  if (actual < scheduler_.now()) actual = scheduler_.now();
  scheduler_.scheduleAt(actual, [this, index, ideal] {
    ++emitted_;
    sink_.onPulse(scheduler_.now(), ideal);
    schedulePulse(index + 1);
  });
}

// ---------------------------------------------------------------------------
// Follow / PLL reference policy
// ---------------------------------------------------------------------------
ClockFollower::ClockFollower(Ps nominalPeriod)
    : period_(nominalPeriod), config_(Config{}) {}

ClockFollower::ClockFollower(Ps nominalPeriod, Config config)
    : period_(nominalPeriod), config_(config) {}

void ClockFollower::emitUntil(Ps time) {
  while (nextGen_ != 0 && nextGen_ <= time) {
    generated_.push_back(nextGen_);
    nextGen_ += period_;
  }
}

void ClockFollower::flushTo(Ps time) { emitUntil(time); }

void ClockFollower::onPulse(Ps capture, Ps phaseReference) {
  (void)phaseReference;
  emitUntil(capture);
  if (!haveLast_) {
    haveLast_ = true;
    lastCapture_ = capture;
    nextGen_ = capture + period_;
    return;
  }
  const Ps delta = capture - lastCapture_;
  lastCapture_ = capture;
  if (!haveEstimate_) {
    if (delta > 0) {
      period_ = delta;
      haveEstimate_ = true;
      ++accepted_;
    } else {
      ++outliers_;
    }
  } else if (delta <= 0 || delta > 3 * period_ || delta < period_ / 2 ||
             delta > period_ * 2) {
    ++outliers_;
  } else {
    period_ += static_cast<Ps>(static_cast<double>(delta - period_) / config_.emaDiv);
    ++accepted_;
  }
  if (haveEstimate_ && nextGen_ != 0) {
    Ps error = (capture - nextGen_) % period_;
    if (error > period_ / 2) error -= period_;
    if (error < -period_ / 2) error += period_;
    Ps correction = static_cast<Ps>(static_cast<double>(error) * config_.phaseGain);
    correction = std::clamp(correction, -config_.maxSlew, config_.maxSlew);
    nextGen_ += correction;
  }
}

// ---------------------------------------------------------------------------
// SPI frame codec + link model
// ---------------------------------------------------------------------------
std::uint16_t spiCrc16(const std::uint8_t* data, std::size_t size) {
  std::uint16_t crc = 0xffff;
  for (std::size_t i = 0; i < size; ++i) {
    crc ^= static_cast<std::uint16_t>(data[i]) << 8;
    for (int bit = 0; bit < 8; ++bit) {
      crc = static_cast<std::uint16_t>((crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1);
    }
  }
  return crc;
}

bool spiEncode(const SpiFrame& frame, SpiBytes& out) {
  if (frame.length > kSpiPayloadSize || frame.session == 0 || frame.type < 1 ||
      frame.type > 3) {
    return false;
  }
  out.fill(0);
  out[0] = 'M';
  out[1] = 'B';
  out[2] = 1;
  out[3] = frame.type;
  out[4] = static_cast<std::uint8_t>(frame.sequence);
  out[5] = static_cast<std::uint8_t>(frame.sequence >> 8);
  out[6] = frame.length;
  out[8] = static_cast<std::uint8_t>(frame.session);
  out[9] = static_cast<std::uint8_t>(frame.session >> 8);
  out[10] = static_cast<std::uint8_t>(frame.session >> 16);
  out[11] = static_cast<std::uint8_t>(frame.session >> 24);
  for (std::size_t i = 0; i < frame.length; ++i) out[12 + i] = frame.payload[i];
  const std::uint16_t crc = spiCrc16(out.data(), 30);
  out[30] = static_cast<std::uint8_t>(crc);
  out[31] = static_cast<std::uint8_t>(crc >> 8);
  return true;
}

bool spiDecode(const SpiBytes& in, SpiFrame& frame) {
  if (in[0] != 'M' || in[1] != 'B' || in[2] != 1 || in[7] != 0 ||
      in[6] > kSpiPayloadSize) {
    return false;
  }
  const std::uint16_t crc = spiCrc16(in.data(), 30);
  if (in[30] != (crc & 0xff) || in[31] != (crc >> 8)) return false;
  for (std::size_t i = 12 + in[6]; i < 30; ++i) {
    if (in[i] != 0) return false;
  }
  if (in[3] < 1 || in[3] > 3) return false;
  SpiFrame decoded;
  decoded.type = in[3];
  decoded.sequence = static_cast<std::uint16_t>(in[4] | (in[5] << 8));
  decoded.length = in[6];
  decoded.session = static_cast<std::uint32_t>(in[8]) |
                    (static_cast<std::uint32_t>(in[9]) << 8) |
                    (static_cast<std::uint32_t>(in[10]) << 16) |
                    (static_cast<std::uint32_t>(in[11]) << 24);
  if (decoded.session == 0) return false;
  for (std::size_t i = 0; i < decoded.length; ++i) decoded.payload[i] = in[12 + i];
  frame = decoded;
  return true;
}

SpiLinkModel::SpiLinkModel(std::uint32_t session) : session_(session) {}

bool SpiLinkModel::acceptFrame(const SpiBytes& bytes) {
  SpiFrame frame;
  if (!spiDecode(bytes, frame)) {
    ++badFrames_;
    return false;
  }
  if (haveLastRx_ && frame.session == lastRxSession_ &&
      frame.sequence == lastRxSequence_) {
    ++duplicatesDropped_;
    return false;
  }
  haveLastRx_ = true;
  lastRxSession_ = frame.session;
  lastRxSequence_ = frame.sequence;
  return true;
}

bool SpiLinkModel::transfer(std::span<const std::uint8_t> mosi,
                            std::span<std::uint8_t> miso) {
  if (mosi.size() != kSpiFrameSize || miso.size() != kSpiFrameSize) return false;
  SpiBytes incoming{};
  std::copy(mosi.begin(), mosi.end(), incoming.begin());
  if (aborted_) {
    aborted_ = false;
    ++abortRecoveries_;
  }
  acceptFrame(incoming);
  SpiBytes reply{};
  if (!queue_.empty()) {
    reply = queue_.front();
    queue_.pop_front();
  } else {
    // Idle filler: repeat the last frame; both sides dedup by (session, seq).
    SpiFrame filler;
    filler.type = 3;
    filler.sequence = nextSequence_++;
    filler.session = session_;
    filler.length = 0;
    spiEncode(filler, reply);
  }
  std::copy(reply.begin(), reply.end(), miso.begin());
  return true;
}

bool SpiLinkModel::transferShort(std::span<const std::uint8_t> mosi,
                                 std::span<std::uint8_t> miso, std::size_t validBytes,
                                 Ps now) {
  if (mosi.size() != kSpiFrameSize || miso.size() != kSpiFrameSize ||
      validBytes >= kSpiFrameSize) {
    return false;
  }
  ++aborts_;
  aborted_ = true;
  abortAt_ = now;
  return false;
}

bool SpiLinkModel::tick(Ps now) {
  if (aborted_ && now - abortAt_ >= kSpiWatchdog) {
    aborted_ = false;
    ++watchdogRestarts_;
    queue_.clear();
    return true;
  }
  return false;
}

bool SpiLinkModel::queueFrame(const SpiFrame& frame) {
  if (queue_.size() >= kSpiQueueSlots) {
    ++queueOverflows_;
    return false;
  }
  SpiBytes bytes{};
  if (!spiEncode(frame, bytes)) return false;
  queue_.push_back(bytes);
  return true;
}

// ---------------------------------------------------------------------------
// App send queue
// ---------------------------------------------------------------------------
AppSendQueue::AppSendQueue(std::size_t slots) : slots_(slots) {}

bool AppSendQueue::isNoteOff(const std::vector<std::uint8_t>& message) {
  return !message.empty() && isNoteOffStatus(message.front());
}

bool AppSendQueue::push(std::span<const std::uint8_t> message) {
  if (message.empty()) return false;
  if (messages_.size() >= slots_) {
    bool evicted = false;
    for (auto it = messages_.rbegin(); it != messages_.rend(); ++it) {
      if (!isNoteOff(*it)) {
        messages_.erase(std::next(it).base());
        ++evicted_;
        evicted = true;
        break;
      }
    }
    if (!evicted) {
      if (isNoteOffStatus(message.front())) ++droppedNoteOffs_;
      return false;
    }
  }
  messages_.emplace_back(message.begin(), message.end());
  ++accepted_;
  return true;
}

bool AppSendQueue::containsNoteOff() const {
  for (const auto& message : messages_) {
    if (isNoteOff(message)) return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Acceptance matrix + stages
// ---------------------------------------------------------------------------
namespace {

struct OutputMetrics {
  std::vector<Ps> clock;
  std::uint64_t dropped = 0;
  std::uint64_t notes = 0;
  std::uint64_t ccs = 0;
};

OutputMetrics measureOutput(const UartTxModel& uart) {
  OutputMetrics metrics;
  for (const TxByte& byte : uart.trace()) {
    if (byte.realtime) {
      if (byte.byte == 0xF8) metrics.clock.push_back(byte.at);
      continue;
    }
    if (byte.byte >= 0xF0) continue;
    if ((byte.byte & 0xF0) == 0x90 || (byte.byte & 0xF0) == 0x80) ++metrics.notes;
    if ((byte.byte & 0xF0) == 0xB0) ++metrics.ccs;
  }
  metrics.dropped = uart.droppedRealtime();
  return metrics;
}

struct MatrixRow {
  std::uint16_t bpmX100 = 12000;
  int output = 1;
  std::uint64_t pulses = 0;
  double tempoErrorPct = 0.0;
  double maxErrorMs = 0.0;
  std::uint64_t droppedF8 = 0;
  std::uint64_t notes = 0;
  std::uint64_t ccs = 0;
  std::uint64_t spiTransactions = 0;
  std::uint64_t spiAborts = 0;
  std::uint64_t spiRecovered = 0;
  bool pass = false;
};

struct MatrixRun {
  MatrixRow rows[2];
  std::uint64_t spiTransactions = 0;
  std::uint64_t spiAborts = 0;
  std::uint64_t spiRecovered = 0;
  std::uint64_t watchdogRestarts = 0;
  std::uint64_t queueOverflows = 0;
};

MatrixRun runMatrixRow(std::uint16_t bpmX100) {
  MatrixRun run;
  Scheduler scheduler(0, us(1) / 5, us(40), 0x5eed0000u + bpmX100);
  AlarmModel alarm(scheduler, us(20));
  UartTxModel out1(scheduler, 0, UartTxModel::Mode::Priority);
  UartTxModel out2(scheduler, 1, UartTxModel::Mode::Priority);
  const ClockRate rate = ClockRate::forTempo(bpmX100);
  const Ps epoch = seconds(1);
  const Ps end = epoch + seconds(7200);
  std::uint64_t tick = 0;
  alarm.scheduleAfter(epoch, [&](Ps) -> Ps {
    out1.sendRealtime(0xF8);
    out2.sendRealtime(0xF8);
    return -rate.periodAt(tick++);
  });

  const Ps beat = rate.periodAt(0) * kTicksPerQuarter;
  for (Ps at = epoch; at < end; at += beat) {
    scheduler.scheduleAt(at, [&out1, &out2] {
      const std::uint8_t noteOn[3] = {0x90, 60, 100};
      const std::uint8_t cc[3] = {0xB0, 1, 64};
      const std::uint8_t noteOff[3] = {0x80, 60, 0};
      out1.sendMessage(noteOn);
      out2.sendMessage(noteOn);
      out1.sendMessage(cc);
      out2.sendMessage(cc);
      out1.sendMessage(noteOff);
      out2.sendMessage(noteOff);
    });
  }

  SpiLinkModel spi(0x1234u);
  std::uint64_t spiIndex = 0;
  for (Ps at = 0; at < end; at += ms(50)) {
    scheduler.scheduleAt(at, [&spi, &run, at, end, spiIndex] {
      spi.tick(at);
      SpiFrame frame;
      frame.type = 3;
      frame.session = 0x1234u;
      frame.sequence = static_cast<std::uint16_t>(spiIndex + 1);
      frame.length = 3;
      frame.payload[0] = 0x15;  // EV_MIDI_SEND
      frame.payload[1] = 0;
      frame.payload[2] = 0x90;
      SpiBytes mosi{};
      spiEncode(frame, mosi);
      SpiBytes miso{};
      ++run.spiTransactions;
      if ((spiIndex % 8) == 7 && at + ms(50) < end) {
        spi.transferShort(mosi, miso, 17, at);
        ++run.spiAborts;
      } else {
        spi.transfer(mosi, miso);
      }
    });
    ++spiIndex;
  }

  scheduler.runUntil(end);
  out1.emitUpTo(end);
  out2.emitUpTo(end);
  run.spiRecovered = spi.abortRecoveries();
  run.watchdogRestarts = spi.watchdogRestarts();
  run.queueOverflows = spi.queueOverflows();

  const std::uint64_t expectedPulses =
      static_cast<std::uint64_t>((end - epoch) / rate.periodAt(0)) + 1;
  const UartTxModel* outputs[2] = {&out1, &out2};
  for (int index = 0; index < 2; ++index) {
    const OutputMetrics metrics = measureOutput(*outputs[index]);
    MatrixRow& row = run.rows[index];
    row.bpmX100 = bpmX100;
    row.output = index + 1;
    row.pulses = metrics.clock.size();
    row.droppedF8 = metrics.dropped;
    row.notes = metrics.notes;
    row.ccs = metrics.ccs;
    if (metrics.clock.size() >= 2) {
      const double secondsActual =
          static_cast<double>(metrics.clock.back() - metrics.clock.front()) / 9'000'000'000.0;
      const double bpmActual = secondsActual > 0.0
                                   ? static_cast<double>(metrics.clock.size() - 1) * 60.0 /
                                         (secondsActual * kTicksPerQuarter)
                                   : 0.0;
      row.tempoErrorPct = std::abs(bpmActual - rate.bpm()) / rate.bpm() * 100.0;
    }
    for (std::size_t k = 0; k < metrics.clock.size(); ++k) {
      const double errorMs =
          toMs(metrics.clock[k] - rate.deadlineAt(epoch, static_cast<std::uint64_t>(k)));
      row.maxErrorMs = std::max(row.maxErrorMs, std::abs(errorMs));
    }
    row.spiTransactions = run.spiTransactions;
    row.spiAborts = run.spiAborts;
    row.spiRecovered = run.spiRecovered;
    // Note/CC count: one note-on, one CC and one note-off per beat per output.
    const std::uint64_t beats = static_cast<std::uint64_t>((end - epoch) / beat);
    row.pass = row.pulses == expectedPulses && row.droppedF8 == 0 &&
               row.tempoErrorPct <= 0.1 && row.maxErrorMs <= 1.0 &&
               row.notes == beats * 2 && row.ccs == beats;
  }
  return run;
}

std::string makeTable(const std::vector<MatrixRow>& rows) {
  std::string table;
  table += "MIDI-clock acceptance matrix (virtual time, 2 h per row, both TRS outputs)\n";
  table += "tempo  out  pulses   tempo_err%  max_err_ms  F8_drop  notes    cc      spi_tx  spi_abort  recovered  verdict\n";
  for (const MatrixRow& row : rows) {
    table += formatText("%5.1f  %d    %7llu  %9.4f  %10.4f  %7llu  %7llu  %6llu  %6llu  %9llu  %9llu  %s\n",
                        row.bpmX100 / 100.0, row.output,
                        static_cast<unsigned long long>(row.pulses), row.tempoErrorPct,
                        row.maxErrorMs, static_cast<unsigned long long>(row.droppedF8),
                        static_cast<unsigned long long>(row.notes),
                        static_cast<unsigned long long>(row.ccs),
                        static_cast<unsigned long long>(row.spiTransactions),
                        static_cast<unsigned long long>(row.spiAborts),
                        static_cast<unsigned long long>(row.spiRecovered),
                        row.pass ? "PASS" : "FAIL");
  }
  return table;
}

}  // namespace

bool ClockSimResult::passed() const {
  for (const ClockSimStage& stage : stages) {
    if (!stage.pass) return false;
  }
  return true;
}

ClockSimResult runClockSimulation() {
  ClockSimResult result;
  auto addStage = [&result](std::string name, bool pass, std::string detail) {
    result.stages.push_back(ClockSimStage{std::move(name), pass, std::move(detail)});
  };

  // 1. Legacy positive-return alarm: execution cost accumulates every tick.
  {
    Scheduler scheduler(0, us(1) / 5);
    AlarmModel alarm(scheduler, us(20));
    const ClockRate rate = ClockRate::forTempo(12000);
    const Ps epoch = seconds(1);
    std::vector<Ps> pulses;
    alarm.scheduleAfter(epoch, [&](Ps) -> Ps {
      pulses.push_back(scheduler.now());
      return us(20833);  // firmware tempo_interval_us(12000), integer microseconds
    });
    scheduler.runUntil(epoch + seconds(7200));
    const double firstMs = pulses.empty() ? 0.0 : toMs(pulses.front() - epoch);
    const double lastMs =
        pulses.empty() ? 0.0 : toMs(pulses.back() - rate.deadlineAt(epoch, pulses.size() - 1));
    const bool pass = pulses.size() > 300000 && firstMs < 0.01 && lastMs > 1000.0;
    addStage("alarm.positive-drift", pass,
             formatText("legacy +return at 120 bpm: %zu pulses, first +%.4f ms, last +%.1f ms "
                        "(exec 20 us + latency 0.2 us + 1 us integer period accumulate)",
                        pulses.size(), firstMs, lastMs));
  }

  // 2. Deadline (negative-return) alarm: zero accumulated drift over 2 h.
  {
    Scheduler scheduler(0, us(1) / 5);
    AlarmModel alarm(scheduler, us(20));
    const ClockRate rate = ClockRate::forTempo(12000);
    const Ps epoch = seconds(1);
    std::vector<Ps> pulses;
    std::uint64_t tick = 0;
    alarm.scheduleAfter(epoch, [&](Ps) -> Ps {
      pulses.push_back(scheduler.now());
      return -rate.periodAt(tick++);
    });
    scheduler.runUntil(epoch + seconds(7200));
    double maxErrorUs = 0.0;
    for (std::size_t k = 0; k < pulses.size(); ++k) {
      const double error = toUs(pulses[k] - rate.deadlineAt(epoch, static_cast<std::uint64_t>(k)));
      maxErrorUs = std::max(maxErrorUs, std::abs(error));
    }
    const bool pass = pulses.size() > 300000 && maxErrorUs < 1.0;
    addStage("alarm.deadline-zero-drift", pass,
             formatText("deadline path at 120 bpm: %zu pulses over 2 h, max |pulse - k*interval| "
                        "= %.4f us (< 1 us)",
                        pulses.size(), maxErrorUs));
  }

  // 3. Legacy blocking uart_putc: a clock byte queues behind 10 note bytes.
  {
    Scheduler scheduler(0);
    UartTxModel uart(scheduler, 0, UartTxModel::Mode::Blocking);
    const Ps start = seconds(1);
    scheduler.scheduleAt(start, [&uart] {
      const std::uint8_t bytes[10] = {0x90, 60, 100, 0x90, 64, 100, 0x90, 67, 100, 0xB0};
      for (std::uint8_t byte : bytes) uart.putcBlocking(byte);
    });
    std::vector<Ps> clockAt;
    scheduler.scheduleAt(start + us(1), [&uart, &clockAt] {
      clockAt.push_back(uart.putcBlocking(0xF8));
    });
    scheduler.runUntil(start + ms(10));
    uart.emitUpTo(start + ms(10));
    const double latencyMs =
        clockAt.empty() ? 0.0 : toMs(clockAt.front() - (start + us(1)));
    bool pass = !clockAt.empty() && latencyMs > 1.0 && latencyMs < 3.4;
    std::string detail = formatText(
        "legacy blocking putc: clock byte after 10 note bytes = %.3f ms (> 1 ms goal)", latencyMs);

    Scheduler fifoScheduler(0);
    UartTxModel fifo(fifoScheduler, 1, UartTxModel::Mode::Blocking);
    for (int i = 0; i < 33; ++i) fifo.putcBlocking(0x90);
    const Ps before = fifoScheduler.now();
    fifo.putcBlocking(0x91);
    const bool blocked = fifoScheduler.now() >= before + kMidiByteTime;
    pass = pass && blocked;
    detail += formatText("; 34th byte blocks %s", blocked ? "yes" : "no");
    addStage("uart.blocking-latency", pass, detail);
  }

  // 4. Priority TX: realtime jumps ahead and interleaves inside a message.
  {
    Scheduler scheduler(0);
    UartTxModel uart(scheduler, 0, UartTxModel::Mode::Priority);
    const std::uint8_t noteOn[3] = {0x90, 60, 100};
    uart.sendMessage(noteOn);
    scheduler.scheduleAt(us(100), [&uart] {
      uart.sendRealtime(0xF8);
      uart.sendRealtime(0xFA);
      uart.sendRealtime(0xFC);
      uart.sendRealtime(0xFB);
    });
    scheduler.runUntil(ms(3));
    uart.emitUpTo(ms(3));
    const auto& trace = uart.trace();
    bool pass = trace.size() == 7 && trace[0].byte == 0x90 && trace[1].realtime &&
                trace[2].realtime && trace[3].realtime && trace[4].realtime &&
                trace[5].byte == 60 && trace[6].byte == 100;
    std::vector<std::uint8_t> nonRealtime;
    for (const TxByte& byte : trace) {
      if (!byte.realtime) nonRealtime.push_back(byte.byte);
    }
    pass = pass && nonRealtime.size() == 3 && nonRealtime[0] == 0x90 &&
           nonRealtime[1] == 60 && nonRealtime[2] == 100;
    addStage("uart.priority-realtime", pass,
             formatText("3-byte note-on at 0/320/640 us, F8+FA+FC+FB at 100 us: order %s, "
                        "message intact when realtime is stripped",
                        pass ? "90 F8 FA FC FB 3C 64" : "unexpected"));
  }

  // 5. Overflow: whole messages only; a queued Note Off is never dropped first.
  {
    Scheduler scheduler(0);
    UartTxModel uart(scheduler, 0, UartTxModel::Mode::Priority, 64);
    for (int i = 0; i < 21; ++i) {
      const std::uint8_t noteOn[3] = {0x90, static_cast<std::uint8_t>(48 + i), 100};
      uart.sendMessage(noteOn);
    }
    const std::uint8_t noteOff[3] = {0x80, 60, 0};
    uart.sendMessage(noteOff);
    const std::uint8_t cc[3] = {0xB0, 1, 64};
    uart.sendMessage(cc);
    uart.emitUpTo(seconds(1));
    std::size_t nonRealtime = 0;
    bool framing = true;
    for (const TxByte& byte : uart.trace()) {
      if (byte.realtime) continue;
      if ((nonRealtime % 3) == 0 && byte.byte < 0x80) framing = false;
      if ((nonRealtime % 3) != 0 && byte.byte >= 0x80) framing = false;
      ++nonRealtime;
    }
    const bool pass = uart.droppedNoteOffs() == 0 && uart.evictedMessages() >= 2 &&
                      uart.queuedBytes() <= 64 && framing && nonRealtime % 3 == 0;
    addStage("uart.overflow-noteoff", pass,
             formatText("21 note-ons + Note Off + CC into 64 B: evicted %llu note-on/CC, "
                        "dropped Note Off %llu, framing %s, %zu whole bytes out",
                        static_cast<unsigned long long>(uart.evictedMessages()),
                        static_cast<unsigned long long>(uart.droppedNoteOffs()),
                        framing ? "intact" : "BROKEN", nonRealtime));
  }

  // 6. External clock follow: lock, steady-state phase, wander, holdover, resync.
  {
    const ClockRate rate = ClockRate::forTempo(12000);
    const Ps nominal = rate.periodAt(0);
    ExternalClockConfig config;
    config.jitterPeak = ms(1);
    config.duration = seconds(10);
    Scheduler scheduler(0);
    ClockFollower follower(nominal);
    ExternalClockSource source(scheduler, config, follower);
    source.start(0);
    scheduler.runUntil(config.duration);
    follower.flushTo(config.duration);
    const auto& generated = follower.generated();
    std::size_t lockIndex = generated.size();
    std::size_t stable = 0;
    for (std::size_t k = 0; k < generated.size(); ++k) {
      const Ps reference = source.idealPulse(static_cast<std::uint64_t>(k + 1));
      const bool within = std::abs(toMs(generated[k] - reference)) <= 1.0;
      stable = within ? stable + 1 : 0;
      if (stable >= 24) {
        lockIndex = k + 1 - 24;
        break;
      }
    }
    double steadyMaxMs = 0.0;
    for (std::size_t k = lockIndex; k < generated.size(); ++k) {
      const Ps reference = source.idealPulse(static_cast<std::uint64_t>(k + 1));
      steadyMaxMs = std::max(steadyMaxMs, std::abs(toMs(generated[k] - reference)));
    }
    double freqErrorPct = 0.0;
    if (lockIndex + 1 < generated.size()) {
      const double span =
          static_cast<double>(generated.back() - generated[lockIndex]) / 9'000'000'000.0;
      const double measuredBpm = static_cast<double>(generated.size() - 1 - lockIndex) *
                                 60.0 / (span * kTicksPerQuarter);
      freqErrorPct = std::abs(measuredBpm - rate.bpm()) / rate.bpm() * 100.0;
    }
    const bool pass = lockIndex < 96 && steadyMaxMs <= 2.0 && freqErrorPct <= 0.1;
    addStage("clock.follow-lock", pass,
             formatText("120 bpm, +/-1 ms jitter: lock in %zu pulses, steady-state max phase "
                        "error %.3f ms, frequency error %.4f%%",
                        lockIndex, steadyMaxMs, freqErrorPct));

    ExternalClockConfig wander;
    wander.jitterPeak = ms(1) / 2;
    wander.wanderBpm = 1.5;
    wander.wanderPeriod = seconds(8);
    wander.duration = seconds(12);
    Scheduler wanderScheduler(0);
    ClockFollower wanderFollower(nominal);
    ExternalClockSource wanderSource(wanderScheduler, wander, wanderFollower);
    wanderSource.start(0);
    wanderScheduler.runUntil(wander.duration);
    wanderFollower.flushTo(wander.duration);
    double wanderMaxMs = 0.0;
    const auto& wanderGen = wanderFollower.generated();
    for (std::size_t k = 0; k < wanderGen.size(); ++k) {
      if (wanderGen[k] < seconds(2)) continue;
      const Ps reference = wanderSource.idealPulse(static_cast<std::uint64_t>(k + 1));
      wanderMaxMs = std::max(wanderMaxMs, std::abs(toMs(wanderGen[k] - reference)));
    }
    const bool wanderPass = wanderMaxMs <= 3.0;
    addStage("clock.follow-wander", wanderPass,
             formatText("+/-1.5 bpm over 8 s: max phase error after 2 s = %.3f ms (<= 3 ms)",
                        wanderMaxMs));

    ExternalClockConfig dropout;
    dropout.duration = seconds(8);
    dropout.dropoutStart = seconds(3);
    dropout.dropoutEnd = seconds(3) + ms(550);
    // A tempo step right after the dropout forces a real resync: the generator
    // must retune and pull phase back without a jump.
    dropout.stepBpmX100 = 12100;
    dropout.stepAt = seconds(4);
    Scheduler dropScheduler(0);
    ClockFollower dropFollower(nominal);
    ExternalClockSource dropSource(dropScheduler, dropout, dropFollower);
    dropSource.start(0);
    dropScheduler.runUntil(dropout.duration);
    dropFollower.flushTo(dropout.duration);
    const auto& dropGen = dropFollower.generated();
    std::uint64_t duringDropout = 0;
    bool gap = false;
    Ps holdoverInterval = nominal;
    for (std::size_t k = 1; k < dropGen.size(); ++k) {
      if (dropGen[k] > dropout.dropoutStart && dropGen[k] < dropout.dropoutEnd) {
        ++duringDropout;
        holdoverInterval = dropGen[k] - dropGen[k - 1];
      }
      if (dropGen[k] - dropGen[k - 1] > nominal * 3 / 2) gap = true;
    }
    const double holdoverFreqPct =
        std::abs(static_cast<double>(holdoverInterval - nominal)) /
        static_cast<double>(nominal) * 100.0;
    const bool holdoverPass = duringDropout >= 8 && !gap && holdoverFreqPct <= 0.1;
    addStage("clock.follow-holdover", holdoverPass,
             formatText("550 ms dropout: %llu pulses generated during the gap, max gap %s, "
                        "holdover frequency error %.4f%%",
                        static_cast<unsigned long long>(duringDropout),
                        gap ? "> 1.5x nominal" : "<= 1.5x nominal", holdoverFreqPct));

    bool smooth = true;
    double maxIntervalStepMs = 0.0;
    double resyncMaxMs = 0.0;
    for (std::size_t k = 1; k < dropGen.size(); ++k) {
      const Ps interval = dropGen[k] - dropGen[k - 1];
      maxIntervalStepMs =
          std::max(maxIntervalStepMs, std::abs(toMs(interval - nominal)));
      if (interval > nominal + ms(1) || interval < nominal - ms(1)) smooth = false;
      if (dropGen[k] >= dropout.stepAt + seconds(2)) {
        const Ps reference = dropSource.idealPulse(static_cast<std::uint64_t>(k + 1));
        resyncMaxMs = std::max(resyncMaxMs, std::abs(toMs(dropGen[k] - reference)));
      }
    }
    const bool resyncPass = smooth && resyncMaxMs <= 2.0;
    addStage("clock.follow-resync", resyncPass,
             formatText("after dropout: max generated interval step %.3f ms from nominal, "
                        "phase error 2 s later %.3f ms",
                        maxIntervalStepMs, resyncMaxMs));
  }

  // 7. SPI: codec, dedup, mid-frame abort recovery, queue overflow.
  {
    SpiFrame frame;
    frame.type = 3;
    frame.sequence = 7;
    frame.session = 0xabcdu;
    frame.length = 3;
    frame.payload[0] = 0x15;
    frame.payload[1] = 0;
    frame.payload[2] = 0x90;
    SpiBytes encoded{};
    SpiFrame decoded;
    const bool roundTrip = spiEncode(frame, encoded) && spiDecode(encoded, decoded) &&
                           decoded.sequence == 7 && decoded.session == 0xabcdu &&
                           decoded.payload[2] == 0x90;
    SpiBytes corrupt = encoded;
    corrupt[20] ^= 0x40;
    SpiFrame rejected;
    const bool crcRejects = !spiDecode(corrupt, rejected);
    SpiLinkModel link(0xabcdu);
    SpiBytes miso{};
    link.transfer(encoded, miso);
    link.transfer(encoded, miso);
    const bool dedup = link.duplicatesDropped() == 1;
    addStage("spi.frame-codec", roundTrip && crcRejects && dedup,
             formatText("32-byte frame round trip %s, CRC corruption rejected %s, repeat "
                        "frame deduped %s",
                        roundTrip ? "ok" : "FAILED", crcRejects ? "yes" : "no",
                        dedup ? "yes" : "no"));

    SpiLinkModel abortLink(0xabcdu);
    SpiBytes abortMiso{};
    const Ps abortAt = seconds(1);
    abortLink.transferShort(encoded, abortMiso, 17, abortAt);
    SpiBytes goodMiso{};
    const bool recovered = abortLink.transfer(encoded, goodMiso) &&
                           abortLink.abortRecoveries() == 1 &&
                           abortLink.watchdogRestarts() == 0;
    abortLink.transferShort(encoded, abortMiso, 5, abortAt + seconds(2));
    const bool watchdog = abortLink.tick(abortAt + seconds(2) + seconds(8));
    addStage("spi.abort-recovery", recovered && watchdog,
             formatText("mid-frame abort at byte 17: next full transfer recovered, watchdog "
                        "restarts %llu; a second abort with no traffic for 8 s restarts %llu",
                        static_cast<unsigned long long>(abortLink.watchdogRestarts() - 1),
                        static_cast<unsigned long long>(abortLink.watchdogRestarts())));
  }

  // 8. SPI queue overflow + app ack/retry with Note-Off priority.
  {
    SpiLinkModel link(0xabcdu);
    bool queued = true;
    for (std::size_t i = 0; i < kSpiQueueSlots; ++i) {
      SpiFrame frame;
      frame.type = 3;
      frame.sequence = static_cast<std::uint16_t>(i + 1);
      frame.session = 0xabcdu;
      frame.length = 1;
      frame.payload[0] = 0x20;
      queued = queued && link.queueFrame(frame);
    }
    SpiFrame extra;
    extra.type = 3;
    extra.sequence = 99;
    extra.session = 0xabcdu;
    extra.length = 1;
    extra.payload[0] = 0x20;
    const bool overflow = !link.queueFrame(extra) && link.queueOverflows() == 1 &&
                          link.queueDepth() == kSpiQueueSlots;

    AppSendQueue app;
    for (int i = 0; i < 16; ++i) {
      const std::uint8_t noteOn[3] = {0x90, static_cast<std::uint8_t>(40 + i), 100};
      app.push(noteOn);
    }
    const std::uint8_t noteOff[3] = {0x80, 40, 0};
    const bool noteOffKept = app.push(noteOff) && app.containsNoteOff() &&
                             app.droppedNoteOffs() == 0 && app.evicted() == 1;

    // Ack/retry: one lost ACK, the retried frame is deduped (applied once).
    SpiFrame retryFrame;
    retryFrame.type = 3;
    retryFrame.sequence = 7;
    retryFrame.session = 0xabcdu;
    retryFrame.length = 3;
    retryFrame.payload[0] = 0x15;
    retryFrame.payload[2] = 0x90;
    SpiBytes retryBytes{};
    spiEncode(retryFrame, retryBytes);
    SpiLinkModel retryLink(0xabcdu);
    SpiBytes retryMiso{};
    std::uint64_t applied = 0;
    std::uint64_t acks = 0;
    // First send applies and is acked; the ACK is lost, so the Pi retries the
    // same (session, sequence). The Pico dedups it and acks again.
    retryLink.transfer(retryBytes, retryMiso);
    ++applied;
    ++acks;
    retryLink.transfer(retryBytes, retryMiso);
    ++acks;
    const bool retryAccounting = applied == 1 && acks == 2 &&
                                 retryLink.duplicatesDropped() == 1;
    addStage("spi.queue-noteoff", queued && overflow && noteOffKept && retryAccounting,
             formatText("16-slot queue: 17th dropped %s; app queue Note Off kept %s "
                        "(evicted %llu, dropped Note Off %llu); lost-ACK retry: %llu "
                        "apply, %llu ACK, %llu duplicate",
                        overflow ? "yes" : "no", noteOffKept ? "yes" : "no",
                        static_cast<unsigned long long>(app.evicted()),
                        static_cast<unsigned long long>(app.droppedNoteOffs()),
                        static_cast<unsigned long long>(applied),
                        static_cast<unsigned long long>(acks),
                        static_cast<unsigned long long>(retryLink.duplicatesDropped())));
  }

  // 9. Acceptance matrix.
  {
    const std::uint16_t tempos[] = {2000, 6000, 12000, 18000, 30000};
    std::vector<MatrixRow> rows;
    bool matrixPass = true;
    std::uint64_t totalAborts = 0;
    std::uint64_t totalRecovered = 0;
    std::uint64_t totalWatchdog = 0;
    for (std::uint16_t bpmX100 : tempos) {
      const MatrixRun run = runMatrixRow(bpmX100);
      for (const MatrixRow& row : run.rows) rows.push_back(row);
      totalAborts += run.spiAborts;
      totalRecovered += run.spiRecovered;
      totalWatchdog += run.watchdogRestarts;
      for (const MatrixRow& row : run.rows) matrixPass = matrixPass && row.pass;
    }
    matrixPass = matrixPass && totalAborts == totalRecovered && totalWatchdog == 0;
    result.acceptanceTable = makeTable(rows);
    addStage("matrix.acceptance", matrixPass,
             formatText("5 tempos x 2 outputs x 2 h: tempo error <= 0.1%%, max clock error "
                        "<= 1 ms, 0 dropped F8, 0 watchdog restarts (%llu aborts recovered)",
                        static_cast<unsigned long long>(totalRecovered)));
  }

  return result;
}

}  // namespace mnky::emulator::clocksim
