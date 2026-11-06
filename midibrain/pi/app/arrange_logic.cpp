#include "arrange_logic.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace mnky::app::arrange {
namespace {

double clampFinite(double value, double fallback) {
  return std::isfinite(value) ? value : fallback;
}

int clampGrid(int grid) {
  if (grid < kGridBar) return kGridBar;
  if (grid >= kGridCount) return kGridSixteenth;
  return grid;
}

double clampStart(double beats) {
  if (!std::isfinite(beats) || beats < 0.0) return 0.0;
  return beats;
}

std::string trimNumber(double value) {
  char buffer[48] = {};
  std::snprintf(buffer, sizeof buffer, "%.2f", value);
  std::string text = buffer;
  while (!text.empty() && text.back() == '0') text.pop_back();
  if (!text.empty() && text.back() == '.') text.pop_back();
  if (text == "-0") text = "0";
  return text;
}

}  // namespace

std::vector<SectionSpan> sectionSpans(
    const std::vector<session::SongSection>& sections) {
  std::vector<SectionSpan> spans;
  spans.reserve(sections.size());
  double start = 0.0;
  for (std::size_t index = 0; index < sections.size(); ++index) {
    const int bars = std::max(1, sections[index].bars);
    SectionSpan span;
    span.index = static_cast<int>(index);
    span.startBeats = start;
    span.endBeats = start + static_cast<double>(bars) * kBeatsPerBar;
    span.bars = bars;
    spans.push_back(span);
    start = span.endBeats;
  }
  return spans;
}

double sectionsEndBeats(const std::vector<session::SongSection>& sections) {
  double end = 0.0;
  for (const auto& section : sections) {
    end += static_cast<double>(std::max(1, section.bars)) * kBeatsPerBar;
  }
  return end;
}

int sectionAtBeat(const std::vector<session::SongSection>& sections, double beats) {
  const auto spans = sectionSpans(sections);
  for (const auto& span : spans) {
    if (beats >= span.startBeats && beats < span.endBeats) return span.index;
  }
  return spans.empty() ? -1 : spans.back().index;
}

double sectionStartBeats(const std::vector<session::SongSection>& sections,
                         int index) {
  const auto spans = sectionSpans(sections);
  if (index < 0 || index >= static_cast<int>(spans.size())) return 0.0;
  return spans[static_cast<std::size_t>(index)].startBeats;
}

double gridStepBeats(int grid) {
  switch (clampGrid(grid)) {
    case kGridHalf:
      return kBeatsPerBar / 2.0;
    case kGridQuarter:
      return kBeatsPerBar / 4.0;
    case kGridSixteenth:
      return kBeatsPerBar / 16.0;
    case kGridBar:
    default:
      return kBeatsPerBar;
  }
}

double snapBeats(double beats, int grid) {
  const double step = gridStepBeats(grid);
  if (!(step > 0.0) || !std::isfinite(beats)) return 0.0;
  const double snapped = std::round(beats / step) * step;
  return snapped < 0.0 ? 0.0 : snapped;
}

double snapLength(double beats, int grid) {
  const double step = gridStepBeats(grid);
  if (!(step > 0.0) || !std::isfinite(beats) || beats < step) return step;
  return std::max(step, std::round(beats / step) * step);
}

std::string gridText(int grid) {
  switch (clampGrid(grid)) {
    case kGridHalf:
      return "1/2";
    case kGridQuarter:
      return "1/4";
    case kGridSixteenth:
      return "1/16";
    case kGridBar:
    default:
      return "BAR";
  }
}

double clipEndBeats(const session::SongClip& clip, const TakeLengthFn& takeLength) {
  const double start = clampStart(clip.startBeats);
  const double take = takeLength ? clampFinite(takeLength(clip), 0.0) : 0.0;
  const double offset = std::max(0.0, clampFinite(clip.offsetBeats, 0.0));
  const double remaining = std::max(0.0, take - offset);
  double length = clampFinite(clip.lengthBeats, 0.0);
  if (length <= 0.0) length = remaining;
  length = std::min(length, remaining > 0.0 ? remaining : length);
  if (!(length > 0.0)) length = 0.0;
  return start + length;
}

