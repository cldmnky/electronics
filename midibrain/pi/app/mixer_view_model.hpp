#pragma once
// QML model for the dynamic banked mixer and the single-track TRACK mode.
// The mixer shows the session's ordered track list, four strips per page; the
// per-slot configuration store is fixed (MixerModel) so a removed track keeps
// its settings and takes and a re-add restores them. Values live in
// MixerModel's atomics; this model only formats them for QML and forwards
// edits. TRACK mode is the same strip shown as one track with many settings
// (MIXER = one setting across many tracks). Every selector is a value + popup
// pair: the list popups live in ConfigPopup and write through this model the
// moment a choice changes.
#include <QAbstractListModel>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <array>

#include "eq_model.hpp"
#include "logic.hpp"
#include "mixer_model.hpp"
#include "session_settings.hpp"

namespace mnky::app {

// Four strips per page: the owner's "8-9 crammed, sliders too small" ask. The
// page count comes from the track list (ceil(tracks / kStripsPerBank)), never
// from a capacity constant.
inline constexpr int kStripsPerBank = 4;

class MixerViewModel final : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(int bank READ bank WRITE setBank NOTIFY bankChanged)
  Q_PROPERTY(int stripsPerBank READ stripsPerBank CONSTANT)
  Q_PROPERTY(int stripCount READ stripCount NOTIFY tracksChanged)
  Q_PROPERTY(int pageCount READ pageCount NOTIFY tracksChanged)
  Q_PROPERTY(bool empty READ empty NOTIFY tracksChanged)
  Q_PROPERTY(QString bankText READ bankText NOTIFY tracksChanged)
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
  Q_PROPERTY(QString trackInsertName READ trackInsertName NOTIFY trackChanged)
  Q_PROPERTY(bool trackInPair READ trackInPair WRITE setTrackInPair NOTIFY trackChanged)
  Q_PROPERTY(int selectedStrip READ selectedStrip WRITE setSelectedStrip NOTIFY selectedStripChanged)
  // Full-channel popout: the strip's expand affordance opens it; the TRACK
  // property family above then addresses the same slot, so the popout shows
  // exactly the channel it was opened on.
  Q_PROPERTY(bool channelOpen READ channelOpen NOTIFY channelOpenChanged)
  Q_PROPERTY(int channelSlot READ channelSlot NOTIFY channelOpenChanged)
  Q_PROPERTY(QString channelEqSummary READ channelEqSummary NOTIFY channelOpenChanged)
  Q_PROPERTY(QString channelInsertName READ channelInsertName NOTIFY channelChanged)
  Q_PROPERTY(bool channelInsertBypass READ channelInsertBypass WRITE setChannelInsertBypass NOTIFY channelChanged)
  Q_PROPERTY(double masterDb READ masterDb WRITE setMasterDb NOTIFY masterChanged)
  Q_PROPERTY(double meterLeft READ meterLeft NOTIFY metersChanged)
  Q_PROPERTY(double meterRight READ meterRight NOTIFY metersChanged)

 public:
  enum Roles {
    IndexRole = Qt::UserRole + 1,
    SlotRole,
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
    InputTextRole,
    OutputTextRole,
    SelectedRole,
  };

  MixerViewModel(MixerModel& model, SessionSettings& settings, EqModel& eq,
                 QObject* parent = nullptr);

  int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

