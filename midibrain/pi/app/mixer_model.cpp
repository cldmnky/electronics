#include "mixer_model.hpp"

#include <algorithm>

#include "logic.hpp"

namespace mnky::app {

MixerModel::MixerModel() {
  for (int index = 0; index < audio::kMaxInputs; ++index) {
    // Identity input source: strip N listens to device channel N until a picker
    // says otherwise, which is Engine::setInputSource's documented default.
    strips_[index].inCh.store(index, std::memory_order_relaxed);
    for (auto& send : strips_[index].send) send.store(0.0f);
  }
  for (auto& fxReturn : fxReturnDb_) fxReturn.store(0.0f);
}

bool MixerModel::valid(int index) {
  return index >= 0 && index < audio::kMaxInputs;
}

bool MixerModel::validSend(int send) {
  return send >= 0 && send < audio::kNumSends;
}

audio::Channel MixerModel::channel(int index) const {
  audio::Channel out;
  if (!valid(index)) return out;
  const auto& strip = strips_[index];
  // Trim and fader are separate UI controls but one engine gain (dB sum).
  out.gain = dbToGain(strip.gainDb.load(std::memory_order_relaxed) +
                      strip.trimDb.load(std::memory_order_relaxed));
  out.pan = strip.pan.load(std::memory_order_relaxed);
  out.mute = strip.mute.load(std::memory_order_relaxed);
  out.solo = strip.solo.load(std::memory_order_relaxed);
  out.arm = strip.arm.load(std::memory_order_relaxed);
  for (int send = 0; send < audio::kNumSends; ++send) {
    out.send[send] = strip.send[send].load(std::memory_order_relaxed);
  }
  out.cueSend = strip.cueSend.load(std::memory_order_relaxed);
  out.out34Send = strip.out34Send.load(std::memory_order_relaxed);
  const int monitor = strip.monitor.load(std::memory_order_relaxed);
  // Session monitor values are the engine enum values: 0 Off (PLAY only),
  // 1 Auto, 2 On (BOTH), 3 InputOnly (MON; live input always, take never).
  // setChannel()/setMonitorMode() sanitize the same range on the engine side.
  switch (monitor) {
    case 0: out.monitor = audio::MonitorMode::Off; break;
    case 2: out.monitor = audio::MonitorMode::On; break;
    case 3: out.monitor = audio::MonitorMode::InputOnly; break;
    default: out.monitor = audio::MonitorMode::Auto; break;
  }
  return out;
}

void MixerModel::applyTo(audio::Engine& engine) const {
  for (int index = 0; index < audio::kMaxInputs; ++index) {
    engine.setChannel(index, channel(index));
    // Routing state is separate from audio::Channel on purpose (setChannel
    // leaves it untouched), so the same snapshot carries it: the input pickers
    // on the mixer strips and the TRACK SHIFT face reach the engine here.
    const auto& strip = strips_[index];
    engine.setInputSource(index, strip.inCh.load(std::memory_order_relaxed));
    engine.setInputPair(index, strip.inPair.load(std::memory_order_relaxed));
    engine.setOutputDestination(
        index, static_cast<audio::OutputDestination>(strip.outDst.load(
                   std::memory_order_relaxed)));
  }
  engine.setMaster(dbToGain(masterDb_.load(std::memory_order_relaxed)));
  for (int send = 0; send < audio::kNumSends; ++send) {
    engine.setFxReturn(send, dbToGain(fxReturnDb_[send].load(std::memory_order_relaxed)));
  }
  engine.setOut34Mode(out34Mode() == 1 ? audio::Out34Mode::DualMono
                                       : audio::Out34Mode::Stereo);
}

void MixerModel::setGainDb(int index, float db) {
  if (!valid(index)) return;
  strips_[index].gainDb.store(db, std::memory_order_relaxed);
}

float MixerModel::gainDb(int index) const {
  return valid(index) ? strips_[index].gainDb.load(std::memory_order_relaxed) : 0.0f;
}

void MixerModel::setPan(int index, float pan) {
  if (!valid(index)) return;
  float value = pan < 0.0f ? 0.0f : (pan > 1.0f ? 1.0f : pan);
  // Centre detent shared by touch drag, panel EDIT and MIDI-learn writes.
  if (std::abs(value - 0.5f) <= 0.02f) value = 0.5f;
  strips_[index].pan.store(value, std::memory_order_relaxed);
}

float MixerModel::pan(int index) const {
  return valid(index) ? strips_[index].pan.load(std::memory_order_relaxed) : 0.5f;
}

void MixerModel::setMute(int index, bool mute) {
  if (valid(index)) strips_[index].mute.store(mute, std::memory_order_relaxed);
}

bool MixerModel::mute(int index) const {
  return valid(index) && strips_[index].mute.load(std::memory_order_relaxed);
}

void MixerModel::setSolo(int index, bool solo) {
  if (valid(index)) strips_[index].solo.store(solo, std::memory_order_relaxed);
}

bool MixerModel::solo(int index) const {
  return valid(index) && strips_[index].solo.load(std::memory_order_relaxed);
}

void MixerModel::setArm(int index, bool arm) {
  if (valid(index)) strips_[index].arm.store(arm, std::memory_order_relaxed);
}

bool MixerModel::arm(int index) const {
  return valid(index) && strips_[index].arm.load(std::memory_order_relaxed);
}

void MixerModel::setSend(int index, int send, float level) {
  if (!valid(index) || !validSend(send)) return;
  strips_[index].send[send].store(level < 0.0f ? 0.0f : (level > 1.0f ? 1.0f : level),
                                  std::memory_order_relaxed);
}

float MixerModel::send(int index, int send) const {
  if (!valid(index) || !validSend(send)) return 0.0f;
  return strips_[index].send[send].load(std::memory_order_relaxed);
}

void MixerModel::setTrimDb(int index, float db) {
  if (valid(index)) strips_[index].trimDb.store(db, std::memory_order_relaxed);
}

float MixerModel::trimDb(int index) const {
  return valid(index) ? strips_[index].trimDb.load(std::memory_order_relaxed) : 0.0f;
}

void MixerModel::setCueSend(int index, float level) {
  if (!valid(index)) return;
  strips_[index].cueSend.store(level < 0.0f ? 0.0f : (level > 1.0f ? 1.0f : level),
                               std::memory_order_relaxed);
}

float MixerModel::cueSend(int index) const {
  return valid(index) ? strips_[index].cueSend.load(std::memory_order_relaxed) : 0.0f;
}

void MixerModel::setOut34Send(int index, float level) {
  if (!valid(index)) return;
  strips_[index].out34Send.store(level < 0.0f ? 0.0f : (level > 1.0f ? 1.0f : level),
                                 std::memory_order_relaxed);
}

float MixerModel::out34Send(int index) const {
  return valid(index) ? strips_[index].out34Send.load(std::memory_order_relaxed) : 0.0f;
}

void MixerModel::setMonitor(int index, int mode) {
  if (!valid(index)) return;
  strips_[index].monitor.store(std::clamp(mode, 0, 3),
                               std::memory_order_relaxed);
}

int MixerModel::monitor(int index) const {
  return valid(index) ? strips_[index].monitor.load(std::memory_order_relaxed) : 1;
}

void MixerModel::setInCh(int index, int channel) {
  if (!valid(index)) return;
  int value = channel < 0 ? -1 : (channel >= audio::kMaxInputs ? audio::kMaxInputs - 1 : channel);
  // A stereo pair needs an even first channel (session contract: odd clamps
  // down), and "none" cannot be a pair.
  if (strips_[index].inPair.load(std::memory_order_relaxed) && value >= 0 &&
      value % 2 != 0) {
    --value;
  }
  strips_[index].inCh.store(value, std::memory_order_relaxed);
}

int MixerModel::inCh(int index) const {
  return valid(index) ? strips_[index].inCh.load(std::memory_order_relaxed) : 0;
}

void MixerModel::setInPair(int index, bool pair) {
  if (!valid(index)) return;
  strips_[index].inPair.store(pair, std::memory_order_relaxed);
  if (!pair) return;
  int value = strips_[index].inCh.load(std::memory_order_relaxed);
  if (value < 0) {
    value = 0;
  } else if (value % 2 != 0) {
    --value;
  }
  strips_[index].inCh.store(value, std::memory_order_relaxed);
}

bool MixerModel::inPair(int index) const {
  return valid(index) && strips_[index].inPair.load(std::memory_order_relaxed);
}

void MixerModel::setOutDst(int index, int destination) {
  if (!valid(index)) return;
  strips_[index].outDst.store(destination < 0 ? 0 : (destination > 3 ? 3 : destination),
                              std::memory_order_relaxed);
}

int MixerModel::outDst(int index) const {
  return valid(index) ? strips_[index].outDst.load(std::memory_order_relaxed) : 0;
}

void MixerModel::setOut34Mode(int mode) {
  out34Mode_.store(mode == 1 ? 1 : 0, std::memory_order_relaxed);
}

int MixerModel::out34Mode() const {
  return out34Mode_.load(std::memory_order_relaxed);
}

void MixerModel::setMasterDb(float db) {
  masterDb_.store(db, std::memory_order_relaxed);
}

float MixerModel::masterDb() const {
  return masterDb_.load(std::memory_order_relaxed);
}

void MixerModel::setFxReturnDb(int send, float db) {
  if (!validSend(send)) return;
  fxReturnDb_[send].store(db, std::memory_order_relaxed);
}

float MixerModel::fxReturnDb(int send) const {
  return validSend(send) ? fxReturnDb_[send].load(std::memory_order_relaxed) : 0.0f;
}

std::uint32_t MixerModel::armMask() const {
  std::uint32_t mask = 0;
  for (int index = 0; index < audio::kMaxInputs; ++index) {
    if (arm(index)) mask |= 1u << static_cast<unsigned>(index);
  }
  return mask;
}

}  // namespace mnky::app