double clipLengthBeats(const session::SongClip& clip,
                       const TakeLengthFn& takeLength) {
  return std::max(0.0, clipEndBeats(clip, takeLength) - clampStart(clip.startBeats));
}

double songEndBeats(const session::SongState& song, const TakeLengthFn& takeLength) {
  double end = sectionsEndBeats(song.sections);
  for (const auto& clip : song.clips) {
    end = std::max(end, clipEndBeats(clip, takeLength));
  }
  return end;
}

double effectiveLengthBeats(const session::SongState& song, double lengthBeats,
                            const TakeLengthFn& takeLength) {
  const double explicitLength = clampFinite(lengthBeats, 0.0);
  if (explicitLength > 0.0) return explicitLength;
  const double derived = songEndBeats(song, takeLength);
  return derived > 0.0 ? derived : kBeatsPerBar;
}

double loopEndBeats(double loopStartBeats, double loopEndBeats,
                    double songLengthBeats) {
  const double start = std::min(clampStart(loopStartBeats), songLengthBeats);
  const double end = clampFinite(loopEndBeats, 0.0);
  if (end <= start) return songLengthBeats;
  return std::min(end, songLengthBeats);
}

bool loopIsWholeSong(double loopStartBeats, double endBeats,
                     double songLengthBeats) {
  return clampStart(loopStartBeats) <= 0.0 &&
         loopEndBeats(loopStartBeats, endBeats, songLengthBeats) >=
             songLengthBeats;
}

std::string lengthText(double beats) {
  const double value = std::max(0.0, clampFinite(beats, 0.0));
  const double bars = std::floor(value / kBeatsPerBar);
  const double rest = value - bars * kBeatsPerBar;
  return trimNumber(bars) + ":" + trimNumber(rest);
}

std::string positionBarsText(double beats) {
  const double bars = std::max(0.0, clampFinite(beats, 0.0)) / kBeatsPerBar;
  return trimNumber(bars);
}

bool clipOverlaps(const std::vector<session::SongClip>& clips, std::size_t index,
                  const session::SongClip& candidate, const TakeLengthFn& takeLength) {
  const double start = clampStart(candidate.startBeats);
  const double end = clipEndBeats(candidate, takeLength);
  for (std::size_t current = 0; current < clips.size(); ++current) {
    if (current == index) continue;
    const auto& other = clips[current];
    if (other.track != candidate.track) continue;
    const double otherStart = clampStart(other.startBeats);
    const double otherEnd = clipEndBeats(other, takeLength);
    if (start < otherEnd && otherStart < end) return true;
  }
  return false;
}

int clipAtBeat(const std::vector<session::SongClip>& clips, int track, double beats,
               const TakeLengthFn& takeLength) {
  for (std::size_t index = 0; index < clips.size(); ++index) {
    const auto& clip = clips[index];
    if (clip.track != track) continue;
    if (beats >= clampStart(clip.startBeats) &&
        beats < clipEndBeats(clip, takeLength)) {
      return static_cast<int>(index);
    }
  }
  return -1;
}

int ensureFirstSection(session::SongState& song) {
  if (!song.sections.empty()) return 0;
  song.sections.push_back(session::SongSection{"INTRO", kDefaultSectionBars});
  return 0;
}

int addSection(session::SongState& song, const std::string& name, int bars) {
  session::SongSection section;
  section.name = name.empty() ? std::string("SECTION") : name;
  section.bars = std::max(1, bars);
  song.sections.push_back(section);
  return static_cast<int>(song.sections.size()) - 1;
}

