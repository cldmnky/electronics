#pragma once
// Mixer settings shared by the message thread (UI) and the audio thread.
//
// Every value is an atomic and the audio callback reads a snapshot once per
// block (applyTo), so a UI change lands on the next block and the audio thread
// never blocks, allocates or takes a lock.
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "../audio/engine.hpp"

namespace mnky::app {

class MixerModel {
 public:
  // One bit per track slot. The audio::Recorder mask is uint64_t and the owner's
  // track ceiling is audio::kMaxTracks (64), so this named type is the app's
  // single mask width: every consumer names it instead of a literal, and a
  // capacity change is a change here plus the recorder's matching type.
  using TrackMask = std::uint64_t;
  static constexpr int kMaskBits = std::numeric_limits<TrackMask>::digits;

  // One active mixer track: the fixed per-slot configuration store plus the
  // ordered list of slots the mixer shows. The owner's contract is that the
  // mixer starts empty, ADD TRACK appends a slot and REMOVE TRACK deletes the
  // strip while the slot's configuration and takes stay in the project; order
  // is the list order, and a re-add of a removed slot restores its settings.
  struct Track {
    int slot = 0;     // 0..audio::kMaxTracks-1
    std::string name;
  };
  // Per-track FX insert (session v2 mixer.channels[].insert). The engine
  // instance path lands in parallel; this model carries the selector, the
  // bypass and the parameter vector the channel popout and the session need.
  struct Insert {
    std::string id;  // empty = none; "builtin.delay" | "builtin.reverb" | CLAP id
    bool bypass = false;
    std::vector<double> params;
  };

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
  // Per-track input monitoring (session v2 mixer.channels[].monitor, MPC
  // parity): 0 = PLAY (take only, never live input), 1 = AUTO (live when armed
  // or no take), 2 = BOTH (live input layered over the take), 3 = MON (live
  // input always, take never). Clamped to 0..3 and passed to
  // audio::MonitorMode::InputOnly (Engine::setMonitorMode value 3).
  void setMonitor(int index, int mode);
  int monitor(int index) const;
  // Per-track I/O assignment (session v2 mixer.channels[].inCh/inPair/outDst):
  // `inCh` is the mono device channel 0..17 or -1 for none, `inPair` binds the
  // next channel as a stereo pair (inCh is forced even), `outDst` is
  // 0 = Main 1&2, 1 = OUT3&4, 2 = phones, 3 = none. A new strip is identity
  // (strip N <- device channel N), as Engine::setInputSource documents, and
  // applyTo() forwards the triple to the engine every block so a picker edit
  // lands on the next block like every other mixer value.
  void setInCh(int index, int channel);
  int inCh(int index) const;
  void setInPair(int index, bool pair);
  bool inPair(int index) const;
  void setOutDst(int index, int destination);
  int outDst(int index) const;

  // 0 = OUT3&4 stereo pair, 1 = two mono sends (session v2 mixer.out34mode).
  // ---- dynamic track list -------------------------------------------------
  // UI-thread only: the audio callback never reads the list itself, it reads
  // the per-slot `active` flag that addTrack/removeTrack publishes, so an
  // inactive slot is silenced (inCh -1, outDst none) while its configuration
  // stays in strips_ for a later re-add. A new project starts with no tracks.
  int trackCount() const { return static_cast<int>(tracks_.size()); }
  const Track* trackAt(int position) const;
  int slotAt(int position) const;
  int positionOfSlot(int slot) const;
  bool trackActive(int slot) const;
  // Appends `slot`. Fails (returns false) for an invalid slot or one that is
  // already active. An empty `name` keeps the stored name or the default.
  bool addTrack(int slot, std::string name);
  // Removes the track at `position`; configuration, name and takes stay.
  bool removeTrackAt(int position);
  void setTrackName(int slot, std::string name);
  std::string trackName(int slot) const;
  // Session load: replaces the whole ordered list (one begin/end reset in the
  // view model) and marks exactly those slots active.
  void setTracks(std::vector<Track> tracks);
  int firstInactiveSlot() const;

  // ---- per-track FX insert ------------------------------------------------
  Insert insert(int slot) const;
  void setInsert(int slot, Insert insert);

  void setOut34Mode(int mode);
  int out34Mode() const;

  void setMasterDb(float db);
  float masterDb() const;
  void setFxReturnDb(int send, float db);
  float fxReturnDb(int send) const;

  // Bit n = active slot n armed for the next take (Recorder::startTake takes
  // the same 64-bit mask). Inactive slots are never in the mask: they are
  // silent and must not record.
  TrackMask armMask() const;

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
    std::atomic<int> inCh{0};
    std::atomic<bool> inPair{false};
    std::atomic<int> outDst{0};
  };

  // The slot store follows the engine's track capacity (audio::kMaxTracks), not
  // the device capture width: every track is mono or stereo and owns one or two
  // internal channels.
  Strip strips_[audio::kMaxTracks];
  // Audio-thread gate: addTrack/removeTrack publish it and applyTo reads it.
  std::array<std::atomic<bool>, audio::kMaxTracks> active_{};
  // UI-thread state below (never touched by the audio callback).
  std::vector<Track> tracks_;
  std::array<std::string, audio::kMaxTracks> names_{};
  std::array<Insert, audio::kMaxTracks> inserts_{};
  std::atomic<int> out34Mode_{0};
  std::atomic<float> masterDb_{0.0f};
  std::atomic<float> fxReturnDb_[audio::kNumSends] = {};
};

}  // namespace mnky::app
