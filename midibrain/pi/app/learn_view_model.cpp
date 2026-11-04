#include "learn_view_model.hpp"

#include <algorithm>
#include <cstdio>

namespace mnky::app {
namespace {
std::string targetPath(int strip, const char* parameter) {
  char buffer[32] = {};
  std::snprintf(buffer, sizeof buffer, "mixer.ch%02d.%s", strip + 1, parameter);
  return buffer;
}
}  // namespace

LearnViewModel::LearnViewModel(MidiGlue& midi, QObject* parent)
    : QObject(parent), midi_(midi) {
  for (int strip = 0; strip < kNumStrips; ++strip) {
    targets_ << QString::fromStdString(targetPath(strip, "gain"));
    targets_ << QString::fromStdString(targetPath(strip, "pan"));
    targets_ << QString::fromStdString(targetPath(strip, "sendA"));
    targets_ << QString::fromStdString(targetPath(strip, "sendB"));
  }
  targets_ << QStringLiteral("mixer.master.gain");
  targets_ << QStringLiteral("click.level");
  refreshBindings();
}

void LearnViewModel::setTargetIndex(int index) {
  const int clamped = index >= 0 && index < targets_.size() ? index : 0;
  if (clamped == targetIndex_) return;
  targetIndex_ = clamped;
  emit targetChanged();
}

void LearnViewModel::startCapture() {
  if (targets_.isEmpty()) return;
  captureTarget_ = targets_.value(targetIndex_);
  midi_.armLearnCapture(false);
  lastCaptureSeq_ = midi_.learnCapture().seq;
  midi_.armLearnCapture(true);
  lastCaptureSeq_ = midi_.learnCapture().seq;
  capturing_ = true;
  statusText_ = QStringLiteral("move a control to learn -> ") + captureTarget_;
  emit captureChanged();
  emit statusChanged();
}

void LearnViewModel::cancelCapture() {
  if (!capturing_) return;
  midi_.armLearnCapture(false);
  capturing_ = false;
  statusText_ = QStringLiteral("capture off");
  emit captureChanged();
  emit statusChanged();
}

void LearnViewModel::removeBinding(int index) {
  if (index < 0 || index >= static_cast<int>(bindingRows_.size())) return;
  const auto binding = bindingRows_[static_cast<std::size_t>(index)];
  midi_.removeLearnBinding(binding.cc, binding.ch);
  refreshBindings();
  statusText_ = QStringLiteral("removed CC %1 / ch %2")
                    .arg(binding.cc)
                    .arg(binding.ch);
  emit statusChanged();
}

void LearnViewModel::clearAll() {
  midi_.clearLearnBindings();
  refreshBindings();
  statusText_ = QStringLiteral("all MIDI-learn bindings removed");
  emit statusChanged();
}

void LearnViewModel::poll() {
  if (!capturing_) return;
  const auto capture = midi_.learnCapture();
  if (capture.seq == lastCaptureSeq_) return;
  lastCaptureSeq_ = capture.seq;
  midi_.armLearnCapture(false);
  capturing_ = false;
  // A second learned control on the same (cc, ch) replaces the old target: the
  // binding table keys on (cc, ch) only.
  midi_.removeLearnBinding(capture.cc, capture.ch);
  const bool added =
      midi_.addLearnBinding(capture.cc, capture.ch, captureTarget_.toStdString(), 0);
  statusText_ = added
                    ? QStringLiteral("learned CC %1 / ch %2 -> %3")
                          .arg(capture.cc)
                          .arg(capture.ch)
                          .arg(captureTarget_)
                    : QStringLiteral("learn failed for CC %1 / ch %2")
                          .arg(capture.cc)
                          .arg(capture.ch);
  refreshBindings();
  emit captureChanged();
  emit statusChanged();
}

void LearnViewModel::refresh() { refreshBindings(); }

void LearnViewModel::refreshBindings() {
  bindingRows_ = midi_.learnBindings();
  QStringList lines;
  lines.reserve(static_cast<int>(bindingRows_.size()));
  for (const auto& binding : bindingRows_) {
    lines << QStringLiteral("CC %1 / ch %2 -> %3")
                 .arg(binding.cc)
                 .arg(binding.ch)
                 .arg(QString::fromStdString(binding.target));
  }
  if (lines == bindings_) return;
  bindings_ = lines;
  emit bindingsChanged();
}

}  // namespace mnky::app
