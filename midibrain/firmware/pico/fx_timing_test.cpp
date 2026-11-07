// P2 host evidence: timestamped clocked-FX emission and the dual-core load
// decision.
//
// Two questions, both measured on the clock_sim models (linked unchanged):
//
//  1. Does the TX scheduler's timed-release path put a clock-derived event on
//     the wire at the grid point it was stamped with, without ever letting it
//     displace a realtime byte (F8/FA/FB/FC)?
//  2. With one core carrying the whole load (24-PPQN clock, note/CC traffic,
//     SPI churn AND clocked FX traffic), does the worst-case clock error stay
//     inside the 1 ms acceptance budget - or does the load matrix demand a
//     second core?
//
// The wire model is the firmware scheduler (mb::MidiTxScheduler) driven through
// a lazy 31250-baud byte boundary, exactly like clock_sim's UartTxModel: bytes
// are placed at the next boundary after the moment they become available, and
// realtime bytes preempt normal messages at the next boundary. The clock alarm
// enqueues F8 first and then releases due FX events, which is the firmware's
// order (midi_emit for the tick, then releaseDue).
#include "midi_tx_scheduler.hpp"

#include "../../pi/emulator/clock_sim.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

using namespace mnky::emulator::clocksim;

std::int64_t unitsToNs(Ps units) { return units / 9'000; }
double toMs(Ps units) { return static_cast<double>(units) / 9'000'000.0; }

// Drives the firmware MidiTxScheduler against the virtual wire.
class FirmwareWire {
 public:
  explicit FirmwareWire(Scheduler& scheduler) : scheduler_(scheduler) {}

  // Records every byte whose slot starts at or before `time`.
  void flushTo(Ps time) {
    for (;;) {
      if (!tx_.pending()) {
        haveNext_ = false;
        return;
      }
      if (!haveNext_) {
        nextStart_ = std::max(floor_, scheduler_.now());
        if (haveWire_ && nextStart_ < lastWire_ + kMidiByteTime)
          nextStart_ = lastWire_ + kMidiByteTime;
        haveNext_ = true;
      }
      if (nextStart_ > time) return;
      const int byte = tx_.nextByte();
      if (byte < 0) {
        haveNext_ = false;
        return;
      }
      trace_.push_back(TxByte{nextStart_, static_cast<std::uint8_t>(byte), byte >= 0xF8});
      lastWire_ = nextStart_;
      haveWire_ = true;
      nextStart_ += kMidiByteTime;
    }
  }

  // Clock/transport byte: realtime class, preempts normal messages at the next
  // byte boundary.
  void realtime(Ps at, std::uint8_t byte) {
    publish(at);
    tx_.enqueueRealtime(byte);
    flushTo(at);
  }

  // Ordinary non-clock message (input traffic, Pi EV_MIDI_SEND).
  void message(Ps at, const std::uint8_t* data, std::size_t size) {
    publish(at);
    tx_.enqueue(data, static_cast<std::uint32_t>(size));
    flushTo(at);
  }

  // Clock-derived event stamped with its absolute grid deadline. `at` is the
  // generation moment (after the main-loop service lag); a deadline already in
  // the past is due immediately, a future one waits for the grid.
  void timed(Ps at, Ps deadline, const std::uint8_t* data, std::size_t size) {
    publish(at);
    tx_.enqueueAt(static_cast<std::uint64_t>(unitsToNs(deadline)), data,
                  static_cast<std::uint32_t>(size));
    flushTo(at);
  }

  // Grid-time release pump: the caller passes the grid point that has just
  // become current, never wall time, so a FX event can only be promoted after
  // that tick's F8 was enqueued.
  void release(Ps at, Ps gridNow) {
    publish(at);
    tx_.releaseDue(static_cast<std::uint64_t>(unitsToNs(gridNow)));
    flushTo(at);
  }

  std::size_t traceSize() const { return trace_.size(); }
  const std::vector<TxByte>& trace() const { return trace_; }
  const mb::MidiTxScheduler& scheduler() const { return tx_; }

 private:
  // Snapshots the wire up to `at` and floors the next byte's slot at the
  // generated-at moment, so a later flush cannot backdate or forward-date it.
  void publish(Ps at) {
    flushTo(at);
    floor_ = std::max(floor_, at);
  }

  Scheduler& scheduler_;
  mb::MidiTxScheduler tx_;
  std::vector<TxByte> trace_;
  Ps nextStart_ = 0;
  Ps lastWire_ = 0;
  Ps floor_ = 0;
  bool haveNext_ = false;
  bool haveWire_ = false;
};

void collectClock(const std::vector<TxByte>& trace, std::vector<Ps>& out,
                  std::uint64_t& dropped) {
  for (const TxByte& byte : trace) {
    if (byte.realtime && byte.byte == 0xF8) out.push_back(byte.at);
  }
  (void)dropped;
}

// Finds the first byte at or after `from` that starts a message with this
// status/note pair. FX notes use note numbers 72..83 while ordinary traffic
// uses 60/64, so a match is unambiguous inside one generation window.
bool findMessageStart(const std::vector<TxByte>& trace, std::size_t from,
                      std::uint8_t status, std::uint8_t note, Ps& start) {
  for (std::size_t i = from; i < trace.size(); ++i) {
    const TxByte& byte = trace[i];
    if (byte.realtime || byte.byte != status) continue;
    for (std::size_t j = i + 1; j < trace.size(); ++j) {
      if (trace[j].realtime) continue;  // legal MIDI interleave
      if (trace[j].byte == note) {
        start = byte.at;
        return true;
      }
      break;
    }
  }
  return false;
}

// ---------------------------------------------------------------------------
// 1. Timestamped FX emission: deadline, byte-boundary budget, realtime first.
// ---------------------------------------------------------------------------
bool runEmissionStage(std::string& detail) {
  Scheduler scheduler(0, 0);
  FirmwareWire wire(scheduler);
  const ClockRate rate = ClockRate::forTempo(12000);
  const Ps epoch = seconds(1);
  const Ps deadline = epoch + rate.periodAt(0);  // tick 1
  Ps f8At = -1;
  Ps fxAt = -1;
  std::size_t fxHint = 0;  // trace index after the tick byte was placed
  const std::uint8_t fx[3] = {0x90, 72, 100};

  scheduler.scheduleAt(deadline - ms(2), [&] {
    // An FX engine generates its tick-N event ahead of the grid point and
    // stamps it with tick N's absolute deadline.
    wire.timed(scheduler.now(), deadline, fx, 3);
  });
  scheduler.scheduleAt(deadline, [&] {
    // Firmware order: the clock alarm enqueues the tick byte, then pumps the
    // due FX queue.
    wire.realtime(scheduler.now(), 0xF8);
    wire.release(scheduler.now(), deadline);
    fxHint = wire.traceSize();
  });
  scheduler.runUntil(deadline + ms(5));
  wire.flushTo(deadline + ms(5));
  const auto& trace = wire.trace();
  bool f8Seen = false;
  for (const TxByte& byte : trace) {
    if (byte.realtime && byte.byte == 0xF8 && !f8Seen) {
      f8At = byte.at;
      f8Seen = true;
    }
  }
  const bool found = findMessageStart(trace, fxHint, 0x90, 72, fxAt);
  const double f8ErrorMs = f8Seen ? toMs(f8At - deadline) : 1e9;
  const double fxErrorMs = found ? toMs(fxAt - deadline) : 1e9;
  // Byte-boundary budget: the tick byte plus at most one message byte that was
  // already in flight when the FX event became due.
  const double budgetMs = toMs(2 * kMidiByteTime);
  const bool pass = f8Seen && found && std::abs(f8ErrorMs) <= 0.05 &&
                    fxErrorMs >= -0.001 && fxErrorMs <= budgetMs &&
                    fxAt >= f8At;
  detail = "tick 1 at 20.833 ms: F8 +" + std::to_string(f8ErrorMs) +
           " ms, FX note +" + std::to_string(fxErrorMs) +
           " ms (byte-boundary budget " + std::to_string(budgetMs) +
           " ms), F8 before FX " + (fxAt >= f8At ? "yes" : "NO");
  return pass;
}

// Realtime precedence: a transport burst enqueued at the same grid point as a
// due FX message still owns the wire, and a queued FX message is split legally
// around it.
bool runRealtimePrecedenceStage(std::string& detail) {
  Scheduler scheduler(0, 0);
  FirmwareWire wire(scheduler);
  const ClockRate rate = ClockRate::forTempo(12000);
  const Ps deadline = seconds(1) + rate.periodAt(0);
  const std::uint8_t note[3] = {0x90, 72, 100};
  scheduler.scheduleAt(deadline, [&] {
    wire.timed(scheduler.now(), deadline, note, 3);
    // A realtime burst lands at the same grid point, ahead of the release.
    for (std::uint8_t byte : {0xF8, 0xFA, 0xFC, 0xFB}) wire.realtime(scheduler.now(), byte);
    wire.release(scheduler.now(), deadline);
  });
  scheduler.runUntil(deadline + ms(5));
  wire.flushTo(deadline + ms(5));
  std::size_t firstNormal = wire.trace().size();
  std::vector<std::uint8_t> realtimeOrder;
  for (std::size_t i = 0; i < wire.trace().size(); ++i) {
    const TxByte& byte = wire.trace()[i];
    if (byte.realtime) {
      realtimeOrder.push_back(byte.byte);
    } else if (firstNormal == wire.trace().size()) {
      firstNormal = i;
    }
  }
  bool realtimeBeforeFx = true;
  for (std::size_t i = firstNormal; i < wire.trace().size(); ++i)
    if (wire.trace()[i].realtime) realtimeBeforeFx = false;
  std::vector<std::uint8_t> noteBytes;
  for (const TxByte& byte : wire.trace()) {
    if (!byte.realtime) noteBytes.push_back(byte.byte);
  }
  const bool order = realtimeOrder.size() == 4 && realtimeOrder[0] == 0xF8 &&
                     realtimeOrder[1] == 0xFA && realtimeOrder[2] == 0xFC &&
                     realtimeOrder[3] == 0xFB;
  const bool framed = noteBytes.size() == 3 && noteBytes[0] == 0x90 &&
                      noteBytes[1] == 72 && noteBytes[2] == 100;
  const bool pass = order && framed && realtimeBeforeFx;
  detail = "F8 FA FC FB at the FX grid point: realtime order " +
           std::string(order ? "kept" : "BROKEN") + ", FX message " +
           std::string(framed ? "intact around realtime" : "BROKEN");
  return pass;
}

// ---------------------------------------------------------------------------
// 2. Full load matrix: one core model, all traffic classes.
// ---------------------------------------------------------------------------
struct LoadRow {
  std::uint16_t bpmX100 = 12000;
  int output = 1;
  std::uint64_t pulses = 0;
  std::uint64_t expectedPulses = 0;
  double tempoErrorPct = 0.0;
  double clockMaxMs = 0.0;
  std::uint64_t fxMessages = 0;
  double fxMaxMs = 0.0;
  double fxMinMs = 0.0;
  std::uint64_t droppedF8 = 0;
  std::uint64_t droppedFx = 0;
  bool pass = false;
};

struct LoadRun {
  LoadRow rows[2];
};

constexpr std::uint32_t kFxStepTicks = 12;  // one clocked FX step per 8th note
constexpr std::uint32_t kFxGateTicks = 4;

LoadRun runLoadRow(std::uint16_t bpmX100) {
  LoadRun run;
  Scheduler scheduler(0, us(1) / 5, us(40), 0x5eed0000u + bpmX100);
  AlarmModel alarm(scheduler, us(20));
  FirmwareWire out[2] = {FirmwareWire(scheduler), FirmwareWire(scheduler)};
  const ClockRate rate = ClockRate::forTempo(bpmX100);
  const Ps epoch = seconds(1);
  const Ps end = epoch + seconds(7200);
  const Ps serviceLag = us(40);  // main-loop service after the tick alarm
  std::uint64_t tick = 0;
  std::uint64_t fxStep = 0;

  struct FxMark {
    int output = 0;
    Ps deadline = 0;
    std::size_t hint = 0;
    std::uint8_t status = 0;
    std::uint8_t note = 0;
  };
  std::vector<FxMark> marks1;
  std::vector<FxMark> marks2;

  alarm.scheduleAfter(epoch, [&](Ps) -> Ps {
    const Ps tickDeadline = rate.deadlineAt(epoch, tick);
    const Ps generation = scheduler.now() + serviceLag;
    // Clock path: F8 on the realtime class, then the grid-time FX pump. The
    // pump only sees grid time, so it cannot promote an event before this F8.
    out[0].realtime(scheduler.now(), 0xF8);
    out[1].realtime(scheduler.now(), 0xF8);
    // Clocked FX traffic (arp/repeater-class): a note-on stamped with this
    // tick's deadline and its gate note-off stamped with a future deadline,
    // both generated in the main loop one service lag after the alarm.
    if (tick % kFxStepTicks == 0) {
      const std::uint8_t note = static_cast<std::uint8_t>(72 + (fxStep % 12));
      const std::uint8_t on[3] = {0x90, note, 100};
      const std::uint8_t off[3] = {0x80, note, 0};
      const Ps offDeadline =
          rate.deadlineAt(epoch, tick + static_cast<std::uint64_t>(kFxGateTicks));
      for (int port = 0; port < 2; ++port) {
        std::size_t hint = out[port].traceSize();
        out[port].timed(generation, tickDeadline, on, 3);
        std::vector<FxMark>& marks = port == 0 ? marks1 : marks2;
        marks.push_back(FxMark{port, tickDeadline, hint, 0x90, note});
        hint = out[port].traceSize();
        out[port].timed(generation, offDeadline, off, 3);
        marks.push_back(FxMark{port, offDeadline, hint, 0x80, note});
      }
      ++fxStep;
    }
    // Grid-time pump after the tick byte and after the main loop's FX service.
    out[0].release(generation, tickDeadline);
    out[1].release(generation, tickDeadline);
    ++tick;
    return -rate.periodAt(0);
  });

  // Ordinary beat traffic (note on/off + CC on both outputs), the same load as
  // the existing acceptance matrix.
  const Ps beat = rate.periodAt(0) * kTicksPerQuarter;
  for (Ps at = epoch; at < end; at += beat) {
    scheduler.scheduleAt(at, [&] {
      const std::uint8_t noteOn[3] = {0x90, 60, 100};
      const std::uint8_t cc[3] = {0xB0, 1, 64};
      const std::uint8_t noteOff[3] = {0x80, 60, 0};
      const Ps now = scheduler.now();
      for (int port = 0; port < 2; ++port) {
        out[port].message(now, noteOn, 3);
        out[port].message(now, cc, 3);
        out[port].message(now, noteOff, 3);
      }
    });
  }

  // SPI churn: a 32-byte transaction every 50 ms with one short abort in eight,
  // exactly the existing matrix's load, plus a host-side app message.
  SpiLinkModel spi(0x1234u);
  std::uint64_t spiIndex = 0;
  for (Ps at = 0; at < end; at += ms(50)) {
    scheduler.scheduleAt(at, [&spi, at, end, spiIndex] {
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
      if ((spiIndex % 8) == 7 && at + ms(50) < end)
        spi.transferShort(mosi, miso, 17, at);
      else
        spi.transfer(mosi, miso);
    });
    ++spiIndex;
  }

  scheduler.runUntil(end);
  out[0].flushTo(end);
  out[1].flushTo(end);

  const std::uint64_t expectedPulses =
      static_cast<std::uint64_t>((end - epoch) / rate.periodAt(0)) + 1;
  for (int index = 0; index < 2; ++index) {
    LoadRow& row = run.rows[index];
    row.bpmX100 = bpmX100;
    row.output = index + 1;
    row.expectedPulses = expectedPulses;
    row.droppedF8 = out[index].scheduler().droppedRealtime();
    row.droppedFx = out[index].scheduler().droppedTimed() +
                    out[index].scheduler().rejectedTimed();
    std::vector<Ps> clock;
    collectClock(out[index].trace(), clock, row.droppedF8);
    row.pulses = clock.size();
    if (clock.size() >= 2) {
      const double secondsActual =
          static_cast<double>(clock.back() - clock.front()) / 9'000'000'000.0;
      const double bpmActual = secondsActual > 0.0
                                   ? static_cast<double>(clock.size() - 1) * 60.0 /
                                         (secondsActual * kTicksPerQuarter)
                                   : 0.0;
      row.tempoErrorPct = std::abs(bpmActual - rate.bpm()) / rate.bpm() * 100.0;
    }
    for (std::size_t k = 0; k < clock.size(); ++k) {
      const double errorMs =
          toMs(clock[k] - rate.deadlineAt(epoch, static_cast<std::uint64_t>(k)));
      row.clockMaxMs = std::max(row.clockMaxMs, std::abs(errorMs));
    }
    row.fxMinMs = 1e9;
    const std::vector<FxMark>& marks = index == 0 ? marks1 : marks2;
    for (const FxMark& mark : marks) {
      Ps start = 0;
      if (!findMessageStart(out[index].trace(), mark.hint, mark.status, mark.note, start))
        continue;
      ++row.fxMessages;
      const double errorMs = toMs(start - mark.deadline);
      row.fxMaxMs = std::max(row.fxMaxMs, errorMs);
      row.fxMinMs = std::min(row.fxMinMs, errorMs);
    }
    if (row.fxMessages == 0) row.fxMinMs = 0.0;
    // Budget: <= 1 ms clock error, <= 0.1 % tempo error, no dropped F8. FX
    // parity: never early; the structural ceiling is the clock's own alarm
    // phase plus the scheduler's byte-boundary budget for a normal message -
    // the tick byte plus the remaining bytes of the one message that was
    // already on the wire (MIDI framing cannot be cut mid-message).
    const double fxBandMs = row.clockMaxMs + toMs(3 * kMidiByteTime) + 0.05;
    row.pass = row.pulses == expectedPulses && row.droppedF8 == 0 &&
               row.droppedFx == 0 && row.tempoErrorPct <= 0.1 &&
               row.clockMaxMs <= 1.0 && row.fxMessages != 0 &&
               row.fxMinMs >= -0.001 && row.fxMaxMs <= fxBandMs;
  }
  return run;
}

std::string makeLoadTable(const std::vector<LoadRow>& rows) {
  std::string table;
  table +=
      "Full-load matrix, ONE CORE model: 24-PPQN clock + note/CC + SPI churn "
      "(1 abort in 8) + clocked FX traffic\n";
  table +=
      "tempo  out  F8_pulses  tempo_err%  clock_max_ms  FX_msgs  fx_max_ms  "
      "fx_min_ms  F8_drop  FX_drop  verdict\n";
  char line[256];
  for (const LoadRow& row : rows) {
    std::snprintf(line, sizeof line,
                  "%5.1f  %d    %9llu  %9.4f  %11.4f  %7llu  %9.4f  %9.4f  "
                  "%7llu  %7llu  %s\n",
                  row.bpmX100 / 100.0, row.output,
                  static_cast<unsigned long long>(row.pulses), row.tempoErrorPct,
                  row.clockMaxMs, static_cast<unsigned long long>(row.fxMessages),
                  row.fxMaxMs, row.fxMinMs,
                  static_cast<unsigned long long>(row.droppedF8),
                  static_cast<unsigned long long>(row.droppedFx),
                  row.pass ? "PASS" : "FAIL");
    table += line;
  }
  return table;
}

}  // namespace

