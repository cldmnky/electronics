#include "mixer_view_model.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "logic.hpp"

namespace mnky::app {
namespace {
std::uint64_t steadyMs() {
  using namespace std::chrono;
  return static_cast<std::uint64_t>(
      duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

QString panText(double pan) {
  const int percent = static_cast<int>(std::lround((pan - 0.5) * 200.0));
  if (percent == 0) return QStringLiteral("C");
  return QString(percent < 0 ? QStringLiteral("L") : QStringLiteral("R")) +
         QString::number(std::abs(percent));
}

double clampPan(double pan) { return std::clamp(pan, 0.0, 1.0); }
double clampSend(double level) { return std::clamp(level, 0.0, 1.0); }

// One EQ band as the channel popout's summary reads it: type plus the value
// the type actually uses (cut types have no gain).
QString bandSummary(const session::EqBand& band) {
  const QString type =
      QString::fromStdString(EqModel::typeShortName(band.type)).toUpper();
  const QString freq = QString::fromStdString(formatHzValue(band.freqHz));
  if (band.type == 0) return type;
  if (!EqModel::typeHasGain(band.type)) return type + QStringLiteral(" ") + freq;
  return type + QStringLiteral(" ") + freq + QStringLiteral(" ") +
         QString::fromStdString(formatDbValue(band.gainDb));
}
}  // namespace

MixerViewModel::MixerViewModel(MixerModel& model, SessionSettings& settings,
                               EqModel& eq, QObject* parent)
    : QAbstractListModel(parent), model_(model), settings_(settings), eq_(eq) {
  clampSelection();
}

int MixerViewModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : model_.trackCount();
}

int MixerViewModel::pageCount() const {
  const int count = model_.trackCount();
  if (count <= 0) return 1;
  return (count + kStripsPerBank - 1) / kStripsPerBank;
}

QString MixerViewModel::bankText() const {
  const int count = model_.trackCount();
  if (count == 0) return QStringLiteral("NO TRACKS");
  const int first = bank_ * kStripsPerBank + 1;
  const int last = std::min(count, (bank_ + 1) * kStripsPerBank);
  return QStringLiteral("TRACKS %1-%2 / %3").arg(first).arg(last).arg(count);
}

double MixerViewModel::stripLevel(int slot) const {
  if (slot < 0 || slot >= audio::kMaxTracks) return 0.0;
  return stripLevels_[static_cast<std::size_t>(slot)];
}

bool MixerViewModel::stripClip(int slot) const {
  if (slot < 0 || slot >= audio::kMaxTracks) return false;
  if (clipDemo_) return model_.positionOfSlot(slot) >= 0 &&
                        model_.positionOfSlot(slot) < 6;
  return clipHolds_[static_cast<std::size_t>(slot)].clipped();
}

QString MixerViewModel::monitorMark(int slot) const {
  const int mode = model_.monitor(slot);
  // MPC parity names: PLAY = take only, AUTO = smart, BOTH = input + take,
  // MON = input always / take never.
  if (mode == 0) return QStringLiteral("PLAY");
  if (mode == 2) return QStringLiteral("BOTH");
  if (mode == 3) return QStringLiteral("MON");
  // Auto is the default; only show it when an active take makes Auto choose
  // the take over the live input, i.e. when the mark carries information.
  return settings_.hasActiveTake(slot) ? QStringLiteral("A") : QString();
}

QVariant MixerViewModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0) return {};
  const int row = index.row();
  const int slot = model_.slotAt(row);
  if (slot < 0) return {};
  switch (role) {
    case IndexRole:
      return row;
    case SlotRole:
      return slot;
    case NameRole:
      return QString::fromStdString(model_.trackName(slot));
    case GainDbRole:
      return model_.gainDb(slot);
    case PanRole:
      return model_.pan(slot);
    case PanTextRole:
      return panText(model_.pan(slot));
    case SendARole:
      return model_.send(slot, 0);
    case SendBRole:
      return model_.send(slot, 1);
    case MuteRole:
      return model_.mute(slot);
    case SoloRole:
      return model_.solo(slot);
    case ArmRole:
      return model_.arm(slot);
    case LevelRole:
      return stripLevel(slot);
    case ClipRole:
      return stripClip(slot);
    case MonitorRole:
      return model_.monitor(slot);
    case MonitorMarkRole:
      return monitorMark(slot);
    case InputTextRole:
      return inputText(model_.inCh(slot), model_.inPair(slot));
    case OutputTextRole:
      return outputText(model_.outDst(slot));
    case SelectedRole:
      return slot == selectedStrip_;
    case ArmMutesTakeRole:
      return model_.arm(slot) && settings_.hasActiveTake(slot);
    case ArmMutesBadgeRole:
      return model_.arm(slot) && settings_.hasActiveTake(slot) &&
             steadyMs() < armMutesUntilMs_;
    default:
      break;
  }
  return {};
}

