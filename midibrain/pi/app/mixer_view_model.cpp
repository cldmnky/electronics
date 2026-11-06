#include "mixer_view_model.hpp"

#include <algorithm>
#include <cmath>

#include "logic.hpp"
#include "project_logic.hpp"

namespace mnky::app {
namespace {
QString panText(double pan) {
  const int percent = static_cast<int>(std::lround((pan - 0.5) * 200.0));
  if (percent == 0) return QStringLiteral("C");
  return QString(percent < 0 ? QStringLiteral("L") : QStringLiteral("R")) +
         QString::number(std::abs(percent));
}

double clampPan(double pan) { return std::clamp(pan, 0.0, 1.0); }
double clampSend(double level) { return std::clamp(level, 0.0, 1.0); }
}  // namespace

MixerViewModel::MixerViewModel(MixerModel& model, SessionSettings& settings,
                               QObject* parent)
    : QAbstractListModel(parent), model_(model), settings_(settings) {}

int MixerViewModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : kNumStrips;
}

double MixerViewModel::stripLevel(int index) const {
  if (index < 0 || index >= kNumStrips) return 0.0;
  return stripLevels_[static_cast<std::size_t>(index)];
}

bool MixerViewModel::stripClip(int index) const {
  if (index < 0 || index >= kNumStrips) return false;
  if (clipDemo_) return index < 6;
  return clipHolds_[static_cast<std::size_t>(index)].clipped();
}

QString MixerViewModel::monitorMark(int index) const {
  const int mode = model_.monitor(index);
  // MPC parity names: PLAY = take only, AUTO = smart, BOTH = input + take,
  // MON = input always / take never.
  if (mode == 0) return QStringLiteral("PLAY");
  if (mode == 2) return QStringLiteral("BOTH");
  if (mode == 3) return QStringLiteral("MON");
  // Auto is the default; only show it when an active take makes Auto choose
  // the take over the live input, i.e. when the mark carries information.
  return settings_.hasActiveTake(index) ? QStringLiteral("A") : QString();
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
    case LevelRole:
      return stripLevel(strip);
    case ClipRole:
      return stripClip(strip);
    case MonitorRole:
      return model_.monitor(strip);
    case MonitorMarkRole:
      return monitorMark(strip);
    case InputTextRole:
      return inputText(model_.inCh(strip), model_.inPair(strip));
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
          {LevelRole, "level"},      {ClipRole, "clip"},
          {MonitorRole, "monitor"},  {MonitorMarkRole, "monitorMark"},
          {InputTextRole, "inputText"},
          {SelectedRole, "selected"}};
}

void MixerViewModel::setBank(int bank) {
  const int clamped = std::clamp(bank, 0, (kNumStrips / kStripsPerBank) - 1);
  if (clamped == bank_) return;
  bank_ = clamped;
  emit bankChanged();
}

void MixerViewModel::setTrackMode(bool enabled) {
  if (enabled == trackMode_) return;
  trackMode_ = enabled;
  emit trackModeChanged();
}

