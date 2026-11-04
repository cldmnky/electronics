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
}  // namespace

SystemViewModel::SystemViewModel(LinkClient& link, AudioGlue& audio, MidiGlue& midi,
                                 FxManager& fx, AppState& state, QObject* parent)
    : QObject(parent), link_(link), audio_(audio), midi_(midi), fx_(fx), state_(state) {
  backendHint_ = QStringLiteral(
      "Qt Multimedia: PulseAudio/PipeWire on Linux, CoreAudio on macOS. JACK device "
      "selection was dropped with the JUCE app; use a PipeWire/Pulse patchbay for "
      "inter-app routing.");
  refreshDevices();
  refresh();
}

void SystemViewModel::setBackendIndex(int index) {
  const int clamped = index >= 0 && index < backendLabels_.size() ? index : 0;
  if (clamped == backendIndex_) return;
  backendIndex_ = clamped;
  emit selectionChanged();
}

void SystemViewModel::setInputIndex(int index) {
  const int clamped = index >= 0 && index < inputLabels_.size() ? index : 0;
  if (clamped == inputIndex_) return;
  inputIndex_ = clamped;
  emit selectionChanged();
}

void SystemViewModel::setBufferIndex(int index) {
  const int clamped = std::clamp(index, 0, kAudioBufferSizeCount - 1);
  if (clamped == bufferIndex_) return;
  bufferIndex_ = clamped;
  emit selectionChanged();
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
  (void)audio_.applyAudioChoice(choice);
  refreshDevices();
  refresh();
}

void SystemViewModel::refreshDevices() {
  audio_.rescanBackends();
  outputs_ = audio_.outputDevices();
  inputs_ = audio_.inputDevices();

  backendLabels_.clear();
  backendLabels_ << QStringLiteral("Default output")
                 << QStringLiteral("Emulator generator");
  for (const auto& device : outputs_) {
    backendLabels_ << QString::fromStdString(device.name) +
                          (device.isDefault ? QStringLiteral(" (default)") : QString());
  }
  inputLabels_.clear();
  inputLabels_ << QStringLiteral("No input");
  for (const auto& device : inputs_) {
    inputLabels_ << QString::fromStdString(device.name) +
                        (device.isDefault ? QStringLiteral(" (default)") : QString());
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
  stream << "AUDIO     " << QString::fromStdString(audio_.backendDescription()) << "\n";
  stream << "          " << QString::number(audio.sampleRate, 'f', 0) << " Hz / "
         << audio.blockSize << " frames   xruns n/a (Qt)   blocks "
         << static_cast<qulonglong>(audio.callbackBlocks) << "   rec "
         << (audio_.recording() ? "on" : "off") << "   drops "
         << static_cast<qulonglong>(audio_.drops()) << "\n";
  stream << "MIDI      " << QString::fromStdString(midi_.describe()) << "   routed "
         << static_cast<qulonglong>(midi_.routedCount()) << "\n";
  stream << "FX        A " << QString::fromStdString(fx_.slot(0).pluginPath()) << "\n";
  stream << "          B " << QString::fromStdString(fx_.slot(1).pluginPath()) << "\n";
  stream << "CLICK     " << (state_.clickEnabled.load() ? "on" : "off") << "   level "
         << QString::number(state_.clickLevel.load(), 'f', 2) << "   bars "
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
    note = QStringLiteral("REQUEST  ") + backendLabels_.value(backendIndex_) +
           QStringLiteral(" / ") + inputLabels_.value(inputIndex_) + QStringLiteral(" / ") +
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
