#include "recorder_view_model.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <system_error>
#include <utility>

#include "audio/peaks.hpp"
#include "logic.hpp"
#include "logging.hpp"
#include "project_logic.hpp"
#include "timeline_model.hpp"
#include "wav_info.hpp"

namespace mnky::app {
namespace {

std::uint64_t steadyNowMs() {
  using namespace std::chrono;
  return static_cast<std::uint64_t>(
      duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

std::string channelFileName(int track) {
  int display = track;
  if (display < 0) display = 0;
  std::string out = std::to_string(display);
  if (out.size() < 2) out.insert(out.begin(), '0');
  return "ch" + out + ".wav";
}

QString trackNameOf(const MixerModel& mixer, int track) {
  return QString::fromStdString(mixer.trackName(track));
}

std::string keyForPath(const std::filesystem::path& file,
                       const std::filesystem::path& base) {
  const auto relative = file.lexically_relative(base);
  const auto text = relative.generic_string();
  if (!relative.empty() && text.rfind("..", 0) != 0) return text;
  std::error_code error;
  const auto absolute = std::filesystem::absolute(file, error);
  if (error) return file.generic_string();
  return absolute.generic_string();
}

int nextTakeNumber(const std::filesystem::path& base, int track) {
  int maximum = 0;
  std::error_code error;
  for (std::filesystem::recursive_directory_iterator it(base, error), end; it != end;
       it.increment(error)) {
    if (error) break;
    std::error_code typeError;
    if (!it->is_regular_file(typeError)) continue;
    if (it->path().extension() != ".wav") continue;
    const auto parts = parseTakeFileName(it->path().filename().string());
    if (!parts.valid || parts.track != track) continue;
    maximum = std::max(maximum, parts.number);
  }
  return maximum;
}

bool isWavFile(const std::filesystem::path& path) {
  auto extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return extension == ".wav";
}

}  // namespace

RecorderArmModel::RecorderArmModel(MixerModel& mixer, QObject* parent)
    : QAbstractListModel(parent), mixer_(mixer) {
  for (int i = 0; i < kNumStrips; ++i) {
    armed_[static_cast<std::size_t>(i)] = mixer_.arm(i);
    names_[static_cast<std::size_t>(i)] = trackNameOf(mixer_, i);
  }
}

int RecorderArmModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : kNumStrips;
}

QVariant RecorderArmModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= kNumStrips) return {};
  if (role == IndexRole) return index.row();
  if (role == ArmedRole) return armed_[static_cast<std::size_t>(index.row())];
  if (role == NameRole) return names_[static_cast<std::size_t>(index.row())];
  return {};
}

QHash<int, QByteArray> RecorderArmModel::roleNames() const {
  return {{IndexRole, "index"}, {ArmedRole, "armed"}, {NameRole, "name"}};
}

void RecorderArmModel::refresh() {
  for (int i = 0; i < kNumStrips; ++i) {
    const bool armed = mixer_.arm(i);
    const QString name = trackNameOf(mixer_, i);
    if (armed_[static_cast<std::size_t>(i)] != armed ||
        names_[static_cast<std::size_t>(i)] != name) {
      armed_[static_cast<std::size_t>(i)] = armed;
      names_[static_cast<std::size_t>(i)] = name;
      emit dataChanged(index(i, 0), index(i, 0), {ArmedRole, NameRole});
    }
  }
}

RecorderTakeModel::RecorderTakeModel(QObject* parent) : QAbstractListModel(parent) {}

int RecorderTakeModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(entries_.size());
}

QVariant RecorderTakeModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 ||
      index.row() >= static_cast<int>(entries_.size())) {
    return {};
  }
  const auto& entry = entries_[static_cast<std::size_t>(index.row())];
  switch (role) {
    case NameRole:
      return entry.name;
    case FilesRole:
      return entry.files;
    case DurationRole:
      return QString::fromStdString(formatDuration(entry.seconds));
    case BytesRole:
      return QString::fromStdString(formatBytes(static_cast<std::int64_t>(entry.bytes)));
    case PathRole:
      return QString::fromStdString(entry.path.string());
    case ActiveRole:
      return entry.active;
    case ImportedRole:
      return entry.imported;
    case WaveRole:
      return entry.wave;
    default:
      break;
  }
  return {};
}

QHash<int, QByteArray> RecorderTakeModel::roleNames() const {
  return {{NameRole, "name"},
          {FilesRole, "files"},
          {DurationRole, "duration"},
          {BytesRole, "bytes"},
          {PathRole, "path"},
          {ActiveRole, "active"},
          {ImportedRole, "imported"},
          {WaveRole, "wave"}};
}

