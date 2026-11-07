#include "bench_clock.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QTextStream>
#include <QTimer>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include "pico_stat.hpp"

namespace mnky::bench {
namespace {

constexpr int kStepMs = 50;
constexpr std::uint64_t kSettleUs = 2'000'000;  // let the PLL/grid settle
constexpr int kLoadEverySteps = 5;              // note/CC burst every 250 ms
constexpr int kChurnEverySteps = 40;            // UI tab churn every 2 s

std::uint64_t monoUs() {
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(now).count());
}

std::string isoNow() {
  return QDateTime::currentDateTime().toString(Qt::ISODateWithMs).toStdString();
}

int openSerial(const char* path) {
  const int fd = ::open(path, O_RDONLY | O_NOCTTY | O_NONBLOCK);
  if (fd < 0) return -1;
  struct termios tio {};
  if (::tcgetattr(fd, &tio) != 0) {
    ::close(fd);
    return -1;
  }
  ::cfmakeraw(&tio);
  ::cfsetispeed(&tio, B115200);
  ::cfsetospeed(&tio, B115200);
  tio.c_cc[VMIN] = 0;
  tio.c_cc[VTIME] = 0;
  if (::tcsetattr(fd, TCSANOW, &tio) != 0) {
    ::close(fd);
    return -1;
  }
  return fd;
}

}  // namespace

BenchClock::BenchClock(Hooks hooks, Profile profile, QObject* parent)
    : QObject(parent), hooks_(std::move(hooks)), profile_(profile) {
  timer_ = new QTimer(this);
  timer_->setInterval(kStepMs);
  connect(timer_, &QTimer::timeout, this, &BenchClock::step);
}

BenchClock::~BenchClock() {
  if (consoleFd_ >= 0) ::close(consoleFd_);
}

bool BenchClock::parseProfile(const QString& text, Profile& out) {
  const QString lower = text.trimmed().toLower();
  if (lower == QStringLiteral("quick")) {
    out = Profile::Quick;
    return true;
  }
  if (lower == QStringLiteral("full")) {
    out = Profile::Full;
    return true;
  }
  return false;
}

int BenchClock::durationSeconds(Profile profile) {
  return profile == Profile::Quick ? kQuickSeconds : kFullSeconds;
}

QString BenchClock::profileName(Profile profile) {
  return profile == Profile::Quick ? QStringLiteral("quick") : QStringLiteral("full");
}

void BenchClock::start() {
  if (running_) return;
  openConsole();
  running_ = true;
  std::printf("[bench] MARK iso=%s mono_us=%llu event=run.start profile=%s "
              "duration_s_per_tempo=%d tempos=20,60,120,180,300\n",
              isoNow().c_str(), static_cast<unsigned long long>(monoUs()),
              qPrintable(profileName(profile_)), durationSeconds(profile_));
  std::fflush(stdout);
  if (hooks_.log) {
    hooks_.log("[bench] MARK event=run.start profile=" +
               profileName(profile_).toStdString() + " duration_s_per_tempo=" +
               std::to_string(durationSeconds(profile_)));
  }
  tempoIndex_ = -1;
  step_ = 0;
  beginTempo();
  timer_->start();
}

void BenchClock::openConsole() {
  const char* configured = std::getenv("MNKYBOX_PICO_CONSOLE");
  if (configured != nullptr && configured[0] != '\0') {
    consoleFd_ = openSerial(configured);
    if (consoleFd_ < 0) {
      std::fprintf(stderr, "[bench] pico console %s not readable: %s\n", configured,
                   std::strerror(errno));
    }
    return;
  }
  for (int index = 0; index < 4 && consoleFd_ < 0; ++index) {
    const std::string path = "/dev/ttyACM" + std::to_string(index);
    consoleFd_ = openSerial(path.c_str());
  }
  if (consoleFd_ < 0) {
    std::printf("[bench] pico console: no /dev/ttyACM* readable; [stat] columns stay "
                "empty (set MNKYBOX_PICO_CONSOLE to the console device)\n");
  }
}

