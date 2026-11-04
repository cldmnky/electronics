#include "mixer_view_model.hpp"

#include <algorithm>
#include <cmath>

#include "logic.hpp"

namespace mnky::app {
namespace {
QString panText(double pan) {
  const int percent = static_cast<int>(std::lround((pan - 0.5) * 200.0));
  if (percent == 0) return QStringLiteral("C");
  return QString(percent < 0 ? QStringLiteral("L") : QStringLiteral("R")) +
         QString::number(std::abs(percent));
}
}  // namespace

MixerViewModel::MixerViewModel(MixerModel& model, QObject* parent)
    : QAbstractListModel(parent), model_(model) {}

int MixerViewModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : kNumStrips;
}

QVariant MixerViewModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= kNumStrips) return {};
  const int strip = index.row();
  switch (role) {
    case IndexRole:
      return strip;
    case NameRole:
      return QStringLiteral("IN %1").arg(strip + 1, 2, 10, QLatin1Char('0'));
    case GainDbRole:
      return model_.gainDb(strip);
    case PanRole:
      return model_.pan(strip);
    case PanTextRole:
      return panText(model_.pan(strip));
    case SendARole:
      return model_.send(strip, 0);
    case SendBRole:
      return model_.send(strip, 1);
    case MuteRole:
      return model_.mute(strip);
    case SoloRole:
      return model_.solo(strip);
    case ArmRole:
      return model_.arm(strip);
    case SelectedRole:
      return strip == selectedStrip_;
    default:
      break;
  }
  return {};
}

QHash<int, QByteArray> MixerViewModel::roleNames() const {
  return {{IndexRole, "index"},       {NameRole, "name"},
          {GainDbRole, "gainDb"},    {PanRole, "pan"},
          {PanTextRole, "panText"},  {SendARole, "sendA"},
          {SendBRole, "sendB"},      {MuteRole, "mute"},
          {SoloRole, "solo"},        {ArmRole, "arm"},
          {SelectedRole, "selected"}};
}

void MixerViewModel::setBank(int bank) {
  const int clamped = std::clamp(bank, 0, (kNumStrips / kStripsPerBank) - 1);
  if (clamped == bank_) return;
  bank_ = clamped;
  emit bankChanged();
}

void MixerViewModel::setSelectedStrip(int index) {
  const int clamped = std::clamp(index, 0, kNumStrips - 1);
  const int oldSelection = selectedStrip_;
  selectedStrip_ = clamped;
  if (oldSelection != selectedStrip_) {
    emit dataChanged(this->index(oldSelection, 0), this->index(oldSelection, 0),
                     {SelectedRole});
    emit dataChanged(this->index(selectedStrip_, 0), this->index(selectedStrip_, 0),
                     {SelectedRole});
    emit selectedStripChanged();
  }
  setBank(selectedStrip_ / kStripsPerBank);
}

void MixerViewModel::setMasterDb(double db) {
  const double clamped = std::clamp(db, static_cast<double>(kMinGainDb),
                                    static_cast<double>(kMaxGainDb));
  model_.setMasterDb(static_cast<float>(clamped));
  emit masterChanged();
}

void MixerViewModel::setGainDb(int index, double db) {
  if (index < 0 || index >= kNumStrips) return;
  const double clamped = std::clamp(db, static_cast<double>(kMinGainDb),
                                    static_cast<double>(kMaxGainDb));
  model_.setGainDb(index, static_cast<float>(clamped));
  emit dataChanged(this->index(index, 0), this->index(index, 0), {GainDbRole});
}

void MixerViewModel::setPan(int index, double pan) {
  if (index < 0 || index >= kNumStrips) return;
  model_.setPan(index, static_cast<float>(std::clamp(pan, 0.0, 1.0)));
  emit dataChanged(this->index(index, 0), this->index(index, 0),
                   {PanRole, PanTextRole});
}

void MixerViewModel::setSend(int index, int send, double level) {
  if (index < 0 || index >= kNumStrips || send < 0 || send >= audio::kNumSends) return;
  model_.setSend(index, send, static_cast<float>(std::clamp(level, 0.0, 1.0)));
  emit dataChanged(this->index(index, 0), this->index(index, 0),
                   {send == 0 ? SendARole : SendBRole});
}

void MixerViewModel::toggleMute(int index) {
  if (index < 0 || index >= kNumStrips) return;
  model_.setMute(index, !model_.mute(index));
  emit dataChanged(this->index(index, 0), this->index(index, 0), {MuteRole});
}

void MixerViewModel::toggleSolo(int index) {
  if (index < 0 || index >= kNumStrips) return;
  model_.setSolo(index, !model_.solo(index));
  emit dataChanged(this->index(index, 0), this->index(index, 0), {SoloRole});
}

void MixerViewModel::toggleArm(int index) {
  if (index < 0 || index >= kNumStrips) return;
  model_.setArm(index, !model_.arm(index));
  emit dataChanged(this->index(index, 0), this->index(index, 0), {ArmRole});
}

void MixerViewModel::adjust(int index, const QString& parameter, double delta) {
  if (index < 0 || index >= kNumStrips) return;
  if (parameter == QStringLiteral("gain")) {
    setGainDb(index, model_.gainDb(index) + delta);
  } else if (parameter == QStringLiteral("pan")) {
    setPan(index, model_.pan(index) + delta);
  } else if (parameter == QStringLiteral("sendA")) {
    setSend(index, 0, model_.send(index, 0) + delta);
  } else if (parameter == QStringLiteral("sendB")) {
    setSend(index, 1, model_.send(index, 1) + delta);
  }
}

void MixerViewModel::setMeters(double left, double right) {
  if (std::abs(left - meterLeft_) < 0.001 && std::abs(right - meterRight_) < 0.001) return;
  meterLeft_ = left;
  meterRight_ = right;
  emit metersChanged();
}

void MixerViewModel::refresh() {
  if (rowCount() > 0) {
    emit dataChanged(index(0, 0), index(rowCount() - 1, 0));
  }
  emit masterChanged();
  emit selectedStripChanged();
}

}  // namespace mnky::app
