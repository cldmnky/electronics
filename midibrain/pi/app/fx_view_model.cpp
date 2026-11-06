#include "fx_view_model.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>

#include "app_state.hpp"
#include "logic.hpp"
#include "logging.hpp"
#include "mixer_model.hpp"

namespace mnky::app {

FxParamModel::FxParamModel(FxSlot& slot, QObject* parent)
    : QAbstractListModel(parent), slot_(slot) {
  rebuild();
}

int FxParamModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(params_.size());
}

QVariant FxParamModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 ||
      index.row() >= static_cast<int>(params_.size())) {
    return {};
  }
  const auto& param = params_[static_cast<std::size_t>(index.row())];
  switch (role) {
    case NameRole:
      return QString::fromStdString(param.name);
    case ValueRole:
      return param.value;
    case MinimumRole:
      return param.min;
    case MaximumRole:
      return param.max;
    default:
      break;
  }
  return {};
}

QHash<int, QByteArray> FxParamModel::roleNames() const {
  return {{NameRole, "name"},
          {ValueRole, "value"},
          {MinimumRole, "minimum"},
          {MaximumRole, "maximum"}};
}

void FxParamModel::rebuild() {
  beginResetModel();
  params_ = slot_.params();
  endResetModel();
}

void FxParamModel::refresh() {
  const auto current = slot_.params();
  bool same = current.size() == params_.size();
  if (same) {
    for (std::size_t i = 0; i < current.size(); ++i) {
      if (current[i].id != params_[i].id || current[i].name != params_[i].name) {
        same = false;
        break;
      }
    }
  }
  if (!same) {
    beginResetModel();
    params_ = current;
    endResetModel();
    return;
  }
  for (std::size_t i = 0; i < current.size(); ++i) {
    if (current[i].value != params_[i].value || current[i].min != params_[i].min ||
        current[i].max != params_[i].max) {
      params_[i] = current[i];
      emit dataChanged(index(static_cast<int>(i), 0), index(static_cast<int>(i), 0),
                       {ValueRole, MinimumRole, MaximumRole});
    }
  }
}

FxSlotViewModel::FxSlotViewModel(FxSlot& slot, int index, AppState& state,
                                 MixerModel& mixer, QObject* parent)
    : QObject(parent),
      slot_(slot),
      index_(index),
      state_(state),
      mixer_(mixer),
      params_(slot, nullptr),
      title_(QStringLiteral("SEND %1").arg(QChar(QLatin1Char('A').unicode() + index))),
      returnDb_(mixer.fxReturnDb(index)) {
  auto directory = state_.takeDirectory();
  if (!directory.empty() && directory.has_parent_path()) directory = directory.parent_path();
  const auto name = index == 1 ? "fx-slot-B.state" : "fx-slot-A.state";
  statePath_ = QString::fromStdString((directory / name).string());
  refresh();
}

void FxSlotViewModel::setBypassed(bool bypassed) {
  slot_.setBypassed(bypassed);
  if (bypassed_ == bypassed) return;
  bypassed_ = bypassed;
  emit bypassedChanged();
  updateStatus();
}

void FxSlotViewModel::setReturnDb(double db) {
  const double clamped = std::clamp(db, static_cast<double>(kMinGainDb),
                                    static_cast<double>(kMaxGainDb));
  mixer_.setFxReturnDb(index_, static_cast<float>(clamped));
  if (std::abs(clamped - returnDb_) < 0.001) return;
  returnDb_ = clamped;
  emit returnChanged();
}

void FxSlotViewModel::loadDefault() {
  if (!slot_.pluginPath().empty()) return;
  const auto candidate = FxSlot::defaultPluginPath();
  if (candidate.empty()) {
    statusText_ = QStringLiteral("no default plugin found (set MNKY_PLUGIN_PATH)");
    emit statusChanged();
    return;
  }
  (void)slot_.load(candidate, 48000.0, audio::kBlockSize);
  rebuildParams();
  refresh();
}

void FxSlotViewModel::eject() {
  slot_.unload();
  rebuildParams();
  refresh();
}

void FxSlotViewModel::saveState() {
  const bool ok = slot_.saveState(statePath_.toStdString());
  statusText_ = ok ? QStringLiteral("state saved to ") + statePath_
                   : QString::fromStdString(slot_.lastError());
  emit statusChanged();
}

void FxSlotViewModel::loadState() {
  const bool ok = slot_.loadState(statePath_.toStdString());
  if (ok) params_.rebuild();
  statusText_ = ok ? QStringLiteral("state loaded from ") + statePath_
                   : QString::fromStdString(slot_.lastError());
  emit statusChanged();
}

void FxSlotViewModel::setParam(int index, double value) {
  if (index < 0 || index >= params_.rowCount()) return;
  const auto list = slot_.params();
  if (index >= static_cast<int>(list.size())) return;
  const auto& param = list[static_cast<std::size_t>(index)];
  const double clamped = std::clamp(value, param.min, param.max);
  slot_.setParam(param.id, clamped);
  params_.refresh();
}

