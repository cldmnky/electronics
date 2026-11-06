#include "system_view_model.hpp"

#include <QString>
#include <QTextStream>

#include <algorithm>

#include "logging.hpp"
#include "logic.hpp"

namespace mnky::app {
namespace {
QString hexString(std::uint32_t value) {
  return QStringLiteral("0x") + QString::number(value, 16);
}

// The backend every enumerated device is opened through: AudioDeviceOption::id
// carries it as its prefix ("pipewire:<node>", "alsa:<pcm>"), which is the one
// per-device backend fact the audio layer exposes today. A row without a
// prefixed id (the platform default) says so instead of guessing.
QString deviceBackend(const AudioDeviceOption& device) {
  const QString id = QString::fromStdString(device.id);
  const int colon = id.indexOf(QLatin1Char(':'));
  return colon > 0 ? id.left(colon) : QStringLiteral("?");
}

// AudioDeviceOption carries no channel count yet (the missing field is a
// per-device output/input channel count filled by the backend enumeration in
// audio_glue.hpp / audio worker scope), so an unknown count is shown as such
// instead of a guessed number.
const QString kUnknownChannels = QStringLiteral("?");

QVariantMap deviceRow(const QString& name, const QString& backend,
                      const QString& channels) {
  const QString detail =
      backend + QStringLiteral("  ch ") + channels;
  QVariantMap row;
  row.insert(QStringLiteral("name"), name);
  row.insert(QStringLiteral("backend"), backend);
  row.insert(QStringLiteral("channels"), channels);
  row.insert(QStringLiteral("detail"), detail);
  row.insert(QStringLiteral("label"), name + QStringLiteral("  ") + detail);
  return row;
}

QStringList rowLabels(const QVariantList& rows) {
  QStringList labels;
  labels.reserve(rows.size());
  for (const auto& row : rows) {
    labels << row.toMap().value(QStringLiteral("label")).toString();
  }
  return labels;
}
}  // namespace

QVariantList SystemViewModel::deviceRows(const std::vector<AudioDeviceOption>& devices) {
  QVariantList rows;
  rows.reserve(static_cast<int>(devices.size()));
  for (const auto& device : devices) {
    rows.push_back(deviceRow(QString::fromStdString(device.name) +
                                 (device.isDefault ? QStringLiteral(" (default)") : QString()),
                             deviceBackend(device), kUnknownChannels));
  }
  return rows;
}

QStringList SystemViewModel::backendLabels() const { return rowLabels(outputRows_); }

QStringList SystemViewModel::inputLabels() const { return rowLabels(inputRows_); }

SystemViewModel::SystemViewModel(LinkClient& link, AudioGlue& audio, MidiGlue& midi,
                                 FxManager& fx, AppState& state, QObject* parent)
    : QObject(parent), link_(link), audio_(audio), midi_(midi), fx_(fx), state_(state) {
  noPico_ = link_.noPico();
  backendHint_ = QStringLiteral(
      "Qt Multimedia: PulseAudio/PipeWire on Linux, CoreAudio on macOS. JACK device "
      "selection was dropped with the JUCE app; use a PipeWire/Pulse patchbay for "
      "inter-app routing.");
  refreshDevices();
  refresh();
}

QString SystemViewModel::noPicoText() const {
  if (!noPico_) return {};
  return QStringLiteral(
      "NO PICO.  Active: audio, USB MIDI, recorder, takes, mixer, FX, EQ, "
      "X/Y, projects, Pi clock -> USB OUT.\n"
      "Absent: TRS MIDI + TRS clock (Pico UARTs), panel controls (touch only), "
      "LEDs, clock-source follow.");
}

void SystemViewModel::setBackendIndex(int index) {
  const int clamped = index >= 0 && index < outputRows_.size() ? index : 0;
  if (clamped == backendIndex_) return;
  backendIndex_ = clamped;
  emit selectionChanged();
}

void SystemViewModel::setInputIndex(int index) {
  const int clamped = index >= 0 && index < inputRows_.size() ? index : 0;
  if (clamped == inputIndex_) return;
  inputIndex_ = clamped;
  emit selectionChanged();
  apply();
}

void SystemViewModel::setBufferIndex(int index) {
  const int clamped = std::clamp(index, 0, kAudioBufferSizeCount - 1);
  if (clamped == bufferIndex_) return;
  bufferIndex_ = clamped;
  emit selectionChanged();
  apply();
}

AudioBackendChoice SystemViewModel::pendingChoice() const {
  AudioBackendChoice choice;
  if (backendIndex_ == 0) {
    choice.backend = AudioBackend::Default;
  } else if (backendIndex_ == 1) {
    choice.backend = AudioBackend::Emulator;
  } else {
    const std::size_t device = static_cast<std::size_t>(backendIndex_ - 2);
    choice.backend = AudioBackend::Device;
    if (device < outputs_.size()) choice.outputDeviceId = outputs_[device].id;
  }
  if (inputIndex_ > 0 && static_cast<std::size_t>(inputIndex_ - 1) < inputs_.size()) {
    choice.inputDeviceId = inputs_[static_cast<std::size_t>(inputIndex_ - 1)].id;
  }
  choice.bufferSize = kAudioBufferSizes[bufferIndex_];
  return choice;
}

void SystemViewModel::apply() {
  const auto choice = pendingChoice();
  // The audio layer resolves the request and reports a fallback reason when the
  // requested device cannot open; the picker keeps the user's choice and the
  // note says what is actually running.
  const QString note = QString::fromStdString(audio_.applyAudioChoice(choice)).trimmed();
  if (note != applyNote_) {
    applyNote_ = note;
    emit applyNoteChanged();
    if (!note.isEmpty()) emit applyNotice(note);
  }
  refreshDevices();
  refresh();
}

void SystemViewModel::refreshDevices() {
  audio_.rescanBackends();
  outputs_ = audio_.outputDevices();
  inputs_ = audio_.inputDevices();

  // The emulator generator fills audio::kMaxInputs strip channels
  // (AudioGlue::renderBlock), so its count is known; a real device's count is
  // pending the backend field. "auto" is the documented Default resolution:
  // PipeWire first, then ALSA, then the generator.
  outputRows_.clear();
  outputRows_ << deviceRow(QStringLiteral("Default output"), QStringLiteral("auto"),
                          kUnknownChannels)
              << deviceRow(QStringLiteral("Emulator generator"), QStringLiteral("emulator"),
                          QString::number(audio::kMaxInputs));
  for (const auto& row : deviceRows(outputs_)) {
    outputRows_ << row;
  }
  inputRows_.clear();
  inputRows_ << deviceRow(QStringLiteral("No input"), QStringLiteral("-"), QStringLiteral("-"));
  for (const auto& row : deviceRows(inputs_)) {
    inputRows_ << row;
  }
  bufferLabels_.clear();
  for (const int frames : kAudioBufferSizes) {
    bufferLabels_ << QString::fromStdString(audioBufferSizeLabel(frames));
  }

  const auto choice = audio_.audioChoice();
  backendIndex_ = 0;
  if (choice.backend == AudioBackend::Emulator) {
    backendIndex_ = 1;
  } else if (choice.backend == AudioBackend::Device) {
    for (std::size_t i = 0; i < outputs_.size(); ++i) {
      if (outputs_[i].id == choice.outputDeviceId) {
        backendIndex_ = static_cast<int>(i) + 2;
        break;
      }
    }
  }
  inputIndex_ = 0;
  for (std::size_t i = 0; i < inputs_.size(); ++i) {
    if (inputs_[i].id == choice.inputDeviceId) {
      inputIndex_ = static_cast<int>(i) + 1;
      break;
    }
  }
  const int buffer = clampAudioBufferSize(choice.bufferSize);
  bufferIndex_ = 0;
  for (int i = 0; i < kAudioBufferSizeCount; ++i) {
    if (kAudioBufferSizes[i] == buffer) {
      bufferIndex_ = i;
      break;
    }
  }
  emit choicesChanged();
  emit selectionChanged();
}

void SystemViewModel::refresh() {
  const auto status = link_.status();
  const auto stats = status.stats;
  const auto audio = audio_.backendState();

  QString text;
  QTextStream stream(&text);
  if (status.noPico) {
    // No PicoLink exists in no-Pico mode: never print the hardware handshake
    // block, and never call the absent link "DOWN".
    stream << "LINK      NO PICO (no SPI, no PicoLink)\n";
    stream << "          Pi clock master; USB OUT only\n";
    stream << "READY     unavailable (no Pico)\n";
    stream << "CLOCK     " << QString::fromStdString(formatTempo(status.tempoX100))
           << "   " << (status.rolling ? "rolling" : "stopped") << "   "
           << (status.clockPrimed ? "free-run armed" : "free-run cold") << "\n";
    stream << "          local Pi phase; clock-source follow off\n";
    stream << "TRANSPORT tick " << static_cast<qulonglong>(status.tick) << "   "
           << QString::fromStdString(formatBarBeatTick(status.tick))
           << "   owned by the Pi (NO PICO)\n";
  } else {
    stream << "LINK      "
           << (status.emulated ? "emulator transport (scripted Pico)"
                               : "spidev0.0 @ 1 MHz, mode 0")
           << "\n";
    stream << "          " << (status.linkUp ? "UP" : "DOWN") << "   Pico session "
           << hexString(status.picoSession) << "   Pi session " << hexString(status.piSession)
           << "\n";
    stream << "          tx " << static_cast<qulonglong>(stats.tx) << "  rx "
           << static_cast<qulonglong>(stats.rx) << "  bad "
           << static_cast<qulonglong>(stats.bad) << "  dup "
           << static_cast<qulonglong>(stats.dup) << "  overruns "
           << static_cast<qulonglong>(stats.overruns) << "\n";
    stream << "READY     "
           << (status.readyValid ? (status.ready ? "high" : "low")
                                 : "not sampled (READY_IRQ needs the Pi GPIO character device)")
           << "\n";
    stream << "CLOCK     " << QString::fromStdString(formatTempo(status.tempoX100)) << "   "
           << (status.rolling ? "rolling" : "stopped") << "   "
           << (status.clockPrimed ? "primed" : "cold") << "\n";
    stream << "          offset " << QString::number(status.offsetUs, 'f', 1) << " us   latency "
           << QString::number(status.latencyUs, 'f', 1) << " us   skipped "
           << static_cast<qulonglong>(status.clockSkipped) << "   reordered "
           << static_cast<qulonglong>(status.clockReordered) << "\n";
    stream << "TRANSPORT tick " << static_cast<qulonglong>(status.tick) << "   "
           << QString::fromStdString(formatBarBeatTick(status.tick)) << "   shift "
           << (status.shift ? "held" : "up") << "   mode "
           << (status.manual ? "manual" : "local") << "\n";
  }
  stream << "AUDIO     " << QString::fromStdString(audio_.backendDescription()) << "\n";
  stream << "          " << QString::number(audio.sampleRate, 'f', 0) << " Hz / "
         << audio.blockSize << " frames   xruns n/a (Qt)   blocks "
         << static_cast<qulonglong>(audio.callbackBlocks) << "   rec "
         << (audio_.recording() ? "on" : "off") << "   drops "
         << static_cast<qulonglong>(audio_.drops()) << "\n";
  stream << "MIDI      " << QString::fromStdString(midi_.describe())
         << (status.noPico ? "   (NO PICO: USB only)" : "") << "   routed "
         << static_cast<qulonglong>(midi_.routedCount()) << "\n";
  stream << "FX        A " << QString::fromStdString(fx_.slot(0).pluginPath()) << "\n";
  stream << "          B " << QString::fromStdString(fx_.slot(1).pluginPath()) << "\n";
  stream << "CLICK     mode "
         << (state_.clickMode.load() == 0 ? "off"
                                           : (state_.clickMode.load() == 1 ? "record" : "play"))
         << "   out " << state_.clickOutput.load() << "   gain "
         << QString::number(state_.clickGainDb.load(), 'f', 1) << " dB   bars "
         << state_.clickBars.load() << "\n";

  QString live;
  if (audio.liveType == "emulator") {
    live = QStringLiteral("LIVE  emulator generator   %1 Hz / %2 frames   no device")
               .arg(QString::number(audio.sampleRate, 'f', 0))
               .arg(audio.blockSize);
  } else if (audio.liveType == "none") {
    live = QStringLiteral("LIVE  no audio device");
  } else {
    live = QStringLiteral("LIVE  %1 / %2   %3 Hz / %4 frames")
               .arg(QString::fromStdString(audio.liveType))
               .arg(QString::fromStdString(audio.liveDevice))
               .arg(QString::number(audio.sampleRate, 'f', 0))
               .arg(audio.blockSize);
  }

  QString note;
  if (!audio.note.empty()) {
    note = QStringLiteral("NOTE  ") + QString::fromStdString(audio.note);
  } else {
    note = QStringLiteral("REQUEST  ") + backendLabels().value(backendIndex_) +
           QStringLiteral(" / ") + inputLabels().value(inputIndex_) + QStringLiteral(" / ") +
           bufferLabels_.value(bufferIndex_) + QStringLiteral("   ->   EFFECTIVE  ") +
           QString::fromStdString(audioBackendLabel(audio.effective));
  }

  const QString log = QString::fromStdString(AppLogger::instance().tail(120));
  if (statsText_ != text) {
    statsText_ = text;
    emit statsChanged();
  }
  if (liveText_ != live || noteText_ != note) {
    liveText_ = live;
    noteText_ = note;
    emit audioStateChanged();
  }
  if (logText_ != log) {
    logText_ = log;
    emit logChanged();
  }
}

}  // namespace mnky::app
