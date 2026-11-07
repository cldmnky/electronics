#include "mixer_view_model.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <utility>

#include "fx_params.hpp"
#include "fx_slot.hpp"
#include "logging.hpp"
#include "logic.hpp"

namespace mnky::app {
namespace {

// The FxManager instance created for a retry when the slot names a plugin but
// no host is live. AudioGlue loads at the device's actual rate on the normal
// path; this retry only runs when that path produced no instance, so the
// engine's 48 kHz project rate is the only value available here.
constexpr double kClapSampleRate = 48000.0;

QString panText(double pan) {
  const int percent = static_cast<int>(std::lround((pan - 0.5) * 200.0));
  if (percent == 0) return QStringLiteral("C");
  return QString(percent < 0 ? QStringLiteral("L") : QStringLiteral("R")) +
         QString::number(std::abs(percent));
}

double clampPan(double pan) { return std::clamp(pan, 0.0, 1.0); }
double clampSend(double level) { return std::clamp(level, 0.0, 1.0); }

// CLAP's params extension has no unit field, so a plugin parameter reads as a
// trimmed number. The plugin's own name/min/max/value text is otherwise shown
// verbatim from its clap.params table; inventing dB/Hz labels here would lie.
QString clapParamText(double value) {
  char buffer[32] = {};
  const double magnitude = std::abs(value);
  if (magnitude >= 100.0) {
    std::snprintf(buffer, sizeof buffer, "%.0f", value);
  } else {
    std::snprintf(buffer, sizeof buffer, "%.2f", value);
  }
  QString text = QString::fromLatin1(buffer);
  if (text.contains(QLatin1Char('.'))) {
    while (text.endsWith(QLatin1Char('0'))) text.chop(1);
    if (text.endsWith(QLatin1Char('.'))) text.chop(1);
  }
  return text;
}

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

QVariantList MixerViewModel::bankPages() const {
  // A fixed grid over the capacity: a page past the track list stays
  // visible but dimmed, so "there is no track 17 yet" is legible instead of the
  // grid silently shrinking.
  constexpr int kPages =
      (audio::kMaxTracks + kStripsPerBank - 1) / kStripsPerBank;
  QVariantList pages;
  const int count = model_.trackCount();
  for (int page = 0; page < kPages; ++page) {
    const int first = page * kStripsPerBank + 1;
    const int last = first + kStripsPerBank - 1;
    QVariantMap row;
    row.insert(QStringLiteral("index"), page);
    row.insert(QStringLiteral("label"),
               QStringLiteral("%1-%2").arg(first).arg(last));
    row.insert(QStringLiteral("empty"), first > count);
    row.insert(QStringLiteral("current"), page == bank_);
    pages.append(row);
  }
  return pages;
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
  if (clipDemo_) {
    return model_.positionOfSlot(slot) >= 0 && model_.positionOfSlot(slot) < 6 &&
           !clipDemoCleared_[static_cast<std::size_t>(slot)];
  }
  return clipHolds_[static_cast<std::size_t>(slot)].clipped();
}

QString MixerViewModel::modeWordAt(int slot) const {
  return QString::fromStdString(trackModeWord(model_.mode(slot)));
}

QString MixerViewModel::selectedModeWord() const { return modeWordAt(trackIndex_); }

QString MixerViewModel::selectedRecSourceText() const {
  return QString::fromStdString(recSourceText(model_.recSource(trackIndex_)));
}

void MixerViewModel::cycleMode(int slot) {
  setMode(slot, nextTrackMode(model_.mode(slot)));
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
    case Fx1WetRole:
      return fxWetAt(slot, 0);
    case Fx2WetRole:
      return fxWetAt(slot, 1);
    case Fx1NameRole:
      return fxNameAt(slot, 0);
    case Fx2NameRole:
      return fxNameAt(slot, 1);
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
    case ModeRole:
      return model_.mode(slot);
    case ModeWordRole:
      return modeWordAt(slot);
    case InputTextRole:
      return inputText(model_.inCh(slot), model_.inPair(slot));
    case OutputTextRole:
      return outputText(model_.outDst(slot));
    case SelectedRole:
      return slot == selectedStrip_;
    default:
      break;
  }
  return {};
}

QHash<int, QByteArray> MixerViewModel::roleNames() const {
  return {{IndexRole, "index"},           {SlotRole, "slot"},
          {NameRole, "name"},             {GainDbRole, "gainDb"},
          {PanRole, "pan"},               {PanTextRole, "panText"},
          {Fx1WetRole, "fx1Wet"},         {Fx2WetRole, "fx2Wet"},
          {Fx1NameRole, "fx1Name"},       {Fx2NameRole, "fx2Name"},
          {MuteRole, "mute"},             {SoloRole, "solo"},
          {ArmRole, "arm"},               {LevelRole, "level"},
          {ClipRole, "clip"},             {ModeRole, "mode"},
          {ModeWordRole, "modeWord"},     {InputTextRole, "inputText"},
          {OutputTextRole, "outputText"}, {SelectedRole, "selected"}};
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
  if (row >= 0) {
    emit dataChanged(index(row, 0), index(row, 0), {ArmRole, ModeRole, ModeWordRole});
  }
  emit trackChanged();
}

void MixerViewModel::setSelectedMode(int mode) { setMode(trackIndex_, mode); }

void MixerViewModel::setMode(int slot, int mode) {
  if (slot < 0 || slot >= audio::kMaxTracks) return;
  model_.setMode(slot, mode);
  const int row = model_.positionOfSlot(slot);
  if (row >= 0) {
    // The mode decides the record arm too, so the strip's ARM fill follows it.
    emit dataChanged(index(row, 0), index(row, 0), {ModeRole, ModeWordRole, ArmRole});
  }
  if (slot == trackIndex_) emit trackChanged();
}

void MixerViewModel::setSelectedRecSource(int source) {
  setRecSource(trackIndex_, source);
}

void MixerViewModel::setRecSource(int slot, int source) {
  model_.setRecSource(slot, source);
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

QString MixerViewModel::channelFxSummary() const {
  return QStringLiteral("FX1 ") + fxNameAt(trackIndex_, 0) + QStringLiteral("  FX2 ") +
         fxNameAt(trackIndex_, 1);
}

QStringList MixerViewModel::fxChoiceLabels() const {
  QStringList labels{QStringLiteral("NONE"), QStringLiteral("DELAY"),
                     QStringLiteral("REVERB")};
  if (clap_ != nullptr) {
    for (const auto& plugin : clap_->plugins()) {
      labels << QString::fromStdString(plugin.name).toUpper();
    }
  }
  return labels;
}

QString MixerViewModel::fxChoiceId(int choice) const {
  switch (choice) {
    case 1: return QString::fromLatin1(kFxIdDelay);
    case 2: return QString::fromLatin1(kFxIdReverb);
    case 0: return QString();
    default: break;
  }
  if (clap_ == nullptr) return {};
  const auto& plugins = clap_->plugins();
  const auto index = static_cast<std::size_t>(choice - kFxChoicePlugin);
  if (index >= plugins.size()) return {};
  return QString::fromStdString(plugins[index].id);
}

int MixerViewModel::fxChoiceOf(const QString& id) const {
  if (id == QString::fromLatin1(kFxIdDelay)) return 1;
  if (id == QString::fromLatin1(kFxIdReverb)) return 2;
  if (id.isEmpty()) return 0;
  if (clap_ != nullptr) {
    const auto& plugins = clap_->plugins();
    for (std::size_t index = 0; index < plugins.size(); ++index) {
      if (QString::fromStdString(plugins[index].id) == id) {
        return kFxChoicePlugin + static_cast<int>(index);
      }
    }
  }
  return kFxChoicePlugin;
}

QString MixerViewModel::fxName(const QString& id) const {
  if (id.isEmpty()) return QStringLiteral("NONE");
  if (id == QString::fromLatin1(kFxIdDelay)) return QStringLiteral("DELAY");
  if (id == QString::fromLatin1(kFxIdReverb)) return QStringLiteral("REVERB");
  if (clap_ != nullptr) {
    if (const auto* plugin = clap_->findPlugin(id.toStdString())) {
      return QString::fromStdString(plugin->name);
    }
  }
  // An unavailable plugin id is still named honestly instead of pretending to
  // be a built-in effect.
  return QStringLiteral("PLUGIN ") + id;
}

QString MixerViewModel::fxChoiceRefusal(int slot, int index) const {
  if (slot != fxRefusalSlot_ || index != fxRefusalIndex_) return {};
  return fxChoiceRefusal_;
}

void MixerViewModel::clearFxChoiceRefusal() {
  fxChoiceRefusal_.clear();
  fxRefusalSlot_ = -1;
  fxRefusalIndex_ = -1;
}

QString MixerViewModel::fxNameAt(int slot, int index) const {
  if (slot < 0 || slot >= audio::kMaxTracks) return QStringLiteral("NONE");
  return fxName(QString::fromStdString(model_.fxSlot(slot, index).id));
}

int MixerViewModel::fxChoiceAt(int slot, int index) const {
  if (slot < 0 || slot >= audio::kMaxTracks) return 0;
  return fxChoiceOf(QString::fromStdString(model_.fxSlot(slot, index).id));
}

bool MixerViewModel::setFxChoiceAt(int slot, int index, int choice) {
  if (slot < 0 || slot >= audio::kMaxTracks) return false;
  fxChoiceRefusal_.clear();
  fxRefusalSlot_ = slot;
  fxRefusalIndex_ = index;
  const QStringList labels = fxChoiceLabels();
  if (choice < 0 || choice >= labels.size()) return false;

  MixerModel::FxSlot fx = model_.fxSlot(slot, index);
  if (choice < kFxChoiceBuiltinCount) {
    // NONE / DELAY / REVERB: the sink detaches and destroys any CLAP instance
    // this slot was holding.
    const QString id = fxChoiceId(choice);
    if (QString::fromStdString(fx.id) != id) {
      fx.id = id.toStdString();
      // A newly selected effect starts from its published defaults (MixerModel
      // fills them) instead of inheriting the previous effect's parameters.
      fx.params.clear();
      fx.bypass = false;
    }
    model_.setFxSlot(slot, index, std::move(fx));
    notifyFxChanged(slot);
    return true;
  }

  if (clap_ == nullptr) {
    fxChoiceRefusal_ = QStringLiteral("PLUGIN HOST UNAVAILABLE");
    notifyFxChanged(slot);
    return false;
  }
  const auto& plugins = clap_->plugins();
  const auto pluginIndex = static_cast<std::size_t>(choice - kFxChoicePlugin);
  if (pluginIndex >= plugins.size()) return false;
  const auto& plugin = plugins[pluginIndex];
  if (fx.id == plugin.id) {
    if (clap_->trackEffectLoaded(slot, index)) {
      // Re-selecting a live plugin is a no-op, not another instance.
      notifyFxChanged(slot);
      return true;
    }
    // The slot names this plugin but no instance is running (an earlier load
    // failed, the host was torn down, the module was replaced on disk). The
    // selector is the owner's enable affordance, so re-selecting the same
    // entry must retry the instance instead of silently doing nothing.
    if (!clap_->loadTrackEffect(slot, index, plugin.id, kClapSampleRate,
                                audio::kBlockSize)) {
      const std::string error = clap_->trackEffectError(slot, index);
      fxChoiceRefusal_ =
          QStringLiteral("NOT LOADED - ") +
          (error.empty() ? QStringLiteral("plugin refused to load")
                         : QString::fromStdString(error));
      notifyFxChanged(slot);
      return false;
    }
    notifyFxChanged(slot);
    return true;
  }
  if (!clap_->canLoadTrackEffect(slot, index)) {
    fxChoiceRefusal_ = QStringLiteral("PLUGIN LIMIT REACHED - unload one first");
    log("fx: track " + std::to_string(slot) + " slot " +
        std::to_string(index) + " refused " + plugin.id +
        ": plugin limit reached");
    notifyFxChanged(slot);
    return false;
  }
  // The host instance itself is created by the AudioGlue sink below (control
  // thread, actual sample rate). The session vector starts from the plugin's
  // own parameter defaults captured by the scan, so a new slot is complete
  // before the first audio block.
  fx.id = plugin.id;
  fx.params.clear();
  fx.params.reserve(plugin.params.size());
  for (const auto& param : plugin.params) fx.params.push_back(param.value);
  fx.bypass = false;
  const MixerModel::FxSlot previous = model_.fxSlot(slot, index);
  model_.setFxSlot(slot, index, std::move(fx));
  // The sink instantiates the host synchronously (AudioGlue::applyTrackFxSlot).
  // If that failed, the exact reason is recorded on the FxManager; put the
  // previous slot back instead of leaving a selected-but-dead entry in the
  // rack that reads as "can be selected but never activates".
  const std::string loadError = clap_->trackEffectError(slot, index);
  if (!clap_->trackEffectLoaded(slot, index) && !loadError.empty()) {
    model_.setFxSlot(slot, index, previous);
    fxChoiceRefusal_ = QStringLiteral("NOT LOADED - ") +
                       QString::fromStdString(loadError);
    notifyFxChanged(slot);
    return false;
  }
  notifyFxChanged(slot);
  return true;
}

bool MixerViewModel::fxBypassAt(int slot, int index) const {
  if (slot < 0 || slot >= audio::kMaxTracks) return false;
  return model_.fxSlot(slot, index).bypass;
}

void MixerViewModel::setFxBypassAt(int slot, int index, bool bypass) {
  if (slot < 0 || slot >= audio::kMaxTracks) return;
  model_.setFxBypass(slot, index, bypass);
  notifyFxChanged(slot);
}

double MixerViewModel::fxWetAt(int slot, int index) const {
  if (slot < 0 || slot >= audio::kMaxTracks) return 0.0;
  return model_.fxSlot(slot, index).wet;
}

void MixerViewModel::setFxWetAt(int slot, int index, double wet) {
  if (slot < 0 || slot >= audio::kMaxTracks) return;
  model_.setFxWet(slot, index, static_cast<float>(clampSend(wet)));
  notifyFxChanged(slot);
}

QVariantList MixerViewModel::fxParams(int slot, int index) const {
  QVariantList cards;
  if (slot < 0 || slot >= audio::kMaxTracks) return cards;
  const auto fx = model_.fxSlot(slot, index);
  if (!fx.id.empty() && !fxIsBuiltin(fx.id)) {
    // CLAP: the plugin's own parameter table (names, ranges and current
    // values) from its clap.params extension; the slot vector is the saved
    // session contract, so a loaded value wins over the host default.
    std::vector<clap::Param> params;
    if (clap_ != nullptr) params = clap_->trackParams(slot, index);
    if (params.empty() && clap_ != nullptr) {
      if (const auto* plugin = clap_->findPlugin(fx.id)) {
        params = plugin->params;
      }
    }
    for (std::size_t param = 0; param < params.size(); ++param) {
      const auto& meta = params[param];
      const double value =
          param < fx.params.size() ? fx.params[param] : meta.value;
      QVariantMap card;
      card.insert(QStringLiteral("index"), static_cast<int>(param));
      card.insert(QStringLiteral("name"),
                  QString::fromStdString(meta.name).toUpper());
      card.insert(QStringLiteral("value"), value);
      card.insert(QStringLiteral("text"), clapParamText(value));
      card.insert(QStringLiteral("unit"), QString());
      card.insert(QStringLiteral("minimum"), meta.min);
      card.insert(QStringLiteral("maximum"), meta.max);
      card.insert(QStringLiteral("stepped"), false);
      cards.push_back(card);
    }
    return cards;
  }
  const auto table = fxParamTable(fx.id);
  if (table.empty()) return cards;
  for (const auto& meta : table) {
    const double value = meta.index < static_cast<int>(fx.params.size())
                             ? fx.params[static_cast<std::size_t>(meta.index)]
                             : meta.def;
    QVariantMap card;
    card.insert(QStringLiteral("index"), meta.index);
    card.insert(QStringLiteral("name"), QString::fromLatin1(meta.label));
    card.insert(QStringLiteral("value"), value);
    card.insert(QStringLiteral("text"),
                QString::fromStdString(
                    fxParamValueText(fx.id, meta.index, value)));
    card.insert(QStringLiteral("unit"), QString::fromLatin1(meta.unit));
    card.insert(QStringLiteral("minimum"), meta.min);
    card.insert(QStringLiteral("maximum"), meta.max);
    card.insert(QStringLiteral("stepped"), meta.stepped);
    cards.push_back(card);
  }
  return cards;
}

void MixerViewModel::setFxParamAt(int slot, int index, int param, double value) {
  if (slot < 0 || slot >= audio::kMaxTracks) return;
  const auto fx = model_.fxSlot(slot, index);
  if (!fx.id.empty() && !fxIsBuiltin(fx.id)) {
    if (clap_ == nullptr) return;
    auto params = clap_->trackParams(slot, index);
    if (params.empty()) {
      if (const auto* plugin = clap_->findPlugin(fx.id)) params = plugin->params;
    }
    if (param < 0 || param >= static_cast<int>(params.size())) return;
    const auto& meta = params[static_cast<std::size_t>(param)];
    const double clamped = std::clamp(value, meta.min, meta.max);
    MixerModel::FxSlot next = fx;
    if (next.params.size() != params.size()) {
      next.params.resize(params.size());
      for (std::size_t entry = 0; entry < params.size(); ++entry) {
        next.params[entry] = entry < fx.params.size() ? fx.params[entry]
                                                      : params[entry].value;
      }
    }
    next.params[static_cast<std::size_t>(param)] = clamped;
    model_.setFxSlot(slot, index, std::move(next));
    notifyFxChanged(slot);
    return;
  }
  model_.setFxParam(slot, index, param, value);
  notifyFxChanged(slot);
}

QString MixerViewModel::fxSubline() const {
  return QStringLiteral("series FX1 -> FX2 - per track");
}

QString MixerViewModel::fxSlotNote(int slot, int index) const {
  if (slot < 0 || slot >= audio::kMaxTracks) return QString();
  const QString refusal = fxChoiceRefusal(slot, index);
  if (!refusal.isEmpty()) return refusal;
  const auto fx = model_.fxSlot(slot, index);
  if (fx.id.empty()) return QStringLiteral("slot is NONE - pick an effect or plugin");
  if (!fxIsBuiltin(fx.id)) {
    const QString name = fxName(QString::fromStdString(fx.id));
    if (clap_ != nullptr && clap_->trackEffectLoaded(slot, index)) {
      const int count = static_cast<int>(fx.params.size());
      return QStringLiteral("%1 - %2 PAR - wet %3% - %4")
          .arg(name)
          .arg(count)
          .arg(static_cast<int>(std::lround(fx.wet * 100.0f)))
          .arg(fx.bypass ? QStringLiteral("BYPASSED") : QStringLiteral("active"));
    }
    if (clap_ != nullptr) {
      const std::string error = clap_->trackEffectError(slot, index);
      if (!error.empty()) {
        return name + QStringLiteral(" - NOT LOADED: ") +
               QString::fromStdString(error);
      }
    }
    return name + QStringLiteral(" - NOT LOADED");
  }
  return QStringLiteral("wet %1% - %2")
      .arg(static_cast<int>(std::lround(fx.wet * 100.0f)))
      .arg(fx.bypass ? QStringLiteral("BYPASSED") : QStringLiteral("active"));
}

void MixerViewModel::notifyFxChanged(int slot) {
  const int row = model_.positionOfSlot(slot);
  if (row >= 0) {
    emit dataChanged(index(row, 0), index(row, 0),
                     {Fx1WetRole, Fx2WetRole, Fx1NameRole, Fx2NameRole});
  }
  if (slot == trackIndex_) {
    emit trackChanged();
    emit channelChanged();
  }
  emit fxChanged();
}

void MixerViewModel::notifyActiveTakesChanged() {
  if (rowCount() == 0) return;
  emit dataChanged(index(0, 0), index(rowCount() - 1, 0), {ModeRole, ModeWordRole});
}

int MixerViewModel::armedWithActiveTakeCount() const {
  int count = 0;
  for (int slot = 0; slot < audio::kMaxTracks; ++slot) {
    // ARM only: an ARM strip suppresses its own take voice (Auto monitor while
    // armed), while OVERDUB keeps the take playing under the live layer.
    if (model_.mode(slot) == kTrackModeArm && settings_.hasActiveTake(slot)) {
      ++count;
    }
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
  } else if (parameter == QStringLiteral("fx1wet")) {
    setFxWetAt(slot, 0, model_.fxSlot(slot, 0).wet + delta);
  } else if (parameter == QStringLiteral("fx2wet")) {
    setFxWetAt(slot, 1, model_.fxSlot(slot, 1).wet + delta);
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
  clipDemoCleared_.fill(false);
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

void MixerViewModel::clearClip(int slot) {
  if (slot < 0 || slot >= audio::kMaxTracks) return;
  clipHolds_[static_cast<std::size_t>(slot)].clear();
  clipDemoCleared_[static_cast<std::size_t>(slot)] = true;
  const int row = model_.positionOfSlot(slot);
  if (row >= 0) emit dataChanged(index(row, 0), index(row, 0), {ClipRole});
  emit metersChanged();
}

void MixerViewModel::clearClipDemo() {
  if (!clipDemo_) return;
  clipDemo_ = false;
  clipDemoCleared_.fill(false);
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