void RecorderTakeModel::setEntries(std::vector<Entry> entries) {
  beginResetModel();
  entries_ = std::move(entries);
  endResetModel();
  emit countChanged();
}

const RecorderTakeModel::Entry* RecorderTakeModel::entry(int row) const {
  if (row < 0 || row >= static_cast<int>(entries_.size())) return nullptr;
  return &entries_[static_cast<std::size_t>(row)];
}

RecorderViewModel::RecorderViewModel(AppState& state, AudioGlue& audio, MixerModel& mixer,
                                     LinkClient& link, SessionSettings& settings,
                                     QObject* parent)
    : QObject(parent),
      state_(state),
      audio_(audio),
      mixer_(mixer),
      link_(link),
      settings_(settings),
      arms_(mixer, nullptr) {
  takeDirectory_ = QString::fromStdString(state_.takeDirectory().string());
  scanTakes();
  resyncActiveTakes();
  updateReadouts();
}

void RecorderViewModel::startStop() {
  // Amount 1 = immediate start/stop; the panel REC button uses 0 so it can
  // pair with PLAY inside the 300 ms chord window.
  state_.postIntent(Intent{Intent::Kind::RecToggle, 1});
}

void RecorderViewModel::setSelectedTrack(int track) {
  const int clamped = std::clamp(track, 0, audio::kMaxTracks - 1);
  if (clamped == selectedTrack_) return;
  selectedTrack_ = clamped;
  selectedRow_ = -1;
  scanTakes();
  updateReadouts();
  emit trackChanged();
  emit loopChanged();
  emit selectionChanged();
}

void RecorderViewModel::selectRow(int row) {
  const int clamped = row >= 0 && row < takes_.rowCount() ? row : -1;
  if (clamped == selectedRow_) return;
  selectedRow_ = clamped;
  emit selectionChanged();
}

QString RecorderViewModel::trackName() const {
  return QStringLiteral("TRACK %1 - %2")
      .arg(selectedTrack_ + 1, 2, 10, QLatin1Char('0'))
      .arg(trackDisplayName(selectedTrack_));
}

QString RecorderViewModel::activeTakePath(int track) const {
  if (track < 0 || track >= audio::kMaxTracks) return {};
  const auto key = settings_.activeTake(track);
  if (key.empty()) return {};
  const auto path = resolveTakeKey(track, key);
  return path.empty() ? QString() : QString::fromStdString(path.string());
}

std::filesystem::path RecorderViewModel::takePathForKey(int track,
                                                        const std::string& key) const {
  if (track < 0 || track >= audio::kMaxTracks || key.empty()) return {};
  return resolveTakeKey(track, key);
}

QString RecorderViewModel::trackDisplayName(int track) const {
  if (track < 0 || track >= audio::kMaxTracks) return {};
  // One identity everywhere (C2): the mixer's user name, with the default
  // chNN name as the fallback for an unnamed slot.
  return trackNameOf(mixer_, track);
}

QString RecorderViewModel::recordingTakeName() const {
  const auto mask = mixer_.armMask();
  const auto base = state_.takeDirectory();
  int first = -1;
  int count = 0;
  for (int track = 0; track < audio::kMaxTracks && track < MixerModel::kMaskBits; ++track) {
    if ((mask & (MixerModel::TrackMask{1} << static_cast<unsigned>(track))) == 0) continue;
    if (first < 0) first = track;
    ++count;
  }
  if (first < 0) return QStringLiteral("no armed track");
  std::string name = takeFileName(first, mixer_.trackName(first),
                                  nextTakeNumber(base, first) + 1);
  // The status names the take without its extension, exactly as the plan
  // writes it: `REC 00:04 - 02-bass-001 (POST+FX)`.
  if (name.size() > 4 && name.compare(name.size() - 4, 4, ".wav") == 0) {
    name.resize(name.size() - 4);
  }
  QString text = QString::fromStdString(name);
  // Several tracks record one file each; the status names the first and counts
  // the rest rather than inventing a combined name.
  if (count > 1) text += QStringLiteral(" +%1").arg(count - 1);
  return text;
}

QString RecorderViewModel::trackModeWord() const {
  return QString::fromStdString(::mnky::app::trackModeWord(mixer_.mode(selectedTrack_)));
}

QString RecorderViewModel::trackRecSourceText() const {
  return QString::fromStdString(recSourceText(mixer_.recSource(selectedTrack_)));
}