  int bank() const { return bank_; }
  Q_INVOKABLE void setBank(int bank);
  Q_INVOKABLE void stepBank(int delta);
  int stripsPerBank() const { return kStripsPerBank; }
  int stripCount() const { return model_.trackCount(); }
  int pageCount() const;
  bool empty() const { return model_.trackCount() == 0; }
  QString bankText() const;
  bool trackMode() const { return trackMode_; }
  Q_INVOKABLE void setTrackMode(bool enabled);
  int trackIndex() const { return trackIndex_; }
  Q_INVOKABLE void setTrackIndex(int slot);
  // TRACK mode strip buttons are positional: row 0..n-1 in track order.
  Q_INVOKABLE void selectTrackAt(int position);
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
  double cueSend(int slot) const { return model_.cueSend(slot); }
  double out34Send(int slot) const { return model_.out34Send(slot); }
  Q_INVOKABLE double gainDbAt(int slot) const { return model_.gainDb(slot); }
  Q_INVOKABLE double panAt(int slot) const { return model_.pan(slot); }
  Q_INVOKABLE double sendAt(int slot, int send) const { return model_.send(slot, send); }
  Q_INVOKABLE double cueAt(int slot) const { return model_.cueSend(slot); }
  Q_INVOKABLE double out34At(int slot) const { return model_.out34Send(slot); }
  Q_INVOKABLE int monitorAt(int slot) const { return model_.monitor(slot); }
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
  Q_INVOKABLE void setMonitor(int slot, int mode);
  // Per-track I/O assignment (session v2 inCh/inPair/outDst). MixerModel holds
  // the values and forwards them to the engine; the TRACK SHIFT pickers, the
  // strip selector popup and the channel popout are the writers.
  int trackInCh() const { return model_.inCh(trackIndex_); }
  bool trackInPair() const { return model_.inPair(trackIndex_); }
  int trackOutDst() const { return model_.outDst(trackIndex_); }
  QString trackInputText() const { return inputText(trackInCh(), trackInPair()); }
  QString trackOutputText() const { return outputText(trackOutDst()); }
  QString trackInsertName() const { return insertDisplayName(insertIdOf(trackIndex_)); }
  Q_INVOKABLE void setTrackInCh(int channel);
  Q_INVOKABLE void setTrackInPair(bool pair);
  Q_INVOKABLE void setTrackOutDst(int destination);
  // Absolute selector writes for the popup lists: `state` is 0 = none, 1..18
  // mono device channels, 19..27 the nine stereo pairs; `destination` is the
  // four output destinations in engine order.
  Q_INVOKABLE int inSourceState(int slot) const;
  Q_INVOKABLE void setInSourceState(int slot, int state);
  Q_INVOKABLE int outDstOf(int slot) const { return model_.outDst(slot); }
  Q_INVOKABLE void setOutDstOf(int slot, int destination);
  // Device capture channels the engine can select as a source: the model's
  // device width, not a track count. "None" and the stereo pairs are derived
  // from it, so a wider device registry extends the list without a code change.
  static int inputChannelCount() { return audio::kMaxDeviceInputs; }
  static QStringList inputSourceLabels();
  static QStringList outputLabels();
  static QString inputSourceLabel(int state);
  static QString outputLabel(int destination);
  static QString inputText(int channel, bool pair);
  static QString outputText(int destination);
  // Panel/touch steppers: -1/+1 through the same lists. Both wrap.
  Q_INVOKABLE void cycleTrackInCh(int delta);
  Q_INVOKABLE void cycleTrackOutDst(int delta);
  Q_INVOKABLE QString cycleInSource(int slot, int delta);
  Q_INVOKABLE QString toggleInPair(int slot);
  Q_INVOKABLE QString inSourceText(int slot) const;

  // ---- dynamic track list -----------------------------------------------
  // ADD TRACK appends the first free slot with the name and I/O the popup
  // collected; REMOVE TRACK (confirmed in QML/AppController) removes the strip
  // but leaves the slot's configuration and takes in the project.
  Q_INVOKABLE bool addTrack(int slot, const QString& name, int inCh, bool inPair,
                            int outDst);
  Q_INVOKABLE void removeTrack(int slot);
  Q_INVOKABLE int nextFreeSlot() const { return model_.firstInactiveSlot(); }
  Q_INVOKABLE QString suggestedTrackName(int slot) const;
  Q_INVOKABLE bool trackSlotActive(int slot) const { return model_.trackActive(slot); }
  Q_INVOKABLE int positionOf(int slot) const { return model_.positionOfSlot(slot); }
  // Session load: replaces the ordered list and resets the model.
  void reloadTracks();
  // Re-evaluate the Auto marker after an active-take change (Auto shows only
  // when a take makes it choose the take over the live input).
  void notifyActiveTakesChanged();
  int selectedStrip() const { return selectedStrip_; }
  void setSelectedStrip(int slot);
  double masterDb() const { return model_.masterDb(); }
  Q_INVOKABLE void setMasterDb(double db);
  double meterLeft() const { return meterLeft_; }
  double meterRight() const { return meterRight_; }

