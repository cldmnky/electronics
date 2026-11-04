#include "eq_model.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace mnky::app {
namespace {

constexpr double kPi = 3.14159265358979323846;

double clampValue(double value, double min, double max) {
  if (!std::isfinite(value)) return min;
  if (value < min) return min;
  if (value > max) return max;
  return value;
}

int clampType(int type) {
  if (type < 0) return 0;
  return type > 5 ? 5 : type;
}

int clampBand(int index) {
  if (index < 0) return 0;
  return index >= EqModel::kBandCount ? EqModel::kBandCount - 1 : index;
}

}  // namespace

EqModel::EqModel() {
  // Session defaults: every band None at 1 kHz. A project without an EQ
  // section therefore loads this exact state.
  apply(session::EqState{});
}

void EqModel::setSubject(int subject) {
  const int clamped = std::clamp(subject, 0, kSubjectCount - 1);
  if (clamped == subject_) return;
  subject_ = clamped;
  curveDirty_ = true;
}

void EqModel::stepSubject(int delta) {
  setSubject((subject_ + delta % kSubjectCount + kSubjectCount) % kSubjectCount);
}

std::string EqModel::subjectName(int subject) {
  if (subject >= kTrackCount) return "MAIN";
  char buffer[16] = {};
  std::snprintf(buffer, sizeof buffer, "IN %02d", subject + 1);
  return buffer;
}

audio::fx::Eq& EqModel::effect() const {
  return effects_[static_cast<std::size_t>(std::clamp(subject_, 0, kSubjectCount - 1))];
}

int EqModel::band() const { return effect().activeBand(); }

void EqModel::setBand(int band) {
  if (effect().activeBand() == clampBand(band)) return;
  effect().setActiveBand(clampBand(band));
}

void EqModel::stepBand(int delta) {
  if (delta == 0) return;
  setBand((effect().activeBand() + delta % kBandCount + kBandCount) % kBandCount);
}

int EqModel::type(int index) const {
  return static_cast<int>(std::lround(effect().param(
      audio::fx::Eq::bandParam(clampBand(index), audio::fx::Eq::kType))));
}

double EqModel::freqHz(int index) const {
  return effect().param(
      audio::fx::Eq::bandParam(clampBand(index), audio::fx::Eq::kFreq));
}

double EqModel::gainDb(int index) const {
  return effect().param(
      audio::fx::Eq::bandParam(clampBand(index), audio::fx::Eq::kGain));
}

double EqModel::q(int index) const {
  return effect().param(
      audio::fx::Eq::bandParam(clampBand(index), audio::fx::Eq::kQ));
}

void EqModel::setType(int index, int value) {
  const int bandIndex = clampBand(index);
  if (type(bandIndex) == clampType(value)) return;
  // A cut band has no gain, so a stale shelf/parametric gain would come back
  // when the type changes again. Neutralise it on the way into a cut.
  if (!typeHasGain(clampType(value))) {
    effect().setParam(audio::fx::Eq::bandParam(bandIndex, audio::fx::Eq::kGain), 0.0);
  }
  effect().setParam(audio::fx::Eq::bandParam(bandIndex, audio::fx::Eq::kType),
                    static_cast<double>(clampType(value)));
  curveDirty_ = true;
}

void EqModel::cycleType(int index, int delta) {
  const int next = (type(index) + delta % kTypeCount + kTypeCount) % kTypeCount;
  setType(index, next);
}

void EqModel::setFreqHz(int index, double hz) {
  const double clamped = clampValue(hz, kMinFreqHz, kMaxFreqHz);
  effect().setParam(audio::fx::Eq::bandParam(clampBand(index), audio::fx::Eq::kFreq),
                    clamped);
  curveDirty_ = true;
}

void EqModel::setGainDb(int index, double db) {
  const int bandIndex = clampBand(index);
  if (!typeHasGain(type(bandIndex))) return;
  const double clamped = clampValue(db, kMinGainDb, kMaxGainDb);
  effect().setParam(audio::fx::Eq::bandParam(bandIndex, audio::fx::Eq::kGain),
                    clamped);
  curveDirty_ = true;
}

void EqModel::setQ(int index, double value) {
  const double clamped = clampValue(value, kMinQ, kMaxQ);
  effect().setParam(audio::fx::Eq::bandParam(clampBand(index), audio::fx::Eq::kQ),
                    clamped);
  curveDirty_ = true;
}

