#include "RoutingScreen.hpp"

#include "../logging.hpp"
#include "../midi_glue.hpp"
#include "../ui_style.hpp"

namespace mnky::app {
namespace {
// Screen chrome (the patchbay's own node/wire geometry is derived from its
// bounds in routing_model.hpp). The 16 channel toggles keep style::kTouch in
// both directions: 84 px label + 16 x 44 px = the 788 px content width.
constexpr int kMargin = 6;
constexpr int kMaskLabelWidth = 84;
constexpr int kResetWidth = 112;
constexpr int kGraphGap = 4;
constexpr int kEditorGap = 4;

juce::String deviceSummary(std::size_t count, const char* singular, const char* plural) {
  return juce::String(static_cast<int>(count)) + (count == 1 ? singular : plural);
}
}  // namespace

RoutingPatchbay::RoutingPatchbay(AppContext& context) : context_(context) {
  refresh();
}

void RoutingPatchbay::clearSelection() {
  if (selectedInput_ < 0) return;
  selectedInput_ = -1;
  repaint();
}

void RoutingPatchbay::refresh() {
  const int inputs = static_cast<int>(context_.midi.inputCount());
  const int outputs = static_cast<int>(context_.midi.outputCount());
  std::uint32_t signature = 0;
  for (int source = 0; source < routing::kInputCount; ++source) {
    for (int destination = 0; destination < routing::kOutputCount; ++destination) {
      if (context_.midi.routeEnabled(routing::kInputs[source],
                                     routing::kOutputs[destination])) {
        signature |= 1u << static_cast<unsigned>(source * routing::kOutputCount +
                                                 destination);
      }
    }
  }
  if (inputs == usbInputs_ && outputs == usbOutputs_ && signature == routeSignature_) return;
  usbInputs_ = inputs;
  usbOutputs_ = outputs;
  routeSignature_ = signature;
  repaint();
}

void RoutingPatchbay::toggleRoute(int source, int destination) {
  const auto input = routing::kInputs[source];
  const auto output = routing::kOutputs[destination];
  const bool enabled = !context_.midi.routeEnabled(input, output);
  context_.midi.setRoute(input, output, enabled);
  log(juce::String("routing: ") + routing::kInputNames[source] + " -> " +
      routing::kOutputNames[destination] + (enabled ? " ON" : " OFF"));
  repaint();
}

void RoutingPatchbay::mouseUp(const juce::MouseEvent& event) {
  const auto point = event.position;

  // Input node: select (or deselect) the source whose routes stay highlighted.
  for (int source = 0; source < routing::kInputCount; ++source) {
    if (!layout_.inputs[source].contains(point)) continue;
    selectedInput_ = selectedInput_ == source ? -1 : source;
    repaint();
    return;
  }

  // Output node: completes input-then-output. An output on its own cannot name
  // a route, so it is a no-op while no input is selected.
  for (int destination = 0; destination < routing::kOutputCount; ++destination) {
    if (!layout_.outputs[destination].contains(point)) continue;
    if (selectedInput_ >= 0) toggleRoute(selectedInput_, destination);
    return;
  }

  // Wire: toggle that pair and keep its input selected so the rest of its
  // routes stay visible.
  int source = 0;
  int destination = 0;
  if (routing::hitWire(layout_, point, source, destination)) {
    selectedInput_ = source;
    toggleRoute(source, destination);
    return;
  }

  if (selectedInput_ >= 0) {
    selectedInput_ = -1;
    repaint();
  }
}

void RoutingPatchbay::paintWire(juce::Graphics& g, int source, int destination, bool enabled,
                                bool selected) {
  const bool dimmed = selectedInput_ >= 0 && !selected;
  const auto& wire = layout_.wires[source][destination];

  juce::Colour colour;
  float thickness = layout_.wireThickness;
  if (enabled) {
    // An active route is the accent, full strength for the selected input and
    // dimmed for the others. Thickness doubles as the shape cue.
    colour = dimmed ? style::accent.withMultipliedAlpha(0.35f) : style::accent;
    thickness *= selected ? 1.6f : 1.0f;
  } else if (dimmed) {
    // Inactive routes are deliberately faint; while an input is selected they
    // recede further.
    colour = style::outline.withMultipliedAlpha(0.7f);
    thickness *= 0.7f;
  } else if (selected) {
    // The selected input's off routes stay visible as the toggles on offer.
    colour = style::textDim;
    thickness *= 0.8f;
  } else {
    colour = style::outline;
    thickness *= 0.7f;
  }

  g.setColour(colour);
  g.strokePath(wire.path, juce::PathStrokeType(thickness, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));

  if (enabled) {
    const float radius = thickness * 1.6f;
    g.fillEllipse(juce::Rectangle<float>(radius * 2.0f, radius * 2.0f)
                      .withCentre(wire.start));
    g.fillEllipse(
        juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(wire.end));
  }
}

void RoutingPatchbay::paintNode(juce::Graphics& g, juce::Rectangle<float> area,
                                const juce::String& name, const juce::String& detail,
                                bool selected, bool active, bool highlight,
                                bool showIndicator) {
  const auto textColour =
      selected ? style::panelDark : (active ? style::text : style::textDim);

  g.setColour(selected ? style::accent : style::panel);
  g.fillRoundedRectangle(area, style::kCornerRadius);
  // Selected input: accent fill plus a dark label (the theme's selected state).
  // A highlighted node: accent 2 px outline against the usual 1 px, so the
  // state is a shape change as well as a colour change.
  const bool accentOutline = selected || highlight;
  g.setColour(accentOutline ? style::accent : style::outline);
  g.drawRoundedRectangle(area.reduced(0.5f), style::kCornerRadius,
                         accentOutline ? 2.0f : style::kOutlineWidth);

  if (showIndicator) {
    const float size = area.getHeight() * 0.20f;
    const float inset = area.getHeight() * 0.18f;
    const auto square = juce::Rectangle<float>(size, size)
                            .withCentre({area.getX() + inset + size * 0.5f,
                                         area.getCentreY()});
    g.setColour(active ? style::accent : style::outline);
    if (active) {
      g.fillRoundedRectangle(square, 2.0f);
    } else {
      g.drawRoundedRectangle(square, 2.0f, style::kOutlineWidth);
    }
  }

  g.setColour(textColour);
  auto text = area.reduced(area.getHeight() * 0.18f, 0.0f);
  if (detail.isEmpty()) {
    g.setFont(style::font(juce::jlimit(12.0f, 16.0f, area.getHeight() * 0.32f), true));
    g.drawFittedText(name, text.toNearestInt(), juce::Justification::centred, 1);
    return;
  }

  g.setFont(style::font(juce::jlimit(12.0f, 15.0f, area.getHeight() * 0.30f), true));
  g.drawFittedText(name, text.removeFromTop(text.getHeight() * 0.58f).toNearestInt(),
                   juce::Justification::centred, 1);
  g.setFont(style::font(juce::jlimit(10.0f, 12.0f, area.getHeight() * 0.24f), true));
  g.drawFittedText(detail, text.toNearestInt(), juce::Justification::centred, 1);
}

void RoutingPatchbay::paint(juce::Graphics& g) {
  const auto panel = getLocalBounds().toFloat().reduced(0.5f);
  g.setColour(style::panelDark);
  g.fillRoundedRectangle(panel, style::kCornerRadius);
  g.setColour(style::outline);
  g.drawRoundedRectangle(panel, style::kCornerRadius, style::kOutlineWidth);

  if (layout_.nodeHeight <= 0.0f) return;

  // Four passes so the routes that matter are drawn last and stay visible at
  // crossings: inactive others, inactive selected, active others, active
  // selected.
  for (int pass = 0; pass < 4; ++pass) {
    for (int source = 0; source < routing::kInputCount; ++source) {
      const bool selected = selectedInput_ == source;
      for (int destination = 0; destination < routing::kOutputCount; ++destination) {
        const bool enabled = context_.midi.routeEnabled(routing::kInputs[source],
                                                        routing::kOutputs[destination]);
        if ((enabled ? 2 : 0) + (selected ? 1 : 0) != pass) continue;
        paintWire(g, source, destination, enabled, selected);
      }
    }
  }

  for (int source = 0; source < routing::kInputCount; ++source) {
    bool routed = false;
    for (int destination = 0; destination < routing::kOutputCount; ++destination) {
      routed = routed || context_.midi.routeEnabled(routing::kInputs[source],
                                                    routing::kOutputs[destination]);
    }
    paintNode(g, layout_.inputs[source], routing::kInputNames[source],
              routing::kInputs[source] == midi::Port::UsbIn
                  ? deviceSummary(static_cast<std::size_t>(usbInputs_), " DEVICE",
                                  " DEVICES")
                  : juce::String(),
              selectedInput_ == source, routed, selectedInput_ == source, false);
  }

  for (int destination = 0; destination < routing::kOutputCount; ++destination) {
    bool routed = false;
    for (int source = 0; source < routing::kInputCount; ++source) {
      routed = routed || context_.midi.routeEnabled(routing::kInputs[source],
                                                    routing::kOutputs[destination]);
    }
    const bool fromSelection =
        selectedInput_ >= 0 &&
        context_.midi.routeEnabled(routing::kInputs[selectedInput_],
                                   routing::kOutputs[destination]);
    // With an input selected only the routes it can toggle stay outlined; with
    // nothing selected, any routed output is highlighted.
    const bool highlight = selectedInput_ >= 0 ? fromSelection : routed;
    paintNode(g, layout_.outputs[destination], routing::kOutputNames[destination],
              routing::kOutputs[destination] == midi::Port::UsbOut
                  ? deviceSummary(static_cast<std::size_t>(usbOutputs_), " PORT", " PORTS")
                  : juce::String(),
              false, routed, highlight, true);
  }
}

void RoutingPatchbay::resized() {
  layout_ = routing::layoutFor(getLocalBounds().toFloat().reduced(0.5f));
}

RoutingScreen::RoutingScreen(AppContext& context)
    : Screen(context), patchbay_(context) {
  addAndMakeVisible(patchbay_);

  // RESET restores the documented default matrix (routing_model.hpp). The
  // channel filters and transpose below are separate editors and keep their
  // values.
  resetButton_.setButtonText("RESET");
  resetButton_.onClick = [this] { resetRoutes(); };
  addAndMakeVisible(resetButton_);

  for (int source = 0; source < routing::kInputCount; ++source) {
    maskLabel_[source].setText(routing::kInputNames[source], juce::dontSendNotification);
    maskLabel_[source].setFont(style::font(12.0f, true));
    maskLabel_[source].setColour(juce::Label::textColourId, style::text);
    addAndMakeVisible(maskLabel_[source]);

    transposeLabel_[source].setText("TRANSPOSE", juce::dontSendNotification);
    transposeLabel_[source].setFont(style::font(12.0f, true));
    transposeLabel_[source].setColour(juce::Label::textColourId, style::textDim);
    addAndMakeVisible(transposeLabel_[source]);

    for (int channel = 0; channel < kChannelCount; ++channel) {
      auto& button = mask_[source][channel];
      button.setButtonText(juce::String(channel + 1));
      button.setClickingTogglesState(true);
      button.onClick = [this, source] { applyMask(source); };
      addAndMakeVisible(button);
    }

    transpose_[source].setSliderStyle(juce::Slider::IncDecButtons);
    transpose_[source].setTextBoxStyle(juce::Slider::TextBoxLeft, false, 56, 22);
    transpose_[source].setRange(-24.0, 24.0, 1.0);
    transpose_[source].setValue(context_.midi.transpose(routing::kInputs[source]),
                                juce::dontSendNotification);
    transpose_[source].setTextValueSuffix(" st");
    transpose_[source].onValueChange = [this, source] { applyTranspose(source); };
    addAndMakeVisible(transpose_[source]);
  }

  // Reflect whatever the app configured at startup (the channel mask defaults
  // to all 16 channels).
  for (int source = 0; source < routing::kInputCount; ++source) syncMask(source);
}

void RoutingScreen::resetRoutes() {
  for (int source = 0; source < routing::kInputCount; ++source) {
    for (int destination = 0; destination < routing::kOutputCount; ++destination) {
      context_.midi.setRoute(routing::kInputs[source], routing::kOutputs[destination],
                             routing::kDefaultRoutes[source][destination]);
    }
  }
  patchbay_.clearSelection();
  // clearSelection() only repaints when the selection actually changed; the
  // matrix changed either way.
  patchbay_.repaint();
  log("routing: RESET to the documented default (TRS IN 1+2 -> USB OUT)");
}

void RoutingScreen::applyMask(int source) {
  std::uint16_t mask = 0;
  for (int channel = 0; channel < kChannelCount; ++channel) {
    if (mask_[source][channel].getToggleState()) {
      mask = static_cast<std::uint16_t>(mask | (1u << static_cast<unsigned>(channel)));
    }
  }
  context_.midi.setChannelMask(routing::kInputs[source], mask);
  syncMask(source);
}

void RoutingScreen::syncMask(int source) {
  const auto mask = context_.midi.channelMask(routing::kInputs[source]);
  for (int channel = 0; channel < kChannelCount; ++channel) {
    const bool enabled = (mask & (1u << static_cast<unsigned>(channel))) != 0;
    mask_[source][channel].setToggleState(enabled, juce::dontSendNotification);
  }
}

void RoutingScreen::applyTranspose(int source) {
  context_.midi.setTranspose(routing::kInputs[source],
                             static_cast<int>(transpose_[source].getValue()));
}

void RoutingScreen::refresh() {
  patchbay_.refresh();
  // A project load replaces the whole router behind these editors, so mirror
  // the live masks/transpose back into the widgets (notifications stay off).
  for (int source = 0; source < routing::kInputCount; ++source) {
    syncMask(source);
    transpose_[source].setValue(context_.midi.transpose(routing::kInputs[source]),
                                juce::dontSendNotification);
  }
}

void RoutingScreen::paint(juce::Graphics& g) {
  g.fillAll(style::background);
  style::drawCaption(g, captionArea_,
                     "ROUTING - tap a wire, or an input then an output");
}

void RoutingScreen::resized() {
  auto area = getLocalBounds().reduced(kMargin);

  auto header = area.removeFromTop(style::kTouch);
  resetButton_.setBounds(header.removeFromRight(kResetWidth));
  header.removeFromRight(8);
  captionArea_ = header;
  area.removeFromTop(kGraphGap);

  // Bottom editors: one 44 px channel-filter row per input, then one 44 px
  // transpose row. Both keep the touch minimum in either direction.
  auto editors = area.removeFromBottom(3 * style::kTouch + kEditorGap + style::kTouch);
  auto maskRows = editors.removeFromTop(3 * style::kTouch);
  for (int source = 0; source < routing::kInputCount; ++source) {
    auto row = maskRows.removeFromTop(style::kTouch);
    maskLabel_[source].setBounds(row.removeFromLeft(kMaskLabelWidth));
    for (int channel = 0; channel < kChannelCount; ++channel) {
      mask_[source][channel].setBounds(row.removeFromLeft(style::kTouch));
    }
  }

  editors.removeFromTop(kEditorGap);
  auto transposeRow = editors.removeFromTop(style::kTouch);
  const int groupWidth = transposeRow.getWidth() / routing::kInputCount;
  for (int source = 0; source < routing::kInputCount; ++source) {
    auto group = transposeRow.removeFromLeft(groupWidth);
    transposeLabel_[source].setBounds(group.removeFromLeft(kMaskLabelWidth));
    transpose_[source].setBounds(group);
  }

  area.removeFromBottom(kGraphGap);
  patchbay_.setBounds(area);
}

}  // namespace mnky::app
