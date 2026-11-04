// JUCE unit tests for the app's non-visual logic (logic.hpp) and for the flat
// theme's colour/contrast contract (theme.hpp, ui_style.hpp). Registered with
// the JUCE test framework and run by `mnkybox --unittests`, which is also the
// CTest entry `mnkybox_app_logic`. No audio, MIDI or link access; the theme
// checks draw into an offscreen image only.

#include <cmath>
#include <iostream>

#include <juce_core/juce_core.h>

#include "fx_slot.hpp"
#include "logic.hpp"
#include "theme.hpp"
#include "ui_style.hpp"

namespace mnky::app {
namespace {

// Offscreen drawing is anti-aliased and colour-managed, so compare with a
// small per-channel tolerance instead of exact equality.
bool colourNear(juce::Colour actual, juce::Colour expected, int tolerance = 2) {
  return std::abs(actual.getRed() - expected.getRed()) <= tolerance &&
         std::abs(actual.getGreen() - expected.getGreen()) <= tolerance &&
         std::abs(actual.getBlue() - expected.getBlue()) <= tolerance;
}

// Owner contrast floors, measured with style::contrastRatio (WCAG 2.x).
constexpr float kPrimaryTextContrast = 7.0f;    // body text, AAA
constexpr float kSecondaryTextContrast = 4.5f;  // micro-labels and dim text, AA
constexpr float kAccentContrast = 4.5f;         // the accent as essential UI colour
constexpr float kStateContrast = 3.0f;          // selected vs idle, non-text UI

class PanelLogicTests final : public juce::UnitTest {
 public:
  PanelLogicTests() : juce::UnitTest("mnkybox app panel and value logic", "mnkybox") {}

  void runTest() override {
    testActionTable();
    testControlDispatch();
    testTempo();
    testValueMapping();
    testFormatting();
  }

 private:
  static Intent control(std::uint8_t id, std::uint8_t value, bool shift = false,
                        int strip = 3) {
    return intentForControl(id, value, shift, PanelState{1, strip});
  }

  void testActionTable() {
    beginTest("action -> intent table");

    for (int i = 0; i <= static_cast<int>(midi::Action::EncoderSw); ++i) {
      const auto intent = intentForAction(static_cast<midi::Action>(i), PanelState{});
      if (i == static_cast<int>(midi::Action::None)) {
        expect(intent.kind == Intent::Kind::None, "None maps to None");
      } else {
        expect(intent.kind != Intent::Kind::None, "every action maps to work");
      }
    }

    const PanelState panel{2, 7};
    expect(intentForAction(midi::Action::Play, panel) == Intent{Intent::Kind::Play, 0});
    expect(intentForAction(midi::Action::PlayStart, panel) ==
           Intent{Intent::Kind::PlayStart, 0});
    expect(intentForAction(midi::Action::Stop, panel) == Intent{Intent::Kind::Stop, 0});
    expect(intentForAction(midi::Action::RecToggle, panel) ==
           Intent{Intent::Kind::RecToggle, 0});
    expect(intentForAction(midi::Action::OverdubToggle, panel) ==
           Intent{Intent::Kind::OverdubToggle, 0});
    expect(intentForAction(midi::Action::NavUp, panel) ==
           Intent{Intent::Kind::MoveTab, -1});
    expect(intentForAction(midi::Action::NavDown, panel) ==
           Intent{Intent::Kind::MoveTab, 1});
    expect(intentForAction(midi::Action::NavLeft, panel) ==
           Intent{Intent::Kind::MoveStrip, -1});
    expect(intentForAction(midi::Action::NavRight, panel) ==
           Intent{Intent::Kind::MoveStrip, 1});
    expect(intentForAction(midi::Action::EncoderSw, panel) ==
           Intent{Intent::Kind::ArmToggle, 7});
    expect(intentForAction(midi::Action::NavPress, panel) ==
           Intent{Intent::Kind::PlayStart, 0});
  }

