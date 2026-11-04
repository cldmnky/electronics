#pragma once
// UI-side app state and the intent queue. QML controls and the link thread post
// intents; the Qt controller performs them in order on the GUI thread, which
// keeps every engine/link side effect on one thread.
#include <atomic>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>

#include "logic.hpp"

namespace mnky::app {

class AppState {
 public:
  AppState();

  void postIntent(const Intent& intent);  // any thread
  bool nextIntent(Intent& out);           // GUI thread

  // Readable from any thread; the link thread needs the panel selection when it
  // translates EV_CONTROL frames.
  std::atomic<int> currentTab{0};
  std::atomic<int> selectedStrip{0};
  std::atomic<int> tempoX100{midi::UsbClock::kDefaultBpmX100};
  std::atomic<bool> recording{false};
  std::atomic<bool> overdub{false};
  std::atomic<bool> rolling{false};
  // Metronome settings live here so the UI, the audio callback and the session
  // save/load path all read one source. `tick` is refreshed by the link thread.
  std::atomic<std::uint32_t> tick{0};
  std::atomic<bool> clickEnabled{false};
  std::atomic<float> clickLevel{0.5f};
  std::atomic<int> clickBars{1};
  // Sticky project dirty flag: set by the controller's state fingerprint,
  // cleared only by save/load/new.
  std::atomic<bool> dirty{false};

  std::filesystem::path takeDirectory() const;
  void setTakeDirectory(const std::filesystem::path& directory);

  // The project directory currently saved/loaded, or empty when untitled.
  std::filesystem::path currentProject() const;
  void setCurrentProject(const std::filesystem::path& directory);

  void markDirty() { dirty.store(true); }
  void clearDirty() { dirty.store(false); }

 private:
  mutable std::mutex intentLock_;
  std::deque<Intent> intents_;

  mutable std::mutex dirLock_;
  std::filesystem::path takeDirectory_;
  std::filesystem::path currentProject_;
};

}  // namespace mnky::app