void MixerViewModel::setTrackIndex(int index) {
  const int clamped = std::clamp(index, 0, kNumStrips - 1);
  if (clamped == trackIndex_) return;
  trackIndex_ = clamped;
  emit trackChanged();
  setSelectedStrip(clamped);
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

QString MixerViewModel::trackName() const {
  return QStringLiteral("IN %1  %2")
      .arg(trackIndex_ + 1, 2, 10, QLatin1Char('0'))
      .arg(QString::fromStdString(defaultTrackName(trackIndex_)));
}

void MixerViewModel::setTrackGainDb(double db) { setGainDb(trackIndex_, db); }

void MixerViewModel::setTrackTrimDb(double db) {
  model_.setTrimDb(trackIndex_, static_cast<float>(std::clamp(db, -24.0, 24.0)));
  emit dataChanged(index(trackIndex_, 0), index(trackIndex_, 0), {GainDbRole});
  emit trackChanged();
}

void MixerViewModel::setTrackPan(double pan) { setPan(trackIndex_, pan); }

QString MixerViewModel::trackPanText() const { return panText(model_.pan(trackIndex_)); }

void MixerViewModel::setTrackSendA(double level) { setSend(trackIndex_, 0, level); }
void MixerViewModel::setTrackSendB(double level) { setSend(trackIndex_, 1, level); }

void MixerViewModel::setTrackCue(double level) { setCue(trackIndex_, level); }
void MixerViewModel::setTrackOut34(double level) { setOut34(trackIndex_, level); }

void MixerViewModel::setTrackMute(bool mute) {
  model_.setMute(trackIndex_, mute);
  emit dataChanged(index(trackIndex_, 0), index(trackIndex_, 0), {MuteRole});
  emit trackChanged();
}

void MixerViewModel::setTrackSolo(bool solo) {
  model_.setSolo(trackIndex_, solo);
  emit dataChanged(index(trackIndex_, 0), index(trackIndex_, 0), {SoloRole});
  emit trackChanged();
}

void MixerViewModel::setTrackArm(bool arm) {
  model_.setArm(trackIndex_, arm);
  emit dataChanged(index(trackIndex_, 0), index(trackIndex_, 0), {ArmRole});
  emit trackChanged();
}

void MixerViewModel::setTrackMonitor(int mode) { setMonitor(trackIndex_, mode); }

void MixerViewModel::setMonitor(int index, int mode) {
  if (index < 0 || index >= kNumStrips) return;
  model_.setMonitor(index, mode);
  emit dataChanged(this->index(index, 0), this->index(index, 0),
                   {MonitorRole, MonitorMarkRole});
  if (index == trackIndex_) emit trackChanged();
}

QString MixerViewModel::inputText(int channel, bool pair) {
  if (channel < 0) return QStringLiteral("NONE");
  const int first = clampInCh(channel);
  const QString left = QStringLiteral("IN %1").arg(first + 1, 2, 10, QLatin1Char('0'));
  if (!pair) return left;
  const int second = std::min(first + 1, kNumStrips - 1);
  return left + QStringLiteral("/%1").arg(second + 1, 2, 10, QLatin1Char('0'));
}

QString MixerViewModel::outputText(int destination) {
  switch (clampOutDst(destination)) {
    case 1: return QStringLiteral("OUT3&4");
    case 2: return QStringLiteral("PHONES");
    case 3: return QStringLiteral("NONE");
    default: return QStringLiteral("MAIN 1&2");
  }
}

void MixerViewModel::setTrackInCh(int channel) {
  model_.setInCh(trackIndex_, channel);
  emit trackChanged();
}

void MixerViewModel::setTrackInPair(bool pair) {
  model_.setInPair(trackIndex_, pair);
  emit trackChanged();
}

void MixerViewModel::setTrackOutDst(int destination) {
  model_.setOutDst(trackIndex_, destination);
  emit trackChanged();
}

void MixerViewModel::cycleTrackInCh(int delta) {
  // -1 = none, then mono channels 0..17; both directions wrap.
  const int current = model_.inCh(trackIndex_);
  const int steps = kNumStrips + 1;
  int index = current + 1;  // 0 = none, 1..18 = channel 0..17
  if (delta > 0) {
    index = (index + 1) % steps;
  } else if (delta < 0) {
    index = (index + steps - 1) % steps;
  }
  model_.setInCh(trackIndex_, index - 1);
  emit trackChanged();
}

void MixerViewModel::cycleTrackOutDst(int delta) {
  const int current = model_.outDst(trackIndex_);
  int next = current;
  if (delta > 0) {
    next = (current + 1) % kOutDstCount;
  } else if (delta < 0) {
    next = (current + kOutDstCount - 1) % kOutDstCount;
  }
  model_.setOutDst(trackIndex_, next);
  emit trackChanged();
}

QString MixerViewModel::inSourceText(int index) const {
  if (index < 0 || index >= kNumStrips) return {};
  return inputText(model_.inCh(index), model_.inPair(index));
}

QString MixerViewModel::cycleInSource(int index, int delta) {
  if (index < 0 || index >= kNumStrips) return {};
  // State 0 is NONE, 1..18 the mono channels and 19..27 the nine stereo pairs;
  // both directions wrap so the selector never dead-ends.
  const int pairBase = 1 + kNumStrips;
  const int states = pairBase + kNumStrips / 2;
  const int channel = model_.inCh(index);
  int state = 0;
  if (channel >= 0) {
    state = model_.inPair(index) ? pairBase + channel / 2 : 1 + channel;
  }
  state = ((state + delta) % states + states) % states;
  if (state == 0) {
    model_.setInPair(index, false);
    model_.setInCh(index, -1);
  } else if (state < pairBase) {
    model_.setInPair(index, false);
    model_.setInCh(index, state - 1);
  } else {
    // Pair first: setInPair clamps an odd channel down, then the even start
    // channel is stored unchanged while the pair flag is on.
    model_.setInPair(index, true);
    model_.setInCh(index, 2 * (state - pairBase));
  }
  emit dataChanged(this->index(index, 0), this->index(index, 0), {InputTextRole});
  if (index == trackIndex_) emit trackChanged();
  return inSourceText(index);
}

QString MixerViewModel::toggleInPair(int index) {
  if (index < 0 || index >= kNumStrips) return {};
  model_.setInPair(index, !model_.inPair(index));
  emit dataChanged(this->index(index, 0), this->index(index, 0), {InputTextRole});
  if (index == trackIndex_) emit trackChanged();
  return inSourceText(index);
}

void MixerViewModel::notifyActiveTakesChanged() {
  if (rowCount() == 0) return;
  emit dataChanged(index(0, 0), index(rowCount() - 1, 0), {MonitorMarkRole});
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
  if (index == trackIndex_) emit trackChanged();
}

void MixerViewModel::setPan(int index, double pan) {
  if (index < 0 || index >= kNumStrips) return;
  double value = clampPan(pan);
  // Centre detent shared by touch and panel EDIT paths; the popup then reads
  // "Center" instead of a near-centre L/R value.
  if (std::abs(value - 0.5) <= 0.02) value = 0.5;
  model_.setPan(index, static_cast<float>(value));
  emit dataChanged(this->index(index, 0), this->index(index, 0),
                   {PanRole, PanTextRole});
  if (index == trackIndex_) emit trackChanged();
}

void MixerViewModel::setSend(int index, int send, double level) {
  if (index < 0 || index >= kNumStrips || send < 0 || send >= audio::kNumSends) return;
  model_.setSend(index, send, static_cast<float>(clampSend(level)));
  emit dataChanged(this->index(index, 0), this->index(index, 0),
                   {send == 0 ? SendARole : SendBRole});
  if (index == trackIndex_) emit trackChanged();
}

void MixerViewModel::setCue(int index, double level) {
  if (index < 0 || index >= kNumStrips) return;
  model_.setCueSend(index, static_cast<float>(clampSend(level)));
  if (index == trackIndex_) emit trackChanged();
}

void MixerViewModel::setOut34(int index, double level) {
  if (index < 0 || index >= kNumStrips) return;
  model_.setOut34Send(index, static_cast<float>(clampSend(level)));
  if (index == trackIndex_) emit trackChanged();
}

void MixerViewModel::toggleMute(int index) {
  if (index < 0 || index >= kNumStrips) return;
  model_.setMute(index, !model_.mute(index));
  emit dataChanged(this->index(index, 0), this->index(index, 0), {MuteRole});
  if (index == trackIndex_) emit trackChanged();
}

void MixerViewModel::toggleSolo(int index) {
  if (index < 0 || index >= kNumStrips) return;
  model_.setSolo(index, !model_.solo(index));
  emit dataChanged(this->index(index, 0), this->index(index, 0), {SoloRole});
  if (index == trackIndex_) emit trackChanged();
}

void MixerViewModel::toggleArm(int index) {
  if (index < 0 || index >= kNumStrips) return;
  model_.setArm(index, !model_.arm(index));
  emit dataChanged(this->index(index, 0), this->index(index, 0), {ArmRole});
  if (index == trackIndex_) emit trackChanged();
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
  } else if (parameter == QStringLiteral("cue")) {
    setCue(index, model_.cueSend(index) + delta);
  } else if (parameter == QStringLiteral("out34")) {
    setOut34(index, model_.out34Send(index) + delta);
  }
}

