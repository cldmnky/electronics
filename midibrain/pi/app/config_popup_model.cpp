#include "config_popup_model.hpp"

#include <algorithm>
#include <cmath>

#include "fx_params.hpp"
#include "logic.hpp"
#include "mixer_view_model.hpp"
#include "pipe_ui.hpp"
#include "routing_view_model.hpp"
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
                                   QObject* parent)
    : QObject(parent),
      state_(state),
      mixer_(mixer),
      settings_(settings),
      eq_(eq) {
  refreshSummaries();
}

namespace {

// One parameter value of the FX target slot, read through the rack table so a
// missing entry falls back to the effect's published default.
double fxValueAt(const MixerModel& mixer, int track, int slot, const char* paramId) {
  const auto fx = mixer.fxSlot(track, slot);
  const int index = fxParamIndex(fx.id, paramId);
  const auto table = fxParamTable(fx.id);
  if (index < 0 || index >= static_cast<int>(table.size())) return 0.0;
  if (index < static_cast<int>(fx.params.size())) {
    return fx.params[static_cast<std::size_t>(index)];
  }
  return table[static_cast<std::size_t>(index)].def;
}

bool fxTargetIs(const MixerModel& mixer, int track, int slot, const char* effectId) {
  return mixer.fxSlot(track, slot).id == effectId;
}

}  // namespace

int ConfigPopupModel::clampChoice(int value, int maximum) {
  return std::clamp(value, 0, maximum);
}
int ConfigPopupModel::clampBoolChoice(int value) { return value == 1 ? 1 : 0; }

