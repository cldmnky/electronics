// JUCE unit tests for the app's non-visual logic (logic.hpp), the routing
// patchbay model (screens/routing_model.hpp) and for the flat theme's
// colour/contrast contract (theme.hpp, ui_style.hpp). Registered with the JUCE
// test framework and run by `mnkybox --unittests`, which is also the CTest
// entry `mnkybox_app_logic`. No audio, MIDI or link access; the theme checks
// draw into an offscreen image only.

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include <juce_core/juce_core.h>

#include "../session/session.hpp"
#include "audio_backend.hpp"
#include "fx_slot.hpp"
#include "logic.hpp"
#include "project_logic.hpp"
#include "screens/routing_model.hpp"
#include "text_buffer.hpp"
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

// The System screen's audio backend selector is a pure model: the same functions
// decide what the selector offers, which backend the one device manager is asked
// for, and what happens when the request cannot be honoured. No JUCE device is
// touched, so the checks run on a machine with no JACK and no audio server.
class AudioBackendTests final : public juce::UnitTest {
 public:
  AudioBackendTests() : juce::UnitTest("mnkybox audio backend selection", "mnkybox") {}

  void runTest() override {
    testKeysAndLabels();
    testAvailabilityAndFallback();
    testBufferSizes();
    testDeviceFallback();
  }

 private:
  void testKeysAndLabels() {
    beginTest("backend keys, device type names and menu labels");

    expect(audioBackendKey(AudioBackend::Default) == "default");
    expect(audioBackendKey(AudioBackend::Alsa) == "alsa");
    expect(audioBackendKey(AudioBackend::Jack) == "jack");
    for (const auto backend : kAudioBackends) {
      expect(audioBackendFromKey(audioBackendKey(backend)) == backend,
             "the persisted key round-trips");
    }
    expect(audioBackendFromKey("") == kDefaultAudioBackend, "empty key -> default");
    expect(audioBackendFromKey("pipewire") == kDefaultAudioBackend,
           "an unknown key never selects a different backend");
    expect(kDefaultAudioBackend == AudioBackend::Default, "ALSA stays the default backend");

    // An empty device type name means "leave JUCE's platform default alone".
    expect(audioDeviceTypeName(AudioBackend::Default).empty());
    expect(audioDeviceTypeName(AudioBackend::Alsa) == "ALSA");
    expect(audioDeviceTypeName(AudioBackend::Jack) == "JACK");

    expect(juce::String(audioBackendLabel(AudioBackend::Default)) == "Default");
    expect(juce::String(audioBackendLabel(AudioBackend::Alsa, true)) == "ALSA");
    expect(juce::String(audioBackendLabel(AudioBackend::Jack, true)) == "JACK");
    expect(juce::String(audioBackendLabel(AudioBackend::Jack, false)) ==
               "JACK (unavailable)",
           "an unavailable JACK entry says so instead of vanishing");
    expect(juce::String(audioBackendLabel(AudioBackend::Alsa, false)) ==
           "ALSA (unavailable)");
  }