  // ---- full channel popout ----------------------------------------------
  // Opening points the TRACK property family at the strip's slot, so the popup
  // reads and writes the same values as that channel's strip.
  Q_INVOKABLE void openChannel(int slot);
  Q_INVOKABLE void closeChannel();
  bool channelOpen() const { return channelOpen_; }
  int channelSlot() const { return trackIndex_; }
  QString channelEqSummary() const { return channelEqSummary_; }
  QString channelInsertName() const;
  bool channelInsertBypass() const;
  void setChannelInsertBypass(bool bypass);
  Q_INVOKABLE void toggleChannelInsertBypass();
  // Per-track FX insert selector (0 = none, 1 = builtin.delay, 2 =
  // builtin.reverb). The engine instance path lands in parallel; the selector,
  // the bypass and the session vector are what this wave ships.
  static constexpr int kInsertChoiceCount = 3;
  static QStringList insertLabels();
  static QString insertId(int choice);
  static int insertChoice(const QString& id);
  static QString insertDisplayName(const QString& id);
  QString insertIdOf(int slot) const;
  Q_INVOKABLE QString trackInsertNameAt(int slot) const {
    return insertDisplayName(insertIdOf(slot));
  }
  Q_INVOKABLE int insertChoiceAt(int slot) const;
  Q_INVOKABLE void setInsertChoiceAt(int slot, int choice);
  Q_INVOKABLE bool insertBypassAt(int slot) const;
  Q_INVOKABLE void setInsertBypassAt(int slot, bool bypass);

  // QML writes and panel-focus adjustments.
  Q_INVOKABLE void setGainDb(int slot, double db);
  Q_INVOKABLE void setPan(int slot, double pan);
  Q_INVOKABLE void setSend(int slot, int send, double level);
  Q_INVOKABLE void setCue(int slot, double level);
  Q_INVOKABLE void setOut34(int slot, double level);
  Q_INVOKABLE void toggleMute(int slot);
  Q_INVOKABLE void toggleSolo(int slot);
  Q_INVOKABLE void toggleArm(int slot);
  Q_INVOKABLE void adjust(int slot, const QString& parameter, double delta);

  // Called by the controller timer after reading the audio peaks.
  void setMeters(double left, double right);
  void setStripMeters(const std::array<double, audio::kMaxTracks>& levels);
  // Deterministic clip/meter demo for the offscreen screenshot run.
  Q_INVOKABLE void showClipDemo();
  Q_INVOKABLE void clearClipDemo();
  // Re-reads every value (project load, MIDI learn writes).
  void refresh();

 signals:
  void bankChanged();
  void tracksChanged();
  void trackModeChanged();
  void trackChanged();
  void selectedStripChanged();
  void masterChanged();
  void metersChanged();
  void channelOpenChanged();
  void channelChanged();

 private:
  double stripLevel(int slot) const;
  bool stripClip(int slot) const;
  QString monitorMark(int slot) const;
  void clampSelection();
  void updateChannelSummary();

  MixerModel& model_;
  SessionSettings& settings_;
  EqModel& eq_;
  int bank_ = 0;
  int selectedStrip_ = 0;
  bool trackMode_ = false;
  int trackIndex_ = 0;
  bool channelOpen_ = false;
  QString channelEqSummary_;
  double meterLeft_ = 0.0;
  double meterRight_ = 0.0;
  std::array<double, audio::kMaxTracks> stripLevels_{};
  std::array<ClipHold, audio::kMaxTracks> clipHolds_{};
  bool clipDemo_ = false;
};

}  // namespace mnky::app
