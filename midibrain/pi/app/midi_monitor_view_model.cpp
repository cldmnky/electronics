#include "midi_monitor_view_model.hpp"

#include <algorithm>
#include <utility>

#include "../midi/clock.hpp"

namespace mnky::app {
namespace {

constexpr const char* kPortNames[midi::kPortCount] = {"TRS IN1", "TRS IN2", "USB IN",
                                                     "TRS OUT1", "TRS OUT2", "USB OUT"};

const char* channelText(std::uint8_t status) {
  static const char* names[16] = {"01", "02", "03", "04", "05", "06", "07", "08",
                                  "09", "10", "11", "12", "13", "14", "15", "16"};
  return names[status & 0x0Fu];
}

}  // namespace

QString midiPortName(int port) {
  if (port < 0 || port >= midi::kPortCount) return QStringLiteral("-");
  return QString::fromLatin1(kPortNames[port]);
}

QString midiMessageText(std::uint8_t size, const std::uint8_t* data) {
  if (data == nullptr || size == 0) return QStringLiteral("-");
  if (size == 1) {
    switch (data[0]) {
      case midi::kClockByte: return QStringLiteral("CLOCK");
      case midi::kStartByte: return QStringLiteral("START");
      case midi::kContinueByte: return QStringLiteral("CONTINUE");
      case midi::kStopByte: return QStringLiteral("STOP");
      default: break;
    }
    return QStringLiteral("RT %1").arg(data[0], 2, 16, QLatin1Char('0')).toUpper();
  }
  const std::uint8_t status = data[0];
  const std::uint8_t kind = status & 0xF0u;
  const auto byte = [](std::uint8_t value) {
    return QStringLiteral("%1").arg(value, 2, 16, QLatin1Char('0')).toUpper();
  };
  if (kind == 0x90u && size >= 3) {
    if (data[2] == 0) {
      return QStringLiteral("N OFF %1 %2").arg(QLatin1String(channelText(status)), byte(data[1]));
    }
    return QStringLiteral("N ON %1 %2 v%3")
        .arg(QLatin1String(channelText(status)), byte(data[1]))
        .arg(static_cast<int>(data[2]));
  }
  if (kind == 0x80u && size >= 3) {
    return QStringLiteral("N OFF %1 %2").arg(QLatin1String(channelText(status)), byte(data[1]));
  }
  if (kind == 0xB0u && size >= 3) {
    return QStringLiteral("CC %1 %2 %3")
        .arg(QLatin1String(channelText(status)))
        .arg(static_cast<int>(data[1]))
        .arg(static_cast<int>(data[2]));
  }
  if (kind == 0xE0u && size >= 3) {
    return QStringLiteral("BEND %1 %2")
        .arg(QLatin1String(channelText(status)))
        .arg(static_cast<int>(data[1]) | (static_cast<int>(data[2]) << 7));
  }
  if (kind == 0xC0u || kind == 0xD0u) {
    return QStringLiteral("%1 %2 %3")
        .arg(kind == 0xC0u ? QStringLiteral("PC") : QStringLiteral("PRESS"),
             QLatin1String(channelText(status)))
        .arg(static_cast<int>(data[1]));
  }
  if (kind == 0xA0u && size >= 3) {
    return QStringLiteral("AT %1 %2 %3")
        .arg(QLatin1String(channelText(status)))
        .arg(static_cast<int>(data[1]))
        .arg(static_cast<int>(data[2]));
  }
  QString text = QStringLiteral("RAW");
  const std::uint8_t count = size > 3 ? 3 : size;
  for (std::uint8_t i = 0; i < count; ++i) text += QLatin1Char(' ') + byte(data[i]);
  return text;
}

QString midiAgeText(std::uint64_t ageUs) {
  if (ageUs < 1000) return QStringLiteral("%1 us").arg(static_cast<qulonglong>(ageUs));
  if (ageUs < 1000000) {
    return QStringLiteral("%1 ms").arg(static_cast<double>(ageUs) / 1000.0, 0, 'f', 1);
  }
  return QStringLiteral("%1 s").arg(static_cast<double>(ageUs) / 1000000.0, 0, 'f', 1);
}

MidiMonitorPortModel::MidiMonitorPortModel(QObject* parent) : QAbstractListModel(parent) {}

int MidiMonitorPortModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : midi::kPortCount;
}

QVariant MidiMonitorPortModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= midi::kPortCount) return {};
  const auto port = static_cast<std::size_t>(index.row());
  switch (role) {
    case NameRole: return name(index.row());
    case DirectionRole:
      return midi::isInput(static_cast<midi::Port>(index.row())) ? QStringLiteral("IN")
                                                                : QStringLiteral("OUT");
    case NotesRole: return static_cast<qulonglong>(counts_[port].notes);
    case CcRole: return static_cast<qulonglong>(counts_[port].cc);
    case ClockRole: return static_cast<qulonglong>(counts_[port].clock);
    case OtherRole: return static_cast<qulonglong>(counts_[port].other);
    default: return {};
  }
}