  void testAvailabilityAndFallback() {
    beginTest("availability and the JACK fallback");

    const AudioBackendAvailability none;
    expect(!audioBackendAvailable(AudioBackend::Alsa, none), "macOS has no ALSA type");
    expect(!audioBackendAvailable(AudioBackend::Jack, none), "no JACK type in this build");
    expect(audioBackendAvailable(AudioBackend::Default, none),
           "Default is always offered");

    AudioBackendAvailability linuxAlsaOnly;
    linuxAlsaOnly.alsa = true;
    expect(audioBackendAvailable(AudioBackend::Alsa, linuxAlsaOnly));
    expect(!audioBackendAvailable(AudioBackend::Jack, linuxAlsaOnly));

    AudioBackendAvailability both;
    both.alsa = true;
    both.jack = true;
    expect(audioBackendAvailable(AudioBackend::Jack, both));

    auto selection = resolveAudioBackend(AudioBackend::Default, none);
    expect(selection.effective == AudioBackend::Default);
    expect(!selection.fellBack);
    expect(selection.note.empty());

    selection = resolveAudioBackend(AudioBackend::Alsa, linuxAlsaOnly);
    expect(selection.effective == AudioBackend::Alsa);
    expect(!selection.fellBack);
    expect(selection.note.empty());

    selection = resolveAudioBackend(AudioBackend::Jack, both);
    expect(selection.effective == AudioBackend::Jack);
    expect(!selection.fellBack);
    expect(selection.note.empty());

    // JACK unavailable: the device manager is asked for the default backend
    // (ALSA on Linux) and the note names the missing JACK server.
    selection = resolveAudioBackend(AudioBackend::Jack, linuxAlsaOnly);
    expect(selection.requested == AudioBackend::Jack);
    expect(selection.effective == AudioBackend::Default);
    expect(selection.fellBack);
    expect(juce::String(selection.note).contains("JACK"));
    expect(juce::String(selection.note).contains("jackd"));

    // ALSA requested where the type does not exist (the macOS dev machine).
    selection = resolveAudioBackend(AudioBackend::Alsa, none);
    expect(selection.effective == AudioBackend::Default);
    expect(selection.fellBack);
    expect(juce::String(selection.note).contains("ALSA"));
  }

  void testBufferSizes() {
    beginTest("buffer size options");

    expect(isAudioBufferSize(128));
    expect(isAudioBufferSize(1024));
    expect(!isAudioBufferSize(0));
    expect(!isAudioBufferSize(100));

    expectEquals(clampAudioBufferSize(128), 128);
    expectEquals(clampAudioBufferSize(0), 64);
    expectEquals(clampAudioBufferSize(96), 64);  // a tie goes to the shorter period
    expectEquals(clampAudioBufferSize(100), 128);
    expectEquals(clampAudioBufferSize(4096), 1024);
    expectEquals(clampAudioBufferSize(-8), 64);

    expect(juce::String(audioBufferSizeLabel(128)) == "128 frames");
    expect(juce::String(audioBufferSizeLabel(300)) == "256 frames");
    expectEquals(kDefaultAudioBufferSize, 128);
    expectEquals(kAudioBufferSizeCount,
                 static_cast<int>(sizeof(kAudioBufferSizes) / sizeof(kAudioBufferSizes[0])));
  }

  void testDeviceFallback() {
    beginTest("persisted choice and stored device fallback");

    const std::vector<std::string> devices{"Loopback"};
    expect(resolveAudioDeviceName("Loopback", devices) == "Loopback");
    expect(resolveAudioDeviceName("", devices).empty(),
           "an empty request keeps the device type's default");
    expect(resolveAudioDeviceName("UMC1820", devices).empty(),
           "a device that is no longer listed falls back to the type default");
    expect(resolveAudioDeviceName("Loopback", {}).empty(),
           "no listed devices -> type default");

    const AudioBackendChoice initial;
    expect(initial.backend == AudioBackend::Default);
    expect(initial.deviceName.empty());
    expectEquals(initial.bufferSize, kDefaultAudioBufferSize);

    AudioBackendChoice jack;
    jack.backend = AudioBackend::Jack;
    jack.deviceName = "system";
    expect(initial != jack);
    expect(jack == AudioBackendChoice{AudioBackend::Jack, "system", 128});
  }
};

AudioBackendTests audioBackendTests;

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

// The patchbay geometry is a pure function of its bounds: the checks below pin
// the 800 x 480 numbers and prove the layout scales instead of carrying pixel
// constants.
class RoutingModelTests final : public juce::UnitTest {
 public:
  RoutingModelTests() : juce::UnitTest("mnkybox routing patchbay", "mnkybox") {}

  void runTest() override {
    testDefaultMatrix();
    testGeometry();
    testWireHitAreas();
  }

