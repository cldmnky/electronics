#pragma once
// Shared look, colours and touch metrics for the 800 x 480 landscape panel.
// Every interactive control keeps at least style::kTouch pixels in both
// directions, so a fingertip can hit it (the panel is touch-only).
#include <juce_gui_basics/juce_gui_basics.h>

namespace mnky::app::style {

inline constexpr int kTouch = 44;
inline constexpr int kMasterWidth = 112;

inline const juce::Colour background{0xff15171b};
inline const juce::Colour panel{0xff1e2127};
inline const juce::Colour panelDark{0xff101216};
inline const juce::Colour outline{0xff2f343d};
inline const juce::Colour text{0xffe6e8ec};
inline const juce::Colour textDim{0xff9aa1ad};
inline const juce::Colour accent{0xff4fc3f7};
inline const juce::Colour record{0xffef5350};
inline const juce::Colour soloOn{0xffffc107};
inline const juce::Colour muteOn{0xffff7043};
inline const juce::Colour ok{0xff66bb6a};

inline juce::Font font(float height, bool bold = false) {
  return juce::Font(juce::FontOptions(height, bold ? juce::Font::bold : juce::Font::plain));
}

// Small caption used above groups of controls.
inline void drawCaption(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& text) {
  g.setColour(style::textDim);
  g.setFont(font(12.0f, true));
  g.drawText(text, area, juce::Justification::centredLeft, true);
}

// A rounded panel behind a group of controls.
inline void drawPanel(juce::Graphics& g, juce::Rectangle<float> area) {
  g.setColour(style::panel);
  g.fillRoundedRectangle(area, 4.0f);
  g.setColour(style::outline);
  g.drawRoundedRectangle(area.reduced(0.5f), 4.0f, 1.0f);
}

}  // namespace mnky::app::style
