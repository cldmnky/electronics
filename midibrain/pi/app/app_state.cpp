#include "app_state.hpp"

#include <cstdlib>
#include <system_error>

#include "logging.hpp"

namespace mnky::app {
namespace {
std::filesystem::path defaultTakeDirectory() {
  std::filesystem::path home;
  if (const char* env = std::getenv("HOME")) home = env;
  if (home.empty()) home = std::filesystem::temp_directory_path();
  auto directory = home / "Music" / "mnkybox-takes";
  std::error_code error;
  std::filesystem::create_directories(directory, error);
  return directory;
}
}  // namespace

AppState::AppState() : takeDirectory_(defaultTakeDirectory()) {
  log("takes: " + takeDirectory_.string());
}

void AppState::postIntent(const Intent& intent) {
  if (intent.kind == Intent::Kind::None) return;
  const std::lock_guard<std::mutex> lock(intentLock_);
  intents_.push_back(intent);
  // A stuck UI must not turn a repeated panel gesture into unbounded memory.
  while (intents_.size() > 64) intents_.pop_front();
}

bool AppState::nextIntent(Intent& out) {
  const std::lock_guard<std::mutex> lock(intentLock_);
  if (intents_.empty()) return false;
  out = intents_.front();
  intents_.pop_front();
  return true;
}

std::filesystem::path AppState::takeDirectory() const {
  const std::lock_guard<std::mutex> lock(dirLock_);
  return takeDirectory_;
}

void AppState::setTakeDirectory(const std::filesystem::path& directory) {
  std::error_code error;
  if (!std::filesystem::is_directory(directory, error)) return;
  {
    const std::lock_guard<std::mutex> lock(dirLock_);
    takeDirectory_ = directory;
  }
  log("takes: " + directory.string());
}

std::filesystem::path AppState::currentProject() const {
  const std::lock_guard<std::mutex> lock(dirLock_);
  return currentProject_;
}

void AppState::setCurrentProject(const std::filesystem::path& directory) {
  {
    const std::lock_guard<std::mutex> lock(dirLock_);
    currentProject_ = directory;
  }
  log("project: " + (directory.empty() ? std::string("none") : directory.string()));
}

}  // namespace mnky::app
