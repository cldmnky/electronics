#pragma once
// Recorder screen: transport readout driven by the Pico link (rolling flag,
// tick count, tempo), per-channel arm toggles, take list from the take
// directory, drop counter and a free-disk meter.
#include <functional>
#include <memory>
#include <vector>

#include <juce_audio_formats/juce_audio_formats.h>

#include "Screen.hpp"

#include "../confirm_panel.hpp"
#include "../keypad.hpp"

namespace mnky::app {

class RecorderScreen final : public Screen {
 public:
  explicit RecorderScreen(AppContext& context);

  void paint(juce::Graphics& g) override;
  void resized() override;
  void refresh() override;

  // Re-scan the take directory (after a take stops or the directory changes).
  void takeStateChanged();

 private:
  class TakeListModel final : public juce::ListBoxModel {
   public:
    struct Entry {
      juce::String name;
      int files = 0;
      double seconds = 0.0;
      juce::int64 bytes = 0;
    };

    void setEntries(std::vector<Entry> entries);
    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height,
                          bool rowIsSelected) override;
    void selectedRowsChanged(int lastRowSelected) override;

    const Entry* entry(int row) const;

    std::function<void(int)> onRowSelected;

   private:
    std::vector<Entry> entries_;
  };

  void chooseDirectory();
  void syncArmButtons();
  void scanTakes();
  void updateReadouts();
  void syncTakeButtons();
  void requestRenameTake();
  void requestDeleteTake();
  juce::File selectedTakeDirectory() const;

  juce::Label transport_;
  juce::Label clock_;
  juce::Label status_;
  juce::Label disk_;
  juce::OwnedArray<juce::TextButton> armButtons_;
  juce::TextButton startStop_;
  juce::TextButton directoryButton_;
  juce::TextButton renameTake_;
  juce::TextButton deleteTake_;
  // The model outlives the ListBox that points at it.
  TakeListModel takeModel_;
  juce::ListBox takeList_;
  juce::AudioFormatManager formats_;
  std::unique_ptr<juce::FileChooser> chooser_;
  Keypad keypad_;
  ConfirmPanel confirm_;
  int refreshCounter_ = 0;

  JUCE_DECLARE_NON_COPYABLE(RecorderScreen)
};

}  // namespace mnky::app
