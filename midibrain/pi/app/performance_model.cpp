#include "performance_model.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace mnky::app {
namespace {

double clamp01(double value) {
  if (!std::isfinite(value)) return 0.0;
  if (value < 0.0) return 0.0;
  if (value > 1.0) return 1.0;
  return value;
}

}  // namespace

// One send bus: the built-in performance effect followed by the app's CLAP
// slot. Control writes take the model lock; the audio thread only tries it, so
// a parameter write never blocks a block and the bus passes through dry for
// the one contended block (the FxSlot::Adapter rule).
class PerformanceModel::Chain final : public audio::IEffect {
 public:
  Chain(PerformanceModel& model, int grid, audio::fx::Effect& builtin)
      : model_(model), grid_(grid), builtin_(builtin) {}

  void process(float* const* ch, int nch, int n) override {
    if (ch == nullptr || nch <= 0 || n <= 0) return;
    std::unique_lock<std::mutex> guard(model_.lock_, std::try_to_lock);
    if (!guard.owns_lock()) return;
    builtin_.process(ch, nch, n);
    if (audio::IEffect* clap = clap_.load(std::memory_order_acquire)) {
      clap->process(ch, nch, n);
    }
  }

  const char* name() const override {
    return grid_ == kDelayGrid ? "delay-send" : "reverb-send";
  }

  void setSampleRate(double sampleRate) override {
    const std::lock_guard<std::mutex> guard(model_.lock_);
    builtin_.setSampleRate(sampleRate);
    if (audio::IEffect* clap = clap_.load(std::memory_order_acquire)) {
      clap->setSampleRate(sampleRate);
    }
  }

  void setClap(audio::IEffect* effect) {
    clap_.store(effect, std::memory_order_release);
  }

 private:
  PerformanceModel& model_;
  int grid_;
  audio::fx::Effect& builtin_;
  std::atomic<audio::IEffect*> clap_{nullptr};
};

PerformanceModel::PerformanceModel() {
  chains_[kDelayGrid] = std::make_unique<Chain>(*this, kDelayGrid, delay_);
  chains_[kReverbGrid] = std::make_unique<Chain>(*this, kReverbGrid, reverb_);
  // The frozen mappings, assigned through the effects' own X/Y seam.
  delay_.setXY(0, "bp_cutoff_hz", "bp_width_oct");
  reverb_.setXY(0, "size", "mix");
  // A send bus is fed a copy of the strip signal and returned by the FX return
  // level, so a fully wet effect is the useful default (the effect defaults are
  // tuned for an insert). Both stay editable through the FX PARAMS popups.
  delay_.setParam(audio::fx::Delay::kMix, 1.0);
  reverb_.setParam(audio::fx::Reverb::kMix, 1.0);
  y_[kReverbGrid] = 1.0;
  applyPoint(kDelayGrid, x_[kDelayGrid], y_[kDelayGrid]);
  applyPoint(kReverbGrid, x_[kReverbGrid], y_[kReverbGrid]);
}

PerformanceModel::~PerformanceModel() = default;

audio::IEffect& PerformanceModel::effect(int grid) {
  const int index = std::clamp(grid, 0, kGridCount - 1);
  return *chains_[static_cast<std::size_t>(index)];
}

void PerformanceModel::setClapEffect(int grid, audio::IEffect* effect) {
  const int index = std::clamp(grid, 0, kGridCount - 1);
  chains_[static_cast<std::size_t>(index)]->setClap(effect);
}

void PerformanceModel::setSampleRate(double sampleRate) {
  chains_[kDelayGrid]->setSampleRate(sampleRate);
  chains_[kReverbGrid]->setSampleRate(sampleRate);
}

double PerformanceModel::x(int grid) const {
  return x_[static_cast<std::size_t>(std::clamp(grid, 0, kGridCount - 1))];
}

double PerformanceModel::y(int grid) const {
  return y_[static_cast<std::size_t>(std::clamp(grid, 0, kGridCount - 1))];
}

void PerformanceModel::applyPoint(int grid, double x, double y) {
  if (grid == kDelayGrid) {
    delay_.setXYNormalized(0, x, y);
  } else {
    reverb_.setXYNormalized(0, x, y);
  }
}

bool PerformanceModel::setPoint(int grid, double px, double py) {
  if (grid < 0 || grid >= kGridCount) return false;
  const double nx = clamp01(px);
  const double ny = clamp01(py);
  const std::lock_guard<std::mutex> guard(lock_);
  if (grid == kReverbGrid && frozen_.load()) return false;
  x_[static_cast<std::size_t>(grid)] = nx;
  y_[static_cast<std::size_t>(grid)] = ny;
  applyPoint(grid, nx, ny);
  return true;
}

