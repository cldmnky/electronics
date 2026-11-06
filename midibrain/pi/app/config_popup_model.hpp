#pragma once
// Draft state for the generic configuration modal. The owner's wave-end rule
// is "no apply buttons": every setting is live, so each setter writes the open
// kind's values to the models immediately; OK/CLOSE keeps them and CANCEL
// restores the values the popup opened with. The summary properties are
// one-line live readouts for the buttons that open each popup; the list kinds
// (input source, output destination, FX slot, add track) are tap-to-open
// selector popups with the full option list.
//
// The X/Y parameter kinds ("delay", "delaydivision", "reverb", "reverbmodel")
// edit one per-track FX rack slot: AppController points the popup at the
// selected track's FX1/FX2 with setFxTarget() before openKind(), so what the
// popup shows is the effect that strip actually runs.
#include <QObject>
#include <QString>
#include <QStringList>

#include <array>
#include <cstdint>
#include <vector>

#include "app_state.hpp"
#include "eq_model.hpp"
#include "mixer_model.hpp"
#include "session_settings.hpp"

namespace mnky::app {

class RoutingViewModel;
class TimelineViewModel;
class MixerViewModel;

class ConfigPopupModel final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool open READ open NOTIFY openChanged)
  Q_PROPERTY(QString kind READ kind NOTIFY openChanged)
  Q_PROPERTY(QString title READ title NOTIFY openChanged)
  Q_PROPERTY(int clickMode READ clickMode WRITE setClickMode NOTIFY changed)
  Q_PROPERTY(int clickOutput READ clickOutput WRITE setClickOutput NOTIFY changed)
  Q_PROPERTY(double clickGainDb READ clickGainDb WRITE setClickGainDb NOTIFY changed)
  Q_PROPERTY(int clickBars READ clickBars WRITE setClickBars NOTIFY changed)
  Q_PROPERTY(int sendCue READ sendCue WRITE setSendCue NOTIFY changed)
  Q_PROPERTY(int sendFx1 READ sendFx1 WRITE setSendFx1 NOTIFY changed)
  Q_PROPERTY(int sendFx2 READ sendFx2 WRITE setSendFx2 NOTIFY changed)
  Q_PROPERTY(int sendOut34 READ sendOut34 WRITE setSendOut34 NOTIFY changed)
  Q_PROPERTY(int sendRec READ sendRec WRITE setSendRec NOTIFY changed)
  Q_PROPERTY(int phonesSrc READ phonesSrc WRITE setPhonesSrc NOTIFY changed)
  Q_PROPERTY(int cueStyle READ cueStyle WRITE setCueStyle NOTIFY changed)
  Q_PROPERTY(int out34Mode READ out34Mode WRITE setOut34Mode NOTIFY changed)
  Q_PROPERTY(bool compOn READ compOn WRITE setCompOn NOTIFY changed)
  Q_PROPERTY(double compThreshDb READ compThreshDb WRITE setCompThreshDb NOTIFY changed)
  Q_PROPERTY(double compRatio READ compRatio WRITE setCompRatio NOTIFY changed)
  Q_PROPERTY(double compAttackMs READ compAttackMs WRITE setCompAttackMs NOTIFY changed)
  Q_PROPERTY(double compReleaseMs READ compReleaseMs WRITE setCompReleaseMs NOTIFY changed)
  Q_PROPERTY(double compMakeupDb READ compMakeupDb WRITE setCompMakeupDb NOTIFY changed)
  Q_PROPERTY(int compMode READ compMode WRITE setCompMode NOTIFY changed)
  // TRACK SHIFT INPUT MON: the four-way selector collapsed into a summary
  // button. The popup edits a draft of the selected track's monitor mode.
  Q_PROPERTY(int trackMonitor READ trackMonitor WRITE setTrackMonitor NOTIFY changed)
  Q_PROPERTY(QString metronomeSummary READ metronomeSummary NOTIFY summariesChanged)
  Q_PROPERTY(QString sendSummary READ sendSummary NOTIFY summariesChanged)
  Q_PROPERTY(QString monitorSummary READ monitorSummary NOTIFY summariesChanged)
  Q_PROPERTY(QString compSummary READ compSummary NOTIFY summariesChanged)
  Q_PROPERTY(int eqType READ eqType WRITE setEqType NOTIFY changed)
  Q_PROPERTY(int delayBeat READ delayBeat WRITE setDelayBeat NOTIFY changed)
  Q_PROPERTY(int delayPing READ delayPing WRITE setDelayPing NOTIFY changed)
  Q_PROPERTY(int delayDivision READ delayDivision WRITE setDelayDivision NOTIFY changed)
  Q_PROPERTY(QString divisionText READ divisionText NOTIFY changed)
  Q_PROPERTY(double delayFeedback READ delayFeedback WRITE setDelayFeedback NOTIFY changed)
  Q_PROPERTY(double delayMix READ delayMix WRITE setDelayMix NOTIFY changed)
  Q_PROPERTY(int reverbModel READ reverbModel WRITE setReverbModel NOTIFY changed)
  Q_PROPERTY(double reverbPredelayMs READ reverbPredelayMs WRITE setReverbPredelayMs NOTIFY changed)
  Q_PROPERTY(double reverbDecay READ reverbDecay WRITE setReverbDecay NOTIFY changed)
  Q_PROPERTY(double reverbDamping READ reverbDamping WRITE setReverbDamping NOTIFY changed)
  // EDIT timeline region ops (kind "timelineops"): op picker plus gain/fade
  // drafts. OK runs the op through TimelineViewModel; RENDER stays blocked and
  // shows the missing core call instead of closing on a fake success.
  Q_PROPERTY(QStringList timelineOps READ timelineOps CONSTANT)
  Q_PROPERTY(int timelineOp READ timelineOp WRITE setTimelineOp NOTIFY changed)
  Q_PROPERTY(double timelineGainDb READ timelineGainDb WRITE setTimelineGainDb NOTIFY changed)
  Q_PROPERTY(double timelineFadeInBeats READ timelineFadeInBeats WRITE setTimelineFadeInBeats NOTIFY changed)
  Q_PROPERTY(double timelineFadeOutBeats READ timelineFadeOutBeats WRITE setTimelineFadeOutBeats NOTIFY changed)
  Q_PROPERTY(QString timelineOpName READ timelineOpName NOTIFY changed)
  // D5: the region the op will use and whether APPLY can run. The header line
  // names IN/OUT; an invalid range disables the verb and states the reason
  // inline instead of leaving a live button that can only fail.
  Q_PROPERTY(QString regionLine READ regionLine NOTIFY changed)
  Q_PROPERTY(bool regionApplies READ regionApplies NOTIFY changed)
  Q_PROPERTY(QString regionReason READ regionReason NOTIFY changed)
  // Only the fields the chosen op reads are shown (GAIN for GAIN, the fades
  // for FADES); the waveform preview follows the same rule.
  Q_PROPERTY(bool showGainField READ showGainField NOTIFY changed)
  Q_PROPERTY(bool showFadeFields READ showFadeFields NOTIFY changed)
  Q_PROPERTY(bool showRegionPreview READ showRegionPreview NOTIFY changed)
  Q_PROPERTY(QString timelineError READ timelineError NOTIFY changed)
  // Per-pipe MIDI channel scope (session::MidiPipe::chMask, kind
  // "pipechannels"): a draft 16-bit mask plus the live compact readout. The
  // popup edits the draft; OK writes it through RoutingViewModel/MidiGlue.
  Q_PROPERTY(int pipeChannelMask READ pipeChannelMask NOTIFY changed)
  Q_PROPERTY(QString pipeChannelText READ pipeChannelText NOTIFY changed)
  Q_PROPERTY(QString pipeScopeContext READ pipeScopeContext NOTIFY changed)
  // Footer text: the popup states its own OK/CANCEL contract (live values vs
  // the add-track create and the region-op run).
  Q_PROPERTY(QString footerHint READ footerHint NOTIFY openChanged)
  // C9: the primary button states its verb (ADD TRACK, APPLY TRIM, ...); the
  // header repeats it. Only a pure settings popup keeps OK.
  Q_PROPERTY(QString acceptLabel READ acceptLabel NOTIFY changed)
  // ---- selector popups (tap-to-open lists) -------------------------------
  // INPUT SOURCE: 0 = none, 1..18 mono device channels, 19..27 stereo pairs.
  // Every pick writes MixerModel the moment it is tapped.
  Q_PROPERTY(QString inputSourceTitle READ inputSourceTitle NOTIFY openChanged)
  Q_PROPERTY(QStringList inputSourceLabels READ inputSourceLabels CONSTANT)
  Q_PROPERTY(int inputSourceIndex READ inputSourceIndex WRITE setInputSourceIndex NOTIFY changed)
  // OUTPUT DEST: Main 1&2 / OUT3&4 / phones / none (engine destination order).
  Q_PROPERTY(QString outputTitle READ outputTitle NOTIFY openChanged)
  Q_PROPERTY(QStringList outputLabels READ outputLabels CONSTANT)
  Q_PROPERTY(int outputIndex READ outputIndex WRITE setOutputIndex NOTIFY changed)
  // Per-track FX rack slot: NONE / DELAY / REVERB plus one entry per discovered
  // CLAP plugin. Selecting a plugin loads it into that track slot; the note
  // states the 16-instance refusal instead of silently changing the slot.
  Q_PROPERTY(QString fxSlotTitle READ fxSlotTitle NOTIFY openChanged)
  Q_PROPERTY(QStringList fxSlotLabels READ fxSlotLabels CONSTANT)
  Q_PROPERTY(int fxSlotChoice READ fxSlotChoice WRITE setFxSlotChoice NOTIFY changed)
  Q_PROPERTY(QString fxSlotNote READ fxSlotNote NOTIFY changed)
  // ADD TRACK: name plus input (none / mono 1..18 / pair) and output; OK adds
  // the strip, CANCEL adds nothing. The name button opens the shared keypad.
  Q_PROPERTY(QString addTrackName READ addTrackName WRITE setAddTrackName NOTIFY changed)
  Q_PROPERTY(int addTrackInputMode READ addTrackInputMode WRITE setAddTrackInputMode NOTIFY changed)
  Q_PROPERTY(int addTrackChannel READ addTrackChannel WRITE setAddTrackChannel NOTIFY changed)
  Q_PROPERTY(int addTrackOutput READ addTrackOutput WRITE setAddTrackOutput NOTIFY changed)
  Q_PROPERTY(QString addTrackSummary READ addTrackSummary NOTIFY changed)
  Q_PROPERTY(QString addTrackError READ addTrackError NOTIFY changed)
  // Upper bound of the ADD TRACK channel grid: the model's device capture
  // width, so a wider device registry grows the grid without a QML edit.
  Q_PROPERTY(int addTrackChannelCount READ addTrackChannelCount CONSTANT)
  // Pipe chain selectors on the Routing SHIFT > PIPES face: FROM/TO/ADD type
  // are tap-to-open lists instead of prev/next steppers. The type list carries
  // the generated schema role per type and the selected type's one-line doc.
  Q_PROPERTY(QStringList pipeSourceLabels READ pipeSourceLabels NOTIFY changed)
  Q_PROPERTY(int pipeSourceIndex READ pipeSourceIndex WRITE setPipeSourceIndex NOTIFY changed)
  Q_PROPERTY(QStringList pipeDestLabels READ pipeDestLabels NOTIFY changed)
  Q_PROPERTY(int pipeDestIndex READ pipeDestIndex WRITE setPipeDestIndex NOTIFY changed)
  Q_PROPERTY(QStringList pipeTypeLabels READ pipeTypeLabels CONSTANT)
  // Roles of the current type list (generator-filtered while a generator route
  // waits for its head), so a row's role always matches its label.
  Q_PROPERTY(QStringList pipeTypeRoles READ pipeTypeRoles NOTIFY changed)
  // True while the route is a generator with no head yet; the type list then
  // contains only generator-role pipes.
  Q_PROPERTY(bool pipeTypeHeadOnly READ pipeTypeHeadOnly NOTIFY changed)
  Q_PROPERTY(QString pipeTypeDocs READ pipeTypeDocs NOTIFY changed)
  Q_PROPERTY(int pipeTypeIndex READ pipeTypeIndex WRITE setPipeTypeIndex NOTIFY changed)
  // One bool/enum pipe parameter (kind "pipeparam"): the parameter card's
  // choice list. A tap writes the positional parameter through RoutingViewModel
  // (live, like every selector popup); CANCEL restores what the popup opened
  // with.
  Q_PROPERTY(QStringList pipeParamChoices READ pipeParamChoices NOTIFY changed)
  Q_PROPERTY(int pipeParamChoice READ pipeParamChoice NOTIFY changed)
  Q_PROPERTY(QString pipeParamContext READ pipeParamContext NOTIFY changed)

 public:
  ConfigPopupModel(AppState& state, MixerModel& mixer, SessionSettings& settings,
                   EqModel& eq, QObject* parent = nullptr);

  bool open() const { return open_; }
  QString kind() const { return kind_; }
  QString title() const { return title_; }
  int clickMode() const { return clickMode_; }
  int clickOutput() const { return clickOutput_; }
  double clickGainDb() const { return clickGainDb_; }
  int clickBars() const { return clickBars_; }
  int sendCue() const { return sendCue_; }
  int sendFx1() const { return sendFx1_; }
  int sendFx2() const { return sendFx2_; }
  int sendOut34() const { return sendOut34_; }
  int sendRec() const { return sendRec_; }
  int phonesSrc() const { return phonesSrc_; }
  int cueStyle() const { return cueStyle_; }
  int out34Mode() const { return out34Mode_; }
  bool compOn() const { return compOn_; }
  double compThreshDb() const { return compThreshDb_; }
  double compRatio() const { return compRatio_; }
  double compAttackMs() const { return compAttackMs_; }
  double compReleaseMs() const { return compReleaseMs_; }
  double compMakeupDb() const { return compMakeupDb_; }
  int compMode() const { return compMode_; }
  int trackMonitor() const { return trackMonitor_; }
  QString metronomeSummary() const { return metronomeSummary_; }
  QString sendSummary() const { return sendSummary_; }
  QString monitorSummary() const { return monitorSummary_; }
  QString compSummary() const { return compSummary_; }
  int eqType() const { return eqType_; }
  int delayBeat() const { return delayBeat_; }
  int delayPing() const { return delayPing_; }
  int delayDivision() const { return delayDivision_; }
  QString divisionText() const;
  double delayFeedback() const { return delayFeedback_; }
  double delayMix() const { return delayMix_; }
  int reverbModel() const { return reverbModel_; }
  double reverbPredelayMs() const { return reverbPredelayMs_; }
  double reverbDecay() const { return reverbDecay_; }
  double reverbDamping() const { return reverbDamping_; }
  QStringList timelineOps() const;
  int timelineOp() const { return timelineOp_; }
  double timelineGainDb() const { return timelineGainDb_; }
  double timelineFadeInBeats() const { return timelineFadeInBeats_; }
  double timelineFadeOutBeats() const { return timelineFadeOutBeats_; }
  QString timelineOpName() const;
  QString regionLine() const;
  bool regionApplies() const;
  QString regionReason() const;
  bool showGainField() const { return timelineOp_ == 2; }
  bool showFadeFields() const { return timelineOp_ == 3; }
  bool showRegionPreview() const { return showGainField() || showFadeFields(); }
  QString timelineError() const { return timelineError_; }
  int pipeChannelMask() const { return pipeChannelMask_; }
  QString pipeChannelText() const;
  QString pipeScopeContext() const { return pipeScopeContext_; }
  QString footerHint() const;
  QString acceptLabel() const;
  QString inputSourceTitle() const;
  QStringList inputSourceLabels() const;
  int inputSourceIndex() const { return inputSourceIndex_; }
  void setInputSourceIndex(int value);
  QString outputTitle() const;
  QStringList outputLabels() const;
  int outputIndex() const { return outputIndex_; }
  void setOutputIndex(int value);
  QString fxSlotTitle() const;
  QStringList fxSlotLabels() const;
  int fxSlotChoice() const { return fxSlotChoice_; }
  void setFxSlotChoice(int value);
  QString fxSlotNote() const;
  QString addTrackName() const { return addTrackName_; }
  void setAddTrackName(const QString& name);
  int addTrackInputMode() const { return addTrackInputMode_; }
  void setAddTrackInputMode(int mode);
  int addTrackChannel() const { return addTrackChannel_; }
  void setAddTrackChannel(int channel);
  int addTrackOutput() const { return addTrackOutput_; }
  void setAddTrackOutput(int destination);
  QString addTrackSummary() const;
  QString addTrackError() const { return addTrackError_; }
  int addTrackChannelCount() const;
  QStringList pipeSourceLabels() const { return pipeSourceLabels_; }
  int pipeSourceIndex() const { return pipeSourceIndex_; }
  void setPipeSourceIndex(int value);
  QStringList pipeDestLabels() const { return pipeDestLabels_; }
  int pipeDestIndex() const { return pipeDestIndex_; }
  void setPipeDestIndex(int value);
  QStringList pipeTypeLabels() const;
  QStringList pipeTypeRoles() const;
  bool pipeTypeHeadOnly() const;
  QString pipeTypeDocs() const;
  int pipeTypeIndex() const { return pipeTypeIndex_; }
  void setPipeTypeIndex(int value);
  // The card that opens the choice list names the pipe slot and positional
  // parameter it edits; AppController points the popup at it before openKind().
  void setPipeParamTarget(int pipe, int param);
  int pipeParamSlot() const { return pipeParamSlot_; }
  int pipeParamIndex() const { return pipeParamIndex_; }
  QStringList pipeParamChoices() const { return pipeParamChoices_; }
  int pipeParamChoice() const { return pipeParamChoice_; }
  QString pipeParamContext() const { return pipeParamContext_; }
  Q_INVOKABLE void setPipeParamChoice(int choice);

  void setClickMode(int value);
  void setClickOutput(int value);
  void setClickGainDb(double value);
  void setClickBars(int value);
  void setSendCue(int value);
  void setSendFx1(int value);
  void setSendFx2(int value);
  void setSendOut34(int value);
  void setSendRec(int value);
  void setPhonesSrc(int value);
  void setCueStyle(int value);
  void setOut34Mode(int value);
  void setCompOn(bool value);
  void setCompThreshDb(double value);
  void setCompRatio(double value);
  void setCompAttackMs(double value);
  void setCompReleaseMs(double value);
  void setCompMakeupDb(double value);
  void setCompMode(int value);
  void setTrackMonitor(int value);
  void setEqType(int value);
  void setDelayBeat(int value);
  void setDelayPing(int value);
  void setDelayDivision(int value);
  void setDelayFeedback(double value);
  void setDelayMix(double value);
  void setReverbModel(int value);
  void setReverbPredelayMs(double value);
  void setReverbDecay(double value);
  void setReverbDamping(double value);
  void setTimelineOp(int value);
  void setTimelineGainDb(double value);
  void setTimelineFadeInBeats(double value);
  void setTimelineFadeOutBeats(double value);
  Q_INVOKABLE void togglePipeChannel(int channel);
  void setPipeChannelMask(int mask);
  // Presets: 0 = all, 1 = CH 1-4, 2 = 5-8, 3 = 9-12, 4 = 13-16.
  Q_INVOKABLE void setPipeChannelPreset(int preset);

  // Set once by AppController after both objects exist; the ops popup delegates
  // preview and the destructive-op refusal to the timeline model.
  void setTimeline(TimelineViewModel* timeline) { timeline_ = timeline; }
  // Set once by AppController; the pipe-scope popup reads and writes the
  // selected pipe's chMask through this view model.
  void setRouting(RoutingViewModel* routing) { routing_ = routing; }
  // Set once by AppController; the selector and add-track popups read the
  // dynamic track list and write the strip they target through this view
  // model.
  void setMixer(MixerViewModel* mixer) { mixerVm_ = mixer; }
  // AppController points the INPUT MON popup at the TRACK screen's selected
  // strip before openKind("trackmonitor"); MixerModel itself is index-free.
  void setTrackMonitorIndex(int index) { trackMonitorSlot_ = index; }
  // AppController points a selector popup at the strip it was opened from.
  void setInputTarget(int slot) { inputTargetSlot_ = slot; }
  void setOutputTarget(int slot) { outputTargetSlot_ = slot; }
  // The rack slot the FX-slot selector and the delay/reverb parameter kinds
  // edit: (track slot, 0 = FX1 / 1 = FX2).
  void setFxTarget(int track, int slot);
  int inputTarget() const { return inputTargetSlot_; }
  int outputTarget() const { return outputTargetSlot_; }
  int fxTargetTrack() const { return fxTrackSlot_; }
  int fxTargetSlot() const { return fxSlotIndex_; }
  // ADD TRACK always appends the first free slot; AppController pre-fills the
  // name with that slot's default and the input with the slot's device channel.
  void setAddTrackSlot(int slot);

  // AppController entry points.
  bool openKind(const QString& kind);
  void accept();
  void cancel();
  void refreshSummaries();

 signals:
  void openChanged();
  void changed();
  void summariesChanged();

 private:
  void loadDraft();
  // emit changed() and push the open kind's values to the models (no apply
  // button anywhere).
  void changedLive();
  // Live write of the open kind's values (no apply button); a setter calls it
  // after every change so the models already hold what the popup shows.
  void applyDraft();
  void captureRestore();
  void restoreRestore();
  static int clampChoice(int value, int maximum);
  static int clampBoolChoice(int value);

  // Values the open popup must put back on CANCEL. Only the open kind's group
  // is written back, so a cancel can never resurrect an unrelated stale value.
  struct Restore {
    int clickMode = 0;
    int clickOutput = 0;
    float clickGainDb = 0.0f;
    int clickBars = 1;
    int sendCue = 1;
    int sendFx1 = 1;
    int sendFx2 = 1;
    int sendOut34 = 1;
    int sendRec = 1;
    int phonesSrc = 0;
    int cueStyle = 0;
    int out34Mode = 0;
    bool compOn = false;
    float compThreshDb = -12.0f;
    float compRatio = 2.0f;
    float compAttackMs = 10.0f;
    float compReleaseMs = 100.0f;
    float compMakeupDb = 0.0f;
    int compMode = 0;
    int trackMonitor = 1;
    int eqType = 0;
    int eqBand = 0;
    int delayChoice[3] = {0, 0, 3};
    double delayValue[2] = {0.35, 1.0};
    int reverbChoice = 0;
    double reverbValue[3] = {20.0, 0.7, 0.5};
    std::uint16_t pipeMask = 0xFFFF;
    int pipeSource = 0;
    int pipeDestination = 0;
    int pipeType = 0;
    int pipeParam = 0;
    int inputSource = 0;
    int outputDst = 0;
    int fxChoice = 0;
    // The target slot's parameter vector as the popup opened with: CANCEL puts
    // every parameter back, including the ones the kind does not display.
    std::vector<double> fxParams;
  };

  AppState& state_;
  MixerModel& mixer_;
  SessionSettings& settings_;
  EqModel& eq_;
  bool open_ = false;
  QString kind_;
  QString title_;
  int clickMode_ = 0;
  int clickOutput_ = 0;
  double clickGainDb_ = 0.0;
  int clickBars_ = 1;
  int sendCue_ = 1;
  int sendFx1_ = 1;
  int sendFx2_ = 1;
  int sendOut34_ = 1;
  int sendRec_ = 1;
  int phonesSrc_ = 0;
  int cueStyle_ = 0;
  int out34Mode_ = 0;
  bool compOn_ = false;
  double compThreshDb_ = -12.0;
  double compRatio_ = 2.0;
  double compAttackMs_ = 10.0;
  double compReleaseMs_ = 100.0;
  double compMakeupDb_ = 0.0;
  int compMode_ = 0;
  int trackMonitor_ = 1;
  int trackMonitorSlot_ = 0;
  QString metronomeSummary_;
  QString sendSummary_;
  QString monitorSummary_;
  QString compSummary_;
  // EQ band type / delay / reverb secondary-parameter drafts.
  int eqType_ = 0;
  int delayBeat_ = 0;
  int delayPing_ = 0;
  int delayDivision_ = 3;
  double delayFeedback_ = 0.35;
  double delayMix_ = 1.0;
  int reverbModel_ = 0;
  double reverbPredelayMs_ = 20.0;
  double reverbDecay_ = 0.7;
  double reverbDamping_ = 0.5;
  TimelineViewModel* timeline_ = nullptr;
  RoutingViewModel* routing_ = nullptr;
  MixerViewModel* mixerVm_ = nullptr;
  // Selector targets and their live values.
  int inputTargetSlot_ = 0;
  int inputSourceIndex_ = 0;
  int outputTargetSlot_ = 0;
  int outputIndex_ = 0;
  int fxTrackSlot_ = 0;
  int fxSlotIndex_ = 0;
  int fxSlotChoice_ = 0;
  QString fxSlotNote_;
  int addTrackSlot_ = 0;
  QString addTrackName_;
  int addTrackInputMode_ = 1;
  int addTrackChannel_ = 0;
  int addTrackOutput_ = 0;
  QString addTrackError_;
  Restore restore_{};
  std::uint16_t pipeChannelMask_ = 0xFFFF;
  QStringList pipeSourceLabels_;
  QStringList pipeDestLabels_;
  std::vector<int> pipeSourcePorts_;
  std::vector<int> pipeDestPorts_;
  int pipeSourceIndex_ = 0;
  int pipeDestIndex_ = 0;
  int pipeTypeIndex_ = 0;
  // The bool/enum parameter the choice popup edits, its choices and the
  // current choice index (value - schema minimum).
  int pipeParamSlot_ = 0;
  int pipeParamIndex_ = 0;
  int pipeParamMinimum_ = 0;
  int pipeParamChoice_ = 0;
  QStringList pipeParamChoices_;
  QString pipeParamContext_;
  QString pipeScopeContext_;
  int timelineOp_ = 0;
  double timelineGainDb_ = 0.0;
  double timelineFadeInBeats_ = 0.0;
  double timelineFadeOutBeats_ = 0.0;
  QString timelineError_;
};

}  // namespace mnky::app