 private:
  void testDefaultMatrix() {
    beginTest("documented default route matrix");

    int enabled = 0;
    for (int source = 0; source < routing::kInputCount; ++source) {
      for (int destination = 0; destination < routing::kOutputCount; ++destination) {
        const bool expected = source < 2 && destination == 2;
        expect(routing::kDefaultRoutes[source][destination] == expected,
               "the default routes are exactly TRS IN 1+2 -> USB OUT");
        if (expected) ++enabled;
      }
    }
    expectEquals(enabled, 2);

    expect(routing::kInputs[0] == midi::Port::TrsIn1);
    expect(routing::kInputs[1] == midi::Port::TrsIn2);
    expect(routing::kInputs[2] == midi::Port::UsbIn);
    expect(routing::kOutputs[0] == midi::Port::TrsOut1);
    expect(routing::kOutputs[1] == midi::Port::TrsOut2);
    expect(routing::kOutputs[2] == midi::Port::UsbOut);
  }

  void testGeometry() {
    beginTest("resolution-independent patchbay geometry");

    // The graph the routing screen gets at 800 x 480: 788 x 420 content minus a
    // 44 px RESET header, two 4 px gaps and the 180 px filter/transpose block.
    const juce::Rectangle<float> panel{0.0f, 0.0f, 788.0f, 188.0f};
    const auto layout = routing::layoutFor(panel);

    expect(layout.nodeHeight >= static_cast<float>(style::kTouch),
           "each node keeps the 44 px touch height at 800 x 480");
    expect(layout.hitSlop >= 18.0f && layout.hitSlop <= 22.0f,
           "the wire tap slop is about 20 px");

    for (int index = 0; index < routing::kInputCount; ++index) {
      expect(panel.contains(layout.inputs[index]), "input nodes stay inside the graph");
      expect(panel.contains(layout.outputs[index]), "output nodes stay inside the graph");
      expect(layout.inputs[index].getHeight() == layout.nodeHeight,
             "all nodes share one height");
    }
    expect(layout.inputs[0].getRight() < layout.outputs[0].getX(),
           "the input and output columns never overlap");

    for (int source = 0; source < routing::kInputCount; ++source) {
      for (int destination = 0; destination < routing::kOutputCount; ++destination) {
        const auto& wire = layout.wires[source][destination];
        expect(!wire.path.isEmpty(), "every routable pair gets a wire");
        expectWithinAbsoluteError(wire.start.x, layout.inputs[source].getRight(), 0.01f);
        expectWithinAbsoluteError(wire.start.y, layout.inputs[source].getCentreY(), 0.01f);
        expectWithinAbsoluteError(wire.end.x, layout.outputs[destination].getX(), 0.01f);
        expectWithinAbsoluteError(wire.end.y, layout.outputs[destination].getCentreY(),
                                  0.01f);
      }
    }

    // Twice the bounds is twice the node, not twice the node plus a constant:
    // no fixed pixel geometry hides in layoutFor().
    const auto doubled =
        routing::layoutFor(panel.withSize(panel.getWidth() * 2.0f, panel.getHeight() * 2.0f));
    expectWithinAbsoluteError(doubled.nodeWidth, layout.nodeWidth * 2.0f, 0.01f);
    expectWithinAbsoluteError(doubled.nodeHeight, layout.nodeHeight * 2.0f, 0.01f);

    // A taller content area (a window grown to 1024 x 600) still keeps the
    // touch minimum and the same relative node size.
    const auto grown = routing::layoutFor({0.0f, 0.0f, 1012.0f, 308.0f});
    expect(grown.nodeHeight >= static_cast<float>(style::kTouch),
           "nodes stay touch-sized on a larger window");
    expectWithinAbsoluteError(grown.nodeHeight / 308.0f, layout.nodeHeight / 188.0f,
                              1.0e-5f);
  }

