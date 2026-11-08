#include "arrange_view_model.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <set>
#include <utility>

#include "logging.hpp"
#include "logic.hpp"
#include "project_logic.hpp"
#include "timeline_model.hpp"
#include "wav_info.hpp"

namespace mnky::app {
namespace {

constexpr int kArrangeWaveColumns = 48;

int clampGridIndex(int grid) {
  if (grid < arrange::kGridBar) return arrange::kGridBar;
  if (grid >= arrange::kGridCount) return arrange::kGridSixteenth;
  return grid;
}

QString takeStem(const std::string& key) {
  std::filesystem::path path(key);
  std::string stem = path.filename().string();
  if (stem.size() > 4 && stem.compare(stem.size() - 4, 4, ".wav") == 0) {
    stem.resize(stem.size() - 4);
  }
  if (stem.empty()) stem = key;
  return QString::fromStdString(stem);
}

}  // namespace

ArrangeViewModel::ArrangeViewModel(AppState& state, AudioGlue& audio,
                                   MixerModel& mixer, RecorderViewModel& recorder,
                                   SessionSettings& settings, QObject* parent)
    : QObject(parent),
      state_(state),
      audio_(audio),
      mixer_(mixer),
      recorder_(recorder),
      settings_(settings) {
  lastTransportTempoX100_ = state_.tempoX100.load();
  playheadBeats_ = beatsOfTick(transportTick(
      state_.tick.load(), state_.transportOffsetTicks.load()));
  clampSelection();
}

double ArrangeViewModel::beatsOfTick(std::uint32_t tick) {
  return static_cast<double>(tick) /
         static_cast<double>(kTicksPerBeat) * arrange::kBeatsPerBar;
}

session::SongClip* ArrangeViewModel::clipAt(int index) {
  if (index < 0 || index >= static_cast<int>(settings_.song.clips.size())) {
    return nullptr;
  }
  return &settings_.song.clips[static_cast<std::size_t>(index)];
}

const session::SongClip* ArrangeViewModel::clipAt(int index) const {
  if (index < 0 || index >= static_cast<int>(settings_.song.clips.size())) {
    return nullptr;
  }
  return &settings_.song.clips[static_cast<std::size_t>(index)];
}

std::filesystem::path ArrangeViewModel::takePath(
    const session::SongClip& clip) const {
  return recorder_.takePathForKey(clip.track, clip.take);
}

double ArrangeViewModel::takeBeats(const session::SongClip& clip) const {
  const auto path = takePath(clip);
  if (path.empty()) return 0.0;
  const double seconds = wavDurationSeconds(path);
  if (!(seconds > 0.0)) return 0.0;
  return timeline::takeLengthBeats(seconds, state_.tempoX100.load());
}

arrange::TakeLengthFn ArrangeViewModel::lengthFn() const {
  return [this](const session::SongClip& clip) { return takeBeats(clip); };
}

QString ArrangeViewModel::trackName(int track) const {
  if (track < 0 || track >= audio::kMaxTracks) return QStringLiteral("TRACK");
  return QString::fromStdString(mixer_.trackName(track));
}

QString ArrangeViewModel::trackModeWord(int track) const {
  if (track < 0 || track >= audio::kMaxTracks) return QStringLiteral("LIVE");
  return QString::fromStdString(mnky::app::trackModeWord(mixer_.mode(track)));
}

int ArrangeViewModel::readableClipCount() const {
  int count = 0;
  for (const auto& clip : settings_.song.clips) {
    if (!takePath(clip).empty()) ++count;
  }
  return count;
}

int ArrangeViewModel::selectedTrack() const {
  return std::clamp(state_.selectedStrip.load(), 0, audio::kMaxTracks - 1);
}

QString ArrangeViewModel::clipName(const session::SongClip& clip) const {
  return takeStem(clip.take);
}

QVariantList ArrangeViewModel::clipWave(const session::SongClip& clip) const {
  QVariantList values;
  const auto path = takePath(clip);
  if (path.empty()) return values;
  const std::string key = path.string();
  std::uint64_t size = 0;
  std::uint64_t mtime = 0;
  std::error_code sizeError;
  const auto bytes = std::filesystem::file_size(path, sizeError);
  if (!sizeError) size = bytes;
  std::error_code timeError;
  const auto stamp = std::filesystem::last_write_time(path, timeError);
  if (!timeError) mtime = static_cast<std::uint64_t>(stamp.time_since_epoch().count());
  ClipWaveCache* cache = nullptr;
  if (const auto found = waveCache_.find(key); found != waveCache_.end()) {
    if (found->second.size == size && found->second.mtime == mtime) {
      cache = &found->second;
    }
  }
  if (cache == nullptr) {
    ClipWaveCache entry;
    entry.path = key;
    entry.size = size;
    entry.mtime = mtime;
    entry.valid = audio::readPeaks(key, entry.summary);
    cache = &(waveCache_[key] = std::move(entry));
  }
  if (!cache->valid) return values;
  const double total = takeBeats(clip);
  if (!(total > 0.0)) return values;
  const double offset = std::max(0.0, clip.offsetBeats);
  const double length = clip.lengthBeats > 0.0
                            ? clip.lengthBeats
                            : std::max(0.0, total - offset);
  if (!(length > 0.0)) return values;
  const auto reduced = timeline::reducePeaks(cache->summary, offset, offset + length,
                                             total, kArrangeWaveColumns);
  values.reserve(static_cast<int>(reduced.size()));
  for (const float value : reduced) values.push_back(value);
  return values;
}

QVariantList ArrangeViewModel::sections() const {
  QVariantList list;
  const auto spans = arrange::sectionSpans(settings_.song.sections);
  const double length = lengthBeats();
  const double loopBegin = effectiveLoopStartBeats();
  const double loopFinish = effectiveLoopEndBeats();
  for (const auto& span : spans) {
    QVariantMap row;
    const auto& section = settings_.song.sections[static_cast<std::size_t>(span.index)];
    row.insert(QStringLiteral("index"), span.index);
    row.insert(QStringLiteral("name"), QString::fromStdString(section.name));
    row.insert(QStringLiteral("bars"), span.bars);
    row.insert(QStringLiteral("startBeats"), span.startBeats);
    row.insert(QStringLiteral("endBeats"), span.endBeats);
    row.insert(QStringLiteral("selected"), span.index == selectedSection_);
    row.insert(QStringLiteral("looping"),
               settings_.loopOn && std::abs(loopBegin - span.startBeats) < 0.001 &&
                   std::abs(loopFinish - span.endBeats) < 0.001);
    row.insert(QStringLiteral("beyondLength"),
               span.endBeats > length + 0.001);
    list.push_back(row);
  }
  return list;
}

QVariantList ArrangeViewModel::lanes() const {
  std::set<int> tracks;
  for (const auto& clip : settings_.song.clips) {
    if (clip.track >= 0 && clip.track < audio::kMaxTracks) tracks.insert(clip.track);
  }
  tracks.insert(selectedTrack());
  QVariantList list;
  for (const int track : tracks) {
    QVariantMap lane;
    lane.insert(QStringLiteral("track"), track);
    lane.insert(QStringLiteral("name"), trackName(track));
    lane.insert(QStringLiteral("mode"), trackModeWord(track));
    lane.insert(QStringLiteral("selected"), track == selectedTrack());
    QVariantList laneClips;
    const auto length = lengthFn();
    for (std::size_t index = 0; index < settings_.song.clips.size(); ++index) {
      const auto& clip = settings_.song.clips[index];
      if (clip.track != track) continue;
      QVariantMap row;
      row.insert(QStringLiteral("index"), static_cast<int>(index));
      row.insert(QStringLiteral("track"), clip.track);
      row.insert(QStringLiteral("startBeats"), std::max(0.0, clip.startBeats));
      row.insert(QStringLiteral("endBeats"),
                 arrange::clipEndBeats(clip, length));
      row.insert(QStringLiteral("lengthBeats"),
                 arrange::clipLengthBeats(clip, length));
      row.insert(QStringLiteral("name"), clipName(clip));
      row.insert(QStringLiteral("take"), QString::fromStdString(clip.take));
      row.insert(QStringLiteral("muted"), clip.muted);
      row.insert(QStringLiteral("loop"), clip.loop);
      row.insert(QStringLiteral("selected"),
                 static_cast<int>(index) == selectedClip_);
      row.insert(QStringLiteral("wave"), clipWave(clip));
      laneClips.push_back(row);
    }
    lane.insert(QStringLiteral("clips"), laneClips);
    list.push_back(lane);
  }
  return list;
}

QVariantList ArrangeViewModel::clips() const {
  QVariantList list;
  const auto length = lengthFn();
  for (std::size_t index = 0; index < settings_.song.clips.size(); ++index) {
    const auto& clip = settings_.song.clips[index];
    QVariantMap row;
    row.insert(QStringLiteral("index"), static_cast<int>(index));
    row.insert(QStringLiteral("track"), clip.track);
    row.insert(QStringLiteral("startBeats"), std::max(0.0, clip.startBeats));
    row.insert(QStringLiteral("endBeats"), arrange::clipEndBeats(clip, length));
    row.insert(QStringLiteral("lengthBeats"),
               arrange::clipLengthBeats(clip, length));
    row.insert(QStringLiteral("name"), clipName(clip));
    row.insert(QStringLiteral("take"), QString::fromStdString(clip.take));
    row.insert(QStringLiteral("muted"), clip.muted);
    row.insert(QStringLiteral("loop"), clip.loop);
    row.insert(QStringLiteral("selected"), static_cast<int>(index) == selectedClip_);
    row.insert(QStringLiteral("wave"), clipWave(clip));
    list.push_back(row);
  }
  return list;
}

QString ArrangeViewModel::songModeText() const {
  return settings_.songMode ? QStringLiteral("SONG ON") : QStringLiteral("SONG");
}

void ArrangeViewModel::setSongMode(bool on) {
  if (settings_.songMode == on) return;
  settings_.songMode = on;
  if (!on) {
    // Free mode is the legacy transport again: no song seek offset survives.
    state_.transportOffsetTicks.store(0);
  }
  if (on && settings_.song.sections.empty()) {
    arrange::ensureFirstSection(settings_.song);
    selectedSection_ = 0;
  }
  applySongToEngine();
  emit songModeChanged();
  emit arrangementChanged();
  emit loopChanged();
  notify(QStringLiteral("info"),
         on ? QStringLiteral("SONG ON - arrangement plays")
            : QStringLiteral("SONG OFF - free take loops"));
  emit songModeEditedByUser();
}

QString ArrangeViewModel::selectedSectionName() const {
  if (selectedSection_ < 0 ||
      selectedSection_ >= static_cast<int>(settings_.song.sections.size())) {
    return QStringLiteral("NO SECTION");
  }
  return QString::fromStdString(
      settings_.song.sections[static_cast<std::size_t>(selectedSection_)].name);
}

QString ArrangeViewModel::selectedClipName() const {
  const auto* clip = clipAt(selectedClip_);
  return clip == nullptr ? QString() : clipName(*clip);
}

bool ArrangeViewModel::hasClipSelection() const { return clipAt(selectedClip_) != nullptr; }

void ArrangeViewModel::setGrid(int grid) {
  const int clamped = clampGridIndex(grid);
  if (settings_.song.grid == clamped) return;
  settings_.song.grid = clamped;
  emit gridChanged();
  markChanged();
  setStatus(QStringLiteral("GRID %1").arg(gridText()));
}

QString ArrangeViewModel::gridText() const { return QString::fromStdString(arrange::gridText(grid())); }

void ArrangeViewModel::setLoopOn(bool on) {
  if (settings_.loopOn == on) return;
  settings_.loopOn = on;
  emit loopChanged();
  markChanged();
  setStatus(on ? QStringLiteral("LOOP ON") : QStringLiteral("LOOP OFF"));
}

double ArrangeViewModel::lengthBeats() const {
  return arrange::effectiveLengthBeats(settings_.song, settings_.songLengthBeats,
                                       lengthFn());
}

QString ArrangeViewModel::lengthText() const {
  return QString::fromStdString(arrange::lengthText(lengthBeats()));
}

double ArrangeViewModel::loopStartBeats() const { return effectiveLoopStartBeats(); }

double ArrangeViewModel::loopEndBeats() const { return effectiveLoopEndBeats(); }

double ArrangeViewModel::effectiveLoopStartBeats() const {
  return std::max(0.0, settings_.loopStartBeats);
}

double ArrangeViewModel::effectiveLoopEndBeats() const {
  return arrange::loopEndBeats(settings_.loopStartBeats, settings_.loopEndBeats,
                               lengthBeats());
}

QString ArrangeViewModel::loopText() const {
  if (!settings_.loopOn) return QStringLiteral("LOOP OFF");
  const double start = effectiveLoopStartBeats();
  const double end = effectiveLoopEndBeats();
  if (arrange::loopIsWholeSong(settings_.loopStartBeats, settings_.loopEndBeats,
                               lengthBeats())) {
    return QStringLiteral("LOOP SONG %1").arg(
        QString::fromStdString(arrange::lengthText(end)));
  }
  return QStringLiteral("LOOP %1 - %2")
      .arg(QString::fromStdString(arrange::positionBarsText(start)),
           QString::fromStdString(arrange::positionBarsText(end)));
}

QString ArrangeViewModel::playheadText() const {
  const auto tick = static_cast<std::uint32_t>(
      std::llround(playheadBeats_ / arrange::kBeatsPerBar *
                   static_cast<double>(kTicksPerBeat)));
  return QString::fromStdString(formatBarBeatTick(tick));
}

QString ArrangeViewModel::clipMenuTitle() const {
  const auto* clip = clipAt(clipMenuIndex_);
  if (clip == nullptr) return QStringLiteral("CLIP");
  return QStringLiteral("CLIP %1 - TRACK %2 %3")
      .arg(clipName(*clip))
      .arg(clip->track + 1, 2, 10, QLatin1Char('0'))
      .arg(trackName(clip->track));
}

QStringList ArrangeViewModel::clipActions() const {
  return {QStringLiteral("OPEN EDIT"), QStringLiteral("MOVE"),
          QStringLiteral("TRIM"),      QStringLiteral("DUPLICATE"),
          QStringLiteral("GAIN/FADE"), QStringLiteral("MUTE"),
          QStringLiteral("DELETE")};
}

bool ArrangeViewModel::clipMuted() const {
  const auto* clip = clipAt(clipMenuIndex_);
  return clip != nullptr && clip->muted;
}

double ArrangeViewModel::clipGainDb() const {
  const auto* clip = clipAt(clipMenuIndex_);
  return clip == nullptr ? 0.0 : clip->gainDb;
}

double ArrangeViewModel::clipFadeInBeats() const {
  const auto* clip = clipAt(clipMenuIndex_);
  return clip == nullptr ? 0.0 : clip->fadeInBeats;
}

double ArrangeViewModel::clipFadeOutBeats() const {
  const auto* clip = clipAt(clipMenuIndex_);
  return clip == nullptr ? 0.0 : clip->fadeOutBeats;
}

QString ArrangeViewModel::clipPositionText() const {
  const auto* clip = clipAt(clipMenuIndex_);
  if (clip == nullptr) return QString();
  return QStringLiteral("POS %1 bars")
      .arg(QString::number(std::max(0.0, clip->startBeats) / arrange::kBeatsPerBar,
                           'f', 1));
}

QString ArrangeViewModel::clipLengthText() const {
  const auto* clip = clipAt(clipMenuIndex_);
  if (clip == nullptr) return QString();
  return QStringLiteral("LEN %1")
      .arg(QString::fromStdString(
          arrange::lengthText(arrange::clipLengthBeats(*clip, lengthFn()))));
}

QString ArrangeViewModel::defaultSectionName() const {
  return QStringLiteral("SECTION %1").arg(newSectionNumber());
}

int ArrangeViewModel::newSectionNumber() const {
  return static_cast<int>(settings_.song.sections.size()) + 1;
}

void ArrangeViewModel::markChanged() {
  clampSelection();
  emit arrangementChanged();
  emit selectionChanged();
}

void ArrangeViewModel::setStatus(const QString& text) {
  if (text == statusText_) return;
  statusText_ = text;
  emit statusChanged();
}

void ArrangeViewModel::notify(const QString& kind, const QString& text) {
  emit noticeRequested(kind, text);
}

void ArrangeViewModel::emitSelection() {
  emit selectionChanged();
  if (clipMenuOpen_) emit clipMenuChanged();
}

void ArrangeViewModel::clampSelection() {
  if (settings_.song.sections.empty()) {
    selectedSection_ = -1;
  } else {
    selectedSection_ = std::clamp(
        selectedSection_, 0, static_cast<int>(settings_.song.sections.size()) - 1);
  }
  if (selectedClip_ < 0 ||
      selectedClip_ >= static_cast<int>(settings_.song.clips.size())) {
    selectedClip_ = -1;
  }
  if (clipMenuIndex_ < 0 ||
      clipMenuIndex_ >= static_cast<int>(settings_.song.clips.size())) {
    clipMenuIndex_ = -1;
    if (clipMenuOpen_) {
      clipMenuOpen_ = false;
      emit clipMenuChanged();
    }
  }
}

void ArrangeViewModel::addSection() {
  const int index = arrange::addSection(settings_.song, defaultSectionName().toStdString(),
                                        arrange::kDefaultSectionBars);
  selectedSection_ = index;
  const double end = arrange::sectionsEndBeats(settings_.song.sections);
  if (end > settings_.songLengthBeats) settings_.songLengthBeats = end;
  markChanged();
  emit loopChanged();
  applySongToEngine();
  setStatus(QStringLiteral("SECTION ADDED - %1").arg(selectedSectionName()));
  notify(QStringLiteral("info"),
         QStringLiteral("SECTION ADDED - %1 %2 bars")
             .arg(selectedSectionName())
             .arg(settings_.song.sections[static_cast<std::size_t>(index)].bars));
}

void ArrangeViewModel::duplicateSection() {
  if (settings_.song.sections.empty()) {
    addSection();
    return;
  }
  const int index = arrange::duplicateSection(
      settings_.song, selectedSection_, lengthFn());
  if (index < 0) {
    notify(QStringLiteral("warn"), QStringLiteral("select a section to duplicate"));
    return;
  }
  const int bars = settings_.song.sections[static_cast<std::size_t>(index)].bars;
  selectedSection_ = index;
  const double end = arrange::sectionsEndBeats(settings_.song.sections);
  if (end > settings_.songLengthBeats) settings_.songLengthBeats = end;
  markChanged();
  emit loopChanged();
  applySongToEngine();
  setStatus(QStringLiteral("SECTION DUPLICATED - %1").arg(selectedSectionName()));
  notify(QStringLiteral("info"),
         QStringLiteral("SECTION DUPLICATED - %1 +%2 bars")
             .arg(selectedSectionName())
             .arg(bars));
}

void ArrangeViewModel::selectSection(int index) {
  if (index < 0 || index >= static_cast<int>(settings_.song.sections.size())) return;
  selectedSection_ = index;
  // Once a loop is armed on a section, tapping another section moves the loop
  // with the selection; the whole-song default keeps tap = select only.
  if (settings_.loopOn &&
      !arrange::loopIsWholeSong(settings_.loopStartBeats, settings_.loopEndBeats,
                                lengthBeats())) {
    const auto span = arrange::sectionSpans(settings_.song.sections)
                          [static_cast<std::size_t>(index)];
    settings_.loopStartBeats = span.startBeats;
    settings_.loopEndBeats = span.endBeats;
    emit loopChanged();
  }
  emitSelection();
  setStatus(QStringLiteral("SECTION %1 - %2 bars")
                .arg(selectedSectionName())
                .arg(settings_.song.sections[static_cast<std::size_t>(index)].bars));
}

void ArrangeViewModel::loopSection() {
  if (settings_.song.sections.empty()) {
    notify(QStringLiteral("warn"), QStringLiteral("add a section before looping one"));
    return;
  }
  const auto span = arrange::sectionSpans(settings_.song.sections)
                        [static_cast<std::size_t>(selectedSection_)];
  settings_.loopOn = true;
  settings_.loopStartBeats = span.startBeats;
  settings_.loopEndBeats = span.endBeats;
  emit loopChanged();
  markChanged();
  notify(QStringLiteral("info"),
         QStringLiteral("LOOP SECTION %1 - %2 bars")
             .arg(selectedSectionName())
             .arg(span.bars));
}

void ArrangeViewModel::renameSection(const QString& name) {
  if (!arrange::renameSection(settings_.song, selectedSection_,
                              name.toStdString())) {
    notify(QStringLiteral("warn"),
           QStringLiteral("section name needs 1-16 printable ASCII characters"));
    return;
  }
  markChanged();
  setStatus(QStringLiteral("SECTION RENAMED - %1").arg(selectedSectionName()));
}

void ArrangeViewModel::selectClip(int index) {
  if (clipAt(index) == nullptr) return;
  selectedClip_ = index;
  const auto* clip = clipAt(index);
  selectedSection_ = arrange::sectionAtBeat(settings_.song.sections,
                                            std::max(0.0, clip->startBeats));
  emitSelection();
  setStatus(QStringLiteral("CLIP %1 - %2")
                .arg(clipName(*clip), QStringLiteral("selected")));
}

void ArrangeViewModel::openClipMenu(int index) {
  if (clipAt(index) == nullptr) return;
  selectedClip_ = index;
  clipMenuIndex_ = index;
  clipMenuPane_ = 0;
  clipMenuOpen_ = true;
  const auto* clip = clipAt(index);
  selectedSection_ = arrange::sectionAtBeat(settings_.song.sections,
                                            std::max(0.0, clip->startBeats));
  emitSelection();
  emit clipMenuChanged();
}

void ArrangeViewModel::closeClipMenu() {
  if (!clipMenuOpen_) return;
  clipMenuOpen_ = false;
  emit clipMenuChanged();
}

void ArrangeViewModel::showClipMenuActions() {
  if (!clipMenuOpen_ || clipMenuPane_ == 0) return;
  clipMenuPane_ = 0;
  emit clipMenuChanged();
}

void ArrangeViewModel::clipMenuAction(int action) {
  const int index = clipMenuIndex_;
  const auto* clip = clipAt(index);
  if (clip == nullptr) {
    closeClipMenu();
    return;
  }
  const QString name = clipName(*clip);
  switch (action) {
    case 0:  // OPEN EDIT (the W3 take edit popup)
      closeClipMenu();
      emit editTakeRequested(clip->track, QString::fromStdString(clip->take));
      break;
    case 1:  // MOVE: the touch drag and the panel knob share POS
      closeClipMenu();
      setStatus(QStringLiteral("MOVE %1 - drag the clip or turn POS").arg(name));
      notify(QStringLiteral("info"),
             QStringLiteral("MOVE %1 - drag the clip or turn POS").arg(name));
      break;
    case 2:  // TRIM: the edge drags and SHIFT+knob share LEN
      closeClipMenu();
      setStatus(QStringLiteral("TRIM %1 - drag an edge or SHIFT+turn LEN").arg(name));
      notify(QStringLiteral("info"),
             QStringLiteral("TRIM %1 - drag an edge or SHIFT+turn LEN").arg(name));
      break;
    case 3:  // DUPLICATE
      closeClipMenu();
      duplicateClipAt(index);
      break;
    case 4:  // GAIN/FADE pane
      clipMenuPane_ = 1;
      emit clipMenuChanged();
      break;
    case 5:  // MUTE
      setClipMuted(index, !clip->muted);
      emit clipMenuChanged();
      break;
    case 6:  // DELETE (AppController confirms first)
      closeClipMenu();
      emit deleteClipRequested(index);
      break;
    default:
      break;
  }
}

void ArrangeViewModel::applyClipMenuValues() {
  emit clipMenuChanged();
}

void ArrangeViewModel::setClipGainDb(double gainDb) {
  auto* clip = clipAt(clipMenuIndex_);
  if (clip == nullptr) return;
  const float clamped = static_cast<float>(std::clamp(gainDb, -96.0, 12.0));
  if (std::abs(clamped - clip->gainDb) < 0.001f) return;
  clip->gainDb = clamped;
  applyClipMenuValues();
  markChanged();
  applySongToEngine();
}

void ArrangeViewModel::setClipFadeIn(double beats) {
  auto* clip = clipAt(clipMenuIndex_);
  if (clip == nullptr) return;
  const double length = arrange::clipLengthBeats(*clip, lengthFn());
  const double clamped = std::clamp(beats, 0.0, std::max(0.0, length));
  if (std::abs(clamped - clip->fadeInBeats) < 0.001) return;
  clip->fadeInBeats = clamped;
  applyClipMenuValues();
  markChanged();
  applySongToEngine();
}

void ArrangeViewModel::setClipFadeOut(double beats) {
  auto* clip = clipAt(clipMenuIndex_);
  if (clip == nullptr) return;
  const double length = arrange::clipLengthBeats(*clip, lengthFn());
  const double clamped = std::clamp(beats, 0.0, std::max(0.0, length));
  if (std::abs(clamped - clip->fadeOutBeats) < 0.001) return;
  clip->fadeOutBeats = clamped;
  applyClipMenuValues();
  markChanged();
  applySongToEngine();
}

void ArrangeViewModel::nudgeClipPosition(int steps) {
  const auto* clip = clipAt(selectedClip_);
  if (clip == nullptr || steps == 0) return;
  const double delta = steps * arrange::gridStepBeats(grid());
  std::string error;
  if (!arrange::moveClip(settings_.song, selectedClip_,
                         std::max(0.0, clip->startBeats + delta), lengthFn(),
                         &error)) {
    notify(QStringLiteral("warn"), QString::fromStdString(error));
    return;
  }
  markChanged();
  applySongToEngine();
  const auto* moved = clipAt(selectedClip_);
  setStatus(QStringLiteral("POS %1 bars")
                .arg(QString::number(
                    std::max(0.0, moved->startBeats) / arrange::kBeatsPerBar, 'f',
                    1)));
}

void ArrangeViewModel::nudgeClipLength(int steps) {
  const auto* clip = clipAt(selectedClip_);
  if (clip == nullptr || steps == 0) return;
  const double delta = steps * arrange::gridStepBeats(grid());
  const double end = arrange::clipEndBeats(*clip, lengthFn()) + delta;
  std::string error;
  if (!arrange::trimClipEnd(settings_.song, selectedClip_, end, lengthFn(),
                            &error)) {
    notify(QStringLiteral("warn"), QString::fromStdString(error));
    return;
  }
  markChanged();
  applySongToEngine();
  const auto* trimmed = clipAt(selectedClip_);
  setStatus(QStringLiteral("LEN %1")
                .arg(QString::fromStdString(
                    arrange::lengthText(arrange::clipLengthBeats(*trimmed,
                                                                 lengthFn())))));
}

QString ArrangeViewModel::clipNameAt(int index) const {
  const auto* clip = clipAt(index);
  return clip == nullptr ? QString() : clipName(*clip);
}

void ArrangeViewModel::duplicateClipAt(int index) {
  std::string error;
  const int copy = arrange::duplicateClip(settings_.song, index, grid(), lengthFn(),
                                          &error);
  if (copy < 0) {
    notify(QStringLiteral("warn"), QString::fromStdString(error));
    return;
  }
  selectedClip_ = copy;
  const auto* clip = clipAt(copy);
  markChanged();
  applySongToEngine();
  setStatus(QStringLiteral("CLIP DUPLICATED - %1 at bar %2")
                .arg(clipName(*clip))
                .arg(arrange::barOfBeat(clip->startBeats)));
  notify(QStringLiteral("info"),
         QStringLiteral("CLIP DUPLICATED - %1 at bar %2")
             .arg(clipName(*clip))
             .arg(arrange::barOfBeat(clip->startBeats)));
}

void ArrangeViewModel::deleteClipAt(int index) {
  const auto* clip = clipAt(index);
  if (clip == nullptr) return;
  const QString name = clipName(*clip);
  if (!arrange::deleteClip(settings_.song, index)) return;
  if (selectedClip_ == index) selectedClip_ = -1;
  else if (selectedClip_ > index) --selectedClip_;
  if (clipMenuIndex_ == index) clipMenuIndex_ = -1;
  else if (clipMenuIndex_ > index) --clipMenuIndex_;
  markChanged();
  applySongToEngine();
  setStatus(QStringLiteral("CLIP DELETED - %1").arg(name));
  notify(QStringLiteral("info"),
         QStringLiteral("CLIP DELETED - %1 (take stays on disk)").arg(name));
}

void ArrangeViewModel::setClipMuted(int index, bool muted) {
  auto* clip = clipAt(index);
  if (clip == nullptr || clip->muted == muted) return;
  clip->muted = muted;
  applySongToEngine();
  markChanged();
  if (clipMenuOpen_) emit clipMenuChanged();
  notify(QStringLiteral("info"),
         muted ? QStringLiteral("CLIP MUTED - %1").arg(clipName(*clip))
               : QStringLiteral("CLIP UNMUTED - %1").arg(clipName(*clip)));
}

void ArrangeViewModel::beginClipDrag(int index, int edge) {
  const auto* clip = clipAt(index);
  if (clip == nullptr) return;
  selectedClip_ = index;
  dragging_ = true;
  dragIndex_ = index;
  dragEdge_ = std::clamp(edge, 0, 2);
  dragOriginal_ = *clip;
  dragMoved_ = false;
  dragError_.clear();
  emitSelection();
  setStatus(dragEdge_ == 0 ? QStringLiteral("MOVE %1").arg(clipName(*clip))
                           : QStringLiteral("TRIM %1").arg(clipName(*clip)));
}

void ArrangeViewModel::updateClipDrag(double beat) {
  if (!dragging_) return;
  auto working = settings_.song;
  const double snapped = arrange::snapBeats(beat, grid());
  std::string error;
  bool ok = false;
  if (dragEdge_ == 0) {
    ok = arrange::moveClip(working, dragIndex_, snapped, lengthFn(), &error);
  } else if (dragEdge_ == 1) {
    ok = arrange::trimClipStart(working, dragIndex_, snapped, lengthFn(), &error);
  } else {
    ok = arrange::trimClipEnd(working, dragIndex_, snapped, lengthFn(), &error);
  }
  if (!ok) {
    dragError_ = QString::fromStdString(error);
    return;
  }
  settings_.song = std::move(working);
  dragMoved_ = true;
  dragError_.clear();
  markChanged();
  const auto* clip = clipAt(dragIndex_);
  if (clip != nullptr) {
    if (dragEdge_ == 0) {
      setStatus(QStringLiteral("POS %1 bars")
                    .arg(QString::number(
                        std::max(0.0, clip->startBeats) / arrange::kBeatsPerBar,
                        'f', 1)));
    } else {
      setStatus(QStringLiteral("LEN %1")
                    .arg(QString::fromStdString(
                        arrange::lengthText(arrange::clipLengthBeats(*clip,
                                                                     lengthFn())))));
    }
  }
}

void ArrangeViewModel::commitClipDrag(int index, int edge, double beat) {
  if (!dragging_ || dragIndex_ != index || dragEdge_ != edge) {
    beginClipDrag(index, edge);
  }
  updateClipDrag(beat);
  endClipDrag();
}

void ArrangeViewModel::endClipDrag() {
  if (!dragging_) return;
  dragging_ = false;
  if (dragMoved_) {
    applySongToEngine();
    applyClipMenuValues();
  } else if (!dragError_.isEmpty()) {
    notify(QStringLiteral("warn"), dragError_);
  }
  dragIndex_ = -1;
  dragEdge_ = 0;
  dragMoved_ = false;
  dragError_.clear();
}

int ArrangeViewModel::placePunchTake(int track, const std::string& takeKey,
                                     double startBeats, std::string* error) {
  if (takeKey.empty()) {
    if (error != nullptr) *error = "the punch wrote no take";
    return -1;
  }
  session::SongClip clip;
  clip.track = std::clamp(track, 0, audio::kMaxTracks - 1);
  clip.startBeats = arrange::snapBeats(std::max(0.0, startBeats), grid());
  clip.take = takeKey;
  if (!arrange::addClip(settings_.song, clip, lengthFn(), error)) return -1;
  selectedClip_ = static_cast<int>(settings_.song.clips.size()) - 1;
  selectedSection_ = arrange::sectionAtBeat(settings_.song.sections,
                                            clip.startBeats);
  markChanged();
  applySongToEngine();
  return selectedClip_;
}

void ArrangeViewModel::seekToBeat(double beats) {
  const double snapped = arrange::snapBeats(std::max(0.0, beats), grid());
  const double targetTick = snapped / arrange::kBeatsPerBar *
                            static_cast<double>(kTicksPerBeat);
  const auto target = static_cast<std::int64_t>(std::llround(targetTick));
  const auto raw = static_cast<std::int64_t>(state_.tick.load());
  state_.transportOffsetTicks.store(target - raw);
  playheadBeats_ = beatsOfTick(transportTick(state_.tick.load(),
                                             state_.transportOffsetTicks.load()));
  if (playheadBeats_ < viewStartBeats_ ||
      playheadBeats_ > viewStartBeats_ + viewBeats_) {
    viewStartBeats_ = std::max(0.0, playheadBeats_ - viewBeats_ * 0.25);
    emit viewChanged();
  }
  setStatus(QStringLiteral("PLAYHEAD %1").arg(
      QString::fromStdString(formatBarBeatTick(static_cast<std::uint32_t>(
          std::llround(snapped / arrange::kBeatsPerBar *
                       static_cast<double>(kTicksPerBeat)))))));
  emit playheadChanged();
}

double ArrangeViewModel::beatAtRatio(double ratio) const {
  return std::max(0.0, viewStartBeats_ +
                           std::clamp(ratio, 0.0, 1.0) * viewBeats_);
}

double ArrangeViewModel::ratioOfBeat(double beats) const {
  if (!(viewBeats_ > 0.0)) return 0.0;
  return (beats - viewStartBeats_) / viewBeats_;
}

QString ArrangeViewModel::beatTextAt(double beats) const {
  const auto tick = static_cast<std::uint32_t>(
      std::llround(std::max(0.0, beats) / arrange::kBeatsPerBar *
                   static_cast<double>(kTicksPerBeat)));
  return QString::fromStdString(formatBarBeatTick(tick));
}

void ArrangeViewModel::zoomIn() { zoomBy(2.0); }

void ArrangeViewModel::zoomOut() { zoomBy(0.5); }

void ArrangeViewModel::zoomBy(double factor) {
  if (!(factor > 0.0) || !std::isfinite(factor)) return;
  const double anchor = std::clamp(playheadBeats_, viewStartBeats_,
                                   viewStartBeats_ + viewBeats_);
  const double ratio = viewBeats_ > 0.0 ? (anchor - viewStartBeats_) / viewBeats_ : 0.5;
  const double minimum = 8.0;
  const double maximum = 256.0;
  const double next = std::clamp(viewBeats_ / factor, minimum, maximum);
  if (std::abs(next - viewBeats_) < 0.001) return;
  viewBeats_ = next;
  viewStartBeats_ = std::max(0.0, anchor - ratio * viewBeats_);
  emit viewChanged();
  setStatus(QStringLiteral("ZOOM %1 bars across").arg(
      QString::fromStdString(arrange::lengthText(viewBeats_))));
}

void ArrangeViewModel::setViewStartBeats(double beats) {
  const double next = std::max(0.0, beats);
  if (std::abs(next - viewStartBeats_) < 0.001) return;
  viewStartBeats_ = next;
  emit viewChanged();
}

void ArrangeViewModel::centerOnPlayhead() {
  viewStartBeats_ = std::max(0.0, playheadBeats_ - viewBeats_ / 2.0);
  emit viewChanged();
}

void ArrangeViewModel::openLengthPopup() {
  if (lengthPopupOpen_) return;
  lengthPopupOpen_ = true;
  emit lengthPopupChanged();
}

void ArrangeViewModel::closeLengthPopup() {
  if (!lengthPopupOpen_) return;
  lengthPopupOpen_ = false;
  emit lengthPopupChanged();
}

void ArrangeViewModel::adjustLengthBars(int deltaBars) {
  if (deltaBars == 0) return;
  const double current = lengthBeats();
  const double next = arrange::snapLength(
      current + static_cast<double>(deltaBars) * arrange::kBeatsPerBar, grid());
  settings_.songLengthBeats = std::max(arrange::kBeatsPerBar, next);
  emit arrangementChanged();
  emit loopChanged();
  markChanged();
  setStatus(QStringLiteral("LEN %1")
                .arg(QString::fromStdString(
                    arrange::lengthText(settings_.songLengthBeats))));
}

void ArrangeViewModel::adjustLengthBeats(int deltaBeats) {
  if (deltaBeats == 0) return;
  const double step = arrange::gridStepBeats(grid());
  const double current = lengthBeats();
  const double next = arrange::snapLength(
      current + static_cast<double>(deltaBeats) * step, grid());
  settings_.songLengthBeats = std::max(arrange::kBeatsPerBar, next);
  emit arrangementChanged();
  emit loopChanged();
  markChanged();
  setStatus(QStringLiteral("LEN %1")
                .arg(QString::fromStdString(
                    arrange::lengthText(settings_.songLengthBeats))));
}

int ArrangeViewModel::neighborClip(int index, int dx, int dy) const {
  const auto* current = clipAt(index);
  if (current == nullptr) {
    return settings_.song.clips.empty() ? -1 : 0;
  }
  if (dy != 0) {
    std::set<int> tracks;
    for (const auto& clip : settings_.song.clips) tracks.insert(clip.track);
    tracks.insert(current->track);
    std::vector<int> ordered(tracks.begin(), tracks.end());
    const auto found = std::find(ordered.begin(), ordered.end(), current->track);
    if (found == ordered.end()) return -1;
    int position = static_cast<int>(std::distance(ordered.begin(), found)) +
                   (dy > 0 ? 1 : -1);
    if (position < 0 || position >= static_cast<int>(ordered.size())) return -1;
    const int targetTrack = ordered[static_cast<std::size_t>(position)];
    int best = -1;
    double bestDistance = 0.0;
    for (std::size_t candidate = 0; candidate < settings_.song.clips.size();
         ++candidate) {
      const auto& clip = settings_.song.clips[candidate];
      if (clip.track != targetTrack) continue;
      const double distance = std::abs(clip.startBeats - current->startBeats);
      if (best < 0 || distance < bestDistance) {
        best = static_cast<int>(candidate);
        bestDistance = distance;
      }
    }
    return best;
  }
  if (dx == 0) return index;
  int best = -1;
  double bestDistance = 0.0;
  for (std::size_t candidate = 0; candidate < settings_.song.clips.size();
       ++candidate) {
    if (static_cast<int>(candidate) == index) continue;
    const auto& clip = settings_.song.clips[candidate];
    if (clip.track != current->track) continue;
    const double delta = clip.startBeats - current->startBeats;
    if (dx > 0 ? delta <= 0.0 : delta >= 0.0) continue;
    const double distance = std::abs(delta);
    if (best < 0 || distance < bestDistance) {
      best = static_cast<int>(candidate);
      bestDistance = distance;
    }
  }
  if (best >= 0) return best;
  // Wrap to the adjacent lane so NAV always reaches every clip.
  const int wrapped = neighborClip(index, 0, dx > 0 ? 1 : -1);
  return wrapped;
}

void ArrangeViewModel::noteLoopWrap() {
  ++loopFlash_;
  emit loopChanged();
}

void ArrangeViewModel::resetView() {
  viewStartBeats_ = 0.0;
  viewBeats_ = 64.0;
  emit viewChanged();
}

void ArrangeViewModel::applySongToEngine() {
  int skipped = 0;
  bool appliedAny = false;
  for (int track = 0; track < audio::kMaxTracks; ++track) {
    std::vector<audio::SongClip> clips;
    for (const auto& clip : settings_.song.clips) {
      if (clip.track != track) continue;
      const auto path = takePath(clip);
      if (path.empty()) {
        ++skipped;
        continue;
      }
      audio::SongClip applied;
      applied.track = clip.track;
      applied.startBeats = std::max(0.0, clip.startBeats);
      applied.take = path.string();
      applied.offsetBeats = clip.offsetBeats;
      applied.lengthBeats = clip.lengthBeats;
      applied.gainDb = clip.gainDb;
      applied.fadeInBeats = clip.fadeInBeats;
      applied.fadeOutBeats = clip.fadeOutBeats;
      applied.normalize = clip.normalize;
      applied.loop = clip.loop;
      applied.muted = clip.muted;
      clips.push_back(std::move(applied));
    }
    if (clips.empty()) {
      audio_.clearSongClips(track);
      continue;
    }
    std::string error;
    if (!audio_.setSongClips(track, clips, &error)) {
      log(std::string("arrange: track ") + std::to_string(track + 1) +
          " lane refused: " + error);
      audio_.clearSongClips(track);
    } else {
      appliedAny = true;
    }
  }
  // The engine clip scheduler only takes over when at least one lane resolved.
  // An empty (or entirely missing-take) SONG ON project keeps the
  // Active-take path audible instead of silently playing nothing.
  audio_.setSongMode(settings_.songMode && appliedAny);
  if (skipped > 0 && settings_.songMode) {
    notify(QStringLiteral("warn"),
           QStringLiteral("%1 clip(s) skipped - take file missing").arg(skipped));
  }
}

void ArrangeViewModel::refresh() {
  const auto tick = transportTick(state_.tick.load(),
                                  state_.transportOffsetTicks.load());
  const double beats = beatsOfTick(tick);
  if (std::abs(beats - playheadBeats_) > 0.001) {
    playheadBeats_ = beats;
    emit playheadChanged();
  }
  refreshTempo();
}

void ArrangeViewModel::refreshTempo() {
  const int tempo = state_.tempoX100.load();
  if (tempo == lastTransportTempoX100_) return;
  lastTransportTempoX100_ = tempo;
  // The engine resolves clip beats at the tempo of the apply call; a tempo
  // change re-applies every lane like take regions/loops do.
  applySongToEngine();
  markChanged();
}

void ArrangeViewModel::reloadFromSettings() {
  lastTransportTempoX100_ = state_.tempoX100.load();
  selectedSection_ = settings_.song.sections.empty() ? -1 : 0;
  selectedClip_ = -1;
  clipMenuOpen_ = false;
  clipMenuIndex_ = -1;
  lengthPopupOpen_ = false;
  waveCache_.clear();
  playheadBeats_ = beatsOfTick(transportTick(
      state_.tick.load(), state_.transportOffsetTicks.load()));
  applySongToEngine();
  emit arrangementChanged();
  emit selectionChanged();
  emit gridChanged();
  emit viewChanged();
  emit playheadChanged();
  emit loopChanged();
  emit clipMenuChanged();
  emit lengthPopupChanged();
}


}  // namespace mnky::app
