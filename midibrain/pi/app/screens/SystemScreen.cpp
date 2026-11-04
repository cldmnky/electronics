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
}  // namespace

SystemScreen::SystemScreen(AppContext& context) : Screen(context) {
  configureEditor(stats_, 12.0f);
  configureEditor(logTail_, 11.0f);
  addAndMakeVisible(stats_);
  addAndMakeVisible(logTail_);
  refresh();
}

void SystemScreen::resized() {
  auto area = getLocalBounds().reduced(6);
  stats_.setBounds(area.removeFromTop(area.getHeight() * 3 / 5));
  area.removeFromTop(4);
  logTail_.setBounds(area);
}

void SystemScreen::refresh() {
  const auto status = context_.link.status();
  const auto stats = status.stats;

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
  text << "          " << juce::String(context_.audio.sampleRate(), 0) << " Hz / "
       << juce::String(context_.audio.blockSize()) << " frames   rec "
       << (context_.audio.recording() ? "on" : "off") << "   drops "
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

  auto logText = AppLogger::instance().tail(120);
  if (logCache_ != logText) {
    logCache_ = logText;
    const bool atEnd = logTail_.getCaretPosition() >= logTail_.getTotalNumChars();
    logTail_.setText(logText, false);
    if (atEnd) logTail_.moveCaretToEnd();
  }
}

}  // namespace mnky::app