  void testWireHitAreas() {
    beginTest("wire taps hit the intended route");

    const auto layout = routing::layoutFor({0.0f, 0.0f, 788.0f, 188.0f});
    for (int source = 0; source < routing::kInputCount; ++source) {
      for (int destination = 0; destination < routing::kOutputCount; ++destination) {
        const auto& path = layout.wires[source][destination].path;
        const float length = path.getLength();
        bool found = false;
        // Sampling away from the ends and the exact crossing point: at a
        // crossing two wires legitimately overlap, so the nearest one wins.
        for (int step = 2; step <= 8 && !found; ++step) {
          const auto point =
              path.getPointAlongPath(length * static_cast<float>(step) / 10.0f);
          int hitSource = -1;
          int hitDestination = -1;
          found = routing::hitWire(layout, point, hitSource, hitDestination) &&
                  hitSource == source && hitDestination == destination;
        }
        expect(found, "somewhere along every wire a tap resolves to that wire");
      }
    }

    int source = -1;
    int destination = -1;
    expect(!routing::hitWire(layout, {4.0f, 4.0f}, source, destination),
           "a tap away from every wire hits nothing");
  }
};

class TouchKeyboardModelTests final : public juce::UnitTest {
 public:
  TouchKeyboardModelTests()
      : juce::UnitTest("mnkybox touch keyboard model", "mnkybox") {}

  void runTest() override {
    beginTest("insert, cursor and delete");

    TextBuffer buffer(8);
    expect(buffer.empty());
    expect(buffer.insert("abc"), "insert accepts printable ASCII");
    expect(buffer.text() == "abc");
    expectEquals(buffer.cursor(), 3);
    expect(buffer.insert(' '), "space is a normal character");
    expect(buffer.text() == "abc ");
    expect(buffer.backspace());
    expect(buffer.text() == "abc");
    buffer.home();
    expectEquals(buffer.cursor(), 0);
    buffer.moveLeft();
    expectEquals(buffer.cursor(), 0, "cursor clamps at the start");
    expect(buffer.insert('X'));
    expect(buffer.text() == "Xabc");
    expectEquals(buffer.cursor(), 1);
    expect(buffer.deleteForward());
    expect(buffer.text() == "Xbc");
    buffer.end();
    expectEquals(buffer.cursor(), 3);
    buffer.moveRight();
    expectEquals(buffer.cursor(), 3, "cursor clamps at the end");

    beginTest("max length and ASCII filtering");

    TextBuffer limited(4);
    expect(limited.insert("abcdef"));
    expect(limited.text() == "abcd");
    expectEquals(limited.cursor(), 4);
    expect(!limited.insert('e'), "a full buffer rejects further input");
    expect(limited.text() == "abcd");
    expect(!limited.insert(static_cast<char>(0x01)), "control characters are rejected");
    expect(!limited.insert(static_cast<char>(0xe9)), "non-ASCII is rejected");
    limited.setText("xy\x01z\xff");
    expect(limited.text() == "xyz", "setText drops non-ASCII instead of failing");
    expectEquals(limited.cursor(), 3);
    expect(limited.clear());
    expect(limited.empty());
    expectEquals(limited.cursor(), 0);

    TextBuffer negative(-3);
    expect(!negative.insert('a'), "negative max length is clamped to zero");
  }
};

class ProjectLogicTests final : public juce::UnitTest {
 public:
  ProjectLogicTests() : juce::UnitTest("mnkybox project logic", "mnkybox") {}

  void runTest() override {
    testNames();
    testDirtyTracker();
    testChordDetector();
  }

