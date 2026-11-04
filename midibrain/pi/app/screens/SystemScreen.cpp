#include "SystemScreen.hpp"

#include "../audio_glue.hpp"
#include "../fx_slot.hpp"
#include "../link_client.hpp"
#include "../logic.hpp"
#include "../logging.hpp"
#include "../midi_glue.hpp"
#include "../ui_style.hpp"

namespace mnky::app {
namespace {
void configureEditor(juce::TextEditor& editor, float fontHeight) {
  editor.setMultiLine(true, true);
  editor.setReadOnly(true);
  editor.setScrollbarsShown(true);
  editor.setCaretVisible(false);
  editor.setPopupMenuEnabled(false);
  editor.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(),
                                              fontHeight, juce::Font::plain)));
}

// The look and feel builds its ComboBox colours from the stock V4 defaults, so
// the tokens are applied per control here (same palette, no second theme).
void styleComboBox(juce::ComboBox& box) {
  box.setColour(juce::ComboBox::backgroundColourId, style::panelDark);
  box.setColour(juce::ComboBox::textColourId, style::text);
  box.setColour(juce::ComboBox::outlineColourId, style::outline);
  box.setColour(juce::ComboBox::arrowColourId, style::textDim);
}

juce::String describeChoice(const AudioBackendChoice& choice) {
  return juce::String(audioBackendLabel(choice.backend)) + " / " +
         (choice.deviceName.empty() ? juce::String("default device")
                                    : juce::String(choice.deviceName)) +
         " / " + juce::String(choice.bufferSize) + " frames";
}
}  // namespace

SystemScreen::SystemScreen(AppContext& context) : Screen(context) {
  audioCaption_.setText("AUDIO BACKEND  -  applies immediately (ALSA stays the default)",
                        juce::dontSendNotification);
  audioCaption_.setFont(style::font(12.0f, true));
  audioCaption_.setColour(juce::Label::textColourId, style::textDim);
  addAndMakeVisible(audioCaption_);

  styleComboBox(backendBox_);
  backendBox_.setTooltip("Audio device type");
  backendBox_.onChange = [this] {
    rebuildDeviceChoices();
    refresh();
  };
  addAndMakeVisible(backendBox_);

  styleComboBox(deviceBox_);
  deviceBox_.setTooltip("Output device of the selected backend");
  addAndMakeVisible(deviceBox_);

  styleComboBox(bufferBox_);
  bufferBox_.setTooltip("Requested period size (JACK uses the server's period)");
  addAndMakeVisible(bufferBox_);

  applyButton_.onClick = [this] { applyAudioSelection(); };
  addAndMakeVisible(applyButton_);

  refreshButton_.onClick = [this] { refreshBackends(); };
  refreshButton_.setTooltip("Re-probe the device types and their devices");
  addAndMakeVisible(refreshButton_);

  audioLive_.setFont(style::font(13.0f));
  audioLive_.setColour(juce::Label::textColourId, style::text);
  audioLive_.setJustificationType(juce::Justification::centredLeft);
  addAndMakeVisible(audioLive_);

  audioNote_.setFont(style::font(11.0f));
  audioNote_.setColour(juce::Label::textColourId, style::textDim);
  audioNote_.setJustificationType(juce::Justification::centredLeft);
  addAndMakeVisible(audioNote_);

  configureEditor(stats_, 12.0f);
  configureEditor(logTail_, 11.0f);
  addAndMakeVisible(stats_);
  addAndMakeVisible(logTail_);

  // Device types and JACK availability can only be probed through the device
  // manager (the JACK probe opens a client), so this runs once here and then
  // only on REFRESH/APPLY, never on the refresh timer.
  context_.audio.rescanBackends();
  rebuildBackendChoices();
  rebuildBufferChoices();
  rebuildDeviceChoices();
  refresh();
}

void SystemScreen::resized() {
  auto area = getLocalBounds().reduced(6);

  audioCaption_.setBounds(area.removeFromTop(16));

  auto controls = area.removeFromTop(style::kTouch);
  backendBox_.setBounds(controls.removeFromLeft(190).reduced(2, 0));
  deviceBox_.setBounds(controls.removeFromLeft(220).reduced(2, 0));
  bufferBox_.setBounds(controls.removeFromLeft(130).reduced(2, 0));
  applyButton_.setBounds(controls.removeFromLeft(100).reduced(2, 0));
  refreshButton_.setBounds(controls.removeFromLeft(100).reduced(2, 0));

  audioLive_.setBounds(area.removeFromTop(18));
  audioNote_.setBounds(area.removeFromTop(16));
  area.removeFromTop(4);

  stats_.setBounds(area.removeFromTop(area.getHeight() * 3 / 5));
  area.removeFromTop(4);
  logTail_.setBounds(area);
}

