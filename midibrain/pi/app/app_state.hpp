#pragma once
// UI-side app state and the intent queue. Screens and the link thread post
// intents; MainComponent performs them in order on the message thread, which
// keeps every engine/link side effect on one thread.
#include <atomic>
#include <deque>

#include <juce_core/juce_core.h>

#include "logic.hpp"

namespace mnky::app {

class AppState {
 public:
  AppState();

  void postIntent(const Intent& intent);  // any thread
  bool nextIntent(Intent& out);           // message thread

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
  // Sticky project dirty flag: set by MainComponent's state fingerprint, cleared
  // only by save/load/new.
  std::atomic<bool> dirty{false};

  juce::File takeDirectory() const;
  void setTakeDirectory(const juce::File& directory);

  // The project directory currently saved/loaded, or an invalid File when the
  // app is running untitled. UI thread only; guarded for consistency with the
  // take directory.
  juce::File currentProject() const;
  void setCurrentProject(const juce::File& directory);

  void markDirty() { dirty.store(true); }
  void clearDirty() { dirty.store(false); }

 private:
  mutable juce::CriticalSection intentLock_;
  std::deque<Intent> intents_;

  mutable juce::CriticalSection dirLock_;
  juce::File takeDirectory_;
  juce::File currentProject_;
};

}  // namespace mnky::app
