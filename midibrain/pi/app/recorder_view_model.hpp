#pragma once
// QML view model for the TAKE screen: one track at a time, its take list with
// ACTIVE flags (empty = live input), a pointer import (no audio copy), rename
// through the shared keypad and delete with confirmation. New takes recorded by
// the core Recorder (`take-<utc>/chNN.wav`) are adopted after stop and renamed
// to the session-v2 per-track form `<nn>-<trackname>-<take#>.wav`.
#include <QAbstractListModel>
#include <QObject>
#include <QString>
#include <QVariant>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "app_state.hpp"
#include "audio_glue.hpp"
#include "link_client.hpp"
#include "mixer_model.hpp"
#include "session_settings.hpp"

namespace mnky::app {

class RecorderArmModel final : public QAbstractListModel {
  Q_OBJECT
 public:
  enum Roles { IndexRole = Qt::UserRole + 1, ArmedRole };

  explicit RecorderArmModel(MixerModel& mixer, QObject* parent = nullptr);

  int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;
  void refresh();

 private:
  MixerModel& mixer_;
  std::array<bool, kNumStrips> armed_{};
};

class RecorderTakeModel final : public QAbstractListModel {
  Q_OBJECT
 public:
  enum Roles {
    NameRole = Qt::UserRole + 1,
    FilesRole,
    DurationRole,
    BytesRole,
    PathRole,
    ActiveRole,
    ImportedRole,
  };

  struct Entry {
    QString name;      // display name, unique inside the track list
    QString key;       // active-take pointer: relative path or absolute path
    int files = 1;
    double seconds = 0.0;
    std::uint64_t bytes = 0;
    std::filesystem::path path;
    bool active = false;
    bool imported = false;
  };

  explicit RecorderTakeModel(QObject* parent = nullptr);

  int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

  void setEntries(std::vector<Entry> entries);
  const Entry* entry(int row) const;

 private:
  std::vector<Entry> entries_;
};

class RecorderViewModel final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString transportText READ transportText NOTIFY readoutsChanged)
  Q_PROPERTY(QString clockText READ clockText NOTIFY readoutsChanged)
  Q_PROPERTY(QString statusText READ statusText NOTIFY readoutsChanged)
  Q_PROPERTY(QString diskText READ diskText NOTIFY readoutsChanged)
  Q_PROPERTY(QString buttonText READ buttonText NOTIFY readoutsChanged)
  Q_PROPERTY(QString takeDirectory READ takeDirectory NOTIFY readoutsChanged)
  Q_PROPERTY(bool recording READ recording NOTIFY readoutsChanged)
  Q_PROPERTY(int selectedRow READ selectedRow NOTIFY selectionChanged)
  Q_PROPERTY(int selectedTrack READ selectedTrack WRITE setSelectedTrack NOTIFY trackChanged)
  Q_PROPERTY(QString trackName READ trackName NOTIFY trackChanged)
  Q_PROPERTY(QString activeTake READ activeTake NOTIFY trackChanged)
  Q_PROPERTY(RecorderArmModel* arms READ arms CONSTANT)
  Q_PROPERTY(RecorderTakeModel* takes READ takes CONSTANT)

 public:
  RecorderViewModel(AppState& state, AudioGlue& audio, MixerModel& mixer,
                    LinkClient& link, SessionSettings& settings, QObject* parent = nullptr);

  QString transportText() const { return transportText_; }
  QString clockText() const { return clockText_; }
  QString statusText() const { return statusText_; }
  QString diskText() const { return diskText_; }
  QString buttonText() const { return buttonText_; }
  QString takeDirectory() const { return takeDirectory_; }
  bool recording() const { return audio_.recording(); }
  int selectedRow() const { return selectedRow_; }
  int selectedTrack() const { return selectedTrack_; }
  Q_INVOKABLE void setSelectedTrack(int track);
  QString trackName() const;
  QString activeTake() const;
  RecorderArmModel* arms() { return &arms_; }
  RecorderTakeModel* takes() { return &takes_; }

  Q_INVOKABLE void startStop();
  Q_INVOKABLE void selectRow(int row);
  Q_INVOKABLE void activateSelected();
  Q_INVOKABLE void clearActive();
  Q_INVOKABLE QString importTake(const QString& path);
  Q_INVOKABLE QString renameSelected(const QString& name);
  Q_INVOKABLE void deleteSelected();
  Q_INVOKABLE void refresh();

  // Re-scan the take directory (after a take stops or the directory changes).
  void takeStateChanged();
  // Rename the just-recorded `chNN.wav` files to the session-v2 per-track form
  // and make each armed track's new take active. Returns the number renamed.
  int adoptRecordedTake(const std::string& takeDirectoryPath, std::uint32_t armMask);
  // Push the session active-take pointers into the engine take player after a
  // project load (or at startup). Missing files clear the track.
  void resyncActiveTakes();

 signals:
  void readoutsChanged();
  void selectionChanged();
  void trackChanged();
  void activeTakeChanged();

 private:
  void scanTakes();
  void updateReadouts();
  std::filesystem::path selectedTakePath() const;
  std::string selectedTakeKey() const;
  std::filesystem::path resolveTakeKey(int track, const std::string& key) const;
  void syncActiveTake(int track);

  AppState& state_;
  AudioGlue& audio_;
  MixerModel& mixer_;
  LinkClient& link_;
  SessionSettings& settings_;
  RecorderArmModel arms_;
  RecorderTakeModel takes_;
  int selectedTrack_ = 0;
  int selectedRow_ = -1;
  int refreshCounter_ = 0;
  QString transportText_;
  QString clockText_;
  QString statusText_;
  QString diskText_;
  QString buttonText_;
  QString takeDirectory_;
  struct ImportedTake {
    int track = 0;
    std::filesystem::path path;
    std::string key;
  };
  std::vector<ImportedTake> imported_;
};

}  // namespace mnky::app