void BenchClock::pollConsole() {
  if (consoleFd_ < 0) return;
  char buffer[512];
  for (;;) {
    const ssize_t got = ::read(consoleFd_, buffer, sizeof buffer);
    if (got <= 0) break;
    consoleBuffer_.append(buffer, static_cast<std::size_t>(got));
  }
  std::size_t newline = 0;
  while ((newline = consoleBuffer_.find('\n')) != std::string::npos) {
    const std::string line = consoleBuffer_.substr(0, newline);
    consoleBuffer_.erase(0, newline + 1);
    PicoStat parsed;
    if (parsePicoStatLine(line, parsed)) {
      consoleStat_ = parsed;
      consoleStatValid_ = true;
      if (hooks_.statLine) {
        // Compact subset for the DIAGNOSTICS page: the raw line is ~300
        // characters and would wrap past the block. The full line stays in
        // the bench log via the delta printer.
        char compact[192];
        std::snprintf(compact, sizeof compact,
                      "[stat] spiok=%llu abort=%llu bad=%llu garb=%llu ack=%llu "
                      "ov=%llu miss=%llu fxdrop=%llu",
                      static_cast<unsigned long long>(parsed.spiok),
                      static_cast<unsigned long long>(parsed.spiabort),
                      static_cast<unsigned long long>(parsed.spibad),
                      static_cast<unsigned long long>(parsed.spigarb),
                      static_cast<unsigned long long>(parsed.spiak),
                      static_cast<unsigned long long>(parsed.spiov),
                      static_cast<unsigned long long>(parsed.miss),
                      static_cast<unsigned long long>(parsed.fxdrop));
        hooks_.statLine(QString::fromLatin1(compact));
      }
    }
  }
  if (consoleBuffer_.size() > 8192) consoleBuffer_.clear();  // no stat line in 8 KiB
}

bool BenchClock::currentPicoStat(PicoStat& out) const {
  if (!consoleStatValid_) return false;
  out = consoleStat_;
  return true;
}

LinkSnapshot BenchClock::currentSnapshot() {
  last_ = hooks_.snapshot ? hooks_.snapshot() : LinkSnapshot{};
  return last_;
}

void BenchClock::emitMarker(const char* event, const std::string& extra) {
  const std::string line = "[bench] MARK iso=" + isoNow() + " mono_us=" +
                           std::to_string(monoUs()) + " event=" + event + extra;
  std::printf("%s\n", line.c_str());
  std::fflush(stdout);
  if (hooks_.log) hooks_.log(line);
}

void BenchClock::beginTempo() {
  ++tempoIndex_;
  if (tempoIndex_ >= kTempoCount) {
    finishRun();
    return;
  }
  const int bpmX100 = kTempos[tempoIndex_];
  if (hooks_.setTempoX100) hooks_.setTempoX100(bpmX100);
  pollConsole();
  tempoStatBeforeValid_ = currentPicoStat(tempoStatBefore_);
  phaseStartedUs_ = monoUs();
  windowStartedUs_ = 0;
  windowTicks_ = 0;
  windowSamples_ = 0;
  windowMaxLatenessMs_ = 0.0;
  last_ = currentSnapshot();
  char extra[128];
  std::snprintf(extra, sizeof extra, " tempo_index=%d bpm=%.2f duration_s=%d",
                tempoIndex_, bpmX100 / 100.0, durationSeconds(profile_));
  emitMarker("tempo.begin", extra);
}

