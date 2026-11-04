#pragma once
// FX screen: two send slots. Each slot loads one CLAP plugin (default: the
// in-tree mnkygain build), exposes the plugin's own parameters as sliders and
// saves/loads the plugin state. The send level itself is per strip on the Mixer
// screen; the return level lives here.
#include <memory>
#include <vector>

#include "Screen.hpp"

#include "../fx_slot.hpp"

namespace mnky::app {

class FxSlotPanel final : public juce::Component {
 public:
  FxSlotPanel(AppContext& context, int slotIndex);
  ~FxSlotPanel() override;

  void paint(juce::Graphics& g) override;
  void resized() override;
  void refresh();
  void loadDefaultIfEmpty();

 private:
  void choosePlugin();
  void loadPlugin(const juce::File& file);
  void ejectPlugin();
  void chooseStateFile(bool save);
  void rebuildParams();
  juce::File defaultStateFile() const;

  AppContext& context_;
  FxSlot& slot_;
  const int slotIndex_;

  juce::Label title_;
  juce::TextButton plugin_;
  juce::TextButton load_;
  juce::TextButton eject_;
  juce::TextButton saveState_;
  juce::TextButton loadState_;
  juce::Label returnLabel_;
  juce::Slider return_;
  juce::Viewport paramViewport_;
  juce::Component paramHolder_;
  juce::OwnedArray<juce::Label> paramLabels_;
  juce::OwnedArray<juce::Slider> paramSliders_;
  std::vector<clap_id> paramIds_;
  int paramLayoutWidth_ = 0;
  juce::Label status_;
  std::unique_ptr<juce::FileChooser> chooser_;

  JUCE_DECLARE_NON_COPYABLE(FxSlotPanel)
};

class FxScreen final : public Screen {
 public:
  explicit FxScreen(AppContext& context);

  void resized() override;
  void refresh() override;

  // Loads the default plugin into the first send when nothing is loaded yet.
  void loadDefaults();

 private:
  juce::OwnedArray<FxSlotPanel> slots_;

  JUCE_DECLARE_NON_COPYABLE(FxScreen)
};

}  // namespace mnky::app