 private:
  void testNames() {
    beginTest("project and take name validation");

    expect(validateProjectName("My Song").empty());
    expect(validateProjectName("Project-20261104-034401").empty());
    expect(!validateProjectName("").empty(), "empty project name is rejected");
    expect(!validateProjectName("a/b").empty(), "path separator is rejected");
    expect(!validateProjectName("a\\b").empty(), "backslash is rejected");
    expect(!validateProjectName(" leading").empty(), "leading space is rejected");
    expect(!validateProjectName("trailing ").empty(), "trailing space is rejected");
    expect(!validateProjectName(".").empty(), "dot is reserved");
    expect(!validateProjectName("..").empty(), "dot-dot is reserved");
    expect(!validateProjectName(std::string(49, 'a')).empty(), "long names are rejected");
    expect(!validateProjectName("caf\xc3\xa9").empty(), "non-ASCII names are rejected");

    expect(validateTakeName("take-20261104-034401").empty());
    expect(validateTakeName("take-drums").empty());
    expect(!validateTakeName("drums").empty(), "a take keeps the take- prefix");
    expect(!validateTakeName("take-a/b").empty(), "take separator is rejected");
    expect(!validateTakeName(std::string("take-") + std::string(50, 'a')).empty(),
           "long take names are rejected");

    expect(juce::String(defaultProjectName(1700000000)) == "Project-20231114-221320");
  }

  void testDirtyTracker() {
    beginTest("dirty transitions");

    DirtyTracker tracker;
    expect(!tracker.poll("a"), "the first poll only establishes the baseline");
    expect(!tracker.dirty());
    expect(!tracker.poll("a"), "an unchanged state stays clean");
    expect(tracker.poll("b"), "a changed fingerprint sets the dirty flag");
    expect(tracker.dirty());
    expect(tracker.poll("a"), "dirty is sticky until an explicit clear");
    expect(tracker.dirty());
    tracker.clear("a");
    expect(!tracker.dirty(), "save/load/new clears the flag");
    expect(!tracker.poll("a"));
    expect(!tracker.dirty());
  }

  void testChordDetector() {
    beginTest("REC+PLAY chord detector");

    ChordDetector detector;
    expect(!detector.onPress(ChordDetector::Button::Rec, 1000));
    expect(detector.onPress(ChordDetector::Button::Play, 1300),
           "exactly 300 ms still counts");
    expect(!detector.onPress(ChordDetector::Button::Play, 1301),
           "the completed pair consumes both edges");

    detector.reset();
    expect(!detector.onPress(ChordDetector::Button::Play, 10));
    expect(detector.onPress(ChordDetector::Button::Rec, 309), "either press order works");
    expect(!detector.onPress(ChordDetector::Button::Rec, 310),
           "a consumed pair does not fire twice");

    detector.reset();
    expect(!detector.onPress(ChordDetector::Button::Rec, 100));
    expect(!detector.onPress(ChordDetector::Button::Play, 401),
           "301 ms is outside the 300 ms window");

    detector.reset();
    expect(!detector.onPress(ChordDetector::Button::Play, 0));
    expect(!detector.onPress(ChordDetector::Button::Play, 100),
           "a second PLAY edge replaces the first");
    expect(detector.onPress(ChordDetector::Button::Rec, 200),
           "the latest PLAY edge still pairs");
  }
};

class ClickSessionTests final : public juce::UnitTest {
 public:
  ClickSessionTests() : juce::UnitTest("mnkybox click session field", "mnkybox") {}

  void runTest() override {
    beginTest("click.enabled round-trips through the session format");

    session::Snapshot snapshot;
    snapshot.click.enabled = true;
    snapshot.click.level = 0.25f;
    snapshot.click.bars = 2;

    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("mnkybox-click-test-" +
                                        juce::String(juce::Time::currentTimeMillis()) +
                                        ".mnkybox");
    expect(session::save(file.getFullPathName().toStdString(), snapshot));
    const auto result = session::load(file.getFullPathName().toStdString());
    expect(result.ok, "the written session reads back");
    expect(result.snapshot.click.enabled, "click.enabled survives save/load");
    expectWithinAbsoluteError(result.snapshot.click.level, 0.25f, 1e-6f);
    expectEquals(result.snapshot.click.bars, 2);
    file.deleteFile();
  }
};

ThemeTests themeTests;
RoutingModelTests routingModelTests;
TouchKeyboardModelTests touchKeyboardModelTests;
ProjectLogicTests projectLogicTests;
ClickSessionTests clickSessionTests;

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