QHash<int, QByteArray> MidiMonitorPortModel::roleNames() const {
  return {{NameRole, "name"},       {DirectionRole, "direction"},
          {NotesRole, "notes"},     {CcRole, "cc"},
          {ClockRole, "clock"},     {OtherRole, "other"}};
}

void MidiMonitorPortModel::setCounts(
    const std::array<MidiGlue::MonitorCounts, midi::kPortCount>& counts) {
  if (counts == counts_) return;
  counts_ = counts;
  emit dataChanged(index(0, 0), index(midi::kPortCount - 1, 0));
}

QString MidiMonitorPortModel::name(int port) const { return midiPortName(port); }

QString MidiMonitorPortModel::countsText(int port) const {
  if (port < 0 || port >= midi::kPortCount) return {};
  const auto& counts = counts_[static_cast<std::size_t>(port)];
  return QStringLiteral("notes %1  cc %2  clock %3  other %4")
      .arg(static_cast<qulonglong>(counts.notes))
      .arg(static_cast<qulonglong>(counts.cc))
      .arg(static_cast<qulonglong>(counts.clock))
      .arg(static_cast<qulonglong>(counts.other));
}

MidiMonitorEventModel::MidiMonitorEventModel(QObject* parent) : QAbstractListModel(parent) {}

int MidiMonitorEventModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(texts_.size());
}

QVariant MidiMonitorEventModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 ||
      index.row() >= static_cast<int>(texts_.size())) {
    return {};
  }
  const auto row = static_cast<std::size_t>(index.row());
  switch (role) {
    case TextRole: return texts_[row];
    case PortRole: return ports_[row];
    case AgeRole: return ages_[row];
    case KindRole: return kinds_[row];
    default: return {};
  }
}

QHash<int, QByteArray> MidiMonitorEventModel::roleNames() const {
  return {{TextRole, "text"}, {PortRole, "port"}, {AgeRole, "age"}, {KindRole, "kind"}};
}

void MidiMonitorEventModel::setEntries(std::vector<QString> texts, std::vector<int> ports,
                                       std::vector<QString> ages,
                                       std::vector<QString> kinds) {
  if (texts == texts_ && ports == ports_ && ages == ages_ && kinds == kinds_) return;
  beginResetModel();
  texts_ = std::move(texts);
  ports_ = std::move(ports);
  ages_ = std::move(ages);
  kinds_ = std::move(kinds);
  endResetModel();
}

MidiMonitorViewModel::MidiMonitorViewModel(MidiGlue& midi, QObject* parent)
    : QObject(parent), midi_(midi) {}

QString MidiMonitorViewModel::headerText() const {
  return QStringLiteral("MIDI MONITOR  -  %1 event(s), newest first").arg(logRows_);
}

void MidiMonitorViewModel::setShownEvents(int count) {
  const int clamped = std::clamp(count, 4, 40);
  if (clamped == shownEvents_) return;
  shownEvents_ = clamped;
  lastTotal_ = 0;
  refresh();
}

void MidiMonitorViewModel::setHideClock(bool hide) {
  if (hideClock_ == hide) return;
  hideClock_ = hide;
  lastTotal_ = 0;
  refresh();
}

void MidiMonitorViewModel::reset() {
  midi_.clearMonitor();
  lastTotal_ = 0;
  refresh();
}

void MidiMonitorViewModel::setSelectedPort(int port) {
  const int clamped = std::clamp(port, 0, midi::kPortCount - 1);
  if (clamped == selectedPort_) return;
  selectedPort_ = clamped;
  emit selectionChanged();
}

QString MidiMonitorViewModel::portSummary(int port) const {
  if (port < 0 || port >= midi::kPortCount) return {};
  return midiPortName(port) + QStringLiteral("  -  ") + ports_.countsText(port);
}

void MidiMonitorViewModel::refresh() {
  const auto snapshot = midi_.monitorSnapshot();
  ports_.setCounts(snapshot.counts);
  if (snapshot.total == lastTotal_) return;
  lastTotal_ = snapshot.total;

  std::vector<QString> texts;
  std::vector<int> ports;
  std::vector<QString> ages;
  std::vector<QString> kinds;
  const std::uint64_t now = midi::piNowUs();
  const std::size_t limit = static_cast<std::size_t>(shownEvents_);
  texts.reserve(limit);
  ports.reserve(limit);
  ages.reserve(limit);
  kinds.reserve(limit);
  // Newest first: walk the snapshot backwards, skipping clock bytes when the
  // log filter is on.
  for (std::size_t i = 0; i < snapshot.count && texts.size() < limit; ++i) {
    const auto& event = snapshot.events[snapshot.count - 1 - i];
    if (hideClock_ && event.size == 1 && event.data[0] == midi::kClockByte) continue;
    texts.push_back(midiMessageText(event.size, event.data));
    ports.push_back(static_cast<int>(event.port));
    ages.push_back(midiAgeText(now > event.stampUs ? now - event.stampUs : 0));
    kinds.push_back(event.outbound ? QStringLiteral("OUT") : QStringLiteral("IN"));
  }
  events_.setEntries(std::move(texts), std::move(ports), std::move(ages),
                     std::move(kinds));
  logRows_ = static_cast<int>(snapshot.count);
  emit logChanged();
}

}  // namespace mnky::app