QString RecorderViewModel::activeTake() const {
  const auto active = settings_.activeTake(selectedTrack_);
  return active.empty() ? QStringLiteral("LIVE INPUT")
                        : QString::fromStdString(active);
}

session::LoopState RecorderViewModel::selectedLoop() const {
  return settings_.takeLoop[static_cast<std::size_t>(selectedTrack_)];
}

void RecorderViewModel::writeLoop(const session::LoopState& loop) {
  settings_.takeLoop[static_cast<std::size_t>(selectedTrack_)] = loop;
  audio_.applyTakeLoops(settings_);
  emit loopChanged();
}

bool RecorderViewModel::loopEnabled() const { return selectedLoop().enabled; }

void RecorderViewModel::setLoopEnabled(bool enabled) {
  auto loop = selectedLoop();
  if (loop.enabled == enabled) return;
  loop.enabled = enabled;
  writeLoop(loop);
}

double RecorderViewModel::loopStartBeats() const { return selectedLoop().startBeats; }

void RecorderViewModel::setLoopStartBeats(double beats) {
  auto loop = selectedLoop();
  const double value = loopBeats(beats);
  if (std::abs(loop.startBeats - value) < 1e-9) return;
  loop.startBeats = value;
  writeLoop(loop);
}

double RecorderViewModel::loopEndBeats() const { return selectedLoop().endBeats; }

void RecorderViewModel::setLoopEndBeats(double beats) {
  auto loop = selectedLoop();
  const double value = loopBeats(beats);
  if (std::abs(loop.endBeats - value) < 1e-9) return;
  loop.endBeats = value;
  writeLoop(loop);
}

QString RecorderViewModel::loopStartText() const {
  return QString::number(selectedLoop().startBeats, 'f', 2);
}

QString RecorderViewModel::loopEndText() const {
  return QString::number(selectedLoop().endBeats, 'f', 2);
}

QString RecorderViewModel::loopText() const {
  const auto loop = selectedLoop();
  if (!loop.enabled) return QStringLiteral("OFF");
  if (loop.endBeats <= loop.startBeats) return QStringLiteral("WHOLE TAKE");
  return QStringLiteral("%1 - %2 b")
      .arg(loop.startBeats, 0, 'f', 2)
      .arg(loop.endBeats, 0, 'f', 2);
}

void RecorderViewModel::activateSelected() {
  const auto* entry = takes_.entry(selectedRow_);
  if (entry == nullptr) return;
  settings_.setActiveTake(selectedTrack_, entry->key.toStdString());
  syncActiveTake(selectedTrack_);
  scanTakes();
  updateReadouts();
  emit trackChanged();
  emit activeTakeChanged();
}

QString RecorderViewModel::activateRow(int row) {
  if (takes_.entry(row) == nullptr) return {};
  selectRow(row);
  // Copy the display name first: activateSelected() rescans the take folder and
  // replaces the model's storage, so any Entry pointer is dangling afterwards.
  const QString name = takeNameAt(row);
  activateSelected();
  return name;
}

void RecorderViewModel::clearActive() {
  settings_.setActiveTake(selectedTrack_, {});
  syncActiveTake(selectedTrack_);
  scanTakes();
  updateReadouts();
  emit trackChanged();
  emit activeTakeChanged();
}

QString RecorderViewModel::takeImportPreview(const QString& path) const {
  if (path.trimmed().isEmpty()) return QStringLiteral("enter a WAV path");
  std::filesystem::path candidate(path.toStdString());
  if (candidate.is_relative()) candidate = state_.takeDirectory() / candidate;
  std::error_code error;
  if (!std::filesystem::is_regular_file(candidate, error)) {
    return QStringLiteral("no such file: ") + QString::fromStdString(candidate.string());
  }
  if (!isWavFile(candidate)) return QStringLiteral("import needs a .wav file");
  const WavInfo info = readWavInfo(candidate);
  const std::string rateError = importSampleRateError(info.ok ? info.sampleRate : 0);
  if (!rateError.empty()) return QString::fromStdString(rateError);
  if (info.channels == 0) return QStringLiteral("import refused: 0 channels");
  const double seconds = info.sampleRate > 0
                             ? static_cast<double>(info.frames) /
                                   static_cast<double>(info.sampleRate)
                             : 0.0;
  return QString::fromStdString(candidate.filename().string()) +
         QStringLiteral(" - 48 kHz - %1 ch - %2")
             .arg(info.channels)
             .arg(QString::fromStdString(formatDuration(seconds)));
}