QHash<int, QByteArray> MixerViewModel::roleNames() const {
  return {{IndexRole, "index"},           {SlotRole, "slot"},
          {NameRole, "name"},             {GainDbRole, "gainDb"},
          {PanRole, "pan"},               {PanTextRole, "panText"},
          {SendARole, "sendA"},           {SendBRole, "sendB"},
          {MuteRole, "mute"},             {SoloRole, "solo"},
          {ArmRole, "arm"},               {LevelRole, "level"},
          {ClipRole, "clip"},             {MonitorRole, "monitor"},
          {MonitorMarkRole, "monitorMark"}, {InputTextRole, "inputText"},
          {OutputTextRole, "outputText"}, {SelectedRole, "selected"},
          {ArmMutesTakeRole, "armMutesTake"}, {ArmMutesBadgeRole, "armMutesBadge"}};
}

void MixerViewModel::setBank(int bank) {
  const int clamped = std::clamp(bank, 0, pageCount() - 1);
  if (clamped == bank_) return;
  bank_ = clamped;
  emit bankChanged();
}

void MixerViewModel::stepBank(int delta) {
  setBank(bank_ + (delta < 0 ? -1 : 1));
}

void MixerViewModel::setTrackMode(bool enabled) {
  if (enabled == trackMode_) return;
  trackMode_ = enabled;
  emit trackModeChanged();
}

void MixerViewModel::clampSelection() {
  // Selected strip and TRACK index are slots; when their slot disappeared (a
  // removed track), fall back to the first track of the list.
  const int count = model_.trackCount();
  const int fallback = count > 0 ? model_.slotAt(0) : 0;
  if (model_.positionOfSlot(selectedStrip_) < 0) selectedStrip_ = fallback;
  if (model_.positionOfSlot(trackIndex_) < 0) trackIndex_ = fallback;
  bank_ = std::clamp(selectedStrip_ < 0 ? 0 : model_.positionOfSlot(selectedStrip_) /
                                             kStripsPerBank,
                     0, pageCount() - 1);
}

void MixerViewModel::reloadTracks() {
  beginResetModel();
  clampSelection();
  endResetModel();
  updateChannelSummary();
  emit tracksChanged();
  emit bankChanged();
  emit selectedStripChanged();
  emit trackChanged();
  emit channelOpenChanged();
}

void MixerViewModel::setTrackIndex(int slot) {
  if (slot < 0 || slot >= audio::kMaxTracks) return;
  if (slot == trackIndex_) {
    setSelectedStrip(slot);
    return;
  }
  trackIndex_ = slot;
  emit trackChanged();
  if (channelOpen_) updateChannelSummary();
  setSelectedStrip(slot);
}

void MixerViewModel::selectTrackAt(int position) {
  const int slot = model_.slotAt(position);
  if (slot >= 0) setTrackIndex(slot);
}

void MixerViewModel::setSelectedStrip(int slot) {
  if (slot < 0 || slot >= audio::kMaxTracks) return;
  const int oldSelection = selectedStrip_;
  selectedStrip_ = slot;
  if (oldSelection != selectedStrip_) {
    const int oldRow = model_.positionOfSlot(oldSelection);
    const int newRow = model_.positionOfSlot(selectedStrip_);
    if (oldRow >= 0) {
      emit dataChanged(index(oldRow, 0), index(oldRow, 0), {SelectedRole});
    }
    if (newRow >= 0) {
      emit dataChanged(index(newRow, 0), index(newRow, 0), {SelectedRole});
    }
    emit selectedStripChanged();
  }
  const int row = model_.positionOfSlot(selectedStrip_);
  if (row >= 0) setBank(row / kStripsPerBank);
}

