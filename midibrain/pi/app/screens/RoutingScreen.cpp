#include "RoutingScreen.hpp"

#include <array>

#include "../midi_glue.hpp"
#include "../ui_style.hpp"

namespace mnky::app {
namespace {
constexpr std::array<midi::Port, 3> kSources = {midi::Port::TrsIn1, midi::Port::TrsIn2,
                                                midi::Port::UsbIn};
constexpr std::array<midi::Port, 3> kDestinations = {midi::Port::TrsOut1, midi::Port::TrsOut2,
                                                     midi::Port::UsbOut};
constexpr std::array<const char*, 3> kSourceNames = {"TRS IN 1", "TRS IN 2", "USB IN"};
constexpr std::array<const char*, 3> kDestinationNames = {"TRS OUT 1", "TRS OUT 2", "USB OUT"};
constexpr int kLabelWidth = 96;
constexpr int kMaskLabelWidth = 84;
}  // namespace

RoutingScreen::RoutingScreen(AppContext& context) : Screen(context) {
  for (int source = 0; source < kSourceCount; ++source) {
    sourceLabel_[source].setText(kSourceNames[static_cast<std::size_t>(source)],
                                 juce::dontSendNotification);
    sourceLabel_[source].setFont(style::font(13.0f, true));
    sourceLabel_[source].setColour(juce::Label::textColourId, style::text);
    addAndMakeVisible(sourceLabel_[source]);

    destLabel_[source].setText(kDestinationNames[static_cast<std::size_t>(source)],
                               juce::dontSendNotification);
    destLabel_[source].setJustificationType(juce::Justification::centred);
    destLabel_[source].setFont(style::font(13.0f, true));
    destLabel_[source].setColour(juce::Label::textColourId, style::textDim);
    addAndMakeVisible(destLabel_[source]);

    maskLabel_[source].setText("CH 1-16", juce::dontSendNotification);
    maskLabel_[source].setFont(style::font(12.0f, true));
    maskLabel_[source].setColour(juce::Label::textColourId, style::textDim);
    addAndMakeVisible(maskLabel_[source]);

    transposeLabel_[source].setText("TRANSPOSE", juce::dontSendNotification);
    transposeLabel_[source].setFont(style::font(12.0f, true));
    transposeLabel_[source].setColour(juce::Label::textColourId, style::textDim);
    addAndMakeVisible(transposeLabel_[source]);

    for (int destination = 0; destination < kSourceCount; ++destination) {
      auto& button = route_[source][destination];
      button.setButtonText("OFF");
      button.setColour(juce::TextButton::buttonColourId, style::panelDark);
      button.onClick = [this, source, destination] { applyRoute(source, destination); };
      addAndMakeVisible(button);
    }

    for (int channel = 0; channel < kChannelCount; ++channel) {
      auto& button = mask_[source][channel];
      button.setButtonText(juce::String(channel + 1));
      button.setClickingTogglesState(true);
      button.setColour(juce::TextButton::buttonColourId, style::panelDark);
      button.onClick = [this, source] { applyMask(source); };
      addAndMakeVisible(button);
    }

    transpose_[source].setSliderStyle(juce::Slider::IncDecButtons);
    transpose_[source].setTextBoxStyle(juce::Slider::TextBoxLeft, false, 56, 22);
    transpose_[source].setRange(-24.0, 24.0, 1.0);
    transpose_[source].setValue(context_.midi.transpose(kSources[source]),
                                juce::dontSendNotification);
    transpose_[source].setTextValueSuffix(" st");
    transpose_[source].onValueChange = [this, source] { applyTranspose(source); };
    addAndMakeVisible(transpose_[source]);
  }

  // Reflect whatever the app configured at startup (routes default to off, the
  // channel mask defaults to all 16 channels).
  for (int source = 0; source < kSourceCount; ++source) {
    syncMask(source);
    for (int destination = 0; destination < kSourceCount; ++destination) {
      syncRoute(source, destination);
    }
  }
}

void RoutingScreen::applyRoute(int source, int destination) {
  const auto src = kSources[static_cast<std::size_t>(source)];
  const auto dst = kDestinations[static_cast<std::size_t>(destination)];
  context_.midi.setRoute(src, dst, !context_.midi.routeEnabled(src, dst));
  syncRoute(source, destination);
}

void RoutingScreen::syncRoute(int source, int destination) {
  const bool enabled =
      context_.midi.routeEnabled(kSources[static_cast<std::size_t>(source)],
                                 kDestinations[static_cast<std::size_t>(destination)]);
  auto& button = route_[source][destination];
  button.setButtonText(enabled ? "ON" : "OFF");
  button.setColour(juce::TextButton::buttonColourId,
                   enabled ? style::accent.darker(0.4f) : style::panelDark);
}

void RoutingScreen::applyMask(int source) {
  const auto src = kSources[static_cast<std::size_t>(source)];
  std::uint16_t mask = 0;
  for (int channel = 0; channel < kChannelCount; ++channel) {
    if (mask_[source][channel].getToggleState()) {
      mask = static_cast<std::uint16_t>(mask | (1u << static_cast<unsigned>(channel)));
    }
  }
  context_.midi.setChannelMask(src, mask);
  syncMask(source);
}

void RoutingScreen::syncMask(int source) {
  const auto mask = context_.midi.channelMask(kSources[static_cast<std::size_t>(source)]);
  for (int channel = 0; channel < kChannelCount; ++channel) {
    const bool enabled = (mask & (1u << static_cast<unsigned>(channel))) != 0;
    mask_[source][channel].setToggleState(enabled, juce::dontSendNotification);
    mask_[source][channel].setColour(juce::TextButton::buttonColourId,
                                     enabled ? style::accent.darker(0.55f) : style::panelDark);
  }
}

void RoutingScreen::applyTranspose(int source) {
  context_.midi.setTranspose(kSources[static_cast<std::size_t>(source)],
                             static_cast<int>(transpose_[source].getValue()));
}

void RoutingScreen::paint(juce::Graphics& g) {
  g.fillAll(style::background);
  style::drawCaption(g, getLocalBounds().reduced(6).removeFromTop(18),
                     "ROUTING  -  source x destination, channel filter, transpose");
}

void RoutingScreen::resized() {
  auto area = getLocalBounds().reduced(6);
  area.removeFromTop(20);

  auto header = area.removeFromTop(22);
  header.removeFromLeft(kLabelWidth);
  const int columnWidth = header.getWidth() / kSourceCount;
  for (int destination = 0; destination < kSourceCount; ++destination) {
    destLabel_[destination].setBounds(header.removeFromLeft(columnWidth));
  }

  for (int source = 0; source < kSourceCount; ++source) {
    auto row = area.removeFromTop(48);
    sourceLabel_[source].setBounds(row.removeFromLeft(kLabelWidth));
    for (int destination = 0; destination < kSourceCount; ++destination) {
      route_[source][destination].setBounds(row.removeFromLeft(columnWidth).reduced(2, 2));
    }
  }

  area.removeFromTop(6);
  for (int source = 0; source < kSourceCount; ++source) {
    auto row = area.removeFromTop(style::kTouch);
    maskLabel_[source].setBounds(row.removeFromLeft(kMaskLabelWidth));
    for (int channel = 0; channel < kChannelCount; ++channel) {
      mask_[source][channel].setBounds(row.removeFromLeft(style::kTouch));
    }
  }

  area.removeFromTop(8);
  auto transposeRow = area.removeFromTop(48);
  const int groupWidth = transposeRow.getWidth() / kSourceCount;
  for (int source = 0; source < kSourceCount; ++source) {
    auto group = transposeRow.removeFromLeft(groupWidth).reduced(2, 0);
    transposeLabel_[source].setBounds(group.removeFromLeft(84));
    transpose_[source].setBounds(group);
  }
}

}  // namespace mnky::app