QString RecorderViewModel::importTake(const QString& path) {
  if (path.trimmed().isEmpty()) return QStringLiteral("enter a WAV path");
  std::filesystem::path candidate(path.toStdString());
  if (candidate.is_relative()) candidate = state_.takeDirectory() / candidate;
  std::error_code error;
  if (!std::filesystem::is_regular_file(candidate, error)) {
    return QStringLiteral("no such file: ") + QString::fromStdString(candidate.string());
  }
  if (!isWavFile(candidate)) return QStringLiteral("import needs a .wav file");
  std::error_code absoluteError;
  const auto absolute = std::filesystem::absolute(candidate, absoluteError);
  if (absoluteError) return QStringLiteral("cannot resolve the path");
  const std::string key = absolute.generic_string();
  const bool known = std::any_of(imported_.begin(), imported_.end(),
                                 [&](const ImportedTake& entry) {
                                   return entry.key == key;
                                 });
  if (!known) {
    imported_.push_back(ImportedTake{selectedTrack_, absolute, key});
  }
  settings_.setActiveTake(selectedTrack_, key);
  syncActiveTake(selectedTrack_);
  scanTakes();
  updateReadouts();
  emit trackChanged();
  emit activeTakeChanged();
  return {};
}

QVariantList RecorderViewModel::takeWave(int row) const {
  const auto* entry = takes_.entry(row);
  return entry == nullptr ? QVariantList() : entry->wave;
}

QString RecorderViewModel::takeNameAt(int row) const {
  const auto* entry = takes_.entry(row);
  return entry == nullptr ? QString() : entry->name;
}

QString RecorderViewModel::renameSelected(const QString& name) {
  if (audio_.recording()) return QStringLiteral("stop the take before renaming");
  const auto* entry = takes_.entry(selectedRow_);
  if (entry == nullptr) return QStringLiteral("select a take first");
  if (entry->imported) {
    return QStringLiteral("imported take is a pointer; rename the original file");
  }
  std::string finalName = name.toStdString();
  if (finalName.size() < 4 || finalName.substr(finalName.size() - 4) != ".wav") {
    finalName += ".wav";
  }
  const auto error = validateTakeName(finalName);
  if (!error.empty()) return QString::fromStdString(error);
  if (finalName == entry->path.filename().string()) return {};
  const auto target = entry->path.parent_path() / finalName;
  std::error_code existsError;
  if (std::filesystem::exists(target, existsError)) {
    return QStringLiteral("a take with that name already exists");
  }
  const bool wasActive = settings_.activeTake(selectedTrack_) == entry->key.toStdString();
  std::error_code moveError;
  std::filesystem::rename(entry->path, target, moveError);
  if (moveError) return QStringLiteral("rename failed (is the take open?)");
  const auto parts = parseTakeFileName(target.filename().string());
  const std::string key =
      parts.valid && parts.track == selectedTrack_
          ? target.filename().string()
          : keyForPath(target, state_.takeDirectory());
  if (wasActive) settings_.setActiveTake(selectedTrack_, key);
  if (wasActive) syncActiveTake(selectedTrack_);
  scanTakes();
  for (int row = 0; row < takes_.rowCount(); ++row) {
    if (const auto* candidate = takes_.entry(row); candidate && candidate->key.toStdString() == key) {
      selectRow(row);
      break;
    }
  }
  updateReadouts();
  emit trackChanged();
  if (wasActive) emit activeTakeChanged();
  return {};
}

void RecorderViewModel::deleteSelected() {
  if (audio_.recording()) return;
  const auto* entry = takes_.entry(selectedRow_);
  if (entry == nullptr) return;
  const bool wasActive = settings_.activeTake(selectedTrack_) == entry->key.toStdString();
  const auto path = entry->path;
  const std::string key = entry->key.toStdString();
  std::error_code error;
  std::filesystem::remove(path, error);
  if (error) {
    statusText_ = QStringLiteral("delete failed: ") +
                  QString::fromStdString(path.filename().string());
    emit readoutsChanged();
    return;
  }
  imported_.erase(std::remove_if(imported_.begin(), imported_.end(),
                                 [&](const ImportedTake& item) { return item.key == key; }),
                  imported_.end());
  if (wasActive) settings_.setActiveTake(selectedTrack_, {});
  if (wasActive) syncActiveTake(selectedTrack_);
  scanTakes();
  updateReadouts();
  emit trackChanged();
  if (wasActive) emit activeTakeChanged();
}