void SystemScreen::rebuildBackendChoices() {
  const auto availability = context_.audio.backendAvailability();
  const auto choice = context_.audio.audioChoice();

  backendBox_.clear(juce::dontSendNotification);
  for (const auto backend : kAudioBackends) {
    const bool available = audioBackendAvailable(backend, availability);
    const int id = static_cast<int>(backend) + 1;
    backendBox_.addItem(juce::String(audioBackendLabel(backend, available)), id);
    backendBox_.setItemEnabled(id, available);
  }
  // A persisted request stays selected even when it is unavailable: the closed
  // box then reads e.g. "JACK (unavailable)" instead of silently showing ALSA.
  backendBox_.setSelectedId(static_cast<int>(choice.backend) + 1, juce::dontSendNotification);
}

void SystemScreen::rebuildBufferChoices() {
  const auto choice = context_.audio.audioChoice();
  bufferBox_.clear(juce::dontSendNotification);
  for (int index = 0; index < kAudioBufferSizeCount; ++index) {
    bufferBox_.addItem(audioBufferSizeLabel(kAudioBufferSizes[index]), index + 1);
  }
  for (int index = 0; index < kAudioBufferSizeCount; ++index) {
    if (kAudioBufferSizes[index] == clampAudioBufferSize(choice.bufferSize)) {
      bufferBox_.setSelectedId(index + 1, juce::dontSendNotification);
      break;
    }
  }
}

void SystemScreen::rebuildDeviceChoices() {
  const auto backend = selectedBackend();
  const auto choice = context_.audio.audioChoice();
  const auto names = context_.audio.outputDeviceNames(backend);

  deviceBox_.clear(juce::dontSendNotification);
  deviceBox_.addItem("(default device)", 1);
  for (int index = 0; index < names.size(); ++index) {
    deviceBox_.addItem(names[index], index + 2);
  }

  int selected = 1;
  if (choice.backend == backend && !choice.deviceName.empty()) {
    const int index = names.indexOf(juce::String(choice.deviceName));
    if (index >= 0) selected = index + 2;
  }
  deviceBox_.setSelectedId(selected, juce::dontSendNotification);
}

AudioBackend SystemScreen::selectedBackend() const {
  const int id = backendBox_.getSelectedId();
  if (id >= 1 && id <= kAudioBackendCount) return static_cast<AudioBackend>(id - 1);
  return kDefaultAudioBackend;
}

std::string SystemScreen::selectedDeviceName() const {
  const int id = deviceBox_.getSelectedId();
  if (id >= 2) return deviceBox_.getItemText(id - 1).toStdString();
  return {};
}

int SystemScreen::selectedBufferSize() const {
  const int id = bufferBox_.getSelectedId();
  if (id >= 1 && id <= kAudioBufferSizeCount) return kAudioBufferSizes[id - 1];
  return kDefaultAudioBufferSize;
}

void SystemScreen::applyAudioSelection() {
  AudioBackendChoice choice;
  choice.backend = selectedBackend();
  choice.deviceName = selectedDeviceName();
  choice.bufferSize = selectedBufferSize();
  // AudioGlue logs the result (and the fallback reason) into the in-app log.
  context_.audio.applyAudioChoice(choice);
  rebuildBackendChoices();
  rebuildDeviceChoices();
  refresh();
  repaint();
}

void SystemScreen::refreshBackends() {
  context_.audio.rescanBackends();
  rebuildBackendChoices();
  rebuildDeviceChoices();
  refresh();
  repaint();
}