void FxSlotViewModel::adjustParam(int index, double delta) {
  const auto list = slot_.params();
  if (index < 0 || index >= static_cast<int>(list.size())) return;
  const auto& param = list[static_cast<std::size_t>(index)];
  setParam(index, param.value + delta);
}

void FxSlotViewModel::toggleBypass() {
  setBypassed(!bypassed_);
}

void FxSlotViewModel::refresh() {
  const auto path = slot_.pluginPath();
  const auto name = slot_.pluginName();
  const bool loaded = slot_.loaded();
  const bool bypassed = slot_.bypassed();
  const double returnDb = mixer_.fxReturnDb(index_);
  const QString newPath = QString::fromStdString(path);
  const QString newName = QString::fromStdString(name);
  bool changed = false;
  if (pluginPath_ != newPath || pluginName_ != newName || loaded_ != loaded) {
    pluginPath_ = newPath;
    pluginName_ = newName;
    loaded_ = loaded;
    changed = true;
  }
  if (bypassed_ != bypassed) {
    bypassed_ = bypassed;
    emit bypassedChanged();
  }
  if (std::abs(returnDb - returnDb_) > 0.001) {
    returnDb_ = returnDb;
    emit returnChanged();
  }
  if (changed) emit pluginChanged();
  params_.refresh();
  updateStatus();
}

void FxSlotViewModel::rebuildParams() {
  params_.rebuild();
}

void FxSlotViewModel::updateStatus() {
  QString text;
  if (!loaded_) {
    text = QStringLiteral("send idle; return passes the bus through");
  } else {
    text = bypassed_ ? QStringLiteral("loaded (bypassed)") : QStringLiteral("loaded");
  }
  const auto error = slot_.lastError();
  if (!error.empty()) text += QStringLiteral("  -  ") + QString::fromStdString(error);
  if (statusText_ != text) {
    statusText_ = text;
    emit statusChanged();
  }
}

BuiltinFxViewModel::BuiltinFxViewModel(PerformanceModel& performance, QObject* parent)
    : QObject(parent), performance_(performance) {}

double BuiltinFxViewModel::param(int grid, int index) const {
  return performance_.param(grid, index);
}

void BuiltinFxViewModel::setParamValue(int grid, int index, double value) {
  if (!performance_.setParam(grid, index, value)) return;
  emit changed();
}