void RecorderViewModel::removeSelected() {
  const auto* entry = takes_.entry(selectedRow_);
  if (entry == nullptr) return;
  const bool wasActive = settings_.activeTake(selectedTrack_) == entry->key.toStdString();
  if (wasActive) {
    // Project-only removal: the file stays on disk, so nothing is deleted and
    // the take list would still list it from the folder scan - clear the
    // pointer and say what happened instead of pretending the row vanished.
    settings_.setActiveTake(selectedTrack_, {});
    syncActiveTake(selectedTrack_);
  }
  scanTakes();
  updateReadouts();
  emit trackChanged();
  if (wasActive) emit activeTakeChanged();
}

int RecorderViewModel::adoptRecordedTake(const std::string& takeDirectoryPath,
                                         MixerModel::TrackMask armMask,
                                         std::string* lastKey) {
  const std::filesystem::path folder(takeDirectoryPath);
  const auto base = state_.takeDirectory();
  int renamed = 0;
  // One bit per track slot, up to the mask width: every armed slot's chNN.wav
  // is renamed, not just the first 18.
  for (int track = 0; track < audio::kMaxTracks; ++track) {
    if (track >= MixerModel::kMaskBits) break;
    if ((armMask & (MixerModel::TrackMask{1} << static_cast<unsigned>(track))) == 0) {
      continue;
    }
    const auto source = folder / channelFileName(track);
    std::error_code existsError;
    if (!std::filesystem::is_regular_file(source, existsError)) continue;
    int number = nextTakeNumber(base, track) + 1;
    // The user's track name travels into the take file name (C2); chNN stays
    // the fallback for an unnamed slot.
    const std::string takeName = mixer_.trackName(track);
    std::filesystem::path target = folder / takeFileName(track, takeName, number);
    std::error_code targetError;
    while (std::filesystem::exists(target, targetError)) {
      ++number;
      target = folder / takeFileName(track, takeName, number);
    }
    std::error_code moveError;
    std::filesystem::rename(source, target, moveError);
    if (moveError) continue;
    // Session v2 stores the take file name stem; per-track names are unique
    // because nextTakeNumber scans the whole take directory.
    settings_.setActiveTake(track, target.filename().string());
    if (lastKey != nullptr) *lastKey = target.filename().string();
    syncActiveTake(track);
    ++renamed;
  }
  if (renamed > 0) {
    lastAdoptedKey_ = lastKey != nullptr ? QString::fromStdString(*lastKey) : QString();
    scanTakes();
    updateReadouts();
    emit trackChanged();
    emit activeTakeChanged();
  }
  return renamed;
}

void RecorderViewModel::refresh() {
  arms_.refresh();
  updateReadouts();
  emit loopChanged();
  // Directory scanning hits the filesystem, so only every ~1.3 s.
  if (++refreshCounter_ % 20 == 0) scanTakes();
}

void RecorderViewModel::takeStateChanged() {
  scanTakes();
  updateReadouts();
}

void RecorderViewModel::tracksChanged() {
  arms_.refresh();
  updateReadouts();
  emit trackChanged();
}

std::filesystem::path RecorderViewModel::resolveTakeKey(int track,
                                                        const std::string& key) const {
  // The owning track is still the caller's question (the session stores one
  // active take per track), but it no longer constrains the file name: the
  // `NN-` prefix is convention only, so a take recorded on track 2 resolves on
  // track 9 after NEW TRACK FROM TAKE.
  (void)track;
  if (key.empty()) return {};
  const auto base = state_.takeDirectory();
  std::filesystem::path candidate(key);
  std::error_code error;
  if (candidate.is_absolute()) {
    return std::filesystem::is_regular_file(candidate, error) ? candidate
                                                              : std::filesystem::path{};
  }
  if (key.find('/') != std::string::npos) {
    const auto relative = base / candidate;
    return std::filesystem::is_regular_file(relative, error) ? relative
                                                             : std::filesystem::path{};
  }
  // Bare file name: search the take folders for the active take.
  for (std::filesystem::recursive_directory_iterator it(base, error), end; it != end;
       it.increment(error)) {
    if (error) break;
    std::error_code regularError;
    if (!it->is_regular_file(regularError)) continue;
    if (it->path().filename().string() != key) continue;
    // The `NN-` file prefix is convention only (docs/daw-plan.md W3): the
    // assignment is session state, so a take recorded on track 2 resolves on
    // track 9 after NEW TRACK FROM TAKE.
    return it->path();
  }
  return {};
}

