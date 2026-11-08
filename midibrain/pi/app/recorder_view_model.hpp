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
#include <map>
#include <string>
#include <vector>

#include "app_state.hpp"
#include "audio_glue.hpp"
#include "link_client.hpp"
#include "mixer_model.hpp"
#include "session_settings.hpp"

namespace mnky::app {

// Mini-waveform resolution of one recorder take row (44 px strip, ~2 px per
// column at the widest layout). One magnitude per column.
inline constexpr int kTakeRowWaveColumns = 64;

class RecorderArmModel final : public QAbstractListModel {
  Q_OBJECT
  // QML cannot read rowCount() as a property; the selector row sizes itself
  // from this.
  Q_PROPERTY(int count READ count CONSTANT)
 public:
  enum Roles { IndexRole = Qt::UserRole + 1, ArmedRole, NameRole };

  explicit RecorderArmModel(MixerModel& mixer, QObject* parent = nullptr);

  int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  int count() const { return kNumStrips; }
  QVariant data(const QModelIndex& index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;
  void refresh();

 private:
  MixerModel& mixer_;
  std::array<bool, kNumStrips> armed_{};
  std::array<QString, kNumStrips> names_{};
};

class RecorderTakeModel final : public QAbstractListModel {
  Q_OBJECT
  // QML cannot read rowCount() as a property; sheet heights use this.
  Q_PROPERTY(int count READ count NOTIFY countChanged)
 public:
  enum Roles {
    NameRole = Qt::UserRole + 1,
    FilesRole,
    DurationRole,
    BytesRole,
    PathRole,
    ActiveRole,
    ImportedRole,
    WaveRole,
    MasterRole,
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
    // RENDER SONG bounce: song-<date>.wav in the take root. Shown on every
    // track's list with a MASTER badge; selecting it only makes it ACTIVE on
    // that track (an audition), never changes the render on disk.
    bool master = false;
    // Mini-waveform magnitudes (0..1, `kTakeRowWaveColumns` of them) reduced
    // from the take's `.peaks` sidecar for the 44 px row strip; empty while the
    // take has no sidecar yet (the row then draws its dotted baseline).
    QVariantList wave;
  };

  explicit RecorderTakeModel(QObject* parent = nullptr);

  int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  int count() const { return static_cast<int>(entries_.size()); }
  QVariant data(const QModelIndex& index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

  void setEntries(std::vector<Entry> entries);
  // Valid only until the next setEntries()/rescan: the model owns its rows and
  // a take-folder rescan replaces (and frees) them. Copy what you need before
  // any call that can rescan (the clip-tap use-after-free class).
  const Entry* entry(int row) const;

 signals:
  void countChanged();

 private:
  std::vector<Entry> entries_;
};

class RecorderViewModel final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString transportText READ transportText NOTIFY readoutsChanged)
  // the dim second line under the REC/PLAY/STOP header. Empty when the
  // state needs no explanation (stopped, or recording a take).
  Q_PROPERTY(QString transportHint READ transportHint NOTIFY readoutsChanged)
  Q_PROPERTY(QString clockText READ clockText NOTIFY readoutsChanged)
  Q_PROPERTY(QString statusText READ statusText NOTIFY readoutsChanged)
  // True while statusText is the post-take ARM warning, so QML paints it in
  // the record colour over two 16 px lines (A1).
  Q_PROPERTY(bool statusWarning READ statusWarning NOTIFY readoutsChanged)
  // Arm state of the recorder's selected track: derived from its mode (ARM and
  // OVERDUB record), so the mode word and the picker can never disagree.
  Q_PROPERTY(bool trackArm READ trackArm NOTIFY readoutsChanged)
  // Track mode of the selected track: the word the
  // recorder's mode button shows. `trackRecSourceText` is the tap point the
  // header names beside it.
  Q_PROPERTY(QString trackModeWord READ trackModeWord NOTIFY readoutsChanged)
  // REC SOURCE of the selected track.
  Q_PROPERTY(QString trackRecSourceText READ trackRecSourceText NOTIFY readoutsChanged)
  Q_PROPERTY(QString diskText READ diskText NOTIFY readoutsChanged)
  Q_PROPERTY(QString buttonText READ buttonText NOTIFY readoutsChanged)
  Q_PROPERTY(QString takeDirectory READ takeDirectory NOTIFY readoutsChanged)
  Q_PROPERTY(bool recording READ recording NOTIFY readoutsChanged)
  Q_PROPERTY(int selectedRow READ selectedRow NOTIFY selectionChanged)
  Q_PROPERTY(int selectedTrack READ selectedTrack WRITE setSelectedTrack NOTIFY trackChanged)
  Q_PROPERTY(QString trackName READ trackName NOTIFY trackChanged)
  Q_PROPERTY(QString activeTake READ activeTake NOTIFY trackChanged)
  Q_PROPERTY(bool loopEnabled READ loopEnabled WRITE setLoopEnabled NOTIFY loopChanged)
  Q_PROPERTY(double loopStartBeats READ loopStartBeats WRITE setLoopStartBeats NOTIFY loopChanged)
  Q_PROPERTY(double loopEndBeats READ loopEndBeats WRITE setLoopEndBeats NOTIFY loopChanged)
  Q_PROPERTY(QString loopStartText READ loopStartText NOTIFY loopChanged)
  Q_PROPERTY(QString loopEndText READ loopEndText NOTIFY loopChanged)
  Q_PROPERTY(QString loopText READ loopText NOTIFY loopChanged)
  Q_PROPERTY(RecorderArmModel* arms READ arms CONSTANT)
  Q_PROPERTY(RecorderTakeModel* takes READ takes CONSTANT)

