#pragma once
// QML view models for the two CLAP send slots: plugin identity, parameters,
// bypass, return level and state files. The GUI thread owns all plugin calls;
// the audio thread only enters FxSlot's try-locked adapter.
#include <QAbstractListModel>
#include <QObject>
#include <QString>

#include <vector>

#include "clap/host.hpp"
#include "fx_slot.hpp"
#include "mixer_model.hpp"

namespace mnky::app {

class AppState;

class FxParamModel final : public QAbstractListModel {
  Q_OBJECT
 public:
  enum Roles {
    NameRole = Qt::UserRole + 1,
    ValueRole,
    MinimumRole,
    MaximumRole,
  };

  explicit FxParamModel(FxSlot& slot, QObject* parent = nullptr);

  int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

  void rebuild();
  void refresh();

 private:
  FxSlot& slot_;
  std::vector<clap::Param> params_;
};

class FxSlotViewModel final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString title READ title CONSTANT)
  Q_PROPERTY(QString pluginName READ pluginName NOTIFY pluginChanged)
  Q_PROPERTY(QString pluginPath READ pluginPath NOTIFY pluginChanged)
  Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
  Q_PROPERTY(QString statePath READ statePath CONSTANT)
  Q_PROPERTY(bool loaded READ loaded NOTIFY pluginChanged)
  Q_PROPERTY(bool bypassed READ bypassed WRITE setBypassed NOTIFY bypassedChanged)
  Q_PROPERTY(double returnDb READ returnDb WRITE setReturnDb NOTIFY returnChanged)
  Q_PROPERTY(FxParamModel* params READ params CONSTANT)

 public:
  FxSlotViewModel(FxSlot& slot, int index, AppState& state, MixerModel& mixer,
                  QObject* parent = nullptr);

  QString title() const { return title_; }
  QString pluginName() const { return pluginName_; }
  QString pluginPath() const { return pluginPath_; }
  QString statusText() const { return statusText_; }
  QString statePath() const { return statePath_; }
  bool loaded() const { return loaded_; }
  bool bypassed() const { return bypassed_; }
  void setBypassed(bool bypassed);
  double returnDb() const { return returnDb_; }
  void setReturnDb(double db);
  FxParamModel* params() { return &params_; }

  Q_INVOKABLE void loadDefault();
  Q_INVOKABLE void eject();
  Q_INVOKABLE void saveState();
  Q_INVOKABLE void loadState();
  Q_INVOKABLE void setParam(int index, double value);
  Q_INVOKABLE void adjustParam(int index, double delta);
  Q_INVOKABLE void toggleBypass();

  void refresh();

 signals:
  void pluginChanged();
  void statusChanged();
  void bypassedChanged();
  void returnChanged();

 private:
  void rebuildParams();
  void updateStatus();

  FxSlot& slot_;
  const int index_;
  AppState& state_;
  MixerModel& mixer_;
  FxParamModel params_;
  QString title_;
  QString pluginName_;
  QString pluginPath_;
  QString statusText_;
  QString statePath_;
  bool loaded_ = false;
  bool bypassed_ = false;
  double returnDb_ = 0.0;
};

class FxViewModel final : public QObject {
  Q_OBJECT
  Q_PROPERTY(FxSlotViewModel* slotA READ slotA CONSTANT)
  Q_PROPERTY(FxSlotViewModel* slotB READ slotB CONSTANT)

 public:
  FxViewModel(FxManager& fx, AppState& state, MixerModel& mixer, double sampleRate,
              QObject* parent = nullptr);

  FxSlotViewModel* slotA() { return &slotA_; }
  FxSlotViewModel* slotB() { return &slotB_; }
  void loadDefaults();
  void refresh();

 private:
  FxSlotViewModel slotA_;
  FxSlotViewModel slotB_;
};

}  // namespace mnky::app