void MixerViewModel::setMeters(double left, double right) {
  if (std::abs(left - meterLeft_) < 0.001 && std::abs(right - meterRight_) < 0.001) return;
  meterLeft_ = left;
  meterRight_ = right;
  emit metersChanged();
}

void MixerViewModel::setStripMeters(const std::array<double, kNumStrips>& levels) {
  bool changed = false;
  for (int strip = 0; strip < kNumStrips; ++strip) {
    auto& hold = clipHolds_[static_cast<std::size_t>(strip)];
    (void)hold.tick();
    const double level = clampSend(levels[static_cast<std::size_t>(strip)]);
    (void)hold.update(static_cast<float>(level));
    if (std::abs(level - stripLevels_[static_cast<std::size_t>(strip)]) >= 0.001) {
      stripLevels_[static_cast<std::size_t>(strip)] = level;
      changed = true;
    }
  }
  if (changed) {
    emit dataChanged(index(0, 0), index(kNumStrips - 1, 0), {LevelRole, ClipRole});
    emit metersChanged();
  }
}

void MixerViewModel::showClipDemo() {
  clipDemo_ = true;
  for (int strip = 0; strip < kNumStrips; ++strip) {
    stripLevels_[static_cast<std::size_t>(strip)] =
        strip < 6 ? 0.55 + 0.07 * static_cast<double>(strip % 3) : 0.12;
  }
  emit dataChanged(index(0, 0), index(kNumStrips - 1, 0), {LevelRole, ClipRole});
  emit metersChanged();
}

void MixerViewModel::clearClipDemo() {
  if (!clipDemo_) return;
  clipDemo_ = false;
  emit dataChanged(index(0, 0), index(kNumStrips - 1, 0), {ClipRole});
  emit metersChanged();
}

void MixerViewModel::refresh() {
  if (rowCount() > 0) {
    emit dataChanged(index(0, 0), index(rowCount() - 1, 0));
  }
  emit masterChanged();
  emit selectedStripChanged();
  emit trackChanged();
}

}  // namespace mnky::app
