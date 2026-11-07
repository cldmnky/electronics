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
          source.channels = info.channels;
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
    if (!source.path.empty() && peak.valid) {
      // A take holds an immutable recording, but a take NAME can be recorded
      // again (delete + re-record into the same slot). Compare the sidecar's
      // recorded size/mtime with the file on disk and rescan when they differ,
      // so the popup can never draw the previous take's peaks.
      std::error_code sizeError;
      const auto size = std::filesystem::file_size(source.path, sizeError);
      std::error_code timeError;
      const auto stamp = std::filesystem::last_write_time(source.path, timeError);
      const auto mtime =
          timeError ? 0u
                    : static_cast<std::uint64_t>(static_cast<std::int64_t>(
                          stamp.time_since_epoch().count()));
      if (!sizeError &&
          (static_cast<std::uint64_t>(size) != peak.summary.sourceBytes ||
           mtime != peak.summary.sourceMtime)) {
        peak = {};
        sidecarCache_.erase(source.path.string());
        queueScan(track);
      }
    }
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
          ++peakGeneration_;
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
      ++peakGeneration_;
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
  if (stack.size() > kUndoDepth) stack.erase(stack.begin());
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

void TimelineViewModel::openEdit(int track) {
  const int clamped = std::clamp(track, 0, audio::kMaxInputs - 1);
  const bool trackChanged = clamped != selectedTrack_;
  selectedTrack_ = clamped;
  refreshSources();
  if (trackChanged) emit selectedTrackChanged();
  emit loopChanged();
  rebuildPunch();
  // Fit the view to the take: the popup edits one take from its start, not the
  // whole session.
  const double length = trackBeats_[static_cast<std::size_t>(selectedTrack_)];
  viewStartBeats_ = 0.0;
  viewBeats_ = length > 0.0 ? length : timeline::kMinViewBeats;
  editPlayheadBeats_ = 0.0;
  selection_ = {};
  cursorBeats_ = 0.0;
  editWaveSignature_.clear();
  if (!editOpen_) {
    editOpen_ = true;
    emit editOpenChanged();
  }
  rebuildEditWave();
  rebuildPreview();
  updateScanBadge();
  emit viewChanged();
  emit selectionChanged();
  emit cursorChanged();
  emit editChanged();
  emit playheadChanged();
}

void TimelineViewModel::closeEdit() {
  if (!editOpen_) return;
  editOpen_ = false;
  emit editOpenChanged();
}

void TimelineViewModel::setSelectedTrack(int track) {
  const int clamped = std::clamp(track, 0, audio::kMaxInputs - 1);
  if (clamped == selectedTrack_) return;
  selectedTrack_ = clamped;
  emit selectedTrackChanged();
  emit loopChanged();
  rebuildPreview();
  rebuildPunch();
  if (editOpen_) {
    editWaveSignature_.clear();
    rebuildEditWave();
    updateScanBadge();
  }
  emit editChanged();
}

QString TimelineViewModel::selectedTrackName() const {
  return QStringLiteral("TRACK %1 - %2")
      .arg(selectedTrack_ + 1, 2, 10, QLatin1Char('0'))
      .arg(recorder_.trackDisplayName(selectedTrack_));
}

QString TimelineViewModel::editTrackText() const { return selectedTrackName(); }