bool PerformanceModel::nudge(int grid, int dx, int dy, bool coarse) {
  if (grid < 0 || grid >= kGridCount || (dx == 0 && dy == 0)) return false;
  const double step = coarse ? kCoarseStep : kTouchStep;
  return setPoint(grid, x(grid) + dx * step, y(grid) + dy * step);
}

void PerformanceModel::setFrozen(bool frozen) {
  const std::lock_guard<std::mutex> guard(lock_);
  if (frozen == frozen_.load()) return;
  if (frozen) {
    for (int index = 0; index < audio::fx::Reverb::kParamCount; ++index) {
      savedReverb_[static_cast<std::size_t>(index)] = reverb_.param(index);
    }
    savedReverbX_ = x_[kReverbGrid];
    savedReverbY_ = y_[kReverbGrid];
    // Freeze = longest decay, fully wet, no damping (brightest tail) and the
    // largest network. The shipped header has no freeze, low-cut or output
    // level parameter, so those three manual controls have no counterpart.
    reverb_.setParam(audio::fx::Reverb::kDecay, 0.98);
    reverb_.setParam(audio::fx::Reverb::kDamping, 0.0);
    reverb_.setParam(audio::fx::Reverb::kMix, 1.0);
    reverb_.setParam(audio::fx::Reverb::kSize, 2.0);
    x_[kReverbGrid] = 1.0;
    y_[kReverbGrid] = 1.0;
    frozen_.store(true);
    return;
  }
  for (int index = 0; index < audio::fx::Reverb::kParamCount; ++index) {
    reverb_.setParam(index, savedReverb_[static_cast<std::size_t>(index)]);
  }
  x_[kReverbGrid] = savedReverbX_;
  y_[kReverbGrid] = savedReverbY_;
  frozen_.store(false);
}

double PerformanceModel::param(int grid, int index) const {
  const std::lock_guard<std::mutex> guard(lock_);
  const audio::fx::Effect& effect =
      grid == kDelayGrid ? static_cast<const audio::fx::Effect&>(delay_) : reverb_;
  return effect.param(index);
}

bool PerformanceModel::setParam(int grid, int index, double value) {
  const std::lock_guard<std::mutex> guard(lock_);
  if (grid == kReverbGrid && frozen_.load()) return false;
  audio::fx::Effect& effect =
      grid == kDelayGrid ? static_cast<audio::fx::Effect&>(delay_) : reverb_;
  return effect.setParam(index, value);
}

bool PerformanceModel::setReverbModel(int model) {
  const std::lock_guard<std::mutex> guard(lock_);
  if (frozen_.load()) return false;
  return reverb_.setModel(model);
}

std::string PerformanceModel::xText(int grid) const {
  char buffer[24] = {};
  if (grid == kDelayGrid) {
    const double hz = delay_.param(audio::fx::Delay::kBpCutoffHz);
    if (hz >= 1000.0) {
      std::snprintf(buffer, sizeof buffer, "%.2f kHz", hz / 1000.0);
    } else {
      std::snprintf(buffer, sizeof buffer, "%.0f Hz", hz);
    }
    return buffer;
  }
  std::snprintf(buffer, sizeof buffer, "%.2f size",
                reverb_.param(audio::fx::Reverb::kSize));
  return buffer;
}

std::string PerformanceModel::yText(int grid) const {
  char buffer[24] = {};
  if (grid == kDelayGrid) {
    std::snprintf(buffer, sizeof buffer, "%.2f oct",
                  delay_.param(audio::fx::Delay::kBpWidthOct));
    return buffer;
  }
  std::snprintf(buffer, sizeof buffer, "%.0f %% level",
                reverb_.param(audio::fx::Reverb::kMix) * 100.0);
  return buffer;
}

std::string PerformanceModel::gridName(int grid) const {
  return grid == kDelayGrid ? "DELAY" : "REVERB";
}

session::XyState PerformanceModel::capture() const {
  session::XyState state;
  state.delayX = static_cast<float>(x_[kDelayGrid]);
  state.delayY = static_cast<float>(y_[kDelayGrid]);
  state.reverbX = static_cast<float>(x_[kReverbGrid]);
  state.reverbY = static_cast<float>(y_[kReverbGrid]);
  return state;
}

void PerformanceModel::apply(const session::XyState& state) {
  if (frozen_.load()) setFrozen(false);
  {
    const std::lock_guard<std::mutex> guard(lock_);
    x_[kDelayGrid] = clamp01(state.delayX);
    y_[kDelayGrid] = clamp01(state.delayY);
    x_[kReverbGrid] = clamp01(state.reverbX);
    y_[kReverbGrid] = clamp01(state.reverbY);
    applyPoint(kDelayGrid, x_[kDelayGrid], y_[kDelayGrid]);
    applyPoint(kReverbGrid, x_[kReverbGrid], y_[kReverbGrid]);
  }
}

}  // namespace mnky::app
