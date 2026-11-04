#include "learn.hpp"

#include <cstdint>
#include <utility>

namespace mnky::midi {

// ------------------------------------------------------------------- table

bool LearnTable::add(int cc, int ch, std::string target, int track) {
  if (cc < kLearnMinCc || cc > kLearnMaxCc || ch < kLearnMinChannel ||
      ch > kLearnMaxChannel || target.empty()) {
    return false;
  }
  const std::pair<int, int> key{cc, ch};
  if (bindings_.find(key) != bindings_.end()) return false;
  bindings_.emplace(key, LearnBinding{cc, ch, std::move(target), track});
  return true;
}

bool LearnTable::remove(int cc, int ch) {
  return bindings_.erase(std::pair<int, int>{cc, ch}) != 0;
}

void LearnTable::clear() { bindings_.clear(); }

const LearnBinding* LearnTable::find(int cc, int ch) const {
  const auto it = bindings_.find(std::pair<int, int>{cc, ch});
  return it == bindings_.end() ? nullptr : &it->second;
}

std::vector<LearnBinding> LearnTable::list() const {
  std::vector<LearnBinding> out;
  out.reserve(bindings_.size());
  for (const auto& entry : bindings_) out.push_back(entry.second);
  return out;
}

// ------------------------------------------------------------------ engine

bool LearnEngine::addBinding(int cc, int ch, std::string target, int track) {
  return table_.add(cc, ch, std::move(target), track);
}

bool LearnEngine::removeBinding(int cc, int ch) { return table_.remove(cc, ch); }

void LearnEngine::clear() { table_.clear(); }

std::vector<LearnBinding> LearnEngine::listBindings() const { return table_.list(); }

void LearnEngine::registerTarget(std::string path, Setter setter) {
  if (path.empty()) return;
  if (!setter) {
    setters_.erase(path);
    return;
  }
  setters_.insert_or_assign(std::move(path), std::move(setter));
}

bool LearnEngine::dispatch(const MidiEvent& ev) {
  constexpr std::uint8_t kStatusMask = 0xF0;
  constexpr std::uint8_t kControlChange = 0xB0;
  if (ev.size != 3) return false;
  if ((ev.data[0] & kStatusMask) != kControlChange) return false;

  const int ch = static_cast<int>(ev.data[0] & 0x0F) + 1;
  const LearnBinding* binding = table_.find(ev.data[1], ch);
  if (binding == nullptr) return false;

  const auto setter = setters_.find(binding->target);
  if (setter != setters_.end() && setter->second) {
    setter->second(static_cast<double>(ev.data[2] & 0x7F) / 127.0);
  }
  return true;
}

}  // namespace mnky::midi
