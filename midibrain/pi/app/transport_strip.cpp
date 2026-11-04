#include "transport_strip.hpp"

#include "app_state.hpp"
#include "link_client.hpp"
#include "logic.hpp"
#include "ui_style.hpp"

namespace mnky::app {

TransportStrip::TransportStrip(AppState& state, LinkClient& link)
    : state_(state), link_(link) {
  click_.setButtonText("CLICK");
  click_.setClickingTogglesState(true);
  click_.setColour(juce::TextButton::buttonOnColourId, style::accent);
  click_.setTooltip("Metronome on/off (click.enabled in the session file)");
  click_.onClick = [this] {
    state_.clickEnabled.store(click_.getToggleState());
    refresh();
  };
  addAndMakeVisible(click_);
  refresh();
}

void TransportStrip::refresh() {
  const auto status = link_.status();
  const bool recording = state_.recording.load();
  const char* state = recording ? "REC" : (status.rolling ? "ROLL" : "STOP");
  const std::string position =
      formatBarBeatTick(status.tick) + " / " + formatDuration(tickSeconds(status.tick,
                                                                          status.tempoX100));
  const std::string tempo = formatTempo(status.tempoX100);
  const bool dirty = state_.dirty.load();
  const bool clickOn = state_.clickEnabled.load();

  if (position != positionText_ || tempo != tempoText_ || state != stateText_ ||
      dirty != dirty_ || clickOn != clickOn_) {
    stateText_ = state;
    positionText_ = position;
    tempoText_ = tempo;
    dirty_ = dirty;
    clickOn_ = clickOn;
    if (click_.getToggleState() != clickOn) {
      click_.setToggleState(clickOn, juce::dontSendNotification);
    }
    repaint();
  }
}

void TransportStrip::paint(juce::Graphics& g) {
  g.fillAll(style::panelDark);
  g.setColour(style::outline);
  g.drawHorizontalLine(getHeight() - 1, 0.0f, static_cast<float>(getWidth()));

  auto area = getLocalBounds().reduced(6, 0);
  const auto stateColour = stateText_ == "REC"   ? style::record
                           : stateText_ == "ROLL" ? style::accent
                                                  : style::textDim;
  g.setColour(stateColour);
  g.setFont(style::font(12.0f, true));
  g.drawText(stateText_, area.removeFromLeft(52), juce::Justification::centredLeft, true);
  g.setColour(style::text);
  g.setFont(style::font(13.0f, true));
  g.drawText(positionText_, area.removeFromLeft(210), juce::Justification::centredLeft, true);
  g.setColour(style::textDim);
  g.setFont(style::font(12.0f));
  g.drawText(tempoText_, area.removeFromLeft(100), juce::Justification::centredLeft, true);

  // Dirty dot at the far right (the tab and SAVE button carry the same flag).
  const int dotX = getWidth() - 12;
  if (dirty_) {
    g.setColour(style::accent);
    g.fillEllipse(static_cast<float>(dotX), static_cast<float>(getHeight() / 2 - 4), 8.0f,
                  8.0f);
  }
}

void TransportStrip::resized() {
  auto area = getLocalBounds().reduced(4, 2);
  area.removeFromRight(16);  // dirty dot
  click_.setBounds(area.removeFromRight(64));
  click_.setColour(juce::TextButton::textColourOnId, style::panelDark);
}

}  // namespace mnky::app
