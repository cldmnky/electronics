#pragma once
// Four-band EQ grid model (Qt-free): per-subject band values, the log-frequency
// / gain grid mapping and the summed response curve.
//
// The band values live in one audio::fx::Eq per subject (18 tracks + Main), so
// the frequency/gain/Q ranges, the band type names and the response curve come
// from the shipped effect instead of a second copy of the DSP constants. The
// engine has no EQ insert yet (audio::Engine has two send buses only), so these
// instances are edited and project-persisted but never processed in the audio
// path; see NOTES.md, "EQ grid".
#include <array>
#include <string>
#include <vector>

#include "../audio/fx/eq.hpp"
#include "../session/session.hpp"

namespace mnky::app {

class EqModel {
 public:
  static constexpr int kBandCount = audio::fx::Eq::kBandCount;  // 4
  static constexpr int kTrackCount = audio::kMaxInputs;         // 18
  static constexpr int kMainSubject = kTrackCount;              // 18 = Main
  static constexpr int kSubjectCount = kTrackCount + 1;
  static constexpr int kTypeCount = 6;  // None, LCut, LShelf, Param, HShelf, HCut

  // Display grid: 20 Hz..20 kHz on a log x axis, +/-24 dB on y (the effect's
  // own gain range). The curve is sampled at kCurvePoints log-spaced
  // frequencies across the same span so it lines up with the node x mapping.
  static constexpr double kMinFreqHz = 20.0;
  static constexpr double kMaxFreqHz = 20000.0;
  static constexpr double kMinGainDb = -24.0;
  static constexpr double kMaxGainDb = 24.0;
  static constexpr double kMinQ = 0.1;
  static constexpr double kMaxQ = 10.0;
  static constexpr int kCurvePoints = 49;
  static constexpr int kCurveImpulseFrames = 1024;
  static constexpr double kCurveSampleRate = 48000.0;

  // Panel EDIT steps (USER-MANUAL section 9: left/right = frequency with SHIFT
  // coarse, up/down = gain, Q on SHIFT+up/down, encoder = the focused value).
  static constexpr double kFreqStepOctaves = 1.0 / 12.0;
  static constexpr double kFreqStepOctavesCoarse = 0.5;
  static constexpr double kGainStepDb = 0.5;
  static constexpr double kQStep = 0.05;
  static constexpr double kQStepFine = 0.005;

  EqModel();

  int subject() const { return subject_; }
  void setSubject(int subject);
  bool isMainSubject() const { return subject_ == kMainSubject; }
  // Track 0..17 then Main (kMainSubject), wrapping at both ends.
  void stepSubject(int delta);
  static std::string subjectName(int subject);

  int band() const;
  void setBand(int band);
  void stepBand(int delta);

  int type(int index) const;
  double freqHz(int index) const;
  double gainDb(int index) const;
  double q(int index) const;
  void setType(int index, int type);
  void cycleType(int index, int delta);
  void setFreqHz(int index, double hz);
  void setGainDb(int index, double db);
  void setQ(int index, double q);

  // Low cut, parametric and high cut? `typeHasGain` is false for None and both
  // cut types, which is why their corners and popups never show a dB value.
  static bool typeHasGain(int type);
  // Short ASCII names used on the corner chips: None/LCut/LShelf/Param/HShelf/
  // HCut. The long names come from audio::fx::Eq::bandTypeName().
  static std::string typeShortName(int type);
  static const char* typeLongName(int type);

  // Grid mapping. x runs 0 (20 Hz) .. 1 (20 kHz) on a log scale; y runs 0
  // (-24 dB, bottom) .. 1 (+24 dB, top). Non-finite input maps to the middle.
  static double freqToX(double hz);
  static double xToFreq(double x);
  static double gainToY(double db);
  static double yToGain(double y);
  double bandX(int index) const { return freqToX(freqHz(index)); }
  double bandY(int index) const { return gainToY(gainDb(index)); }

  // Nearest band within `radius` (normalized grid distance), or -1 when no
  // node is that close. Bands with type None are skipped: they draw a dimmed
  // marker but there is no filter to bend until the band gets a type. A touch
  // drag uses this to decide which node was grabbed.
  int nodeAt(double x, double y, double radius = 0.16) const;

  // Touch drag: normalized grid position -> frequency + gain of `index`. Cut
  // and None bands ignore the vertical axis (they have no gain).
  void dragBand(int index, double x, double y);
  // Panel EDIT: one step. `dx`/`dy` are -1/0/+1; `coarse` is the SHIFT step.
  // dx is frequency, dy is gain (-1 = down) unless the band is a cut, where
  // dy is Q per the manual.
  void nudgeBand(int index, int dx, int dy, bool coarse);
  // Panel encoder in EDIT: frequency in octaves (coarse = SHIFT).
  void adjustFreqOctaves(int index, double octaves);
  void adjustGainDb(int index, double delta);
  void adjustQ(int index, double delta);

  // Summed magnitude response in dB at kCurvePoints log-spaced frequencies,
  // measured from the selected subject's effect impulse response. Rebuilt on
  // the next call after any band write. Const because it is a pure cache fill:
  // the effect instance is reset and replayed, never left in a new state.
  const std::vector<float>& curve() const;

  std::string freqText(int index) const;
  std::string gainText(int index) const;
  std::string qText(int index) const;

  session::EqState capture() const;
  // Applies a loaded session EQ. The session/engine contract allows Q up to
  // 24, but the UI and the CLAP effect cap it at kMaxQ (10). Returns how many
  // loaded bands were above that cap and were normalized to kMaxQ, so the
  // loader can log one explicit note instead of silently changing the Q.
  int apply(const session::EqState& state);

 private:
  audio::fx::Eq& effect() const;
  void rebuildCurve() const;

  // Mutable only for the curve cache: curve() replays this instance's impulse
  // response, which the audio path never touches (no EQ insert yet).
  mutable std::array<audio::fx::Eq, kSubjectCount> effects_;
  int subject_ = kMainSubject;
  mutable std::vector<float> curve_;
  mutable bool curveDirty_ = true;
};

}  // namespace mnky::app
