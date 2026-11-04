#pragma once
// QML model for the banked mixer and the single-track TRACK mode: 18 strips
// plus a pinned master. Values live in MixerModel's atomics; this model only
// formats them for QML and forwards edits. Bank 0 shows strips 1-9, bank 1
// shows 10-18. TRACK mode is the same strip shown as one track with many
// settings (MIXER = one setting across many tracks).
#include <QAbstractListModel>
#include <QObject>
#include <QString>
#include <QVariant>

#include <array>

#include "logic.hpp"
#include "mixer_model.hpp"
#include "session_settings.hpp"

namespace mnky::app {

inline constexpr int kStripsPerBank = 9;

class MixerViewModel final : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(int bank READ bank WRITE setBank NOTIFY bankChanged)
  Q_PROPERTY(int stripsPerBank READ stripsPerBank CONSTANT)
  Q_PROPERTY(bool trackMode READ trackMode WRITE setTrackMode NOTIFY trackModeChanged)
  Q_PROPERTY(int trackIndex READ trackIndex WRITE setTrackIndex NOTIFY trackChanged)
  Q_PROPERTY(QString trackName READ trackName NOTIFY trackChanged)
  Q_PROPERTY(double trackGainDb READ trackGainDb WRITE setTrackGainDb NOTIFY trackChanged)
  Q_PROPERTY(double trackTrimDb READ trackTrimDb WRITE setTrackTrimDb NOTIFY trackChanged)
  Q_PROPERTY(double trackPan READ trackPan WRITE setTrackPan NOTIFY trackChanged)
  Q_PROPERTY(QString trackPanText READ trackPanText NOTIFY trackChanged)
  Q_PROPERTY(double trackSendA READ trackSendA WRITE setTrackSendA NOTIFY trackChanged)
  Q_PROPERTY(double trackSendB READ trackSendB WRITE setTrackSendB NOTIFY trackChanged)
  Q_PROPERTY(double trackCue READ trackCue WRITE setTrackCue NOTIFY trackChanged)
  Q_PROPERTY(double trackOut34 READ trackOut34 WRITE setTrackOut34 NOTIFY trackChanged)
  Q_PROPERTY(bool trackMute READ trackMute WRITE setTrackMute NOTIFY trackChanged)
  Q_PROPERTY(bool trackSolo READ trackSolo WRITE setTrackSolo NOTIFY trackChanged)
  Q_PROPERTY(bool trackArm READ trackArm WRITE setTrackArm NOTIFY trackChanged)
  Q_PROPERTY(double trackLevel READ trackLevel NOTIFY metersChanged)
  Q_PROPERTY(bool trackClip READ trackClip NOTIFY metersChanged)
  Q_PROPERTY(int trackMonitor READ trackMonitor WRITE setTrackMonitor NOTIFY trackChanged)
  Q_PROPERTY(QString trackInputText READ trackInputText NOTIFY trackChanged)
  Q_PROPERTY(QString trackOutputText READ trackOutputText NOTIFY trackChanged)
  Q_PROPERTY(bool trackInPair READ trackInPair WRITE setTrackInPair NOTIFY trackChanged)
  Q_PROPERTY(int selectedStrip READ selectedStrip WRITE setSelectedStrip NOTIFY selectedStripChanged)
  Q_PROPERTY(double masterDb READ masterDb WRITE setMasterDb NOTIFY masterChanged)
  Q_PROPERTY(double meterLeft READ meterLeft NOTIFY metersChanged)
  Q_PROPERTY(double meterRight READ meterRight NOTIFY metersChanged)

 public:
  enum Roles {
    IndexRole = Qt::UserRole + 1,
    NameRole,
    GainDbRole,
    PanRole,
    PanTextRole,
    SendARole,
    SendBRole,
    MuteRole,
    SoloRole,
    ArmRole,
    LevelRole,
    ClipRole,
    MonitorRole,
    MonitorMarkRole,
    SelectedRole,
  };

  explicit MixerViewModel(MixerModel& model, SessionSettings& settings,
                          QObject* parent = nullptr);

  int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