void RecorderViewModel::syncActiveTake(int track) {
  const auto key = settings_.activeTake(track);
  if (key.empty()) {
    audio_.clearActiveTake(track);
    return;
  }
  const auto path = resolveTakeKey(track, key);
  if (path.empty()) {
    audio_.clearActiveTake(track);
    return;
  }
  std::string error;
  if (!audio_.setActiveTake(track, path, &error) && !error.empty()) {
    log("take playback refused for track " + std::to_string(track + 1) + ": " + error);
  }
}

void RecorderViewModel::resyncActiveTakes() {
  for (int track = 0; track < audio::kMaxTracks; ++track) syncActiveTake(track);
}

void RecorderViewModel::fillTakeMetrics(RecorderTakeModel::Entry& entry) {
  std::error_code sizeError;
  const auto size = std::filesystem::file_size(entry.path, sizeError);
  if (!sizeError) entry.bytes = size;
  entry.seconds = wavDurationSeconds(entry.path);
  const std::string key = entry.path.string();
  std::uint64_t mtime = 0;
  std::error_code timeError;
  const auto stamp = std::filesystem::last_write_time(entry.path, timeError);
  if (!timeError) {
    mtime = static_cast<std::uint64_t>(stamp.time_since_epoch().count());
  }
  if (const auto cached = miniWaveCache_.find(key); cached != miniWaveCache_.end()) {
    if (cached->second.wavBytes == entry.bytes && cached->second.wavMtime == mtime) {
      entry.wave = cached->second.wave;
      return;
    }
  }
  QVariantList wave;
  const double beats =
      timeline::takeLengthBeats(entry.seconds, state_.tempoX100.load());
  audio::PeakSummary summary;
  std::string error;
  if (beats > 0.0 && audio::readPeaks(key, summary, &error)) {
    // 64 columns is the row strip's resolution: one magnitude per ~2 px.
    const auto reduced = timeline::reducePeaks(summary, 0.0, beats, beats,
                                               kTakeRowWaveColumns);
    wave.reserve(static_cast<int>(reduced.size()));
    for (const float value : reduced) wave.push_back(value);
  }
  // Only a real reduction is cached: a missing sidecar must be retried on the
  // next scan, because the edit popup's scan writes it without touching the WAV.
  if (!wave.isEmpty()) miniWaveCache_[key] = CachedWave{entry.bytes, mtime, wave};
  entry.wave = wave;
}