bool ConfigPopupModel::openKind(const QString& kind) {
  const QStringList kinds = {
      QStringLiteral("metronome"), QStringLiteral("sends"),
      QStringLiteral("monitor"),   QStringLiteral("comp"),
      QStringLiteral("eqtype"),    QStringLiteral("delay"),
      QStringLiteral("reverb"),    QStringLiteral("timelineops"),
      QStringLiteral("pipechannels"), QStringLiteral("trackmode"),
      QStringLiteral("recmode"),       QStringLiteral("takeactions"),
      QStringLiteral("takeedit"),      QStringLiteral("trackfromtake"),
      QStringLiteral("inputsource"), QStringLiteral("outputdest"),
      QStringLiteral("fxslot"),    QStringLiteral("addtrack"),
      QStringLiteral("delaydivision"), QStringLiteral("reverbmodel"),
      QStringLiteral("pipesource"), QStringLiteral("pipedest"),
      QStringLiteral("pipetype"), QStringLiteral("pipeparam"),
      QStringLiteral("chainmenu")};
  if (!kinds.contains(kind)) return false;
  if (kind == QStringLiteral("timelineops") && timeline_ == nullptr) return false;
  if (kind == QStringLiteral("pipechannels")) {
    if (routing_ == nullptr || routing_->pipeCount() == 0) return false;
  }
  if (kind == QStringLiteral("pipeparam") &&
      (routing_ == nullptr || routing_->pipeCount() == 0)) {
    return false;
  }
  if ((kind == QStringLiteral("inputsource") || kind == QStringLiteral("outputdest") ||
       kind == QStringLiteral("fxslot")) && mixerVm_ == nullptr) {
    return false;
  }
  if (kind == QStringLiteral("addtrack")) {
    if (mixerVm_ == nullptr || mixerVm_->nextFreeSlot() < 0) return false;
  }
  kind_ = kind;
  if (kind_ == QStringLiteral("fxslot") && mixerVm_ != nullptr) {
    // A refusal from an earlier open is stale once the popup is reopened; the
    // next tap only shows a new refusal.
    mixerVm_->clearFxChoiceRefusal();
  }
  title_ = kind == QStringLiteral("metronome")   ? QStringLiteral("METRONOME")
           : kind == QStringLiteral("sends")     ? QStringLiteral("SEND SOURCE")
           : kind == QStringLiteral("monitor")   ? QStringLiteral("OUTPUT")
           : kind == QStringLiteral("eqtype")    ? QStringLiteral("EQ BAND TYPE")
           : kind == QStringLiteral("delay")     ? QStringLiteral("DELAY")
           : kind == QStringLiteral("reverb")    ? QStringLiteral("REVERB")
           : kind == QStringLiteral("timelineops") ? QStringLiteral("REGION OPS")
           : kind == QStringLiteral("pipechannels") ? QStringLiteral("MIDI CHANNEL FILTER")
           : kind == QStringLiteral("trackmode") ? QStringLiteral("TRACK MODE")
           : kind == QStringLiteral("recmode") ? QStringLiteral("REC MODE")
           : kind == QStringLiteral("takeactions") ? takeTargetName_
           : kind == QStringLiteral("takeedit") ? takeTargetName_
           : kind == QStringLiteral("trackfromtake") ? QStringLiteral("NEW TRACK FROM TAKE")
           : kind == QStringLiteral("inputsource") ? inputSourceTitle()
           : kind == QStringLiteral("outputdest") ? outputTitle()
           : kind == QStringLiteral("fxslot")     ? fxSlotTitle()
           : kind == QStringLiteral("addtrack")   ? QStringLiteral("ADD TRACK")
           : kind == QStringLiteral("delaydivision") ? QStringLiteral("DELAY DIVISION")
           : kind == QStringLiteral("reverbmodel") ? QStringLiteral("REVERB MODEL")
           : kind == QStringLiteral("pipesource")  ? QStringLiteral("PIPE FROM")
           : kind == QStringLiteral("pipedest")    ? QStringLiteral("PIPE TO")
           : kind == QStringLiteral("pipetype")    ? QStringLiteral("ADD PIPE TYPE")
           : kind == QStringLiteral("pipeparam")   ? QStringLiteral("PIPE PARAMETER")
           : kind == QStringLiteral("chainmenu")   ? QStringLiteral("CHAIN")
                                                 : QStringLiteral("MAIN COMP");
  loadDraft();
  captureRestore();
  open_ = true;
  emit openChanged();
  changedLive();
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
  trackMode_ = clampTrackMode(mixer_.mode(trackModeSlot_));
  trackRecSource_ = clampRecSource(mixer_.recSource(trackModeSlot_));
  recMode_ = clampRecMode(state_.recMode.load());
  countInBars_ = clampCountInBars(state_.countInBars.load());
  eqType_ = eq_.type(eq_.band());
  // The X/Y parameter kinds edit the rack slot AppController pointed them at.
  delayBeat_ = static_cast<int>(
      std::lround(fxValueAt(mixer_, fxTrackSlot_, fxSlotIndex_, "beat_mode")));
  delayPing_ = static_cast<int>(
      std::lround(fxValueAt(mixer_, fxTrackSlot_, fxSlotIndex_, "ping_pong")));
  delayDivision_ = static_cast<int>(
      std::lround(fxValueAt(mixer_, fxTrackSlot_, fxSlotIndex_, "division")));
  delayFeedback_ = fxValueAt(mixer_, fxTrackSlot_, fxSlotIndex_, "feedback");
  delayMix_ = fxValueAt(mixer_, fxTrackSlot_, fxSlotIndex_, "mix");
  reverbModel_ = static_cast<int>(
      std::lround(fxValueAt(mixer_, fxTrackSlot_, fxSlotIndex_, "model")));
  reverbPredelayMs_ = fxValueAt(mixer_, fxTrackSlot_, fxSlotIndex_, "predelay_ms");
  reverbDecay_ = fxValueAt(mixer_, fxTrackSlot_, fxSlotIndex_, "decay");
  reverbDamping_ = fxValueAt(mixer_, fxTrackSlot_, fxSlotIndex_, "damping");
  if (kind_ == QStringLiteral("pipechannels")) {
    pipeChannelMask_ = static_cast<std::uint16_t>(
        routing_ == nullptr ? 0xFFFF : routing_->selectedPipeChannelMask());
    pipeScopeContext_ =
        routing_ == nullptr
            ? QString()
            : routing_->pipeRouteText() + QStringLiteral(" - PIPE ") +
                  QString::number(routing_->selectedPipe() + 1) + QStringLiteral(" / ") +
                  QString::number(routing_->pipeCount());
  }
  if (kind_ == QStringLiteral("inputsource")) {
    inputSourceIndex_ =
        mixerVm_ == nullptr ? 0 : mixerVm_->inSourceState(inputTargetSlot_);
  }
  if (kind_ == QStringLiteral("outputdest")) {
    outputIndex_ = mixerVm_ == nullptr ? 0 : mixerVm_->outDstOf(outputTargetSlot_);
  }
  if (kind_ == QStringLiteral("fxslot")) {
    fxSlotChoice_ =
        mixerVm_ == nullptr ? 0 : mixerVm_->fxChoiceAt(fxTrackSlot_, fxSlotIndex_);
    fxSlotNote_.clear();
  }
  if (kind_ == QStringLiteral("pipesource") || kind_ == QStringLiteral("pipedest")) {
    pipeSourceLabels_.clear();
    pipeDestLabels_.clear();
    pipeSourcePorts_.clear();
    pipeDestPorts_.clear();
    if (routing_ != nullptr) {
      for (const auto& row : routing_->inputNodes()) {
        const auto map = row.toMap();
        pipeSourceLabels_ << map.value(QStringLiteral("name")).toString();
        pipeSourcePorts_.push_back(map.value(QStringLiteral("port")).toInt());
      }
      for (const auto& row : routing_->outputNodes()) {
        const auto map = row.toMap();
        pipeDestLabels_ << map.value(QStringLiteral("name")).toString();
        pipeDestPorts_.push_back(map.value(QStringLiteral("port")).toInt());
      }
    }
    // The lists are built in node order, so the node index is the list index
    // (a port-id comparison would misplace the source-less GENERATOR node).
    pipeSourceIndex_ = routing_ == nullptr ? 0 : routing_->pipeSource();
    pipeDestIndex_ = routing_ == nullptr ? 0 : routing_->pipeDestination();
  }
  if (kind_ == QStringLiteral("pipetype")) {
    pipeTypeIndex_ = routing_ == nullptr ? 0 : routing_->addTypeIndexInList();
  }
  if (kind_ == QStringLiteral("pipeparam")) {
    pipeParamChoices_.clear();
    pipeParamContext_.clear();
  }
  if (kind_ == QStringLiteral("pipeparam") && routing_ != nullptr) {
    // The choice list and the current value come from the same row the
    // parameter card renders, and pipeParams() is the selected pipe's list, so
    // the write target is that same pipe.
    pipeParamSlot_ = routing_->selectedPipe();
    for (const auto& entry : routing_->pipeParams()) {
      const auto row = entry.toMap();
      if (row.value(QStringLiteral("param")).toInt() != pipeParamIndex_) continue;
      pipeParamMinimum_ = row.value(QStringLiteral("minimum")).toInt();
      const int value = row.value(QStringLiteral("value")).toInt();
      pipeParamChoice_ = value - pipeParamMinimum_;
      for (const auto& choice : row.value(QStringLiteral("choices")).toStringList()) {
        pipeParamChoices_ << choice;
      }
      pipeParamContext_ =
          routing_->pipeRouteText() + QStringLiteral(" - PIPE ") +
          QString::number(routing_->selectedPipe() + 1) + QStringLiteral(" ") +
          routing_->selectedPipeName() + QStringLiteral(" - ") +
          row.value(QStringLiteral("name")).toString() + QStringLiteral(" ") +
          row.value(QStringLiteral("valueText")).toString();
      break;
    }
  }
  if (kind_ == QStringLiteral("addtrack") && mixerVm_ != nullptr) {
    // ADD TRACK appends the first free slot; its default name follows the take
    // naming rule (chNN) and the default input is the slot's own device
    // channel, so a fresh strip is sane without any further tap.
    addTrackSlot_ = mixerVm_->nextFreeSlot();
    addTrackName_ = mixerVm_->suggestedTrackName(addTrackSlot_);
    addTrackInputMode_ = 1;
    addTrackChannel_ = std::clamp(addTrackSlot_, 0, mixerVm_->inputChannelCount() - 1);
    addTrackOutput_ = 0;
    addTrackError_.clear();
  }
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
  if (kind_ == QStringLiteral("addtrack")) {
    if (mixerVm_ == nullptr) return;
    const int inputChannel = addTrackInputMode_ == 0 ? -1 : addTrackChannel_;
    if (!mixerVm_->addTrack(addTrackSlot_, addTrackName_, inputChannel,
                            addTrackInputMode_ == 2, addTrackOutput_)) {
      addTrackError_ = QStringLiteral("SLOT %1 IS ALREADY A TRACK")
                           .arg(addTrackSlot_ + 1);
      changedLive();
      return;
    }
    open_ = false;
    emit openChanged();
    refreshSummaries();
    return;
  }
  if (kind_ == QStringLiteral("timelineops")) {
    if (timeline_ == nullptr) return;
    if (!regionApplies()) {
      timelineError_ = regionReason();
      changedLive();
      return;
    }
    const QString error = timeline_->applyOp(timelineOp_, timelineGainDb_,
                                             timelineFadeInBeats_,
                                             timelineFadeOutBeats_);
    if (!error.isEmpty()) {
      timelineError_ = error;
      changedLive();
      return;
    }
    timelineError_.clear();
    open_ = false;
    emit openChanged();
    refreshSummaries();
    return;
  }
  // Every setting already reached the model on the tap that changed it; OK
  // only closes and keeps what the popup shows.
  applyDraft();
  open_ = false;
  emit openChanged();
  refreshSummaries();
}