void BenchClock::step() {
  if (!running_) return;
  ++step_;
  pollConsole();
  const LinkSnapshot snapshot = currentSnapshot();

  // App-side load: note-on/note-off/CC through the TRS output every 250 ms,
  // plus UI tab churn every 2 s. Both go through the app's existing paths.
  if (step_ % kLoadEverySteps == 0 && hooks_.sendMidi) {
    hooks_.sendMidi(0, 0x90, 60, 100);
    hooks_.sendMidi(0, 0x80, 60, 0);
    hooks_.sendMidi(0, 0xB0, 1, 64);
  }
  if (step_ % kChurnEverySteps == 0 && hooks_.churn) {
    hooks_.churn(step_ / kChurnEverySteps);
  }

  const std::uint64_t now = monoUs();
  const std::uint64_t durationUs =
      static_cast<std::uint64_t>(durationSeconds(profile_)) * 1'000'000ull;
  const std::uint64_t settle = std::min<std::uint64_t>(kSettleUs, durationUs / 10);
  if (now - phaseStartedUs_ < settle) return;

  if (windowStartedUs_ == 0) {
    windowStartedUs_ = now;
    // The tick counter at the measurement start. `snapshot.tick` is the last
    // tick-frame number the link delivered.
    windowTicks_ = snapshot.tick;
    char extra[160];
    std::snprintf(extra, sizeof extra,
                  " tempo_index=%d bpm=%.2f app_tick=%llu", tempoIndex_,
                  kTempos[tempoIndex_] / 100.0,
                  static_cast<unsigned long long>(snapshot.tick));
    emitMarker("tempo.measure.start", extra);
  }

  ++windowSamples_;
  windowMaxLatenessMs_ = std::max(windowMaxLatenessMs_, std::abs(snapshot.latencyUs) / 1000.0);
  if (now - windowStartedUs_ >= durationUs) finishTempo();
}

void BenchClock::finishTempo() {
  const LinkSnapshot snapshot = currentSnapshot();
  const std::uint64_t elapsedUs = monoUs() - windowStartedUs_;
  const std::uint64_t observedTicks =
      snapshot.tick >= windowTicks_ ? snapshot.tick - windowTicks_ : 0;

  TempoResult result;
  result.bpmX100 = kTempos[tempoIndex_];
  result.ticks = observedTicks;
  result.elapsedS = static_cast<double>(elapsedUs) / 1e6;
  const double observedBpm =
      result.elapsedS > 0.0
          ? static_cast<double>(observedTicks) * 60.0 / (result.elapsedS * 24.0)
          : 0.0;
  const double target = result.bpmX100 / 100.0;
  result.tempoErrorPct = target > 0.0 ? std::abs(observedBpm - target) / target * 100.0 : 100.0;
  result.maxLatenessMs = windowMaxLatenessMs_;
  result.latencySamples = windowSamples_;
  result.droppedTicks =
      snapshot.clockSkipped >= last_.clockSkipped ? snapshot.clockSkipped - last_.clockSkipped : 0;
  result.linkBad = snapshot.bad >= last_.bad ? snapshot.bad - last_.bad : 0;
  result.linkRetries =
      snapshot.retries >= last_.retries ? snapshot.retries - last_.retries : 0;
  result.noteOffDrops = snapshot.noteOffDrops >= last_.noteOffDrops
                            ? snapshot.noteOffDrops - last_.noteOffDrops
                            : 0;
  PicoStat statNow;
  const PicoStat statBefore = tempoStatBefore_;
  // The console line is 1 Hz, so the "before" snapshot is the line that was
  // current when this tempo began (captured in beginTempo). The delta printer
  // uses the field values, so a missing line can never be reported as a
  // healthy zero.
  result.statValid = tempoStatBeforeValid_ && currentPicoStat(statNow);
  if (result.statValid) {
    result.statSpiAbort = statNow.spiabort >= statBefore.spiabort
                              ? statNow.spiabort - statBefore.spiabort
                              : 0;
    result.statSpiBad = statNow.spibad >= statBefore.spibad ? statNow.spibad - statBefore.spibad : 0;
    result.statSpiGarb =
        statNow.spigarb >= statBefore.spigarb ? statNow.spigarb - statBefore.spigarb : 0;
    result.statSpiOk = statNow.spiok >= statBefore.spiok ? statNow.spiok - statBefore.spiok : 0;
    result.statSpiOv = statNow.spiov >= statBefore.spiov ? statNow.spiov - statBefore.spiov : 0;
    result.statMiss = statNow.miss >= statBefore.miss ? statNow.miss - statBefore.miss : 0;
    result.statMtxRt = statNow.mtxrt >= statBefore.mtxrt ? statNow.mtxrt - statBefore.mtxrt : 0;
    result.statFxDrop = statNow.fxdrop >= statBefore.fxdrop ? statNow.fxdrop - statBefore.fxdrop : 0;
    std::printf("[bench] stat.delta%s\n", formatPicoStatDelta(statBefore, statNow).c_str());
    std::fflush(stdout);
  }
  result.pass = result.tempoErrorPct <= 0.1 && result.droppedTicks == 0 &&
                result.noteOffDrops == 0 && result.latencySamples > 0;
  results_.push_back(result);

  char extra[256];
  std::snprintf(extra, sizeof extra,
                " tempo_index=%d bpm=%.2f ticks=%llu tempo_err_pct=%.4f "
                "max_arrival_late_ms=%.3f skipped=%llu noteoff_drops=%llu",
                tempoIndex_, target, static_cast<unsigned long long>(result.ticks),
                result.tempoErrorPct, result.maxLatenessMs,
                static_cast<unsigned long long>(result.droppedTicks),
                static_cast<unsigned long long>(result.noteOffDrops));
  emitMarker("tempo.end", extra);
  beginTempo();
}

