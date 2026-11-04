#pragma once
// The two X/Y performance grids over the built-in send effects (Qt-free).
//
// Frozen mapping (USER-MANUAL section 10): delay X = feedback-filter cutoff,
// Y = width; reverb X = size, Y = level. Both come from the effects' own
// documented X/Y seam (Effect::setXY + Effect::setXYNormalized), so the
// normalization and the parameter ranges belong to audio::fx, not to a UI copy.
//
// The two instances run on the engine's FX1/FX2 send buses ahead of the CLAP
// slot adapters (see AudioGlue), so a grid move is audible on the send returns.
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>

#include "../audio/fx/delay.hpp"
#include "../audio/fx/reverb.hpp"
#include "../session/session.hpp"

namespace mnky::app {

class PerformanceModel {
 public:
  static constexpr int kDelayGrid = 0;
  static constexpr int kReverbGrid = 1;
  static constexpr int kGridCount = 2;

  // Touch and panel steps on the normalized 0..1 axes.
  static constexpr double kTouchStep = 0.02;
  static constexpr double kCoarseStep = 0.1;

  PerformanceModel();
  ~PerformanceModel();

  // Audio thread: the send-bus chain that runs the built-in effect and then the
  // CLAP adapter. Always non-null and valid for the model's lifetime.
  audio::IEffect& effect(int grid);
  // Control thread: the CLAP adapter the chain hands the bus to after the
  // built-in effect; nullptr leaves the built-in running alone.
  void setClapEffect(int grid, audio::IEffect* effect);
  // Control thread (Engine::setSampleRate forwarding on a device change).
  void setSampleRate(double sampleRate);

  double x(int grid) const;
  double y(int grid) const;
  // Normalized grid position; clamped to 0..1 and applied to the effect.
  // Returns false for an unknown grid. Reverb moves are ignored while frozen,
  // because freeze owns the reverb parameters until it is released.
  bool setPoint(int grid, double x, double y);
  // Panel EDIT step: dx/dy are -1/0/+1; `coarse` is the SHIFT step.
  bool nudge(int grid, int dx, int dy, bool coarse);

  // Reverb freeze macro. The shipped Reverb header has no freeze parameter, so
  // freeze applies the deepest/fully-wet/brightest/max-size state and restores
  // every reverb parameter on release (NOTES.md, "X/Y performance").
  bool frozen() const { return frozen_; }
  void setFrozen(bool frozen);
  void toggleFrozen() { setFrozen(!frozen_); }

  std::string xText(int grid) const;
  std::string yText(int grid) const;
  std::string gridName(int grid) const;

  // Secondary-parameter access for the popups and the tests. Writes take the
  // same lock the audio thread only ever tries, so a parameter write can never
  // race a block.
  double param(int grid, int index) const;
  bool setParam(int grid, int index, double value);
  // Reverb model preset (decay/damping/size), same lock.
  bool setReverbModel(int model);

  session::XyState capture() const;
  void apply(const session::XyState& state);

 private:
  class Chain;

  void applyPoint(int grid, double x, double y);

  audio::fx::Delay delay_;
  audio::fx::Reverb reverb_;
  std::array<std::unique_ptr<Chain>, kGridCount> chains_;
  // Grid writes (control thread) against the audio thread's process(); a
  // contended block passes the bus through dry, the FxSlot::Adapter rule.
  mutable std::mutex lock_;
  std::array<double, kGridCount> x_ = {0.5, 0.5};
  std::array<double, kGridCount> y_ = {0.5, 0.5};
  std::atomic<bool> frozen_{false};
  std::array<double, audio::fx::Reverb::kParamCount> savedReverb_{};
  double savedReverbX_ = 0.5;
  double savedReverbY_ = 0.5;
};

}  // namespace mnky::app