int duplicateSection(session::SongState& song, int index,
                     const TakeLengthFn& takeLength) {
  const auto spans = sectionSpans(song.sections);
  if (index < 0 || index >= static_cast<int>(spans.size())) return -1;
  const auto span = spans[static_cast<std::size_t>(index)];
  const double length = span.endBeats - span.startBeats;
  if (!(length > 0.0)) return -1;

  std::vector<session::SongClip> copies;
  for (const auto& clip : song.clips) {
    const double start = clampStart(clip.startBeats);
    if (start >= span.startBeats && start < span.endBeats) {
      copies.push_back(clip);
    }
  }
  // Make room first: the new section pushes every later section and clip by its
  // length, so the direct copy lands in the new (empty) span.
  for (auto& clip : song.clips) {
    if (clampStart(clip.startBeats) >= span.endBeats) {
      clip.startBeats = clampStart(clip.startBeats) + length;
    }
  }
  session::SongSection duplicate = song.sections[static_cast<std::size_t>(index)];
  duplicate.bars = std::max(1, duplicate.bars);
  song.sections.insert(song.sections.begin() + index + 1, duplicate);
  for (auto& copy : copies) {
    copy.startBeats = clampStart(copy.startBeats) + length;
    song.clips.push_back(copy);
  }
  (void)takeLength;
  return index + 1;
}

bool renameSection(session::SongState& song, int index, const std::string& name) {
  if (index < 0 || index >= static_cast<int>(song.sections.size())) return false;
  if (name.empty() || name.size() > session::kMaxSectionNameBytes) return false;
  for (const char character : name) {
    const auto value = static_cast<unsigned char>(character);
    if (value < 0x20 || value > 0x7e) return false;
  }
  song.sections[static_cast<std::size_t>(index)].name = name;
  return true;
}

bool addClip(session::SongState& song, const session::SongClip& clip,
             const TakeLengthFn& takeLength, std::string* error) {
  if (clip.take.empty()) {
    if (error != nullptr) *error = "the clip names no take";
    return false;
  }
  session::SongClip placed = clip;
  placed.startBeats = clampStart(placed.startBeats);
  if (clipOverlaps(song.clips, song.clips.size(), placed, takeLength)) {
    if (error != nullptr) *error = "clips never overlap on one track";
    return false;
  }
  song.clips.push_back(std::move(placed));
  return true;
}

bool moveClip(session::SongState& song, int index, double newStartBeats,
              const TakeLengthFn& takeLength, std::string* error) {
  if (index < 0 || index >= static_cast<int>(song.clips.size())) {
    if (error != nullptr) *error = "the clip is gone";
    return false;
  }
  session::SongClip candidate = song.clips[static_cast<std::size_t>(index)];
  candidate.startBeats = clampStart(newStartBeats);
  if (clipOverlaps(song.clips, static_cast<std::size_t>(index), candidate,
                   takeLength)) {
    if (error != nullptr) *error = "clips never overlap on one track";
    return false;
  }
  song.clips[static_cast<std::size_t>(index)] = candidate;
  return true;
}

bool trimClipStart(session::SongState& song, int index, double newStartBeats,
                   const TakeLengthFn& takeLength, std::string* error) {
  if (index < 0 || index >= static_cast<int>(song.clips.size())) {
    if (error != nullptr) *error = "the clip is gone";
    return false;
  }
  session::SongClip candidate = song.clips[static_cast<std::size_t>(index)];
  const double oldStart = clampStart(candidate.startBeats);
  const double oldEnd = clipEndBeats(candidate, takeLength);
  const double take = takeLength ? std::max(0.0, takeLength(candidate)) : 0.0;
  const double offset = std::max(0.0, candidate.offsetBeats);
  const double earliest = std::max(0.0, oldStart - offset);
  double start = clampStart(newStartBeats);
  if (start < earliest) start = earliest;
  if (start >= oldEnd) {
    if (error != nullptr) *error = "the clip needs at least one grid step";
    return false;
  }
  candidate.startBeats = start;
  candidate.offsetBeats = std::max(0.0, offset + (start - oldStart));
  candidate.lengthBeats = oldEnd - start;
  if (!clipOverlaps(song.clips, static_cast<std::size_t>(index), candidate,
                    takeLength)) {
    song.clips[static_cast<std::size_t>(index)] = candidate;
    return true;
  }
  if (error != nullptr) *error = "clips never overlap on one track";
  (void)take;
  return false;
}

