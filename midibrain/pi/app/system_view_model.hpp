#pragma once
// QML view model for the System screen: link/clock/READY diagnostics, the Qt
// Multimedia backend/device selector, metronome configuration and the in-app
// log tail. The selector is the only place to switch output/input device and
// the requested period size; Apply restarts audio through AudioGlue.
#include <QObject>
#include <QString>
#include <QStringList>

#include <vector>

#include "app_state.hpp"
#include "audio_glue.hpp"
#include "fx_slot.hpp"
#include "link_client.hpp"
#include "midi_glue.hpp"

namespace mnky::app {

class SystemViewModel final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString statsText READ statsText NOTIFY statsChanged)
  Q_PROPERTY(QString liveText READ liveText NOTIFY audioStateChanged)
  Q_PROPERTY(QString noteText READ noteText NOTIFY audioStateChanged)
  Q_PROPERTY(QString logText READ logText NOTIFY logChanged)
  Q_PROPERTY(QString backendHint READ backendHint CONSTANT)
  Q_PROPERTY(QStringList backendLabels READ backendLabels NOTIFY choicesChanged)
  Q_PROPERTY(QStringList inputLabels READ inputLabels NOTIFY choicesChanged)
  Q_PROPERTY(QStringList bufferLabels READ bufferLabels NOTIFY choicesChanged)
  Q_PROPERTY(int backendIndex READ backendIndex WRITE setBackendIndex NOTIFY selectionChanged)
  Q_PROPERTY(int inputIndex READ inputIndex WRITE setInputIndex NOTIFY selectionChanged)
  Q_PROPERTY(int bufferIndex READ bufferIndex WRITE setBufferIndex NOTIFY selectionChanged)

 public:
  SystemViewModel(LinkClient& link, AudioGlue& audio, MidiGlue& midi, FxManager& fx,
                  AppState& state, QObject* parent = nullptr);

  QString statsText() const { return statsText_; }
  QString liveText() const { return liveText_; }
  QString noteText() const { return noteText_; }
  QString logText() const { return logText_; }
  QString backendHint() const { return backendHint_; }
  QStringList backendLabels() const { return backendLabels_; }
  QStringList inputLabels() const { return inputLabels_; }
  QStringList bufferLabels() const { return bufferLabels_; }
  int backendIndex() const { return backendIndex_; }
  int inputIndex() const { return inputIndex_; }
  int bufferIndex() const { return bufferIndex_; }

  void setBackendIndex(int index);
  void setInputIndex(int index);
  void setBufferIndex(int index);

  Q_INVOKABLE void apply();
  Q_INVOKABLE void refreshDevices();
  void refresh();

 signals:
  void statsChanged();
  void audioStateChanged();
  void logChanged();
  void choicesChanged();
  void selectionChanged();

 private:
  void rebuildChoices();
  AudioBackendChoice pendingChoice() const;

  LinkClient& link_;
  AudioGlue& audio_;
  MidiGlue& midi_;
  FxManager& fx_;
  AppState& state_;
  QString statsText_;
  QString liveText_;
  QString noteText_;
  QString logText_;
  QString backendHint_;
  QStringList backendLabels_;
  QStringList inputLabels_;
  QStringList bufferLabels_;
  std::vector<AudioDeviceOption> outputs_;
  std::vector<AudioDeviceOption> inputs_;
  int backendIndex_ = 0;
  int inputIndex_ = 0;
  int bufferIndex_ = 1;
};

}  // namespace mnky::app