  void testControlDispatch() {
    beginTest("control id dispatch");

    expect(control(mb::CTL_PLAY, 1).kind == Intent::Kind::Play);
    expect(control(mb::CTL_PLAY, 0).kind == Intent::Kind::None, "release is ignored");
    expect(control(mb::CTL_STOP, 1).kind == Intent::Kind::Stop);
    expect(control(mb::CTL_REC, 1).kind == Intent::Kind::RecToggle);
    expect(control(mb::CTL_OVER_DUB, 1).kind == Intent::Kind::OverdubToggle);
    expect(control(mb::CTL_NAV_UP, 1).kind == Intent::Kind::MoveTab);
    expect(control(mb::CTL_NAV_RIGHT, 1).amount == 1);
    expect(control(mb::CTL_ENCODER_SW, 1).kind == Intent::Kind::ArmToggle);
    expect(control(mb::CTL_SHIFT_EXIT, 1).kind == Intent::Kind::None,
           "SHIFT is a modifier, not an action");
    expect(control(99, 1).kind == Intent::Kind::None, "unknown id is ignored");

    // Encoder detents: the signed payload is the magnitude, SHIFT scales it.
    expect(control(mb::CTL_ENCODER_TURN, 3) ==
           Intent{Intent::Kind::TempoStep, 3 * kTempoStepCoarseX100});
    expect(control(mb::CTL_ENCODER_TURN, 2, true) ==
           Intent{Intent::Kind::TempoStep, 2 * kTempoStepFineX100});
    expect(control(mb::CTL_ENCODER_TURN, static_cast<std::uint8_t>(-1)) ==
           Intent{Intent::Kind::TempoStep, -kTempoStepCoarseX100});
    expect(control(mb::CTL_ENCODER_TURN, 0).kind == Intent::Kind::None);
  }

  void testTempo() {
    beginTest("tempo clamping and steps");

    expectEquals(clampTempoX100(12000), 12000);
    expectEquals(clampTempoX100(0), static_cast<int>(midi::UsbClock::kMinBpmX100));
    expectEquals(clampTempoX100(90000), static_cast<int>(midi::UsbClock::kMaxBpmX100));
    // 12.30 bpm is below the 20 bpm floor; a steady step from 120 bpm is not.
    expectEquals(clampTempoX100(1230), static_cast<int>(midi::UsbClock::kMinBpmX100));
    expectEquals(clampTempoX100(12000 + 12 * kTempoStepCoarseX100), 13200);
    expectWithinAbsoluteError(tickSeconds(48, 12000), 1.0, 1e-9);
    expectWithinAbsoluteError(tickSeconds(24, 6000), 1.0, 1e-9);
  }

  void testValueMapping() {
    beginTest("gain and meter mapping");

    expectWithinAbsoluteError(dbToGain(0.0f), 1.0f, 1e-6f);
    expectWithinAbsoluteError(dbToGain(-6.0f), 0.5011872f, 1e-4f);
    expectEquals(dbToGain(kMinGainDb), 0.0f);
    expectEquals(dbToGain(-200.0f), 0.0f);
    expectEquals(gainToDb(0.0f), kMinGainDb);

    expectWithinAbsoluteError(gainToDb(dbToGain(-12.0f)), -12.0f, 1e-4f);
    expectEquals(meterFraction(0.0f), 0.0f);
    expectEquals(meterFraction(1.0f), 1.0f);
    expect(meterFraction(0.01f) > 0.0f && meterFraction(0.01f) < 1.0f);
  }

  void testFormatting() {
    beginTest("readout formatting");

    expect(juce::String(formatTempo(12000)) == "120.00 BPM");
    expect(juce::String(formatTempo(2000)) == "20.00 BPM");
    expect(juce::String(formatBarBeatTick(0)) == "001:1:00");
    expect(juce::String(formatBarBeatTick(kTicksPerBar)) == "002:1:00");
    expect(juce::String(formatBarBeatTick(kTicksPerBar + kTicksPerBeat + 4)) == "002:2:04");

    expectEquals(moveIndex(0, -1, 5), 4);
    expectEquals(moveIndex(4, 1, 5), 0);
    expectEquals(moveIndex(2, 3, 5), 0);
    expectEquals(moveIndex(2, 0, 0), 0);

    expect(juce::String(formatBytes(512)) == "512 B");
    expect(juce::String(formatBytes(1536)) == "1.5 KiB");
    expect(juce::String(formatDuration(0.0)) == "00:00");
    expect(juce::String(formatDuration(61.0)) == "01:01");
  }
};

PanelLogicTests panelLogicTests;

// The FX screen's default plugin is the in-tree mnkygain build. Loading,
// parameter exposure, audio and state save/load are all message-thread work, so
// they are testable here without a display or an audio device.
class FxSlotTests final : public juce::UnitTest {
 public:
  FxSlotTests() : juce::UnitTest("mnkybox app fx slot", "mnkybox") {}

