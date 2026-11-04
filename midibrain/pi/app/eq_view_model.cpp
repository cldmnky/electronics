#include "eq_view_model.hpp"

#include <QVariantMap>

namespace mnky::app {

EqViewModel::EqViewModel(EqModel& model, QObject* parent)
    : QObject(parent), model_(model) {}

QVariantList EqViewModel::nodes() const {
  QVariantList list;
  for (int band = 0; band < EqModel::kBandCount; ++band) {
    QVariantMap node;
    node.insert(QStringLiteral("x"), model_.bandX(band));
    node.insert(QStringLiteral("y"), model_.bandY(band));
    node.insert(QStringLiteral("type"), model_.type(band));
    node.insert(QStringLiteral("name"), QString::number(band + 1));
    node.insert(QStringLiteral("selected"), band == model_.band());
    list.append(node);
  }
  return list;
}

QVariantList EqViewModel::curve() const {
  // The magnitude response, mapped onto the same +/-24 dB grid the nodes use.
  QVariantList points;
  const auto& response = model_.curve();
  points.reserve(static_cast<int>(response.size()));
  for (const float db : response) {
    points.append(EqModel::gainToY(static_cast<double>(db)));
  }
  return points;
}

QString EqViewModel::subjectText() const {
  return QString::fromStdString(EqModel::subjectName(model_.subject()));
}

QString EqViewModel::bandText() const {
  return QStringLiteral("BAND ") + QString::number(model_.band() + 1) +
         QStringLiteral(" ") + typeText();
}

QString EqViewModel::typeText() const {
  return QString::fromStdString(EqModel::typeShortName(model_.type(model_.band())));
}

QString EqViewModel::freqText() const {
  return QString::fromStdString(model_.freqText(model_.band()));
}

QString EqViewModel::gainText() const {
  return QString::fromStdString(model_.gainText(model_.band()));
}

QString EqViewModel::qText() const {
  return QString::fromStdString(model_.qText(model_.band()));
}

bool EqViewModel::cutBand() const {
  return !EqModel::typeHasGain(model_.type(model_.band()));
}

QString EqViewModel::summaryText() const {
  return subjectText() + QStringLiteral(" - ") + bandText() + QStringLiteral(" - ") +
         freqText() +
         (cutBand() ? QString() : QStringLiteral(" - ") + gainText()) +
         QStringLiteral(" - ") + qText();
}

void EqViewModel::emitChanged() {
  emit changed();
  emit curveChanged();
}

void EqViewModel::popupForBand(int band) {
  const QString label = QStringLiteral("EQ ") + subjectText() + QStringLiteral(" ") +
                        QStringLiteral("BAND ") + QString::number(band + 1);
  QString text = typeText();
  text += QStringLiteral(" - ") + QString::fromStdString(model_.freqText(band));
  if (EqModel::typeHasGain(model_.type(band))) {
    text += QStringLiteral(" - ") + QString::fromStdString(model_.gainText(band));
  }
  emit textPopupRequested(label, text);
}

void EqViewModel::selectSubject(int subject) {
  model_.setSubject(subject);
  emitChanged();
  emit textPopupRequested(QStringLiteral("EQ SUBJECT"), subjectText());
}

void EqViewModel::stepSubject(int delta) {
  if (delta == 0) return;
  model_.stepSubject(delta);
  emitChanged();
  emit textPopupRequested(QStringLiteral("EQ SUBJECT"), subjectText());
}

void EqViewModel::selectMain() {
  model_.setSubject(EqModel::kMainSubject);
  emitChanged();
  emit textPopupRequested(QStringLiteral("EQ SUBJECT"), subjectText());
}

void EqViewModel::selectBand(int band) {
  model_.setBand(band);
  emitChanged();
  popupForBand(model_.band());
}

void EqViewModel::stepBand(int delta) {
  if (delta == 0) return;
  model_.stepBand(delta);
  emitChanged();
  popupForBand(model_.band());
}

int EqViewModel::nodeAt(double x, double y) const {
  return model_.nodeAt(x, y);
}

void EqViewModel::dragBand(int band, double x, double y) {
  model_.dragBand(band, x, y);
  model_.setBand(band);
  emitChanged();
  popupForBand(band);
}

void EqViewModel::cycleType(int delta) {
  model_.cycleType(model_.band(), delta);
  emitChanged();
  popupForBand(model_.band());
}

void EqViewModel::selectType(int type) {
  model_.setType(model_.band(), type);
  emitChanged();
  popupForBand(model_.band());
}

void EqViewModel::applyDraftType(int type) { selectType(type); }

void EqViewModel::showBandPopup(int band) { popupForBand(band); }

void EqViewModel::nudgeSelected(int dx, int dy, bool coarse) {
  const int band = model_.band();
  model_.nudgeBand(band, dx, dy, coarse);
  emitChanged();
  const QString label = QStringLiteral("BAND %1").arg(band + 1);
  if (dx != 0) {
    emit valuePopupRequested(label + QStringLiteral(" FREQ"), QStringLiteral("hz"),
                             model_.freqHz(band));
  }
  if (dy != 0) {
    if (EqModel::typeHasGain(model_.type(band))) {
      emit valuePopupRequested(label + QStringLiteral(" GAIN"), QStringLiteral("db"),
                               model_.gainDb(band));
    } else {
      emit valuePopupRequested(label, QStringLiteral("q"), model_.q(band));
    }
  }
}

void EqViewModel::adjustFreq(double octaves) {
  const int band = model_.band();
  model_.adjustFreqOctaves(band, octaves);
  emitChanged();
  emit valuePopupRequested(QStringLiteral("BAND %1 FREQ").arg(band + 1),
                           QStringLiteral("hz"), model_.freqHz(band));
}

void EqViewModel::adjustGain(double deltaDb) {
  const int band = model_.band();
  model_.adjustGainDb(band, deltaDb);
  emitChanged();
  emit valuePopupRequested(QStringLiteral("BAND %1 GAIN").arg(band + 1),
                           QStringLiteral("db"), model_.gainDb(band));
}

void EqViewModel::adjustQ(double delta) {
  const int band = model_.band();
  model_.adjustQ(band, delta);
  emitChanged();
  emit valuePopupRequested(QStringLiteral("BAND %1").arg(band + 1),
                           QStringLiteral("q"), model_.q(band));
}

}  // namespace mnky::app
