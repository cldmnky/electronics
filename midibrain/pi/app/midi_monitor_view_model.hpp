#pragma once
// QML model for the MIDI MONITOR screen: one row per MIDI port (TRS IN1/IN2,
// USB IN, TRS OUT1/OUT2, USB OUT) with note/CC/clock counters, plus the last-N
// message log newest-first. The data comes from MidiGlue's monitor ring, which
// sees every message the Pi handles: TRS inputs from the Pico link, the USB
// input callback, routed outputs when the link thread flushes them and the
// Pi-side USB clock mirror. The Pico's own clock/transport generation for the
// TRS outputs is not echoed back over the link, so TRS OUT rows show routed
// messages only; the screen says so on its header line.
#include <QAbstractListModel>
#include <QObject>
#include <QString>
#include <QVariant>

#include <array>
#include <cstdint>
#include <vector>

#include "midi_glue.hpp"

namespace mnky::app {

// Pure formatting used by the Monitor screen (and by logic_tests.cpp).
QString midiPortName(int port);
QString midiMessageText(std::uint8_t size, const std::uint8_t* data);
QString midiAgeText(std::uint64_t ageUs);

// Port rows, newest state per port. Roles are read-only; refresh() pulls a new
// MidiGlue snapshot and only emits when the counters or the log changed.
class MidiMonitorPortModel final : public QAbstractListModel {
  Q_OBJECT
 public:
  enum Roles {
    NameRole = Qt::UserRole + 1,
    DirectionRole,
    NotesRole,
    CcRole,
    ClockRole,
    OtherRole,
    // False for the four TRS rows in no-Pico mode: the Pico owns those UARTs, so
    // the screen says UNAVAILABLE instead of showing a zero that looks idle.
    AvailableRole,
  };

  explicit MidiMonitorPortModel(QObject* parent = nullptr);

  int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

  void setCounts(const std::array<MidiGlue::MonitorCounts, midi::kPortCount>& counts);
  void setNoPico(bool noPico) { noPico_ = noPico; }
  bool available(int port) const;
  QString name(int port) const;
  QString countsText(int port) const;

 private:
  std::array<MidiGlue::MonitorCounts, midi::kPortCount> counts_{};
  bool noPico_ = false;
};

// Message log, newest first. Kept short enough that a full rebuild per change
// stays cheap (the screen refreshes about 15 times a second).
class MidiMonitorEventModel final : public QAbstractListModel {
  Q_OBJECT
 public:
  enum Roles { TextRole = Qt::UserRole + 1, PortRole, AgeRole, KindRole };

  explicit MidiMonitorEventModel(QObject* parent = nullptr);

  int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

  void setEntries(std::vector<QString> texts, std::vector<int> ports,
                  std::vector<QString> ages, std::vector<QString> kinds);

 private:
  std::vector<QString> texts_;
  std::vector<int> ports_;
  std::vector<QString> ages_;
  std::vector<QString> kinds_;
};

class MidiMonitorViewModel final : public QObject {
  Q_OBJECT
  Q_PROPERTY(MidiMonitorPortModel* ports READ ports CONSTANT)
  Q_PROPERTY(MidiMonitorEventModel* events READ events CONSTANT)
  Q_PROPERTY(int logRows READ logRows NOTIFY logChanged)
  Q_PROPERTY(QString headerText READ headerText NOTIFY logChanged)
  Q_PROPERTY(int shownEvents READ shownEvents WRITE setShownEvents NOTIFY logChanged)
  Q_PROPERTY(int selectedPort READ selectedPort WRITE setSelectedPort NOTIFY selectionChanged)
  Q_PROPERTY(bool hideClock READ hideClock WRITE setHideClock NOTIFY logChanged)
  Q_PROPERTY(bool noPicoMode READ noPicoMode CONSTANT)

 public:
  MidiMonitorViewModel(MidiGlue& midi, bool noPico, QObject* parent = nullptr);

  MidiMonitorPortModel* ports() { return &ports_; }
  MidiMonitorEventModel* events() { return &events_; }
  int logRows() const { return logRows_; }
  QString headerText() const;
  int shownEvents() const { return shownEvents_; }
  Q_INVOKABLE void setShownEvents(int count);
  int selectedPort() const { return selectedPort_; }
  Q_INVOKABLE void setSelectedPort(int port);
  bool noPicoMode() const { return noPico_; }
  // Log filter: clock bytes arrive at 24 ppq and would drown every other
  // message. The counters always count them; only the log honours this.
  bool hideClock() const { return hideClock_; }
  void setHideClock(bool hide);

  // GUI-thread poll; cheap when nothing new arrived.
  void refresh();
  // Drop the ring and the counters (RESET button / panel focus).
  Q_INVOKABLE void reset();
  // Activate a port row from the panel: read its counters out in the popup.
  Q_INVOKABLE QString portSummary(int port) const;
  Q_INVOKABLE QString portName(int port) const { return midiPortName(port); }

 signals:
  void logChanged();
  void selectionChanged();

 private:
  MidiGlue& midi_;
  MidiMonitorPortModel ports_;
  MidiMonitorEventModel events_;
  int logRows_ = 0;
  int shownEvents_ = 14;
  int selectedPort_ = 0;
  bool hideClock_ = false;
  std::uint64_t lastTotal_ = 0;
  const bool noPico_;
};

}  // namespace mnky::app