  void runTest() override {
#if defined(MNKY_DEFAULT_PLUGIN)
    beginTest("load in-tree mnkygain, params, process, state");
    const juce::File plugin(juce::String::fromUTF8(MNKY_DEFAULT_PLUGIN));
    if (!plugin.exists()) {
      logMessage("skipped: " + plugin.getFullPathName() + " has not been built");
      return;
    }

    FxSlot slot(0);
    expect(slot.load(plugin.getFullPathName(), 48000.0, audio::kBlockSize),
           "in-tree plugin loads: " + slot.lastError());
    if (!slot.loaded()) return;

    const auto params = slot.params();
    expectEquals(static_cast<int>(params.size()), 4);

    float left[audio::kBlockSize] = {};
    float right[audio::kBlockSize] = {};
    for (int i = 0; i < audio::kBlockSize; ++i) left[i] = right[i] = 0.25f;
    float* channels[2] = {left, right};
    slot.effect().process(channels, 2, audio::kBlockSize);
    expect(std::abs(left[static_cast<int>(audio::kBlockSize) - 1]) > 0.01f,
           "adapter passes the send bus through host.process()");

    if (!params.empty()) {
      expect(slot.setParam(params.front().id, params.front().min), "parameter write accepted");
    }

    const auto stateFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                               .getChildFile("mnkybox-fx-slot-test.state");
    stateFile.deleteFile();
    expect(slot.saveState(stateFile), "state saved: " + slot.lastError());
    expect(stateFile.existsAsFile());
    expect(slot.loadState(stateFile), "state loaded: " + slot.lastError());
    stateFile.deleteFile();

    slot.unload();
    expect(!slot.loaded());
#else
    beginTest("default plugin unavailable");
    logMessage("MNKY_DEFAULT_PLUGIN is not defined for this build");
#endif
  }
};

FxSlotTests fxSlotTests;

// The theme is the single palette: these checks keep MnkyLookAndFeel's colour
// IDs tied to ui_style.hpp and prove the flat controls draw with those tokens.
class ThemeTests final : public juce::UnitTest {
 public:
  ThemeTests() : juce::UnitTest("mnkybox app theme", "mnkybox") {}

  void runTest() override {
    testMetrics();
    testColourIds();
    testContrast();
    testFlatControlDrawing();
  }

 private:
  void testMetrics() {
    beginTest("touch and flat-theme metrics");
    expect(style::kTouch >= 44, "every touch target is at least 44 px");
    expect(style::kTabDepth >= style::kTouch, "the tab bar keeps the touch height");
    expect(style::kThumbRadius >= 10, "slider thumbs are fat");
    expect(style::kTrackThickness >= 4 && style::kTrackThickness < style::kTouch,
           "the track is a thin line, not a block");
    expect(style::kCornerRadius > 0.0f && style::kCornerRadius <= 6.0f,
           "corner radii stay small");
    expect(style::kOutlineWidth <= 1.5f, "outlines stay thin");
  }

  void expectToken(const MnkyLookAndFeel& look, int colourId, juce::Colour expected,
                   const char* what) {
    expect(look.findColour(colourId) == expected, what);
  }

  void testColourIds() {
    beginTest("registered colour ids match ui_style tokens");
    MnkyLookAndFeel look;

    expectToken(look, juce::ResizableWindow::backgroundColourId, style::background,
                "window background");
    expectToken(look, juce::DocumentWindow::textColourId, style::text, "window text");
    expectToken(look, juce::Label::textColourId, style::text, "label text");

    expectToken(look, juce::Slider::backgroundColourId, style::panelDark,
                "slider empty track");
    expectToken(look, juce::Slider::trackColourId, style::accent, "slider filled track");
    expectToken(look, juce::Slider::thumbColourId, style::text, "slider thumb");
    expectToken(look, juce::Slider::rotarySliderFillColourId, style::accent, "rotary arc");
    expectToken(look, juce::Slider::rotarySliderOutlineColourId, style::outline,
                "rotary arc background");
    expectToken(look, juce::Slider::textBoxTextColourId, style::text, "slider value text");

    expectToken(look, juce::TextButton::buttonColourId, style::panel, "idle button fill");
    expectToken(look, juce::TextButton::buttonOnColourId, style::accent,
                "selected button fill");
    expectToken(look, juce::TextButton::textColourOffId, style::text, "idle button text");
    expectToken(look, juce::TextButton::textColourOnId, style::panelDark,
                "selected button text");

    expectToken(look, juce::ToggleButton::textColourId, style::text, "toggle text");
    expectToken(look, juce::ToggleButton::tickColourId, style::accent, "toggle on fill");
    expectToken(look, juce::ToggleButton::tickDisabledColourId, style::outline,
                "toggle outline");

    expectToken(look, juce::TabbedButtonBar::tabOutlineColourId, style::outline,
                "tab outline");
    expectToken(look, juce::TabbedButtonBar::frontOutlineColourId, style::accent,
                "selected tab underline");
    expectToken(look, juce::TabbedButtonBar::tabTextColourId, style::textDim,
                "idle tab text");
    expectToken(look, juce::TabbedButtonBar::frontTextColourId, style::text,
                "selected tab text");
    expectToken(look, juce::TabbedComponent::backgroundColourId, style::background,
                "tab content background");

    expectToken(look, juce::ListBox::backgroundColourId, style::panelDark, "list background");
    expectToken(look, juce::ListBox::outlineColourId, style::outline, "list outline");
    expectToken(look, juce::TextEditor::backgroundColourId, style::panelDark,
                "text editor background");
    expectToken(look, juce::TextEditor::outlineColourId, style::outline,
                "text editor outline");

    // The font stack is the platform sans: no font file is shipped, but the
    // system typeface must resolve.
    expect(look.getTypefaceForFont(style::font(12.0f)) != nullptr,
           "system sans resolves for the default font");
  }

