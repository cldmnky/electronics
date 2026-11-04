// mnkybox — JUCE touchscreen DAW application (Pi 5 + Pico link).
//
// Flags:
//   --emulator   use the emulator backends (scripted Pico SPI link, generated
//                audio) instead of the Linux hardware paths. On a machine
//                without /dev/spidev0.0 the link falls back to the emulator on
//                its own and says so in the log.
//   --unittests  run the non-visual panel/value logic tests and exit; no window,
//                no audio device, no MIDI port is opened.

#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "MainComponent.hpp"
#include "logging.hpp"
#include "ui_style.hpp"

namespace mnky::app {
int runLogicUnitTests();
}

namespace {

constexpr const char* kInstanceLockName = "mnkybox-instance.lock";

class MainWindow final : public juce::DocumentWindow {
 public:
  MainWindow(const juce::String& name, bool emulator)
      : juce::DocumentWindow(name, mnky::app::style::background,
                             juce::DocumentWindow::allButtons) {
    setUsingNativeTitleBar(true);
    setContentOwned(new mnky::app::MainComponent(emulator), true);
    // Landscape-first: the panel is 800 x 480; the window may grow for desktop
    // work but never below the panel geometry.
    setResizable(true, false);
    setResizeLimits(800, 480, 1600, 960);
    centreWithSize(800, 480);
    setVisible(true);
  }

  void closeButtonPressed() override {
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
  }

 private:
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

}  // namespace

class MnkyboxApplication final : public juce::JUCEApplication {
 public:
  const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
  const juce::String getApplicationVersion() override {
    return JUCE_APPLICATION_VERSION_STRING;
  }
  bool moreThanOneInstanceAllowed() override { return false; }
  void anotherInstanceStarted(const juce::String&) override {}

  void initialise(const juce::String& commandLine) override {
    juce::ignoreUnused(commandLine);
    mnky::app::AppLogger::instance().install();

    const auto arguments = getCommandLineParameterArray();
    if (arguments.contains("--unittests")) {
      const int failures = mnky::app::runLogicUnitTests();
      setApplicationReturnValue(failures == 0 ? 0 : 1);
      quit();
      return;
    }

    // One mnkybox per machine: the app owns the take directory, the audio
    // device and the single Pico SPI link.
    const auto lockFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                              .getChildFile(kInstanceLockName);
    instanceLock_ = std::make_unique<juce::InterProcessLock>(lockFile.getFullPathName());
    if (!instanceLock_->enter(0)) {
      mnky::app::log("app: another mnkybox instance owns " + lockFile.getFullPathName() +
                     "; exiting");
      setApplicationReturnValue(0);
      quit();
      return;
    }

    const bool emulator = arguments.contains("--emulator");
    mnky::app::log(juce::String("app: backends ") +
                   (emulator ? "emulator (--emulator)" : "hardware (Linux SPI/ALSA)"));
    mainWindow_ = std::make_unique<MainWindow>(getApplicationName(), emulator);
  }

  void shutdown() override {
    mainWindow_.reset();
    if (instanceLock_ != nullptr) {
      instanceLock_->exit();
      instanceLock_.reset();
    }
    juce::Logger::setCurrentLogger(nullptr);
  }

  void systemRequestedQuit() override {
    quit();
  }

 private:
  std::unique_ptr<juce::InterProcessLock> instanceLock_;
  std::unique_ptr<MainWindow> mainWindow_;
};

START_JUCE_APPLICATION(MnkyboxApplication)
