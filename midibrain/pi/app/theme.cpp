#include "theme.hpp"

#include <cmath>

namespace mnky::app {
namespace {

// Text colour for a fill, chosen by measured contrast instead of a fixed hue
// rule: a selected state flips the fill bright and its text dark, so the state
// reads as a luminance change (never as a hue change alone).
bool brightFill(juce::Colour fill) {
  return style::contrastRatio(fill, style::panelDark) >=
         style::contrastRatio(fill, style::text);
}

juce::Colour textOn(juce::Colour fill) {
  return brightFill(fill) ? style::panelDark : style::text;
}

juce::Colour fade(juce::Colour colour, bool enabled) {
  return enabled ? colour : colour.withMultipliedAlpha(0.4f);
}

}  // namespace

MnkyLookAndFeel::MnkyLookAndFeel() {
  // Start from the dark scheme so any colour this class does not override is
  // dark too, then register every token the app draws with.
  setColourScheme(getMidnightColourScheme());

  setColour(juce::ResizableWindow::backgroundColourId, style::background);
  setColour(juce::DocumentWindow::textColourId, style::text);

  setColour(juce::Label::textColourId, style::text);
  setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
  setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);

  // Slider: dark empty track, accent filled portion, light round thumb.
  setColour(juce::Slider::backgroundColourId, style::panelDark);
  setColour(juce::Slider::trackColourId, style::accent);
  setColour(juce::Slider::thumbColourId, style::text);
  setColour(juce::Slider::rotarySliderFillColourId, style::accent);
  setColour(juce::Slider::rotarySliderOutlineColourId, style::outline);
  setColour(juce::Slider::textBoxTextColourId, style::text);
  setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
  setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
  setColour(juce::Slider::textBoxHighlightColourId, style::accent.withAlpha(0.35f));

  // Text buttons: idle panel fill, accent fill while selected, dark text on it.
  setColour(juce::TextButton::buttonColourId, style::panel);
  setColour(juce::TextButton::buttonOnColourId, style::accent);
  setColour(juce::TextButton::textColourOffId, style::text);
  setColour(juce::TextButton::textColourOnId, style::panelDark);

  // Toggles: the tick colour doubles as the "on" fill, so the screens keep
  // their semantic red/amber/orange for arm/solo/mute.
  setColour(juce::ToggleButton::textColourId, style::text);
  setColour(juce::ToggleButton::tickColourId, style::accent);
  setColour(juce::ToggleButton::tickDisabledColourId, style::outline);

  setColour(juce::TabbedButtonBar::tabOutlineColourId, style::outline);
  setColour(juce::TabbedButtonBar::frontOutlineColourId, style::accent);
  setColour(juce::TabbedButtonBar::tabTextColourId, style::textDim);
  setColour(juce::TabbedButtonBar::frontTextColourId, style::text);
  setColour(juce::TabbedComponent::backgroundColourId, style::background);
  setColour(juce::TabbedComponent::outlineColourId, style::outline);

  setColour(juce::ListBox::backgroundColourId, style::panelDark);
  setColour(juce::ListBox::outlineColourId, style::outline);
  setColour(juce::ListBox::textColourId, style::text);

  setColour(juce::TextEditor::backgroundColourId, style::panelDark);
  setColour(juce::TextEditor::outlineColourId, style::outline);
  setColour(juce::TextEditor::focusedOutlineColourId, style::accent);
  setColour(juce::TextEditor::textColourId, style::text);
  setColour(juce::TextEditor::highlightColourId, style::accent.withAlpha(0.35f));

  setColour(juce::ScrollBar::backgroundColourId, juce::Colours::transparentBlack);
  setColour(juce::ScrollBar::thumbColourId, style::outline);

  setColour(juce::PopupMenu::backgroundColourId, style::panel);
  setColour(juce::PopupMenu::textColourId, style::text);
  setColour(juce::PopupMenu::headerTextColourId, style::textDim);
  setColour(juce::PopupMenu::highlightedBackgroundColourId, style::accent);
  setColour(juce::PopupMenu::highlightedTextColourId, style::panelDark);

  setColour(juce::TooltipWindow::backgroundColourId, style::panel);
  setColour(juce::TooltipWindow::textColourId, style::text);
  setColour(juce::TooltipWindow::outlineColourId, style::outline);
}

