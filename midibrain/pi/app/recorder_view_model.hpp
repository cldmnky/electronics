#pragma once
// QML view model for the Recorder screen: transport readout, per-channel arm
// toggles, the take list scanned from the take directory, rename/delete,
// drop/free-disk readouts. Take rename/delete use the same ASCII rules as the
// project directory names.
#include <QAbstractListModel>
#include <QObject>
#include <QString>
#include <QVariant>

#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "app_state.hpp"
#include "logic.hpp"
#include "audio_glue.hpp"
#include "link_client.hpp"
#include "mixer_model.hpp"

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
  };

  struct Entry {
    QString name;
    int files = 0;
    double seconds = 0.0;
    std::uint64_t bytes = 0;
    std::filesystem::path path;
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
  Q_PROPERTY(RecorderArmModel* arms READ arms CONSTANT)
  Q_PROPERTY(RecorderTakeModel* takes READ takes CONSTANT)

 public:
  RecorderViewModel(AppState& state, AudioGlue& audio, MixerModel& mixer,
                    LinkClient& link, QObject* parent = nullptr);

  QString transportText() const { return transportText_; }
  QString clockText() const { return clockText_; }
  QString statusText() const { return statusText_; }
  QString diskText() const { return diskText_; }
  QString buttonText() const { return buttonText_; }
  QString takeDirectory() const { return takeDirectory_; }
  bool recording() const { return audio_.recording(); }
  int selectedRow() const { return selectedRow_; }
  RecorderArmModel* arms() { return &arms_; }
  RecorderTakeModel* takes() { return &takes_; }

  Q_INVOKABLE void startStop();
  Q_INVOKABLE void selectRow(int row);
  Q_INVOKABLE QString renameSelected(const QString& name);
  Q_INVOKABLE void deleteSelected();
  Q_INVOKABLE void refresh();

  // Re-scan the take directory (after a take stops or the directory changes).
  void takeStateChanged();

 signals:
  void readoutsChanged();
  void selectionChanged();

 private:
  void scanTakes();
  void updateReadouts();
  std::filesystem::path selectedTakeDirectory() const;

  AppState& state_;
  AudioGlue& audio_;
  MixerModel& mixer_;
  LinkClient& link_;
  RecorderArmModel arms_;
  RecorderTakeModel takes_;
  int selectedRow_ = -1;
  int refreshCounter_ = 0;
  QString transportText_;
  QString clockText_;
  QString statusText_;
  QString diskText_;
  QString buttonText_;
  QString takeDirectory_;
};

}  // namespace mnky::app