  void testContrast() {
    beginTest("measured contrast floors and state cues");
    MnkyLookAndFeel look;

    // Near-black surfaces and light text: the owner constraint in numbers.
    expect(style::relativeLuminance(style::background) <= 0.02f,
           "window background is near-black");
    expect(style::relativeLuminance(style::panel) <= 0.03f, "panels are near-black");
    expect(style::relativeLuminance(style::text) >= 0.7f, "primary text is light");

    // Primary text clears AAA on every dark surface.
    expect(style::contrastRatio(style::text, style::background) >= kPrimaryTextContrast,
           "primary text on the window background");
    expect(style::contrastRatio(style::text, style::panel) >= kPrimaryTextContrast,
           "primary text on a panel");
    expect(style::contrastRatio(style::text, style::panelDark) >= kPrimaryTextContrast,
           "primary text on a dark panel");

    // Secondary micro-labels clear AA: no mid-grey-on-black essential text.
    expect(style::contrastRatio(style::textDim, style::background) >=
               kSecondaryTextContrast,
           "dim text on the window background");
    expect(style::contrastRatio(style::textDim, style::panel) >= kSecondaryTextContrast,
           "dim micro-labels on a panel");
    expect(style::contrastRatio(style::textDim, style::panelDark) >= kSecondaryTextContrast,
           "dim text on a dark panel");

    // The single accent on the dark surfaces.
    expect(style::contrastRatio(style::accent, style::background) >= kAccentContrast,
           "the accent is readable on the window background");
    expect(style::contrastRatio(style::accent, style::panel) >= kAccentContrast,
           "the accent is readable on a panel");

    // Selected vs idle is a luminance flip, not a hue change: the bright fill
    // and its dark text keep the state visible in a monochrome view.
    const auto idleButton = look.findColour(juce::TextButton::buttonColourId);
    const auto selectedButton = look.findColour(juce::TextButton::buttonOnColourId);
    expect(style::contrastRatio(selectedButton, idleButton) >= kStateContrast,
           "selected button fill differs from idle");
    expect(style::relativeLuminance(selectedButton) >
               5.0f * style::relativeLuminance(idleButton),
           "selected button fill is brighter, so the state is not colour-only");
    expect(style::contrastRatio(style::text, idleButton) >= kPrimaryTextContrast,
           "idle button text is light on the dark fill");
    expect(style::contrastRatio(style::panelDark, selectedButton) >=
               kSecondaryTextContrast,
           "selected button text is dark on the bright fill");

    const auto idleToggle = look.findColour(juce::ToggleButton::tickDisabledColourId);
    const auto onToggle = look.findColour(juce::ToggleButton::tickColourId);
    expect(style::contrastRatio(onToggle, idleToggle) >= kStateContrast,
           "toggle on fill differs from the idle outline");
    expect(style::relativeLuminance(onToggle) >
               5.0f * style::relativeLuminance(style::panelDark),
           "toggle on fill is brighter than the idle fill");

    // The tab carries a shape marker (underline) as well as brighter text.
    expect(style::contrastRatio(look.findColour(juce::TabbedButtonBar::frontOutlineColourId),
                                look.findColour(juce::TabbedButtonBar::tabOutlineColourId)) >=
               kStateContrast,
           "selected tab underline differs from the idle outline");
    expect(style::contrastRatio(look.findColour(juce::TabbedButtonBar::frontTextColourId),
                                look.findColour(juce::TabbedButtonBar::tabTextColourId)) >=
               2.0f,
           "selected tab text is brighter than idle tab text");

    // Armed / solo / muted / ok: the same rule for the semantic fills, plus the
    // on-marker bar the toggle draws (shape cue).
    const auto expectStatusFill = [this](juce::Colour fill, const char* name) {
      const juce::String label(name);
      expect(style::contrastRatio(fill, style::panelDark) >= kSecondaryTextContrast,
             label + " fill separates from the dark idle fill and carries dark text");
      expect(style::relativeLuminance(fill) >
                 5.0f * style::relativeLuminance(style::panelDark),
             label + " fill is a brightness change, not only a hue change");
    };
    expectStatusFill(style::record, "record");
    expectStatusFill(style::soloOn, "solo");
    expectStatusFill(style::muteOn, "mute");
    expectStatusFill(style::ok, "ok");
  }

