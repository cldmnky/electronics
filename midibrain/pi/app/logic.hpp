#pragma once
// Non-visual mnkybox app logic: panel control -> app intent mapping,
// mixer/transport value mapping and readout formatting. Deliberately free of
// Qt and JUCE so the same code runs in the app and under `mnkybox --unittests`
// (logic_tests.cpp); the QML layer only presents the state and performs the
// intents decided here.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <string>

#include "../midi/buttons.hpp"
#include "../midi/clock.hpp"

namespace mnky::app {

inline constexpr int kScreenCount = 7;  // Mixer, Routing, FX, Recorder, System,
                                        // Projects, Monitor
inline constexpr int kNumStrips = 18;   // audio::kMaxInputs

// One thing the app does in response to a panel gesture or a screen control.
// Screens post intents; the Qt controller performs them on the GUI thread.
//
// NAVIGATE/EDIT/SHIFT (docs/panel-interaction.md) is expressed with the Nav*,
// Encoder* and Shift kinds: the controller owns the mode/focus ring, while the
// pure mapping here only turns one EV_CONTROL frame into one intent.
struct Intent {
  enum class Kind : std::uint8_t {
    None,
    Play,          // start unless already rolling
    PlayStart,     // restart from tick 0
    Stop,
    RecToggle,     // amount 0 = panel REC (300 ms chord window), 1 = immediate
    OverdubToggle,
    TempoStep,     // amount = bpm x100 delta
    NavUp,         // focus ring up
    NavDown,       // focus ring down
    NavLeft,       // focus ring left
    NavRight,      // focus ring right
    NavPress,      // activate the focused element
    EncoderSw,     // arm the selected strip / confirm an edit
    EncoderTurn,   // amount = signed detent delta (tempo or focused value)
    Shift,         // amount = 1 while CTL_SHIFT_EXIT is held, 0 on release
  };