bool trimClipEnd(session::SongState& song, int index, double newEndBeats,
                 const TakeLengthFn& takeLength, std::string* error) {
  if (index < 0 || index >= static_cast<int>(song.clips.size())) {
    if (error != nullptr) *error = "the clip is gone";
    return false;
  }
  session::SongClip candidate = song.clips[static_cast<std::size_t>(index)];
  const double start = clampStart(candidate.startBeats);
  double end = clampFinite(newEndBeats, start);
  if (end <= start) {
    if (error != nullptr) *error = "the clip needs at least one grid step";
    return false;
  }
  const double take = takeLength ? std::max(0.0, takeLength(candidate)) : 0.0;
  const double offset = std::max(0.0, candidate.offsetBeats);
  if (take > 0.0) end = std::min(end, start + std::max(0.0, take - offset));
  if (end <= start) {
    if (error != nullptr) *error = "the take ends before the clip";
    return false;
  }
  candidate.lengthBeats = end - start;
  if (!clipOverlaps(song.clips, static_cast<std::size_t>(index), candidate,
                    takeLength)) {
    song.clips[static_cast<std::size_t>(index)] = candidate;
    return true;
  }
  if (error != nullptr) *error = "clips never overlap on one track";
  return false;
}

int duplicateClip(session::SongState& song, int index, int grid,
                  const TakeLengthFn& takeLength, std::string* error) {
  if (index < 0 || index >= static_cast<int>(song.clips.size())) {
    if (error != nullptr) *error = "select a clip first";
    return -1;
  }
  const auto source = song.clips[static_cast<std::size_t>(index)];
  const double length = std::max(gridStepBeats(grid),
                                 clipLengthBeats(source, takeLength));
  double start = clipEndBeats(source, takeLength);
  // Walk forward until the copy fits; a duplicate never overlaps.
  for (int attempt = 0; attempt < 256; ++attempt) {
    session::SongClip candidate = source;
    candidate.startBeats = start;
    if (!clipOverlaps(song.clips, song.clips.size(), candidate, takeLength)) {
      song.clips.push_back(std::move(candidate));
      return static_cast<int>(song.clips.size()) - 1;
    }
    double next = start + gridStepBeats(grid);
    for (const auto& clip : song.clips) {
      if (clip.track != source.track) continue;
      const double otherStart = clampStart(clip.startBeats);
      if (otherStart >= start && otherStart < next) {
        next = std::max(next, clipEndBeats(clip, takeLength));
      }
    }
    if (next <= start) break;
    start = next;
  }
  (void)length;
  if (error != nullptr) *error = "no free space on this track";
  return -1;
}

bool deleteClip(session::SongState& song, int index) {
  if (index < 0 || index >= static_cast<int>(song.clips.size())) return false;
  song.clips.erase(song.clips.begin() + index);
  return true;
}

std::vector<std::size_t> clipsInSection(const session::SongState& song, int index) {
  const auto spans = sectionSpans(song.sections);
  std::vector<std::size_t> result;
  if (index < 0 || index >= static_cast<int>(spans.size())) return result;
  const auto span = spans[static_cast<std::size_t>(index)];
  for (std::size_t current = 0; current < song.clips.size(); ++current) {
    const double start = clampStart(song.clips[current].startBeats);
    if (start >= span.startBeats && start < span.endBeats) {
      result.push_back(current);
    }
  }
  return result;
}

double punchStartBeats(int punchInMeasure, double loopStartBeats) {
  const int measure = punchInMeasure < 1 ? 1 : punchInMeasure;
  return clampStart(loopStartBeats) +
         static_cast<double>(measure - 1) * kBeatsPerBar;
}

int barOfBeat(double beats) {
  return static_cast<int>(std::floor(std::max(0.0, clampFinite(beats, 0.0)) /
                                     kBeatsPerBar)) +
         1;
}

}  // namespace mnky::app::arrange
