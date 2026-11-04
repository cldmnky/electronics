#include "fx_slot.hpp"

#include "logging.hpp"

namespace mnky::app {

class FxSlot::Adapter final : public audio::IEffect {
 public:
  explicit Adapter(FxSlot& slot) : slot_(slot) {}

  void process(float* const* ch, int nch, int n) override {
    // Host is activated for exactly one main stereo port and kBlockSize frames.
    if (ch == nullptr || nch != 2 || n <= 0) return;
    const juce::SpinLock::ScopedTryLockType lock(slot_.lock_);
    if (!lock.isLocked() || !slot_.host_.loaded() || !slot_.host_.isActive()) return;
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

bool FxSlot::load(const juce::String& path, double sampleRate, int maxFrames) {
  const juce::SpinLock::ScopedLockType lock(lock_);
  host_.unload();
  path_.clear();
  lastError_.clear();

  if (path.isEmpty()) {
    lastError_ = "no plugin path";
    return false;
  }
  if (!host_.load(path.toStdString())) {
    lastError_ = juce::String(host_.lastError()).isEmpty() ? juce::String("load failed")
                                                           : juce::String(host_.lastError());
    log("fx " + juce::String(index_) + ": " + lastError_ + " (" + path + ")");
    return false;
  }
  if (!host_.activate(sampleRate, static_cast<std::uint32_t>(maxFrames))) {
    lastError_ = juce::String("activate failed: ") + host_.lastError();
    host_.unload();
    log("fx " + juce::String(index_) + ": " + lastError_);
    return false;
  }
  path_ = path;
  const auto count = host_.params().size();
  log("fx " + juce::String(index_) + ": loaded " + juce::String(host_.name()) + " from " +
      path + " (" + juce::String(static_cast<int>(count)) + " params)");
  return true;
}

void FxSlot::unload() {
  const juce::SpinLock::ScopedLockType lock(lock_);
  host_.unload();
  path_.clear();
  lastError_.clear();
}

bool FxSlot::loaded() const {
  const juce::SpinLock::ScopedLockType lock(lock_);
  return host_.loaded();
}

juce::String FxSlot::pluginPath() const {
  const juce::SpinLock::ScopedLockType lock(lock_);
  return path_;
}

juce::String FxSlot::lastError() const {
  const juce::SpinLock::ScopedLockType lock(lock_);
  return lastError_;
}

std::vector<clap::Param> FxSlot::params() const {
  const juce::SpinLock::ScopedLockType lock(lock_);
  if (!host_.loaded()) return {};
  return host_.params();
}

bool FxSlot::setParam(clap_id id, double value) {
  const juce::SpinLock::ScopedLockType lock(lock_);
  return host_.setParam(id, value);
}

bool FxSlot::saveState(const juce::File& file) {
  const juce::SpinLock::ScopedLockType lock(lock_);
  const bool ok = host_.loaded() && host_.saveState(file.getFullPathName().toStdString());
  if (!ok) {
    lastError_ = "state save failed (no state extension or unwritable path)";
    log("fx " + juce::String(index_) + ": " + lastError_);
  }
  return ok;
}

bool FxSlot::loadState(const juce::File& file) {
  const juce::SpinLock::ScopedLockType lock(lock_);
  const bool ok = host_.loaded() && host_.loadState(file.getFullPathName().toStdString());
  if (!ok) {
    lastError_ = "state load failed (no state extension or unreadable file)";
    log("fx " + juce::String(index_) + ": " + lastError_);
  }
  return ok;
}

juce::String FxSlot::defaultPluginPath() {
  juce::StringArray candidates;
#if defined(MNKY_DEFAULT_PLUGIN)
  candidates.add(juce::String::fromUTF8(MNKY_DEFAULT_PLUGIN));
#endif
  const auto override = juce::SystemStats::getEnvironmentVariable("MNKY_PLUGIN_PATH", {});
  if (override.isNotEmpty()) candidates.add(override);

  const auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
  candidates.add(home.getChildFile("Library/Audio/Plug-Ins/CLAP/mnkygain.clap").getFullPathName());
  candidates.add(home.getChildFile(".clap/mnkygain.clap").getFullPathName());
  candidates.add("/usr/lib/clap/mnkygain.clap");
  candidates.add(juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                     .getParentDirectory()
                     .getChildFile("mnkygain.clap")
                     .getFullPathName());

  for (const auto& candidate : candidates) {
    if (candidate.isNotEmpty() && juce::File(candidate).exists()) return candidate;
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
