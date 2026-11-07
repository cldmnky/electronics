#include "fx_slot.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <set>

#include "logging.hpp"

namespace mnky::app {
namespace {

// CLAP_PATH is the user's explicit search path (the same contract as PATH):
// colon-separated entries, each either a plugin module or a directory of
// modules. Empty entries are skipped so a trailing colon cannot scan cwd.
std::vector<std::string> splitClapPath(const char* value) {
  std::vector<std::string> entries;
  if (value == nullptr) return entries;
  const std::string text = value;
  std::size_t start = 0;
  while (start <= text.size()) {
    const std::size_t end = text.find(':', start);
    const std::string entry = text.substr(
        start, end == std::string::npos ? std::string::npos : end - start);
    if (!entry.empty()) entries.push_back(entry);
    if (end == std::string::npos) break;
    start = end + 1;
  }
  return entries;
}

// One module (a .clap file or a macOS bundle directory) from `path`.
void addClapModule(std::vector<std::string>& candidates, const std::string& path) {
  if (path.empty()) return;
  if (std::find(candidates.begin(), candidates.end(), path) == candidates.end()) {
    candidates.push_back(path);
  }
}

// Every .clap entry directly inside a search directory. Symlinks are followed
// because distro packages often link /usr/lib/clap/<name>.clap into /usr/lib.
void addClapDirectory(std::vector<std::string>& candidates,
                      const std::filesystem::path& directory) {
  std::error_code error;
  if (!std::filesystem::is_directory(directory, error) || error) return;
  for (std::filesystem::directory_iterator it(directory, error), end;
       !error && it != end; it.increment(error)) {
    if (it->path().extension() != ".clap") continue;
    std::error_code typeError;
    if (it->is_directory(typeError) || it->is_regular_file(typeError)) {
      addClapModule(candidates, it->path().string());
    }
  }
}

}  // namespace

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

bool FxSlot::load(const std::string& path, double sampleRate, int maxFrames,
                  const std::string& pluginId) {
  const std::lock_guard<std::mutex> lock(lock_);
  host_.unload();
  path_.clear();
  lastError_.clear();

  if (path.empty()) {
    lastError_ = "no plugin path";
    return false;
  }
  if (!host_.load(path, pluginId.empty() ? nullptr : pluginId.c_str())) {
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
  // CLAP_PATH is searched first: an explicit path always wins over an install.
  for (const auto& entry : splitClapPath(std::getenv("CLAP_PATH"))) {
    const std::filesystem::path path(entry);
    std::error_code error;
    if (std::filesystem::is_directory(path, error) && !error) {
      candidates.push_back((path / "mnkygain.clap").string());
    } else {
      candidates.push_back(path.string());
    }
  }
  if (!home.empty()) {
    candidates.push_back((home / "Library/Audio/Plug-Ins/CLAP/mnkygain.clap").string());
    candidates.push_back((home / ".clap/mnkygain.clap").string());
  }
  candidates.emplace_back("/Library/Audio/Plug-Ins/CLAP/mnkygain.clap");
  candidates.emplace_back("/usr/lib/clap/mnkygain.clap");
  candidates.emplace_back("/usr/local/lib/clap/mnkygain.clap");
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

bool FxManager::validSlot(int track, int slot) {
  return track >= 0 && track < audio::kMaxTracks && slot >= 0 &&
         slot < kTrackSlots;
}

std::size_t FxManager::flatSlot(int track, int slot) {
  return static_cast<std::size_t>(track) * kTrackSlots +
         static_cast<std::size_t>(slot);
}

void FxManager::scanPlugins() {
  if (scanned_) return;
  scanned_ = true;

  std::vector<std::string> candidates;
  const auto add = [&candidates](const std::string& path) {
    addClapModule(candidates, path);
  };
#if defined(MNKY_DEFAULT_PLUGIN)
  add(MNKY_DEFAULT_PLUGIN);
#endif
  if (const char* override = std::getenv("MNKY_PLUGIN_PATH")) add(override);
  add(FxSlot::defaultPluginPath());

  // CLAP_PATH: entries are modules or directories in the order the user gave.
  // This is the canonical place a hand-installed plugin lives, so it is read
  // before the distro-wide install directories.
  for (const auto& entry : splitClapPath(std::getenv("CLAP_PATH"))) {
    std::error_code error;
    if (std::filesystem::is_directory(entry, error) && !error) {
      addClapDirectory(candidates, entry);
    } else {
      add(entry);
    }
  }

  std::filesystem::path home;
  if (const char* env = std::getenv("HOME")) home = env;
  if (!home.empty()) {
    addClapDirectory(candidates, home / "Library/Audio/Plug-Ins/CLAP");
    addClapDirectory(candidates, home / ".clap");
  }
  addClapDirectory(candidates, "/Library/Audio/Plug-Ins/CLAP");
  addClapDirectory(candidates, "/usr/lib/clap");
  addClapDirectory(candidates, "/usr/local/lib/clap");

  std::set<std::string> seenIds;
  for (const auto& candidate : candidates) {
    clap::Host probe;
    if (!probe.load(candidate)) continue;
    PluginInfo info;
    info.id = probe.id();
    info.name = probe.name();
    info.path = candidate;
    info.params = probe.params();
    probe.unload();
    if (info.id.empty() || info.name.empty()) continue;
    if (!seenIds.insert(info.id).second) continue;
    log("clap: discovered " + info.name + " (" + info.id + ") at " + candidate);
    plugins_.push_back(std::move(info));
  }
  if (plugins_.empty()) {
    log("clap: no plugin found (set CLAP_PATH or install a .clap module)");
  }
}

const std::vector<FxManager::PluginInfo>& FxManager::plugins() {
  scanPlugins();
  return plugins_;
}

const FxManager::PluginInfo* FxManager::findPlugin(const std::string& id) {
  if (id.empty()) return nullptr;
  for (const auto& plugin : plugins()) {
    if (plugin.id == id) return &plugin;
  }
  return nullptr;
}

bool FxManager::loadTrackEffect(int track, int slot, const std::string& pluginId,
                                double sampleRate, int maxFrames) {
  if (!validSlot(track, slot) || pluginId.empty()) return false;
  const std::size_t flat = flatSlot(track, slot);
  auto& holder = trackSlots_[static_cast<std::size_t>(track)]
                            [static_cast<std::size_t>(slot)];
  if (!holder) holder = std::make_unique<FxSlot>(track * kTrackSlots + slot);
  if (holder->loaded()) {
    if (holder->pluginId() == pluginId) {
      trackErrors_[flat].clear();
      return true;
    }
    // Switching a slot keeps its single admission: unload the old instance
    // before the cap test so the new one sees the same instance count.
    holder->unload();
  }
  if (trackInstanceCount() >= kMaxTrackInstances) {
    trackErrors_[flat] = "plugin limit reached";
    log("track fx " + std::to_string(track) + "/" + std::to_string(slot) +
        ": plugin limit reached (" + std::to_string(kMaxTrackInstances) + ")");
    return false;
  }
  const PluginInfo* plugin = findPlugin(pluginId);
  const std::string path =
      plugin != nullptr ? plugin->path : FxSlot::defaultPluginPath();
  if (path.empty()) {
    trackErrors_[flat] = "no plugin path for " + pluginId;
    log("track fx " + std::to_string(track) + "/" + std::to_string(slot) + ": " +
        trackErrors_[flat]);
    return false;
  }
  if (!holder->load(path, sampleRate, maxFrames, pluginId)) {
    trackErrors_[flat] =
        holder->lastError().empty() ? "load failed" : holder->lastError();
    log("track fx " + std::to_string(track) + "/" + std::to_string(slot) + ": " +
        trackErrors_[flat] + " (" + path + ")");
    return false;
  }
  trackErrors_[flat].clear();
  return true;
}

void FxManager::unloadTrackEffect(int track, int slot) {
  if (!validSlot(track, slot)) return;
  auto& holder = trackSlots_[static_cast<std::size_t>(track)]
                            [static_cast<std::size_t>(slot)];
  if (!holder) return;
  holder->unload();
  trackErrors_[flatSlot(track, slot)].clear();
}

bool FxManager::trackEffectLoaded(int track, int slot) const {
  if (!validSlot(track, slot)) return false;
  const auto& holder = trackSlots_[static_cast<std::size_t>(track)]
                                  [static_cast<std::size_t>(slot)];
  return holder != nullptr && holder->loaded();
}

audio::IEffect* FxManager::trackEffect(int track, int slot) {
  if (!trackEffectLoaded(track, slot)) return nullptr;
  auto& holder = trackSlots_[static_cast<std::size_t>(track)]
                            [static_cast<std::size_t>(slot)];
  return &holder->effect();
}

std::string FxManager::trackEffectId(int track, int slot) const {
  if (!trackEffectLoaded(track, slot)) return {};
  const auto& holder = trackSlots_[static_cast<std::size_t>(track)]
                                  [static_cast<std::size_t>(slot)];
  return holder->pluginId();
}

std::string FxManager::trackEffectName(int track, int slot) const {
  if (!trackEffectLoaded(track, slot)) return {};
  const auto& holder = trackSlots_[static_cast<std::size_t>(track)]
                                  [static_cast<std::size_t>(slot)];
  return holder->pluginName();
}

std::string FxManager::trackEffectError(int track, int slot) const {
  if (!validSlot(track, slot)) return {};
  return trackErrors_[flatSlot(track, slot)];
}

std::vector<clap::Param> FxManager::trackParams(int track, int slot) {
  if (!trackEffectLoaded(track, slot)) return {};
  auto& holder = trackSlots_[static_cast<std::size_t>(track)]
                            [static_cast<std::size_t>(slot)];
  return holder->params();
}

double FxManager::trackParamValue(int track, int slot, std::size_t index) const {
  if (!trackEffectLoaded(track, slot)) return 0.0;
  const auto& holder = trackSlots_[static_cast<std::size_t>(track)]
                                  [static_cast<std::size_t>(slot)];
  return holder->paramValue(index);
}

bool FxManager::setTrackParam(int track, int slot, std::size_t index,
                              double value) {
  if (!trackEffectLoaded(track, slot)) return false;
  auto& holder = trackSlots_[static_cast<std::size_t>(track)]
                            [static_cast<std::size_t>(slot)];
  return holder->setParamValue(index, value);
}

int FxManager::trackInstanceCount() const {
  int count = 0;
  for (int track = 0; track < audio::kMaxTracks; ++track) {
    for (int slot = 0; slot < kTrackSlots; ++slot) {
      const auto& holder = trackSlots_[static_cast<std::size_t>(track)]
                                      [static_cast<std::size_t>(slot)];
      if (holder != nullptr && holder->loaded()) ++count;
    }
  }
  return count;
}

bool FxManager::canLoadTrackEffect(int track, int slot) const {
  if (!validSlot(track, slot)) return false;
  if (trackEffectLoaded(track, slot)) return true;
  return trackInstanceCount() < kMaxTrackInstances;
}

}  // namespace mnky::app