QString TimelineViewModel::editTitle() const {
  const auto& source = sources_[static_cast<std::size_t>(selectedTrack_)];
  if (source.path.empty()) return QStringLiteral("EDIT - no active take");
  const int seconds = static_cast<int>(source.seconds + 0.5);
  return QStringLiteral("EDIT %1  %2:%3")
      .arg(takeLabel(source.path))
      .arg(seconds / 60)
      .arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

QString TimelineViewModel::editMetaText() const {
  const auto& source = sources_[static_cast<std::size_t>(selectedTrack_)];
  if (source.path.empty()) return QStringLiteral("NO ACTIVE TAKE");
  const QString takeText = QStringLiteral("%1 kHz - %2 ch - %3 beats")
                               .arg(source.sampleRate / 1000)
                               .arg(source.channels)
                               .arg(trackBeats_[static_cast<std::size_t>(selectedTrack_)], 0,
                                    'f', 1);
  // Once a region op changes the edited length the header states it too, so
  // TRIM/DELETE are visible as a duration change and not only in the waveform.
  const double edited = editedBeats();
  if (edited <= 0.0 || std::abs(edited - takeBeats()) < 0.05) return takeText;
  return takeText + QStringLiteral(" -> %1 edited").arg(edited, 0, 'f', 1);
}

double TimelineViewModel::editedBeats() const {
  const auto& edits = editList(selectedTrack_);
  if (edits.empty()) return 0.0;
  const timeline::TakeLengthFn lengthFn = lengthFnFor(selectedTrack_);
  // The sounding length of the committed edit, not its outer span: DELETE
  // removes a middle slice, so the sum of the region lengths is what remains.
  double total = 0.0;
  for (const auto& region : edits) {
    total += std::max(0.0, timeline::regionEndBeats(region, lengthFn) -
                               region.offsetBeats);
  }
  return total;
}

void TimelineViewModel::selectEditOp(int op) {
  const int clamped = std::clamp(op, 0, timeline::kEditOpCount - 1);
  if (clamped != editOp_) {
    editOp_ = clamped;
    emit editOpChanged();
  }
  // A repeat tap on a value card re-arms the draft preview.
  setPreviewDraft(previewGainDb_, previewFadeInBeats_, previewFadeOutBeats_);
}

QString TimelineViewModel::editOpName() const {
  return textOf(timeline::editOpName(static_cast<timeline::EditOp>(editOp_)));
}

QString TimelineViewModel::editFooterText() const {
  if (!selection_.active || selection_.span() <= 0.0) {
    return QStringLiteral("NO IN/OUT - drag on the waveform or drag the edges");
  }
  return QStringLiteral("IN %1 OUT %2 (%3 beats)")
      .arg(textOf(timeline::beatTextCompact(selection_.start())))
      .arg(textOf(timeline::beatTextCompact(selection_.end())))
      .arg(selection_.span(), 0, 'f', 2);
}

QString TimelineViewModel::editHintText() const {
  if (sources_[static_cast<std::size_t>(selectedTrack_)].path.empty()) {
    return QStringLiteral("track %1 has no ACTIVE take - pick one on RECORDER")
        .arg(selectedTrack_ + 1, 2, 10, QLatin1Char('0'));
  }
  return QStringLiteral("TAP SETS THE PLAYHEAD - DRAG IN/OUT - THE PANEL KNOB MOVES THE FOCUSED CONTROL");
}

void TimelineViewModel::setViewStartBeats(double beats) {
  const double total = editOpen_ ? takeBeats() : totalBeats_;
  const double maximum = std::max(0.0, total - viewBeats_);
  const double clamped = clamp(beats, 0.0, maximum);
  if (std::abs(clamped - viewStartBeats_) < 1e-9) return;
  viewStartBeats_ = clamped;
  emit viewChanged();
  rebuildEditWave();
}

void TimelineViewModel::scrollView(int direction) {
  setViewStartBeats(viewStartBeats_ +
                    (direction < 0 ? -viewBeats_ * 0.5 : viewBeats_ * 0.5));
}

void TimelineViewModel::zoom(int direction) {
  zoomBy(direction > 0 ? 0.5 : 2.0);
}

void TimelineViewModel::zoomBy(double factor) {
  if (!(factor > 0.0)) return;
  const double length = editOpen_ ? takeBeats() : totalBeats_;
  const double minimum = editOpen_ ? timeline::kEditMinViewBeats
                                   : timeline::kMinViewBeats;
  const double maximum = editOpen_ ? std::max(minimum, length)
                                   : timeline::kMaxViewBeats;
  const double wanted = clamp(viewBeats_ * factor, minimum, maximum);
  if (std::abs(wanted - viewBeats_) < 1e-9) return;
  // Zoom around the cursor when it is inside the window (a pinch then holds the
  // point the finger is on), else around the window centre.
  const double anchor =
      editOpen_ && cursorBeats_ > viewStartBeats_ &&
              cursorBeats_ < viewStartBeats_ + viewBeats_
          ? cursorBeats_
          : viewStartBeats_ + viewBeats_ * 0.5;
  const double ratio = (anchor - viewStartBeats_) / viewBeats_;
  viewBeats_ = wanted;
  viewStartBeats_ = 0.0;
  setViewStartBeats(anchor - ratio * viewBeats_);
  emit viewChanged();
  rebuildEditWave();
}

double TimelineViewModel::beatAtRatio(double ratio) const {
  return timeline::clampBeat(viewStartBeats_ +
                             clamp(ratio, 0.0, 1.0) * viewBeats_);
}

double TimelineViewModel::ratioOfBeat(double beats) const {
  if (!(viewBeats_ > 0.0)) return 0.0;
  return (beats - viewStartBeats_) / viewBeats_;
}

void TimelineViewModel::setCursorBeats(double beats) {
  const double clamped = timeline::clampBeat(beats);
  if (std::abs(clamped - cursorBeats_) < 1e-9) return;
  cursorBeats_ = clamped;
  emit cursorChanged();
  rebuildPreview();
}

void TimelineViewModel::seekTo(double beats) {
  // Bluebox rule: the touch point is the playhead from now on. The transport
  // has no seek command in this build, so the popup playhead and the edit
  // cursor move together and the next gesture starts from the touched point.
  const double clamped = timeline::clampBeat(beats);
  editPlayheadBeats_ = clamped;
  setCursorBeats(clamped);
  emit playheadChanged();
  rebuildPreview();
}

double TimelineViewModel::setInBeats(double beats) {
  const double total = takeBeats();
  const double upper = selection_.active ? selection_.end() : total;
  selection_.active = true;
  selection_.in = std::min(timeline::clampBeat(beats), upper > 0.0 ? upper : total);
  if (!(total > 0.0)) selection_.in = timeline::clampBeat(beats);
  emit selectionChanged();
  rebuildPreview();
  return selection_.in;
}

double TimelineViewModel::setOutBeats(double beats) {
  const double lower = selection_.active ? selection_.start() : 0.0;
  selection_.active = true;
  selection_.out = std::max(timeline::clampBeat(beats), lower);
  emit selectionChanged();
  rebuildPreview();
  return selection_.out;
}

void TimelineViewModel::moveCursor(double beatDelta, int trackDelta) {
  if (trackDelta != 0 && !editOpen_) setSelectedTrack(selectedTrack_ + trackDelta);
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

QString TimelineViewModel::renderPeaks() {
  const auto& source = sources_[static_cast<std::size_t>(selectedTrack_)];
  if (source.path.empty()) return QStringLiteral("no active take to scan");
  auto& peak = peaks_[static_cast<std::size_t>(selectedTrack_)];
  peak = {};
  sidecarCache_.erase(source.path.string());
  queueScan(selectedTrack_);
  return QStringLiteral("scanning peaks: ") + takeLabel(source.path);
}

QString TimelineViewModel::applyOp(int op, double gainDb, double fadeIn,
                                   double fadeOut) {
  if (op < 0 || op >= timeline::kTimelineUiOpCount) return QStringLiteral("unknown op");
  const auto operation = static_cast<timeline::TimelineOp>(op);
  if (!timeline::timelineOpAvailable(operation)) {
    return QStringLiteral("render is not available in this build");
  }
  std::vector<session::TakeRegion> current = editList(selectedTrack_);
  if (current.empty()) {
    // The first region op starts from the active take as one implicit "whole
    // take" region; no take file is touched. DELETE is a region op too - the
    // user can cut a piece out of a fresh take without first applying TRIM.
    // CLEAR of an empty list is the only true no-op.
    if (operation == timeline::TimelineOp::Clear) {
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
  // The popup's ops act on IN..OUT, not on every whole region the range
  // happens to touch. Cut the region list at both range edges first, so GAIN /
  // FADES / NORMALIZE change only the selected span and DELETE removes only
  // that span. Without this a GAIN on a 4-beat selection rewrote the whole
  // take (and DELETE dropped the whole take, which an empty list then played
  // back unedited - the owner's "when I apply nothing happens"). TRIM already
  // clips to the range and SPLIT cuts at the cursor, so both skip the cut.
  const bool rangeOp = operation == timeline::TimelineOp::Gain ||
                       operation == timeline::TimelineOp::Fades ||
                       operation == timeline::TimelineOp::Normalize ||
                       operation == timeline::TimelineOp::Delete;
  if (rangeOp && range.active && range.span() > 0.0) {
    current = timeline::splitRegionsAt(current, range.start(), length);
    current = timeline::splitRegionsAt(current, range.end(), length);
  }
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
  if (next.empty() && !current.empty() &&
      operation == timeline::TimelineOp::Delete) {
    // A region list has no way to say "play nothing": applyTrackEdit sends an
    // empty list as CLEAR, which would restore the unedited take. Refuse the
    // full-take delete instead of silently undoing the edit.
    return QStringLiteral("DELETE would remove the whole take - select a part of it");
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
  // The header meta (edited length) is a NOTIFY editChanged property; without
  // this emit the popup keeps the pre-apply text even though the region list
  // and the waveform already changed.
  emit editChanged();
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

QString TimelineViewModel::applyEditOp() {
  const auto op = static_cast<timeline::EditOp>(editOp_);
  if (op == timeline::EditOp::Undo) {
    return undo() ? QString() : QStringLiteral("UNDO - nothing to undo");
  }
  timeline::EditOpRequest request;
  if (!timeline::editOpRequest(op, static_cast<float>(previewGainDb_),
                               previewFadeInBeats_, previewFadeOutBeats_,
                               &request)) {
    return QStringLiteral("unknown region op");
  }
  if (timeline::editOpNeedsRange(op) &&
      !(selection_.active && selection_.span() > 0.0)) {
    return QStringLiteral("set IN and OUT first (drag on the waveform)");
  }
  return applyOp(static_cast<int>(request.op), request.gainDb,
                 request.fadeInBeats, request.fadeOutBeats);
}

void TimelineViewModel::setEditGainDb(double gainDb) {
  const double clamped = std::clamp(gainDb, -24.0, 12.0);
  setPreviewDraft(clamped, previewFadeInBeats_, previewFadeOutBeats_);
}

void TimelineViewModel::setEditFadeIn(double beats) {
  setPreviewDraft(previewGainDb_, std::max(0.0, beats), previewFadeOutBeats_);
}

void TimelineViewModel::setEditFadeOut(double beats) {
  setPreviewDraft(previewGainDb_, previewFadeInBeats_, std::max(0.0, beats));
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
  // Same reason as applyOp: the header states the edited length.
  emit editChanged();
  refresh();
  return true;
}

void TimelineViewModel::refreshViewWindow() {
  if (editOpen_) {
    // The popup owns the window: it is take-relative, so the session's total
    // length and the punch/loop extents never push it around.
    const double length = takeBeats();
    totalBeats_ = std::max(length, timeline::kEditMinViewBeats);
    const double minimum = timeline::kEditMinViewBeats;
    const double maximum = std::max(minimum, totalBeats_);
    const double clampedBeats = clamp(viewBeats_, minimum, maximum);
    const double clampedStart =
        clamp(viewStartBeats_, 0.0, std::max(0.0, totalBeats_ - clampedBeats));
    const bool changed = std::abs(clampedBeats - viewBeats_) > 1e-9 ||
                         std::abs(clampedStart - viewStartBeats_) > 1e-9;
    viewBeats_ = clampedBeats;
    viewStartBeats_ = clampedStart;
    if (changed) emit viewChanged();
    return;
  }
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
  emit punchChanged();
}

// The committed region list as drawable spans. A region's length 0 means "to
// the end of the take" (session contract), and normalize scales the region so
// its peak lands at the normalize target instead of at the take's own peak.
auto TimelineViewModel::appliedShapes(int track) const -> std::vector<RegionShape> {
  std::vector<RegionShape> shapes;
  const auto& edits = editList(track);
  if (edits.empty()) return shapes;
  const timeline::TakeLengthFn lengthFn = lengthFnFor(track);
  const auto& peak = peaks_[static_cast<std::size_t>(track)];
  for (const auto& region : edits) {
    RegionShape shape;
    shape.start = region.offsetBeats;
    shape.end = timeline::regionEndBeats(region, lengthFn);
    if (shape.end <= shape.start) continue;
    double scale = std::pow(10.0, static_cast<double>(region.gainDb) / 20.0);
    if (region.normalize) {
      double regionPeak = 0.0;
      if (peak.valid && !peak.summary.empty()) {
        const auto reduced = timeline::reducePeakPairs(peak.summary, shape.start,
                                                       shape.end, takeBeats(), 64);
        for (const float value : reduced) regionPeak = std::max(regionPeak,
                                                                static_cast<double>(value));
      }
      if (regionPeak > 1e-6) {
        scale *= std::min(4.0, static_cast<double>(
                                   std::pow(10.0, audio::kNormalizePeakDbfs / 20.0f)) /
                                   regionPeak);
      }
    }
    shape.gain = std::min(4.0, scale);
    shape.fadeIn = std::max(0.0, region.fadeInBeats);
    shape.fadeOut = std::max(0.0, region.fadeOutBeats);
    shapes.push_back(shape);
  }
  return shapes;
}

// Committed amplitude multiplier at one beat: 0 outside every region (TRIM /
// DELETE take that part of the take out of the picture), the region's gain and
// its linear fade ramps inside one.
double TimelineViewModel::shapeGainAt(const std::vector<RegionShape>& shapes,
                                      double beat) {
  if (shapes.empty()) return 1.0;
  for (const auto& shape : shapes) {
    if (beat < shape.start || beat >= shape.end) continue;
    double gain = shape.gain;
    if (shape.fadeIn > 0.0 && beat < shape.start + shape.fadeIn) {
      gain *= (beat - shape.start) / shape.fadeIn;
    }
    if (shape.fadeOut > 0.0 && beat > shape.end - shape.fadeOut) {
      gain *= (shape.end - beat) / shape.fadeOut;
    }
    return std::clamp(gain, 0.0, 4.0);
  }
  return 0.0;
}

void TimelineViewModel::rebuildEditWave() {
  if (!editOpen_) return;
  const auto& source = sources_[static_cast<std::size_t>(selectedTrack_)];
  const auto& peak = peaks_[static_cast<std::size_t>(selectedTrack_)];
  // The popup waveform is 800 px wide; one peak pair per pixel keeps the paint
  // pass a pure polyline (no reduction there).
  constexpr int kColumns = 800;
  const double viewEnd = viewStartBeats_ + viewBeats_;
  // The wave draws the take AFTER the committed ops, so the region list is part
  // of the cache key: an APPLY (or an UNDO) redraws the waveform.
  const QString signature =
      QStringLiteral("%1|%2|%3|%4|%5|%6|%7")
          .arg(QString::fromStdString(source.path.string()))
          .arg(viewStartBeats_, 0, 'f', 6)
          .arg(viewBeats_, 0, 'f', 6)
          .arg(takeBeats(), 0, 'f', 6)
          .arg(peakGeneration_)
          .arg(peak.valid ? 1 : 0)
          .arg(QString::fromStdString(editSignature(settings_.takeEdit)));
  if (signature == editWaveSignature_) return;
  editWaveSignature_ = signature;
  QVariantList wave;
  if (peak.valid && !peak.summary.empty()) {
    const auto reduced = timeline::reducePeakPairs(
        peak.summary, viewStartBeats_, viewEnd, takeBeats(), kColumns);
    const std::vector<RegionShape> shapes = appliedShapes(selectedTrack_);
    wave.reserve(static_cast<int>(reduced.size()));
    const int pairs = static_cast<int>(reduced.size()) / 2;
    for (int column = 0; column < pairs; ++column) {
      const double beat = viewStartBeats_ +
                          (static_cast<double>(column) + 0.5) * viewBeats_ /
                              std::max(1, kColumns);
      const double gain = shapeGainAt(shapes, beat);
      wave.push_back(static_cast<float>(
          std::min(1.0, static_cast<double>(reduced[static_cast<std::size_t>(column) * 2]) *
                            gain)));
      wave.push_back(static_cast<float>(
          std::min(1.0, static_cast<double>(reduced[static_cast<std::size_t>(column) * 2 + 1]) *
                            gain)));
    }
  }
  const bool columnsChanged = kColumns != editWaveColumns_;
  editWaveColumns_ = kColumns;
  if (wave == editWave_ && !columnsChanged) return;
  editWave_ = std::move(wave);
  emit editWaveChanged();
}

void TimelineViewModel::updateScanBadge() {
  const auto& source = sources_[static_cast<std::size_t>(selectedTrack_)];
  const bool missing =
      editOpen_ && !source.path.empty() &&
      !peaks_[static_cast<std::size_t>(selectedTrack_)].valid;
  bool scanning = false;
  QString text;
  if (missing) {
    if (scanningTrack_ == selectedTrack_ && !scanningPath_.empty()) {
      if (scanner_) {
        const int percent = static_cast<int>(scanner_->progress() * 100.0f);
        text = QStringLiteral("SCANNING %1%").arg(std::clamp(percent, 0, 100));
      } else {
        text = QStringLiteral("SCANNING");
      }
      scanning = true;
    } else {
      // Queued or about to be: the badge states why the block is empty, so the
      // popup never shows dead text while the scanner works.
      text = QStringLiteral("SCANNING");
      scanning = true;
    }
  }
  if (scanning == editScanning_ && text == editScanText_) return;
  editScanning_ = scanning;
  editScanText_ = text;
  emit scanChanged();
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

  // BEFORE: the stored shape of the targeted region (what the take plays
  // today). AFTER: the drafts above. The popup draws both over the waveform so
  // an op preview is a comparison, not a guess.
  const bool regionFound = selected != nullptr;
  const double regionStart = regionFound ? selected->offsetBeats
                                         : (selection_.active ? selection_.start() : 0.0);
  const double regionLength =
      regionFound ? std::max(0.0, timeline::regionEndBeats(*selected, lengthFn) -
                                      selected->offsetBeats)
                  : length;
  const double regionGain = regionFound ? selected->gainDb : 0.0;
  const double regionFadeIn = regionFound ? selected->fadeInBeats : 0.0;
  const double regionFadeOut = regionFound ? selected->fadeOutBeats : 0.0;
  const bool shapeChanged =
      previewGainDb_ != gainDb || previewFadeInBeats_ != fadeIn ||
      previewFadeOutBeats_ != fadeOut || previewLengthBeats_ != length ||
      previewRegionFound_ != regionFound ||
      previewRegionStartBeats_ != regionStart ||
      previewRegionLengthBeats_ != regionLength ||
      previewRegionGainDb_ != regionGain ||
      previewRegionFadeInBeats_ != regionFadeIn ||
      previewRegionFadeOutBeats_ != regionFadeOut;
  previewGainDb_ = gainDb;
  previewFadeInBeats_ = fadeIn;
  previewFadeOutBeats_ = fadeOut;
  previewLengthBeats_ = length;
  previewRegionFound_ = regionFound;
  previewRegionStartBeats_ = regionStart;
  previewRegionLengthBeats_ = regionLength;
  previewRegionGainDb_ = regionGain;
  previewRegionFadeInBeats_ = regionFadeIn;
  previewRegionFadeOutBeats_ = regionFadeOut;
  if (wave == previewWave_ && previewText == previewText_ && !shapeChanged) return;
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
  bool playheadMoved = false;
  if (std::abs(beats - playheadBeats_) > 1e-9) {
    playheadBeats_ = beats;
    playheadText_ = textOf(timeline::beatText(beats));
    playheadMoved = true;
  }
  if (editOpen_ && status.rolling) {
    // While the transport rolls the popup playhead follows it, so the shaded
    // part of the take is the part playback has already passed.
    const double clamped = timeline::clampBeat(beats);
    if (std::abs(clamped - editPlayheadBeats_) > 1e-9) {
      editPlayheadBeats_ = clamped;
      playheadMoved = true;
    }
  }
  if (playheadMoved) emit playheadChanged();

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
  rebuildEditWave();
  updateScanBadge();
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
  editWaveSignature_.clear();
  editWave_ = {};
  ++peakGeneration_;
  emit editWaveChanged();
  emitUndo();
  refresh();
}

}  // namespace mnky::app