double BuiltinFxViewModel::delayTimeMs() const {
  return param(PerformanceModel::kDelayGrid, audio::fx::Delay::kTimeMs);
}
void BuiltinFxViewModel::setDelayTimeMs(double value) {
  setParamValue(PerformanceModel::kDelayGrid, audio::fx::Delay::kTimeMs,
                std::clamp(value, 1.0, audio::fx::Delay::kMaxTimeMs));
}
double BuiltinFxViewModel::delayFeedback() const {
  return param(PerformanceModel::kDelayGrid, audio::fx::Delay::kFeedback);
}
void BuiltinFxViewModel::setDelayFeedback(double value) {
  setParamValue(PerformanceModel::kDelayGrid, audio::fx::Delay::kFeedback,
                std::clamp(value, 0.0, 0.95));
}
double BuiltinFxViewModel::delayMix() const {
  return param(PerformanceModel::kDelayGrid, audio::fx::Delay::kMix);
}
void BuiltinFxViewModel::setDelayMix(double value) {
  setParamValue(PerformanceModel::kDelayGrid, audio::fx::Delay::kMix,
                std::clamp(value, 0.0, 1.0));
}
bool BuiltinFxViewModel::delayBeat() const {
  return param(PerformanceModel::kDelayGrid, audio::fx::Delay::kBeatMode) >= 0.5;
}
void BuiltinFxViewModel::setDelayBeat(bool value) {
  setParamValue(PerformanceModel::kDelayGrid, audio::fx::Delay::kBeatMode,
                value ? 1.0 : 0.0);
}
int BuiltinFxViewModel::delayDivision() const {
  return static_cast<int>(std::lround(
      param(PerformanceModel::kDelayGrid, audio::fx::Delay::kDivision)));
}
void BuiltinFxViewModel::setDelayDivision(int value) {
  setParamValue(PerformanceModel::kDelayGrid, audio::fx::Delay::kDivision,
                std::clamp(value, 0, audio::fx::Delay::kDivCount - 1));
}
QString BuiltinFxViewModel::delayDivisionText() const {
  return QString::fromLatin1(audio::fx::Delay::divisionName(delayDivision()));
}
bool BuiltinFxViewModel::delayPingPong() const {
  return param(PerformanceModel::kDelayGrid, audio::fx::Delay::kPingPong) >= 0.5;
}
void BuiltinFxViewModel::setDelayPingPong(bool value) {
  setParamValue(PerformanceModel::kDelayGrid, audio::fx::Delay::kPingPong,
                value ? 1.0 : 0.0);
}
double BuiltinFxViewModel::delayCutoffHz() const {
  return param(PerformanceModel::kDelayGrid, audio::fx::Delay::kBpCutoffHz);
}
void BuiltinFxViewModel::setDelayCutoffHz(double value) {
  setParamValue(PerformanceModel::kDelayGrid, audio::fx::Delay::kBpCutoffHz,
                std::clamp(value, 20.0, 20000.0));
}
double BuiltinFxViewModel::delayWidthOct() const {
  return param(PerformanceModel::kDelayGrid, audio::fx::Delay::kBpWidthOct);
}
void BuiltinFxViewModel::setDelayWidthOct(double value) {
  setParamValue(PerformanceModel::kDelayGrid, audio::fx::Delay::kBpWidthOct,
                std::clamp(value, 0.1, 4.0));
}
double BuiltinFxViewModel::reverbPredelayMs() const {
  return param(PerformanceModel::kReverbGrid, audio::fx::Reverb::kPredelayMs);
}
void BuiltinFxViewModel::setReverbPredelayMs(double value) {
  setParamValue(PerformanceModel::kReverbGrid, audio::fx::Reverb::kPredelayMs,
                std::clamp(value, 0.0, 200.0));
}
double BuiltinFxViewModel::reverbDecay() const {
  return param(PerformanceModel::kReverbGrid, audio::fx::Reverb::kDecay);
}
void BuiltinFxViewModel::setReverbDecay(double value) {
  setParamValue(PerformanceModel::kReverbGrid, audio::fx::Reverb::kDecay,
                std::clamp(value, 0.0, 0.98));
}
double BuiltinFxViewModel::reverbDamping() const {
  return param(PerformanceModel::kReverbGrid, audio::fx::Reverb::kDamping);
}
void BuiltinFxViewModel::setReverbDamping(double value) {
  setParamValue(PerformanceModel::kReverbGrid, audio::fx::Reverb::kDamping,
                std::clamp(value, 0.0, 1.0));
}
double BuiltinFxViewModel::reverbMix() const {
  return param(PerformanceModel::kReverbGrid, audio::fx::Reverb::kMix);
}
void BuiltinFxViewModel::setReverbMix(double value) {
  setParamValue(PerformanceModel::kReverbGrid, audio::fx::Reverb::kMix,
                std::clamp(value, 0.0, 1.0));
}
int BuiltinFxViewModel::reverbModel() const {
  return static_cast<int>(std::lround(
      param(PerformanceModel::kReverbGrid, audio::fx::Reverb::kModel)));
}
void BuiltinFxViewModel::setReverbModel(int value) {
  const int model = std::clamp(value, 0, audio::fx::Reverb::kModelCount - 1);
  // The model is a preset: applying it also rewrites decay/damping/size, so the
  // panel reads them back from the effect instead of showing stale values.
  if (!performance_.setReverbModel(model)) return;
  emit changed();
}
QString BuiltinFxViewModel::reverbModelText() const {
  return QString::fromLatin1(audio::fx::Reverb::modelName(reverbModel()));
}
double BuiltinFxViewModel::reverbSize() const {
  return param(PerformanceModel::kReverbGrid, audio::fx::Reverb::kSize);
}
void BuiltinFxViewModel::setReverbSize(double value) {
  setParamValue(PerformanceModel::kReverbGrid, audio::fx::Reverb::kSize,
                std::clamp(value, 0.25, 2.0));
}

void BuiltinFxViewModel::cycleDelayDivision(int delta) {
  const int next = delayDivision() + (delta < 0 ? -1 : 1);
  setDelayDivision((next + audio::fx::Delay::kDivCount) % audio::fx::Delay::kDivCount);
}

void BuiltinFxViewModel::cycleReverbModel(int delta) {
  const int next = reverbModel() + (delta < 0 ? -1 : 1);
  setReverbModel((next + audio::fx::Reverb::kModelCount) %
                 audio::fx::Reverb::kModelCount);
}

void BuiltinFxViewModel::refresh() { emit changed(); }

FxViewModel::FxViewModel(FxManager& fx, AppState& state, MixerModel& mixer,
                         PerformanceModel& performance, double sampleRate,
                         QObject* parent)
    : QObject(parent),
      slotA_(fx.slot(0), 0, state, mixer, this),
      slotB_(fx.slot(1), 1, state, mixer, this),
      builtin_(performance) {
  (void)sampleRate;
}

void FxViewModel::loadDefaults() {
  slotA_.loadDefault();
}

void FxViewModel::refresh() {
  slotA_.refresh();
  slotB_.refresh();
  builtin_.refresh();
}

}  // namespace mnky::app
