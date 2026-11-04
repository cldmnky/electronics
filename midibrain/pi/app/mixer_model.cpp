#include "mixer_model.hpp"

#include "logic.hpp"

namespace mnky::app {

MixerModel::MixerModel() {
  for (auto& strip : strips_) {
    for (auto& send : strip.send) send.store(0.0f);
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
  out.gain = dbToGain(strip.gainDb.load(std::memory_order_relaxed));
  out.pan = strip.pan.load(std::memory_order_relaxed);
  out.mute = strip.mute.load(std::memory_order_relaxed);
  out.solo = strip.solo.load(std::memory_order_relaxed);
  out.arm = strip.arm.load(std::memory_order_relaxed);
  for (int send = 0; send < audio::kNumSends; ++send) {
    out.send[send] = strip.send[send].load(std::memory_order_relaxed);
  }
  return out;
}

void MixerModel::applyTo(audio::Engine& engine) const {
  for (int index = 0; index < audio::kMaxInputs; ++index) {
    engine.setChannel(index, channel(index));
  }
  engine.setMaster(dbToGain(masterDb_.load(std::memory_order_relaxed)));
  for (int send = 0; send < audio::kNumSends; ++send) {
    engine.setFxReturn(send, dbToGain(fxReturnDb_[send].load(std::memory_order_relaxed)));
  }
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
  strips_[index].pan.store(pan < 0.0f ? 0.0f : (pan > 1.0f ? 1.0f : pan),
                           std::memory_order_relaxed);
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
