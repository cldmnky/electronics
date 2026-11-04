#pragma once
// QML bridge for the X/Y performance screen: the two grid points, their mapped
// readouts, the reverb freeze macro and the multitouch bookkeeping.
//
// Touch model: every QML MultiPointTouchArea forwards its TouchPoints here with
// their Qt point id, so two fingers on the two grids are tracked at once and
// the screen can show how many points are down on which grid. The panel path
// (NAV directions in EDIT) calls the same nudge() the touch drag uses, so both
// inputs share one mapping.
#include <QHash>
#include <QObject>
#include <QString>

#include "performance_model.hpp"

namespace mnky::app {

class XyViewModel final : public QObject {
  Q_OBJECT
  Q_PROPERTY(double delayX READ delayX NOTIFY changed)
  Q_PROPERTY(double delayY READ delayY NOTIFY changed)
  Q_PROPERTY(double reverbX READ reverbX NOTIFY changed)
  Q_PROPERTY(double reverbY READ reverbY NOTIFY changed)
  Q_PROPERTY(QString delayXText READ delayXText NOTIFY changed)
  Q_PROPERTY(QString delayYText READ delayYText NOTIFY changed)
  Q_PROPERTY(QString reverbXText READ reverbXText NOTIFY changed)
  Q_PROPERTY(QString reverbYText READ reverbYText NOTIFY changed)
  Q_PROPERTY(bool frozen READ frozen NOTIFY changed)
  Q_PROPERTY(QString freezeText READ freezeText NOTIFY changed)
  Q_PROPERTY(QString touchText READ touchText NOTIFY touchChanged)
  Q_PROPERTY(int touchCount READ touchCount NOTIFY touchChanged)

 public:
  explicit XyViewModel(PerformanceModel& model, QObject* parent = nullptr);

  double delayX() const { return model_.x(PerformanceModel::kDelayGrid); }
  double delayY() const { return model_.y(PerformanceModel::kDelayGrid); }
  double reverbX() const { return model_.x(PerformanceModel::kReverbGrid); }
  double reverbY() const { return model_.y(PerformanceModel::kReverbGrid); }
  QString delayXText() const;
  QString delayYText() const;
  QString reverbXText() const;
  QString reverbYText() const;
  bool frozen() const { return model_.frozen(); }
  QString freezeText() const;
  QString touchText() const;
  int touchCount() const { return touchGrid_.size(); }

  // TouchPoint path. `pointId` is the Qt TouchPoint id (unique per window), so
  // one finger per grid is two entries in one map.
  Q_INVOKABLE void touchBegin(int grid, int pointId, double x, double y);
  Q_INVOKABLE void touchMove(int grid, int pointId, double x, double y);
  Q_INVOKABLE void touchEnd(int grid, int pointId);
  // Tap-to-place and the panel path share this.
  Q_INVOKABLE void setPoint(int grid, double x, double y);
  Q_INVOKABLE void nudge(int grid, int dx, int dy, bool coarse);
  Q_INVOKABLE void toggleFreeze();
  Q_INVOKABLE void centerGrid(int grid);
  Q_INVOKABLE void showGridPopup(int grid);

 signals:
  void changed();
  void touchChanged();
  void textPopupRequested(const QString& label, const QString& text);

 private:
  void emitChanged();
  void popupForGrid(int grid);

  PerformanceModel& model_;
  QHash<int, int> touchGrid_;  // Qt touch point id -> grid
};

}  // namespace mnky::app
