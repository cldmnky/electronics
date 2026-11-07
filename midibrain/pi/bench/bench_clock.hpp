#pragma once
// --bench-clock: automated MIDI-clock acceptance matrix against the real device.
//
// The logic-analyser session needs a repeatable driver, not a human turning the
// encoder. This class runs the review's matrix (20/60/120/180/300 bpm, both TRS
// outputs on the wire) over the real app + Pico: it sets the tempo through the
// same LinkClient path the UI uses, generates note/CC traffic on the TRS output,
// churns the UI, and samples the app-side clock observation (tick frames and
// their arrival lateness) plus the firmware `[stat]` counters before/after each
// tempo. Every tempo change prints a timestamped MARK line on stdout (and the
// app log) so the analyser capture can be aligned with the run.
//
// The class lives in pi/bench and takes hooks; pi/app only constructs it and
// supplies existing accessors. There is no QML control and no persisted write.
#include <QObject>
#include <QString>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "pico_stat.hpp"

class QTimer;

namespace mnky::bench {

// What the app already observes through LinkClient::status(): clock tick count,
// the Pico-stamp arrival statistics and the link reliability counters. No new
// protocol field, no direct PicoLink access from here.
struct LinkSnapshot {
  bool linkUp = false;
  bool rolling = false;
  std::uint64_t tick = 0;
  std::uint16_t tempoX100 = 0;
  double offsetUs = 0.0;
  double latencyUs = 0.0;
  std::uint64_t clockSkipped = 0;
  std::uint64_t clockReordered = 0;
  std::uint64_t tx = 0;
  std::uint64_t rx = 0;
  std::uint64_t bad = 0;
  std::uint64_t dup = 0;
  std::uint64_t overruns = 0;
  std::uint64_t retries = 0;
  std::uint64_t acks = 0;
  std::uint64_t noteOffDrops = 0;
  std::uint64_t evicted = 0;
  std::uint64_t txov = 0;
  std::uint64_t rxov = 0;
};

struct TempoResult {
  int bpmX100 = 0;
  std::uint64_t ticks = 0;       // app-observed tick frames during the window
  double elapsedS = 0.0;
  double tempoErrorPct = 0.0;    // |observed - target| / target
  double maxLatenessMs = 0.0;    // app-side arrival lateness, not the TRS edge
  std::uint64_t latencySamples = 0;
  std::uint64_t droppedTicks = 0;  // clockSkipped delta
  std::uint64_t linkBad = 0;
  std::uint64_t linkRetries = 0;
  std::uint64_t noteOffDrops = 0;
  std::uint64_t statSpiAbort = 0;
  std::uint64_t statSpiBad = 0;
  std::uint64_t statSpiGarb = 0;
  std::uint64_t statSpiOk = 0;
  std::uint64_t statSpiOv = 0;
  std::uint64_t statMiss = 0;
  std::uint64_t statMtxRt = 0;
  std::uint64_t statFxDrop = 0;
  bool statValid = false;
  bool pass = false;  // app-side target: <=0.1 % tempo, 0 dropped, 0 Note Off drops
};

class BenchClock final : public QObject {
  Q_OBJECT

 public:
  enum class Profile { Quick, Full };

  struct Hooks {
    // AppController::benchSetTempoX100 (same link_.requestTempo the UI uses).
    std::function<void(int bpmX100)> setTempoX100;
    // AppController::benchLinkSnapshot.
    std::function<LinkSnapshot()> snapshot;
    // Emits one note-on / note-off / CC pair on TRS OUT 1 (the Pico TX load).
    std::function<void(int port, int status, int data1, int data2)> sendMidi;
    // Tabs + System refresh: UI load without opening a popup or writing a file.
    std::function<void(int step)> churn;
    // Mirror of a marker line into the app log (stdout is printed here).
    std::function<void(const std::string&)> log;
    // Latest firmware [stat] line for the DIAGNOSTICS page (bench mode only).
    std::function<void(const QString&)> statLine;
  };

  static constexpr int kTempos[] = {2000, 6000, 12000, 18000, 30000};
  static constexpr int kTempoCount = 5;
  // 180 s at 20 bpm is 1440 ticks: a one-tick boundary is 0.07 %, below the
  // 0.1 % acceptance target, so the quick profile cannot false-fail on
  // quantisation. It still finishes in ~15 min for all five tempos.
  static constexpr int kQuickSeconds = 180;
  static constexpr int kFullSeconds = 2 * 60 * 60;

  BenchClock(Hooks hooks, Profile profile, QObject* parent = nullptr);
  ~BenchClock() override;

  // "quick" or "full". Case-insensitive.
  static bool parseProfile(const QString& text, Profile& out);
  static int durationSeconds(Profile profile);
  static QString profileName(Profile profile);

  void start();
  bool running() const { return running_; }
  // Full results table after the run (also printed by finishRun()).
  QString resultsTable() const;

 signals:
  void finished(int exitCode);

 private:
  void step();
  void beginTempo();
  void finishTempo();
  void finishRun();
  void emitMarker(const char* event, const std::string& extra);
  void openConsole();
  void pollConsole();
  LinkSnapshot currentSnapshot();
  bool currentPicoStat(PicoStat& out) const;

  Hooks hooks_;
  Profile profile_;
  QTimer* timer_ = nullptr;
  bool running_ = false;
  bool finishing_ = false;
  int tempoIndex_ = -1;
  int step_ = 0;
  std::uint64_t phaseStartedUs_ = 0;
  std::uint64_t windowStartedUs_ = 0;
  std::uint64_t windowTicks_ = 0;
  std::uint64_t windowSamples_ = 0;
  double windowMaxLatenessMs_ = 0.0;
  LinkSnapshot last_;
  PicoStat consoleStat_{};
  bool consoleStatValid_ = false;
  PicoStat tempoStatBefore_{};
  bool tempoStatBeforeValid_ = false;
  int consoleFd_ = -1;
  std::string consoleBuffer_;
  std::vector<TempoResult> results_;
};

}  // namespace mnky::bench