int main() {
  int failures = 0;
  auto check = [&failures](const char* name, bool pass, const std::string& detail) {
    std::printf("%s %s: %s\n", pass ? "PASS" : "FAIL", name, detail.c_str());
    if (!pass) ++failures;
  };

  std::string detail;
  check("fx.timestamped-emission", runEmissionStage(detail), detail);
  detail.clear();
  check("fx.realtime-precedence", runRealtimePrecedenceStage(detail), detail);

  const std::uint16_t tempos[] = {2000, 6000, 12000, 18000, 30000};
  std::vector<LoadRow> rows;
  bool matrixPass = true;
  for (std::uint16_t bpmX100 : tempos) {
    const LoadRun run = runLoadRow(bpmX100);
    for (const LoadRow& row : run.rows) {
      rows.push_back(row);
      matrixPass = matrixPass && row.pass;
    }
  }
  std::printf("\n%s\n", makeLoadTable(rows).c_str());
  check("matrix.single-core-full-load", matrixPass,
        "2 h x 20/60/120/180/300 bpm x both outputs under note/CC/SPI/FX load");

  if (failures == 0) {
    std::printf("PASS: clocked FX timestamped release + single-core full-load matrix\n");
    return 0;
  }
  std::printf("FAIL: %d check(s) failed\n", failures);
  return 1;
}
