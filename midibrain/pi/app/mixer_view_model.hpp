#pragma once
// QML model for the banked mixer: 18 strips plus a pinned master. Values live
// in MixerModel's atomics; this model only formats them for QML and forwards
// edits. Bank 0 shows strips 1-9, bank 1 shows 10-18.
#include <QAbstractListModel>
#include <QObject>
#include <QString>
#include <QVariant>

#include "mixer_model.hpp"

namespace mnky::app {

inline constexpr int kStripsPerBank = 9;

class MixerViewModel final : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(int bank READ bank WRITE setBank NOTIFY bankChanged)
  Q_PROPERTY(int stripsPerBank READ stripsPerBank CONSTANT)
  Q_PROPERTY(int selectedStrip READ selectedStrip WRITE setSelectedStrip NOTIFY selectedStripChanged)
  Q_PROPERTY(double masterDb READ masterDb WRITE setMasterDb NOTIFY masterChanged)
  Q_PROPERTY(double meterLeft READ meterLeft NOTIFY metersChanged)
  Q_PROPERTY(double meterRight READ meterRight NOTIFY metersChanged)

 public:
  enum Roles {
    IndexRole = Qt::UserRole + 1,
    NameRole,
    GainDbRole,
    PanRole,
    PanTextRole,
    SendARole,
    SendBRole,
    MuteRole,
    SoloRole,
    ArmRole,
    SelectedRole,
  };

  explicit MixerViewModel(MixerModel& model, QObject* parent = nullptr);

  int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

  int bank() const { return bank_; }
  Q_INVOKABLE void setBank(int bank);
  int stripsPerBank() const { return kStripsPerBank; }
  int selectedStrip() const { return selectedStrip_; }
  void setSelectedStrip(int index);
  double masterDb() const { return model_.masterDb(); }
  Q_INVOKABLE void setMasterDb(double db);
  double meterLeft() const { return meterLeft_; }
  double meterRight() const { return meterRight_; }

  // QML writes and panel-focus adjustments.
  Q_INVOKABLE void setGainDb(int index, double db);
  Q_INVOKABLE void setPan(int index, double pan);
  Q_INVOKABLE void setSend(int index, int send, double level);
  Q_INVOKABLE void toggleMute(int index);
  Q_INVOKABLE void toggleSolo(int index);
  Q_INVOKABLE void toggleArm(int index);
  Q_INVOKABLE void adjust(int index, const QString& parameter, double delta);

  // Called by the controller timer after reading the audio peaks.
  void setMeters(double left, double right);
  // Re-reads every value (project load, MIDI learn writes).
  void refresh();

 signals:
  void bankChanged();
  void selectedStripChanged();
  void masterChanged();
  void metersChanged();

 private:
  MixerModel& model_;
  int bank_ = 0;
  int selectedStrip_ = 0;
  double meterLeft_ = 0.0;
  double meterRight_ = 0.0;
};

}  // namespace mnky::app
