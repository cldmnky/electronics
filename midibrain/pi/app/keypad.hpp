#pragma once
// Reusable on-screen keyboard for project and take names. Keys are laid out in
// QWERTY rows plus digits, `-`/`_`, space, cursor movement, backspace, clear and
// OK/Cancel; every key is at least style::kTouch (44 px) in both directions at
// the 800 x 480 panel size. The component fills its parent screen when open.
#include <array>
#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "text_buffer.hpp"

namespace mnky::app {

class Keypad final : public juce::Component {
 public:
  Keypad();
  ~Keypad() override = default;

  // Opens over the current screen with the buffer pre-filled. `maxLength`
  // defaults to the shared 48-character project/take limit.
  void open(const juce::String& title, const juce::String& initial,
            int maxLength = TextBuffer::kDefaultMaxLength);
  void close();
  bool isOpen() const { return isVisible(); }

  // Return an empty string to accept and close, or an ASCII error message that
  // stays visible under the text (the dialog remains open).
  std::function<juce::String(const juce::String&)> onAccept;
  std::function<void()> onCancel;

  void paint(juce::Graphics& g) override;
  void resized() override;
  void mouseDown(const juce::MouseEvent& event) override;

 private:
  void openInitial(const juce::String& title, const juce::String& initial, int maxLength);
  juce::TextButton* addKey(const juce::String& label, std::function<void()> action);
  juce::Rectangle<int> panelBounds() const;
  void refreshPreview();

  TextBuffer buffer_;
  juce::String title_;
  juce::String error_;
  juce::OwnedArray<juce::TextButton> keys_;
  std::array<std::vector<juce::TextButton*>, 4> letterRows_;
  juce::TextButton* space_ = nullptr;
  juce::TextButton* left_ = nullptr;
  juce::TextButton* right_ = nullptr;
  juce::TextButton* backspace_ = nullptr;
  juce::TextButton* clear_ = nullptr;
  juce::TextButton* cancel_ = nullptr;
  juce::TextButton* ok_ = nullptr;

  JUCE_DECLARE_NON_COPYABLE(Keypad)
};

}  // namespace mnky::app
