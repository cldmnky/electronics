#include "mixer_model.hpp"

#include <algorithm>
#include <cmath>

#include "../audio/fx/reverb.hpp"
#include "fx_params.hpp"
#include "logic.hpp"
#include "project_logic.hpp"

namespace mnky::app {

MixerModel::MixerModel() {
  for (int index = 0; index < audio::kMaxTracks; ++index) {
    // Identity input source: strip N listens to device channel N until a picker
    // says otherwise, which is Engine::setInputSource's documented default.
    strips_[index].inCh.store(index, std::memory_order_relaxed);
    for (auto& send : strips_[index].send) send.store(0.0f);
    active_[index].store(false, std::memory_order_relaxed);
  }
  for (auto& fxReturn : fxReturnDb_) fxReturn.store(0.0f);
}

const MixerModel::Track* MixerModel::trackAt(int position) const {
  if (position < 0 || position >= static_cast<int>(tracks_.size())) return nullptr;
  return &tracks_[static_cast<std::size_t>(position)];
}

int MixerModel::slotAt(int position) const {
  const Track* track = trackAt(position);
  return track == nullptr ? -1 : track->slot;
}

int MixerModel::positionOfSlot(int slot) const {
  for (std::size_t position = 0; position < tracks_.size(); ++position) {
    if (tracks_[position].slot == slot) return static_cast<int>(position);
  }
  return -1;
}

bool MixerModel::trackActive(int slot) const {
  if (!valid(slot)) return false;
  return active_[static_cast<std::size_t>(slot)].load(std::memory_order_relaxed);
}

bool MixerModel::addTrack(int slot, std::string name) {
  if (!valid(slot) || trackActive(slot)) return false;
  if (!name.empty()) names_[static_cast<std::size_t>(slot)] = std::move(name);
  tracks_.push_back(Track{slot, trackName(slot)});
  active_[static_cast<std::size_t>(slot)].store(true, std::memory_order_relaxed);
  return true;
}

bool MixerModel::removeTrackAt(int position) {
  const Track* track = trackAt(position);
  if (track == nullptr) return false;
  const int slot = track->slot;
  tracks_.erase(tracks_.begin() + position);
  // Config, name and takes stay; the audio gate is what makes it silent.
  active_[static_cast<std::size_t>(slot)].store(false, std::memory_order_relaxed);
  return true;
}

void MixerModel::setTrackName(int slot, std::string name) {
  if (!valid(slot)) return;
  names_[static_cast<std::size_t>(slot)] = std::move(name);
  const int position = positionOfSlot(slot);
  if (position >= 0) tracks_[static_cast<std::size_t>(position)].name = trackName(slot);
}

std::string MixerModel::trackName(int slot) const {
  if (!valid(slot)) return {};
  const auto& stored = names_[static_cast<std::size_t>(slot)];
  return stored.empty() ? defaultTrackName(slot) : stored;
}

void MixerModel::setTracks(std::vector<Track> tracks) {
  for (auto& active : active_) active.store(false, std::memory_order_relaxed);
  tracks_.clear();
  for (const auto& track : tracks) {
    if (!valid(track.slot) || trackActive(track.slot)) continue;
    if (!track.name.empty()) names_[static_cast<std::size_t>(track.slot)] = track.name;
    tracks_.push_back(Track{track.slot, trackName(track.slot)});
    active_[static_cast<std::size_t>(track.slot)].store(true, std::memory_order_relaxed);
  }
}

int MixerModel::firstInactiveSlot() const {
  for (int slot = 0; slot < audio::kMaxTracks; ++slot) {
    if (!trackActive(slot)) return slot;
  }
  return -1;
}

MixerModel::FxSlot MixerModel::fxSlot(int slot, int index) const {
  if (!valid(slot) || index < 0 || index >= kFxSlotCount) return {};
  return fxSlots_[static_cast<std::size_t>(slot)][static_cast<std::size_t>(index)];
}

void MixerModel::setFxSlot(int slot, int index, FxSlot fx) {
  if (!valid(slot) || index < 0 || index >= kFxSlotCount) return;
  if (!std::isfinite(fx.wet)) fx.wet = 1.0f;
  fx.wet = std::clamp(fx.wet, 0.0f, 1.0f);
  if (fxIsBuiltin(fx.id) &&
      fx.params.size() != fxParamTable(fx.id).size()) {
    // A freshly selected effect starts from the table's published defaults (the
    // same values the engine instance uses); replacing an id with the effect's
    // full positional vector keeps the incoming values.
    fx.params = fxDefaultParams(fx.id);
  }
  auto& target = fxSlots_[static_cast<std::size_t>(slot)][static_cast<std::size_t>(index)];
  const bool changed = target.id != fx.id || target.bypass != fx.bypass ||
                       target.wet != fx.wet || target.params != fx.params;
  target = std::move(fx);
  if (changed && fxSink_) fxSink_(slot, index);
}

void MixerModel::setFxSlotSink(FxSlotSink sink) { fxSink_ = std::move(sink); }

void MixerModel::setFxWet(int slot, int index, float wet) {
  FxSlot fx = fxSlot(slot, index);
  fx.wet = wet;
  setFxSlot(slot, index, std::move(fx));
}

void MixerModel::setFxBypass(int slot, int index, bool bypass) {
  FxSlot fx = fxSlot(slot, index);
  fx.bypass = bypass;
  setFxSlot(slot, index, std::move(fx));
}

void MixerModel::setFxParam(int slot, int index, int param, double value) {
  FxSlot fx = fxSlot(slot, index);
  fxSetParam(fx.params, fx.id, param, value);
  if (fx.id == kFxIdReverb && param == audio::fx::Reverb::kModel &&
      fx.params.size() == fxParamTable(fx.id).size()) {
    // Reverb::setModel documents the model as a preset of decay/damping/size;
    // the rack stores parameters positionally, so the preset is expanded here or
    // the engine would receive a model index with the previous decay.
    const auto& preset = audio::fx::Reverb::modelPreset(
        std::clamp(static_cast<int>(std::lround(fx.params[audio::fx::Reverb::kModel])),
                   0, audio::fx::Reverb::kModelCount - 1));
    fx.params[audio::fx::Reverb::kDecay] = preset.decay;
    fx.params[audio::fx::Reverb::kDamping] = preset.damping;
    fx.params[audio::fx::Reverb::kSize] = preset.size;
  }
  setFxSlot(slot, index, std::move(fx));
}

bool MixerModel::valid(int index) {
  return index >= 0 && index < audio::kMaxTracks;
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
  const int modeValue = strip.mode.load(std::memory_order_relaxed);
  // Only ARM arms the engine strip: an armed strip mutes its own take voice
  // (the explicit ARM mode says so), while OVERDUB has to keep the take voice
  // to layer the live input over it. OVERDUB still records (armMask below).
  out.arm = modeValue == kTrackModeArm;
  for (int send = 0; send < audio::kNumSends; ++send) {
    out.send[send] = strip.send[send].load(std::memory_order_relaxed);
  }
  out.cueSend = strip.cueSend.load(std::memory_order_relaxed);
  out.out34Send = strip.out34Send.load(std::memory_order_relaxed);
  // REC SOURCE: the app stores 0/1/2 and audio::RecSource is the same
  // order (Pre/Post/PostFx), so the snapshot carries the tap point the strip
  // chose; tapPoint stays the PRE variant selector (raw vs post-gain).
  out.recSource =
      static_cast<audio::RecSource>(clampRecSource(strip.recSource.load(std::memory_order_relaxed)));
  // Session mode values are the engine enum values: 0 Off (PLAY only), 1 Auto
  // (ARM), 2 On (OVERDUB layer), 3 InputOnly (LIVE). setChannel() /
  // setMonitorMode() sanitize the same range on the engine side.
  switch (modeValue) {
    case kTrackModePlay: out.monitor = audio::MonitorMode::Off; break;
    case kTrackModeOverdub: out.monitor = audio::MonitorMode::On; break;
    case kTrackModeLive: out.monitor = audio::MonitorMode::InputOnly; break;
    default: out.monitor = audio::MonitorMode::Auto; break;
  }
  return out;
}

void MixerModel::applyTo(audio::Engine& engine) const {
  for (int index = 0; index < audio::kMaxTracks; ++index) {
    engine.setChannel(index, channel(index));
    // Routing state is separate from audio::Channel on purpose (setChannel
    // leaves it untouched), so the same snapshot carries it: the input pickers
    // on the mixer strips and the TRACK SHIFT face reach the engine here.
    // A removed track has no input source and no output destination: silent,
    // with every setting retained for a re-add.
    const auto& strip = strips_[index];
    const bool active = trackActive(index);
    engine.setInputSource(index,
                          active ? strip.inCh.load(std::memory_order_relaxed) : -1);
    engine.setInputPair(index,
                        active && strip.inPair.load(std::memory_order_relaxed));
    engine.setOutputDestination(
        index, active ? static_cast<audio::OutputDestination>(strip.outDst.load(
                            std::memory_order_relaxed))
                      : audio::OutputDestination::None);
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
  setMode(index, arm ? kTrackModeArm : kTrackModePlay);
}

bool MixerModel::arm(int index) const { return trackModeRecords(mode(index)); }

void MixerModel::setMode(int index, int mode) {
  if (!valid(index)) return;
  strips_[index].mode.store(clampTrackMode(mode), std::memory_order_relaxed);
}

int MixerModel::mode(int index) const {
  return valid(index) ? strips_[index].mode.load(std::memory_order_relaxed)
                      : kTrackModeArm;
}

void MixerModel::setRecSource(int index, int source) {
  if (!valid(index)) return;
  strips_[index].recSource.store(clampRecSource(source), std::memory_order_relaxed);
}

int MixerModel::recSource(int index) const {
  return valid(index) ? strips_[index].recSource.load(std::memory_order_relaxed)
                      : kRecSourcePre;
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

void MixerModel::setInCh(int index, int channel) {
  if (!valid(index)) return;
  // The input source is a device capture channel, so it is bounded by the
  // device width (audio::kMaxDeviceInputs), never by the track capacity.
  int value = channel < 0 ? -1 : (channel >= audio::kMaxDeviceInputs
                                      ? audio::kMaxDeviceInputs - 1
                                      : channel);
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

MixerModel::TrackMask MixerModel::armMask() const {
  TrackMask mask = 0;
  for (int index = 0; index < audio::kMaxTracks; ++index) {
    // The owner ceiling is 64 tracks and TrackMask has 64 bits; the guard keeps
    // the shift defined if the slot store ever grows before the mask does.
    if (index >= kMaskBits) break;
    if (trackActive(index) && arm(index)) {
      mask |= TrackMask{1} << static_cast<unsigned>(index);
    }
  }
  return mask;
}

}  // namespace mnky::app
