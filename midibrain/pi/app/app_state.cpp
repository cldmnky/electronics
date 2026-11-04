#include "app_state.hpp"

#include "logging.hpp"

namespace mnky::app {
namespace {
juce::File defaultTakeDirectory() {
  auto directory =
      juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile("mnkybox-takes");
  if (directory.createDirectory().wasOk()) return directory;
  directory =
      juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("mnkybox-takes");
  directory.createDirectory();
  return directory;
}
}  // namespace

AppState::AppState() : takeDirectory_(defaultTakeDirectory()) {
  log("takes: " + takeDirectory_.getFullPathName());
}

void AppState::postIntent(const Intent& intent) {
  if (intent.kind == Intent::Kind::None) return;
  const juce::ScopedLock lock(intentLock_);
  intents_.push_back(intent);
  // A stuck UI must not turn a repeated panel gesture into unbounded memory.
  while (intents_.size() > 64) intents_.pop_front();
}

bool AppState::nextIntent(Intent& out) {
  const juce::ScopedLock lock(intentLock_);
  if (intents_.empty()) return false;
  out = intents_.front();
  intents_.pop_front();
  return true;
}

juce::File AppState::takeDirectory() const {
  const juce::ScopedLock lock(dirLock_);
  return takeDirectory_;
}

void AppState::setTakeDirectory(const juce::File& directory) {
  if (!directory.isDirectory()) return;
  {
    const juce::ScopedLock lock(dirLock_);
    takeDirectory_ = directory;
  }
  log("takes: " + directory.getFullPathName());
}

}  // namespace mnky::app
