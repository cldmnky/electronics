// JUCE unit tests for the app's non-visual logic (logic.hpp). Registered with
// the JUCE test framework and run by `mnkybox --unittests`, which is also the
// CTest entry `mnkybox_app_logic`. No audio, MIDI, link or display access.

#include <iostream>

#include <juce_core/juce_core.h>

#include "fx_slot.hpp"
#include "logic.hpp"

namespace mnky::app {
namespace {

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