  int bank() const { return bank_; }
  Q_INVOKABLE void setBank(int bank);
  int stripsPerBank() const { return kStripsPerBank; }
  bool trackMode() const { return trackMode_; }
  Q_INVOKABLE void setTrackMode(bool enabled);
  int trackIndex() const { return trackIndex_; }
  Q_INVOKABLE void setTrackIndex(int index);
  QString trackName() const;
  double trackGainDb() const { return model_.gainDb(trackIndex_); }
  Q_INVOKABLE void setTrackGainDb(double db);
  double trackTrimDb() const { return model_.trimDb(trackIndex_); }
  Q_INVOKABLE void setTrackTrimDb(double db);
  double trackPan() const { return model_.pan(trackIndex_); }
  Q_INVOKABLE void setTrackPan(double pan);
  QString trackPanText() const;
  double trackSendA() const { return model_.send(trackIndex_, 0); }
  Q_INVOKABLE void setTrackSendA(double level);
  double trackSendB() const { return model_.send(trackIndex_, 1); }
  Q_INVOKABLE void setTrackSendB(double level);
  double cueSend(int index) const { return model_.cueSend(index); }
  double out34Send(int index) const { return model_.out34Send(index); }
  Q_INVOKABLE double gainDbAt(int index) const { return model_.gainDb(index); }
  Q_INVOKABLE double sendAt(int index, int send) const { return model_.send(index, send); }
  Q_INVOKABLE double cueAt(int index) const { return model_.cueSend(index); }
  Q_INVOKABLE double out34At(int index) const { return model_.out34Send(index); }
  Q_INVOKABLE int monitorAt(int index) const { return model_.monitor(index); }
  double trackCue() const { return model_.cueSend(trackIndex_); }
  Q_INVOKABLE void setTrackCue(double level);
  double trackOut34() const { return model_.out34Send(trackIndex_); }
  Q_INVOKABLE void setTrackOut34(double level);
  bool trackMute() const { return model_.mute(trackIndex_); }
  Q_INVOKABLE void setTrackMute(bool mute);
  bool trackSolo() const { return model_.solo(trackIndex_); }
  Q_INVOKABLE void setTrackSolo(bool solo);
  bool trackArm() const { return model_.arm(trackIndex_); }
  Q_INVOKABLE void setTrackArm(bool arm);
  double trackLevel() const { return stripLevel(trackIndex_); }
  bool trackClip() const { return stripClip(trackIndex_); }
  int trackMonitor() const { return model_.monitor(trackIndex_); }
  Q_INVOKABLE void setTrackMonitor(int mode);
  Q_INVOKABLE void setMonitor(int index, int mode);
  // Per-track I/O assignment (session v2 inCh/inPair/outDst). The engine has no
  // source/destination fields yet, so these values live in MixerModel and the
  // session; the pickers on the TRACK SHIFT face are the only writer.
  int trackInCh() const { return model_.inCh(trackIndex_); }
  bool trackInPair() const { return model_.inPair(trackIndex_); }
  int trackOutDst() const { return model_.outDst(trackIndex_); }
  QString trackInputText() const { return inputText(trackInCh(), trackInPair()); }
  QString trackOutputText() const { return outputText(trackOutDst()); }
  Q_INVOKABLE void setTrackInCh(int channel);
  Q_INVOKABLE void setTrackInPair(bool pair);
  Q_INVOKABLE void setTrackOutDst(int destination);
  // Panel/touch steppers: -1/+1 through "none, IN 01..18" and the four output
  // destinations. Both wrap.
  Q_INVOKABLE void cycleTrackInCh(int delta);
  Q_INVOKABLE void cycleTrackOutDst(int delta);
  static QString inputText(int channel, bool pair);
  static QString outputText(int destination);
  // Re-evaluate the Auto marker after an active-take change (Auto shows only
  // when a take makes it choose the take over the live input).
  void notifyActiveTakesChanged();
  int selectedStrip() const { return selectedStrip_; }
  void setSelectedStrip(int index);
  double masterDb() const { return model_.masterDb(); }
  Q_INVOKABLE void setMasterDb(double db);
  double meterLeft() const { return meterLeft_; }
  double meterRight() const { return meterRight_; }

  // QML writes and panel-focus adjustments.
  Q_INVOKABLE void setGainDb(int index, double db);
  Q_INVOKABLE void setPan(int index, double pan);
  Q_INVOKABLE void setSend(int index, int send, double level);
  Q_INVOKABLE void setCue(int index, double level);
  Q_INVOKABLE void setOut34(int index, double level);
  Q_INVOKABLE void toggleMute(int index);
  Q_INVOKABLE void toggleSolo(int index);
  Q_INVOKABLE void toggleArm(int index);
  Q_INVOKABLE void adjust(int index, const QString& parameter, double delta);

  // Called by the controller timer after reading the audio peaks.
  void setMeters(double left, double right);
  void setStripMeters(const std::array<double, kNumStrips>& levels);
  // Deterministic clip/meter demo for the offscreen screenshot run.
  Q_INVOKABLE void showClipDemo();
  Q_INVOKABLE void clearClipDemo();
  // Re-reads every value (project load, MIDI learn writes).
  void refresh();

 signals:
  void bankChanged();
  void trackModeChanged();
  void trackChanged();
  void selectedStripChanged();
  void masterChanged();
  void metersChanged();

 private:
  double stripLevel(int index) const;
  bool stripClip(int index) const;
  QString monitorMark(int index) const;

  MixerModel& model_;
  SessionSettings& settings_;
  int bank_ = 0;
  int selectedStrip_ = 0;
  bool trackMode_ = false;
  int trackIndex_ = 0;
  double meterLeft_ = 0.0;
  double meterRight_ = 0.0;
  std::array<double, kNumStrips> stripLevels_{};
  std::array<ClipHold, kNumStrips> clipHolds_{};
  bool clipDemo_ = false;
};

}  // namespace mnky::app