void ConfigPopupModel::cancel() {
  if (!open_) return;
  // Live fields mean CANCEL must put back exactly what the popup opened with;
  // the add-track popup has nothing to restore because nothing was created.
  if (kind_ != QStringLiteral("addtrack") &&
      kind_ != QStringLiteral("timelineops")) {
    restoreRestore();
  }
  open_ = false;
  emit openChanged();
  refreshSummaries();
}

void ConfigPopupModel::changedLive() {
  emit changed();
  applyDraft();
}

void ConfigPopupModel::captureRestore() {
  Restore& restore = restore_;
  restore.clickMode = state_.clickMode.load();
  restore.clickOutput = state_.clickOutput.load();
  restore.clickGainDb = state_.clickGainDb.load();
  restore.clickBars = state_.clickBars.load();
  restore.sendCue = settings_.sends.cue;
  restore.sendFx1 = settings_.sends.fx1;
  restore.sendFx2 = settings_.sends.fx2;
  restore.sendOut34 = settings_.sends.out34;
  restore.sendRec = settings_.sends.rec;
  restore.phonesSrc = settings_.cue.phonesSrc;
  restore.cueStyle = settings_.cue.style;
  restore.out34Mode = mixer_.out34Mode();
  restore.compOn = settings_.mainComp.on;
  restore.compThreshDb = settings_.mainComp.threshDb;
  restore.compRatio = settings_.mainComp.ratio;
  restore.compAttackMs = settings_.mainComp.attackMs;
  restore.compReleaseMs = settings_.mainComp.releaseMs;
  restore.compMakeupDb = settings_.mainComp.makeupDb;
  restore.compMode = settings_.mainComp.mode;
  restore.trackMode = mixer_.mode(trackModeSlot_);
  restore.trackRecSource = mixer_.recSource(trackModeSlot_);
  restore.recMode = state_.recMode.load();
  restore.countInBars = state_.countInBars.load();
  restore.eqBand = eq_.band();
  restore.eqType = eq_.type(eq_.band());
  restore.pipeMask =
      static_cast<std::uint16_t>(routing_ == nullptr ? 0xFFFF
                                                    : routing_->selectedPipeChannelMask());
  restore.pipeSource = routing_ == nullptr ? 0 : routing_->pipeSource();
  restore.pipeDestination = routing_ == nullptr ? 0 : routing_->pipeDestination();
  restore.pipeType = routing_ == nullptr ? 0 : routing_->addTypeIndex();
  restore.pipeParam = pipeParamMinimum_ + pipeParamChoice_;
  restore.inputSource =
      mixerVm_ == nullptr ? 0 : mixerVm_->inSourceState(inputTargetSlot_);
  restore.outputDst = mixerVm_ == nullptr ? 0 : mixerVm_->outDstOf(outputTargetSlot_);
  restore.fxChoice =
      mixerVm_ == nullptr ? 0 : mixerVm_->fxChoiceAt(fxTrackSlot_, fxSlotIndex_);
  restore.fxParams =
      mixerVm_ == nullptr ? std::vector<double>{}
                          : mixer_.fxSlot(fxTrackSlot_, fxSlotIndex_).params;
}