void SystemScreen::refresh() {
  const auto status = context_.link.status();
  const auto stats = status.stats;
  const auto audio = context_.audio.backendState();

  juce::String text;
  text << "LINK      "
       << (status.emulated ? "emulator transport (scripted Pico)"
                           : "spidev0.0 @ 1 MHz, mode 0")
       << "\n";
  text << "          " << (status.linkUp ? "UP" : "DOWN") << "   Pico session 0x"
       << juce::String::toHexString(static_cast<juce::int64>(status.picoSession))
       << "   Pi session 0x"
       << juce::String::toHexString(static_cast<juce::int64>(status.piSession)) << "\n";
  text << "          tx " << juce::String(static_cast<juce::int64>(stats.tx)) << "  rx "
       << juce::String(static_cast<juce::int64>(stats.rx)) << "  bad "
       << juce::String(static_cast<juce::int64>(stats.bad)) << "  dup "
       << juce::String(static_cast<juce::int64>(stats.dup)) << "  overruns "
       << juce::String(static_cast<juce::int64>(stats.overruns)) << "\n";
  text << "READY     "
       << (status.readyValid ? (status.ready ? "high" : "low")
                             : "not sampled (READY_IRQ needs the Pi GPIO character device)")
       << "\n";
  text << "CLOCK     " << juce::String(formatTempo(status.tempoX100)) << "   "
       << (status.rolling ? "rolling" : "stopped") << "   "
       << (status.clockPrimed ? "primed" : "cold") << "\n";
  text << "          offset " << juce::String(status.offsetUs, 1) << " us   latency "
       << juce::String(status.latencyUs, 1) << " us   skipped "
       << juce::String(static_cast<juce::int64>(status.clockSkipped)) << "   reordered "
       << juce::String(static_cast<juce::int64>(status.clockReordered)) << "\n";
  text << "TRANSPORT tick " << juce::String(static_cast<juce::int64>(status.tick))
       << "   " << juce::String(formatBarBeatTick(status.tick)) << "   shift "
       << (status.shift ? "held" : "up") << "   mode "
       << (status.manual ? "manual" : "local") << "\n";
  text << "AUDIO     " << context_.audio.backendDescription() << "\n";
  text << "          " << juce::String(audio.sampleRate, 0) << " Hz / "
       << juce::String(audio.blockSize) << " frames   xruns " << juce::String(audio.xruns)
       << "   rec " << (context_.audio.recording() ? "on" : "off") << "   drops "
       << juce::String(static_cast<juce::int64>(context_.audio.drops())) << "\n";
  text << "MIDI      " << context_.midi.describe() << "   routed "
       << juce::String(static_cast<juce::int64>(context_.midi.routedCount())) << "\n";

  auto describeSlot = [this](int index) {
    FxSlot& slot = context_.fx.slot(index);
    const auto path = slot.pluginPath();
    return path.isEmpty() ? juce::String("(empty)") : path;
  };
  text << "FX        A " << describeSlot(0) << "\n";
  text << "          B " << describeSlot(1) << "\n";

  if (statsCache_ != text) {
    statsCache_ = text;
    stats_.setText(text, false);
  }

  // Live device readout: the type/device actually driving the callback plus the
  // numbers the device reports, so a JACK server on another period/rate is
  // visible instead of assumed.
  juce::String live;
  if (audio.liveType == "emulator") {
    live << "LIVE  emulator generator   " << juce::String(audio.sampleRate, 0) << " Hz / "
         << juce::String(audio.blockSize) << " frames   no device";
  } else if (audio.liveType == "none") {
    live << "LIVE  no audio device";
  } else {
    live << "LIVE  " << audio.liveType << " / " << audio.liveDevice << "   "
         << juce::String(audio.sampleRate, 0) << " Hz / " << juce::String(audio.blockSize)
         << " frames   xruns " << juce::String(audio.xruns);
  }
  if (liveCache_ != live) {
    liveCache_ = live;
    audioLive_.setText(live, juce::dontSendNotification);
  }

  juce::String note;
  if (audio.note.isNotEmpty()) {
    note = "NOTE  " + audio.note;
  } else {
    note = "REQUEST  " + describeChoice(audio.choice) + "   ->   EFFECTIVE  " +
           juce::String(audioBackendLabel(audio.effective));
  }
  if (noteCache_ != note) {
    noteCache_ = note;
    audioNote_.setText(note, juce::dontSendNotification);
  }

  auto logText = AppLogger::instance().tail(120);
  if (logCache_ != logText) {
    logCache_ = logText;
    const bool atEnd = logTail_.getCaretPosition() >= logTail_.getTotalNumChars();
    logTail_.setText(logText, false);
    if (atEnd) logTail_.moveCaretToEnd();
  }
}

}  // namespace mnky::app
