#pragma once
// QML bridge for the EQ grid screen: node positions, the summed response curve,
// the selected band's Gain/Q/Frequency/Type corners, the track+Main subject
// selector and the panel EDIT entry points. Every value comes from EqModel;
// this class only formats and forwards, so the screen cannot invent a range.
#include <QObject>
#include <QString>
#include <QVariantList>

#include "eq_model.hpp"

namespace mnky::app {

class EqViewModel final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantList nodes READ nodes NOTIFY changed)
  Q_PROPERTY(QVariantList curve READ curve NOTIFY curveChanged)
  Q_PROPERTY(int subject READ subject NOTIFY changed)
  // Index of the Main-bus subject in the band store (EqModel::kMainSubject):
  // QML compares against this instead of a literal track count.
  Q_PROPERTY(int mainSubject READ mainSubject CONSTANT)
  Q_PROPERTY(int band READ band NOTIFY changed)
  Q_PROPERTY(QString subjectText READ subjectText NOTIFY changed)
  Q_PROPERTY(QString bandText READ bandText NOTIFY changed)
  Q_PROPERTY(QString typeText READ typeText NOTIFY changed)
  Q_PROPERTY(QString freqText READ freqText NOTIFY changed)
  Q_PROPERTY(QString gainText READ gainText NOTIFY changed)
  Q_PROPERTY(QString qText READ qText NOTIFY changed)
  Q_PROPERTY(bool cutBand READ cutBand NOTIFY changed)
  Q_PROPERTY(QString summaryText READ summaryText NOTIFY changed)

 public:
  explicit EqViewModel(EqModel& model, QObject* parent = nullptr);

  QVariantList nodes() const;
  QVariantList curve() const;
  int subject() const { return model_.subject(); }
  int mainSubject() const { return EqModel::kMainSubject; }
  int band() const { return model_.band(); }
  QString subjectText() const;
  QString bandText() const;
  QString typeText() const;
  QString freqText() const;
  QString gainText() const;
  QString qText() const;
  bool cutBand() const;
  QString summaryText() const;

  // Touch / QML actions.
  Q_INVOKABLE void selectSubject(int subject);
  Q_INVOKABLE void stepSubject(int delta);
  Q_INVOKABLE void selectMain();
  Q_INVOKABLE void selectBand(int band);
  Q_INVOKABLE void stepBand(int delta);
  Q_INVOKABLE int nodeAt(double x, double y) const;
  Q_INVOKABLE void dragBand(int band, double x, double y);
  Q_INVOKABLE void cycleType(int delta);
  Q_INVOKABLE void selectType(int type);
  Q_INVOKABLE void showBandPopup(int band);

  // Panel EDIT actions (one NAV event or encoder detent each).
  Q_INVOKABLE void nudgeSelected(int dx, int dy, bool coarse);
  Q_INVOKABLE void adjustFreq(double octaves);
  Q_INVOKABLE void adjustGain(double deltaDb);
  Q_INVOKABLE void adjustQ(double delta);

  // ConfigPopup "eqtype" draft hand-off.
  int draftType() const { return model_.type(model_.band()); }
  void applyDraftType(int type);

 signals:
  void changed();
  void curveChanged();
  void valuePopupRequested(const QString& label, const QString& kind, double value);
  void textPopupRequested(const QString& label, const QString& text);

 private:
  void emitChanged();
  void popupForBand(int band);

  EqModel& model_;
};

}  // namespace mnky::app
