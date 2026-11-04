#include "session_settings.hpp"

#include <utility>

namespace mnky::app {

std::string SessionSettings::activeTake(int track) const {
  if (track < 0 || track >= audio::kMaxInputs) return {};
  const std::lock_guard<std::mutex> lock(takeLock_);
  return activeTakes_[static_cast<std::size_t>(track)];
}

void SessionSettings::setActiveTake(int track, std::string value) {
  if (track < 0 || track >= audio::kMaxInputs) return;
  const std::lock_guard<std::mutex> lock(takeLock_);
  activeTakes_[static_cast<std::size_t>(track)] = std::move(value);
}

bool SessionSettings::hasActiveTake(int track) const {
  return !activeTake(track).empty();
}

void SessionSettings::reset() {
  punch = session::PunchState{};
  cue = session::CueState{};
  sends = session::SendsState{};
  mainComp = session::MainCompState{};
  isTemplate = false;
  const std::lock_guard<std::mutex> lock(takeLock_);
  for (auto& take : activeTakes_) take.clear();
}

}  // namespace mnky::app
