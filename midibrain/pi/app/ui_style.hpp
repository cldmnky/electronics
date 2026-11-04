#pragma once
// Shared look, colours and touch metrics for the 800 x 480 landscape panel.
// Every interactive control keeps at least style::kTouch pixels in both
// directions, so a fingertip can hit it (the panel is touch-only).
// This is the single source for the flat dark theme: MnkyLookAndFeel
// (theme.hpp) registers these tokens as JUCE colour IDs and reads the metrics.
//
// Contrast is a measured property, not a taste: the look and feel picks text
// colours with contrastRatio() and the unit tests assert the floors
// (primary text >= 7:1, dim text and the accent >= 4.5:1, selected vs idle
// >= 3:1). Surfaces stay near-black (relative luminance <= 0.03).
#include <cmath>

#include <juce_gui_basics/juce_gui_basics.h>

namespace mnky::app::style {

inline constexpr int kTouch = 44;
inline constexpr int kMasterWidth = 112;

// Flat theme metrics: no gradients or bevels, one 1 px outline, small radii and
// a fat round thumb so a slider stays findable with a fingertip.
inline constexpr int kTabDepth = kTouch + 4;
inline constexpr float kCornerRadius = 3.0f;
inline constexpr float kOutlineWidth = 1.0f;
inline constexpr int kTrackThickness = 5;
inline constexpr int kThumbRadius = 11;

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

// WCAG 2.x relative luminance of an sRGB colour (0 = black, 1 = white).
inline float relativeLuminance(juce::Colour colour) {
  const auto channel = [](juce::uint8 value) {
    const float c = static_cast<float>(value) / 255.0f;
    return c <= 0.03928f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
  };
  return 0.2126f * channel(colour.getRed()) + 0.7152f * channel(colour.getGreen()) +
         0.0722f * channel(colour.getBlue());
}

// WCAG 2.x contrast ratio between two opaque colours (1 = identical, 21 = black
// on white). Used by the theme to choose the more legible text colour and by
// the unit tests to assert the theme's contrast floors.
inline float contrastRatio(juce::Colour a, juce::Colour b) {
  const float first = relativeLuminance(a);
  const float second = relativeLuminance(b);
  return (juce::jmax(first, second) + 0.05f) / (juce::jmin(first, second) + 0.05f);
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
  g.fillRoundedRectangle(area, style::kCornerRadius);
  g.setColour(style::outline);
  g.drawRoundedRectangle(area.reduced(0.5f), style::kCornerRadius, style::kOutlineWidth);
}

}  // namespace mnky::app::style
