#include "keypad.hpp"

#include <utility>

#include "ui_style.hpp"

namespace mnky::app {

Keypad::Keypad() {
  const char* const digits = "1234567890";
  const char* const qwerty = "qwertyuiop";
  const char* const home = "asdfghjkl";
  const char* const bottom = "zxcvbnm-_";
  const char* const rows[4] = {digits, qwerty, home, bottom};
  for (int row = 0; row < 4; ++row) {
    for (const char* key = rows[row]; *key != '\0'; ++key) {
      const char character = *key;
      const auto text = juce::String::charToString(static_cast<juce::juce_wchar>(character));
      letterRows_[static_cast<std::size_t>(row)].push_back(
          addKey(text, [this, character] {
            if (buffer_.insert(character)) refreshPreview();
          }));
    }
  }

  // Cursor movement lives on the bottom row; backspace/clear/space are on the
  // action row, so the keyboard stays usable without a hardware keyboard.
  letterRows_[3].push_back(addKey("<", [this] {
    buffer_.moveLeft();
    refreshPreview();
  }));
  letterRows_[3].push_back(addKey(">", [this] {
    buffer_.moveRight();
    refreshPreview();
  }));

  space_ = addKey("SPACE", [this] {
    if (buffer_.insert(' ')) refreshPreview();
  });
  left_ = addKey("<", [this] {
    buffer_.moveLeft();
    refreshPreview();
  });
  right_ = addKey(">", [this] {
    buffer_.moveRight();
    refreshPreview();
  });
  backspace_ = addKey("BACK", [this] {
    if (buffer_.backspace()) refreshPreview();
  });
  clear_ = addKey("CLEAR", [this] {
    if (buffer_.clear()) refreshPreview();
  });
  cancel_ = addKey("CANCEL", [this] {
    close();
    if (onCancel) onCancel();
  });
  ok_ = addKey("OK", [this] {
    const auto result = onAccept ? onAccept(juce::String(buffer_.text())) : juce::String();
    if (result.isEmpty()) {
      close();
    } else {
      error_ = result;
      resized();
      repaint();
    }
  });

  setVisible(false);
  setWantsKeyboardFocus(false);
}

juce::TextButton* Keypad::addKey(const juce::String& label, std::function<void()> action) {
  auto* button = keys_.add(new juce::TextButton(label));
  button->onClick = std::move(action);
  addAndMakeVisible(button);
  return button;
}

void Keypad::open(const juce::String& title, const juce::String& initial, int maxLength) {
  buffer_ = TextBuffer(maxLength);
  buffer_.setText(initial.toStdString());
  title_ = title;
  error_.clear();
  setVisible(true);
  toFront(true);
  resized();
  refreshPreview();
}

void Keypad::close() {
  error_.clear();
  setVisible(false);
}

juce::Rectangle<int> Keypad::panelBounds() const {
  const int width = juce::jmin(760, juce::jmax(480, getWidth() - 24));
  const int height = juce::jmin(392, juce::jmax(320, getHeight() - 12));
  return juce::Rectangle<int>(width, height).withCentre(getLocalBounds().getCentre());
}

void Keypad::paint(juce::Graphics& g) {
  g.fillAll(juce::Colours::black.withAlpha(0.62f));
  const auto panel = panelBounds();
  style::drawPanel(g, panel.toFloat());
  auto area = panel.reduced(12);

  g.setColour(style::textDim);
  g.setFont(style::font(12.0f, true));
  g.drawText(title_, area.removeFromTop(18), juce::Justification::centredLeft, true);

  auto preview = area.removeFromTop(30);
  g.setColour(style::panelDark);
  g.fillRoundedRectangle(preview.toFloat(), style::kCornerRadius);
  g.setColour(style::outline);
  g.drawRoundedRectangle(preview.toFloat().reduced(0.5f), style::kCornerRadius,
                         style::kOutlineWidth);

  const auto font = style::font(18.0f, true);
  g.setFont(font);
  const auto text = juce::String(buffer_.text());
  const int textX = preview.getX() + 10;
  const int textY = preview.getCentreY();

  if (text.isEmpty()) {
    g.setColour(style::textDim);
    g.drawText("type a name", preview.reduced(10, 0), juce::Justification::centredLeft, true);
  } else {
    g.setColour(style::text);
    g.drawText(text, preview.reduced(10, 0), juce::Justification::centredLeft, true);
  }
  const int cursor = textX + (buffer_.cursor() == 0
                                  ? 0
                                  : juce::GlyphArrangement::getStringWidthInt(
                                        font, text.substring(0, buffer_.cursor())));
  g.setColour(style::accent);
  g.fillRect(cursor, textY - 9, 2, 18);

  if (error_.isNotEmpty()) {
    g.setColour(style::record);
    g.setFont(style::font(12.0f, true));
    g.drawText(error_, area.removeFromTop(16), juce::Justification::centredLeft, true);
  }
}

void Keypad::resized() {
  const auto panel = panelBounds();
  auto area = panel.reduced(12);
  area.removeFromTop(18);  // title
  area.removeFromTop(30);  // preview
  if (error_.isNotEmpty()) area.removeFromTop(16);
  area.removeFromTop(4);

  constexpr int kGap = 4;
  const int rowHeight = juce::jmax(style::kTouch, area.getHeight() / 5);
  auto placeRow = [&area, rowHeight](const std::vector<juce::TextButton*>& row) {
    auto rowArea = area.removeFromTop(rowHeight);
    if (row.empty()) return;
    const int count = static_cast<int>(row.size());
    const int usable = rowArea.getWidth() - kGap * (count - 1);
    const int width = juce::jmax(style::kTouch, usable / count);
    for (auto* button : row) {
      button->setBounds(rowArea.removeFromLeft(width));
      rowArea.removeFromLeft(kGap);
    }
  };

  for (const auto& row : letterRows_) placeRow(row);

  auto actions = area.removeFromTop(rowHeight);
  const std::vector<juce::TextButton*> actionButtons = {space_, left_, right_,
                                                        backspace_, clear_, cancel_, ok_};
  const int units = 2 + 6;  // space is twice as wide
  const int usable = actions.getWidth() - kGap * (static_cast<int>(actionButtons.size()) - 1);
  const int unit = juce::jmax(1, usable / units);
  for (auto* button : actionButtons) {
    const int width = button == space_ ? unit * 2 : unit;
    button->setBounds(actions.removeFromLeft(width));
    actions.removeFromLeft(kGap);
  }
}

void Keypad::mouseDown(const juce::MouseEvent& event) {
  // A tap outside the panel dismisses the keyboard like CANCEL.
  if (!panelBounds().contains(event.getPosition())) {
    close();
    if (onCancel) onCancel();
  }
}

void Keypad::refreshPreview() {
  repaint();
}

}  // namespace mnky::app