  void testFlatControlDrawing() {
    beginTest("flat controls draw the theme tokens");
    MnkyLookAndFeel look;
    // Components resolve their colour IDs through the default look and feel, so
    // install this instance for the draw calls and restore the previous one.
    auto* previous = &juce::Desktop::getInstance().getDefaultLookAndFeel();
    juce::LookAndFeel::setDefaultLookAndFeel(&look);

    juce::Image image(juce::Image::ARGB, 240, 100, true);
    juce::Graphics g(image);
    g.fillAll(style::background);

    juce::Slider slider;
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setBounds(0, 0, 120, 40);
    slider.setRange(0.0, 1.0);
    slider.setValue(0.5, juce::dontSendNotification);
    look.drawLinearSlider(g, 0, 0, 120, 40, 60.0f, 0.0f, 120.0f,
                          juce::Slider::LinearHorizontal, slider);
    expect(colourNear(image.getPixelAt(10, 20), style::accent),
           "the filled track is the accent token");
    expect(colourNear(image.getPixelAt(110, 20), style::panelDark),
           "the empty track is the dark token");
    expect(colourNear(image.getPixelAt(60, 20), style::text),
           "the round thumb is the light token");

    juce::Slider rotary;
    rotary.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    rotary.setBounds(120, 0, 60, 60);
    rotary.setRange(0.0, 1.0);
    rotary.setValue(0.5, juce::dontSendNotification);
    look.drawRotarySlider(g, 120, 0, 60, 60, 0.5f, -2.356194f, 2.356194f, rotary);
    expect(colourNear(image.getPixelAt(150, 18), style::text),
           "the rotary needle is the light token");

    juce::ToggleButton toggle("MUTE");
    toggle.setBounds(0, 50, 100, 44);
    toggle.setToggleState(true, juce::dontSendNotification);
    // Draw methods paint in the component's local coordinates.
    look.drawToggleButton(g, toggle, false, false);
    expect(colourNear(image.getPixelAt(20, 22), style::accent),
           "a selected toggle is filled with the accent");
    expect(colourNear(image.getPixelAt(10, 22), style::panelDark),
           "a selected toggle carries the dark on-marker bar (shape cue)");

    juce::TabbedButtonBar bar(juce::TabbedButtonBar::TabsAtTop);
    bar.setBounds(0, 0, 300, style::kTabDepth);
    bar.addTab("MIXER", style::background, -1);
    bar.addTab("ROUTING", style::background, -1);
    bar.setCurrentTabIndex(0);
    if (auto* tab = bar.getTabButton(0)) {
      look.drawTabButton(*tab, g, false, false);
      const auto area = tab->getActiveArea();
      expect(colourNear(image.getPixelAt(area.getCentreX(), area.getBottom() - 3),
                        style::accent),
             "the selected tab has an accent underline");
    } else {
      expect(false, "the tab bar exposes its first button");
    }

    juce::LookAndFeel::setDefaultLookAndFeel(previous);
  }
};

ThemeTests themeTests;

}  // namespace

// Runs only this app's tests (category "mnkybox") and reports to stdout, so the
// result is visible in a terminal and in CTest output.
int runLogicUnitTests() {
  juce::UnitTestRunner runner;
  runner.setAssertOnFailure(false);
  runner.setPassesAreLogged(false);
  runner.runTestsInCategory("mnkybox");

  int passes = 0;
  int failures = 0;
  for (int i = 0; i < runner.getNumResults(); ++i) {
    if (const auto* result = runner.getResult(i)) {
      passes += result->passes;
      failures += result->failures;
      for (const auto& message : result->messages) {
        std::cout << "FAIL " << result->unitTestName << " / "
                  << result->subcategoryName << ": " << message << std::endl;
      }
    }
  }

  std::cout << (failures == 0 ? "PASS " : "FAIL ") << "mnkybox app logic: " << passes
            << " checks, " << failures << " failures" << std::endl;
  return failures == 0 ? 0 : 1;
}

}  // namespace mnky::app
