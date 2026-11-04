#pragma once
// Project/take naming, dirty tracking and the REC+PLAY chord detector. Pure
// C++ so every rule is exercised by `mnkybox --unittests` without a UI, files or
// a transport.
#include <cstdint>
#include <string>
#include <string_view>

namespace mnky::app {

// A project and a take both become directory names under the user's Music
// folder, so the same conservative ASCII rules apply to both. A take keeps its
// `take-` prefix because the recorder scans for that pattern.
inline constexpr int kProjectNameMaxLength = 48;
inline constexpr int kTakeNameMaxLength = 48;
inline constexpr std::string_view kTakePrefix = "take-";

// Empty reason = valid. The reason is a short ASCII message for the status line.
std::string validateProjectName(std::string_view name);
std::string validateTakeName(std::string_view name);

// "Project-YYYYMMDD-HHMMSS" in UTC. `unixSeconds` is a time_t-compatible value;
// the UTC form keeps generated names identical across the dev Mac and the Pi.
std::string defaultProjectName(std::int64_t unixSeconds);

// Sticky dirty flag: the first poll only records the baseline, every later poll
// marks the state dirty when the fingerprint changed, and only clear() drops
// it. A manual undo back to the saved values therefore stays dirty until the
// next save/load/new, which is what the project UI promises.
class DirtyTracker {
 public:
  bool dirty() const { return dirty_; }

  // True when the fingerprint differs from the baseline. The first call
  // establishes the baseline and always returns false.
  bool poll(std::string_view fingerprint);
  void clear(std::string_view fingerprint);

 private:
  std::string baseline_;
  bool initialized_ = false;
  bool dirty_ = false;
};

// The two transport buttons that may form a chord. The detector only compares
// press edges and times; the caller decides whether the transport is stopped.
class ChordDetector {
 public:
  enum class Button { Play, Rec };

  static constexpr std::uint32_t kWindowMs = 300;

  // Returns true when this press completes a Play+Rec pair inside kWindowMs
  // (in either order) and consumes both edges. Presses outside the window
  // replace the stored edge.
  bool onPress(Button button, std::uint32_t nowMs);

  void reset();

 private:
  void update(Button button, std::uint32_t nowMs);
  bool has(Button button) const { return button == Button::Play ? hasPlay_ : hasRec_; }
  std::uint32_t stamp(Button button) const {
    return button == Button::Play ? playMs_ : recMs_;
  }
  void set(Button button, std::uint32_t nowMs) {
    if (button == Button::Play) {
      playMs_ = nowMs;
      hasPlay_ = true;
    } else {
      recMs_ = nowMs;
      hasRec_ = true;
    }
  }
  void clearButton(Button button) {
    if (button == Button::Play) {
      hasPlay_ = false;
    } else {
      hasRec_ = false;
    }
  }

  bool hasPlay_ = false;
  bool hasRec_ = false;
  std::uint32_t playMs_ = 0;
  std::uint32_t recMs_ = 0;
};

}  // namespace mnky::app
