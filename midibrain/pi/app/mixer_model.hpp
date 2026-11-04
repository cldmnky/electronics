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
  // App-side input trim in dB. The engine exposes one per-channel gain, so the
  // mixer applies trim + fader as a dB sum; the session stores the combined
  // value in mixer.channels[].gain (see project_io.cpp).
  void setTrimDb(int index, float db);
  float trimDb(int index) const;
  // Monitor sends from session v2: CUE bus and OUT3&4. The engine has no cue /
  // out34 processing yet, so these values are held here for the UI/session and
  // are not audible until the audio worker adds the hooks.
  void setCueSend(int index, float level);
  float cueSend(int index) const;
  void setOut34Send(int index, float level);
  float out34Send(int index) const;
  // Per-track input monitoring (session v2 mixer.channels[].monitor):
  // 0 = off (take only), 1 = auto (live when armed or no take), 2 = on (layer
  // live input over a take). Clamped to 0..2.
  void setMonitor(int index, int mode);
  int monitor(int index) const;

  // 0 = OUT3&4 stereo pair, 1 = two mono sends (session v2 mixer.out34mode).
  void setOut34Mode(int mode);
  int out34Mode() const;

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
    std::atomic<float> trimDb{0.0f};
    std::atomic<float> cueSend{0.0f};
    std::atomic<float> out34Send{0.0f};
    std::atomic<int> monitor{1};
  };

  Strip strips_[audio::kMaxInputs];
  std::atomic<int> out34Mode_{0};
  std::atomic<float> masterDb_{0.0f};
  std::atomic<float> fxReturnDb_[audio::kNumSends] = {};
};

}  // namespace mnky::app
