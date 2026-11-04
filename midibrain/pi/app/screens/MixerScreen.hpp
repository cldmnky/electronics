#pragma once
// Mixer screen: 18 input strips (gain / pan / two FX sends / mute / solo / arm)
// plus the master strip. 800 x 480 landscape-first: the strips are split into
// two banks of nine so a bank always fits the panel without scrolling, and the
// master stays pinned on the right in both banks.
#include "Screen.hpp"

#include "../audio/engine.hpp"
#include "../ui_style.hpp"

namespace mnky::app {

class MixerModel;
class AudioGlue;

// 18 inputs over two banks. Nine strips plus the master column fill the 800 px
// panel exactly, so a bank never clips a strip or a label.
inline constexpr int kStripsPerBank = 9;

class MixerStrip final : public juce::Component {
 public:
  MixerStrip(MixerModel& model, int index);

  void paint(juce::Graphics& g) override;
  void resized() override;
  void refresh();
  void setSelected(bool selected);

 private:
  MixerModel& model_;
  const int index_;
  bool selected_ = false;

  juce::Label name_;
  juce::Slider gain_;
  juce::Slider pan_;
  juce::Label sendLabel_[audio::kNumSends];
  juce::Slider send_[audio::kNumSends];
  juce::ToggleButton mute_{"MUTE"};
  juce::ToggleButton solo_{"SOLO"};
  juce::ToggleButton arm_{"ARM"};

  JUCE_DECLARE_NON_COPYABLE(MixerStrip)
};

class MasterStrip final : public juce::Component {
 public:
  MasterStrip(MixerModel& model, AudioGlue& audio);

  void paint(juce::Graphics& g) override;
  void resized() override;
  void refresh();

 private:
  MixerModel& model_;
  AudioGlue& audio_;

  juce::Label name_;
  juce::Slider gain_;
  juce::Rectangle<int> meterArea_;
  float meter_[2] = {0.0f, 0.0f};

  JUCE_DECLARE_NON_COPYABLE(MasterStrip)
};

class MixerScreen final : public Screen {
 public:
  explicit MixerScreen(AppContext& context);

  void resized() override;
  void refresh() override;
  void setSelectedStrip(int index);

 private:
  void setBank(int bank);
  void layoutStrips();
  void syncBankButtons();

  juce::Component stripHolder_;
  juce::OwnedArray<MixerStrip> strips_;
  MasterStrip master_;
  juce::TextButton bankButtons_[2];
  int bank_ = 0;

  JUCE_DECLARE_NON_COPYABLE(MixerScreen)
};

}  // namespace mnky::app
