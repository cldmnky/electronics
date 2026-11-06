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
#include "performance_model.hpp"

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

// Built-in send effects (the two X/Y performance instances) as a flat QML
// model: every Delay and Reverb parameter is a property, so the FX tab can show
// their FULL settings beside the CLAP slots instead of only the X/Y grid. The
// values live in PerformanceModel (the same instance the audio bind bus runs),
// so a knob here and an X/Y grid move are the same parameter.
class BuiltinFxViewModel final : public QObject {
  Q_OBJECT
  Q_PROPERTY(double delayTimeMs READ delayTimeMs WRITE setDelayTimeMs NOTIFY changed)
  Q_PROPERTY(double delayFeedback READ delayFeedback WRITE setDelayFeedback NOTIFY changed)
  Q_PROPERTY(double delayMix READ delayMix WRITE setDelayMix NOTIFY changed)
  Q_PROPERTY(bool delayBeat READ delayBeat WRITE setDelayBeat NOTIFY changed)
  Q_PROPERTY(int delayDivision READ delayDivision WRITE setDelayDivision NOTIFY changed)
  Q_PROPERTY(QString delayDivisionText READ delayDivisionText NOTIFY changed)
  Q_PROPERTY(bool delayPingPong READ delayPingPong WRITE setDelayPingPong NOTIFY changed)
  Q_PROPERTY(double delayCutoffHz READ delayCutoffHz WRITE setDelayCutoffHz NOTIFY changed)
  Q_PROPERTY(double delayWidthOct READ delayWidthOct WRITE setDelayWidthOct NOTIFY changed)
  Q_PROPERTY(double reverbPredelayMs READ reverbPredelayMs WRITE setReverbPredelayMs NOTIFY changed)
  Q_PROPERTY(double reverbDecay READ reverbDecay WRITE setReverbDecay NOTIFY changed)
  Q_PROPERTY(double reverbDamping READ reverbDamping WRITE setReverbDamping NOTIFY changed)
  Q_PROPERTY(double reverbMix READ reverbMix WRITE setReverbMix NOTIFY changed)
  Q_PROPERTY(int reverbModel READ reverbModel WRITE setReverbModel NOTIFY changed)
  Q_PROPERTY(QString reverbModelText READ reverbModelText NOTIFY changed)
  Q_PROPERTY(double reverbSize READ reverbSize WRITE setReverbSize NOTIFY changed)

 public:
  explicit BuiltinFxViewModel(PerformanceModel& performance, QObject* parent = nullptr);

  double delayTimeMs() const;
  void setDelayTimeMs(double value);
  double delayFeedback() const;
  void setDelayFeedback(double value);
  double delayMix() const;
  void setDelayMix(double value);
  bool delayBeat() const;
  void setDelayBeat(bool value);
  int delayDivision() const;
  void setDelayDivision(int value);
  QString delayDivisionText() const;
  bool delayPingPong() const;
  void setDelayPingPong(bool value);
  double delayCutoffHz() const;
  void setDelayCutoffHz(double value);
  double delayWidthOct() const;
  void setDelayWidthOct(double value);
  double reverbPredelayMs() const;
  void setReverbPredelayMs(double value);
  double reverbDecay() const;
  void setReverbDecay(double value);
  double reverbDamping() const;
  void setReverbDamping(double value);
  double reverbMix() const;
  void setReverbMix(double value);
  int reverbModel() const;
  void setReverbModel(int value);
  QString reverbModelText() const;
  double reverbSize() const;
  void setReverbSize(double value);

  Q_INVOKABLE void cycleDelayDivision(int delta);
  Q_INVOKABLE void cycleReverbModel(int delta);
  // Re-reads every parameter (project load, X/Y moves).
  void refresh();

 signals:
  void changed();

 private:
  double param(int grid, int index) const;
  void setParamValue(int grid, int index, double value);

  PerformanceModel& performance_;
};

class FxViewModel final : public QObject {
  Q_OBJECT
  Q_PROPERTY(FxSlotViewModel* slotA READ slotA CONSTANT)
  Q_PROPERTY(FxSlotViewModel* slotB READ slotB CONSTANT)
  Q_PROPERTY(BuiltinFxViewModel* builtin READ builtin CONSTANT)

 public:
  FxViewModel(FxManager& fx, AppState& state, MixerModel& mixer,
              PerformanceModel& performance, double sampleRate,
              QObject* parent = nullptr);

  FxSlotViewModel* slotA() { return &slotA_; }
  FxSlotViewModel* slotB() { return &slotB_; }
  BuiltinFxViewModel* builtin() { return &builtin_; }
  void loadDefaults();
  void refresh();

 private:
  FxSlotViewModel slotA_;
  FxSlotViewModel slotB_;
  BuiltinFxViewModel builtin_;
};

}  // namespace mnky::app
