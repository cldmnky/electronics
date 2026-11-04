#include "config_popup_model.hpp"

#include <algorithm>
#include <cmath>

#include "logic.hpp"
#include "timeline_model.hpp"
#include "timeline_view_model.hpp"

namespace mnky::app {
namespace {
QString textOf(const std::string& text) { return QString::fromStdString(text); }
QString modeName(int mode) {
  return mode == 0 ? QStringLiteral("OFF")
                   : (mode == 1 ? QStringLiteral("REC") : QStringLiteral("ON PLAY"));
}
QString outputName(int output) {
  return output == 0 ? QStringLiteral("MAIN")
                     : (output == 1 ? QStringLiteral("PHONES")
                                    : QStringLiteral("OUT34"));
}
QString sendName(int source) {
  return source == 0 ? QStringLiteral("PRE") : QStringLiteral("POST");
}
}  // namespace

ConfigPopupModel::ConfigPopupModel(AppState& state, MixerModel& mixer,
                                   SessionSettings& settings, EqModel& eq,
                                   PerformanceModel& performance, QObject* parent)
    : QObject(parent),
      state_(state),
      mixer_(mixer),
      settings_(settings),
      eq_(eq),
      performance_(performance) {
  refreshSummaries();
}

int ConfigPopupModel::clampChoice(int value, int maximum) {
  return std::clamp(value, 0, maximum);
}
int ConfigPopupModel::clampBoolChoice(int value) { return value == 1 ? 1 : 0; }

bool ConfigPopupModel::openKind(const QString& kind) {
  if (kind != QStringLiteral("metronome") && kind != QStringLiteral("sends") &&
      kind != QStringLiteral("monitor") && kind != QStringLiteral("comp") &&
      kind != QStringLiteral("eqtype") && kind != QStringLiteral("delay") &&
      kind != QStringLiteral("reverb") && kind != QStringLiteral("timelineops")) {
    return false;
  }
  if (kind == QStringLiteral("timelineops") && timeline_ == nullptr) return false;
  kind_ = kind;
  title_ = kind == QStringLiteral("metronome")   ? QStringLiteral("METRONOME")
           : kind == QStringLiteral("sends")     ? QStringLiteral("SEND SOURCE")
           : kind == QStringLiteral("monitor")   ? QStringLiteral("MONITOR")
           : kind == QStringLiteral("eqtype")    ? QStringLiteral("EQ BAND TYPE")
           : kind == QStringLiteral("delay")     ? QStringLiteral("DELAY")
           : kind == QStringLiteral("reverb")    ? QStringLiteral("REVERB")
           : kind == QStringLiteral("timelineops") ? QStringLiteral("REGION OPS")
                                                 : QStringLiteral("MAIN COMP");
  loadDraft();
  open_ = true;
  emit openChanged();
  emit changed();
  return true;
}

void ConfigPopupModel::loadDraft() {
  clickMode_ = clampChoice(state_.clickMode.load(), 2);
  clickOutput_ = clampChoice(state_.clickOutput.load(), 2);
  clickGainDb_ = state_.clickGainDb.load();
  clickBars_ = state_.clickBars.load();
  sendCue_ = settings_.sends.cue;
  sendFx1_ = settings_.sends.fx1;
  sendFx2_ = settings_.sends.fx2;
  sendOut34_ = settings_.sends.out34;
  sendRec_ = settings_.sends.rec;
  phonesSrc_ = settings_.cue.phonesSrc;
  cueStyle_ = settings_.cue.style;
  out34Mode_ = mixer_.out34Mode();
  compOn_ = settings_.mainComp.on;
  compThreshDb_ = settings_.mainComp.threshDb;
  compRatio_ = settings_.mainComp.ratio;
  compAttackMs_ = settings_.mainComp.attackMs;
  compReleaseMs_ = settings_.mainComp.releaseMs;
  compMakeupDb_ = settings_.mainComp.makeupDb;
  compMode_ = settings_.mainComp.mode;
  eqType_ = eq_.type(eq_.band());
  const int delayGrid = PerformanceModel::kDelayGrid;
  const int reverbGrid = PerformanceModel::kReverbGrid;
  delayBeat_ = static_cast<int>(std::lround(
      performance_.param(delayGrid, audio::fx::Delay::kBeatMode)));
  delayPing_ = static_cast<int>(std::lround(
      performance_.param(delayGrid, audio::fx::Delay::kPingPong)));
  delayDivision_ = static_cast<int>(std::lround(
      performance_.param(delayGrid, audio::fx::Delay::kDivision)));
  delayFeedback_ = performance_.param(delayGrid, audio::fx::Delay::kFeedback);
  delayMix_ = performance_.param(delayGrid, audio::fx::Delay::kMix);
  reverbModel_ = static_cast<int>(std::lround(
      performance_.param(reverbGrid, audio::fx::Reverb::kModel)));
  reverbPredelayMs_ = performance_.param(reverbGrid, audio::fx::Reverb::kPredelayMs);
  reverbDecay_ = performance_.param(reverbGrid, audio::fx::Reverb::kDecay);
  reverbDamping_ = performance_.param(reverbGrid, audio::fx::Reverb::kDamping);
  if (kind_ == QStringLiteral("timelineops")) {
    timelineOp_ = 0;
    timelineGainDb_ = 0.0;
    timelineFadeInBeats_ = 0.0;
    timelineFadeOutBeats_ = 0.0;
    timelineError_.clear();
    if (timeline_ != nullptr) {
      timeline_->setPreviewDraft(timelineGainDb_, timelineFadeInBeats_,
                                 timelineFadeOutBeats_);
    }
  }
}

void ConfigPopupModel::accept() {
  if (!open_) return;
  if (kind_ == QStringLiteral("timelineops")) {
    if (timeline_ == nullptr) return;
    const QString error = timeline_->applyOp(timelineOp_, timelineGainDb_,
                                             timelineFadeInBeats_,
                                             timelineFadeOutBeats_);
    if (!error.isEmpty()) {
      timelineError_ = error;
      emit changed();
      return;
    }
    timelineError_.clear();
    open_ = false;
    emit openChanged();
    refreshSummaries();
    return;
  }
  commitDraft();
  open_ = false;
  emit openChanged();
  refreshSummaries();
}

void ConfigPopupModel::cancel() {
  if (!open_) return;
  open_ = false;
  emit openChanged();
}

void ConfigPopupModel::commitDraft() {
  if (kind_ == QStringLiteral("timelineops")) return;
  state_.clickMode.store(clampChoice(clickMode_, 2));
  state_.clickEnabled.store(clickMode_ != 0);
  state_.clickOutput.store(clampChoice(clickOutput_, 2));
  state_.clickGainDb.store(static_cast<float>(std::clamp(clickGainDb_, -96.0, 20.0)));
  state_.clickLevel.store(std::clamp(
      dbToGain(static_cast<float>(std::clamp(clickGainDb_, -96.0, 20.0))), 0.0f, 1.0f));
  state_.clickBars.store(std::clamp(clickBars_, 1, 8));
  settings_.sends.cue = clampBoolChoice(sendCue_);
  settings_.sends.fx1 = clampBoolChoice(sendFx1_);
  settings_.sends.fx2 = clampBoolChoice(sendFx2_);
  settings_.sends.out34 = clampBoolChoice(sendOut34_);
  settings_.sends.rec = clampBoolChoice(sendRec_);
  settings_.cue.phonesSrc = clampBoolChoice(phonesSrc_);
  settings_.cue.style = clampBoolChoice(cueStyle_);
  mixer_.setOut34Mode(clampBoolChoice(out34Mode_));
  settings_.mainComp.on = compOn_;
  settings_.mainComp.threshDb = static_cast<float>(std::clamp(compThreshDb_, -48.0, 0.0));
  settings_.mainComp.ratio = static_cast<float>(std::clamp(compRatio_, 1.0, 100.0));
  settings_.mainComp.attackMs = static_cast<float>(std::clamp(compAttackMs_, 0.0, 1000.0));
  settings_.mainComp.releaseMs =
      static_cast<float>(std::clamp(compReleaseMs_, 0.0, 1000.0));
  settings_.mainComp.makeupDb =
      static_cast<float>(std::clamp(compMakeupDb_, -36.0, 36.0));
  settings_.mainComp.mode = clampBoolChoice(compMode_);
  // Only the open popup's own drafts are written: an OK on the metronome popup
  // must not push a stale EQ/delay/reverb draft back into the models.
  if (kind_ == QStringLiteral("eqtype")) {
    eq_.setType(eq_.band(), eqType_);
  } else if (kind_ == QStringLiteral("delay")) {
    const int delayGrid = PerformanceModel::kDelayGrid;
    performance_.setParam(delayGrid, audio::fx::Delay::kBeatMode, delayBeat_);
    performance_.setParam(delayGrid, audio::fx::Delay::kPingPong, delayPing_);
    performance_.setParam(delayGrid, audio::fx::Delay::kDivision, delayDivision_);
    performance_.setParam(delayGrid, audio::fx::Delay::kFeedback, delayFeedback_);
    performance_.setParam(delayGrid, audio::fx::Delay::kMix, delayMix_);
  } else if (kind_ == QStringLiteral("reverb")) {
    const int reverbGrid = PerformanceModel::kReverbGrid;
    performance_.setParam(reverbGrid, audio::fx::Reverb::kModel, reverbModel_);
    performance_.setParam(reverbGrid, audio::fx::Reverb::kPredelayMs, reverbPredelayMs_);
    performance_.setParam(reverbGrid, audio::fx::Reverb::kDecay, reverbDecay_);
    performance_.setParam(reverbGrid, audio::fx::Reverb::kDamping, reverbDamping_);
  }
}

void ConfigPopupModel::refreshSummaries() {
  const QString metronome =
      modeName(state_.clickMode.load()) + QStringLiteral(" - ") +
      outputName(state_.clickOutput.load()) + QStringLiteral(" - ") +
      textOf(formatDbValue(state_.clickGainDb.load()));
  const QString send =
      QStringLiteral("CUE ") + sendName(settings_.sends.cue) + QStringLiteral(" - FX1 ") +
      sendName(settings_.sends.fx1) + QStringLiteral(" - FX2 ") +
      sendName(settings_.sends.fx2) + QStringLiteral(" - OUT34 ") +
      sendName(settings_.sends.out34) + QStringLiteral(" - REC ") +
      sendName(settings_.sends.rec);
  const QString monitor =
      QStringLiteral("PHONES ") +
      (settings_.cue.phonesSrc == 0 ? QStringLiteral("CUE") : QStringLiteral("MAIN")) +
      QStringLiteral(" - CUE ") +
      (settings_.cue.style == 0 ? QStringLiteral("FADER") : QStringLiteral("TOGGLE")) +
      QStringLiteral(" - OUT34 ") +
      (mixer_.out34Mode() == 0 ? QStringLiteral("STEREO") : QStringLiteral("MONO"));
  QString comp = QStringLiteral("OFF");
  if (settings_.mainComp.on) {
    comp = (settings_.mainComp.mode == 0 ? QStringLiteral("PEAK") : QStringLiteral("RMS")) +
           QStringLiteral(" ") + textOf(formatDbValue(settings_.mainComp.threshDb)) +
           QStringLiteral(" - ") +
           textOf(formatRatioValue(settings_.mainComp.ratio)) + QStringLiteral(" - ") +
           textOf(formatMillisecondsValue(settings_.mainComp.releaseMs));
  }
  if (metronome == metronomeSummary_ && send == sendSummary_ &&
      monitor == monitorSummary_ && comp == compSummary_) {
    return;
  }
  metronomeSummary_ = metronome;
  sendSummary_ = send;
  monitorSummary_ = monitor;
  compSummary_ = comp;
  emit summariesChanged();
}

QString ConfigPopupModel::divisionText() const {
  return QString::fromLatin1(audio::fx::Delay::divisionName(delayDivision_));
}

void ConfigPopupModel::setEqType(int value) {
  eqType_ = std::clamp(value, 0, EqModel::kTypeCount - 1);
  emit changed();
}
void ConfigPopupModel::setDelayBeat(int value) {
  delayBeat_ = clampBoolChoice(value);
  emit changed();
}
void ConfigPopupModel::setDelayPing(int value) {
  delayPing_ = clampBoolChoice(value);
  emit changed();
}
void ConfigPopupModel::setDelayDivision(int value) {
  delayDivision_ = std::clamp(value, 0, audio::fx::Delay::kDivCount - 1);
  emit changed();
}
void ConfigPopupModel::setDelayFeedback(double value) {
  delayFeedback_ = std::clamp(value, 0.0, 0.95);
  emit changed();
}
void ConfigPopupModel::setDelayMix(double value) {
  delayMix_ = std::clamp(value, 0.0, 1.0);
  emit changed();
}
void ConfigPopupModel::setReverbModel(int value) {
  reverbModel_ = std::clamp(value, 0, audio::fx::Reverb::kModelCount - 1);
  // A reverb model is a preset: show the decay/damping it selects so OK writes
  // back exactly what the popup displays.
  performance_.setReverbModel(reverbModel_);
  reverbDecay_ = performance_.param(PerformanceModel::kReverbGrid,
                                    audio::fx::Reverb::kDecay);
  reverbDamping_ = performance_.param(PerformanceModel::kReverbGrid,
                                      audio::fx::Reverb::kDamping);
  emit changed();
}
void ConfigPopupModel::setReverbPredelayMs(double value) {
  reverbPredelayMs_ = std::clamp(value, 0.0, 200.0);
  emit changed();
}
void ConfigPopupModel::setReverbDecay(double value) {
  reverbDecay_ = std::clamp(value, 0.0, 0.98);
  emit changed();
}
void ConfigPopupModel::setReverbDamping(double value) {
  reverbDamping_ = std::clamp(value, 0.0, 1.0);
  emit changed();
}

QStringList ConfigPopupModel::timelineOps() const {
  QStringList ops;
  for (int op = 0; op < timeline::kTimelineOpCount; ++op) {
    ops << QString::fromLatin1(
        timeline::timelineOpName(static_cast<timeline::TimelineOp>(op)));
  }
  return ops;
}

QString ConfigPopupModel::timelineOpName() const {
  if (timelineOp_ < 0 || timelineOp_ >= timeline::kTimelineOpCount) return {};
  return QString::fromLatin1(
      timeline::timelineOpName(static_cast<timeline::TimelineOp>(timelineOp_)));
}

void ConfigPopupModel::setTimelineOp(int value) {
  timelineOp_ = std::clamp(value, 0, timeline::kTimelineOpCount - 1);
  timelineError_.clear();
  if (timeline_ != nullptr) {
    timeline_->setPreviewDraft(timelineGainDb_, timelineFadeInBeats_,
                               timelineFadeOutBeats_);
  }
  emit changed();
}

void ConfigPopupModel::setTimelineGainDb(double value) {
  timelineGainDb_ = std::clamp(value, -96.0, 12.0);
  if (timeline_ != nullptr) {
    timeline_->setPreviewDraft(timelineGainDb_, timelineFadeInBeats_,
                               timelineFadeOutBeats_);
  }
  emit changed();
}

void ConfigPopupModel::setTimelineFadeInBeats(double value) {
  timelineFadeInBeats_ = std::clamp(value, 0.0, 64.0);
  if (timeline_ != nullptr) {
    timeline_->setPreviewDraft(timelineGainDb_, timelineFadeInBeats_,
                               timelineFadeOutBeats_);
  }
  emit changed();
}

void ConfigPopupModel::setTimelineFadeOutBeats(double value) {
  timelineFadeOutBeats_ = std::clamp(value, 0.0, 64.0);
  if (timeline_ != nullptr) {
    timeline_->setPreviewDraft(timelineGainDb_, timelineFadeInBeats_,
                               timelineFadeOutBeats_);
  }
  emit changed();
}

#define CONFIG_SETTER(name, field, expression)                 \
  void ConfigPopupModel::set##name(double value) {             \
    field##_ = expression;                                     \
    emit changed();                                            \
  }