bool EqModel::typeHasGain(int value) {
  const int type = clampType(value);
  return type != 0 && type != 1 && type != 5;
}

std::string EqModel::typeShortName(int value) {
  switch (clampType(value)) {
    case 0: return "None";
    case 1: return "LCut";
    case 2: return "LShelf";
    case 3: return "Param";
    case 4: return "HShelf";
    default: return "HCut";
  }
}

const char* EqModel::typeLongName(int value) {
  return audio::fx::Eq::bandTypeName(clampType(value));
}

double EqModel::freqToX(double hz) {
  const double clamped = clampValue(hz, kMinFreqHz, kMaxFreqHz);
  return std::log(clamped / kMinFreqHz) / std::log(kMaxFreqHz / kMinFreqHz);
}

double EqModel::xToFreq(double x) {
  const double clamped = clampValue(x, 0.0, 1.0);
  return kMinFreqHz * std::pow(kMaxFreqHz / kMinFreqHz, clamped);
}

double EqModel::gainToY(double db) {
  const double clamped = clampValue(db, kMinGainDb, kMaxGainDb);
  return (clamped - kMinGainDb) / (kMaxGainDb - kMinGainDb);
}

double EqModel::yToGain(double y) {
  return kMinGainDb + clampValue(y, 0.0, 1.0) * (kMaxGainDb - kMinGainDb);
}

int EqModel::nodeAt(double x, double y, double radius) const {
  int nearest = -1;
  double best = radius;
  for (int index = 0; index < kBandCount; ++index) {
    if (type(index) == 0) continue;
    const double dx = bandX(index) - x;
    const double dy = bandY(index) - y;
    const double distance = std::sqrt(dx * dx + dy * dy);
    if (distance <= best) {
      best = distance;
      nearest = index;
    }
  }
  return nearest;
}

void EqModel::dragBand(int index, double x, double y) {
  const int bandIndex = clampBand(index);
  setFreqHz(bandIndex, xToFreq(x));
  setGainDb(bandIndex, yToGain(y));
}

void EqModel::nudgeBand(int index, int dx, int dy, bool coarse) {
  const int bandIndex = clampBand(index);
  if (dx != 0) {
    const double octaves = dx * (coarse ? kFreqStepOctavesCoarse : kFreqStepOctaves);
    setFreqHz(bandIndex, freqHz(bandIndex) * std::pow(2.0, octaves));
  }
  if (dy == 0) return;
  if (typeHasGain(type(bandIndex))) {
    setGainDb(bandIndex, gainDb(bandIndex) + dy * kGainStepDb * (coarse ? 4.0 : 1.0));
  } else {
    // USER-MANUAL section 9: Q rides SHIFT+up/down, and a cut band has no gain
    // axis at all, so up/down adjusts Q for every band without a gain.
    adjustQ(bandIndex, dy * (coarse ? 4.0 : 2.0) * kQStep);
  }
}

void EqModel::adjustFreqOctaves(int index, double octaves) {
  const int bandIndex = clampBand(index);
  setFreqHz(bandIndex, freqHz(bandIndex) * std::pow(2.0, octaves));
}

void EqModel::adjustGainDb(int index, double delta) {
  const int bandIndex = clampBand(index);
  setGainDb(bandIndex, gainDb(bandIndex) + delta);
}

void EqModel::adjustQ(int index, double delta) {
  setQ(index, q(index) + delta);
}

std::string EqModel::freqText(int index) const {
  char buffer[24] = {};
  const double hz = freqHz(index);
  if (hz >= 1000.0) {
    std::snprintf(buffer, sizeof buffer, "%.2f kHz", hz / 1000.0);
  } else {
    std::snprintf(buffer, sizeof buffer, "%.0f Hz", hz);
  }
  return buffer;
}

std::string EqModel::gainText(int index) const {
  if (!typeHasGain(type(index))) return "--";
  char buffer[24] = {};
  std::snprintf(buffer, sizeof buffer, "%+.1f dB", gainDb(index));
  return buffer;
}

std::string EqModel::qText(int index) const {
  char buffer[24] = {};
  std::snprintf(buffer, sizeof buffer, "Q %.2f", q(index));
  return buffer;
}