 public:
  RecorderViewModel(AppState& state, AudioGlue& audio, MixerModel& mixer,
                    LinkClient& link, SessionSettings& settings, QObject* parent = nullptr);

  QString transportText() const { return transportText_; }
  QString transportHint() const { return transportHint_; }
  QString clockText() const { return clockText_; }
  QString statusText() const { return statusText_; }
  bool statusWarning() const { return statusWarning_; }
  bool trackArm() const { return mixer_.arm(selectedTrack_); }
  QString trackModeWord() const;
  QString trackRecSourceText() const;
  QString diskText() const { return diskText_; }
  QString buttonText() const { return buttonText_; }
  QString takeDirectory() const { return takeDirectory_; }
  bool recording() const { return audio_.recording(); }
  int selectedRow() const { return selectedRow_; }
  int selectedTrack() const { return selectedTrack_; }
  Q_INVOKABLE void setSelectedTrack(int track);
  QString trackName() const;
  QString activeTake() const;
  // Timeline seam: resolve one track's active take to a real file (empty when
  // there is no pointer or the file is missing) and the default track name.
  // The timeline never copies a take; it only reads the path and its sidecars.
  QString activeTakePath(int track) const;
  std::filesystem::path takePathForKey(int track, const std::string& key) const;
  QString trackDisplayName(int track) const;
  // Per-track take loop (session v2 takes.loop) for the selected track. The
  // engine window is `[startBeats, endBeats)`; an end at or below the start
  // loops the whole active take, which is the UI copy on the Recorder SHIFT
  // face. Values are 0.25-beat steps from 0 to kLoopMaxBeats.
  bool loopEnabled() const;
  Q_INVOKABLE void setLoopEnabled(bool enabled);
  double loopStartBeats() const;
  Q_INVOKABLE void setLoopStartBeats(double beats);
  double loopEndBeats() const;
  Q_INVOKABLE void setLoopEndBeats(double beats);
  QString loopStartText() const;
  QString loopEndText() const;
  QString loopText() const;
  RecorderArmModel* arms() { return &arms_; }
  RecorderTakeModel* takes() { return &takes_; }

