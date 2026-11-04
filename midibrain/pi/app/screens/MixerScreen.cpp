#include "MixerScreen.hpp"

#include <cmath>

#include "../audio_glue.hpp"
#include "../logic.hpp"
#include "../mixer_model.hpp"

namespace mnky::app {
namespace {
constexpr int kStripWidth = style::kFaderWidth;
}  // namespace

MixerStrip::MixerStrip(MixerModel& model, int index) : model_(model), index_(index) {
  name_.setText("IN " + juce::String(index + 1).paddedLeft('0', 2), juce::dontSendNotification);
  name_.setJustificationType(juce::Justification::centred);
  name_.setFont(style::font(13.0f, true));
  name_.setColour(juce::Label::textColourId, style::textDim);
  addAndMakeVisible(name_);

  gain_.setSliderStyle(juce::Slider::LinearVertical);
  gain_.setTextBoxStyle(juce::Slider::TextBoxBelow, false, kStripWidth - 14, 15);
  gain_.setRange(static_cast<double>(kMinGainDb), static_cast<double>(kMaxGainDb), 0.1);
  gain_.setSkewFactorFromMidPoint(-12.0);
  gain_.setValue(model_.gainDb(index_), juce::dontSendNotification);
  gain_.setNumDecimalPlacesToDisplay(1);
  gain_.setTextValueSuffix(" dB");
  gain_.onValueChange = [this] {
    model_.setGainDb(index_, static_cast<float>(gain_.getValue()));
  };
  addAndMakeVisible(gain_);

  pan_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
  pan_.setTextBoxStyle(juce::Slider::TextBoxBelow, false, kStripWidth - 14, 15);
  pan_.setRange(0.0, 1.0, 0.01);
  pan_.setValue(model_.pan(index_), juce::dontSendNotification);
  pan_.textFromValueFunction = [](double value) {
    const int percent = static_cast<int>(std::lround((value - 0.5) * 200.0));
    if (percent == 0) return juce::String("C");
    return juce::String(percent < 0 ? "L" : "R") + juce::String(std::abs(percent));
  };
  pan_.valueFromTextFunction = [](const juce::String& text) {
    const auto trimmed = text.trim().toUpperCase();
    if (trimmed.startsWithChar('L')) return 0.5 - trimmed.substring(1).getDoubleValue() / 200.0;
    if (trimmed.startsWithChar('R')) return 0.5 + trimmed.substring(1).getDoubleValue() / 200.0;
    return 0.5;
  };
  pan_.onValueChange = [this] {
    model_.setPan(index_, static_cast<float>(pan_.getValue()));
  };
  addAndMakeVisible(pan_);

  for (int send = 0; send < audio::kNumSends; ++send) {
    auto& label = sendLabel_[send];
    label.setText(juce::String::charToString(static_cast<juce::juce_wchar>('A' + send)),
                  juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setFont(style::font(11.0f, true));
    label.setColour(juce::Label::textColourId, style::textDim);
    addAndMakeVisible(label);

    auto& slider = send_[send];
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setRange(0.0, 1.0, 0.01);
    slider.setValue(model_.send(index_, send), juce::dontSendNotification);
    slider.onValueChange = [this, send] {
      model_.setSend(index_, send, static_cast<float>(send_[send].getValue()));
    };
    addAndMakeVisible(slider);
  }

  mute_.setClickingTogglesState(true);
  mute_.setToggleState(model_.mute(index_), juce::dontSendNotification);
  mute_.setColour(juce::ToggleButton::tickColourId, style::muteOn);
  mute_.setColour(juce::ToggleButton::textColourId, style::text);
  mute_.onClick = [this] { model_.setMute(index_, mute_.getToggleState()); };
  addAndMakeVisible(mute_);

  solo_.setClickingTogglesState(true);
  solo_.setToggleState(model_.solo(index_), juce::dontSendNotification);
  solo_.setColour(juce::ToggleButton::tickColourId, style::soloOn);
  solo_.setColour(juce::ToggleButton::textColourId, style::text);
  solo_.onClick = [this] { model_.setSolo(index_, solo_.getToggleState()); };
  addAndMakeVisible(solo_);

  arm_.setClickingTogglesState(true);
  arm_.setToggleState(model_.arm(index_), juce::dontSendNotification);
  arm_.setColour(juce::ToggleButton::tickColourId, style::record);
  arm_.setColour(juce::ToggleButton::textColourId, style::text);
  arm_.onClick = [this] { model_.setArm(index_, arm_.getToggleState()); };
  addAndMakeVisible(arm_);
}

void MixerStrip::paint(juce::Graphics& g) {
  g.fillAll(selected_ ? style::panel.brighter(0.08f) : style::panel);
  g.setColour(selected_ ? style::accent : style::outline);
  g.drawRect(getLocalBounds(), selected_ ? 2 : 1);
}

void MixerStrip::resized() {
  auto area = getLocalBounds().reduced(4, 3);
  name_.setBounds(area.removeFromTop(16));
  gain_.setBounds(area.removeFromTop(104));
  pan_.setBounds(area.removeFromTop(52).reduced(4, 0));

  // Sends are touch targets too: one 44 px row each, with a 12 px letter.
  for (int send = 0; send < audio::kNumSends; ++send) {
    auto row = area.removeFromTop(style::kTouch);
    sendLabel_[send].setBounds(row.removeFromLeft(12));
    send_[send].setBounds(row);
    area.removeFromTop(2);
  }

  area.removeFromTop(4);
  mute_.setBounds(area.removeFromTop(style::kTouch));
  solo_.setBounds(area.removeFromTop(style::kTouch));
  arm_.setBounds(area.removeFromTop(style::kTouch));
}

void MixerStrip::setSelected(bool selected) {
  if (selected_ == selected) return;
  selected_ = selected;
  repaint();
}

MasterStrip::MasterStrip(MixerModel& model, AudioGlue& audio) : model_(model), audio_(audio) {
  name_.setText("MASTER", juce::dontSendNotification);
  name_.setJustificationType(juce::Justification::centred);
  name_.setFont(style::font(14.0f, true));
  name_.setColour(juce::Label::textColourId, style::text);
  addAndMakeVisible(name_);

  gain_.setSliderStyle(juce::Slider::LinearVertical);
  gain_.setTextBoxStyle(juce::Slider::TextBoxBelow, false, style::kMasterWidth - 24, 16);
  gain_.setRange(static_cast<double>(kMinGainDb), static_cast<double>(kMaxGainDb), 0.1);
  gain_.setSkewFactorFromMidPoint(-12.0);
  gain_.setValue(model_.masterDb(), juce::dontSendNotification);
  gain_.setNumDecimalPlacesToDisplay(1);
  gain_.setTextValueSuffix(" dB");
  gain_.onValueChange = [this] {
    model_.setMasterDb(static_cast<float>(gain_.getValue()));
  };
  addAndMakeVisible(gain_);
}

void MasterStrip::paint(juce::Graphics& g) {
  g.fillAll(style::panelDark);
  g.setColour(style::outline);
  g.drawRect(getLocalBounds(), 1);

  if (meterArea_.isEmpty()) return;
  auto meter = meterArea_;
  const auto barWidth = (meter.getWidth() - 6) / 2;
  for (int channel = 0; channel < 2; ++channel) {
    auto bar = meter.removeFromLeft(barWidth);
    meter.removeFromLeft(6);
    g.setColour(style::panel);
    g.fillRect(bar);
    g.setColour(style::outline);
    g.drawRect(bar, 1);

    const float fraction = meterFraction(meter_[channel]);
    auto filled = bar.reduced(1).toFloat();
    filled.setTop(filled.getBottom() - filled.getHeight() * fraction);
    g.setColour(fraction > 0.95f ? style::record : style::ok);
    g.fillRect(filled);
  }
}

void MasterStrip::resized() {
  auto area = getLocalBounds().reduced(6, 4);
  name_.setBounds(area.removeFromTop(20));
  gain_.setBounds(area.removeFromTop(150));
  area.removeFromTop(8);
  meterArea_ = area.removeFromTop(area.getHeight() - 8);
}

void MasterStrip::refresh() {
  meter_[0] = audio_.peak(0);
  meter_[1] = audio_.peak(1);
  if (!meterArea_.isEmpty()) repaint(meterArea_);
}

MixerScreen::MixerScreen(AppContext& context)
    : Screen(context), master_(context.mixer, context.audio) {
  viewport_.setScrollBarsShown(false, true, false, true);
  viewport_.setViewedComponent(&stripHolder_, false);
  viewport_.setScrollBarThickness(16);
  addAndMakeVisible(viewport_);
  addAndMakeVisible(master_);

  for (int index = 0; index < kNumStrips; ++index) {
    auto* strip = strips_.add(new MixerStrip(context_.mixer, index));
    stripHolder_.addAndMakeVisible(strip);
  }
}

void MixerScreen::resized() {
  auto area = getLocalBounds();
  master_.setBounds(area.removeFromRight(style::kMasterWidth));
  viewport_.setBounds(area);

  const int availableHeight = juce::jmax(0, area.getHeight() - viewport_.getScrollBarThickness());
  stripHolder_.setSize(kNumStrips * kStripWidth, availableHeight);
  for (int index = 0; index < strips_.size(); ++index) {
    strips_[index]->setBounds(index * kStripWidth, 0, kStripWidth, availableHeight);
  }
}

void MixerScreen::refresh() {
  master_.refresh();
}

void MixerScreen::setSelectedStrip(int index) {
  for (int i = 0; i < strips_.size(); ++i) {
    strips_[i]->setSelected(i == index);
  }
  if (index >= 0 && index < strips_.size()) {
    const int centre = index * kStripWidth - (viewport_.getMaximumVisibleWidth() - kStripWidth) / 2;
    viewport_.setViewPosition(juce::jmax(0, centre), viewport_.getViewPositionY());
  }
}

}  // namespace mnky::app
