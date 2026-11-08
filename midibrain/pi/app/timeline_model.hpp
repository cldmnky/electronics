#pragma once
// Non-visual timeline logic for the EDIT screen: beat/window math, region
// selection, the non-destructive take-region operations and the `.peaks`
// overview reduction used by the waveform thumbnails. Qt-free so
// `mnkybox --unittests` can exercise every operation without a window.
//
// The region type is the frozen session v2 `takes.edit` entry
// (pi/session/session.hpp TakeRegion, landed in 43130e8); the audio-side mirror
// is pi/audio/take_player.hpp TakeRegion. Files are never rewritten here: every
// region op edits the region list. The one destructive path, RENDER REGION TO
// TRACK, bakes IN..OUT into a new take and is a controller action
// (Engine::renderRegionToTake + the take -> track flow), not a region op.
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "../audio/peaks.hpp"
#include "../session/session.hpp"

namespace mnky::app::timeline {

inline constexpr int kTracksPerPage = 6;
inline constexpr double kBeatsPerBar = 4.0;
// Panel and touch edit steps. The edge/knob grid is 0.25 beat; SHIFT is one bar
// (coarse), matching the bars:beats readout in the value popup.
inline constexpr double kStepFineBeats = 0.25;
inline constexpr double kStepCoarseBeats = 1.0;
inline constexpr double kCursorCoarseBeats = 4.0;
inline constexpr double kMinViewBeats = 8.0;
inline constexpr double kMaxViewBeats = 64.0;
// EDIT popup zoom floor: the popup edits one take, so it zooms below the
// whole-session floor down to a quarter beat across the screen.
inline constexpr double kEditMinViewBeats = 0.25;
// RENDER is not a region op: it writes a new take through
// Engine::renderRegionToTake (AppController::renderRegionToTrack), so the id
// stays in the enum for the session/IDL shape and the UI never offers it.

enum class TimelineOp : std::uint8_t {
  Trim = 0,
  Split = 1,
  Gain = 2,
  Fades = 3,
  Normalize = 4,
  Delete = 5,
  Clear = 6,
  Render = 7,
};

inline constexpr int kTimelineOpCount = 8;
// Ops the ConfigPopup region-ops grid offers; RENDER (op 7) is a controller
// action (RENDER REGION TO TRACK), so the ring stops before it (A14).
inline constexpr int kTimelineUiOpCount = 7;

// The EDIT popup's ops strip: TRIM, SPLIT, GAIN, FADE IN,
// FADE OUT, NORMALIZE, DELETE REGION, UNDO - in that order. The two fade cards
// both run TimelineOp::Fades, zeroing the other fade; UNDO is an action on the
// undo stack, not a region op.
enum class EditOp : std::uint8_t {
  Trim = 0,
  Split = 1,
  Gain = 2,
  FadeIn = 3,
  FadeOut = 4,
  Normalize = 5,
  DeleteRegion = 6,
  Undo = 7,
};

inline constexpr int kEditOpCount = 8;
// Cards that run a region op (everything except UNDO).
inline constexpr int kEditOpActionCount = 7;

const char* editOpName(EditOp op);
// GAIN and the two fade cards carry a value knob; the rest are verbs.
bool editOpHasValue(EditOp op);
// TRIM and DELETE REGION rewrite IN..OUT, so they need a real selection.
bool editOpNeedsRange(EditOp op);
// The region op one strip card runs, with its drafted values. False for UNDO.
struct EditOpRequest {
  TimelineOp op = TimelineOp::Trim;
  float gainDb = 0.0f;
  double fadeInBeats = 0.0;
  double fadeOutBeats = 0.0;
};
bool editOpRequest(EditOp op, float gainDb, double fadeInBeats,
                   double fadeOutBeats, EditOpRequest* out);

const char* timelineOpName(TimelineOp op);
// RENDER is refused until the core render call exists; the ops popup does not
// offer it, and this guard keeps a stale id from writing anything.
bool timelineOpAvailable(TimelineOp op);

using TakeLengthFn = std::function<double(const std::string& take)>;

struct BeatRange {
  bool active = false;
  double in = 0.0;
  double out = 0.0;

  double start() const { return in <= out ? in : out; }
  double end() const { return in <= out ? out : in; }
  double span() const { return end() - start(); }
  friend bool operator==(const BeatRange&, const BeatRange&) = default;
};

double clampBeat(double beats);
double takeLengthBeats(double seconds, int bpmX100);
double measureBeat(int measure);
// "bar:beat:tick" for a beat position (24 ticks per beat, 4/4), 1-based.
std::string beatText(double beats);
double regionEndBeats(const session::TakeRegion& region, const TakeLengthFn& length);
// Clamps a region to its take's length: a zero length means "to the end of the
// take" (session contract) and is kept as zero when the range reaches the end.
session::TakeRegion clampRegion(const session::TakeRegion& region,
                                const TakeLengthFn& length);
bool regionOverlaps(const session::TakeRegion& region, const BeatRange& range,
                    const TakeLengthFn& length);

// Ops over one track's ordered region list. `range.active` false means "all
// regions". TRIM requires an active range and clips the whole list to it;
// DELETE removes every region that overlaps the range; SPLIT cuts every region
// containing `splitBeat`. Gain adds dB (clamped -96..+12); fades are set on the
// targeted regions and clamped to the region length; normalize sets the flag.
std::vector<session::TakeRegion> trimRegions(
    const std::vector<session::TakeRegion>& regions, const BeatRange& range,
    const TakeLengthFn& length);
std::vector<session::TakeRegion> splitRegionsAt(
    const std::vector<session::TakeRegion>& regions, double splitBeat,
    const TakeLengthFn& length);
std::vector<session::TakeRegion> gainRegions(
    const std::vector<session::TakeRegion>& regions, const BeatRange& range,
    float gainDb, const TakeLengthFn& length);
std::vector<session::TakeRegion> fadeRegions(
    const std::vector<session::TakeRegion>& regions, const BeatRange& range,
    double fadeInBeats, double fadeOutBeats, const TakeLengthFn& length);
std::vector<session::TakeRegion> normalizeRegions(
    const std::vector<session::TakeRegion>& regions, const BeatRange& range,
    const TakeLengthFn& length);
std::vector<session::TakeRegion> deleteRegions(
    const std::vector<session::TakeRegion>& regions, const BeatRange& range,
    const TakeLengthFn& length);

// Reduces a `.peaks` overview to `columns` peak magnitudes (0..1) between two
// take-relative beat positions. Empty when the summary has no buckets.
std::vector<float> reducePeaks(const audio::PeakSummary& summary,
                               double startBeats, double endBeats,
                               double takeBeats, int columns);

// Same window, but keeps the polarity: two entries per column
// (`2 * columns` floats, lo then hi, both 0..1 magnitudes) so the EDIT popup
// can draw an asymmetric mirrored envelope plus its peak-hold line. The fine
// 256-frame buckets are used while a bucket is at least two columns wide
// (half a column per bucket or coarser); past that the folded coarse level is
// used, so a zoomed-out view never walks every fine bucket in the paint path.
std::vector<float> reducePeakPairs(const audio::PeakSummary& summary,
                                   double startBeats, double endBeats,
                                   double takeBeats, int columns);

// "bar:beat:tick" without the zero-padded bar, for the EDIT popup's footer
// (`IN 1:2:00 OUT 2:2:00`).
std::string beatTextCompact(double beats);

}  // namespace mnky::app::timeline
