#pragma once
// Bounded in-memory log tail for the System screen. The app is expected to run
// without a terminal on the Pi, so everything that matters is also visible on
// screen. Thread-safe and Qt-free so it can be used from the link/audio/MIDI
// threads before the GUI exists.
#include <deque>
#include <mutex>
#include <string>

namespace mnky::app {

class AppLogger final {
 public:
  static AppLogger& instance();

  // Timestamped line into the ring (and stderr). Safe from any thread.
  void write(const std::string& message);

  // Last `maxLines` lines, oldest first.
  std::string tail(int maxLines) const;

 private:
  AppLogger() = default;

  mutable std::mutex lock_;
  std::deque<std::string> lines_;
  int capacity_ = 400;
};

// Convenience: AppLogger::instance().write(...).
void log(const std::string& message);

}  // namespace mnky::app