void EqModel::rebuildCurve() const {
  // The impulse response of the subject's real biquad cascade (all four bands
  // in series), then a rotation recurrence per display frequency instead of a
  // trig call per sample: the curve is rebuilt on every drag move.
  audio::fx::Eq& subjectEffect = effect();
  subjectEffect.setSampleRate(kCurveSampleRate);
  subjectEffect.reset();

  std::vector<float> left(kCurveImpulseFrames, 0.0f);
  std::vector<float> right(kCurveImpulseFrames, 0.0f);
  left[0] = 1.0f;
  float* channels[2] = {left.data(), right.data()};
  subjectEffect.process(channels, 2, kCurveImpulseFrames);

  curve_.assign(kCurvePoints, 0.0f);
  const double omega = 2.0 * kPi / kCurveSampleRate;
  for (int point = 0; point < kCurvePoints; ++point) {
    const double x = static_cast<double>(point) / (kCurvePoints - 1);
    const double phase = omega * xToFreq(x);
    const double stepCos = std::cos(phase);
    const double stepSin = std::sin(phase);
    double cosValue = 1.0;
    double sinValue = 0.0;
    double real = 0.0;
    double imag = 0.0;
    for (int frame = 0; frame < kCurveImpulseFrames; ++frame) {
      const double sample = left[static_cast<std::size_t>(frame)];
      real += sample * cosValue;
      imag -= sample * sinValue;
      const double nextCos = cosValue * stepCos - sinValue * stepSin;
      sinValue = sinValue * stepCos + cosValue * stepSin;
      cosValue = nextCos;
    }
    const double magnitude = std::sqrt(real * real + imag * imag);
    const double db = 20.0 * std::log10(std::max(magnitude, 1.0e-4));
    curve_[static_cast<std::size_t>(point)] =
        static_cast<float>(std::clamp(db, kMinGainDb - 6.0, kMaxGainDb + 6.0));
  }
  curveDirty_ = false;
}

const std::vector<float>& EqModel::curve() const {
  if (curveDirty_ || curve_.size() != static_cast<std::size_t>(kCurvePoints)) {
    rebuildCurve();
  }
  return curve_;
}

session::EqState EqModel::capture() const {
  session::EqState state;
  for (int subject = 0; subject < kSubjectCount; ++subject) {
    const auto& source = effects_[static_cast<std::size_t>(subject)];
    for (int band = 0; band < kBandCount; ++band) {
      session::EqBand entry;
      entry.type = static_cast<int>(std::lround(source.param(
          audio::fx::Eq::bandParam(band, audio::fx::Eq::kType))));
      entry.gainDb = static_cast<float>(source.param(
          audio::fx::Eq::bandParam(band, audio::fx::Eq::kGain)));
      entry.freqHz = static_cast<float>(source.param(
          audio::fx::Eq::bandParam(band, audio::fx::Eq::kFreq)));
      entry.q = static_cast<float>(source.param(
          audio::fx::Eq::bandParam(band, audio::fx::Eq::kQ)));
      if (subject >= kTrackCount) {
        state.main[static_cast<std::size_t>(band)] = entry;
      } else {
        state.tracks[static_cast<std::size_t>(subject)]
                    [static_cast<std::size_t>(band)] = entry;
      }
    }
  }
  return state;
}

void EqModel::apply(const session::EqState& state) {
  for (int subject = 0; subject < kSubjectCount; ++subject) {
    auto& target = effects_[static_cast<std::size_t>(subject)];
    for (int band = 0; band < kBandCount; ++band) {
      const session::EqBand& entry =
          subject >= kTrackCount
              ? state.main[static_cast<std::size_t>(band)]
              : state.tracks[static_cast<std::size_t>(subject)]
                            [static_cast<std::size_t>(band)];
      target.setParam(audio::fx::Eq::bandParam(band, audio::fx::Eq::kType),
                      entry.type);
      target.setParam(audio::fx::Eq::bandParam(band, audio::fx::Eq::kGain),
                      entry.gainDb);
      target.setParam(audio::fx::Eq::bandParam(band, audio::fx::Eq::kFreq),
                      entry.freqHz);
      target.setParam(audio::fx::Eq::bandParam(band, audio::fx::Eq::kQ), entry.q);
    }
    target.reset();
  }
  curveDirty_ = true;
}

}  // namespace mnky::app
