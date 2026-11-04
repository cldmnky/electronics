#include "recorder_view_model.hpp"

#include <algorithm>
#include <cstdlib>
#include <system_error>
#include <utility>

#include "logic.hpp"
#include "logging.hpp"
#include "project_logic.hpp"
#include "wav_info.hpp"

namespace mnky::app {

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
          {PathRole, "path"}};
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
                                     LinkClient& link, QObject* parent)
    : QObject(parent),
      state_(state),
      audio_(audio),
      mixer_(mixer),
      link_(link),
      arms_(mixer, nullptr) {
  takeDirectory_ = QString::fromStdString(state_.takeDirectory().string());
  scanTakes();
  updateReadouts();
}

void RecorderViewModel::startStop() {
  // Amount 1 = immediate start/stop; the panel REC button uses 0 so it can
  // pair with PLAY inside the 300 ms chord window.
  state_.postIntent(Intent{Intent::Kind::RecToggle, 1});
}

void RecorderViewModel::selectRow(int row) {
  const int clamped = row >= 0 && row < takes_.rowCount() ? row : -1;
  if (clamped == selectedRow_) return;
  selectedRow_ = clamped;
  emit selectionChanged();
}

QString RecorderViewModel::renameSelected(const QString& name) {
  if (audio_.recording()) return QStringLiteral("stop the take before renaming");
  const auto directory = selectedTakeDirectory();
  if (directory.empty()) return QStringLiteral("select a take first");
  const auto text = name.toStdString();
  const auto error = validateTakeName(text);
  if (!error.empty()) return QString::fromStdString(error);
  if (name.toStdString() == directory.filename().string()) return {};
  const auto target = directory.parent_path() / name.toStdString();
  std::error_code existsError;
  if (std::filesystem::exists(target, existsError)) {
    return QStringLiteral("a take with that name already exists");
  }
  std::error_code moveError;
  std::filesystem::rename(directory, target, moveError);
  if (moveError) return QStringLiteral("rename failed (is the take open?)");
  takeStateChanged();
  for (int row = 0; row < takes_.rowCount(); ++row) {
    if (const auto* entry = takes_.entry(row); entry && entry->name == name) {
      selectRow(row);
      break;
    }
  }
  return {};
}

void RecorderViewModel::deleteSelected() {
  if (audio_.recording()) return;
  const auto directory = selectedTakeDirectory();
  if (directory.empty()) return;
  std::error_code error;
  std::filesystem::remove_all(directory, error);
  if (error) {
    statusText_ = QStringLiteral("delete failed: ") +
                  QString::fromStdString(directory.filename().string());
    emit readoutsChanged();
    return;
  }
  takeStateChanged();
}

void RecorderViewModel::refresh() {
  arms_.refresh();
  updateReadouts();
  // Directory scanning hits the filesystem, so only every ~1.3 s.
  if (++refreshCounter_ % 20 == 0) scanTakes();
}

void RecorderViewModel::takeStateChanged() {
  scanTakes();
  updateReadouts();
}

void RecorderViewModel::scanTakes() {
  const QString selected =
      takes_.entry(selectedRow_) != nullptr ? takes_.entry(selectedRow_)->name : QString();
  const auto base = state_.takeDirectory();
  std::vector<RecorderTakeModel::Entry> entries;
  std::error_code error;
  std::vector<std::filesystem::path> directories;
  for (std::filesystem::directory_iterator it(base, error), end; it != end;
       it.increment(error)) {
    if (error) break;
    std::error_code typeError;
    if (!it->is_directory(typeError)) continue;
    const auto name = it->path().filename().string();
    if (name.rfind(std::string(kTakePrefix), 0) == 0) directories.push_back(it->path());
  }
  std::sort(directories.begin(), directories.end());

  // Newest first: take names start with a UTC timestamp, so lexical order is
  // chronological.
  for (auto it = directories.rbegin(); it != directories.rend(); ++it) {
    RecorderTakeModel::Entry entry;
    entry.path = *it;
    entry.name = QString::fromStdString(it->filename().string());
    std::error_code fileError;
    for (std::filesystem::directory_iterator file(*it, fileError), fileEnd; file != fileEnd;
         file.increment(fileError)) {
      if (fileError) break;
      std::error_code typeError;
      if (!file->is_regular_file(typeError)) continue;
      if (file->path().extension() != ".wav") continue;
      ++entry.files;
      std::error_code sizeError;
      const auto size = std::filesystem::file_size(file->path(), sizeError);
      if (!sizeError) entry.bytes += size;
    }
    const auto master = *it / "master.wav";
    std::error_code masterError;
    if (std::filesystem::is_regular_file(master, masterError)) {
      entry.seconds = wavDurationSeconds(master);
    }
    entries.push_back(entry);
  }

  takes_.setEntries(std::move(entries));

  int selection = -1;
  for (int row = 0; row < takes_.rowCount(); ++row) {
    if (const auto* entry = takes_.entry(row); entry && entry->name == selected) {
      selection = row;
      break;
    }
  }
  if (selection < 0 && takes_.rowCount() > 0) selection = 0;
  selectedRow_ = selection;
  emit selectionChanged();
}

std::filesystem::path RecorderViewModel::selectedTakeDirectory() const {
  const auto* entry = takes_.entry(selectedRow_);
  if (entry == nullptr) return {};
  return entry->path;
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
  } else {
    statusText = QStringLiteral("arm a channel, then START TAKE");
  }
  if (statusText_ != statusText) statusText_ = statusText;

  const auto directory = state_.takeDirectory();
  std::error_code spaceError;
  const auto space = std::filesystem::space(directory, spaceError);
  QString disk;
  if (!spaceError) {
    disk = QStringLiteral("disk %1 free of %2  -  drops %3")
               .arg(QString::fromStdString(
                   formatBytes(static_cast<std::int64_t>(space.available))))
               .arg(QString::fromStdString(
                   formatBytes(static_cast<std::int64_t>(space.capacity))))
               .arg(static_cast<qulonglong>(audio_.drops()));
  } else {
    disk = QStringLiteral("disk unavailable  -  drops %1")
               .arg(static_cast<qulonglong>(audio_.drops()));
  }
  if (diskText_ != disk) diskText_ = disk;

  const QString directoryText = QString::fromStdString(state_.takeDirectory().string());
  if (takeDirectory_ != directoryText) takeDirectory_ = directoryText;

  emit readoutsChanged();
}

}  // namespace mnky::app
