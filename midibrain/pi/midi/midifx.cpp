// Wave-1 MIDI FX pipe evaluation (see midifx.hpp for the contract and the
// ownership split). Semantics mirror firmware/pico/midi_pipe.hpp so the two
// stay adoptable: same defaults, clamping, note-on-with-velocity-0-as-off rule,
// dispatcher rotation, sustain latch, fixed-length note-offs and xorshift32
// stream. The Pi-only differences are visible in the header: caller-owned
// config, a configurable seed, Wave-2 rejection instead of pass-through, and
// a dropped-event counter instead of silent truncation.
#include "midifx.hpp"

namespace mnky::midi {
namespace {

constexpr std::uint8_t typeId(PipeType type) { return static_cast<std::uint8_t>(type); }

constexpr bool isWave2(PipeType type) {
  return typeId(type) >= kFirstWave2Pipe && typeId(type) <= 16;
}

}  // namespace

const char* wave2Reason(PipeType type) noexcept {
  switch (type) {
    case PipeType::Arp:
      return "pipe 12 (Arp) is a Wave-2 clocked pipe; evaluation is rejected";
    case PipeType::Repeater:
      return "pipe 13 (Repeater) is a Wave-2 clocked pipe; evaluation is rejected";
    case PipeType::CcLfo:
      return "pipe 14 (CcLfo) is a Wave-2 clocked pipe; evaluation is rejected";
    case PipeType::Harmonizer:
      return "pipe 15 (Harmonizer) is a Wave-2 clocked pipe; evaluation is rejected";
    case PipeType::MidiDelay:
      return "pipe 16 (MidiDelay) is a Wave-2 clocked pipe; evaluation is rejected";
    default:
      return nullptr;
  }
}

bool PipeChain::configure(const PipeConfig* pipes, std::size_t count) {
  if (count > kMaxRoutePipes) return false;
  if (pipes == nullptr && count != 0) return false;
  int wave2 = -1;
  for (std::size_t i = 0; i < count; ++i) {
    const std::uint8_t type = typeId(pipes[i].type);
    if (type < 1 || type > 16) return false;
    if (pipes[i].paramCount > kMaxPipeParams) return false;
    if (!pipes[i].bypass && isWave2(pipes[i].type) && wave2 < 0)
      wave2 = static_cast<int>(i);
  }
  for (std::size_t i = 0; i < count; ++i) pipes_[i] = pipes[i];
  for (std::size_t i = count; i < kMaxRoutePipes; ++i) pipes_[i] = PipeConfig{};
  pipe_count_ = count;
  wave2_pipe_ = wave2;
  resetState();
  return true;
}

void PipeChain::setSeed(std::uint32_t seed) {
  seed_ = seed != 0 ? seed : 1u;
  rng_state_ = seed_;
}

void PipeChain::resetState() {
  dispatch_index_ = 0;
  for (auto& channel : note_channel_) channel.fill(0);
  sustain_latched_ = false;
  for (auto& channel : sustain_held_) channel.fill(0);
  for (auto& pending : pending_) pending = PendingOff{};
  rng_state_ = seed_;
}

int PipeChain::kindOf(const PipeEvent& event) {
  if (event.size < 1) return kOther;
  const std::uint8_t status = event.data[0];
  if (status >= 0xF8) return kRealtime;
  if (status == 0xF0 || status == 0xF7) return kSysex;
  if (status >= 0xF0) return kOther;
  switch (status & 0xF0) {
    case 0x80:
      return kNoteOff;
    case 0x90:
      return (event.size >= 3 && event.data[2] == 0) ? kNoteOff : kNoteOn;
    case 0xB0:
      return kCc;
    case 0xC0:
      return kPc;
    case 0xD0:
    case 0xA0:
      return kPressure;
    case 0xE0:
      return kBend;
    default:
      return kOther;
  }
}

int PipeChain::clampInt(int value, int lo, int hi) {
  if (value < lo) return lo;
  if (value > hi) return hi;
  return value;
}

int PipeChain::param(const PipeConfig& pipe, std::size_t index, int fallback) {
  return index < pipe.paramCount ? static_cast<int>(pipe.params[index]) : fallback;
}

void PipeChain::append(std::array<PipeEvent, kMaxPipeEvents>& out, std::size_t& count,
                       std::size_t& dropped, const PipeEvent& event) {
  if (count < kMaxPipeEvents) {
    out[count++] = event;
  } else {
    ++dropped;
  }
}

std::uint32_t PipeChain::nextRandom() {
  std::uint32_t x = rng_state_;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  rng_state_ = x != 0 ? x : 0x9e3779b9u;
  return rng_state_;
}

bool PipeChain::scaleAllowed(int scale, int root, std::uint16_t mask12,
                             std::array<bool, 12>& allowed) {
  allowed.fill(false);
  if (scale == 6) {
    for (int pc = 0; pc < 12; ++pc) allowed[pc] = ((mask12 >> pc) & 1u) != 0;
    return true;
  }
  const std::uint8_t* intervals = nullptr;
  std::size_t count = 0;
  switch (scale) {
    case 1: {
      static constexpr std::uint8_t kMajor[] = {0, 2, 4, 5, 7, 9, 11};
      intervals = kMajor;
      count = 7;
      break;
    }
    case 2: {
      static constexpr std::uint8_t kMinor[] = {0, 2, 3, 5, 7, 8, 10};
      intervals = kMinor;
      count = 7;
      break;
    }
    case 3: {
      static constexpr std::uint8_t kPentaMajor[] = {0, 2, 4, 7, 9};
      intervals = kPentaMajor;
      count = 5;
      break;
    }
    case 4: {
      static constexpr std::uint8_t kPentaMinor[] = {0, 3, 5, 7, 10};
      intervals = kPentaMinor;
      count = 5;
      break;
    }
    case 5: {
      static constexpr std::uint8_t kDorian[] = {0, 2, 3, 5, 7, 9, 10};
      intervals = kDorian;
      count = 7;
      break;
    }
    default:
      return false;
  }
  for (std::size_t i = 0; i < count; ++i)
    allowed[static_cast<std::size_t>((root + intervals[i]) % 12)] = true;
  return true;
}

bool PipeChain::quantizeNote(int note, int scale, int root, std::uint16_t mask12,
                             int& result) {
  std::array<bool, 12> allowed{};
  if (!scaleAllowed(scale, root, mask12, allowed)) return false;
  bool any = false;
  for (bool value : allowed) any = any || value;
  if (!any) return false;
  const int pc = note % 12;
  if (allowed[static_cast<std::size_t>(pc)]) {
    result = note;
    return true;
  }
  // Nearest pitch class, downward on a tie. Octave-wrap the result so a note
  // at the 0/127 edge still lands on a real MIDI note.
  for (int distance = 1; distance <= 6; ++distance) {
    int down = pc - distance;
    if (down < 0) down += 12;
    if (allowed[static_cast<std::size_t>(down)]) {
      int candidate = note - distance;
      if (candidate < 0) candidate += 12;
      if (candidate > 127) candidate -= 12;
      result = candidate;
      return true;
    }
    int up = pc + distance;
    if (up >= 12) up -= 12;
    if (allowed[static_cast<std::size_t>(up)]) {
      int candidate = note + distance;
      if (candidate > 127) candidate -= 12;
      if (candidate < 0) candidate += 12;
      result = candidate;
      return true;
    }
  }
  return false;
}

PipeOutput PipeChain::process(const PipeEvent& input, std::uint32_t nowMs) {
  PipeOutput out;
  if (wave2_pipe_ >= 0) {
    out.rejected = true;
    out.rejectPipe = static_cast<std::uint8_t>(wave2_pipe_);
    out.reason = wave2Reason(pipes_[static_cast<std::size_t>(wave2_pipe_)].type);
    return out;
  }
  if (input.size < 1 || input.size > 3) return out;
  return runFrom(0, input, nowMs);
}

PipeOutput PipeChain::tick(std::uint32_t nowMs) {
  PipeOutput out;
  if (wave2_pipe_ >= 0) {
    out.rejected = true;
    out.rejectPipe = static_cast<std::uint8_t>(wave2_pipe_);
    out.reason = wave2Reason(pipes_[static_cast<std::size_t>(wave2_pipe_)].type);
    return out;
  }
  for (auto& pending : pending_) {
    if (!pending.used) continue;
    if (static_cast<std::int32_t>(nowMs - pending.dueMs) < 0) continue;
    pending.used = false;
    PipeEvent off;
    off.size = 3;
    off.data[0] = static_cast<std::uint8_t>(0x80 | pending.channel);
    off.data[1] = pending.note;
    off.data[2] = 0;
    PipeOutput tail = runFrom(pending.stage, off, nowMs);
    for (std::size_t i = 0; i < tail.count; ++i) out.push(tail.events[i]);
    out.dropped += tail.dropped;
  }
  return out;
}

PipeOutput PipeChain::runFrom(std::size_t startPipe, const PipeEvent& start,
                              std::uint32_t nowMs) {
  PipeOutput out;
  std::array<PipeEvent, kMaxPipeEvents> current{};
  std::array<PipeEvent, kMaxPipeEvents> next{};
  current[0] = start;
  std::size_t currentCount = 1;
  for (std::size_t index = startPipe; index < pipe_count_ && currentCount != 0;
       ++index) {
    std::size_t nextCount = 0;
    runPipe(pipes_[index], index, current, currentCount, next, nextCount, out.dropped,
            nowMs);
    current = next;
    currentCount = nextCount;
  }
  for (std::size_t i = 0; i < currentCount; ++i) out.push(current[i]);
  return out;
}

void PipeChain::runPipe(const PipeConfig& pipe, std::size_t pipeIndex,
                        const std::array<PipeEvent, kMaxPipeEvents>& in,
                        std::size_t inCount,
                        std::array<PipeEvent, kMaxPipeEvents>& out,
                        std::size_t& outCount, std::size_t& dropped,
                        std::uint32_t nowMs) {
  outCount = 0;
  if (pipe.bypass || isWave2(pipe.type)) {
    for (std::size_t i = 0; i < inCount; ++i) append(out, outCount, dropped, in[i]);
    return;
  }
  switch (pipe.type) {
    case PipeType::Filter: {
      for (std::size_t i = 0; i < inCount; ++i) {
        const int kind = kindOf(in[i]);
        if (kind < kOther) {
          const int pass = param(pipe, static_cast<std::size_t>(kind), 1);
          if (pass == 0) continue;
        }
        append(out, outCount, dropped, in[i]);
      }
      return;
    }

    case PipeType::NoteRange: {
      int lo = clampInt(param(pipe, 0, 0), 0, 127);
      int hi = clampInt(param(pipe, 1, 127), 0, 127);
      if (lo > hi) {
        const int swap = lo;
        lo = hi;
        hi = swap;
      }
      const int velLo = clampInt(param(pipe, 2, 0), 0, 127);
      const int velHi = clampInt(param(pipe, 3, 127), 0, 127);
      for (std::size_t i = 0; i < inCount; ++i) {
        const int kind = kindOf(in[i]);
        if (kind != kNoteOn && kind != kNoteOff) {
          append(out, outCount, dropped, in[i]);
          continue;
        }
        const int note = in[i].data[1];
        if (note < lo || note > hi) continue;
        // Release velocity is commonly 0; the window is a note-on filter so a
        // high floor cannot strand a held note.
        if (kind == kNoteOn) {
          const int velocity = in[i].data[2];
          if (velocity < velLo || velocity > velHi) continue;
        }
        append(out, outCount, dropped, in[i]);
      }
      return;
    }

    case PipeType::CcRemap: {
      int inLo = clampInt(param(pipe, 0, 0), 0, 127);
      int inHi = clampInt(param(pipe, 1, 127), 0, 127);
      if (inLo > inHi) {
        const int swap = inLo;
        inLo = inHi;
        inHi = swap;
      }
      const int outLo = clampInt(param(pipe, 2, 0), 0, 127);
      const int outHi = clampInt(param(pipe, 3, 127), 0, 127);
      for (std::size_t i = 0; i < inCount; ++i) {
        PipeEvent event = in[i];
        if (event.size >= 3 && (event.data[0] & 0xF0) == 0xB0) {
          const int cc = event.data[1];
          if (cc >= inLo && cc <= inHi) {
            int mapped = outLo;
            const int span = inHi - inLo;
            if (span > 0) {
              const long long numerator =
                  static_cast<long long>(cc - inLo) * (outHi - outLo);
              const long long rounded =
                  numerator >= 0 ? (numerator + span / 2) / span
                                 : (numerator - span / 2) / span;
              mapped = outLo + static_cast<int>(rounded);
            }
            event.data[1] = static_cast<std::uint8_t>(clampInt(mapped, 0, 127));
          }
        }
        append(out, outCount, dropped, event);
      }
      return;
    }

    case PipeType::ChannelRemap: {
      const int inChannel = clampInt(param(pipe, 0, 0), 0, 16);
      const int outChannel = clampInt(param(pipe, 1, 1), 1, 16);
      for (std::size_t i = 0; i < inCount; ++i) {
        PipeEvent event = in[i];
        if (event.size >= 1 && event.data[0] >= 0x80 && event.data[0] < 0xF0) {
          const int channel = (event.data[0] & 0x0F) + 1;
          if (inChannel == 0 || channel == inChannel)
            event.data[0] = static_cast<std::uint8_t>((event.data[0] & 0xF0) |
                                                      (outChannel - 1));
        }
        append(out, outCount, dropped, event);
      }
      return;
    }

    case PipeType::ScaleQuantize: {
      const int scale = param(pipe, 0, 0);
      if (scale <= 0 || scale > 6) {
        for (std::size_t i = 0; i < inCount; ++i)
          append(out, outCount, dropped, in[i]);
        return;
      }
      const int root = clampInt(param(pipe, 1, 0), 0, 11);
      const auto mask12 =
          static_cast<std::uint16_t>(param(pipe, 2, 0x0FFF) & 0x0FFF);
      for (std::size_t i = 0; i < inCount; ++i) {
        PipeEvent event = in[i];
        const int kind = kindOf(event);
        if (kind == kNoteOn || kind == kNoteOff) {
          int quantized = event.data[1];
          if (quantizeNote(event.data[1], scale, root, mask12, quantized))
            event.data[1] = static_cast<std::uint8_t>(clampInt(quantized, 0, 127));
        }
        append(out, outCount, dropped, event);
      }
      return;
    }

    case PipeType::Chance: {
      const int percent = clampInt(param(pipe, 0, 0), 0, 100);
      for (std::size_t i = 0; i < inCount; ++i) {
        if (kindOf(in[i]) == kNoteOn && percent > 0 &&
            static_cast<int>(nextRandom() % 100u) < percent)
          continue;
        append(out, outCount, dropped, in[i]);
      }
      return;
    }

    case PipeType::VelocityRandom: {
      const int amount = clampInt(param(pipe, 0, 0), 0, 127);
      const int floorVelocity = clampInt(param(pipe, 1, 0), 0, 127);
      for (std::size_t i = 0; i < inCount; ++i) {
        PipeEvent event = in[i];
        if (kindOf(event) == kNoteOn) {
          int delta = 0;
          if (amount > 0) {
            const int span = 2 * amount + 1;
            delta = static_cast<int>(nextRandom() %
                                     static_cast<std::uint32_t>(span)) -
                    amount;
          }
          const int velocity = clampInt(static_cast<int>(event.data[2]) + delta,
                                        floorVelocity, 127);
          event.data[2] = static_cast<std::uint8_t>(velocity);
        }
        append(out, outCount, dropped, event);
      }
      return;
    }

    case PipeType::Dispatcher: {
      const int numChannels = clampInt(param(pipe, 0, 2), 2, 16);
      const int startChannel = clampInt(param(pipe, 1, 1), 1, 16);
      for (std::size_t i = 0; i < inCount; ++i) {
        PipeEvent event = in[i];
        const int kind = kindOf(event);
        if (kind == kNoteOn || kind == kNoteOff) {
          const std::size_t inputChannel =
              static_cast<std::size_t>(event.data[0] & 0x0F);
          const std::uint8_t note = event.data[1];
          if (kind == kNoteOn) {
            const int slot = static_cast<int>(
                dispatch_index_ % static_cast<std::uint32_t>(numChannels));
            const int channel = 1 + ((startChannel - 1 + slot) % 16);
            note_channel_[inputChannel][note] = static_cast<std::uint8_t>(channel);
            ++dispatch_index_;
            event.data[0] = static_cast<std::uint8_t>((event.data[0] & 0xF0) |
                                                      (channel - 1));
          } else {
            const std::uint8_t assigned = note_channel_[inputChannel][note];
            if (assigned != 0) {
              event.data[0] = static_cast<std::uint8_t>((event.data[0] & 0xF0) |
                                                        (assigned - 1));
              note_channel_[inputChannel][note] = 0;
            }
          }
        }
        append(out, outCount, dropped, event);
      }
      return;
    }

    case PipeType::Sustain: {
      const bool latchEnabled = param(pipe, 0, 0) != 0;
      if (!latchEnabled) {
        for (std::size_t i = 0; i < inCount; ++i)
          append(out, outCount, dropped, in[i]);
        return;
      }
      for (std::size_t i = 0; i < inCount; ++i) {
        const PipeEvent& event = in[i];
        const int kind = kindOf(event);
        if (kind == kCc && event.size >= 3 && event.data[1] == 64) {
          if (event.data[2] >= 64) {
            sustain_latched_ = true;
          } else {
            sustain_latched_ = false;
            for (std::size_t channel = 0; channel < sustain_held_.size(); ++channel) {
              for (int note = 0; note < 128; ++note) {
                const std::size_t word = static_cast<std::size_t>(note >> 6);
                const auto bit = static_cast<std::uint64_t>(1) << (note & 63);
                if ((sustain_held_[channel][word] & bit) == 0) continue;
                PipeEvent off;
                off.size = 3;
                off.data[0] = static_cast<std::uint8_t>(0x80 | channel);
                off.data[1] = static_cast<std::uint8_t>(note);
                off.data[2] = 0;
                append(out, outCount, dropped, off);
              }
            }
            for (auto& channel : sustain_held_) channel.fill(0);
          }
          append(out, outCount, dropped, event);
          continue;
        }
        if (kind == kNoteOff) {
          const std::size_t channel =
              static_cast<std::size_t>(event.data[0] & 0x0F);
          const int note = event.data[1];
          const std::size_t word = static_cast<std::size_t>(note >> 6);
          const auto bit = static_cast<std::uint64_t>(1) << (note & 63);
          if (sustain_latched_) {
            sustain_held_[channel][word] |= bit;
            continue;
          }
          sustain_held_[channel][word] &= ~bit;
          append(out, outCount, dropped, event);
          continue;
        }
        append(out, outCount, dropped, event);
      }
      return;
    }

    case PipeType::NoteLength: {
      int lengthMs = param(pipe, 0, 0);
      if (lengthMs <= 0) {
        for (std::size_t i = 0; i < inCount; ++i)
          append(out, outCount, dropped, in[i]);
        return;
      }
      if (lengthMs > 60000) lengthMs = 60000;
      for (std::size_t i = 0; i < inCount; ++i) {
        PipeEvent event = in[i];
        const int kind = kindOf(event);
        if (kind == kNoteOn) {
          append(out, outCount, dropped, event);
          scheduleNoteOff(event, pipeIndex, lengthMs, nowMs);
        } else if (kind == kNoteOff) {
          // A fixed-length note ignores the incoming off and waits for the
          // scheduled one; otherwise a short key press would defeat the pipe.
          if (!hasPending(event.data[0] & 0x0F, event.data[1]))
            append(out, outCount, dropped, event);
        } else {
          append(out, outCount, dropped, event);
        }
      }
      return;
    }

    case PipeType::Transform: {
      // A partial config without an output kind is not a conversion; leave the
      // bytes alone instead of silently retuning every note to 0.
      if (pipe.paramCount < 4) {
        for (std::size_t i = 0; i < inCount; ++i)
          append(out, outCount, dropped, in[i]);
        return;
      }
      const int matchKind = clampInt(param(pipe, 0, 0), 0, 4);
      const bool bendMatch = matchKind == 3;
      const int matchScale = bendMatch ? 16383 : 127;
      int matchLo = clampInt(param(pipe, 1, 0), 0, matchScale);
      int matchHi = clampInt(param(pipe, 2, matchScale), 0, matchScale);
      if (matchLo > matchHi) {
        const int swap = matchLo;
        matchLo = matchHi;
        matchHi = swap;
      }
      const int outKind = clampInt(param(pipe, 3, 0), 0, 4);
      const int outValue = clampInt(param(pipe, 4, 0), 0, 16383);
      for (std::size_t i = 0; i < inCount; ++i) {
        PipeEvent event = in[i];
        if (!transformMatches(event, matchKind, matchLo, matchHi)) {
          append(out, outCount, dropped, event);
          continue;
        }
        PipeEvent converted;
        if (makeTransform(event, outKind, outValue, converted))
          append(out, outCount, dropped, converted);
        else
          append(out, outCount, dropped, event);
      }
      return;
    }

    default:
      // Wave-2 ids are rejected before runFrom (wave2_pipe_ != -1); this arm is
      // the defensive pass-through for an unhandled id.
      for (std::size_t i = 0; i < inCount; ++i)
        append(out, outCount, dropped, in[i]);
      return;
  }
}

bool PipeChain::transformMatches(const PipeEvent& event, int matchKind, int lo,
                                 int hi) {
  const int kind = kindOf(event);
  int value = event.data[1];
  if (matchKind == 0) {
    if (kind != kNoteOn && kind != kNoteOff) return false;
  } else if (matchKind == 1) {
    if (kind != kCc) return false;
  } else if (matchKind == 2) {
    if (kind != kPc) return false;
  } else if (matchKind == 3) {
    if (kind != kBend || event.size < 3) return false;
    value = static_cast<int>(event.data[1]) |
            (static_cast<int>(event.data[2]) << 7);
  } else if (matchKind == 4) {
    if (kind != kPressure) return false;
  } else {
    return false;
  }
  return value >= lo && value <= hi;
}

bool PipeChain::makeTransform(const PipeEvent& source, int outKind, int outValue,
                              PipeEvent& out) {
  const std::uint8_t channel = source.data[0] & 0x0F;
  const int sourceKind = kindOf(source);
  switch (outKind) {
    case 0: {
      out.size = 3;
      out.data[0] = static_cast<std::uint8_t>(0x90 | channel);
      out.data[1] = static_cast<std::uint8_t>(clampInt(outValue, 0, 127));
      out.data[2] = static_cast<std::uint8_t>(
          sourceKind == kNoteOn ? source.data[2]
                                : (sourceKind == kNoteOff ? 0 : 100));
      return true;
    }
    case 1: {
      out.size = 3;
      out.data[0] = static_cast<std::uint8_t>(0xB0 | channel);
      out.data[1] = static_cast<std::uint8_t>(clampInt(outValue, 0, 127));
      out.data[2] = source.size >= 3 ? source.data[2] : 127;
      return true;
    }
    case 2: {
      out.size = 2;
      out.data[0] = static_cast<std::uint8_t>(0xC0 | channel);
      out.data[1] = static_cast<std::uint8_t>(clampInt(outValue, 0, 127));
      return true;
    }
    case 3: {
      const int bend = clampInt(outValue, 0, 16383);
      out.size = 3;
      out.data[0] = static_cast<std::uint8_t>(0xE0 | channel);
      out.data[1] = static_cast<std::uint8_t>(bend & 0x7F);
      out.data[2] = static_cast<std::uint8_t>((bend >> 7) & 0x7F);
      return true;
    }
    case 4: {
      out.size = 2;
      out.data[0] = static_cast<std::uint8_t>(0xD0 | channel);
      out.data[1] = static_cast<std::uint8_t>(clampInt(outValue, 0, 127));
      return true;
    }
    default:
      return false;
  }
}

bool PipeChain::hasPending(std::uint8_t channel, std::uint8_t note) const {
  for (const auto& pending : pending_) {
    if (pending.used && pending.channel == channel && pending.note == note)
      return true;
  }
  return false;
}

void PipeChain::scheduleNoteOff(const PipeEvent& noteOn, std::size_t pipeIndex,
                                int lengthMs, std::uint32_t nowMs) {
  const std::uint8_t channel = noteOn.data[0] & 0x0F;
  const std::uint8_t note = noteOn.data[1];
  PendingOff* slot = nullptr;
  for (auto& pending : pending_) {
    if (pending.used && pending.channel == channel && pending.note == note) {
      slot = &pending;
      break;
    }
  }
  if (slot == nullptr) {
    for (auto& pending : pending_) {
      if (!pending.used) {
        slot = &pending;
        break;
      }
    }
  }
  if (slot == nullptr) return;  // full: fall back to the incoming note-off
  slot->used = true;
  slot->dueMs = nowMs + static_cast<std::uint32_t>(lengthMs);
  slot->stage = static_cast<std::uint8_t>(pipeIndex + 1);
  slot->channel = channel;
  slot->note = note;
}

}  // namespace mnky::midi