void BenchClock::finishRun() {
  if (finishing_) return;
  finishing_ = true;
  running_ = false;
  timer_->stop();
  if (consoleFd_ >= 0) {
    ::close(consoleFd_);
    consoleFd_ = -1;
  }
  const QString table = resultsTable();
  std::printf("%s\n", qPrintable(table));
  std::fflush(stdout);
  if (hooks_.log) hooks_.log("[bench] MARK event=run.end\n" + table.toStdString());
  emitMarker("run.end", " rows=" + std::to_string(results_.size()));
  emit finished(0);
}

QString BenchClock::resultsTable() const {
  QString table;
  QTextStream out(&table);
  out << "MIDI-clock acceptance matrix (BENCH, real device, profile "
      << profileName(profile_) << ", " << durationSeconds(profile_)
      << " s per tempo; app-observed columns; TRS edge columns come from the "
         "logic-analyser capture)\n";
  out << "tempo  out  pulses   tempo_err%  max_err_ms  F8_drop  notes    cc      "
         "spi_tx  spi_abort  recovered  verdict\n";
  for (const TempoResult& row : results_) {
    // Two rows per tempo so the table lines up 1:1 with the clock_sim matrix
    // (out 1/2). Both TRS outputs are gated on in the default route and the
    // app observes one tick frame per generated tick, not per output, so the
    // app-observed values are the same until the analyser fills the edges.
    const std::uint64_t notes = static_cast<std::uint64_t>(
        std::llround(row.elapsedS / (kStepMs / 1000.0 / kLoadEverySteps))) * 2;
    const std::uint64_t ccs = notes / 2;
    for (int output = 1; output <= 2; ++output) {
      out << QString::asprintf(
                 "%5.1f  %d    %7llu  %9.4f  %10.3f  %7llu  %7llu  %6llu  %6llu  "
                 "%9llu  %9llu  %s\n",
                 row.bpmX100 / 100.0, output,
                 static_cast<unsigned long long>(row.ticks), row.tempoErrorPct,
                 row.maxLatenessMs, static_cast<unsigned long long>(row.droppedTicks),
                 static_cast<unsigned long long>(notes),
                 static_cast<unsigned long long>(ccs),
                 static_cast<unsigned long long>(row.linkBad),
                 static_cast<unsigned long long>(row.statSpiAbort),
                 static_cast<unsigned long long>(row.linkRetries),
                 row.pass ? "PASS" : "FAIL");
    }
  }
  out << "note: max_err_ms here is app-side tick-frame arrival lateness "
         "(Pico stamp vs Pi monotonic), not the TRS edge; spi_tx/spi_abort are "
         "the Pi link deltas, recovered is link retries. Firmware [stat]: "
      << (results_.empty() || !results_.front().statValid ? "not captured"
                                                          : "captured per tempo");
  return table;
}

}  // namespace mnky::bench
