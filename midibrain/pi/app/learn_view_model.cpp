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

QString parameterLabel(const QString& parameter) {
  if (parameter == QStringLiteral("gain")) return QStringLiteral("GAIN");
  if (parameter == QStringLiteral("pan")) return QStringLiteral("PAN");
  if (parameter == QStringLiteral("fx1wet")) return QStringLiteral("FX1 WET");
  if (parameter == QStringLiteral("fx2wet")) return QStringLiteral("FX2 WET");
  return parameter.toUpper();
}
}  // namespace

LearnViewModel::LearnViewModel(MidiGlue& midi, QObject* parent)
    : QObject(parent), midi_(midi) {
  for (int strip = 0; strip < kNumStrips; ++strip) {
    baseTargets_ << QString::fromStdString(targetPath(strip, "gain"));
    baseTargets_ << QString::fromStdString(targetPath(strip, "pan"));
    baseTargets_ << QString::fromStdString(targetPath(strip, "fx1wet"));
    baseTargets_ << QString::fromStdString(targetPath(strip, "fx2wet"));
  }
  baseTargets_ << QStringLiteral("mixer.master.gain");
  baseTargets_ << QStringLiteral("click.level");
  refreshTargets();
  refreshBindings();
}

void LearnViewModel::setTargetIndex(int index) {
  const int clamped = index >= 0 && index < targets_.size() ? index : 0;
  if (clamped == targetIndex_) return;
  targetIndex_ = clamped;
  emit targetChanged();
}

QStringList LearnViewModel::targetLabels() const {
  QStringList labels;
  labels.reserve(targets_.size());
  for (const auto& target : targets_) labels << humanTarget(target);
  return labels;
}

QString LearnViewModel::captureTargetLabel() const {
  return captureTarget_.isEmpty() ? QString() : humanTarget(captureTarget_);
}

QString LearnViewModel::humanTarget(const QString& path) {
  if (path.startsWith(QStringLiteral("mixer.ch")) && path.count(QLatin1Char('.')) == 2) {
    const QString number = path.mid(8, 2);
    const QString parameter = path.section(QLatin1Char('.'), 2, 2);
    return QStringLiteral("Mixer %1 %2").arg(number, parameterLabel(parameter));
  }
  if (path == QStringLiteral("mixer.master.gain")) return QStringLiteral("Master GAIN");
  if (path == QStringLiteral("click.level")) return QStringLiteral("Click level");
  if (path.startsWith(QStringLiteral("routing.pipe."))) {
    // routing.pipe.<source>.<destination>.<pipe>.<param>
    const QStringList parts = path.split(QLatin1Char('.'));
    if (parts.size() == 6) {
      return QStringLiteral("Pipe %1>%2 #%3 P%4")
          .arg(parts.at(2), parts.at(3), parts.at(4), parts.at(5));
    }
  }
  return path;
}

void LearnViewModel::armTargetCapture() {
  midi_.armLearnCapture(false);
  capturing_ = false;
  captureTarget_.clear();
  targetArmed_ = true;
  statusText_ = QStringLiteral("touch a control to choose the target");
  emit targetArmedChanged();
  emit captureChanged();
  emit statusChanged();
}

void LearnViewModel::setTargetByPath(const QString& path) {
  if (!targetArmed_ || path.isEmpty()) return;
  targetArmed_ = false;
  captureTarget_ = path;
  midi_.armLearnCapture(false);
  lastCaptureSeq_ = midi_.learnCapture().seq;
  midi_.armLearnCapture(true);
  lastCaptureSeq_ = midi_.learnCapture().seq;
  capturing_ = true;
  statusText_ = QStringLiteral("target ") + humanTarget(path) +
                QStringLiteral(" - move a control on your controller");
  emit targetArmedChanged();
  emit captureChanged();
  emit statusChanged();
}

void LearnViewModel::startCapture() {
  if (targets_.isEmpty()) return;
  targetArmed_ = false;
  captureTarget_ = targets_.value(targetIndex_);
  midi_.armLearnCapture(false);
  lastCaptureSeq_ = midi_.learnCapture().seq;
  midi_.armLearnCapture(true);
  lastCaptureSeq_ = midi_.learnCapture().seq;
  capturing_ = true;
  statusText_ = QStringLiteral("move a control to learn -> ") +
                humanTarget(captureTarget_);
  emit targetArmedChanged();
  emit captureChanged();
  emit statusChanged();
}

void LearnViewModel::cancelCapture() {
  if (!capturing_ && !targetArmed_) return;
  midi_.armLearnCapture(false);
  capturing_ = false;
  targetArmed_ = false;
  captureTarget_.clear();
  statusText_ = QStringLiteral("capture off");
  emit targetArmedChanged();
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
                          .arg(humanTarget(captureTarget_))
                    : QStringLiteral("learn failed for CC %1 / ch %2")
                          .arg(capture.cc)
                          .arg(capture.ch);
  refreshBindings();
  emit captureChanged();
  emit statusChanged();
}

void LearnViewModel::refresh() {
  refreshTargets();
  refreshBindings();
}

void LearnViewModel::refreshTargets() {
  QStringList next = baseTargets_;
  // MIDI FX pipe parameters join the same dotted-path table: one path per
  // positional parameter of every configured pipe (routing.pipe.s.d.p.i).
  for (const auto& target : midi_.routePipeLearnTargets()) {
    next << QString::fromStdString(target);
  }
  if (next == targets_) return;
  targets_ = next;
  if (targetIndex_ >= targets_.size()) {
    targetIndex_ = targets_.isEmpty() ? 0 : targets_.size() - 1;
    emit targetChanged();
  }
  emit targetsChanged();
}

void LearnViewModel::refreshBindings() {
  bindingRows_ = midi_.learnBindings();
  QStringList lines;
  lines.reserve(static_cast<int>(bindingRows_.size()));
  for (const auto& binding : bindingRows_) {
    lines << QStringLiteral("CC %1 ch%2 -> %3")
                 .arg(binding.cc)
                 .arg(binding.ch)
                 .arg(humanTarget(QString::fromStdString(binding.target)));
  }
  if (lines == bindings_) return;
  bindings_ = lines;
  emit bindingsChanged();
}

}  // namespace mnky::app