QString MixerViewModel::trackName() const {
  return QString::fromStdString(model_.trackName(trackIndex_));
}

void MixerViewModel::setTrackGainDb(double db) { setGainDb(trackIndex_, db); }

void MixerViewModel::setTrackTrimDb(double db) {
  model_.setTrimDb(trackIndex_, static_cast<float>(std::clamp(db, -24.0, 24.0)));
  const int row = model_.positionOfSlot(trackIndex_);
  if (row >= 0) emit dataChanged(index(row, 0), index(row, 0), {GainDbRole});
  emit trackChanged();
}

void MixerViewModel::setTrackPan(double pan) { setPan(trackIndex_, pan); }

QString MixerViewModel::trackPanText() const { return panText(model_.pan(trackIndex_)); }

QString MixerViewModel::panTextAt(int slot) const { return panText(model_.pan(slot)); }

void MixerViewModel::setTrackSendA(double level) { setSend(trackIndex_, 0, level); }
void MixerViewModel::setTrackSendB(double level) { setSend(trackIndex_, 1, level); }

void MixerViewModel::setTrackCue(double level) { setCue(trackIndex_, level); }
void MixerViewModel::setTrackOut34(double level) { setOut34(trackIndex_, level); }

void MixerViewModel::setTrackMute(bool mute) {
  model_.setMute(trackIndex_, mute);
  const int row = model_.positionOfSlot(trackIndex_);
  if (row >= 0) emit dataChanged(index(row, 0), index(row, 0), {MuteRole});
  emit trackChanged();
}

void MixerViewModel::setTrackSolo(bool solo) {
  model_.setSolo(trackIndex_, solo);
  const int row = model_.positionOfSlot(trackIndex_);
  if (row >= 0) emit dataChanged(index(row, 0), index(row, 0), {SoloRole});
  emit trackChanged();
}

void MixerViewModel::setTrackArm(bool arm) {
  model_.setArm(trackIndex_, arm);
  const int row = model_.positionOfSlot(trackIndex_);
  if (row >= 0) emit dataChanged(index(row, 0), index(row, 0), {ArmRole});
  emit trackChanged();
}

void MixerViewModel::setTrackMonitor(int mode) { setMonitor(trackIndex_, mode); }

void MixerViewModel::setMonitor(int slot, int mode) {
  if (slot < 0 || slot >= audio::kMaxTracks) return;
  model_.setMonitor(slot, mode);
  const int row = model_.positionOfSlot(slot);
  if (row >= 0) {
    emit dataChanged(index(row, 0), index(row, 0), {MonitorRole, MonitorMarkRole});
  }
  if (slot == trackIndex_) emit trackChanged();
}