  Q_INVOKABLE void startStop();
  Q_INVOKABLE void selectRow(int row);
  Q_INVOKABLE void activateSelected();
  // Row-body tap: select + activate one row and return the activated take's
  // display name as a value. The name is copied before the rescan, because
  // activating rescans the list and frees the row storage (the pointer a
  // caller might hold is invalid from then on - this is the crash class the
  // clip-click tap gate covers).
  QString activateRow(int row);
  Q_INVOKABLE void clearActive();
  Q_INVOKABLE QString importTake(const QString& path);
  // the pre-accept preview/refusal of an import path. Returns
  // "name - 48 kHz - 2 ch - 0:12" when the file may be imported, else a first
  // line starting with "import refused:" (48 kHz rule) or a path error.
  Q_INVOKABLE QString takeImportPreview(const QString& path) const;
  Q_INVOKABLE QString renameSelected(const QString& name);
  // Display name of one take row, for the SHIFT face's selected-object line.
  Q_INVOKABLE QString takeNameAt(int row) const;
  // The row's mini-waveform magnitudes (same data as the model's `wave` role,
  // for surfaces that read the list by row instead of by delegate role).
  Q_INVOKABLE QVariantList takeWave(int row) const;
  Q_INVOKABLE void deleteSelected();
  // REMOVE: project only - the take file stays on disk,
  // the track simply stops pointing at it.
  Q_INVOKABLE void removeSelected();
  Q_INVOKABLE void refresh();

  // Re-scan the take directory (after a take stops or the directory changes).
  void takeStateChanged();
  // Track names live in the mixer; a rename refreshes the selector labels and
  // the header without touching the take list (A4 + C2).
  void tracksChanged();
  // Rename the just-recorded `chNN.wav` files to the session-v2 per-track form
  // and make each armed track's new take active. Returns the number renamed;
  // `lastKey` receives the last adopted take key (the take the TAKE SAVED
  // notice names).
  int adoptRecordedTake(const std::string& takeDirectoryPath,
                        MixerModel::TrackMask armMask,
                        std::string* lastKey = nullptr);
  // Push the session active-take pointers into the engine take player after a
  // project load (or at startup). Missing files clear the track.
  void resyncActiveTakes();

 signals:
  void readoutsChanged();
  void selectionChanged();
  void trackChanged();
  void activeTakeChanged();
  void loopChanged();

 private:
  void scanTakes();
  // Bytes, duration and the row mini-waveform for one take file. The wave comes
  // from a cache keyed by the WAV size/mtime, so a rescan of an unchanged take
  // folder does not re-read every sidecar.
  void fillTakeMetrics(RecorderTakeModel::Entry& entry);
  void updateReadouts();
  std::filesystem::path selectedTakePath() const;
  std::string selectedTakeKey() const;
  std::filesystem::path resolveTakeKey(int track, const std::string& key) const;
  void syncActiveTake(int track);
  // Whole-take rule: an enabled loop whose end is at or below the start loops
  // the whole active take (session::LoopState contract).
  session::LoopState selectedLoop() const;
  void writeLoop(const session::LoopState& loop);

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
  QString transportHint_;
  QString clockText_;
  QString statusText_;
  bool statusWarning_ = false;
  QString diskText_;
  QString buttonText_;
  QString takeDirectory_;
  // Whole-project take totals for the disk line ("N takes - X MiB"), filled by
  // scanTakes.
  int projectTakes_ = 0;
  std::uint64_t projectBytes_ = 0;
  // Take start time for the REC mm:ss readout while a take runs.
  std::uint64_t recStartMs_ = 0;
  // The take the last stop adopted, for the TAKE SAVED status line; cleared as
  // soon as the next take starts recording.
  QString lastAdoptedKey_;
  // The take name a running take will carry once it stops (the first recording
  // track's `<nn>-<name>-<n>` form), for the REC status line.
  QString recordingTakeName() const;
  struct CachedWave {
    std::uint64_t wavBytes = 0;
    std::uint64_t wavMtime = 0;
    QVariantList wave;
  };
  std::map<std::string, CachedWave> miniWaveCache_;
  struct ImportedTake {
    int track = 0;
    std::filesystem::path path;
    std::string key;
  };
  std::vector<ImportedTake> imported_;
};

}  // namespace mnky::app
