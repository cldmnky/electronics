#pragma once
// Draft state for the generic configuration modal. open() copies the live
// session values, the QML popup edits these drafts, accept() writes them back,
// cancel()/EXIT discards them. The summary properties are one-line live
// readouts for the buttons that open each popup.
#include <QObject>
#include <QString>
#include <QStringList>

#include <cstdint>

#include "app_state.hpp"
#include "eq_model.hpp"
#include "mixer_model.hpp"
#include "performance_model.hpp"
#include "session_settings.hpp"

namespace mnky::app {

class RoutingViewModel;
class TimelineViewModel;

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
  Q_PROPERTY(QString timelineError READ timelineError NOTIFY changed)
  // Per-pipe MIDI channel scope (session::MidiPipe::chMask, kind
  // "pipechannels"): a draft 16-bit mask plus the live compact readout. The
  // popup edits the draft; OK writes it through RoutingViewModel/MidiGlue.
  Q_PROPERTY(int pipeChannelMask READ pipeChannelMask NOTIFY changed)
  Q_PROPERTY(QString pipeChannelText READ pipeChannelText NOTIFY changed)
  Q_PROPERTY(QString pipeScopeContext READ pipeScopeContext NOTIFY changed)

 public:
  ConfigPopupModel(AppState& state, MixerModel& mixer, SessionSettings& settings,
                   EqModel& eq, PerformanceModel& performance,
                   QObject* parent = nullptr);

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
  QString timelineError() const { return timelineError_; }
  int pipeChannelMask() const { return pipeChannelMask_; }
  QString pipeChannelText() const;
  QString pipeScopeContext() const { return pipeScopeContext_; }

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
  void commitDraft();
  static int clampChoice(int value, int maximum);
  static int clampBoolChoice(int value);

  AppState& state_;
  MixerModel& mixer_;
  SessionSettings& settings_;
  EqModel& eq_;
  PerformanceModel& performance_;
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
  std::uint16_t pipeChannelMask_ = 0xFFFF;
  QString pipeScopeContext_;
  int timelineOp_ = 0;
  double timelineGainDb_ = 0.0;
  double timelineFadeInBeats_ = 0.0;
  double timelineFadeOutBeats_ = 0.0;
  QString timelineError_;
};

}  // namespace mnky::app