QString MixerViewModel::inputText(int channel, bool pair) {
  if (channel < 0) return QStringLiteral("NONE");
  const int first = clampInCh(channel);
  const QString left = QStringLiteral("IN %1").arg(first + 1, 2, 10, QLatin1Char('0'));
  if (!pair) return left;
  const int second = std::min(first + 1, inputChannelCount() - 1);
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

QStringList MixerViewModel::inputSourceLabels() {
  // One option per device capture channel the model reports, plus "none" and
  // the stereo pairs inside that width. Nothing here knows the number 18.
  const int channels = inputChannelCount();
  QStringList labels;
  labels.reserve(1 + channels + channels / 2);
  labels << QStringLiteral("NONE");
  for (int channel = 0; channel < channels; ++channel) {
    labels << QStringLiteral("IN %1").arg(channel + 1, 2, 10, QLatin1Char('0'));
  }
  for (int pair = 0; pair < channels / 2; ++pair) {
    labels << QStringLiteral("PAIR %1/%2")
                  .arg(pair * 2 + 1, 2, 10, QLatin1Char('0'))
                  .arg(pair * 2 + 2, 2, 10, QLatin1Char('0'));
  }
  return labels;
}

QStringList MixerViewModel::outputLabels() {
  QStringList labels;
  for (int destination = 0; destination < kOutDstCount; ++destination) {
    labels << outputText(destination);
  }
  return labels;
}

QString MixerViewModel::inputSourceLabel(int state) {
  const QStringList labels = inputSourceLabels();
  if (state < 0 || state >= labels.size()) return labels.value(0);
  return labels.at(state);
}

QString MixerViewModel::outputLabel(int destination) {
  return outputText(destination);
}

int MixerViewModel::inSourceState(int slot) const {
  if (slot < 0 || slot >= audio::kMaxTracks) return 0;
  const int channel = model_.inCh(slot);
  if (channel < 0) return 0;
  if (model_.inPair(slot)) {
    return 1 + inputChannelCount() + clampInCh(channel) / 2;
  }
  return 1 + clampInCh(channel);
}

void MixerViewModel::setInSourceState(int slot, int state) {
  if (slot < 0 || slot >= audio::kMaxTracks) return;
  const int pairBase = 1 + inputChannelCount();
  if (state <= 0) {
    model_.setInPair(slot, false);
    model_.setInCh(slot, -1);
  } else if (state < pairBase) {
    model_.setInPair(slot, false);
    model_.setInCh(slot, state - 1);
  } else {
    // Pair first: setInPair clamps an odd channel down, then the even start
    // channel is stored unchanged while the pair flag is on.
    model_.setInPair(slot, true);
    model_.setInCh(slot, 2 * (state - pairBase));
  }
  const int row = model_.positionOfSlot(slot);
  if (row >= 0) {
    emit dataChanged(index(row, 0), index(row, 0), {InputTextRole});
  }
  if (slot == trackIndex_) emit trackChanged();
  updateChannelSummary();
}

void MixerViewModel::setOutDstOf(int slot, int destination) {
  if (slot < 0 || slot >= audio::kMaxTracks) return;
  model_.setOutDst(slot, destination);
  const int row = model_.positionOfSlot(slot);
  if (row >= 0) {
    emit dataChanged(index(row, 0), index(row, 0), {OutputTextRole});
  }
  if (slot == trackIndex_) emit trackChanged();
  updateChannelSummary();
}

void MixerViewModel::setTrackInCh(int channel) {
  model_.setInCh(trackIndex_, channel);
  emit trackChanged();
  updateChannelSummary();
}

void MixerViewModel::setTrackInPair(bool pair) {
  model_.setInPair(trackIndex_, pair);
  emit trackChanged();
  updateChannelSummary();
}

void MixerViewModel::setTrackOutDst(int destination) {
  model_.setOutDst(trackIndex_, destination);
  emit trackChanged();
  updateChannelSummary();
}

void MixerViewModel::cycleTrackInCh(int delta) {
  // -1 = none, then every device capture channel; both directions wrap.
  const int current = model_.inCh(trackIndex_);
  const int steps = inputChannelCount() + 1;
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

QString MixerViewModel::inSourceText(int slot) const {
  if (slot < 0 || slot >= audio::kMaxTracks) return {};
  return inputText(model_.inCh(slot), model_.inPair(slot));
}

QString MixerViewModel::cycleInSource(int slot, int delta) {
  if (slot < 0 || slot >= audio::kMaxTracks) return {};
  // State 0 is NONE, then the mono device channels, then the stereo pairs;
  // both directions wrap so the panel encoder never dead-ends.
  const int states = 1 + inputChannelCount() + inputChannelCount() / 2;
  int state = inSourceState(slot);
  state = ((state + delta) % states + states) % states;
  setInSourceState(slot, state);
  return inSourceText(slot);
}

QString MixerViewModel::toggleInPair(int slot) {
  if (slot < 0 || slot >= audio::kMaxTracks) return {};
  model_.setInPair(slot, !model_.inPair(slot));
  const int row = model_.positionOfSlot(slot);
  if (row >= 0) emit dataChanged(index(row, 0), index(row, 0), {InputTextRole});
  if (slot == trackIndex_) emit trackChanged();
  return inSourceText(slot);
}

// ---- dynamic track list ---------------------------------------------------

QString MixerViewModel::suggestedTrackName(int slot) const {
  return QString::fromStdString(model_.trackName(
      slot >= 0 && slot < audio::kMaxTracks ? slot : std::max(0, nextFreeSlot())));
}

bool MixerViewModel::addTrack(int slot, const QString& name, int inCh, bool inPair,
                              int outDst) {
  const int target = slot >= 0 ? slot : nextFreeSlot();
  if (target < 0) return false;
  if (!model_.addTrack(target, name.trimmed().toStdString())) return false;
  model_.setInPair(target, inPair);
  model_.setInCh(target, inCh);
  model_.setOutDst(target, outDst);
  reloadTracks();
  return true;
}

void MixerViewModel::removeTrack(int slot) {
  const int position = model_.positionOfSlot(slot);
  if (position < 0) return;
  if (!model_.removeTrackAt(position)) return;
  reloadTracks();
}

// ---- full channel popout --------------------------------------------------

void MixerViewModel::updateChannelSummary() {
  if (!channelOpen_) return;
  // The EQ band store is still sized to the device width (EqModel::kTrackCount,
  // session EqState) while the mixer supports audio::kMaxTracks slots. A slot
  // above the EQ store has no bands yet; say so instead of reading past the
  // array (the widening is a later wave).
  if (trackIndex_ < 0 || trackIndex_ >= EqModel::kTrackCount) {
    channelEqSummary_ = QStringLiteral("EQ UNAVAILABLE - SLOT BEYOND THE EQ STORE");
    emit channelChanged();
    return;
  }
  const auto bands = eq_.capture().tracks[static_cast<std::size_t>(trackIndex_)];
  QStringList parts;
  for (int band = 0; band < EqModel::kBandCount; ++band) {
    parts << QStringLiteral("%1 %2")
                 .arg(band + 1)
                 .arg(bandSummary(bands[static_cast<std::size_t>(band)]));
  }
  channelEqSummary_ = parts.join(QStringLiteral("  -  "));
  emit channelChanged();
}

void MixerViewModel::openChannel(int slot) {
  if (slot < 0 || slot >= audio::kMaxTracks) return;
  trackIndex_ = slot;
  setSelectedStrip(slot);
  channelOpen_ = true;
  updateChannelSummary();
  emit trackChanged();
  emit channelOpenChanged();
  emit channelChanged();
}

void MixerViewModel::closeChannel() {
  if (!channelOpen_) return;
  channelOpen_ = false;
  emit channelOpenChanged();
}

QString MixerViewModel::channelInsertName() const {
  return insertDisplayName(
      QString::fromStdString(model_.insert(trackIndex_).id));
}

bool MixerViewModel::channelInsertBypass() const {
  return model_.insert(trackIndex_).bypass;
}

void MixerViewModel::setChannelInsertBypass(bool bypass) {
  MixerModel::Insert insert = model_.insert(trackIndex_);
  insert.bypass = bypass;
  model_.setInsert(trackIndex_, std::move(insert));
  emit channelChanged();
}

void MixerViewModel::toggleChannelInsertBypass() {
  setChannelInsertBypass(!channelInsertBypass());
}

QStringList MixerViewModel::insertLabels() {
  return {QStringLiteral("NONE"), QStringLiteral("BUILTIN DELAY"),
          QStringLiteral("BUILTIN REVERB")};
}

QString MixerViewModel::insertId(int choice) {
  switch (choice) {
    case 1: return QStringLiteral("builtin.delay");
    case 2: return QStringLiteral("builtin.reverb");
    default: return QString();
  }
}

int MixerViewModel::insertChoice(const QString& id) {
  if (id == QStringLiteral("builtin.delay")) return 1;
  if (id == QStringLiteral("builtin.reverb")) return 2;
  return 0;
}

QString MixerViewModel::insertIdOf(int slot) const {
  if (slot < 0 || slot >= audio::kMaxTracks) return {};
  return QString::fromStdString(model_.insert(slot).id);
}

QString MixerViewModel::insertDisplayName(const QString& id) {
  if (id.isEmpty()) return QStringLiteral("NONE");
  if (id == QStringLiteral("builtin.delay")) return QStringLiteral("DELAY");
  if (id == QStringLiteral("builtin.reverb")) return QStringLiteral("REVERB");
  return id;
}

int MixerViewModel::insertChoiceAt(int slot) const {
  return insertChoice(QString::fromStdString(model_.insert(slot).id));
}

void MixerViewModel::setInsertChoiceAt(int slot, int choice) {
  MixerModel::Insert insert = model_.insert(slot);
  const QString id = insertId(choice);
  if (QString::fromStdString(insert.id) != id) {
    insert.id = id.toStdString();
    // A built-in insert starts from the effect's own documented defaults: the
    // parameter vector stays empty until the engine instance path (parallel
    // work item) owns per-track parameter storage.
    insert.params.clear();
  }
  model_.setInsert(slot, std::move(insert));
  const int row = model_.positionOfSlot(slot);
  if (row >= 0) emit dataChanged(index(row, 0), index(row, 0));
  if (slot == trackIndex_) emit channelChanged();
}

bool MixerViewModel::insertBypassAt(int slot) const {
  return model_.insert(slot).bypass;
}

void MixerViewModel::setInsertBypassAt(int slot, bool bypass) {
  MixerModel::Insert insert = model_.insert(slot);
  insert.bypass = bypass;
  model_.setInsert(slot, std::move(insert));
  if (slot == trackIndex_) emit channelChanged();
}

void MixerViewModel::notifyActiveTakesChanged() {
  if (rowCount() == 0) return;
  emit dataChanged(index(0, 0), index(rowCount() - 1, 0),
                   {MonitorMarkRole, ArmMutesTakeRole, ArmMutesBadgeRole});
}

void MixerViewModel::notifyTakeStopped() {
  armMutesUntilMs_ = steadyMs() + 8000;
  if (rowCount() == 0) return;
  emit dataChanged(index(0, 0), index(rowCount() - 1, 0),
                   {ArmMutesTakeRole, ArmMutesBadgeRole});
}

int MixerViewModel::armedWithActiveTakeCount() const {
  int count = 0;
  for (int slot = 0; slot < audio::kMaxTracks; ++slot) {
    if (model_.arm(slot) && settings_.hasActiveTake(slot)) ++count;
  }
  return count;
}

void MixerViewModel::notifyTrackNamesChanged() {
  if (rowCount() == 0) return;
  emit dataChanged(index(0, 0), index(rowCount() - 1, 0), {NameRole});
  emit trackChanged();
}

void MixerViewModel::setMasterDb(double db) {
  const double clamped = std::clamp(db, static_cast<double>(kMinGainDb),
                                    static_cast<double>(kMaxGainDb));
  model_.setMasterDb(static_cast<float>(clamped));
  emit masterChanged();
}

void MixerViewModel::setGainDb(int slot, double db) {
  if (slot < 0 || slot >= audio::kMaxTracks) return;
  const double clamped = std::clamp(db, static_cast<double>(kMinGainDb),
                                    static_cast<double>(kMaxGainDb));
  model_.setGainDb(slot, static_cast<float>(clamped));
  const int row = model_.positionOfSlot(slot);
  if (row >= 0) emit dataChanged(index(row, 0), index(row, 0), {GainDbRole});
  if (slot == trackIndex_) emit trackChanged();
}

void MixerViewModel::setPan(int slot, double pan) {
  if (slot < 0 || slot >= audio::kMaxTracks) return;
  double value = clampPan(pan);
  // Centre detent shared by touch and panel EDIT paths; the popup then reads
  // "Center" instead of a near-centre L/R value.
  if (std::abs(value - 0.5) <= 0.02) value = 0.5;
  model_.setPan(slot, static_cast<float>(value));
  const int row = model_.positionOfSlot(slot);
  if (row >= 0) {
    emit dataChanged(index(row, 0), index(row, 0), {PanRole, PanTextRole});
  }
  if (slot == trackIndex_) emit trackChanged();
}

void MixerViewModel::setSend(int slot, int send, double level) {
  if (slot < 0 || slot >= audio::kMaxTracks || send < 0 || send >= audio::kNumSends) return;
  model_.setSend(slot, send, static_cast<float>(clampSend(level)));
  const int row = model_.positionOfSlot(slot);
  if (row >= 0) {
    emit dataChanged(index(row, 0), index(row, 0), {send == 0 ? SendARole : SendBRole});
  }
  if (slot == trackIndex_) emit trackChanged();
}

void MixerViewModel::setCue(int slot, double level) {
  if (slot < 0 || slot >= audio::kMaxTracks) return;
  model_.setCueSend(slot, static_cast<float>(clampSend(level)));
  if (slot == trackIndex_) emit trackChanged();
}

void MixerViewModel::setOut34(int slot, double level) {
  if (slot < 0 || slot >= audio::kMaxTracks) return;
  model_.setOut34Send(slot, static_cast<float>(clampSend(level)));
  if (slot == trackIndex_) emit trackChanged();
}

void MixerViewModel::toggleMute(int slot) {
  if (slot < 0 || slot >= audio::kMaxTracks) return;
  model_.setMute(slot, !model_.mute(slot));
  const int row = model_.positionOfSlot(slot);
  if (row >= 0) emit dataChanged(index(row, 0), index(row, 0), {MuteRole});
  if (slot == trackIndex_) emit trackChanged();
}

void MixerViewModel::toggleSolo(int slot) {
  if (slot < 0 || slot >= audio::kMaxTracks) return;
  model_.setSolo(slot, !model_.solo(slot));
  const int row = model_.positionOfSlot(slot);
  if (row >= 0) emit dataChanged(index(row, 0), index(row, 0), {SoloRole});
  if (slot == trackIndex_) emit trackChanged();
}

void MixerViewModel::toggleArm(int slot) {
  if (slot < 0 || slot >= audio::kMaxTracks) return;
  model_.setArm(slot, !model_.arm(slot));
  const int row = model_.positionOfSlot(slot);
  if (row >= 0) emit dataChanged(index(row, 0), index(row, 0), {ArmRole});
  if (slot == trackIndex_) emit trackChanged();
}

void MixerViewModel::adjust(int slot, const QString& parameter, double delta) {
  if (slot < 0 || slot >= audio::kMaxTracks) return;
  if (parameter == QStringLiteral("gain")) {
    setGainDb(slot, model_.gainDb(slot) + delta);
  } else if (parameter == QStringLiteral("pan")) {
    setPan(slot, model_.pan(slot) + delta);
  } else if (parameter == QStringLiteral("sendA")) {
    setSend(slot, 0, model_.send(slot, 0) + delta);
  } else if (parameter == QStringLiteral("sendB")) {
    setSend(slot, 1, model_.send(slot, 1) + delta);
  } else if (parameter == QStringLiteral("cue")) {
    setCue(slot, model_.cueSend(slot) + delta);
  } else if (parameter == QStringLiteral("out34")) {
    setOut34(slot, model_.out34Send(slot) + delta);
  }
}

void MixerViewModel::setMeters(double left, double right) {
  if (std::abs(left - meterLeft_) < 0.001 && std::abs(right - meterRight_) < 0.001) return;
  meterLeft_ = left;
  meterRight_ = right;
  emit metersChanged();
}

void MixerViewModel::setStripMeters(const std::array<double, audio::kMaxTracks>& levels) {
  bool changed = false;
  for (int strip = 0; strip < audio::kMaxTracks; ++strip) {
    auto& hold = clipHolds_[static_cast<std::size_t>(strip)];
    (void)hold.tick();
    const double level = clampSend(levels[static_cast<std::size_t>(strip)]);
    (void)hold.update(static_cast<float>(level));
    if (std::abs(level - stripLevels_[static_cast<std::size_t>(strip)]) >= 0.001) {
      stripLevels_[static_cast<std::size_t>(strip)] = level;
      changed = true;
    }
  }
  if (changed && rowCount() > 0) {
    emit dataChanged(index(0, 0), index(rowCount() - 1, 0), {LevelRole, ClipRole});
    emit metersChanged();
  }
}

void MixerViewModel::showClipDemo() {
  clipDemo_ = true;
  for (int strip = 0; strip < audio::kMaxTracks; ++strip) {
    const int position = model_.positionOfSlot(strip);
    stripLevels_[static_cast<std::size_t>(strip)] =
        position >= 0 && position < 6
            ? 0.55 + 0.07 * static_cast<double>(position % 3)
            : 0.12;
  }
  if (rowCount() > 0) {
    emit dataChanged(index(0, 0), index(rowCount() - 1, 0), {LevelRole, ClipRole});
    emit metersChanged();
  }
}

void MixerViewModel::clearClipDemo() {
  if (!clipDemo_) return;
  clipDemo_ = false;
  if (rowCount() > 0) {
    emit dataChanged(index(0, 0), index(rowCount() - 1, 0), {ClipRole});
    emit metersChanged();
  }
}

void MixerViewModel::refresh() {
  if (rowCount() > 0) {
    emit dataChanged(index(0, 0), index(rowCount() - 1, 0));
  }
  updateChannelSummary();
  emit tracksChanged();
  emit masterChanged();
  emit selectedStripChanged();
  emit trackChanged();
}

}  // namespace mnky::app
