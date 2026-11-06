#include "fx_view_model.hpp"

#include "fx_params.hpp"

namespace mnky::app {

FxSlotCard::FxSlotCard(TrackFxViewModel& owner, int slot)
    : QObject(&owner), owner_(owner), slot_(slot) {}

QString FxSlotCard::title() const {
  return slot_ == 0 ? QStringLiteral("FX1") : QStringLiteral("FX2");
}

QString FxSlotCard::effectName() const { return owner_.effectNameAt(slot_); }

int FxSlotCard::choice() const { return owner_.choiceAt(slot_); }

bool FxSlotCard::bypass() const { return owner_.bypassAt(slot_); }

double FxSlotCard::wet() const { return owner_.wetAt(slot_); }

int FxSlotCard::paramCount() const { return owner_.paramsAt(slot_).size(); }

QVariantList FxSlotCard::params() const { return owner_.paramsAt(slot_); }

QString FxSlotCard::note() const { return owner_.noteAt(slot_); }

bool FxSlotCard::builtin() const { return owner_.builtinAt(slot_); }

TrackFxViewModel::TrackFxViewModel(MixerModel& model, MixerViewModel& mixer,
                                   QObject* parent)
    : QObject(parent),
      model_(model),
      mixer_(mixer),
      fx1_(*this, 0),
      fx2_(*this, 1) {
  // Both the rack edit signal and a track switch re-read the cards, so the
  // popup never shows one track's slot over another track's values.
  connect(&mixer_, &MixerViewModel::fxChanged, this, &TrackFxViewModel::refresh);
  connect(&mixer_, &MixerViewModel::trackChanged, this,
          [this] { emit trackChanged(); });
}

int TrackFxViewModel::track() const { return mixer_.trackIndex(); }

QString TrackFxViewModel::title() const {
  return QStringLiteral("TRACK %1 - FX RACK")
      .arg(track() + 1, 2, 10, QLatin1Char('0'));
}

QString TrackFxViewModel::subline() const { return mixer_.fxSubline(); }

QString TrackFxViewModel::footer() const {
  return QStringLiteral("live - every change applies at once; OK/CLOSE keeps it");
}

void TrackFxViewModel::openFor(int track, int slot) {
  if (track < 0 || track >= audio::kMaxTracks) return;
  focusSlot_ = slot == 1 ? 1 : 0;
  mixer_.clearFxChoiceRefusal();
  mixer_.setTrackIndex(track);
  if (!open_) {
    open_ = true;
    emit openChanged();
  }
  refresh();
}

void TrackFxViewModel::close() {
  if (!open_) return;
  open_ = false;
  emit openChanged();
}

void TrackFxViewModel::refresh() {
  fx1_.refresh();
  fx2_.refresh();
  emit trackChanged();
}

QString TrackFxViewModel::effectNameAt(int slot) const {
  if (slot < 0 || slot >= MixerModel::kFxSlotCount) return QString();
  return mixer_.fxNameAt(track(), slot);
}

int TrackFxViewModel::choiceAt(int slot) const {
  if (slot < 0 || slot >= MixerModel::kFxSlotCount) return 0;
  return mixer_.fxChoiceAt(track(), slot);
}

bool TrackFxViewModel::bypassAt(int slot) const {
  if (slot < 0 || slot >= MixerModel::kFxSlotCount) return false;
  return mixer_.fxBypassAt(track(), slot);
}

double TrackFxViewModel::wetAt(int slot) const {
  if (slot < 0 || slot >= MixerModel::kFxSlotCount) return 0.0;
  return mixer_.fxWetAt(track(), slot);
}

QVariantList TrackFxViewModel::paramsAt(int slot) const {
  if (slot < 0 || slot >= MixerModel::kFxSlotCount) return {};
  return mixer_.fxParams(track(), slot);
}

QString TrackFxViewModel::noteAt(int slot) const {
  if (slot < 0 || slot >= MixerModel::kFxSlotCount) return QString();
  return mixer_.fxSlotNote(track(), slot);
}

bool TrackFxViewModel::builtinAt(int slot) const {
  if (slot < 0 || slot >= MixerModel::kFxSlotCount) return false;
  return fxIsBuiltin(model_.fxSlot(track(), slot).id);
}

void TrackFxViewModel::setChoice(int slot, int choice) {
  mixer_.setFxChoiceAt(track(), slot, choice);
}

void TrackFxViewModel::setWet(int slot, double wet) {
  mixer_.setFxWetAt(track(), slot, wet);
}

void TrackFxViewModel::toggleBypass(int slot) {
  mixer_.setFxBypassAt(track(), slot, !mixer_.fxBypassAt(track(), slot));
}

void TrackFxViewModel::setParam(int slot, int index, double value) {
  mixer_.setFxParamAt(track(), slot, index, value);
}

}  // namespace mnky::app