void RecorderViewModel::scanTakes() {
  const QString selectedKey = selectedTakeKey().empty()
                                  ? QString()
                                  : QString::fromStdString(selectedTakeKey());
  const auto base = state_.takeDirectory();
  const std::string active = settings_.activeTake(selectedTrack_);
  const std::string legacyName = channelFileName(selectedTrack_);
  std::vector<RecorderTakeModel::Entry> entries;
  // Whole-project totals for the disk line: every take file in every take
  // folder, not only the selected track's list.
  int projectTakes = 0;
  std::uint64_t projectBytes = 0;
  std::error_code error;
  for (std::filesystem::directory_iterator it(base, error), end; it != end;
       it.increment(error)) {
    if (error) break;
    std::error_code typeError;
    if (it->is_directory(typeError)) {
      const auto folderName = it->path().filename().string();
      if (folderName.rfind(std::string(kTakePrefix), 0) != 0) continue;
      std::error_code fileError;
      for (std::filesystem::directory_iterator file(*it, fileError), fileEnd;
           file != fileEnd; file.increment(fileError)) {
        if (fileError) break;
        std::error_code regularError;
        if (!file->is_regular_file(regularError)) continue;
        if (file->path().extension() != ".wav") continue;
        ++projectTakes;
        std::error_code projectSizeError;
        const auto projectSize = std::filesystem::file_size(file->path(), projectSizeError);
        if (!projectSizeError) projectBytes += projectSize;
        const auto fileName = file->path().filename().string();
        const auto parts = parseTakeFileName(fileName);
        const bool perTrack = parts.valid && parts.track == selectedTrack_;
        const bool legacy = fileName == legacyName;
        if (!perTrack && !legacy) continue;
        RecorderTakeModel::Entry entry;
        entry.path = file->path();
        entry.key = perTrack
                        ? QString::fromStdString(fileName)
                        : QString::fromStdString(folderName + "/" + fileName);
        entry.name = perTrack ? QString::fromStdString(fileName)
                              : QString::fromStdString(folderName + "/" + fileName);
        entry.active = entry.key.toStdString() == active;
        fillTakeMetrics(entry);
        entries.push_back(entry);
      }
    } else if (it->path().extension() == ".wav") {
      const auto parts = parseTakeFileName(it->path().filename().string());
      if (!parts.valid || parts.track != selectedTrack_) continue;
      RecorderTakeModel::Entry entry;
      entry.path = it->path();
      entry.key = QString::fromStdString(it->path().filename().string());
      entry.name = QString::fromStdString(it->path().filename().string());
      entry.active = entry.key.toStdString() == active;
      fillTakeMetrics(entry);
      entries.push_back(entry);
    }
  }
  for (const auto& imported : imported_) {
    if (imported.track != selectedTrack_) continue;
    RecorderTakeModel::Entry entry;
    entry.path = imported.path;
    entry.key = QString::fromStdString(imported.key);
    entry.name = QString::fromStdString(imported.path.filename().string());
    entry.active = imported.key == active;
    entry.imported = true;
    fillTakeMetrics(entry);
    entries.push_back(entry);
  }
  // The track's ACTIVE take is always listed, whatever its file prefix says: the
  // `NN-` name is convention only (W3 NEW TRACK FROM TAKE hands a take to another
  // track without renaming the file), and a project can persist an absolute
  // imported pointer whose registry did not survive a restart.
  if (!active.empty()) {
    const bool listed = std::any_of(entries.begin(), entries.end(),
                                    [&](const RecorderTakeModel::Entry& entry) {
                                      return entry.key.toStdString() == active;
                                    });
    const auto activePath = resolveTakeKey(selectedTrack_, active);
    if (!listed && !activePath.empty()) {
      RecorderTakeModel::Entry entry;
      entry.path = activePath;
      entry.key = QString::fromStdString(active);
      entry.name = QString::fromStdString(activePath.filename().string());
      entry.active = true;
      // PTR marks an external import (the session key is an absolute path), not
      // a take that merely lives in the absolute take folder.
      entry.imported = std::filesystem::path(active).is_absolute();
      fillTakeMetrics(entry);
      entries.push_back(entry);
    }
  }

  std::sort(entries.begin(), entries.end(),
            [](const RecorderTakeModel::Entry& a, const RecorderTakeModel::Entry& b) {
              if (a.imported != b.imported) return !a.imported;
              return a.key > b.key;
            });
  takes_.setEntries(std::move(entries));
  projectTakes_ = projectTakes;
  projectBytes_ = projectBytes;

  int selection = -1;
  for (int row = 0; row < takes_.rowCount(); ++row) {
    if (const auto* entry = takes_.entry(row); entry && entry->key == selectedKey) {
      selection = row;
      break;
    }
  }
  if (selection < 0 && takes_.rowCount() > 0) selection = 0;
  selectedRow_ = selection;
  emit selectionChanged();
}

std::filesystem::path RecorderViewModel::selectedTakePath() const {
  const auto* entry = takes_.entry(selectedRow_);
  if (entry == nullptr) return {};
  return entry->path;
}

std::string RecorderViewModel::selectedTakeKey() const {
  const auto* entry = takes_.entry(selectedRow_);
  return entry == nullptr ? std::string() : entry->key.toStdString();
}