void ConfigPopupModel::restoreRestore() {
  const Restore& restore = restore_;
  if (kind_ == QStringLiteral("metronome")) {
    state_.clickMode.store(restore.clickMode);
    state_.clickEnabled.store(restore.clickMode != 0);
    state_.clickOutput.store(restore.clickOutput);
    state_.clickGainDb.store(restore.clickGainDb);
    state_.clickLevel.store(std::clamp(dbToGain(restore.clickGainDb), 0.0f, 1.0f));
    state_.clickBars.store(restore.clickBars);
  } else if (kind_ == QStringLiteral("sends")) {
    settings_.sends.cue = restore.sendCue;
    settings_.sends.fx1 = restore.sendFx1;
    settings_.sends.fx2 = restore.sendFx2;
    settings_.sends.out34 = restore.sendOut34;
    settings_.sends.rec = restore.sendRec;
  } else if (kind_ == QStringLiteral("monitor")) {
    settings_.cue.phonesSrc = restore.phonesSrc;
    settings_.cue.style = restore.cueStyle;
    mixer_.setOut34Mode(restore.out34Mode);
  } else if (kind_ == QStringLiteral("comp")) {
    settings_.mainComp.on = restore.compOn;
    settings_.mainComp.threshDb = restore.compThreshDb;
    settings_.mainComp.ratio = restore.compRatio;
    settings_.mainComp.attackMs = restore.compAttackMs;
    settings_.mainComp.releaseMs = restore.compReleaseMs;
    settings_.mainComp.makeupDb = restore.compMakeupDb;
    settings_.mainComp.mode = restore.compMode;
  } else if (kind_ == QStringLiteral("trackmode")) {
    mixer_.setMode(trackModeSlot_, restore.trackMode);
    mixer_.setRecSource(trackModeSlot_, restore.trackRecSource);
  } else if (kind_ == QStringLiteral("recmode")) {
    state_.recMode.store(restore.recMode);
    state_.countInBars.store(restore.countInBars);
  } else if (kind_ == QStringLiteral("eqtype")) {
    eq_.setType(restore.eqBand, restore.eqType);
  } else if (kind_ == QStringLiteral("delay") ||
             kind_ == QStringLiteral("delaydivision") ||
             kind_ == QStringLiteral("reverb") ||
             kind_ == QStringLiteral("reverbmodel")) {
    // CANCEL puts back the whole slot vector, not only the fields this kind
    // displays: the grid's cutoff/size are the same slot's parameters.
    if (mixerVm_ != nullptr && mixerVm_->fxChoiceAt(fxTrackSlot_, fxSlotIndex_) ==
                                   restore.fxChoice) {
      for (std::size_t param = 0; param < restore.fxParams.size(); ++param) {
        mixerVm_->setFxParamAt(fxTrackSlot_, fxSlotIndex_,
                               static_cast<int>(param),
                               restore.fxParams[param]);
      }
    }
  } else if (kind_ == QStringLiteral("pipechannels")) {
    if (routing_ != nullptr) routing_->setSelectedPipeChannelMask(restore.pipeMask);
  } else if (kind_ == QStringLiteral("pipesource")) {
    if (routing_ != nullptr) routing_->setPipeSource(restore.pipeSource);
  } else if (kind_ == QStringLiteral("pipedest")) {
    if (routing_ != nullptr) routing_->setPipeDestination(restore.pipeDestination);
  } else if (kind_ == QStringLiteral("pipetype")) {
    if (routing_ != nullptr) routing_->setAddTypeIndex(restore.pipeType);
  } else if (kind_ == QStringLiteral("pipeparam")) {
    if (routing_ != nullptr) {
      routing_->setPipeParam(pipeParamSlot_, pipeParamIndex_, restore.pipeParam);
    }
  } else if (kind_ == QStringLiteral("inputsource")) {
    if (mixerVm_ != nullptr) {
      mixerVm_->setInSourceState(inputTargetSlot_, restore.inputSource);
    }
  } else if (kind_ == QStringLiteral("outputdest")) {
    if (mixerVm_ != nullptr) {
      mixerVm_->setOutDstOf(outputTargetSlot_, restore.outputDst);
    }
  } else if (kind_ == QStringLiteral("fxslot")) {
    if (mixerVm_ != nullptr) {
      (void)mixerVm_->setFxChoiceAt(fxTrackSlot_, fxSlotIndex_, restore.fxChoice);
    }
    fxSlotNote_.clear();
  }
  loadDraft();
}

