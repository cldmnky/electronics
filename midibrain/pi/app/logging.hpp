#pragma once
// Bounded in-memory log tail for the System screen plus the juce::Logger sink.
// The app is expected to run without a terminal on the Pi, so everything that
// matters is also visible on screen.
#include <deque>

#include <juce_core/juce_core.h>

namespace mnky::app {

class AppLogger final : private juce::Logger {
 public:
  static AppLogger& instance();

  // Timestamped line into the ring (and stderr). Safe from any thread.
  void write(const juce::String& message);

  // Last `maxLines` lines, oldest first.
  juce::String tail(int maxLines) const;

  // Routes juce::Logger traffic (JUCE itself, the CLAP host) into the ring.
  void install();

 private:
  AppLogger() = default;
  ~AppLogger() override;

  void logMessage(const juce::String& message) override;

  mutable juce::CriticalSection lock_;
  std::deque<juce::String> lines_;
  int capacity_ = 400;
  bool installed_ = false;
};

// Convenience: AppLogger::instance().write(...).
void log(const juce::String& message);

}  // namespace mnky::app
