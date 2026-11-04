#include "xy_view_model.hpp"

#include <QStringList>

#include "logging.hpp"

namespace mnky::app {

XyViewModel::XyViewModel(PerformanceModel& model, QObject* parent)
    : QObject(parent), model_(model) {}

QString XyViewModel::delayXText() const {
  return QString::fromStdString(model_.xText(PerformanceModel::kDelayGrid));
}

QString XyViewModel::delayYText() const {
  return QString::fromStdString(model_.yText(PerformanceModel::kDelayGrid));
}

QString XyViewModel::reverbXText() const {
  return QString::fromStdString(model_.xText(PerformanceModel::kReverbGrid));
}

QString XyViewModel::reverbYText() const {
  return QString::fromStdString(model_.yText(PerformanceModel::kReverbGrid));
}

QString XyViewModel::freezeText() const {
  return model_.frozen() ? QStringLiteral("FREEZE ON") : QStringLiteral("FREEZE");
}

QString XyViewModel::touchText() const {
  if (touchGrid_.isEmpty()) return QStringLiteral("NO TOUCH");
  int delay = 0;
  int reverb = 0;
  for (auto it = touchGrid_.cbegin(); it != touchGrid_.cend(); ++it) {
    if (it.value() == PerformanceModel::kDelayGrid) {
      ++delay;
    } else {
      ++reverb;
    }
  }
  QStringList parts;
  if (delay > 0) parts << QStringLiteral("DELAY x%1").arg(delay);
  if (reverb > 0) parts << QStringLiteral("REVERB x%1").arg(reverb);
  return QStringLiteral("%1 TOUCH - %2")
      .arg(touchGrid_.size())
      .arg(parts.join(QStringLiteral(" + ")));
}

void XyViewModel::emitChanged() {
  emit changed();
  emit touchChanged();
}

void XyViewModel::popupForGrid(int grid) {
  const QString label = grid == PerformanceModel::kDelayGrid
                            ? QStringLiteral("DELAY X/Y")
                            : QStringLiteral("REVERB X/Y");
  const QString text = QString::fromStdString(model_.xText(grid)) +
                       QStringLiteral(" / ") +
                       QString::fromStdString(model_.yText(grid));
  emit textPopupRequested(label, text);
}

void XyViewModel::touchBegin(int grid, int pointId, double x, double y) {
  touchGrid_.insert(pointId, grid);
  const bool moved = model_.setPoint(grid, x, y);
  if (touchGrid_.size() > 1) {
    // Evidence line for the multitouch proof run (and a useful log in general).
    log("xy: " + touchText().toStdString());
  }
  emitChanged();
  if (moved) popupForGrid(grid);
}

void XyViewModel::touchMove(int grid, int pointId, double x, double y) {
  const auto it = touchGrid_.constFind(pointId);
  if (it == touchGrid_.cend()) {
    touchGrid_.insert(pointId, grid);
    emit touchChanged();
  }
  if (model_.setPoint(grid, x, y)) {
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
  if (!model_.setPoint(grid, x, y)) {
    if (model_.frozen() && grid == PerformanceModel::kReverbGrid) {
      emit textPopupRequested(QStringLiteral("REVERB X/Y"),
                              QStringLiteral("FROZEN - release to bend"));
    }
    return;
  }
  emitChanged();
  popupForGrid(grid);
}

void XyViewModel::nudge(int grid, int dx, int dy, bool coarse) {
  if (!model_.nudge(grid, dx, dy, coarse)) {
    if (model_.frozen() && grid == PerformanceModel::kReverbGrid) {
      emit textPopupRequested(QStringLiteral("REVERB X/Y"),
                              QStringLiteral("FROZEN - release to bend"));
    }
    return;
  }
  emitChanged();
  popupForGrid(grid);
}

void XyViewModel::toggleFreeze() {
  model_.toggleFrozen();
  emitChanged();
  emit textPopupRequested(QStringLiteral("REVERB"), freezeText());
}

void XyViewModel::centerGrid(int grid) {
  setPoint(grid, 0.5, 0.5);
}

void XyViewModel::showGridPopup(int grid) { popupForGrid(grid); }

}  // namespace mnky::app
