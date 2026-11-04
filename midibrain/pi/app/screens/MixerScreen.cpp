#include "MixerScreen.hpp"

#include <cmath>

#include "../audio_glue.hpp"
#include "../logic.hpp"
#include "../mixer_model.hpp"

namespace mnky::app {
namespace {
// Value read-outs under the fader and the pan knob: wide enough for "-60.0 dB"
// inside the 76 px strip a bank gets at the 800 px panel width.
constexpr int kStripValueWidth = 58;
constexpr int kBankCount = 2;

static_assert(kNumStrips == kBankCount * kStripsPerBank,
              "bank buttons and strip count must agree");
}  // namespace

MixerStrip::MixerStrip(MixerModel& model, int index) : model_(model), index_(index) {
  name_.setText("IN " + juce::String(index + 1).paddedLeft('0', 2), juce::dontSendNotification);
  name_.setJustificationType(juce::Justification::centred);
  name_.setFont(style::font(13.0f, true));
  name_.setColour(juce::Label::textColourId, style::textDim);
  addAndMakeVisible(name_);

  gain_.setSliderStyle(juce::Slider::LinearVertical);
  gain_.setTextBoxStyle(juce::Slider::TextBoxBelow, false, kStripValueWidth, 15);
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
  pan_.setTextBoxStyle(juce::Slider::TextBoxBelow, false, kStripValueWidth, 15);
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
    label.setFont(style::font(12.0f, true));
    // A horizontally squashed letter renders as an unreadable blob on the
    // target panel: refuse the squash so a too-narrow column fails visibly.
    label.setMinimumHorizontalScale(1.0f);
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

  // Sends are touch targets too: one 44 px row each, with a 20 px letter
  // column. JUCE's fitted-text layout curtails a single letter to an ellipsis
  // in a narrower Label, so the column is not made any tighter than this.
  for (int send = 0; send < audio::kNumSends; ++send) {
    auto row = area.removeFromTop(style::kTouch);
    sendLabel_[send].setBounds(row.removeFromLeft(20));
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
  addAndMakeVisible(stripHolder_);
  addAndMakeVisible(master_);

  for (int index = 0; index < kNumStrips; ++index) {
    auto* strip = strips_.add(new MixerStrip(context_.mixer, index));
    stripHolder_.addAndMakeVisible(strip);
  }

  static const char* const kBankNames[kBankCount] = {"1-9", "10-18"};
  for (int bank = 0; bank < kBankCount; ++bank) {
    auto& button = bankButtons_[bank];
    button.setButtonText(kBankNames[bank]);
    button.onClick = [this, bank] { setBank(bank); };
    addAndMakeVisible(button);
  }

  layoutStrips();
  syncBankButtons();
}

void MixerScreen::resized() {
  auto area = getLocalBounds();

  // Right column: the bank switch on top of the always-visible master. Each
  // button keeps the full style::kTouch height and a 1 px gap to its neighbour.
  auto column = area.removeFromRight(style::kMasterWidth);
  auto bankRow = column.removeFromTop(style::kTouch);
  auto leftButton = bankRow.removeFromLeft(bankRow.getWidth() / 2);
  bankButtons_[0].setBounds(leftButton.withTrimmedRight(1));
  bankButtons_[1].setBounds(bankRow.withTrimmedLeft(1));
  master_.setBounds(column);

  stripHolder_.setBounds(area);
  layoutStrips();
}

void MixerScreen::layoutStrips() {
  const int width = stripHolder_.getWidth();
  const int height = stripHolder_.getHeight();

  // Column edges are distributed over the full width so the bank fills it
  // exactly; the odd pixels land on the left-hand strips instead of leaving a
  // gap that reads as a clipped strip.
  int edges[kStripsPerBank + 1] = {};
  for (int column = 0; column <= kStripsPerBank; ++column) {
    edges[column] = (width * column) / kStripsPerBank;
  }

  for (int index = 0; index < strips_.size(); ++index) {
    auto* strip = strips_[index];
    const bool visible = index / kStripsPerBank == bank_;
    strip->setVisible(visible);
    if (!visible) continue;
    const int column = index % kStripsPerBank;
    strip->setBounds(edges[column], 0, edges[column + 1] - edges[column], height);
  }
}

void MixerScreen::setBank(int bank) {
  const int clamped = juce::jlimit(0, kBankCount - 1, bank);
  if (clamped == bank_) return;
  bank_ = clamped;
  layoutStrips();
  syncBankButtons();
}

void MixerScreen::syncBankButtons() {
  for (int bank = 0; bank < kBankCount; ++bank) {
    bankButtons_[bank].setColour(juce::TextButton::buttonColourId,
                                 bank == bank_ ? style::accent.darker(0.5f) : style::panelDark);
  }
}

void MixerScreen::refresh() {
  master_.refresh();
}

void MixerScreen::setSelectedStrip(int index) {
  for (int i = 0; i < strips_.size(); ++i) {
    strips_[i]->setSelected(i == index);
  }
  // Panel navigation can walk past the bank edge: follow the selection so the
  // highlighted strip is always on screen.
  if (index >= 0 && index < strips_.size()) setBank(index / kStripsPerBank);
}

}  // namespace mnky::app
