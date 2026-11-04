#include "fx_slot.hpp"

#include <cstdlib>
#include <filesystem>

#include "logging.hpp"

namespace mnky::app {

class FxSlot::Adapter final : public audio::IEffect {
 public:
  explicit Adapter(FxSlot& slot) : slot_(slot) {}

  void process(float* const* ch, int nch, int n) override {
    // Host is activated for exactly one main stereo port and kBlockSize frames.
    if (ch == nullptr || nch != 2 || n <= 0) return;
    std::unique_lock<std::mutex> lock(slot_.lock_, std::try_to_lock);
    if (!lock.owns_lock() || !slot_.host_.loaded() || !slot_.host_.isActive() ||
        slot_.bypassed()) {
      return;
    }
    slot_.host_.process(ch, 2, ch, 2, static_cast<std::uint32_t>(n));
  }

  const char* name() const override { return "clap-send"; }

 private:
  FxSlot& slot_;
};

FxSlot::FxSlot(int index) : adapter_(std::make_unique<Adapter>(*this)), index_(index) {}

FxSlot::~FxSlot() = default;

audio::IEffect& FxSlot::effect() {
  return *adapter_;
}

bool FxSlot::load(const std::string& path, double sampleRate, int maxFrames) {
  const std::lock_guard<std::mutex> lock(lock_);
  host_.unload();
  path_.clear();
  lastError_.clear();

  if (path.empty()) {
    lastError_ = "no plugin path";
    return false;
  }
  if (!host_.load(path)) {
    lastError_ = host_.lastError().empty() ? "load failed" : host_.lastError();
    log("fx " + std::to_string(index_) + ": " + lastError_ + " (" + path + ")");
    return false;
  }
  if (!host_.activate(sampleRate, static_cast<std::uint32_t>(maxFrames))) {
    lastError_ = "activate failed: " + host_.lastError();
    host_.unload();
    log("fx " + std::to_string(index_) + ": " + lastError_);
    return false;
  }
  path_ = path;
  const auto count = host_.params().size();
  log("fx " + std::to_string(index_) + ": loaded " + host_.name() + " from " + path +
      " (" + std::to_string(count) + " params)");
  return true;
}

void FxSlot::unload() {
  const std::lock_guard<std::mutex> lock(lock_);
  host_.unload();
  path_.clear();
  lastError_.clear();
}

bool FxSlot::loaded() const {
  const std::lock_guard<std::mutex> lock(lock_);
  return host_.loaded();
}

std::string FxSlot::pluginPath() const {
  const std::lock_guard<std::mutex> lock(lock_);
  return path_;
}

std::string FxSlot::pluginName() const {
  const std::lock_guard<std::mutex> lock(lock_);
  return host_.loaded() ? host_.name() : std::string();
}

std::string FxSlot::lastError() const {
  const std::lock_guard<std::mutex> lock(lock_);
  return lastError_;
}

std::vector<clap::Param> FxSlot::params() const {
  const std::lock_guard<std::mutex> lock(lock_);
  if (!host_.loaded()) return {};
  return host_.params();
}

bool FxSlot::setParam(clap_id id, double value) {
  const std::lock_guard<std::mutex> lock(lock_);
  return host_.setParam(id, value);
}

std::size_t FxSlot::paramCount() const {
  const std::lock_guard<std::mutex> lock(lock_);
  return host_.loaded() ? host_.params().size() : 0;
}

double FxSlot::paramValue(std::size_t index) const {
  const std::lock_guard<std::mutex> lock(lock_);
  const auto list = host_.params();
  if (index >= list.size()) return 0.0;
  return host_.getParam(list[index].id);
}

bool FxSlot::setParamValue(std::size_t index, double value) {
  const std::lock_guard<std::mutex> lock(lock_);
  const auto list = host_.params();
  if (index >= list.size()) return false;
  return host_.setParam(list[index].id, value);
}

std::string FxSlot::pluginId() const {
  const std::lock_guard<std::mutex> lock(lock_);
  return host_.id();
}

bool FxSlot::saveState(const std::string& path) {
  const std::lock_guard<std::mutex> lock(lock_);
  const bool ok = host_.loaded() && host_.saveState(path);
  if (!ok) {
    lastError_ = "state save failed (no state extension or unwritable path)";
    log("fx " + std::to_string(index_) + ": " + lastError_);
  }
  return ok;
}

bool FxSlot::loadState(const std::string& path) {
  const std::lock_guard<std::mutex> lock(lock_);
  const bool ok = host_.loaded() && host_.loadState(path);
  if (!ok) {
    lastError_ = "state load failed (no state extension or unreadable file)";
    log("fx " + std::to_string(index_) + ": " + lastError_);
  }
  return ok;
}

std::string FxSlot::defaultPluginPath() {
  std::vector<std::string> candidates;
#if defined(MNKY_DEFAULT_PLUGIN)
  candidates.emplace_back(MNKY_DEFAULT_PLUGIN);
#endif
  if (const char* override = std::getenv("MNKY_PLUGIN_PATH")) {
    if (*override != '\0') candidates.emplace_back(override);
  }

  std::filesystem::path home;
  if (const char* env = std::getenv("HOME")) home = env;
  if (!home.empty()) {
    candidates.push_back((home / "Library/Audio/Plug-Ins/CLAP/mnkygain.clap").string());
    candidates.push_back((home / ".clap/mnkygain.clap").string());
  }
  candidates.emplace_back("/usr/lib/clap/mnkygain.clap");
  candidates.emplace_back("mnkygain.clap");

  std::error_code error;
  for (const auto& candidate : candidates) {
    if (!candidate.empty() && std::filesystem::exists(candidate, error)) return candidate;
  }
  return {};
}

FxManager::FxManager() : slotA_(0), slotB_(1) {}

FxSlot& FxManager::slot(int index) {
  return index == 1 ? slotB_ : slotA_;
}

audio::IEffect* FxManager::effect(int index) {
  return &slot(index).effect();
}

}  // namespace mnky::app
