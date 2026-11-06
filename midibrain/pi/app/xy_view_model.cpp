#include "xy_view_model.hpp"

#include <algorithm>
#include <cmath>

#include <QStringList>

#include "../audio/fx/reverb.hpp"
#include "logging.hpp"

namespace mnky::app {
namespace {

constexpr int kAxisX = 0;
constexpr int kAxisY = 1;

}  // namespace

XySlotView::XySlotView(XyViewModel& owner, int slot)
    : QObject(&owner), owner_(owner), slot_(slot) {}

QString XySlotView::title() const { return owner_.titleAt(slot_); }

QString XySlotView::kind() const { return owner_.kindAt(slot_); }

QString XySlotView::axisX() const { return owner_.axisXAt(slot_); }

QString XySlotView::axisY() const { return owner_.axisYAt(slot_); }

double XySlotView::x() const { return owner_.xAt(slot_); }

double XySlotView::y() const { return owner_.yAt(slot_); }

QString XySlotView::xValue() const { return owner_.valueTextAt(slot_, kAxisX); }

QString XySlotView::yValue() const { return owner_.valueTextAt(slot_, kAxisY); }

bool XySlotView::bound() const { return owner_.boundAt(slot_); }

QString XySlotView::note() const { return owner_.noteAt(slot_); }

XyViewModel::XyViewModel(MixerModel& model, MixerViewModel& mixer, QObject* parent)
    : QObject(parent),
      model_(model),
      mixer_(mixer),
      fx1_(*this, 0),
      fx2_(*this, 1) {
  connect(&mixer_, &MixerViewModel::fxChanged, this, &XyViewModel::refresh);
  connect(&mixer_, &MixerViewModel::selectedStripChanged, this,
          &XyViewModel::refresh);
}

int XyViewModel::track() const { return mixer_.selectedStrip(); }

QString XyViewModel::trackName() const {
  const QString name = QString::fromStdString(model_.trackName(track()));
  return name.isEmpty()
             ? QStringLiteral("IN %1").arg(track() + 1, 2, 10, QLatin1Char('0'))
             : name;
}

void XyViewModel::setTrack(int slot) {
  if (slot < 0 || slot >= audio::kMaxTracks) return;
  mixer_.setSelectedStrip(slot);
}

QString XyViewModel::titleAt(int grid) const {
  const QString slot = grid == 1 ? QStringLiteral("FX2") : QStringLiteral("FX1");
  const QString kind = kindAt(grid);
  return kind.isEmpty() || kind == QStringLiteral("NONE")
             ? slot
             : slot + QStringLiteral(" ") + kind;
}

QString XyViewModel::kindAt(int grid) const {
  const std::string effect = effectAt(grid);
  if (effect.empty()) return QStringLiteral("NONE");
  return QString::fromStdString(fxEffectName(effect));
}

QString XyViewModel::axisXAt(int grid) const {
  const std::string effect = effectAt(grid);
  if (effect == kFxIdDelay) return QStringLiteral("X CUTOFF");
  if (effect == kFxIdReverb) return QStringLiteral("X SIZE");
  return QStringLiteral("X");
}

QString XyViewModel::axisYAt(int grid) const {
  const std::string effect = effectAt(grid);
  if (effect == kFxIdDelay) return QStringLiteral("Y WIDTH");
  if (effect == kFxIdReverb) return QStringLiteral("Y LEVEL");
  return QStringLiteral("Y");
}

double XyViewModel::xAt(int grid) const {
  const std::string effect = effectAt(grid);
  const int param = fxGridXParam(effect);
  if (param < 0) return 0.5;
  return fxGridPosition(effect, param, paramAt(grid, param));
}

double XyViewModel::yAt(int grid) const {
  const std::string effect = effectAt(grid);
  const int param = fxGridYParam(effect);
  if (param < 0) return 0.5;
  return fxGridPosition(effect, param, paramAt(grid, param));
}

QString XyViewModel::valueTextAt(int grid, int axis) const {
  const std::string effect = effectAt(grid);
  const int param = axis == kAxisX ? fxGridXParam(effect) : fxGridYParam(effect);
  if (param < 0) return QStringLiteral("-");
  const auto table = fxParamTable(effect);
  if (param >= static_cast<int>(table.size())) return QStringLiteral("-");
  const auto& meta = table[static_cast<std::size_t>(param)];
  return QString::fromStdString(fxParamValueText(effect, param, paramAt(grid, param))) +
         QString::fromLatin1(meta.unit);
}

QString XyViewModel::noteAt(int grid) const {
  const std::string effect = effectAt(grid);
  if (effect.empty()) {
    return QStringLiteral("slot is NONE - pick DELAY or REVERB");
  }
  if (!fxIsBuiltin(effect)) {
    return QStringLiteral("slot is a CLAP plugin - edit its parameters in TRACK FX");
  }
  return QStringLiteral("series FX1 -> FX2 - per track");
}

bool XyViewModel::boundAt(int grid) const {
  const std::string effect = effectAt(grid);
  return effect == kFxIdDelay || effect == kFxIdReverb;
}

std::string XyViewModel::effectAt(int grid) const {
  if (grid < 0 || grid >= MixerModel::kFxSlotCount) return {};
  return model_.fxSlot(track(), grid).id;
}

double XyViewModel::paramAt(int grid, int param) const {
  const std::string effect = effectAt(grid);
  if (param < 0) return 0.0;
  const auto fx = model_.fxSlot(track(), grid);
  const auto table = fxParamTable(effect);
  if (param >= static_cast<int>(table.size())) return 0.0;
  if (param >= static_cast<int>(fx.params.size())) {
    return table[static_cast<std::size_t>(param)].def;
  }
  return fx.params[static_cast<std::size_t>(param)];
}

bool XyViewModel::writePoint(int grid, double x, double y) {
  const std::string effect = effectAt(grid);
  const int xParam = fxGridXParam(effect);
  const int yParam = fxGridYParam(effect);
  if (xParam < 0 || yParam < 0) return false;
  if (!std::isfinite(x) || !std::isfinite(y)) return false;
  // Through the mixer view model, so the slot's engine forward and the rack
  // change notification are the same path a knob edit takes.
  mixer_.setFxParamAt(track(), grid, xParam, fxGridValue(effect, xParam, x));
  mixer_.setFxParamAt(track(), grid, yParam, fxGridValue(effect, yParam, y));
  return true;
}

void XyViewModel::emitChanged() {
  fx1_.refresh();
  fx2_.refresh();
  emit changed();
  emit touchChanged();
}

void XyViewModel::refresh() { emitChanged(); }

QString XyViewModel::freezeText() const {
  return frozen_ ? QStringLiteral("FREEZE ON") : QStringLiteral("FREEZE");
}

QString XyViewModel::touchText() const {
  if (touchGrid_.isEmpty()) return QStringLiteral("NO TOUCH");
  int first = 0;
  int second = 0;
  for (auto it = touchGrid_.cbegin(); it != touchGrid_.cend(); ++it) {
    if (it.value() == 1) {
      ++second;
    } else {
      ++first;
    }
  }
  QStringList parts;
  if (first > 0) parts << QStringLiteral("FX1 x%1").arg(first);
  if (second > 0) parts << QStringLiteral("FX2 x%1").arg(second);
  return QStringLiteral("%1 TOUCH - %2")
      .arg(touchGrid_.size())
      .arg(parts.join(QStringLiteral(" + ")));
}

void XyViewModel::popupForGrid(int grid) {
  const QString label = grid == 1 ? QStringLiteral("FX2 X/Y") : QStringLiteral("FX1 X/Y");
  if (!boundAt(grid)) {
    emit textPopupRequested(label, noteAt(grid));
    return;
  }
  emit textPopupRequested(label, valueTextAt(grid, kAxisX) + QStringLiteral(" / ") +
                                    valueTextAt(grid, kAxisY));
}

void XyViewModel::touchBegin(int grid, int pointId, double x, double y) {
  touchGrid_.insert(pointId, grid);
  if (touchGrid_.size() > 1) {
    // Evidence line for the multitouch proof run (and a useful log in general).
    log("xy: " + touchText().toStdString());
  }
  if (!boundAt(grid) || (frozen_ && grid == 1)) {
    emitChanged();
    popupForGrid(grid);
    return;
  }
  if (writePoint(grid, x, y)) {
    emitChanged();
    popupForGrid(grid);
  }
}

void XyViewModel::touchMove(int grid, int pointId, double x, double y) {
  const auto it = touchGrid_.constFind(pointId);
  if (it == touchGrid_.cend()) {
    touchGrid_.insert(pointId, grid);
    emit touchChanged();
  }
  if (!boundAt(grid)) return;
  if (frozen_ && grid == 1) {
    emit textPopupRequested(QStringLiteral("FX2 X/Y"),
                            QStringLiteral("FROZEN - release to bend"));
    return;
  }
  if (writePoint(grid, x, y)) {
    emitChanged();
    popupForGrid(grid);
  }
}

void XyViewModel::touchEnd(int grid, int pointId) {
  Q_UNUSED(grid);
  if (touchGrid_.remove(pointId) == 0) return;
  emit touchChanged();
}

void XyViewModel::setPoint(int grid, double x, double y) {
  if (!boundAt(grid)) {
    emit textPopupRequested(grid == 1 ? QStringLiteral("FX2 X/Y")
                                      : QStringLiteral("FX1 X/Y"),
                            noteAt(grid));
    return;
  }
  if (frozen_ && grid == 1) {
    emit textPopupRequested(QStringLiteral("FX2 X/Y"),
                            QStringLiteral("FROZEN - release to bend"));
    return;
  }
  if (!writePoint(grid, x, y)) return;
  emitChanged();
  popupForGrid(grid);
}

void XyViewModel::nudge(int grid, int dx, int dy, bool coarse) {
  if (!boundAt(grid)) {
    emit textPopupRequested(grid == 1 ? QStringLiteral("FX2 X/Y")
                                      : QStringLiteral("FX1 X/Y"),
                            noteAt(grid));
    return;
  }
  if (frozen_ && grid == 1) {
    emit textPopupRequested(QStringLiteral("FX2 X/Y"),
                            QStringLiteral("FROZEN - release to bend"));
    return;
  }
  const double step = coarse ? kCoarseStep : kTouchStep;
  const double nextX = xAt(grid) + dx * step;
  const double nextY = yAt(grid) + dy * step;
  if (!writePoint(grid, nextX, nextY)) return;
  emitChanged();
  popupForGrid(grid);
}

void XyViewModel::toggleFreeze() {
  if (frozen_) {
    // Release: restore exactly the parameters the slot held when freeze latched.
    for (const auto& saved : frozenSlots_) {
      for (std::size_t param = 0; param < saved.params.size(); ++param) {
        mixer_.setFxParamAt(track(), saved.slot, static_cast<int>(param),
                            saved.params[param]);
      }
    }
    frozenSlots_.clear();
    frozen_ = false;
    emitChanged();
    emit textPopupRequested(QStringLiteral("REVERB"), freezeText());
    return;
  }
  frozenSlots_.clear();
  for (int grid = 0; grid < MixerModel::kFxSlotCount; ++grid) {
    const std::string effect = effectAt(grid);
    if (effect != kFxIdReverb) continue;
    FrozenSlot saved;
    saved.slot = grid;
    saved.params = model_.fxSlot(track(), grid).params;
    frozenSlots_.push_back(std::move(saved));
    // The shipped reverb has no freeze parameter, so freeze is the documented
    // macro: longest decay, no damping, largest size, fully wet.
    mixer_.setFxParamAt(track(), grid, audio::fx::Reverb::kDecay, 0.98);
    mixer_.setFxParamAt(track(), grid, audio::fx::Reverb::kDamping, 0.0);
    mixer_.setFxParamAt(track(), grid, audio::fx::Reverb::kSize, 2.0);
    mixer_.setFxParamAt(track(), grid, audio::fx::Reverb::kMix, 1.0);
  }
  if (frozenSlots_.empty()) {
    // No reverb slot on this track: freeze is the reverb macro, so name the gap
    // instead of latching a state nothing honours.
    emit textPopupRequested(QStringLiteral("FREEZE"),
                            QStringLiteral("no REVERB slot on this track"));
    return;
  }
  frozen_ = true;
  emitChanged();
  emit textPopupRequested(QStringLiteral("REVERB"), freezeText());
}

void XyViewModel::centerGrid(int grid) { setPoint(grid, 0.5, 0.5); }

void XyViewModel::showGridPopup(int grid) { popupForGrid(grid); }

}  // namespace mnky::app
