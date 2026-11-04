#include "recorder_view_model.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <system_error>
#include <utility>

#include "logic.hpp"
#include "logging.hpp"
#include "project_logic.hpp"
#include "wav_info.hpp"

namespace mnky::app {
namespace {

std::string channelFileName(int track) {
  int display = track;
  if (display < 0) display = 0;
  std::string out = std::to_string(display);
  if (out.size() < 2) out.insert(out.begin(), '0');
  return "ch" + out + ".wav";
}

QString displayTrackName(int track) {
  return QStringLiteral("IN %1  %2")
      .arg(track + 1, 2, 10, QLatin1Char('0'))
      .arg(QString::fromStdString(defaultTrackName(track)));
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
  for (int i = 0; i < kNumStrips; ++i) armed_[static_cast<std::size_t>(i)] = mixer_.arm(i);
}

int RecorderArmModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : kNumStrips;
}

QVariant RecorderArmModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= kNumStrips) return {};
  if (role == IndexRole) return index.row();
  if (role == ArmedRole) return armed_[static_cast<std::size_t>(index.row())];
  return {};
}

QHash<int, QByteArray> RecorderArmModel::roleNames() const {
  return {{IndexRole, "index"}, {ArmedRole, "armed"}};
}

void RecorderArmModel::refresh() {
  for (int i = 0; i < kNumStrips; ++i) {
    const bool armed = mixer_.arm(i);
    if (armed_[static_cast<std::size_t>(i)] != armed) {
      armed_[static_cast<std::size_t>(i)] = armed;
      emit dataChanged(index(i, 0), index(i, 0), {ArmedRole});
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
          {ImportedRole, "imported"}};
}

void RecorderTakeModel::setEntries(std::vector<Entry> entries) {
  beginResetModel();
  entries_ = std::move(entries);
  endResetModel();
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
  const int clamped = std::clamp(track, 0, kNumStrips - 1);
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

QString RecorderViewModel::trackName() const { return displayTrackName(selectedTrack_); }

QString RecorderViewModel::activeTakePath(int track) const {
  if (track < 0 || track >= kNumStrips) return {};
  const auto key = settings_.activeTake(track);
  if (key.empty()) return {};
  const auto path = resolveTakeKey(track, key);
  return path.empty() ? QString() : QString::fromStdString(path.string());
}

std::filesystem::path RecorderViewModel::takePathForKey(int track,
                                                        const std::string& key) const {
  if (track < 0 || track >= kNumStrips || key.empty()) return {};
  return resolveTakeKey(track, key);
}

QString RecorderViewModel::trackDisplayName(int track) const {
  if (track < 0 || track >= kNumStrips) return {};
  return QString::fromStdString(defaultTrackName(track));
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

void RecorderViewModel::clearActive() {
  settings_.setActiveTake(selectedTrack_, {});
  syncActiveTake(selectedTrack_);
  scanTakes();
  updateReadouts();
  emit trackChanged();
  emit activeTakeChanged();
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

int RecorderViewModel::adoptRecordedTake(const std::string& takeDirectoryPath,
                                         std::uint32_t armMask) {
  const std::filesystem::path folder(takeDirectoryPath);
  const auto base = state_.takeDirectory();
  int renamed = 0;
  for (int track = 0; track < kNumStrips; ++track) {
    if ((armMask & (1u << static_cast<unsigned>(track))) == 0) continue;
    const auto source = folder / channelFileName(track);
    std::error_code existsError;
    if (!std::filesystem::is_regular_file(source, existsError)) continue;
    int number = nextTakeNumber(base, track) + 1;
    std::filesystem::path target = folder / takeFileName(track, defaultTrackName(track), number);
    std::error_code targetError;
    while (std::filesystem::exists(target, targetError)) {
      ++number;
      target = folder / takeFileName(track, defaultTrackName(track), number);
    }
    std::error_code moveError;
    std::filesystem::rename(source, target, moveError);
    if (moveError) continue;
    // Session v2 stores the take file name stem; per-track names are unique
    // because nextTakeNumber scans the whole take directory.
    settings_.setActiveTake(track, target.filename().string());
    syncActiveTake(track);
    ++renamed;
  }
  if (renamed > 0) {
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

std::filesystem::path RecorderViewModel::resolveTakeKey(int track,
                                                        const std::string& key) const {
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
    const auto parts = parseTakeFileName(key);
    if (parts.valid && parts.track != track) continue;
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
  for (int track = 0; track < kNumStrips; ++track) syncActiveTake(track);
}

void RecorderViewModel::scanTakes() {
  const QString selectedKey = selectedTakeKey().empty()
                                  ? QString()
                                  : QString::fromStdString(selectedTakeKey());
  const auto base = state_.takeDirectory();
  const std::string active = settings_.activeTake(selectedTrack_);
  const std::string legacyName = channelFileName(selectedTrack_);
  std::vector<RecorderTakeModel::Entry> entries;
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
        std::error_code sizeError;
        const auto size = std::filesystem::file_size(entry.path, sizeError);
        if (!sizeError) entry.bytes = size;
        entry.seconds = wavDurationSeconds(entry.path);
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
      std::error_code sizeError;
      const auto size = std::filesystem::file_size(entry.path, sizeError);
      if (!sizeError) entry.bytes = size;
      entry.seconds = wavDurationSeconds(entry.path);
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
    std::error_code sizeError;
    const auto size = std::filesystem::file_size(entry.path, sizeError);
    if (!sizeError) entry.bytes = size;
    entry.seconds = wavDurationSeconds(entry.path);
    entries.push_back(entry);
  }
  // A project can persist an absolute imported pointer; show it after a restart
  // even though the in-memory import registry did not survive.
  if (!active.empty()) {
    const std::filesystem::path activePath(active);
    const bool listed = std::any_of(entries.begin(), entries.end(),
                                    [&](const RecorderTakeModel::Entry& entry) {
                                      return entry.key.toStdString() == active;
                                    });
    std::error_code activeError;
    if (!listed && activePath.is_absolute() &&
        std::filesystem::is_regular_file(activePath, activeError)) {
      RecorderTakeModel::Entry entry;
      entry.path = activePath;
      entry.key = QString::fromStdString(active);
      entry.name = QString::fromStdString(activePath.filename().string());
      entry.active = true;
      entry.imported = true;
      std::error_code sizeError;
      const auto size = std::filesystem::file_size(entry.path, sizeError);
      if (!sizeError) entry.bytes = size;
      entry.seconds = wavDurationSeconds(entry.path);
      entries.push_back(entry);
    }
  }

  std::sort(entries.begin(), entries.end(),
            [](const RecorderTakeModel::Entry& a, const RecorderTakeModel::Entry& b) {
              if (a.imported != b.imported) return !a.imported;
              return a.key > b.key;
            });
  takes_.setEntries(std::move(entries));

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

  const QString transport =
      QString(recording ? QStringLiteral("REC ")
                        : (status.rolling ? QStringLiteral("ROLLING ")
                                          : QStringLiteral("STOPPED "))) +
      QString::fromStdString(formatBarBeatTick(status.tick));
  if (transportText_ != transport) transportText_ = transport;

  QString clock = QString::fromStdString(formatTempo(status.tempoX100)) +
                  QStringLiteral("  -  tick %1").arg(static_cast<qulonglong>(status.tick));
  if (!status.linkUp) clock += QStringLiteral("  -  link down");
  if (clockText_ != clock) clockText_ = clock;

  const QString button = recording ? QStringLiteral("STOP TAKE")
                                   : QStringLiteral("START TAKE");
  if (buttonText_ != button) buttonText_ = button;

  const auto take = audio_.lastTake();
  const QString active = activeTake();
  QString statusText;
  if (recording) {
    statusText = QStringLiteral("recording take  -  ") +
                 QString::fromStdString(state_.takeDirectory().string());
  } else if (take.files > 0) {
    const double seconds = audio_.sampleRate() > 0.0
                               ? static_cast<double>(take.frames) / audio_.sampleRate()
                               : 0.0;
    statusText = QStringLiteral("last take ") +
                 QString::fromStdString(std::filesystem::path(take.path).filename().string()) +
                 QStringLiteral("  -  %1 files  -  %2")
                     .arg(take.files)
                     .arg(QString::fromStdString(formatDuration(seconds)));
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
  if (!spaceError) {
    disk = QStringLiteral("disk %1 free  -  drops %2")
               .arg(QString::fromStdString(
                   formatBytes(static_cast<std::int64_t>(space.available))))
               .arg(static_cast<qulonglong>(audio_.drops()));
  } else {
    disk = QStringLiteral("disk unavailable  -  drops %1")
               .arg(static_cast<qulonglong>(audio_.drops()));
  }
  if (diskText_ != disk) diskText_ = disk;

  const QString directoryText = QString::fromStdString(directory.string());
  if (takeDirectory_ != directoryText) takeDirectory_ = directoryText;

  emit readoutsChanged();
}

}  // namespace mnky::app
