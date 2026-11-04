#pragma once
// Modal-looking confirmation panel for destructive project/take actions. It
// fills its parent screen with a dim scrim and keeps OK/Cancel at the 44 px
// touch height; the owner adds it as the last child and calls open().
#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

namespace mnky::app {

class ConfirmPanel final : public juce::Component {
 public:
  ConfirmPanel();
  ~ConfirmPanel() override = default;

  void open(const juce::String& title, const juce::String& message,
            const juce::String& confirmLabel);
  void close();
  bool isOpen() const { return isVisible(); }

  std::function<void()> onConfirm;

  void paint(juce::Graphics& g) override;
  void resized() override;

 private:
  juce::Rectangle<int> panelBounds() const;

  juce::String title_;
  juce::String message_;
  juce::TextButton cancel_;
  juce::TextButton confirm_;

  JUCE_DECLARE_NON_COPYABLE(ConfirmPanel)
};

}  // namespace mnky::app
