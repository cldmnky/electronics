#pragma once
// Base class for the six mnkybox screens. The tab bar owns them; MainComponent
// drives refresh() from its timer so no screen needs its own timer.
#include <juce_gui_basics/juce_gui_basics.h>

#include "../app_context.hpp"

namespace mnky::app {

class Screen : public juce::Component {
 public:
  explicit Screen(AppContext& context) : context_(context) {}
  ~Screen() override = default;

  virtual void refresh() {}

 protected:
  AppContext& context_;

  JUCE_DECLARE_NON_COPYABLE(Screen)
};

}  // namespace mnky::app