void ConfigPopupModel::applyDraft() {
  if (!open_) return;
  if (kind_ == QStringLiteral("metronome")) {
    state_.clickMode.store(clampChoice(clickMode_, 2));
    state_.clickEnabled.store(clickMode_ != 0);
    state_.clickOutput.store(clampChoice(clickOutput_, 2));
    state_.clickGainDb.store(static_cast<float>(std::clamp(clickGainDb_, -96.0, 20.0)));
    state_.clickLevel.store(std::clamp(
        dbToGain(static_cast<float>(std::clamp(clickGainDb_, -96.0, 20.0))), 0.0f,
        1.0f));
    state_.clickBars.store(std::clamp(clickBars_, 1, 8));
  } else if (kind_ == QStringLiteral("sends")) {
    settings_.sends.cue = clampBoolChoice(sendCue_);
    settings_.sends.fx1 = clampBoolChoice(sendFx1_);
    settings_.sends.fx2 = clampBoolChoice(sendFx2_);
    settings_.sends.out34 = clampBoolChoice(sendOut34_);
    settings_.sends.rec = clampBoolChoice(sendRec_);
  } else if (kind_ == QStringLiteral("monitor")) {
    settings_.cue.phonesSrc = clampBoolChoice(phonesSrc_);
    settings_.cue.style = clampBoolChoice(cueStyle_);
    mixer_.setOut34Mode(clampBoolChoice(out34Mode_));
  } else if (kind_ == QStringLiteral("comp")) {
    settings_.mainComp.on = compOn_;
    settings_.mainComp.threshDb =
        static_cast<float>(std::clamp(compThreshDb_, -48.0, 0.0));
    settings_.mainComp.ratio = static_cast<float>(std::clamp(compRatio_, 1.0, 100.0));
    settings_.mainComp.attackMs =
        static_cast<float>(std::clamp(compAttackMs_, 0.0, 1000.0));
    settings_.mainComp.releaseMs =
        static_cast<float>(std::clamp(compReleaseMs_, 0.0, 1000.0));
    settings_.mainComp.makeupDb =
        static_cast<float>(std::clamp(compMakeupDb_, -36.0, 36.0));
    settings_.mainComp.mode = clampBoolChoice(compMode_);
  } else if (kind_ == QStringLiteral("trackmode")) {
    mixer_.setMode(trackModeSlot_, clampTrackMode(trackMode_));
    mixer_.setRecSource(trackModeSlot_, clampRecSource(trackRecSource_));
  } else if (kind_ == QStringLiteral("recmode")) {
    state_.recMode.store(clampRecMode(recMode_));
    state_.countInBars.store(clampCountInBars(countInBars_));
  } else if (kind_ == QStringLiteral("eqtype")) {
    eq_.setType(eq_.band(), eqType_);
  } else if (kind_ == QStringLiteral("delay")) {
    if (mixerVm_ != nullptr && fxTargetIs(mixer_, fxTrackSlot_, fxSlotIndex_, kFxIdDelay)) {
      mixerVm_->setFxParamAt(fxTrackSlot_, fxSlotIndex_,
                             audio::fx::Delay::kBeatMode, delayBeat_);
      mixerVm_->setFxParamAt(fxTrackSlot_, fxSlotIndex_,
                             audio::fx::Delay::kPingPong, delayPing_);
      mixerVm_->setFxParamAt(fxTrackSlot_, fxSlotIndex_,
                             audio::fx::Delay::kDivision, delayDivision_);
      mixerVm_->setFxParamAt(fxTrackSlot_, fxSlotIndex_,
                             audio::fx::Delay::kFeedback, delayFeedback_);
      mixerVm_->setFxParamAt(fxTrackSlot_, fxSlotIndex_, audio::fx::Delay::kMix,
                             delayMix_);
    }
  } else if (kind_ == QStringLiteral("reverb")) {
    if (mixerVm_ != nullptr && fxTargetIs(mixer_, fxTrackSlot_, fxSlotIndex_, kFxIdReverb)) {
      mixerVm_->setFxParamAt(fxTrackSlot_, fxSlotIndex_,
                             audio::fx::Reverb::kModel, reverbModel_);
      mixerVm_->setFxParamAt(fxTrackSlot_, fxSlotIndex_,
                             audio::fx::Reverb::kPredelayMs, reverbPredelayMs_);
      mixerVm_->setFxParamAt(fxTrackSlot_, fxSlotIndex_,
                             audio::fx::Reverb::kDecay, reverbDecay_);
      mixerVm_->setFxParamAt(fxTrackSlot_, fxSlotIndex_,
                             audio::fx::Reverb::kDamping, reverbDamping_);
    }
  } else if (kind_ == QStringLiteral("pipechannels")) {
    if (routing_ != nullptr) routing_->setSelectedPipeChannelMask(pipeChannelMask_);
  } else if (kind_ == QStringLiteral("pipesource")) {
    if (routing_ != nullptr && pipeSourceIndex_ >= 0 &&
        pipeSourceIndex_ < static_cast<int>(pipeSourcePorts_.size())) {
      routing_->setPipeSource(pipeSourcePorts_[static_cast<std::size_t>(pipeSourceIndex_)]);
    }
  } else if (kind_ == QStringLiteral("pipedest")) {
    if (routing_ != nullptr && pipeDestIndex_ >= 0 &&
        pipeDestIndex_ < static_cast<int>(pipeDestPorts_.size())) {
      routing_->setPipeDestination(pipeDestPorts_[static_cast<std::size_t>(pipeDestIndex_)]);
    }
  } else if (kind_ == QStringLiteral("pipetype")) {
    if (routing_ != nullptr) routing_->setAddTypeIndex(pipeTypeIndex_);
  } else if (kind_ == QStringLiteral("pipeparam")) {
    if (routing_ != nullptr) {
      routing_->setPipeParam(pipeParamSlot_, pipeParamIndex_,
                             pipeParamMinimum_ + pipeParamChoice_);
    }
  } else if (kind_ == QStringLiteral("inputsource")) {
    if (mixerVm_ != nullptr) {
      mixerVm_->setInSourceState(inputTargetSlot_, inputSourceIndex_);
    }
  } else if (kind_ == QStringLiteral("outputdest")) {
    if (mixerVm_ != nullptr) mixerVm_->setOutDstOf(outputTargetSlot_, outputIndex_);
  } else if (kind_ == QStringLiteral("fxslot")) {
    if (mixerVm_ != nullptr) {
      (void)mixerVm_->setFxChoiceAt(fxTrackSlot_, fxSlotIndex_, fxSlotChoice_);
    }
  } else if (kind_ == QStringLiteral("delaydivision")) {
    if (mixerVm_ != nullptr && fxTargetIs(mixer_, fxTrackSlot_, fxSlotIndex_, kFxIdDelay)) {
      mixerVm_->setFxParamAt(fxTrackSlot_, fxSlotIndex_,
                             audio::fx::Delay::kDivision, delayDivision_);
    }
  } else if (kind_ == QStringLiteral("reverbmodel")) {
    if (mixerVm_ != nullptr && fxTargetIs(mixer_, fxTrackSlot_, fxSlotIndex_, kFxIdReverb)) {
      mixerVm_->setFxParamAt(fxTrackSlot_, fxSlotIndex_, audio::fx::Reverb::kModel,
                             reverbModel_);
    }
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

QString ConfigPopupModel::pipeChannelText() const {
  return QString::fromStdString(formatChannelMask(pipeChannelMask_));
}

void ConfigPopupModel::togglePipeChannel(int channel) {
  if (channel < 0 || channel >= 16) return;
  const auto bit = static_cast<std::uint16_t>(1u << static_cast<unsigned>(channel));
  pipeChannelMask_ = static_cast<std::uint16_t>(pipeChannelMask_ ^ bit);
  changedLive();
}

void ConfigPopupModel::setPipeChannelMask(int mask) {
  pipeChannelMask_ =
      static_cast<std::uint16_t>(std::clamp(mask, 0, 0xFFFF));
  changedLive();
}

void ConfigPopupModel::setPipeChannelPreset(int preset) {
  switch (preset) {
    case 0: pipeChannelMask_ = 0xFFFF; break;
    case 1: pipeChannelMask_ = 0x000F; break;
    case 2: pipeChannelMask_ = 0x00F0; break;
    case 3: pipeChannelMask_ = 0x0F00; break;
    case 4: pipeChannelMask_ = 0xF000; break;
    default: return;
  }
  changedLive();
}

void ConfigPopupModel::setEqType(int value) {
  eqType_ = std::clamp(value, 0, EqModel::kTypeCount - 1);
  changedLive();
}
void ConfigPopupModel::setDelayBeat(int value) {
  delayBeat_ = clampBoolChoice(value);
  changedLive();
}
void ConfigPopupModel::setDelayPing(int value) {
  delayPing_ = clampBoolChoice(value);
  changedLive();
}
void ConfigPopupModel::setDelayDivision(int value) {
  delayDivision_ = std::clamp(value, 0, audio::fx::Delay::kDivCount - 1);
  changedLive();
}
void ConfigPopupModel::setDelayFeedback(double value) {
  delayFeedback_ = std::clamp(value, 0.0, 0.95);
  changedLive();
}
void ConfigPopupModel::setDelayMix(double value) {
  delayMix_ = std::clamp(value, 0.0, 1.0);
  changedLive();
}
void ConfigPopupModel::setReverbModel(int value) {
  reverbModel_ = std::clamp(value, 0, audio::fx::Reverb::kModelCount - 1);
  // A reverb model is a preset (Reverb::setModel documents decay/damping/size
  // following it), so the rack write applies the preset and the popup re-reads
  // the values it now holds.
  if (mixerVm_ != nullptr && fxTargetIs(mixer_, fxTrackSlot_, fxSlotIndex_, kFxIdReverb)) {
    mixerVm_->setFxParamAt(fxTrackSlot_, fxSlotIndex_, audio::fx::Reverb::kModel,
                           reverbModel_);
    reverbDecay_ = fxValueAt(mixer_, fxTrackSlot_, fxSlotIndex_, "decay");
    reverbDamping_ = fxValueAt(mixer_, fxTrackSlot_, fxSlotIndex_, "damping");
  }
  changedLive();
}
void ConfigPopupModel::setReverbPredelayMs(double value) {
  reverbPredelayMs_ = std::clamp(value, 0.0, 200.0);
  changedLive();
}
void ConfigPopupModel::setReverbDecay(double value) {
  reverbDecay_ = std::clamp(value, 0.0, 0.98);
  changedLive();
}
void ConfigPopupModel::setReverbDamping(double value) {
  reverbDamping_ = std::clamp(value, 0.0, 1.0);
  changedLive();
}

QStringList ConfigPopupModel::timelineOps() const {
  QStringList ops;
  for (int op = 0; op < timeline::kTimelineUiOpCount; ++op) {
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
  timelineOp_ = std::clamp(value, 0, timeline::kTimelineUiOpCount - 1);
  timelineError_.clear();
  if (timeline_ != nullptr) {
    timeline_->setPreviewDraft(timelineGainDb_, timelineFadeInBeats_,
                               timelineFadeOutBeats_);
  }
  changedLive();
}

void ConfigPopupModel::setTimelineGainDb(double value) {
  timelineGainDb_ = std::clamp(value, -96.0, 12.0);
  if (timeline_ != nullptr) {
    timeline_->setPreviewDraft(timelineGainDb_, timelineFadeInBeats_,
                               timelineFadeOutBeats_);
  }
  changedLive();
}

void ConfigPopupModel::setTimelineFadeInBeats(double value) {
  timelineFadeInBeats_ = std::clamp(value, 0.0, 64.0);
  if (timeline_ != nullptr) {
    timeline_->setPreviewDraft(timelineGainDb_, timelineFadeInBeats_,
                               timelineFadeOutBeats_);
  }
  changedLive();
}

void ConfigPopupModel::setTimelineFadeOutBeats(double value) {
  timelineFadeOutBeats_ = std::clamp(value, 0.0, 64.0);
  if (timeline_ != nullptr) {
    timeline_->setPreviewDraft(timelineGainDb_, timelineFadeInBeats_,
                               timelineFadeOutBeats_);
  }
  changedLive();
}

#define CONFIG_SETTER(name, field, expression)                 \
  void ConfigPopupModel::set##name(double value) {             \
    field##_ = expression;                                     \
    changedLive();                                            \
  }

void ConfigPopupModel::setClickMode(int value) {
  clickMode_ = clampChoice(value, 2);
  changedLive();
}
void ConfigPopupModel::setClickOutput(int value) {
  clickOutput_ = clampChoice(value, 2);
  changedLive();
}
void ConfigPopupModel::setClickGainDb(double value) {
  clickGainDb_ = value;
  changedLive();
}
void ConfigPopupModel::setClickBars(int value) {
  clickBars_ = std::clamp(value, 1, 8);
  changedLive();
}
void ConfigPopupModel::setSendCue(int value) {
  sendCue_ = clampBoolChoice(value);
  changedLive();
}
void ConfigPopupModel::setSendFx1(int value) {
  sendFx1_ = clampBoolChoice(value);
  changedLive();
}
void ConfigPopupModel::setSendFx2(int value) {
  sendFx2_ = clampBoolChoice(value);
  changedLive();
}
void ConfigPopupModel::setSendOut34(int value) {
  sendOut34_ = clampBoolChoice(value);
  changedLive();
}
void ConfigPopupModel::setSendRec(int value) {
  sendRec_ = clampBoolChoice(value);
  changedLive();
}
void ConfigPopupModel::setPhonesSrc(int value) {
  phonesSrc_ = clampBoolChoice(value);
  changedLive();
}
void ConfigPopupModel::setCueStyle(int value) {
  cueStyle_ = clampBoolChoice(value);
  changedLive();
}
void ConfigPopupModel::setOut34Mode(int value) {
  out34Mode_ = clampBoolChoice(value);
  changedLive();
}
void ConfigPopupModel::setCompOn(bool value) {
  compOn_ = value;
  changedLive();
}
void ConfigPopupModel::setCompThreshDb(double value) {
  compThreshDb_ = value;
  changedLive();
}
void ConfigPopupModel::setCompRatio(double value) {
  compRatio_ = value;
  changedLive();
}
void ConfigPopupModel::setCompAttackMs(double value) {
  compAttackMs_ = value;
  changedLive();
}
void ConfigPopupModel::setCompReleaseMs(double value) {
  compReleaseMs_ = value;
  changedLive();
}
void ConfigPopupModel::setCompMakeupDb(double value) {
  compMakeupDb_ = value;
  changedLive();
}
void ConfigPopupModel::setTrackMode(int value) {
  const int next = clampTrackMode(value);
  if (next == trackMode_) return;
  trackMode_ = next;
  changedLive();
}

void ConfigPopupModel::setTrackRecSource(int value) {
  const int next = clampRecSource(value);
  if (next == trackRecSource_) return;
  trackRecSource_ = next;
  changedLive();
}

QString ConfigPopupModel::trackRecSourceText() const {
  return QString::fromStdString(recSourceText(trackRecSource_));
}

void ConfigPopupModel::setRecMode(int value) {
  const int next = clampRecMode(value);
  if (next == recMode_) return;
  recMode_ = next;
  changedLive();
}

void ConfigPopupModel::setCountInBars(int value) {
  const int next = clampCountInBars(value);
  if (next == countInBars_) return;
  countInBars_ = next;
  changedLive();
}

QStringList ConfigPopupModel::takeActions() const {
  return {QStringLiteral("ACTIVE"),   QStringLiteral("EDIT"),
          QStringLiteral("RENAME"),   QStringLiteral("DELETE"),
          QStringLiteral("REMOVE"),   QStringLiteral("IMPORT"),
          QStringLiteral("NEW TRACK FROM THIS TAKE")};
}

QStringList ConfigPopupModel::trackFromTakeChoices() const {
  return {QStringLiteral("MOVE"), QStringLiteral("KEEP ON SOURCE")};
}

QString ConfigPopupModel::recModeSummary() const {
  // The one-line readout under the three choice rows: what the pick does when
  // REC is pressed.
  switch (clampRecMode(recMode_)) {
    case kRecModeCountIn:
      return QStringLiteral("ROLLS THE CLICK - RECORDING STARTS AT THE DOWNBEAT");
    case kRecModePunch:
      return QStringLiteral("ROLLS FROM PLAY FROM - RECORDS BETWEEN PUNCH IN AND OUT");
    default:
      return QStringLiteral("RECORDS THE MOMENT YOU PRESS REC");
  }
}

void ConfigPopupModel::chooseTakeAction(int action) {
  if (takeActionSink_) takeActionSink_(action);
}

void ConfigPopupModel::chooseTrackFromTake(int choice) {
  if (trackFromTakeSink_) trackFromTakeSink_(choice);
}

void ConfigPopupModel::setCompMode(int value) {
  compMode_ = clampBoolChoice(value);
  changedLive();
}

#undef CONFIG_SETTER

QString ConfigPopupModel::footerHint() const {
  // C9: the header repeats the button's verb, so the two can never disagree.
  if (kind_ == QStringLiteral("addtrack")) {
    return QStringLiteral("ADD TRACK - CANCEL ADDS NOTHING");
  }
  if (kind_ == QStringLiteral("timelineops")) {
    return acceptLabel() + QStringLiteral(" - CANCEL KEEPS EDITS");
  }
  if (kind_ == QStringLiteral("takeactions") ||
      kind_ == QStringLiteral("trackfromtake")) {
    return QStringLiteral("TAP AN ACTION - CLOSE CHANGES NOTHING");
  }
  return QStringLiteral("LIVE - EVERY CHANGE APPLIES - CANCEL RESTORES");
}

QString ConfigPopupModel::acceptLabel() const {
  if (kind_ == QStringLiteral("addtrack")) return QStringLiteral("ADD TRACK");
  if (kind_ == QStringLiteral("timelineops")) {
    return QStringLiteral("APPLY ") + timelineOpName();
  }
  // An action menu changes nothing on OK/CANCEL, so the button says CLOSE and
  // the header repeats it.
  if (kind_ == QStringLiteral("takeactions") ||
      kind_ == QStringLiteral("trackfromtake") ||
      kind_ == QStringLiteral("takeedit")) {
    return QStringLiteral("CLOSE");
  }
  return QStringLiteral("OK");
}

QString ConfigPopupModel::regionLine() const {
  if (timeline_ == nullptr) return {};
  if (!timeline_->selectionActive()) {
    return QStringLiteral("NO IN/OUT - set them with the cursor");
  }
  const double in = timeline_->inBeats();
  const double out = timeline_->outBeats();
  const double beats = out - in;
  return QStringLiteral("IN %1 -> OUT %2  (%3 beats)")
      .arg(timeline_->beatTextOf(in), timeline_->beatTextOf(out))
      .arg(beats, 0, 'f', 2);
}

bool ConfigPopupModel::regionApplies() const {
  if (timeline_ == nullptr) return false;
  // TRIM and DELETE rewrite the selected range, so they need a real IN..OUT.
  // The other ops either use the cursor (SPLIT), the whole take (GAIN, FADES,
  // NORM) or no range (CLEAR).
  const bool needsRange = timelineOp_ == 0 || timelineOp_ == 5;
  if (!needsRange) return true;
  return timeline_->selectionActive() && timeline_->outBeats() > timeline_->inBeats();
}

QString ConfigPopupModel::regionReason() const {
  if (regionApplies()) return {};
  if (timeline_ == nullptr || !timeline_->selectionActive()) {
    return QStringLiteral("select a region with a drag or IN/OUT first");
  }
  return QStringLiteral("OUT must be after IN");
}

QString ConfigPopupModel::inputSourceTitle() const {
  const QString name = mixerVm_ == nullptr
                           ? QString()
                           : mixerVm_->suggestedTrackName(inputTargetSlot_);
  return QStringLiteral("INPUT SOURCE  %1").arg(name.toUpper());
}

QStringList ConfigPopupModel::inputSourceLabels() const {
  return MixerViewModel::inputSourceLabels();
}

void ConfigPopupModel::setInputSourceIndex(int value) {
  inputSourceIndex_ =
      std::clamp(value, 0, static_cast<int>(inputSourceLabels().size()) - 1);
  changedLive();
}

QString ConfigPopupModel::outputTitle() const {
  const QString name = mixerVm_ == nullptr
                           ? QString()
                           : mixerVm_->suggestedTrackName(outputTargetSlot_);
  return QStringLiteral("OUTPUT DEST  %1").arg(name.toUpper());
}

QStringList ConfigPopupModel::outputLabels() const {
  return MixerViewModel::outputLabels();
}

void ConfigPopupModel::setOutputIndex(int value) {
  outputIndex_ = clampOutDst(value);
  changedLive();
}

QString ConfigPopupModel::fxSlotTitle() const {
  const QString name = mixerVm_ == nullptr
                           ? QString()
                           : mixerVm_->suggestedTrackName(fxTrackSlot_);
  return QStringLiteral("FX%1 SLOT  %2")
      .arg(fxSlotIndex_ == 1 ? 2 : 1)
      .arg(name.toUpper());
}

QStringList ConfigPopupModel::fxSlotLabels() const {
  return mixerVm_ == nullptr ? QStringList{QStringLiteral("NONE")}
                             : mixerVm_->fxChoiceLabels();
}

void ConfigPopupModel::setFxSlotChoice(int value) {
  if (mixerVm_ == nullptr) return;
  const int maximum = std::max(0, static_cast<int>(fxSlotLabels().size()) - 1);
  value = std::clamp(value, 0, maximum);
  if (!mixerVm_->setFxChoiceAt(fxTrackSlot_, fxSlotIndex_, value)) {
    // The rack refused (plugin cap, no host): keep the selector on the value
    // the slot actually holds and show the exact reason under it.
    fxSlotChoice_ = mixerVm_->fxChoiceAt(fxTrackSlot_, fxSlotIndex_);
    fxSlotNote_ = mixerVm_->fxChoiceRefusal(fxTrackSlot_, fxSlotIndex_);
    emit changed();
    return;
  }
  fxSlotChoice_ = value;
  fxSlotNote_.clear();
  emit changed();
}

QString ConfigPopupModel::fxSlotNote() const {
  if (!fxSlotNote_.isEmpty()) return fxSlotNote_;
  if (mixerVm_ == nullptr) return QString();
  return mixerVm_->fxSlotNote(fxTrackSlot_, fxSlotIndex_);
}

void ConfigPopupModel::setFxTarget(int track, int slot) {
  fxTrackSlot_ = std::clamp(track, 0, audio::kMaxTracks - 1);
  fxSlotIndex_ = slot == 1 ? 1 : 0;
}

int ConfigPopupModel::addTrackChannelCount() const {
  return MixerViewModel::inputChannelCount();
}

QString ConfigPopupModel::addTrackSummary() const {
  const QString input =
      addTrackInputMode_ == 0
          ? QStringLiteral("NONE")
          : MixerViewModel::inputText(addTrackChannel_, addTrackInputMode_ == 2);
  return QStringLiteral("%1  -  %2  -  %3")
      .arg(addTrackName_.isEmpty() ? QStringLiteral("(default)")
                                   : addTrackName_.toUpper())
      .arg(input)
      .arg(MixerViewModel::outputText(addTrackOutput_));
}

void ConfigPopupModel::setAddTrackName(const QString& name) {
  addTrackName_ = name.trimmed().left(24);
  changedLive();
}

void ConfigPopupModel::setAddTrackInputMode(int mode) {
  addTrackInputMode_ = std::clamp(mode, 0, 2);
  if (addTrackInputMode_ == 2 && addTrackChannel_ % 2 != 0) --addTrackChannel_;
  changedLive();
}

void ConfigPopupModel::setAddTrackChannel(int channel) {
  addTrackChannel_ = std::clamp(channel, 0, MixerViewModel::inputChannelCount() - 1);
  if (addTrackInputMode_ == 2 && addTrackChannel_ % 2 != 0) --addTrackChannel_;
  changedLive();
}

void ConfigPopupModel::setAddTrackOutput(int destination) {
  addTrackOutput_ = clampOutDst(destination);
  changedLive();
}

void ConfigPopupModel::setAddTrackSlot(int slot) {
  if (slot < 0 || slot >= audio::kMaxTracks) return;
  addTrackSlot_ = slot;
  addTrackName_ = mixerVm_ == nullptr ? QString() : mixerVm_->suggestedTrackName(slot);
}

QStringList ConfigPopupModel::pipeTypeLabels() const {
  return routing_ == nullptr ? QStringList() : routing_->addTypeLabels();
}

void ConfigPopupModel::setPipeSourceIndex(int value) {
  pipeSourceIndex_ = std::clamp(value, 0, static_cast<int>(pipeSourcePorts_.size()) - 1);
  changedLive();
}

void ConfigPopupModel::setPipeDestIndex(int value) {
  pipeDestIndex_ = std::clamp(value, 0, static_cast<int>(pipeDestPorts_.size()) - 1);
  changedLive();
}

void ConfigPopupModel::setPipeTypeIndex(int value) {
  pipeTypeIndex_ =
      std::clamp(value, 0, std::max(0, static_cast<int>(pipeTypeLabels().size()) - 1));
  changedLive();
}

QStringList ConfigPopupModel::pipeTypeRoles() const {
  QStringList roles;
  if (routing_ == nullptr) return roles;
  const QStringList labels = routing_->addTypeLabels();
  roles.reserve(labels.size());
  for (int index = 0; index < labels.size(); ++index) {
    roles << pipeui::pipeRoleName(routing_->addTypeIdAt(index));
  }
  return roles;
}

bool ConfigPopupModel::pipeTypeHeadOnly() const {
  return routing_ != nullptr && routing_->generatorHeadPending();
}

QString ConfigPopupModel::pipeTypeDocs() const {
  if (routing_ == nullptr) return {};
  return pipeui::pipeDocs(routing_->addTypeIdAt(pipeTypeIndex_));
}

void ConfigPopupModel::setPipeParamTarget(int pipe, int param) {
  pipeParamSlot_ = pipe;
  pipeParamIndex_ = param;
}

void ConfigPopupModel::setPipeParamChoice(int choice) {
  if (pipeParamChoices_.isEmpty()) return;
  pipeParamChoice_ = std::clamp(choice, 0, static_cast<int>(pipeParamChoices_.size()) - 1);
  changedLive();
}

}  // namespace mnky::app