void ConfigPopupModel::setClickMode(int value) {
  clickMode_ = clampChoice(value, 2);
  emit changed();
}
void ConfigPopupModel::setClickOutput(int value) {
  clickOutput_ = clampChoice(value, 2);
  emit changed();
}
void ConfigPopupModel::setClickGainDb(double value) {
  clickGainDb_ = value;
  emit changed();
}
void ConfigPopupModel::setClickBars(int value) {
  clickBars_ = std::clamp(value, 1, 8);
  emit changed();
}
void ConfigPopupModel::setSendCue(int value) {
  sendCue_ = clampBoolChoice(value);
  emit changed();
}
void ConfigPopupModel::setSendFx1(int value) {
  sendFx1_ = clampBoolChoice(value);
  emit changed();
}
void ConfigPopupModel::setSendFx2(int value) {
  sendFx2_ = clampBoolChoice(value);
  emit changed();
}
void ConfigPopupModel::setSendOut34(int value) {
  sendOut34_ = clampBoolChoice(value);
  emit changed();
}
void ConfigPopupModel::setSendRec(int value) {
  sendRec_ = clampBoolChoice(value);
  emit changed();
}
void ConfigPopupModel::setPhonesSrc(int value) {
  phonesSrc_ = clampBoolChoice(value);
  emit changed();
}
void ConfigPopupModel::setCueStyle(int value) {
  cueStyle_ = clampBoolChoice(value);
  emit changed();
}
void ConfigPopupModel::setOut34Mode(int value) {
  out34Mode_ = clampBoolChoice(value);
  emit changed();
}
void ConfigPopupModel::setCompOn(bool value) {
  compOn_ = value;
  emit changed();
}
void ConfigPopupModel::setCompThreshDb(double value) {
  compThreshDb_ = value;
  emit changed();
}
void ConfigPopupModel::setCompRatio(double value) {
  compRatio_ = value;
  emit changed();
}
void ConfigPopupModel::setCompAttackMs(double value) {
  compAttackMs_ = value;
  emit changed();
}
void ConfigPopupModel::setCompReleaseMs(double value) {
  compReleaseMs_ = value;
  emit changed();
}
void ConfigPopupModel::setCompMakeupDb(double value) {
  compMakeupDb_ = value;
  emit changed();
}
void ConfigPopupModel::setCompMode(int value) {
  compMode_ = clampBoolChoice(value);
  emit changed();
}

#undef CONFIG_SETTER

}  // namespace mnky::app
