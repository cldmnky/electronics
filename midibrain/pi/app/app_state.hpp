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

  juce::File takeDirectory() const;
  void setTakeDirectory(const juce::File& directory);

 private:
  mutable juce::CriticalSection intentLock_;
  std::deque<Intent> intents_;

  mutable juce::CriticalSection dirLock_;
  juce::File takeDirectory_;
};

}  // namespace mnky::app