juce::Typeface::Ptr MnkyLookAndFeel::getTypefaceForFont(const juce::Font& font) {
  // No font files ship with the app: the platform sans is the whole stack. A
  // named typeface that is not one of JUCE's placeholders falls back to it.
  const auto name = font.getTypefaceName();
  if (name == juce::Font::getDefaultSansSerifFontName() ||
      name == juce::Font::getDefaultMonospacedFontName()) {
    return juce::LookAndFeel_V4::getTypefaceForFont(font);
  }
  const juce::Font fallback(juce::FontOptions(juce::Font::getDefaultSansSerifFontName(),
                                              font.getTypefaceStyle(), font.getHeight()));
  return juce::LookAndFeel_V4::getTypefaceForFont(fallback);
}

int MnkyLookAndFeel::getSliderThumbRadius(juce::Slider& slider) {
  // A fat round thumb, capped so the track still has travel inside a 44 px row.
  return juce::jlimit(8, style::kThumbRadius,
                      juce::jmin(slider.getWidth(), slider.getHeight()) / 2);
}

juce::Label* MnkyLookAndFeel::createSliderTextBox(juce::Slider& slider) {
  auto* label = juce::LookAndFeel_V4::createSliderTextBox(slider);
  // Value read-out: compact bold digits, no box around them.
  label->setFont(style::font(12.0f, true));
  label->setJustificationType(juce::Justification::centred);
  label->setColour(juce::Label::textColourId,
                   slider.findColour(juce::Slider::textBoxTextColourId));
  label->setColour(juce::Label::backgroundColourId,
                   slider.findColour(juce::Slider::textBoxBackgroundColourId));
  label->setColour(juce::Label::outlineColourId,
                   slider.findColour(juce::Slider::textBoxOutlineColourId));
  return label;
}

void MnkyLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                                       float sliderPos, float minSliderPos, float maxSliderPos,
                                       juce::Slider::SliderStyle style, juce::Slider& slider) {
  const bool multiValue =
      style == juce::Slider::SliderStyle::TwoValueHorizontal ||
      style == juce::Slider::SliderStyle::TwoValueVertical ||
      style == juce::Slider::SliderStyle::ThreeValueHorizontal ||
      style == juce::Slider::SliderStyle::ThreeValueVertical;
  if (slider.isBar() || multiValue) {
    // Not used by the app; keep the built-in behaviour instead of drawing a
    // half-styled control.
    juce::LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos, minSliderPos,
                                           maxSliderPos, style, slider);
    return;
  }

  const bool enabled = slider.isEnabled();
  const auto emptyColour = fade(slider.findColour(juce::Slider::backgroundColourId), enabled);
  const auto filledColour = fade(slider.findColour(juce::Slider::trackColourId), enabled);
  const auto thumbColour = fade(slider.findColour(juce::Slider::thumbColourId), enabled);
  const float thickness = static_cast<float>(style::kTrackThickness);
  const float radius = static_cast<float>(getSliderThumbRadius(slider));

  if (slider.isHorizontal()) {
    const float centreY = static_cast<float>(y) + static_cast<float>(height) * 0.5f;
    const juce::Rectangle<float> track(static_cast<float>(x), centreY - thickness * 0.5f,
                                       static_cast<float>(width), thickness);
    g.setColour(emptyColour);
    g.fillRoundedRectangle(track, thickness * 0.5f);

    const float filledWidth = juce::jlimit(0.0f, track.getWidth(), sliderPos - track.getX());
    if (filledWidth > 0.5f) {
      g.setColour(filledColour);
      g.fillRoundedRectangle(track.withWidth(filledWidth), thickness * 0.5f);
    }

    g.setColour(thumbColour);
    g.fillEllipse(juce::Rectangle<float>(radius * 2.0f, radius * 2.0f)
                      .withCentre({sliderPos, centreY}));
  } else {
    const float centreX = static_cast<float>(x) + static_cast<float>(width) * 0.5f;
    const juce::Rectangle<float> track(centreX - thickness * 0.5f, static_cast<float>(y),
                                       thickness, static_cast<float>(height));
    g.setColour(emptyColour);
    g.fillRoundedRectangle(track, thickness * 0.5f);

    // A vertical fader fills from the bottom, like the level it controls.
    const float filledTop = juce::jlimit(track.getY(), track.getBottom(), sliderPos);
    if (track.getBottom() - filledTop > 0.5f) {
      g.setColour(filledColour);
      g.fillRoundedRectangle(track.withTop(filledTop), thickness * 0.5f);
    }

    g.setColour(thumbColour);
    g.fillEllipse(juce::Rectangle<float>(radius * 2.0f, radius * 2.0f)
                      .withCentre({centreX, sliderPos}));
  }
}

void MnkyLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                       float sliderPosProportional, float rotaryStartAngle,
                                       float rotaryEndAngle, juce::Slider& slider) {
  const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(3.0f);
  const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
  if (radius <= 1.0f) return;

  const bool enabled = slider.isEnabled();
  const float centreX = bounds.getCentreX();
  const float centreY = bounds.getCentreY();
  const float arcThickness = juce::jmax(2.0f, radius * 0.18f);
  const float arcRadius = radius - arcThickness * 0.5f;
  const float angle =
      rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

  juce::Path backgroundArc;
  backgroundArc.addCentredArc(centreX, centreY, arcRadius, arcRadius, 0.0f, rotaryStartAngle,
                              rotaryEndAngle, true);
  g.setColour(fade(slider.findColour(juce::Slider::rotarySliderOutlineColourId), enabled));
  g.strokePath(backgroundArc, juce::PathStrokeType(arcThickness, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

  if (std::abs(angle - rotaryStartAngle) > 1.0e-3f) {
    juce::Path valueArc;
    valueArc.addCentredArc(centreX, centreY, arcRadius, arcRadius, 0.0f, rotaryStartAngle, angle,
                           true);
    g.setColour(fade(slider.findColour(juce::Slider::rotarySliderFillColourId), enabled));
    g.strokePath(valueArc, juce::PathStrokeType(arcThickness, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));
  }

  // Needle from the centre to the arc: reads at a glance from stage distance.
  const float needleLength = juce::jmax(2.0f, arcRadius - arcThickness);
  const juce::Point<float> tip(centreX + std::sin(angle) * needleLength,
                               centreY - std::cos(angle) * needleLength);
  g.setColour(fade(slider.findColour(juce::Slider::thumbColourId), enabled));
  g.drawLine({centreX, centreY, tip.x, tip.y}, 2.0f);
}

void MnkyLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                       bool shouldDrawButtonAsHighlighted,
                                       bool shouldDrawButtonAsDown) {
  const bool on = button.getToggleState();
  const bool enabled = button.isEnabled();
  const auto onColour = button.findColour(juce::ToggleButton::tickColourId);
  auto fill = on ? onColour
                 : (shouldDrawButtonAsDown || shouldDrawButtonAsHighlighted
                        ? style::panel.brighter(0.08f)
                        : style::panelDark);

  const auto bounds = button.getLocalBounds().toFloat().reduced(style::kOutlineWidth * 0.5f);
  g.setColour(fade(fill, enabled));
  g.fillRoundedRectangle(bounds, style::kCornerRadius);

  g.setColour(fade(on ? onColour.darker(0.3f)
                      : button.findColour(juce::ToggleButton::tickDisabledColourId),
                   enabled));
  g.drawRoundedRectangle(bounds, style::kCornerRadius, style::kOutlineWidth);

  // Shape as well as colour: the on state carries a dark bar, so armed/muted/
  // solo survives a monochrome view or a colour-vision difference.
  if (on) {
    g.setColour(fade(textOn(fill), enabled));
    g.fillRect(juce::Rectangle<float>(bounds.getX() + 8.0f, bounds.getY() + 8.0f, 4.0f,
                                      bounds.getHeight() - 16.0f));
  }

  g.setColour(fade(textOn(fill), enabled));
  g.setFont(style::font(12.0f, true));
  g.drawFittedText(button.getButtonText().toUpperCase(), button.getLocalBounds(),
                   juce::Justification::centred, 1);
}

void MnkyLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                           const juce::Colour& backgroundColour,
                                           bool shouldDrawButtonAsHighlighted,
                                           bool shouldDrawButtonAsDown) {
  auto fill = backgroundColour;
  if (shouldDrawButtonAsDown) {
    fill = fill.brighter(0.16f);
  } else if (shouldDrawButtonAsHighlighted) {
    fill = fill.brighter(0.07f);
  }

  const auto bounds = button.getLocalBounds().toFloat().reduced(style::kOutlineWidth * 0.5f);
  g.setColour(fade(fill, button.isEnabled()));
  g.fillRoundedRectangle(bounds, style::kCornerRadius);

  // One thin outline, darker than a bright fill and the standard outline token
  // around a dark one.
  g.setColour(fade(brightFill(fill) ? fill.darker(0.3f) : style::outline,
                   button.isEnabled()));
  g.drawRoundedRectangle(bounds, style::kCornerRadius, style::kOutlineWidth);
}

void MnkyLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button,
                                     bool shouldDrawButtonAsHighlighted,
                                     bool shouldDrawButtonAsDown) {
  juce::ignoreUnused(shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);
  const auto fill = button.findColour(button.getToggleState()
                                          ? juce::TextButton::buttonOnColourId
                                          : juce::TextButton::buttonColourId);
  g.setColour(fade(textOn(fill), button.isEnabled()));
  g.setFont(getTextButtonFont(button, button.getHeight()));
  const int yIndent = juce::jmin(4, button.proportionOfHeight(0.3f));
  // A 4 px indent (not the corner radius) keeps a two-word label like "SAVE
  // STATE" whole in the 92 px FX buttons; drawFittedText still squashes a
  // longer label instead of clipping it.
  g.drawFittedText(button.getButtonText().toUpperCase(),
                   button.getLocalBounds().reduced(4, yIndent),
                   juce::Justification::centred, 1);
}

juce::Font MnkyLookAndFeel::getTextButtonFont(juce::TextButton& button, int buttonHeight) {
  juce::ignoreUnused(button);
  return style::font(juce::jlimit(11.0f, 14.0f, static_cast<float>(buttonHeight) * 0.32f), true);
}

void MnkyLookAndFeel::drawTabButton(juce::TabBarButton& button, juce::Graphics& g,
                                    bool isMouseOver, bool isMouseDown) {
  if (button.getTabbedButtonBar().getOrientation() != juce::TabbedButtonBar::TabsAtTop) {
    juce::LookAndFeel_V4::drawTabButton(button, g, isMouseOver, isMouseDown);
    return;
  }

  const auto area = button.getActiveArea();
  const bool front = button.isFrontTab();
  const bool pressed = isMouseOver || isMouseDown;

  g.setColour(style::background);
  g.fillRect(area);

  g.setColour(findColour(front ? juce::TabbedButtonBar::frontTextColourId
                               : (pressed ? juce::TabbedButtonBar::frontTextColourId
                                          : juce::TabbedButtonBar::tabTextColourId)));
  g.setFont(getTabButtonFont(button, static_cast<float>(area.getHeight())));
  g.drawFittedText(button.getButtonText().toUpperCase(), button.getTextArea(),
                   juce::Justification::centred, 1);

  if (front) {
    // Accent underline: the selected tab is the only saturated element of the
    // bar, which is what a touch user looks for (there is no hover).
    g.setColour(findColour(juce::TabbedButtonBar::frontOutlineColourId));
    g.fillRect(area.getX() + 12, area.getBottom() - 4, area.getWidth() - 24, 3);
  }
}

void MnkyLookAndFeel::drawTabbedButtonBarBackground(juce::TabbedButtonBar& bar,
                                                    juce::Graphics& g) {
  g.fillAll(style::background);
  g.setColour(style::outline);
  g.fillRect(0, bar.getHeight() - 1, bar.getWidth(), 1);
}

void MnkyLookAndFeel::drawTabAreaBehindFrontButton(juce::TabbedButtonBar&, juce::Graphics&,
                                                   int, int) {
  // The built-in look draws a gradient shadow here; the flat theme has none.
}

juce::Font MnkyLookAndFeel::getTabButtonFont(juce::TabBarButton& button, float height) {
  juce::ignoreUnused(button);
  return style::font(juce::jlimit(12.0f, 15.0f, height * 0.32f), true);
}

int MnkyLookAndFeel::getTabButtonBestWidth(juce::TabBarButton& button, int tabDepth) {
  // Big tabs: five of them still fit the 800 px panel, and each one is a
  // comfortable touch target.
  return juce::jmax(128, juce::LookAndFeel_V2::getTabButtonBestWidth(button, tabDepth));
}

int MnkyLookAndFeel::getTabButtonOverlap(int tabDepth) {
  juce::ignoreUnused(tabDepth);
  return 0;
}

}  // namespace mnky::app