  Kind kind = Kind::None;
  int amount = 0;
};

inline constexpr bool operator==(const Intent& a, const Intent& b) {
  return a.kind == b.kind && a.amount == b.amount;
}

// Screen/selection context the dispatcher needs. Plain value so the mapping is
// testable without a UI; the controller fills it from its state.
struct PanelState {
  int tab = 0;
  int strip = 0;
};

// Action -> intent table (indexed by midi::Action). Encoder detents are the one
// case whose magnitude lives in the control payload instead of the Action, so
// intentForControl() computes those amounts.
inline constexpr Intent::Kind kActionIntent[] = {
    Intent::Kind::None,           // None
    Intent::Kind::Play,           // Play
    Intent::Kind::PlayStart,      // PlayStart
    Intent::Kind::Stop,           // Stop
    Intent::Kind::RecToggle,      // RecToggle (amount filled by the caller)
    Intent::Kind::OverdubToggle,  // OverdubToggle
    Intent::Kind::TempoStep,      // TempoCoarse
    Intent::Kind::TempoStep,      // TempoFine
    Intent::Kind::NavUp,          // NavUp
    Intent::Kind::NavDown,        // NavDown
    Intent::Kind::NavLeft,        // NavLeft
    Intent::Kind::NavRight,       // NavRight
    Intent::Kind::NavPress,       // NavPress: activate the focused element
    Intent::Kind::EncoderSw,      // EncoderSw: arm the selected strip
};

// One encoder detent changes the tempo by 1.00 bpm, or 0.10 bpm while SHIFT is
// held (the protocol's bpm unit is 0.01 bpm).
inline constexpr int kTempoStepCoarseX100 = 100;
inline constexpr int kTempoStepFineX100 = 10;

inline int clampTempoX100(int bpmX100) {
  if (bpmX100 < midi::UsbClock::kMinBpmX100) return midi::UsbClock::kMinBpmX100;
  if (bpmX100 > midi::UsbClock::kMaxBpmX100) return midi::UsbClock::kMaxBpmX100;
  return bpmX100;
}

// Pure mapping from one midi::Action. Releases never reach here (mapControl
// returns None for them).
inline Intent intentForAction(midi::Action action, const PanelState&) {
  Intent out{};
  const auto index = static_cast<std::size_t>(action);
  if (index >= std::size(kActionIntent)) return out;
  out.kind = kActionIntent[index];
  return out;
}

// Full EV_CONTROL path: CTL_SHIFT_EXIT is a modifier and is handled by the
// caller (it updates the `shift` flag); CTL_ENCODER_TURN carries a signed
// detent delta in `value`, everything else goes through midi::mapControl.
inline Intent intentForControl(std::uint8_t id, std::uint8_t value, bool shift,
                               const PanelState& state) {
  if (id == mb::CTL_ENCODER_TURN) {
    const int detents = static_cast<int>(static_cast<std::int8_t>(value));
    if (detents == 0) return {};
    return Intent{Intent::Kind::EncoderTurn, detents};
  }
  return intentForAction(midi::mapControl(id, value, shift), state);
}

// --- take loop / I/O assignment value mapping -------------------------------
// Take-loop beat fields move on a 0.25-beat grid between 0 and kLoopMaxBeats.
// The whole-take rule (end <= start) is a session::LoopState property, so the
// field values themselves are only clamped to that range.
inline constexpr double kLoopBeatsPerStep = 0.25;
inline constexpr double kLoopMaxBeats = 1024.0;

inline double loopBeats(double beats) {
  if (!std::isfinite(beats) || beats <= 0.0) return 0.0;
  const double snapped =
      std::round(beats / kLoopBeatsPerStep) * kLoopBeatsPerStep;
  if (snapped > kLoopMaxBeats) return kLoopMaxBeats;
  return snapped;
}

// Per-track input source choices: -1 (none) then mono channels 0..17. A stereo
// pair keeps the chosen channel even and uses the next one as the right side.
inline int clampInCh(int channel) {
  if (channel < 0) return -1;
  if (channel >= kNumStrips) return kNumStrips - 1;
  return channel;
}

inline int evenInCh(int channel) {
  const int clamped = clampInCh(channel);
  if (clamped < 0) return clamped;
  return clamped % 2 == 0 ? clamped : clamped - 1;
}

// Per-track output destination: 0 = Main 1&2, 1 = OUT3&4, 2 = phones, 3 = none
// (session::MixerChannelState::outDst).
inline constexpr int kOutDstCount = 4;

inline int clampOutDst(int destination) {
  if (destination < 0) return 0;
  if (destination >= kOutDstCount) return kOutDstCount - 1;
  return destination;
}

// --- mixer / meter value mapping -------------------------------------------
inline constexpr float kMinGainDb = -60.0f;  // strip fader bottom (= silence)
inline constexpr float kMaxGainDb = 12.0f;   // strip fader top
inline constexpr float kMeterFloorDb = -48.0f;

// Fader dB -> linear gain. The floor and NaN both land on exact silence.
inline float dbToGain(float db) {
  if (!(db > kMinGainDb)) return 0.0f;
  return std::pow(10.0f, db / 20.0f);
}

inline float gainToDb(float gain) {
  if (!(gain > 0.0f)) return kMinGainDb;
  return 20.0f * std::log10(gain);
}

// Meter bar fill (0..1) for a linear peak value, floored at kMeterFloorDb.
inline float meterFraction(float peak) {
  const float db = gainToDb(peak);
  if (db <= kMeterFloorDb) return 0.0f;
  const float fraction = (db - kMeterFloorDb) / (0.0f - kMeterFloorDb);
  return fraction > 1.0f ? 1.0f : fraction;
}

// One clip-hold indicator: a meter value at/above full scale latches the clip
// flag for a fixed number of UI ticks (~2 s at 66 ms) so a short overload stays
// visible. Pure and Qt-free so --unittests measures the hold behaviour.
class ClipHold {
 public:
  static constexpr int kHoldTicks = 30;
  static constexpr float kClipLevel = 1.0f;

  // Returns true when this meter value clipped; latches the hold.
  bool update(float peak) {
    if (!(peak >= kClipLevel)) return false;
    remaining_ = kHoldTicks;
    return true;
  }
  // Advances one UI tick and clears an expired hold. Returns clipped().
  bool tick() {
    if (remaining_ > 0) --remaining_;
    return clipped();
  }
  bool clipped() const { return remaining_ > 0; }
  void clear() { remaining_ = 0; }

