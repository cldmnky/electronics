#pragma once
// MIDI-learn table and dispatch for the mnkybox Pi core. JUCE-free and
// host-testable: the app adds/removes bindings from the UI, registers one
// setter per target path, then feeds incoming MidiEvents to dispatch().
//
// A target is an opaque dotted app path ("mixer.ch3.gain", "fx.0.mix"); this
// module never parses or resolves it. `LearnBinding` mirrors the session
// "midimap" fields (cc 0..127, ch 1..16, target, track), so the app can copy
// listBindings() into session::MidiMapEntry entries for persistence.
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "router.hpp"

namespace mnky::midi {

inline constexpr int kLearnMinCc = 0;
inline constexpr int kLearnMaxCc = 127;
inline constexpr int kLearnMinChannel = 1;
inline constexpr int kLearnMaxChannel = 16;

// One learned control. `track` is opaque session passthrough; dispatch keys on
// (cc, ch) only, so a binding is unique per control-change on one channel.
struct LearnBinding {
  int cc = 0;
  int ch = 1;
  std::string target;
  int track = 0;
  friend bool operator==(const LearnBinding&, const LearnBinding&) = default;
};

// The binding table alone. Out-of-range rows, empty targets and duplicate
// (cc, ch) pairs are rejected; list() is ordered by cc then channel.
class LearnTable {
 public:
  bool add(int cc, int ch, std::string target, int track = 0);
  bool remove(int cc, int ch);
  void clear();
  const LearnBinding* find(int cc, int ch) const;
  std::vector<LearnBinding> list() const;

 private:
  std::map<std::pair<int, int>, LearnBinding> bindings_;
};

// Dispatch front end. Not thread-safe: the app drives add/remove and dispatch
// from its single MIDI-handling thread.
class LearnEngine {
 public:
  // Receives a control-change value scaled to 0..1 (data byte / 127).
  using Setter = std::function<void(double value)>;

  bool addBinding(int cc, int ch, std::string target, int track = 0);
  bool removeBinding(int cc, int ch);
  void clear();
  std::vector<LearnBinding> listBindings() const;

  // Registers the setter for one opaque target path; re-registering replaces
  // it and an empty setter unregisters it. Target paths are not validated.
  void registerTarget(std::string path, Setter setter);

  // Control-change only. Returns true when (cc, channel) is bound, even if the
  // target has no setter registered yet; everything else is ignored (false).
  bool dispatch(const MidiEvent& ev);

 private:
  LearnTable table_;
  std::map<std::string, Setter> setters_;
};

}  // namespace mnky::midi