void RecorderViewModel::updateReadouts() {
  const auto status = link_.status();
  const bool recording = audio_.recording();
  if (recording) {
    // A new take owns the REC clock and retires the previous TAKE SAVED line.
    if (recStartMs_ == 0) recStartMs_ = steadyNowMs();
    lastAdoptedKey_.clear();
  } else {
    recStartMs_ = 0;
  }

  // A6: REC / PLAY / STOP only. "ROLLING" is studio jargon that collides with
  // REC on a recorder, so the state word and the colour are the answer.
  const QString transport =
      QString(recording ? QStringLiteral("REC ")
                        : (status.rolling ? QStringLiteral("PLAY ")
                                          : QStringLiteral("STOP "))) +
      QString::fromStdString(formatBarBeatTick(status.tick));
  if (transportText_ != transport) transportText_ = transport;
  // The dim second line says only what the state does not: a rolling transport
  // with the inputs still live is not recording them.
  const QString activeNow = activeTake();
  QString transportHint;
  if (status.rolling && !recording) {
    transportHint =
        activeNow == QStringLiteral("LIVE INPUT")
            ? QStringLiteral("not recording - tap START TAKE to capture these inputs")
            : QStringLiteral("playing take ") + activeNow;
  } else if (recording) {
    // W2b: the tap point decides what the meter means. PRE is the dry capture,
    // so the meter (which follows the mix) intentionally disagrees with the
    // take; say it once, where the state is read.
    if (mixer_.recSource(selectedTrack_) == kRecSourcePre) {
      transportHint = QStringLiteral("meter shows the mix - recording is PRE");
    }
  }
  if (transportHint_ != transportHint) transportHint_ = transportHint;

  QString clock = QString::fromStdString(formatTempo(status.tempoX100)) +
                  QStringLiteral("  -  tick %1").arg(static_cast<qulonglong>(status.tick));
  if (status.noPico) {
    clock += QStringLiteral("  -  NO PICO (Pi clock)");
  } else if (!status.linkUp) {
    clock += QStringLiteral("  -  link down");
  }
  if (clockText_ != clock) clockText_ = clock;

  const QString button = recording ? QStringLiteral("STOP TAKE")
                                   : QStringLiteral("START TAKE");
  if (buttonText_ != button) buttonText_ = button;

  const auto take = audio_.lastTake();
  const QString active = activeTake();
  QString statusText;
  statusWarning_ = false;
  const int mode = mixer_.mode(selectedTrack_);
  const bool scheduled = state_.recScheduled.load();
  if (scheduled) {
    // W2: state exactly what the schedule will do and when. A transport START
    // zeroes the tick, so a count-in always engages at bar 1; PUNCH names the
    // PUNCH IN/OUT measures it will record between.
    const std::string text =
        recScheduleStatus(state_.recMode.load(), state_.countInBars.load(), true, 1,
                          settings_.punch.in.measure, settings_.punch.out.measure);
    statusText = QString::fromStdString(text);
  } else if (recording) {
    // W2b status: `REC 00:04 - 02-bass-001 (POST+FX)`.
    const double elapsed = static_cast<double>(steadyNowMs() - recStartMs_) / 1000.0;
    statusText = QStringLiteral("REC %1 - %2 (%3)")
                     .arg(QString::fromStdString(formatDuration(elapsed)),
                          recordingTakeName(), trackRecSourceText());
  } else if (take.files > 0 && !lastAdoptedKey_.isEmpty()) {
    // W1 step 4: the take landed and the track switched to PLAY, so the status
    // names the take and the way to hear it.
    statusText = QStringLiteral("TAKE SAVED %1 - mode PLAY to hear it")
                     .arg(lastAdoptedKey_);
  } else if (take.files > 0) {
    const double seconds = audio_.sampleRate() > 0.0
                               ? static_cast<double>(take.frames) / audio_.sampleRate()
                               : 0.0;
    statusText = QStringLiteral("last take ") +
                 QString::fromStdString(std::filesystem::path(take.path).filename().string()) +
                 QStringLiteral("  -  %1 files  -  %2")
                     .arg(take.files)
                     .arg(QString::fromStdString(formatDuration(seconds)));
  } else if (mode == kTrackModeLive && settings_.activeTake(selectedTrack_).empty()) {
    // plan §5: a LIVE track never plays a take, so say it instead of leaving a
    // silent strip.
    statusText = QStringLiteral("TRACK %1 is LIVE - no take is active")
                     .arg(selectedTrack_ + 1, 2, 10, QLatin1Char('0'));
  } else if (active != QStringLiteral("LIVE INPUT")) {
    statusText = QStringLiteral("ACTIVE take  -  ") + active;
  } else {
    statusText = QStringLiteral("no active take  -  track plays the live input");
  }
  if (statusText_ != statusText) statusText_ = statusText;

  const auto directory = state_.takeDirectory();
  std::error_code spaceError;
  const auto space = std::filesystem::space(directory, spaceError);
  QString disk;
  const QString used = QStringLiteral("%1 takes  -  %2")
                           .arg(projectTakes_)
                           .arg(QString::fromStdString(
                               formatBytes(static_cast<std::int64_t>(projectBytes_))));
  if (!spaceError) {
    // W4 disk line: `177.6 GiB free - 4 takes - 12.4 MiB`.
    disk = QStringLiteral("%1 free  -  %2")
               .arg(QString::fromStdString(
                        formatBytes(static_cast<std::int64_t>(space.available))),
                    used);
  } else {
    disk = QStringLiteral("disk unavailable  -  ") + used;
  }
  if (diskText_ != disk) diskText_ = disk;

  const QString directoryText = QString::fromStdString(directory.string());
  if (takeDirectory_ != directoryText) takeDirectory_ = directoryText;

  emit readoutsChanged();
}

}  // namespace mnky::app
