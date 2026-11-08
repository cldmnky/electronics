#pragma once
// QML model for the dynamic banked mixer and the single-track TRACK mode.
// The mixer shows the session's ordered track list, three strips per page; the
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
#include <cstdint>

#include "eq_model.hpp"
#include "logic.hpp"
#include "mixer_model.hpp"
#include "session_settings.hpp"

namespace mnky::app {

class FxManager;

// Three strips per page: eight or nine strips on one page cramp the faders,
// and three leaves room for taller faders and real meters. The page count
// comes from the track list (ceil(tracks / kStripsPerBank)), never from a
// capacity constant.
inline constexpr int kStripsPerBank = 3;

class MixerViewModel final : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(int bank READ bank WRITE setBank NOTIFY bankChanged)
  Q_PROPERTY(int stripsPerBank READ stripsPerBank CONSTANT)
  Q_PROPERTY(int stripCount READ stripCount NOTIFY tracksChanged)
  Q_PROPERTY(int pageCount READ pageCount NOTIFY tracksChanged)
  Q_PROPERTY(bool empty READ empty NOTIFY tracksChanged)
  Q_PROPERTY(QString bankText READ bankText NOTIFY tracksChanged)
  // one row per three-track page, for the pager grid ({index, label,
  // empty, current}). A page is empty when its first slot is past the list.
  Q_PROPERTY(QVariantList bankPages READ bankPages NOTIFY tracksChanged)
  Q_PROPERTY(bool trackMode READ trackMode WRITE setTrackMode NOTIFY trackModeChanged)
  Q_PROPERTY(int trackIndex READ trackIndex WRITE setTrackIndex NOTIFY trackChanged)
  Q_PROPERTY(QString trackName READ trackName NOTIFY trackChanged)
  Q_PROPERTY(double trackGainDb READ trackGainDb WRITE setTrackGainDb NOTIFY trackChanged)
  Q_PROPERTY(double trackTrimDb READ trackTrimDb WRITE setTrackTrimDb NOTIFY trackChanged)
  Q_PROPERTY(double trackPan READ trackPan WRITE setTrackPan NOTIFY trackChanged)
  Q_PROPERTY(QString trackPanText READ trackPanText NOTIFY trackChanged)
  // FX1/FX2 mix knobs: the per-track rack slot's wet (session fxSlots[i].wet,
  // 0..1 dry/wet crossfade). The deprecated send values are session-load only.
  Q_PROPERTY(double trackFxWet1 READ trackFxWet1 WRITE setTrackFxWet1 NOTIFY trackChanged)
  Q_PROPERTY(double trackFxWet2 READ trackFxWet2 WRITE setTrackFxWet2 NOTIFY trackChanged)
  Q_PROPERTY(double trackCue READ trackCue WRITE setTrackCue NOTIFY trackChanged)
  Q_PROPERTY(double trackOut34 READ trackOut34 WRITE setTrackOut34 NOTIFY trackChanged)
  Q_PROPERTY(bool trackMute READ trackMute WRITE setTrackMute NOTIFY trackChanged)
  Q_PROPERTY(bool trackSolo READ trackSolo WRITE setTrackSolo NOTIFY trackChanged)
  Q_PROPERTY(bool trackArm READ trackArm WRITE setTrackArm NOTIFY trackChanged)
  Q_PROPERTY(double trackLevel READ trackLevel NOTIFY metersChanged)
  Q_PROPERTY(bool trackClip READ trackClip NOTIFY metersChanged)
  // The selected strip's mode and REC SOURCE: the
  // TRACK screen and the channel popout select LIVE/PLAY/ARM/OVERDUB and
  // PRE/POST/POST+FX through these. (`trackMode` above is the MIXER/TRACK screen
  // toggle, so the per-track value is named selected*.)
  Q_PROPERTY(int selectedMode READ selectedMode WRITE setSelectedMode NOTIFY trackChanged)
  Q_PROPERTY(QString selectedModeWord READ selectedModeWord NOTIFY trackChanged)
  Q_PROPERTY(int selectedRecSource READ selectedRecSource WRITE setSelectedRecSource NOTIFY trackChanged)
  Q_PROPERTY(QString selectedRecSourceText READ selectedRecSourceText NOTIFY trackChanged)
  Q_PROPERTY(QString trackInputText READ trackInputText NOTIFY trackChanged)
  Q_PROPERTY(QString trackOutputText READ trackOutputText NOTIFY trackChanged)
  Q_PROPERTY(bool trackInPair READ trackInPair WRITE setTrackInPair NOTIFY trackChanged)
  Q_PROPERTY(int selectedStrip READ selectedStrip WRITE setSelectedStrip NOTIFY selectedStripChanged)
  // Full-channel popout: the strip's expand affordance opens it; the TRACK
  // property family above then addresses the same slot, so the popout shows
  // exactly the channel it was opened on.
  Q_PROPERTY(bool channelOpen READ channelOpen NOTIFY channelOpenChanged)
  Q_PROPERTY(int channelSlot READ channelSlot NOTIFY channelOpenChanged)
  Q_PROPERTY(QString channelEqSummary READ channelEqSummary NOTIFY channelOpenChanged)
  // The channel popout's FX rack summary: one effect name per slot plus the
  // subline that states the series order and the per-track scope.
  Q_PROPERTY(QString channelFxSummary READ channelFxSummary NOTIFY channelChanged)
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
    // Track mode: the value (0..3) and its word (LIVE/PLAY/ARM/OVERDUB).
    ModeRole,
    ModeWordRole,
    InputTextRole,
    OutputTextRole,
    SelectedRole,
    // FX rack mix knobs (fxSlots[i].wet) and effect names for the strip's
    // SHIFT face, which is the per-strip FX1/FX2 row.
    Fx1WetRole,
    Fx2WetRole,
    Fx1NameRole,
    Fx2NameRole,
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
  QVariantList bankPages() const;
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
  double trackFxWet1() const { return fxWetAt(trackIndex_, 0); }
  Q_INVOKABLE void setTrackFxWet1(double wet) { setFxWetAt(trackIndex_, 0, wet); }
  double trackFxWet2() const { return fxWetAt(trackIndex_, 1); }
  Q_INVOKABLE void setTrackFxWet2(double wet) { setFxWetAt(trackIndex_, 1, wet); }
  double cueSend(int slot) const { return model_.cueSend(slot); }
  double out34Send(int slot) const { return model_.out34Send(slot); }
  Q_INVOKABLE double gainDbAt(int slot) const { return model_.gainDb(slot); }
  Q_INVOKABLE double panAt(int slot) const { return model_.pan(slot); }
  // Compact pan readout of any strip ("C", "L50", "R32"), the same text the
  // model's PanTextRole carries.
  Q_INVOKABLE QString panTextAt(int slot) const;
  Q_INVOKABLE double cueAt(int slot) const { return model_.cueSend(slot); }
  Q_INVOKABLE double out34At(int slot) const { return model_.out34Send(slot); }
  Q_INVOKABLE int modeAt(int slot) const { return model_.mode(slot); }
  Q_INVOKABLE QString modeWordAt(int slot) const;
  Q_INVOKABLE void cycleMode(int slot);
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
  int selectedMode() const { return model_.mode(trackIndex_); }
  QString selectedModeWord() const;
  Q_INVOKABLE void setSelectedMode(int mode);
  Q_INVOKABLE void setMode(int slot, int mode);
  int selectedRecSource() const { return model_.recSource(trackIndex_); }
  QString selectedRecSourceText() const;
  Q_INVOKABLE void setSelectedRecSource(int source);
  Q_INVOKABLE void setRecSource(int slot, int source);
  // Per-track I/O assignment (session v2 inCh/inPair/outDst). MixerModel holds
  // the values and forwards them to the engine; the TRACK SHIFT pickers, the
  // strip selector popup and the channel popout are the writers.
  int trackInCh() const { return model_.inCh(trackIndex_); }
  bool trackInPair() const { return model_.inPair(trackIndex_); }
  int trackOutDst() const { return model_.outDst(trackIndex_); }
  QString trackInputText() const { return inputText(trackInCh(), trackInPair()); }
  QString trackOutputText() const { return outputText(trackOutDst()); }
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
  // Re-evaluate the mode word after an active-take change (the strip shows the
  // mode, which does not depend on takes any more - kept for one emit point).
  void notifyActiveTakesChanged();
  // Number of ARM-mode strips with an active take: the play() warning names
  // them, because ARM suppresses the take voice by contract.
  int armedWithActiveTakeCount() const;
  // A track rename: every strip re-reads its name.
  void notifyTrackNamesChanged();
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
  QString channelFxSummary() const;
  // ---- per-track FX rack ------------------------------------------------
  // Slot index 0 = FX1, 1 = FX2. `slot` is a track slot; the track-scoped
  // helpers above address the TRACK/channel-popout track.
  static constexpr int kFxSlotCount = MixerModel::kFxSlotCount;
  // Selector choices in the popup order: 0 NONE, 1 DELAY, 2 REVERB, then one
  // entry per discovered CLAP plugin in FxManager::plugins() order (index 3 is
  // the first plugin). The labels are live: a build with no discoverable module
  // simply has the three built-ins.
  static constexpr int kFxChoiceBuiltinCount = 3;
  static constexpr int kFxChoicePlugin = 3;
  QStringList fxChoiceLabels() const;
  QString fxChoiceId(int choice) const;
  int fxChoiceOf(const QString& id) const;
  QString fxName(const QString& id) const;
  // AppController installs the app's CLAP host once. Without it the plugin
  // choices are unavailable and the rack stays built-in only (unit tests).
  void setClapHost(FxManager* host) { clap_ = host; }
  // Last refused CLAP selection ("PLUGIN LIMIT REACHED - unload one first"),
  // cleared by the next accepted write. `track`/`slot`-qualified so a note on
  // another card never inherits it.
  QString fxChoiceRefusal(int slot, int index) const;
  void clearFxChoiceRefusal();
  Q_INVOKABLE QString fxNameAt(int slot, int index) const;
  Q_INVOKABLE int fxChoiceAt(int slot, int index) const;
  Q_INVOKABLE bool setFxChoiceAt(int slot, int index, int choice);
  Q_INVOKABLE bool fxBypassAt(int slot, int index) const;
  Q_INVOKABLE void setFxBypassAt(int slot, int index, bool bypass);
  Q_INVOKABLE double fxWetAt(int slot, int index) const;
  Q_INVOKABLE void setFxWetAt(int slot, int index, double wet);
  // One slot's parameter cards: {"index","name","value","text","unit",
  // "minimum","maximum","stepped"} in the effect's stable order. A CLAP id
  // uses the plugin's own clap.params table (its names/ranges), read through
  // FxManager; NONE is empty.
  Q_INVOKABLE QVariantList fxParams(int slot, int index) const;
  Q_INVOKABLE void setFxParamAt(int slot, int index, int param, double value);
  // The subline both rack surfaces state: series order plus scope.
  Q_INVOKABLE QString fxSubline() const;
  Q_INVOKABLE QString fxSlotNote(int slot, int index) const;

  // QML writes and panel-focus adjustments.
  Q_INVOKABLE void setGainDb(int slot, double db);
  Q_INVOKABLE void setPan(int slot, double pan);
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
  // a tap on a meter drops its latch (and its demo crown).
  Q_INVOKABLE void clearClip(int slot);
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
  void fxChanged();

 private:
  double stripLevel(int slot) const;
  bool stripClip(int slot) const;
  void clampSelection();
  void updateChannelSummary();
  void notifyFxChanged(int slot);

  MixerModel& model_;
  SessionSettings& settings_;
  EqModel& eq_;
  FxManager* clap_ = nullptr;
  QString fxChoiceRefusal_;
  int fxRefusalSlot_ = -1;
  int fxRefusalIndex_ = -1;
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
  // Per-strip dismissals of the deterministic demo crowns, so one tap clears
  // one meter without ending the whole screenshot fixture.
  std::array<bool, audio::kMaxTracks> clipDemoCleared_{};
  bool clipDemo_ = false;
};

}  // namespace mnky::app