 private:
  int remaining_ = 0;
};

// Low-space rule for the TOOLS banner: below 2 GiB free or below 5 % of the
// volume is low. A zero capacity (stat failed) is not low.
inline constexpr std::uint64_t kLowSpaceBytes =
    2ull * 1024ull * 1024ull * 1024ull;
inline bool isLowDiskSpace(std::uint64_t available, std::uint64_t capacity) {
  if (capacity == 0) return false;
  if (available < kLowSpaceBytes) return true;
  return static_cast<long double>(available) / static_cast<long double>(capacity) <
         0.05L;
}

// --- readout formatting ----------------------------------------------------
inline std::string formatTempo(int bpmX100) {
  const int bpm = clampTempoX100(bpmX100);
  char buffer[24] = {};
  std::snprintf(buffer, sizeof buffer, "%d.%02d BPM", bpm / 100, bpm % 100);
  return buffer;
}

inline constexpr int kTicksPerBeat = midi::kTicksPerQuarter;  // 24
inline constexpr int kBeatsPerBar = 4;                       // 4/4 display
inline constexpr int kTicksPerBar = kTicksPerBeat * kBeatsPerBar;

// "bar:beat:tick" with 1-based bar/beat and the sub-beat tick count, 4/4.
inline std::string formatBarBeatTick(std::uint32_t tick) {
  const std::uint32_t bar = tick / kTicksPerBar + 1;
  const std::uint32_t beat = (tick % kTicksPerBar) / kTicksPerBeat + 1;
  const std::uint32_t sub = tick % kTicksPerBeat;
  char buffer[32] = {};
  std::snprintf(buffer, sizeof buffer, "%03u:%u:%02u", bar, beat, sub);
  return buffer;
}

// Seconds of musical time for a tick count at a given tempo (display only).
inline double tickSeconds(std::uint32_t tick, int bpmX100) {
  const double bpm = clampTempoX100(bpmX100) / 100.0;
  return static_cast<double>(tick) * 60.0 / (kTicksPerBeat * bpm);
}

// Wraps a selection index; a non-positive count collapses to 0.
inline int moveIndex(int index, int delta, int count) {
  if (count <= 0) return 0;
  int next = (index + delta) % count;
  if (next < 0) next += count;
  return next;
}

inline std::string formatBytes(std::int64_t bytes) {
  static const char* const units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
  double value = bytes > 0 ? static_cast<double>(bytes) : 0.0;
  int unit = 0;
  while (value >= 1024.0 && unit < 4) {
    value /= 1024.0;
    ++unit;
  }
  char buffer[32] = {};
  std::snprintf(buffer, sizeof buffer, unit == 0 ? "%.0f %s" : "%.1f %s", value,
                units[unit]);
  return buffer;
}

inline std::string formatDuration(double seconds) {
  if (!(seconds > 0.0)) seconds = 0.0;
  const auto total = static_cast<std::uint64_t>(seconds + 0.5);
  char buffer[32] = {};
  std::snprintf(buffer, sizeof buffer, "%02llu:%02llu",
                static_cast<unsigned long long>(total / 60),
                static_cast<unsigned long long>(total % 60));
  return buffer;
}

// --- transient value-popup formatting -------------------------------------
// Pan reads as Center / Lxx / Rxx (the motivating case: a bare slider cannot
// show centredness). Gains are dB, sends/levels are percent, ratios and times
// carry their unit, and generic FX parameters keep two decimals.
inline std::string formatPanValue(float pan) {
  const int percent = static_cast<int>(std::lround((pan - 0.5f) * 200.0f));
  if (percent == 0) return "Center";
  char buffer[24] = {};
  std::snprintf(buffer, sizeof buffer, "%c%d", percent < 0 ? 'L' : 'R',
                std::abs(percent));
  return buffer;
}

inline std::string formatDbValue(float db) {
  char buffer[32] = {};
  std::snprintf(buffer, sizeof buffer, "%+.1f dB", static_cast<double>(db));
  return buffer;
}

inline std::string formatPercentValue(float level) {
  const float percent = level * 100.0f;
  char buffer[24] = {};
  std::snprintf(buffer, sizeof buffer, "%.0f %%", static_cast<double>(percent));
  return buffer;
}

inline std::string formatIntValue(double value) {
  char buffer[24] = {};
  std::snprintf(buffer, sizeof buffer, "%.0f", value);
  return buffer;
}

inline std::string formatRatioValue(float ratio) {
  char buffer[24] = {};
  std::snprintf(buffer, sizeof buffer, "%.1f:1", static_cast<double>(ratio));
  return buffer;
}

inline std::string formatMillisecondsValue(float ms) {
  char buffer[24] = {};
  std::snprintf(buffer, sizeof buffer, "%.0f ms", static_cast<double>(ms));
  return buffer;
}

inline std::string formatGenericValue(double value) {
  char buffer[32] = {};
  std::snprintf(buffer, sizeof buffer, "%.2f", value);
  return buffer;
}

}  // namespace mnky::app
