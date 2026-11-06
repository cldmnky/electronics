#include "timeline_view_model.hpp"

#include <algorithm>
#include <cmath>
#include <string>

#include <QVariantMap>

#include "logging.hpp"
#include "wav_info.hpp"

namespace mnky::app {
namespace {

QString textOf(const std::string& text) { return QString::fromStdString(text); }

// Templates on the store width so the take-edit array follows SessionSettings
// when its capacity changes; the timeline's visible lanes are paged separately.
template <std::size_t N>
std::string editSignature(
    const std::array<std::vector<session::TakeRegion>, N>& edits) {
  std::string out;
  for (std::size_t track = 0; track < N; ++track) {
    for (const auto& region : edits[static_cast<std::size_t>(track)]) {
      out += region.take;
      out += ':';
      out += std::to_string(region.offsetBeats);
      out += ':';
      out += std::to_string(region.lengthBeats);
      out += ':';
      out += std::to_string(region.gainDb);
      out += ':';
      out += std::to_string(region.fadeInBeats);
      out += ':';
      out += std::to_string(region.fadeOutBeats);
      out += region.normalize ? ":n;" : ":;";
    }
    out += '|';
  }
  return out;
}

QString takeLabel(const std::filesystem::path& path) {
  return path.empty() ? QStringLiteral("LIVE INPUT")
                      : QString::fromStdString(path.filename().string());
}

double clamp(double value, double lo, double hi) {
  return std::max(lo, std::min(hi, value));
}

}  // namespace

TimelineViewModel::TimelineViewModel(AppState& state, AudioGlue& audio,
                                     RecorderViewModel& recorder, LinkClient& link,
                                     SessionSettings& settings, QObject* parent)
    : QObject(parent),
      state_(state),
      audio_(audio),
      recorder_(recorder),
      link_(link),
      settings_(settings) {
  refresh();
}

std::vector<session::TakeRegion>& TimelineViewModel::editList(int track) {
  static std::vector<session::TakeRegion> empty;
  if (track < 0 || track >= audio::kMaxInputs) return empty;
  return settings_.takeEdit[static_cast<std::size_t>(track)];
}

const std::vector<session::TakeRegion>& TimelineViewModel::editList(int track) const {
  static const std::vector<session::TakeRegion> empty;
  if (track < 0 || track >= audio::kMaxInputs) return empty;
  return settings_.takeEdit[static_cast<std::size_t>(track)];
}

std::filesystem::path TimelineViewModel::resolveTake(int track,
                                                     const std::string& key) const {
  return recorder_.takePathForKey(track, key);
}

double TimelineViewModel::takeLengthFor(int track, const std::string& key) const {
  if (key.empty()) return 0.0;
  const auto path = resolveTake(track, key);
  if (path.empty()) return 0.0;
  const std::string id = path.string();
  if (const auto cached = takeBeatsByKey_.find(id); cached != takeBeatsByKey_.end()) {
    return cached->second;
  }
  const auto info = readWavInfo(path);
  const double seconds =
      info.ok && info.sampleRate > 0
          ? static_cast<double>(info.frames) / static_cast<double>(info.sampleRate)
          : 0.0;
  const double beats =
      timeline::takeLengthBeats(seconds, state_.tempoX100.load());
  takeBeatsByKey_[id] = beats;
  return beats;
}

timeline::TakeLengthFn TimelineViewModel::lengthFnFor(int track) const {
  return [this, track](const std::string& key) { return takeLengthFor(track, key); };
}

void TimelineViewModel::refreshSources() {
  for (int track = 0; track < audio::kMaxInputs; ++track) {
    auto& source = sources_[static_cast<std::size_t>(track)];
    const auto key = settings_.activeTake(track);
    if (key != source.key) {
      source = {};
      source.key = key;
      source.path = resolveTake(track, key);
      if (!source.path.empty()) {
        const auto info = readWavInfo(source.path);
        if (info.ok) {
          source.sampleRate = info.sampleRate;
          source.frames = info.frames;
          source.seconds = info.sampleRate > 0
                               ? static_cast<double>(info.frames) /
                                     static_cast<double>(info.sampleRate)
                               : 0.0;
        }
      }
      peaks_[static_cast<std::size_t>(track)] = {};
      takeBeatsByKey_.clear();
    }
    auto& peak = peaks_[static_cast<std::size_t>(track)];
    if (!source.path.empty() && !peak.valid && peak.path != source.path.string()) {
      audio::PeakSummary summary;
      std::string error;
      peak.path = source.path.string();
      if (audio::readPeaks(source.path.string(), summary, &error)) {
        peak.summary = std::move(summary);
        peak.valid = true;
        sidecarCache_[peak.path] = peak.summary;
      } else {
        queueScan(track);
      }
    }
  }
}

void TimelineViewModel::queueScan(int track) {
  if (track < 0 || track >= audio::kMaxInputs) return;
  if (scanningTrack_ == track) return;
  if (std::find(scanQueue_.begin(), scanQueue_.end(), track) != scanQueue_.end()) return;
  scanQueue_.push_back(track);
}

void TimelineViewModel::pumpScanner() {
  if (!scanner_) scanner_ = std::make_unique<audio::PeakScanner>();
  if (!scanner_->busy() && !scanningPath_.empty()) {
    if (scanningTrack_ >= 0 && scanningTrack_ < audio::kMaxInputs) {
      if (const auto* summary = scanner_->summary(); summary != nullptr) {
        auto& peak = peaks_[static_cast<std::size_t>(scanningTrack_)];
        if (peak.path == scanningPath_) {
          peak.summary = *summary;
          peak.valid = true;
          sidecarCache_[peak.path] = peak.summary;
        }
      }
    }
    scanningPath_.clear();
    scanningTrack_ = -1;
  }
  while (!scanner_->busy() && scanningPath_.empty() && !scanQueue_.empty()) {
    const int track = scanQueue_.front();
    scanQueue_.erase(scanQueue_.begin());
    if (track < 0 || track >= audio::kMaxInputs) continue;
    const auto& source = sources_[static_cast<std::size_t>(track)];
    if (source.path.empty()) continue;
    if (peaks_[static_cast<std::size_t>(track)].valid) continue;
    scanningTrack_ = track;
    scanningPath_ = source.path.string();
    scanner_->start(scanningPath_);
  }
}

QString TimelineViewModel::preparePeaksForDemo() {
  refreshSources();
  int scanned = 0;
  for (int track = 0; track < audio::kMaxInputs; ++track) {
    auto& source = sources_[static_cast<std::size_t>(track)];
    if (source.path.empty()) continue;
    auto& peak = peaks_[static_cast<std::size_t>(track)];
    if (peak.valid) continue;
    audio::PeakScanner scanner;
    scanner.start(source.path.string());
    scanner.wait();
    if (const auto* summary = scanner.summary(); summary != nullptr) {
      peak.path = source.path.string();
      peak.summary = *summary;
      peak.valid = true;
      sidecarCache_[peak.path] = peak.summary;
      ++scanned;
    }
  }
  refresh();
  return QStringLiteral("peaks prepared for %1 take(s)").arg(scanned);
}

void TimelineViewModel::applyTrackEdit(int track) {
  if (track < 0 || track >= audio::kMaxInputs) return;
  const auto& edits = editList(track);
  if (edits.empty()) {
    (void)audio_.setTakeRegions(track, {});
    return;
  }
  std::vector<audio::TakeRegion> regions;
  regions.reserve(edits.size());
  for (const auto& edit : edits) {
    const auto path = resolveTake(track, edit.take);
    if (path.empty()) {
      setStatus(QStringLiteral("TRACK %1: take %2 is missing; edit not applied")
                    .arg(track + 1)
                    .arg(textOf(edit.take)));
      (void)audio_.setTakeRegions(track, {});
      return;
    }
    audio::TakeRegion region;
    region.take = path.string();
    region.offsetBeats = edit.offsetBeats;
    region.lengthBeats = edit.lengthBeats;
    region.gainDb = edit.gainDb;
    region.fadeInBeats = edit.fadeInBeats;
    region.fadeOutBeats = edit.fadeOutBeats;
    region.normalize = edit.normalize;
    regions.push_back(std::move(region));
  }
  std::string error;
  if (!audio_.setTakeRegions(track, regions, &error) && !error.empty()) {
    setStatus(QStringLiteral("TRACK %1: %2").arg(track + 1).arg(textOf(error)));
    log("timeline: track " + std::to_string(track + 1) + " edit refused: " + error);
  }
}

void TimelineViewModel::applyAllEdits() {
  // Empty lists clear the engine edit explicitly, so a project load or an undo
  // to no regions cannot leave an old edit stream playing.
  for (int track = 0; track < audio::kMaxInputs; ++track) applyTrackEdit(track);
}

void TimelineViewModel::pushUndo(int track) {
  if (track < 0 || track >= audio::kMaxInputs) return;
  auto& stack = undo_[static_cast<std::size_t>(track)];
  stack.push_back(UndoEntry{editList(track)});
  constexpr std::size_t kMaxUndo = 16;
  if (stack.size() > kMaxUndo) stack.erase(stack.begin());
  emitUndo();
}

void TimelineViewModel::emitUndo() {
  emit undoChanged();
}

void TimelineViewModel::setStatus(const QString& text) {
  if (text == statusText_) return;
  statusText_ = text;
  emit statusChanged();
}

void TimelineViewModel::setPage(int page) {
  const int clamped = std::clamp(page, 0, pageCount() - 1);
  if (clamped == page_) return;
  page_ = clamped;
  emit pageChanged();
  rebuildLanes();
  rebuildPreview();
}

void TimelineViewModel::nextPage() { setPage(page_ + 1); }
void TimelineViewModel::prevPage() { setPage(page_ - 1); }

void TimelineViewModel::setSelectedTrack(int track) {
  const int clamped = std::clamp(track, 0, audio::kMaxInputs - 1);
  if (clamped == selectedTrack_) return;
  selectedTrack_ = clamped;
  const int wantedPage = clamped / timeline::kTracksPerPage;
  if (wantedPage != page_) {
    page_ = wantedPage;
    emit pageChanged();
  }
  emit selectedTrackChanged();
  emit loopChanged();
  rebuildLanes();
  rebuildPreview();
  rebuildPunch();
}

QString TimelineViewModel::selectedTrackName() const {
  return QStringLiteral("TRACK %1  %2")
      .arg(selectedTrack_ + 1, 2, 10, QLatin1Char('0'))
      .arg(recorder_.trackDisplayName(selectedTrack_));
}

void TimelineViewModel::setViewStartBeats(double beats) {
  const double maximum = std::max(0.0, totalBeats_ - viewBeats_);
  const double clamped = clamp(beats, 0.0, maximum);
  if (std::abs(clamped - viewStartBeats_) < 1e-9) return;
  viewStartBeats_ = clamped;
  emit viewChanged();
}

void TimelineViewModel::scrollView(int direction) {
  setViewStartBeats(viewStartBeats_ +
                    (direction < 0 ? -viewBeats_ * 0.5 : viewBeats_ * 0.5));
}

void TimelineViewModel::zoom(int direction) {
  const double factor = direction > 0 ? 0.5 : 2.0;
  const double wanted = clamp(viewBeats_ * factor, timeline::kMinViewBeats,
                              timeline::kMaxViewBeats);
  if (std::abs(wanted - viewBeats_) < 1e-9) return;
  const double centre = viewStartBeats_ + viewBeats_ * 0.5;
  viewBeats_ = wanted;
  setViewStartBeats(centre - viewBeats_ * 0.5);
  emit viewChanged();
}

void TimelineViewModel::setCursorBeats(double beats) {
  const double clamped = timeline::clampBeat(beats);
  if (std::abs(clamped - cursorBeats_) < 1e-9) return;
  cursorBeats_ = clamped;
  emit cursorChanged();
  rebuildPreview();
}

void TimelineViewModel::moveCursor(double beatDelta, int trackDelta) {
  if (trackDelta != 0) setSelectedTrack(selectedTrack_ + trackDelta);
  setCursorBeats(cursorBeats_ + beatDelta);
}

QString TimelineViewModel::beatTextOf(double beats) const {
  return textOf(timeline::beatText(beats));
}

QString TimelineViewModel::playheadText() const { return playheadText_; }

QString TimelineViewModel::selectionText() const {
  if (!selection_.active) return QStringLiteral("NO REGION SELECTED");
  return QStringLiteral("IN %1  OUT %2  %3 BEATS")
      .arg(beatTextOf(selection_.start()))
      .arg(beatTextOf(selection_.end()))
      .arg(selection_.span(), 0, 'f', 2);
}

bool TimelineViewModel::loopEnabled() const {
  return settings_.takeLoop[static_cast<std::size_t>(selectedTrack_)].enabled;
}

double TimelineViewModel::loopStartBeats() const {
  return settings_.takeLoop[static_cast<std::size_t>(selectedTrack_)].startBeats;
}

double TimelineViewModel::loopEndBeats() const {
  return settings_.takeLoop[static_cast<std::size_t>(selectedTrack_)].endBeats;
}

QString TimelineViewModel::loopText() const {
  const auto& loop = settings_.takeLoop[static_cast<std::size_t>(selectedTrack_)];
  if (!loop.enabled) return QStringLiteral("LOOP OFF");
  return QStringLiteral("LOOP %1 - %2")
      .arg(beatTextOf(loop.startBeats))
      .arg(beatTextOf(loop.endBeats));
}

bool TimelineViewModel::canUndo() const {
  return !undo_[static_cast<std::size_t>(selectedTrack_)].empty();
}

QString TimelineViewModel::undoText() const {
  const auto depth = undo_[static_cast<std::size_t>(selectedTrack_)].size();
  if (depth == 0) return QStringLiteral("UNDO - empty");
  return QStringLiteral("UNDO %1 LEVEL(S)").arg(depth);
}

int TimelineViewModel::regionCount() const {
  return static_cast<int>(editList(selectedTrack_).size());
}

void TimelineViewModel::beginSelection(int track, double beat) {
  setSelectedTrack(track);
  cursorBeats_ = timeline::clampBeat(beat);
  selection_.active = true;
  selection_.in = cursorBeats_;
  selection_.out = cursorBeats_;
  emit cursorChanged();
  emit selectionChanged();
  rebuildLanes();
  rebuildPreview();
}

void TimelineViewModel::updateSelection(double beat) {
  if (!selection_.active) return;
  selection_.out = timeline::clampBeat(beat);
  emit selectionChanged();
  rebuildPreview();
}

void TimelineViewModel::endSelection() {
  if (!selection_.active) return;
  if (selection_.in > selection_.out) std::swap(selection_.in, selection_.out);
  emit selectionChanged();
  rebuildPreview();
}

void TimelineViewModel::clearSelection() {
  if (!selection_.active) return;
  selection_ = {};
  emit selectionChanged();
  rebuildPreview();
}

void TimelineViewModel::setInAtCursor() {
  if (!selection_.active) {
    selection_.active = true;
    selection_.out = cursorBeats_;
  }
  selection_.in = cursorBeats_;
  if (selection_.in > selection_.out) std::swap(selection_.in, selection_.out);
  emit selectionChanged();
  rebuildPreview();
}

void TimelineViewModel::setOutAtCursor() {
  if (!selection_.active) {
    selection_.active = true;
    selection_.in = cursorBeats_;
  }
  selection_.out = cursorBeats_;
  if (selection_.in > selection_.out) std::swap(selection_.in, selection_.out);
  emit selectionChanged();
  rebuildPreview();
}

QString TimelineViewModel::nudgeIn(double deltaBeats) {
  if (!selection_.active) selection_.active = true;
  selection_.in = std::max(0.0, selection_.in + deltaBeats);
  if (selection_.in > selection_.out) selection_.out = selection_.in;
  emit selectionChanged();
  rebuildPreview();
  return beatTextOf(selection_.in);
}

QString TimelineViewModel::nudgeOut(double deltaBeats) {
  if (!selection_.active) selection_.active = true;
  selection_.out = std::max(0.0, selection_.out + deltaBeats);
  if (selection_.out < selection_.in) selection_.in = selection_.out;
  emit selectionChanged();
  rebuildPreview();
  return beatTextOf(selection_.out);
}

QString TimelineViewModel::applyOp(int op, double gainDb, double fadeIn,
                                   double fadeOut) {
  if (op < 0 || op >= timeline::kTimelineOpCount) return QStringLiteral("unknown op");
  const auto operation = static_cast<timeline::TimelineOp>(op);
  if (!timeline::timelineOpAvailable(operation)) {
    return QStringLiteral("RENDER blocked: ") +
           QString::fromLatin1(timeline::kRenderMissingApi) +
           QStringLiteral(" is missing; edits never rewrite a take file");
  }
  std::vector<session::TakeRegion> current = editList(selectedTrack_);
  if (current.empty()) {
    // The first region-creating op starts from the active take as one implicit
    // "whole take" region; no take file is touched. CLEAR/DELETE of an empty
    // list would just be a no-op, so they stay "nothing here" instead.
    if (operation == timeline::TimelineOp::Clear ||
        operation == timeline::TimelineOp::Delete) {
      return QStringLiteral("no region edit on track %1").arg(selectedTrack_ + 1);
    }
    const auto& source = sources_[static_cast<std::size_t>(selectedTrack_)];
    if (source.key.empty() || source.path.empty()) {
      return QStringLiteral("track %1 has no active take to edit")
          .arg(selectedTrack_ + 1);
    }
    session::TakeRegion base;
    base.take = source.key;
    current.push_back(base);
  }
  if ((operation == timeline::TimelineOp::Trim ||
       operation == timeline::TimelineOp::Delete) &&
      !(selection_.active && selection_.span() > 0.0)) {
    return QStringLiteral("select a region with a touch drag (or IN/OUT) first");
  }
  const auto length = lengthFnFor(selectedTrack_);
  const timeline::BeatRange range = selection_.active
                                        ? selection_
                                        : timeline::BeatRange{};
  std::vector<session::TakeRegion> next = current;
  switch (operation) {
    case timeline::TimelineOp::Trim:
      next = timeline::trimRegions(current, range, length);
      break;
    case timeline::TimelineOp::Split:
      next = timeline::splitRegionsAt(current, selection_.active ? selection_.start()
                                                                 : cursorBeats_,
                                      length);
      break;
    case timeline::TimelineOp::Gain:
      next = timeline::gainRegions(current, range, static_cast<float>(gainDb), length);
      break;
    case timeline::TimelineOp::Fades:
      next = timeline::fadeRegions(current, range, fadeIn, fadeOut, length);
      break;
    case timeline::TimelineOp::Normalize:
      next = timeline::normalizeRegions(current, range, length);
      break;
    case timeline::TimelineOp::Delete:
      next = timeline::deleteRegions(current, range, length);
      break;
    case timeline::TimelineOp::Clear:
      next.clear();
      break;
    case timeline::TimelineOp::Render:
      break;
  }
  if (next == current) {
    lastOpText_ = textOf(timeline::timelineOpName(operation)) +
                  QStringLiteral(" - no change");
    emit statusChanged();
    return {};
  }
  pushUndo(selectedTrack_);
  editList(selectedTrack_) = std::move(next);
  applyTrackEdit(selectedTrack_);
  appliedEditSignature_ = editSignature(settings_.takeEdit);
  previewDraftActive_ = false;
  lastOpText_ = textOf(timeline::timelineOpName(operation)) + QStringLiteral(" -> ") +
                QString::number(regionCount()) + QStringLiteral(" REGION(S)");
  refresh();
  return {};
}

void TimelineViewModel::setPreviewDraft(double gainDb, double fadeIn, double fadeOut) {
  previewDraftActive_ = true;
  previewGainDb_ = gainDb;
  previewFadeInBeats_ = std::max(0.0, fadeIn);
  previewFadeOutBeats_ = std::max(0.0, fadeOut);
  rebuildPreview();
}

bool TimelineViewModel::undo() {
  auto& stack = undo_[static_cast<std::size_t>(selectedTrack_)];
  if (stack.empty()) return false;
  editList(selectedTrack_) = stack.back().regions;
  stack.pop_back();
  applyTrackEdit(selectedTrack_);
  appliedEditSignature_ = editSignature(settings_.takeEdit);
  previewDraftActive_ = false;
  lastOpText_ = QStringLiteral("UNDO -> ") + QString::number(regionCount()) +
                QStringLiteral(" REGION(S)");
  emitUndo();
  refresh();
  return true;
}

void TimelineViewModel::refreshViewWindow() {
  double total = 16.0;
  for (int track = 0; track < audio::kMaxInputs; ++track) {
    total = std::max(total, trackBeats_[static_cast<std::size_t>(track)]);
    for (const auto& region : editList(track)) {
      total = std::max(total, timeline::regionEndBeats(region, lengthFnFor(track)));
    }
    const auto& loop = settings_.takeLoop[static_cast<std::size_t>(track)];
    if (loop.enabled) total = std::max(total, loop.endBeats);
  }
  for (const auto& point : {settings_.punch.playFrom, settings_.punch.in,
                            settings_.punch.out}) {
    if (point.enabled) {
      total = std::max(total, timeline::measureBeat(point.measure) + 4.0);
    }
  }
  total = std::max(total, playheadBeats_ + 4.0);
  total = std::ceil(total / timeline::kBeatsPerBar) * timeline::kBeatsPerBar;
  const bool totalChanged = std::abs(total - totalBeats_) > 1e-9;
  totalBeats_ = total;
  viewBeats_ = clamp(viewBeats_, timeline::kMinViewBeats, timeline::kMaxViewBeats);
  const double maximum = std::max(0.0, totalBeats_ - viewBeats_);
  const double start = clamp(viewStartBeats_, 0.0, maximum);
  const bool startChanged = std::abs(start - viewStartBeats_) > 1e-9;
  viewStartBeats_ = start;
  if (totalChanged || startChanged) emit viewChanged();
}

void TimelineViewModel::rebuildPunch() {
  QVariantList markers;
  const auto add = [&markers](const char* label, const session::PunchPoint& point) {
    QVariantMap marker;
    marker.insert(QStringLiteral("label"), QString::fromLatin1(label));
    marker.insert(QStringLiteral("measure"), point.measure);
    marker.insert(QStringLiteral("beat"), timeline::measureBeat(point.measure));
    marker.insert(QStringLiteral("enabled"), point.enabled);
    markers.push_back(marker);
  };
  add("FROM", settings_.punch.playFrom);
  add("IN", settings_.punch.in);
  add("OUT", settings_.punch.out);
  if (markers == punchMarkers_) return;
  punchMarkers_ = std::move(markers);
  emit lanesChanged();
}

void TimelineViewModel::rebuildLanes() {
  QVariantList lanes;
  const int first = page_ * timeline::kTracksPerPage;
  lanes.reserve(timeline::kTracksPerPage);
  for (int offset = 0; offset < timeline::kTracksPerPage; ++offset) {
    const int track = first + offset;
    if (track >= audio::kMaxInputs) break;
    const auto& source = sources_[static_cast<std::size_t>(track)];
    const auto& peak = peaks_[static_cast<std::size_t>(track)];
    QVariantMap lane;
    lane.insert(QStringLiteral("track"), track);
    lane.insert(QStringLiteral("label"),
                QStringLiteral("%1").arg(track + 1, 2, 10, QLatin1Char('0')));
    lane.insert(QStringLiteral("name"), recorder_.trackDisplayName(track));
    const auto& edits = editList(track);
    lane.insert(QStringLiteral("take"), takeLabel(source.path));
    lane.insert(QStringLiteral("hasTake"), !source.path.empty());
    lane.insert(QStringLiteral("takeBeats"), trackBeats_[static_cast<std::size_t>(track)]);
    lane.insert(QStringLiteral("hasPeaks"), peak.valid && !peak.summary.empty());
    lane.insert(QStringLiteral("edits"), static_cast<int>(edits.size()));
    QVariantList wave;
    if (peak.valid) {
      const double takeBeats = trackBeats_[static_cast<std::size_t>(track)];
      const auto reduced = timeline::reducePeaks(peak.summary, 0.0, takeBeats,
                                                 takeBeats, 96);
      wave.reserve(static_cast<int>(reduced.size()));
      for (const float value : reduced) wave.push_back(value);
    }
    lane.insert(QStringLiteral("wave"), wave);
    QVariantList regions;
    for (const auto& region : edits) {
      QVariantMap entry;
      const double end = timeline::regionEndBeats(region, lengthFnFor(track));
      entry.insert(QStringLiteral("start"), region.offsetBeats);
      entry.insert(QStringLiteral("length"), std::max(0.0, end - region.offsetBeats));
      entry.insert(QStringLiteral("gainDb"), region.gainDb);
      entry.insert(QStringLiteral("fadeIn"), region.fadeInBeats);
      entry.insert(QStringLiteral("fadeOut"), region.fadeOutBeats);
      entry.insert(QStringLiteral("normalize"), region.normalize);
      entry.insert(QStringLiteral("take"), textOf(region.take));
      regions.push_back(entry);
    }
    lane.insert(QStringLiteral("regions"), regions);
    lane.insert(QStringLiteral("selected"), track == selectedTrack_);
    const auto& loop = settings_.takeLoop[static_cast<std::size_t>(track)];
    lane.insert(QStringLiteral("loopOn"), loop.enabled);
    lane.insert(QStringLiteral("loopStart"), loop.startBeats);
    lane.insert(QStringLiteral("loopEnd"), loop.endBeats);
    lanes.push_back(lane);
  }
  if (lanes != lanes_) {
    lanes_ = std::move(lanes);
    emit lanesChanged();
  }
}

void TimelineViewModel::rebuildPreview() {
  std::string take;
  double start = 0.0;
  double length = 0.0;
  double gainDb = previewGainDb_;
  double fadeIn = previewFadeInBeats_;
  double fadeOut = previewFadeOutBeats_;
  const auto& edits = editList(selectedTrack_);
  const timeline::TakeLengthFn lengthFn = lengthFnFor(selectedTrack_);
  const session::TakeRegion* selected = nullptr;
  if (selection_.active) {
    for (const auto& region : edits) {
      if (timeline::regionOverlaps(region, selection_, lengthFn)) {
        selected = &region;
        break;
      }
    }
  } else if (!edits.empty()) {
    selected = &edits.front();
  }
  if (selected != nullptr) {
    take = selected->take;
    start = selected->offsetBeats;
    length = std::max(0.0, timeline::regionEndBeats(*selected, lengthFn) - start);
    if (!previewDraftActive_) {
      gainDb = selected->gainDb;
      fadeIn = selected->fadeInBeats;
      fadeOut = selected->fadeOutBeats;
    }
  } else {
    const auto& source = sources_[static_cast<std::size_t>(selectedTrack_)];
    if (!source.path.empty()) {
      take = source.key;
      start = selection_.active ? selection_.start() : 0.0;
      length = selection_.active ? std::max(0.25, selection_.span())
                                 : trackBeats_[static_cast<std::size_t>(selectedTrack_)];
      if (!previewDraftActive_) {
        gainDb = 0.0;
        fadeIn = 0.0;
        fadeOut = 0.0;
      }
    }
  }

  QVariantList wave;
  QString previewText;
  double takeBeats = 0.0;
  if (!take.empty()) {
    const auto path = resolveTake(selectedTrack_, take);
    takeBeats = takeLengthFor(selectedTrack_, take);
    if (!path.empty() && takeBeats > 0.0) {
      audio::PeakSummary summary;
      bool have = false;
      const auto& source = sources_[static_cast<std::size_t>(selectedTrack_)];
      if (take == source.key &&
          peaks_[static_cast<std::size_t>(selectedTrack_)].valid) {
        summary = peaks_[static_cast<std::size_t>(selectedTrack_)].summary;
        have = true;
      } else if (const auto cached = sidecarCache_.find(path.string());
                 cached != sidecarCache_.end()) {
        summary = cached->second;
        have = true;
      } else {
        std::string error;
        audio::PeakSummary read;
        if (audio::readPeaks(path.string(), read, &error)) {
          sidecarCache_[path.string()] = read;
          summary = std::move(read);
          have = true;
        }
      }
      if (have) {
        const auto reduced =
            timeline::reducePeaks(summary, start, start + length, takeBeats, 128);
        wave.reserve(static_cast<int>(reduced.size()));
        for (const float value : reduced) wave.push_back(value);
      }
    }
    previewText = takeLabel(path) + QStringLiteral("  ") + beatTextOf(start) +
                  QStringLiteral(" - ") + beatTextOf(start + length) +
                  QStringLiteral("  ") + QString::number(gainDb, 'f', 1) +
                  QStringLiteral(" dB  FADE ") + QString::number(fadeIn, 'f', 2) +
                  QStringLiteral("/") + QString::number(fadeOut, 'f', 2);
  }

  previewGainDb_ = gainDb;
  previewFadeInBeats_ = fadeIn;
  previewFadeOutBeats_ = fadeOut;
  previewLengthBeats_ = length;
  if (wave == previewWave_ && previewText == previewText_) return;
  previewWave_ = std::move(wave);
  previewText_ = previewText;
  emit previewChanged();
}

void TimelineViewModel::refresh() {
  const int tempo = state_.tempoX100.load();
  const bool tempoChanged = tempo != appliedTempoX100_;
  if (tempoChanged) {
    appliedTempoX100_ = tempo;
    takeBeatsByKey_.clear();
  }
  refreshSources();
  pumpScanner();
  for (int track = 0; track < audio::kMaxInputs; ++track) {
    const auto& source = sources_[static_cast<std::size_t>(track)];
    trackBeats_[static_cast<std::size_t>(track)] =
        timeline::takeLengthBeats(source.seconds, state_.tempoX100.load());
  }
  const auto status = link_.status();
  const double beats =
      static_cast<double>(status.tick) / static_cast<double>(kTicksPerBeat);
  if (std::abs(beats - playheadBeats_) > 1e-9) {
    playheadBeats_ = beats;
    playheadText_ = textOf(timeline::beatText(beats));
    emit playheadChanged();
  }

  const auto signature = editSignature(settings_.takeEdit);
  if (signature != appliedEditSignature_) {
    appliedEditSignature_ = signature;
    applyAllEdits();
  }
  if (tempoChanged) {
    // The engine maps region beats with the transport tempo at apply time.
    applyAllEdits();
  }

  const QString nextSelection = selectionText();
  if (nextSelection != selectionText_) {
    selectionText_ = nextSelection;
    emit selectionChanged();
  }
  refreshViewWindow();
  rebuildPunch();
  rebuildLanes();
  rebuildPreview();

  const auto& loop = settings_.takeLoop[static_cast<std::size_t>(selectedTrack_)];
  const QString nextOps = QStringLiteral("%1 REGION(S)").arg(regionCount()) +
                          (loop.enabled ? QStringLiteral(" - ") + loopText()
                                        : QStringLiteral(" - LOOP OFF"));
  if (nextOps != opsText_) {
    opsText_ = nextOps;
    emit statusChanged();
  }
  const QString nextStatus =
      QStringLiteral("%1 - %2 - %3")
          .arg(selectedTrackName())
          .arg(takeLabel(sources_[static_cast<std::size_t>(selectedTrack_)].path))
          .arg(selectionText());
  if (statusText_.startsWith(QStringLiteral("TRACK ")) &&
      statusText_.contains(QStringLiteral("missing"))) {
    // Keep an apply error visible until the next successful apply.
  } else if (nextStatus != statusText_) {
    setStatus(nextStatus);
  }
}

void TimelineViewModel::reloadFromSettings() {
  for (auto& stack : undo_) stack.clear();
  for (auto& source : sources_) source = {};
  for (auto& peak : peaks_) peak = {};
  takeBeatsByKey_.clear();
  sidecarCache_.clear();
  appliedEditSignature_.clear();
  appliedTempoX100_ = 0;
  previewDraftActive_ = false;
  emitUndo();
  refresh();
}

}  // namespace mnky::app
