#pragma once
// Qt-free song-arrangement math for the ARRANGE tab (docs/daw-plan.md W6):
// section spans, grid snapping, clip placement/trim/duplicate and the song
// length/loop window. The view model owns the Qt-facing state; this file owns
// the rules so `mnkybox --unittests` can exercise every edit without a window.
//
// The wire fields are the frozen session::SongClip entries. The W4/W5
// addendum adds the settled length/loop semantics ahead of the session seam:
// lengthBeats 0 derives from the last clip end, the loop is on by default and
// an end at or below the start means "to the song length". The app carries the
// not-yet-landed fields in an app-owned project sidecar (see project_io.cpp).
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "../session/session.hpp"

namespace mnky::app::arrange {

inline constexpr double kBeatsPerBar = 4.0;
inline constexpr int kDefaultGrid = 0;  // bar
inline constexpr int kGridBar = 0;
inline constexpr int kGridHalf = 1;
inline constexpr int kGridQuarter = 2;
inline constexpr int kGridSixteenth = 3;
inline constexpr int kGridCount = 4;
inline constexpr double kDefaultSongLengthBeats = 32.0;  // 8 bars at 4/4
inline constexpr int kDefaultSectionBars = 4;

// Resolves a clip's take length in beats. A clip with lengthBeats > 0 already
// owns its explicit end; a zero length asks this function (length 0 = to the
// end of the take, the session contract).
using TakeLengthFn = std::function<double(const session::SongClip&)>;

struct SectionSpan {
  int index = 0;
  double startBeats = 0.0;
  double endBeats = 0.0;
  int bars = 0;
  friend bool operator==(const SectionSpan&, const SectionSpan&) = default;
};

// Sequential spans from the section markers. Bars are clamped to >= 1 by the
// session reader, but this function clamps too so a hand-built state is safe.
std::vector<SectionSpan> sectionSpans(
    const std::vector<session::SongSection>& sections);
double sectionsEndBeats(const std::vector<session::SongSection>& sections);
int sectionAtBeat(const std::vector<session::SongSection>& sections, double beats);
double sectionStartBeats(const std::vector<session::SongSection>& sections,
                         int index);

// 0 = bar, 1 = half, 2 = quarter, 3 = sixteenth.
double gridStepBeats(int grid);
double snapBeats(double beats, int grid);
double snapLength(double beats, int grid);
std::string gridText(int grid);

// Effective clip end in beats. Length 0 means "to the end of the take"; an
// unresolvable take returns the start (a zero-length display block).
double clipEndBeats(const session::SongClip& clip, const TakeLengthFn& takeLength);
double clipLengthBeats(const session::SongClip& clip,
                       const TakeLengthFn& takeLength);
double songEndBeats(const session::SongState& song, const TakeLengthFn& takeLength);
// lengthBeats > 0 wins; 0 derives from the last clip/section end; an empty song
// gets one bar. The value is always >= one bar.
double effectiveLengthBeats(const session::SongState& song, double lengthBeats,
                            const TakeLengthFn& takeLength);
// Loop end: <= start means "to the song length"; the start is clamped to 0 and
// the end to the effective length.
double loopEndBeats(double loopStartBeats, double loopEndBeats,
                    double songLengthBeats);
bool loopIsWholeSong(double loopStartBeats, double loopEndBeats,
                     double songLengthBeats);
std::string lengthText(double beats);
std::string positionBarsText(double beats);

// True when the clip's [start,end) intersects another clip on the same track.
bool clipOverlaps(const std::vector<session::SongClip>& clips, std::size_t index,
                  const session::SongClip& candidate, const TakeLengthFn& takeLength);
// Index of the clip covering `beats` on `track`, or -1.
int clipAtBeat(const std::vector<session::SongClip>& clips, int track, double beats,
               const TakeLengthFn& takeLength);

// Adds a default INTRO section when the song has none; returns its index.
int ensureFirstSection(session::SongState& song);
// Appends a section and returns its index.
int addSection(session::SongState& song, const std::string& name, int bars);
// Duplicates section `index` in place: shifts every later section marker and
// every clip that starts at/after its end by its length, then copies the
// section's clips into the new section. Returns the new section index.
int duplicateSection(session::SongState& song, int index,
                     const TakeLengthFn& takeLength);
// Replaces the selected section's name. Returns false for an empty/out-of-range
// name or index; the caller owns the keypad validation message.
bool renameSection(session::SongState& song, int index, const std::string& name);

// Appends a clip. Clips on one track never overlap: the call refuses an
// overlapping placement. Returns false with `error` when refused.
bool addClip(session::SongState& song, const session::SongClip& clip,
             const TakeLengthFn& takeLength, std::string* error);
// Moves clip `index` to `newStartBeats` (grid-snapped by the caller), refusing
// an overlap. Returns false with `error`; the song is unchanged on refusal.
bool moveClip(session::SongState& song, int index, double newStartBeats,
              const TakeLengthFn& takeLength, std::string* error);
// Trims one edge to an absolute beat. The impossible directions (start past
// end, end before start) refuse. A zero stored length is materialized from the
// take length so a trim never silently changes the other edge.
bool trimClipStart(session::SongState& song, int index, double newStartBeats,
                   const TakeLengthFn& takeLength, std::string* error);
bool trimClipEnd(session::SongState& song, int index, double newEndBeats,
                 const TakeLengthFn& takeLength, std::string* error);
// Duplicates clip `index` at its end (or the next free grid position on the
// track when the direct copy would overlap) and returns the new index, or -1.
int duplicateClip(session::SongState& song, int index, int grid,
                  const TakeLengthFn& takeLength, std::string* error);
// Removes clip `index`; returns false for an out-of-range index.
bool deleteClip(session::SongState& song, int index);
// Clips whose start lies inside section `index`'s span. The vector is the clip
// list order, so a copy preserves the source order.
std::vector<std::size_t> clipsInSection(const session::SongState& song, int index);

// Punch-into-song placement: the written take becomes a clip at the punch in
// point. `punchInMeasure` is 1-based and is interpreted relative to the loop
// start (the W4/W5 addendum); the returned beat is an absolute song position.
double punchStartBeats(int punchInMeasure, double loopStartBeats);
// "clip at bar 9" for the auto-place notice (1-based absolute bar).
int barOfBeat(double beats);

}  // namespace mnky::app::arrange
