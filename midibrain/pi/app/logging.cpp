#include "logging.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>

namespace mnky::app {
namespace {
std::string timestamped(const std::string& message) {
  const auto now = std::chrono::system_clock::now();
  const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
  std::tm parts{};
#if defined(_WIN32)
  localtime_s(&parts, &seconds);
#else
  localtime_r(&seconds, &parts);
#endif
  char buffer[16] = {};
  std::snprintf(buffer, sizeof buffer, "%02d:%02d:%02d", parts.tm_hour, parts.tm_min,
                parts.tm_sec);
  return std::string(buffer) + " " + message;
}
}  // namespace

AppLogger& AppLogger::instance() {
  static AppLogger logger;
  return logger;
}

void AppLogger::write(const std::string& message) {
  const auto line = timestamped(message);
  {
    const std::lock_guard<std::mutex> lock(lock_);
    lines_.push_back(line);
    while (static_cast<int>(lines_.size()) > capacity_) lines_.pop_front();
  }
  std::fprintf(stderr, "mnkybox: %s\n", line.c_str());
}

std::string AppLogger::tail(int maxLines) const {
  const std::lock_guard<std::mutex> lock(lock_);
  std::string out;
  const int start = maxLines > 0 && static_cast<int>(lines_.size()) > maxLines
                        ? static_cast<int>(lines_.size()) - maxLines
                        : 0;
  for (int i = start; i < static_cast<int>(lines_.size()); ++i) {
    out += lines_[static_cast<std::size_t>(i)];
    out += '\n';
  }
  return out;
}

void log(const std::string& message) {
  AppLogger::instance().write(message);
}

}  // namespace mnky::app
