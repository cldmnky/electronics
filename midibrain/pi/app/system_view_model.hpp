#pragma once
// QML view model for the System screen: link/clock/READY diagnostics, the Qt
// Multimedia backend/device selector, metronome configuration and the in-app
// log tail. The selector is the only place to switch output/input device and
// the requested period size; Apply restarts audio through AudioGlue.
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include <vector>

#include "app_state.hpp"
#include "audio_glue.hpp"
#include "fx_slot.hpp"
#include "link_client.hpp"
#include "midi_glue.hpp"
#include "session_settings.hpp"

namespace mnky::app {

class SystemViewModel final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString statsText READ statsText NOTIFY statsChanged)
  Q_PROPERTY(QString liveText READ liveText NOTIFY audioStateChanged)
  Q_PROPERTY(QString noteText READ noteText NOTIFY audioStateChanged)
  Q_PROPERTY(QString logText READ logText NOTIFY logChanged)
  Q_PROPERTY(QString backendHint READ backendHint CONSTANT)
  // No-Pico mode (--no-pico): no Pico. The banner says which subsystems are live
  // and which are physically absent; statsText draws the same distinction.
  Q_PROPERTY(bool noPicoMode READ noPicoMode CONSTANT)
  Q_PROPERTY(QString noPicoText READ noPicoText CONSTANT)
  Q_PROPERTY(QStringList bufferLabels READ bufferLabels NOTIFY choicesChanged)
  // One row per selectable output/input device, with the three columns the
  // System screen shows: "name" (the device name exactly as the backend
  // enumerated it, plus " (default)"), "backend" and "channels"; "label" is the
  // same row flattened for the closed field and the panel readout. Two USB
  // devices that share a generic name stay distinguishable because the UI never
  // substitutes a name of its own.
  Q_PROPERTY(QVariantList outputRows READ outputRows NOTIFY choicesChanged)
  Q_PROPERTY(QVariantList inputRows READ inputRows NOTIFY choicesChanged)
  // Live device selection: a picker change reopens the stream immediately, so
  // there is no APPLY button. `applyNote` carries the audio layer's fallback
  // reason (a requested device that could not open falls back and says why).
  Q_PROPERTY(QString applyNote READ applyNote NOTIFY applyNoteChanged)
  Q_PROPERTY(int backendIndex READ backendIndex WRITE setBackendIndex NOTIFY selectionChanged)
  Q_PROPERTY(int inputIndex READ inputIndex WRITE setInputIndex NOTIFY selectionChanged)
  Q_PROPERTY(int bufferIndex READ bufferIndex WRITE setBufferIndex NOTIFY selectionChanged)

 public:
  SystemViewModel(LinkClient& link, AudioGlue& audio, MidiGlue& midi, FxManager& fx,
                  AppState& state, SessionSettings& settings,
                  QObject* parent = nullptr);

  QString statsText() const { return statsText_; }
  QString liveText() const { return liveText_; }
  QString noteText() const { return noteText_; }
  QString logText() const { return logText_; }
  QString backendHint() const { return backendHint_; }
  bool noPicoMode() const { return link_.noPico(); }
  QString noPicoText() const;
  QStringList backendLabels() const;
  QStringList inputLabels() const;
  QStringList bufferLabels() const { return bufferLabels_; }
  QVariantList outputRows() const { return outputRows_; }
  QVariantList inputRows() const { return inputRows_; }
  // The device rows alone (no Default/Emulator header rows), so the three-column
  // contract is testable without an audio backend. The backend column comes from
  // the device id prefix the backend enumeration sets.
  static QVariantList deviceRows(const std::vector<AudioDeviceOption>& devices);
  QString applyNote() const { return applyNote_; }
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
  void applyNoteChanged();
  // One-shot notice for the app's transient popup: the fallback reason when the
  // requested device cannot open. Empty notes never fire it.
  void applyNotice(const QString& note);

 private:
  void rebuildChoices();
  AudioBackendChoice pendingChoice() const;
  QString clockSourceLine() const;

  LinkClient& link_;
  AudioGlue& audio_;
  MidiGlue& midi_;
  FxManager& fx_;
  AppState& state_;
  SessionSettings& settings_;
  QString statsText_;
  QString liveText_;
  QString noteText_;
  QString logText_;
  QString backendHint_;
  bool noPico_ = false;
  QStringList bufferLabels_;
  QVariantList outputRows_;
  QVariantList inputRows_;
  std::vector<AudioDeviceOption> outputs_;
  std::vector<AudioDeviceOption> inputs_;
  QString applyNote_;
  int backendIndex_ = 0;
  int inputIndex_ = 0;
  int bufferIndex_ = 1;
};

}  // namespace mnky::app
