#include "logging.hpp"

#include <cstdio>

namespace mnky::app {
namespace {
juce::String timestamped(const juce::String& message) {
  return juce::Time::getCurrentTime().formatted("%H:%M:%S") + " " + message;
}
}  // namespace

AppLogger& AppLogger::instance() {
  static AppLogger logger;
  return logger;
}

AppLogger::~AppLogger() {
  if (installed_) juce::Logger::setCurrentLogger(nullptr);
}

void AppLogger::install() {
  if (installed_) return;
  installed_ = true;
  juce::Logger::setCurrentLogger(this);
}

void AppLogger::logMessage(const juce::String& message) {
  write(message);
}

void AppLogger::write(const juce::String& message) {
  const auto line = timestamped(message);
  {
    const juce::ScopedLock lock(lock_);
    lines_.push_back(line);
    while (static_cast<int>(lines_.size()) > capacity_) lines_.pop_front();
  }
  std::fprintf(stderr, "mnkybox: %s\n", line.toRawUTF8());
}

juce::String AppLogger::tail(int maxLines) const {
  const juce::ScopedLock lock(lock_);
  juce::String out;
  const int start = maxLines > 0 && static_cast<int>(lines_.size()) > maxLines
                        ? static_cast<int>(lines_.size()) - maxLines
                        : 0;
  for (int i = start; i < static_cast<int>(lines_.size()); ++i) {
    out << lines_[static_cast<std::size_t>(i)] << "\n";
  }
  return out;
}

void log(const juce::String& message) {
  AppLogger::instance().write(message);
}

}  // namespace mnky::app
