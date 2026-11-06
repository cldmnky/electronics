#pragma once
// Non-visual timeline logic for the EDIT screen: beat/window math, region
// selection, the non-destructive take-region operations and the `.peaks`
// overview reduction used by the waveform thumbnails. Qt-free so
// `mnkybox --unittests` can exercise every operation without a window.
//
// The region type is the frozen session v2 `takes.edit` entry
// (pi/session/session.hpp TakeRegion, landed in 43130e8); the audio-side mirror
// is pi/audio/take_player.hpp TakeRegion. Files are never rewritten here:
// every op edits the region list, and RENDER is the explicit destructive op the
// core API does not provide yet (see kRenderMissingApi).
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
// The destructive render op has no core call yet: take_player.hpp documents
// that render-to-new-take is "a later op that is not part of this API". The UI
// names the missing call instead of faking a render.
inline constexpr const char* kRenderMissingApi =
    "audio::TakePlayer::renderRegionsToTake()";

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
// Ops the UI offers. RENDER (op 7) has no core call, so the region-ops grid
// and the panel ring stop before it instead of offering a button that can only
// be refused (A14).
inline constexpr int kTimelineUiOpCount = 7;

const char* timelineOpName(TimelineOp op);
// RENDER is refused until the core render call exists; the ops popup no longer
// offers it, and this guard keeps a stale id from writing anything.
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

}  // namespace mnky::app::timeline
