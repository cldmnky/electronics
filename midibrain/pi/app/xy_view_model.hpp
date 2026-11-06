#pragma once
// QML bridge for the X/Y performance screen, now per track: grid 0 bends the
// selected track's FX1 slot and grid 1 its FX2 slot. The mapping is the
// effects' own documented X/Y pair (USER-MANUAL section 10): delay X = feedback
// filter cutoff, Y = width; reverb X = size, Y = level. The grid writes the
// slot's positional parameters through MixerModel, which forwards that slot to
// the engine on the control thread, so a grid move is what the strip runs.
//
// A slot that holds nothing (or a plugin id) has no grid: the screen shows the
// slot picker chip and this model's note instead of a position.
//
// Touch model: every QML MultiPointTouchArea forwards its TouchPoints here with
// its Qt point id, so two fingers on the two grids are tracked at once and the
// screen can show how many points are down on which grid. The panel path (NAV
// directions in EDIT) calls the same nudge() the touch drag uses, so both
// inputs share one mapping.
#include <QHash>
#include <QObject>
#include <QString>

#include <vector>

#include "fx_params.hpp"
#include "mixer_model.hpp"
#include "mixer_view_model.hpp"

namespace mnky::app {

class XyViewModel;

// One grid = one FX slot of the selected track.
class XySlotView final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString title READ title CONSTANT)
  Q_PROPERTY(QString kind READ kind NOTIFY changed)
  Q_PROPERTY(QString axisX READ axisX NOTIFY changed)
  Q_PROPERTY(QString axisY READ axisY NOTIFY changed)
  Q_PROPERTY(double x READ x NOTIFY changed)
  Q_PROPERTY(double y READ y NOTIFY changed)
  Q_PROPERTY(QString xValue READ xValue NOTIFY changed)
  Q_PROPERTY(QString yValue READ yValue NOTIFY changed)
  Q_PROPERTY(bool bound READ bound NOTIFY changed)
  Q_PROPERTY(QString note READ note NOTIFY changed)

 public:
  XySlotView(XyViewModel& owner, int slot);

  QString title() const;
  QString kind() const;
  QString axisX() const;
  QString axisY() const;
  double x() const;
  double y() const;
  QString xValue() const;
  QString yValue() const;
  bool bound() const;
  QString note() const;
  int slot() const { return slot_; }
  void refresh() { emit changed(); }

 signals:
  void changed();

 private:
  XyViewModel& owner_;
  const int slot_;
};

class XyViewModel final : public QObject {
  Q_OBJECT
  Q_PROPERTY(int track READ track NOTIFY trackChanged)
  Q_PROPERTY(QString trackName READ trackName NOTIFY trackChanged)
  Q_PROPERTY(bool frozen READ frozen NOTIFY changed)
  Q_PROPERTY(QString freezeText READ freezeText NOTIFY changed)
  Q_PROPERTY(QString touchText READ touchText NOTIFY touchChanged)
  Q_PROPERTY(int touchCount READ touchCount NOTIFY touchChanged)
  Q_PROPERTY(XySlotView* fx1 READ fx1 CONSTANT)
  Q_PROPERTY(XySlotView* fx2 READ fx2 CONSTANT)

 public:
  // Touch and panel steps on the normalized 0..1 axes.
  static constexpr double kTouchStep = 0.02;
  static constexpr double kCoarseStep = 0.1;

  XyViewModel(MixerModel& model, MixerViewModel& mixer, QObject* parent = nullptr);

  int track() const;
  QString trackName() const;
  bool frozen() const { return frozen_; }
  QString freezeText() const;
  QString touchText() const;
  int touchCount() const { return touchGrid_.size(); }
  XySlotView* fx1() { return &fx1_; }
  XySlotView* fx2() { return &fx2_; }

  // The selected track is the mixer's selected strip: the X/Y track row and a
  // strip tap select the same track.
  void setTrack(int slot);
  // `grid` is the FX slot: 0 = FX1, 1 = FX2.
  Q_INVOKABLE void touchBegin(int grid, int pointId, double x, double y);
  Q_INVOKABLE void touchMove(int grid, int pointId, double x, double y);
  Q_INVOKABLE void touchEnd(int grid, int pointId);
  Q_INVOKABLE void setPoint(int grid, double x, double y);
  Q_INVOKABLE void nudge(int grid, int dx, int dy, bool coarse);
  Q_INVOKABLE void toggleFreeze();
  Q_INVOKABLE void centerGrid(int grid);
  Q_INVOKABLE void showGridPopup(int grid);
  // Re-reads both grids (rack edits, track switch, session load).
  void refresh();

  // Grid reads for the slot views.
  QString kindAt(int grid) const;
  QString axisXAt(int grid) const;
  QString axisYAt(int grid) const;
  double xAt(int grid) const;
  double yAt(int grid) const;
  QString valueTextAt(int grid, int axis) const;
  QString noteAt(int grid) const;
  bool boundAt(int grid) const;
  QString titleAt(int grid) const;

 signals:
  void changed();
  void touchChanged();
  void trackChanged();
  void textPopupRequested(const QString& label, const QString& text);

 private:
  void emitChanged();
  void popupForGrid(int grid);
  // The slot's effect id at `grid` ("" when the grid index is out of range).
  std::string effectAt(int grid) const;
  double paramAt(int grid, int param) const;
  bool writePoint(int grid, double x, double y);
  // Every reverb-type slot of the selected track, saved so a freeze release
  // restores exactly what the grid held.
  struct FrozenSlot {
    int slot = 0;
    std::vector<double> params;
  };

  MixerModel& model_;
  MixerViewModel& mixer_;
  XySlotView fx1_;
  XySlotView fx2_;
  QHash<int, int> touchGrid_;  // Qt touch point id -> grid
  bool frozen_ = false;
  std::vector<FrozenSlot> frozenSlots_;
};

}  // namespace mnky::app
