#include "timeline_model.hpp"

#include <algorithm>
#include <cmath>

#include "logic.hpp"

namespace mnky::app::timeline {
namespace {

constexpr float kMinRegionGainDb = -96.0f;
constexpr float kMaxRegionGainDb = 12.0f;

double availableLength(const session::TakeRegion& region,
                       const TakeLengthFn& length) {
  if (region.lengthBeats > 0.0) return region.lengthBeats;
  const double take = length ? length(region.take) : 0.0;
  return std::max(0.0, take - region.offsetBeats);
}

bool contains(const session::TakeRegion& region, double beat,
              const TakeLengthFn& length) {
  const double end = regionEndBeats(region, length);
  return beat > region.offsetBeats && beat < end;
}

}  // namespace

const char* timelineOpName(TimelineOp op) {
  switch (op) {
    case TimelineOp::Trim:
      return "TRIM";
    case TimelineOp::Split:
      return "SPLIT";
    case TimelineOp::Gain:
      return "GAIN";
    case TimelineOp::Fades:
      return "FADES";
    case TimelineOp::Normalize:
      return "NORMALIZE";
    case TimelineOp::Delete:
      return "DELETE";
    case TimelineOp::Clear:
      return "CLEAR";
    case TimelineOp::Render:
      return "RENDER";
  }
  return "?";
}

bool timelineOpAvailable(TimelineOp op) { return op != TimelineOp::Render; }

const char* editOpName(EditOp op) {
  switch (op) {
    case EditOp::Trim:
      return "TRIM";
    case EditOp::Split:
      return "SPLIT";
    case EditOp::Gain:
      return "GAIN";
    case EditOp::FadeIn:
      return "FADE IN";
    case EditOp::FadeOut:
      return "FADE OUT";
    case EditOp::Normalize:
      return "NORMALIZE";
    case EditOp::DeleteRegion:
      return "DELETE REGION";
    case EditOp::Undo:
      return "UNDO";
  }
  return "?";
}

bool editOpHasValue(EditOp op) {
  return op == EditOp::Gain || op == EditOp::FadeIn || op == EditOp::FadeOut;
}

bool editOpNeedsRange(EditOp op) {
  return op == EditOp::Trim || op == EditOp::DeleteRegion;
}

bool editOpRequest(EditOp op, float gainDb, double fadeInBeats,
                   double fadeOutBeats, EditOpRequest* out) {
  if (out == nullptr) return false;
  EditOpRequest request;
  switch (op) {
    case EditOp::Undo:
      return false;
    case EditOp::Trim:
      request.op = TimelineOp::Trim;
      break;
    case EditOp::Split:
      request.op = TimelineOp::Split;
      break;
    case EditOp::Gain:
      request.op = TimelineOp::Gain;
      request.gainDb = gainDb;
      break;
    case EditOp::FadeIn:
      request.op = TimelineOp::Fades;
      request.fadeInBeats = fadeInBeats;
      break;
    case EditOp::FadeOut:
      request.op = TimelineOp::Fades;
      request.fadeOutBeats = fadeOutBeats;
      break;
    case EditOp::Normalize:
      request.op = TimelineOp::Normalize;
      break;
    case EditOp::DeleteRegion:
      request.op = TimelineOp::Delete;
      break;
  }
  *out = request;
  return true;
}

double clampBeat(double beats) {
  if (!std::isfinite(beats) || beats < 0.0) return 0.0;
  return std::min(beats, 100000.0);
}

double takeLengthBeats(double seconds, int bpmX100) {
  const double bpm = clampTempoX100(bpmX100) / 100.0;
  if (!(seconds > 0.0) || !(bpm > 0.0)) return 0.0;
  return seconds * bpm / 60.0;
}

double measureBeat(int measure) {
  return static_cast<double>(std::max(1, measure) - 1) * kBeatsPerBar;
}

std::string beatText(double beats) {
  const double clamped = clampBeat(beats);
  const auto tick = static_cast<std::uint32_t>(
      std::llround(clamped * static_cast<double>(kTicksPerBeat)));
  return formatBarBeatTick(tick);
}

std::string beatTextCompact(double beats) {
  const double clamped = clampBeat(beats);
  const auto tick = static_cast<std::uint32_t>(
      std::llround(clamped * static_cast<double>(kTicksPerBeat)));
  const std::uint32_t bar = tick / kTicksPerBar + 1;
  const std::uint32_t beat = (tick % kTicksPerBar) / kTicksPerBeat + 1;
  const std::uint32_t sub = tick % kTicksPerBeat;
  char buffer[32] = {};
  std::snprintf(buffer, sizeof buffer, "%u:%u:%02u", bar, beat, sub);
  return buffer;
}

double regionEndBeats(const session::TakeRegion& region,
                      const TakeLengthFn& length) {
  if (region.lengthBeats > 0.0) return region.offsetBeats + region.lengthBeats;
  const double take = length ? length(region.take) : 0.0;
  return std::max(region.offsetBeats, take);
}

session::TakeRegion clampRegion(const session::TakeRegion& region,
                                const TakeLengthFn& length) {
  session::TakeRegion out = region;
  out.offsetBeats = clampBeat(out.offsetBeats);
  out.gainDb = std::clamp(out.gainDb, kMinRegionGainDb, kMaxRegionGainDb);
  const double take = length ? length(out.take) : 0.0;
  if (out.lengthBeats > 0.0) {
    if (take > 0.0) {
      out.lengthBeats = std::min(out.lengthBeats, std::max(0.0, take - out.offsetBeats));
    }
  } else if (take > 0.0 && out.offsetBeats >= take) {
    // A to-end region starting past the take would otherwise be open-ended.
    out.lengthBeats = 0.0;
  }
  const double available = availableLength(out, length);
  out.fadeInBeats = std::clamp(out.fadeInBeats, 0.0, available);
  out.fadeOutBeats = std::clamp(out.fadeOutBeats, 0.0, available);
  return out;
}

bool regionOverlaps(const session::TakeRegion& region, const BeatRange& range,
                    const TakeLengthFn& length) {
  if (!range.active) return true;
  const double start = range.start();
  const double end = range.end();
  if (end <= start) return false;
  const double regionStart = region.offsetBeats;
  const double regionEnd = regionEndBeats(region, length);
  return regionStart < end && regionEnd > start;
}

std::vector<session::TakeRegion> trimRegions(
    const std::vector<session::TakeRegion>& regions, const BeatRange& range,
    const TakeLengthFn& length) {
  std::vector<session::TakeRegion> out;
  if (!range.active) return regions;
  const double start = range.start();
  const double end = range.end();
  if (end <= start) return out;
  for (const auto& region : regions) {
    const double regionStart = region.offsetBeats;
    const double regionEnd = regionEndBeats(region, length);
    const double newStart = std::max(regionStart, start);
    const double newEnd = std::min(regionEnd, end);
    if (newEnd <= newStart) continue;
    session::TakeRegion clipped = region;
    clipped.offsetBeats = newStart;
    const bool originalToEnd = region.lengthBeats <= 0.0;
    const double take = length ? length(region.take) : 0.0;
    if (originalToEnd && take > 0.0 && std::abs(newEnd - take) < 1e-9) {
      clipped.lengthBeats = 0.0;  // keep the to-end marker
    } else {
      clipped.lengthBeats = newEnd - newStart;
    }
    clipped.fadeInBeats = std::clamp(clipped.fadeInBeats, 0.0, newEnd - newStart);
    clipped.fadeOutBeats = std::clamp(clipped.fadeOutBeats, 0.0, newEnd - newStart);
    out.push_back(clipped);
  }
  return out;
}

std::vector<session::TakeRegion> splitRegionsAt(
    const std::vector<session::TakeRegion>& regions, double splitBeat,
    const TakeLengthFn& length) {
  std::vector<session::TakeRegion> out;
  out.reserve(regions.size() + 1);
  for (const auto& region : regions) {
    if (!contains(region, splitBeat, length)) {
      out.push_back(region);
      continue;
    }
    const double end = regionEndBeats(region, length);
    session::TakeRegion first = region;
    first.lengthBeats = splitBeat - region.offsetBeats;
    first.fadeOutBeats = 0.0;
    session::TakeRegion second = region;
    second.offsetBeats = splitBeat;
    if (region.lengthBeats > 0.0) {
      second.lengthBeats = end - splitBeat;
    } else {
      second.lengthBeats = 0.0;  // still "to the end of the take"
    }
    second.fadeInBeats = 0.0;
    out.push_back(clampRegion(first, length));
    out.push_back(clampRegion(second, length));
  }
  return out;
}

std::vector<session::TakeRegion> gainRegions(
    const std::vector<session::TakeRegion>& regions, const BeatRange& range,
    float gainDb, const TakeLengthFn& length) {
  std::vector<session::TakeRegion> out = regions;
  for (auto& region : out) {
    if (!regionOverlaps(region, range, length)) continue;
    // GAIN sets the region's gain (absolute, matching the value card and the
    // live preview), it does not accumulate: applying GAIN -6 twice is a
    // no-change, and every distinct apply is its own undo level.
    region.gainDb = std::clamp(gainDb, kMinRegionGainDb, kMaxRegionGainDb);
  }
  return out;
}

std::vector<session::TakeRegion> fadeRegions(
    const std::vector<session::TakeRegion>& regions, const BeatRange& range,
    double fadeInBeats, double fadeOutBeats, const TakeLengthFn& length) {
  std::vector<session::TakeRegion> out = regions;
  const double fadeIn = std::max(0.0, fadeInBeats);
  const double fadeOut = std::max(0.0, fadeOutBeats);
  for (auto& region : out) {
    if (!regionOverlaps(region, range, length)) continue;
    const double available = availableLength(region, length);
    region.fadeInBeats = std::min(fadeIn, available);
    region.fadeOutBeats = std::min(fadeOut, available);
  }
  return out;
}

std::vector<session::TakeRegion> normalizeRegions(
    const std::vector<session::TakeRegion>& regions, const BeatRange& range,
    const TakeLengthFn& length) {
  std::vector<session::TakeRegion> out = regions;
  for (auto& region : out) {
    if (!regionOverlaps(region, range, length)) continue;
    region.normalize = true;
  }
  return out;
}

std::vector<session::TakeRegion> deleteRegions(
    const std::vector<session::TakeRegion>& regions, const BeatRange& range,
    const TakeLengthFn& length) {
  std::vector<session::TakeRegion> out;
  out.reserve(regions.size());
  for (const auto& region : regions) {
    if (regionOverlaps(region, range, length)) continue;
    out.push_back(region);
  }
  return out;
}

std::vector<float> reducePeaks(const audio::PeakSummary& summary,
                               double startBeats, double endBeats,
                               double takeBeats, int columns) {
  std::vector<float> out;
  if (columns <= 0 || summary.buckets.empty() || summary.sourceFrames == 0 ||
      !(takeBeats > 0.0)) {
    return out;
  }
  const double start = std::clamp(std::min(startBeats, endBeats), 0.0, takeBeats);
  const double end = std::clamp(std::max(startBeats, endBeats), 0.0, takeBeats);
  if (!(end > start)) return out;
  const double frames = static_cast<double>(summary.sourceFrames);
  const double startFrame = start / takeBeats * frames;
  const double endFrame = end / takeBeats * frames;
  const double span = std::max(1.0, endFrame - startFrame);
  const double bucketFrames =
      static_cast<double>(std::max<std::uint32_t>(1, summary.bucketFrames));
  out.reserve(static_cast<std::size_t>(columns));
  for (int column = 0; column < columns; ++column) {
    const double from = startFrame + span * static_cast<double>(column) /
                                         static_cast<double>(columns);
    const double to = startFrame + span * static_cast<double>(column + 1) /
                                       static_cast<double>(columns);
    const auto first = static_cast<std::size_t>(
        std::clamp<std::uint64_t>(
            static_cast<std::uint64_t>(from / bucketFrames), 0,
            summary.buckets.size() - 1));
    // The column's bucket window is half-open: subtract one frame from the
    // exclusive end so a boundary bucket does not leak into the previous column.
    const double lastFrame = std::max(from, to - 1.0);
    auto last = static_cast<std::size_t>(
        std::clamp<std::uint64_t>(
            static_cast<std::uint64_t>(lastFrame / bucketFrames), 0,
            summary.buckets.size() - 1));
    if (last < first) last = first;
    float peak = 0.0f;
    for (std::size_t bucket = first; bucket <= last; ++bucket) {
      const auto& range = summary.buckets[bucket];
      peak = std::max(peak, std::max(std::abs(range.min), std::abs(range.max)));
    }
    out.push_back(std::clamp(peak, 0.0f, 1.0f));
  }
  return out;
}

std::vector<float> reducePeakPairs(const audio::PeakSummary& summary,
                                   double startBeats, double endBeats,
                                   double takeBeats, int columns) {
  std::vector<float> out;
  if (columns <= 0 || summary.buckets.empty() || summary.sourceFrames == 0 ||
      !(takeBeats > 0.0)) {
    return out;
  }
  const double start = std::clamp(std::min(startBeats, endBeats), 0.0, takeBeats);
  const double end = std::clamp(std::max(startBeats, endBeats), 0.0, takeBeats);
  if (!(end > start)) return out;
  const double frames = static_cast<double>(summary.sourceFrames);
  const double startFrame = start / takeBeats * frames;
  const double endFrame = end / takeBeats * frames;
  const double span = std::max(1.0, endFrame - startFrame);
  const double bucketFrames =
      static_cast<double>(std::max<std::uint32_t>(1, summary.bucketFrames));
  // Level choice: the fine 256-frame buckets stay "one bucket >= 2 columns"
  // down to half a column per bucket; below that the folded coarse level is the
  // source, so a zoomed-out window never touches every fine bucket.
  const bool fineIsWide = span / bucketFrames <= static_cast<double>(columns) * 0.5;
  const bool useCoarse = !fineIsWide && !summary.coarse.empty();
  const auto& level = useCoarse ? summary.coarse : summary.buckets;
  const double levelFrames =
      useCoarse ? bucketFrames * static_cast<double>(
                                    std::max<std::uint32_t>(1, summary.coarseFactor))
                : bucketFrames;
  out.reserve(static_cast<std::size_t>(columns) * 2);
  for (int column = 0; column < columns; ++column) {
    const double from = startFrame + span * static_cast<double>(column) /
                                         static_cast<double>(columns);
    const double to = startFrame + span * static_cast<double>(column + 1) /
                                       static_cast<double>(columns);
    const auto first = static_cast<std::size_t>(
        std::clamp<std::uint64_t>(
            static_cast<std::uint64_t>(from / levelFrames), 0,
            level.size() - 1));
    // Half-open column window: the exclusive end loses one frame so a boundary
    // entry does not leak into the previous column.
    const double lastFrame = std::max(from, to - 1.0);
    auto last = static_cast<std::size_t>(
        std::clamp<std::uint64_t>(
            static_cast<std::uint64_t>(lastFrame / levelFrames), 0,
            level.size() - 1));
    if (last < first) last = first;
    float lo = 0.0f;
    float hi = 0.0f;
    for (std::size_t entry = first; entry <= last; ++entry) {
      const auto& range = level[entry];
      lo = std::max(lo, std::abs(range.min));
      hi = std::max(hi, range.max);
    }
    out.push_back(std::clamp(lo, 0.0f, 1.0f));
    out.push_back(std::clamp(hi, 0.0f, 1.0f));
  }
  return out;
}

}  // namespace mnky::app::timeline
