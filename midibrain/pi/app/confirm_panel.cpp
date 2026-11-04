#include "confirm_panel.hpp"

#include "ui_style.hpp"

namespace mnky::app {

ConfirmPanel::ConfirmPanel() {
  cancel_.setButtonText("CANCEL");
  addAndMakeVisible(cancel_);
  addAndMakeVisible(confirm_);
  cancel_.onClick = [this] { close(); };
  confirm_.onClick = [this] {
    close();
    if (onConfirm) onConfirm();
  };
  setVisible(false);
}

void ConfirmPanel::open(const juce::String& title, const juce::String& message,
                        const juce::String& confirmLabel) {
  title_ = title;
  message_ = message;
  confirm_.setButtonText(confirmLabel);
  confirm_.setColour(juce::TextButton::buttonColourId, style::record.darker(0.25f));
  setVisible(true);
  toFront(true);
  resized();
  repaint();
}

void ConfirmPanel::close() { setVisible(false); }

juce::Rectangle<int> ConfirmPanel::panelBounds() const {
  const int width = juce::jmin(560, juce::jmax(420, getWidth() - 60));
  const int height = juce::jmin(220, juce::jmax(170, getHeight() - 80));
  return juce::Rectangle<int>(width, height).withCentre(getLocalBounds().getCentre());
}

void ConfirmPanel::paint(juce::Graphics& g) {
  g.fillAll(juce::Colours::black.withAlpha(0.62f));
  const auto panel = panelBounds();
  style::drawPanel(g, panel.toFloat());
  auto area = panel.reduced(16);
  g.setColour(style::text);
  g.setFont(style::font(17.0f, true));
  g.drawText(title_, area.removeFromTop(26), juce::Justification::centredLeft, true);
  g.setColour(style::textDim);
  g.setFont(style::font(13.0f));
  g.drawFittedText(message_, area, juce::Justification::topLeft, 3);
}

void ConfirmPanel::resized() {
  auto panel = panelBounds().reduced(16);
  auto buttons = panel.removeFromBottom(style::kTouch);
  confirm_.setBounds(buttons.removeFromRight(150));
  buttons.removeFromRight(8);
  cancel_.setBounds(buttons.removeFromRight(130));
}

}  // namespace mnky::app
