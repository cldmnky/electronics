#pragma once
// Mixer settings shared by the message thread (UI) and the audio thread.
//
// Every value is an atomic and the audio callback reads a snapshot once per
// block (applyTo), so a UI change lands on the next block and the audio thread
// never blocks, allocates or takes a lock.
#include <atomic>
#include <cstdint>

#include "../audio/engine.hpp"

namespace mnky::app {

class MixerModel {
 public:
  MixerModel();

  audio::Channel channel(int index) const;  // snapshot for Engine::setChannel
  void applyTo(audio::Engine& engine) const;  // audio thread

  void setGainDb(int index, float db);
  float gainDb(int index) const;
  void setPan(int index, float pan);  // 0 = hard left, 0.5 = centre, 1 = right
  float pan(int index) const;
  void setMute(int index, bool mute);
  bool mute(int index) const;
  void setSolo(int index, bool solo);
  bool solo(int index) const;
  void setArm(int index, bool arm);
  bool arm(int index) const;
  void setSend(int index, int send, float level);
  float send(int index, int send) const;

  void setMasterDb(float db);
  float masterDb() const;
  void setFxReturnDb(int send, float db);
  float fxReturnDb(int send) const;

  // Bit n = strip n armed for the next take (Recorder::startTake arm mask).
  std::uint32_t armMask() const;

 private:
  static bool valid(int index);
  static bool validSend(int send);

  struct Strip {
    std::atomic<float> gainDb{0.0f};
    std::atomic<float> pan{0.5f};
    std::atomic<bool> mute{false};
    std::atomic<bool> solo{false};
    std::atomic<bool> arm{false};
    std::atomic<float> send[audio::kNumSends] = {};
  };

  Strip strips_[audio::kMaxInputs];
  std::atomic<float> masterDb_{0.0f};
  std::atomic<float> fxReturnDb_[audio::kNumSends] = {};
};

}  // namespace mnky::app
