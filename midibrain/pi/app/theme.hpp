#pragma once
// The mnkybox look and feel: flat, dark, one cyan accent, thin outlines and
// small corner radii, tuned for the 800 x 480 touch panel (stage use, no hover).
//
// Colours and metrics come from ui_style.hpp only; this class registers them as
// JUCE colour IDs (LookAndFeel::setColour) and draws the controls that the
// built-in LookAndFeel_V4 renders with gradients. Screens therefore keep using
// findColour(<standard id>) and never need a second palette.
//
// Lifetime: one instance, owned by the JUCEApplication (main.cpp), installed
// with LookAndFeel::setDefaultLookAndFeel() before the first widget is created
// and cleared after the window is gone.
#include <juce_gui_basics/juce_gui_basics.h>

#include "ui_style.hpp"

namespace mnky::app {

class MnkyLookAndFeel final : public juce::LookAndFeel_V4 {
 public:
  MnkyLookAndFeel();

  // System sans for everything (monospaced stays monospaced): no font files are
  // shipped, the platform default stack is used as-is.
  juce::Typeface::Ptr getTypefaceForFont(const juce::Font& font) override;

  // Flat track with a filled portion and a round thumb; the rotary pan knob is
  // an arc plus a needle.
  void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                        float sliderPos, float minSliderPos, float maxSliderPos,
                        juce::Slider::SliderStyle style, juce::Slider& slider) override;
  void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                        float sliderPosProportional, float rotaryStartAngle,
                        float rotaryEndAngle, juce::Slider& slider) override;
  int getSliderThumbRadius(juce::Slider& slider) override;
  juce::Label* createSliderTextBox(juce::Slider& slider) override;

  // Touch toggles: the whole button is the indicator, filled with the tick
  // colour (accent by default, red/amber for arm/solo/mute) when on.
  void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                        bool shouldDrawButtonAsHighlighted,
                        bool shouldDrawButtonAsDown) override;

  // Flat rounded buttons; the selected state is an accent fill with dark text.
  void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                            const juce::Colour& backgroundColour,
                            bool shouldDrawButtonAsHighlighted,
                            bool shouldDrawButtonAsDown) override;
  void drawButtonText(juce::Graphics& g, juce::TextButton& button,
                      bool shouldDrawButtonAsHighlighted,
                      bool shouldDrawButtonAsDown) override;
  juce::Font getTextButtonFont(juce::TextButton& button, int buttonHeight) override;

  // Flat tabs with an accent underline on the selected one.
  void drawTabButton(juce::TabBarButton& button, juce::Graphics& g, bool isMouseOver,
                     bool isMouseDown) override;
  void drawTabbedButtonBarBackground(juce::TabbedButtonBar& bar,
                                     juce::Graphics& g) override;
  void drawTabAreaBehindFrontButton(juce::TabbedButtonBar& bar, juce::Graphics& g, int w,
                                    int h) override;
  juce::Font getTabButtonFont(juce::TabBarButton& button, float height) override;
  int getTabButtonBestWidth(juce::TabBarButton& button, int tabDepth) override;
  int getTabButtonOverlap(int tabDepth) override;

 private:
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MnkyLookAndFeel)
};

}  // namespace mnky::app
