#pragma once
// Editable single-line text buffer behind the touch keyboard. Pure C++ so the
// cursor/insert/delete/max-length rules are unit-tested without JUCE.
//
// Only printable ASCII (0x20..0x7e) is accepted: the touch keyboard offers no
// other characters and project/take names must stay safe as directory names on
// the Pi. A rejected character leaves the buffer and cursor unchanged.
#include <cstddef>
#include <string>
#include <string_view>

namespace mnky::app {

class TextBuffer {
 public:
  static constexpr int kDefaultMaxLength = 48;

  explicit TextBuffer(int maxLength = kDefaultMaxLength)
      : maxLength_(maxLength < 0 ? 0 : maxLength) {}

  const std::string& text() const { return text_; }
  int cursor() const { return cursor_; }
  int maxLength() const { return maxLength_; }

  // Replaces the text and puts the cursor at the end. Over-long input is
  // truncated; non-ASCII characters are dropped.
  void setText(std::string_view text);

  // Cursor movement clamps to 0..size(); a negative index clamps to 0.
  void setCursor(int index);
  void moveLeft();
  void moveRight();
  void home();
  void end();

  // Returns true when the buffer changed.
  bool insert(char character);
  bool insert(std::string_view characters);
  bool backspace();
  bool deleteForward();
  bool clear();

  bool empty() const { return text_.empty(); }

 private:
  static bool printableAscii(char character) {
    return character >= 0x20 && character <= 0x7e;
  }

  std::string text_;
  int cursor_ = 0;
  int maxLength_ = kDefaultMaxLength;
};

}  // namespace mnky::app
