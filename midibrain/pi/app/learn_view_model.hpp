#pragma once
// MIDI-learn capture mode for the System screen. The LearnEngine lives in
// MidiGlue (dispatch runs on the MIDI input thread); this model offers a target
// picker, arms one capture, polls the lock-free capture snapshot and maintains
// the binding table shown in QML. Bindings are session "midimap" entries.
#include <QObject>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <string>
#include <vector>

#include "logic.hpp"
#include "midi/learn.hpp"
#include "midi_glue.hpp"

namespace mnky::app {

class LearnViewModel final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QStringList targets READ targets NOTIFY targetsChanged)
  Q_PROPERTY(QStringList bindings READ bindings NOTIFY bindingsChanged)
  Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
  Q_PROPERTY(QString captureTarget READ captureTarget NOTIFY captureChanged)
  Q_PROPERTY(int targetIndex READ targetIndex WRITE setTargetIndex NOTIFY targetChanged)
  Q_PROPERTY(bool capturing READ capturing NOTIFY captureChanged)

 public:
  explicit LearnViewModel(MidiGlue& midi, QObject* parent = nullptr);

  QStringList targets() const { return targets_; }
  QStringList bindings() const { return bindings_; }
  QString statusText() const { return statusText_; }
  QString captureTarget() const { return captureTarget_; }
  int targetIndex() const { return targetIndex_; }
  void setTargetIndex(int index);
  bool capturing() const { return capturing_; }

  Q_INVOKABLE void startCapture();
  Q_INVOKABLE void cancelCapture();
  Q_INVOKABLE void removeBinding(int index);
  Q_INVOKABLE void clearAll();

  // Called by the controller timer; consumes one captured CC.
  void poll();
  void refresh();

 signals:
  void targetsChanged();
  void bindingsChanged();
  void statusChanged();
  void captureChanged();
  void targetChanged();

 private:
  void refreshBindings();
  void refreshTargets();

  MidiGlue& midi_;
  QStringList baseTargets_;
  QStringList targets_;
  QStringList bindings_;
  std::vector<midi::LearnBinding> bindingRows_;
  QString statusText_ = QStringLiteral("capture off");
  QString captureTarget_;
  int targetIndex_ = 0;
  bool capturing_ = false;
  std::uint64_t lastCaptureSeq_ = 0;
};

}  // namespace mnky::app
