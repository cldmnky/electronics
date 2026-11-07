#pragma once
// Per-output MIDI TX scheduler for the Pico UARTs. Pure logic: no Pico SDK,
// host-tested in protocol_test.cpp. main.cpp owns the UART TX interrupt that
// drains nextByte(); this class owns the queues and the priority policy.
//
// Why: sending 0xF8 with a blocking uart_putc inside the clock alarm lets a
// full TX FIFO (or a queue of ordinary MIDI bytes) delay the clock edge by
// milliseconds. The alarm now only enqueues. The UART TX interrupt drains this
// scheduler with a direct data-register write while the hardware reports
// writable space; no blocking UART helper is ever called from an ISR.
//
// Priority classes:
//   * realtime transport (0xF8 CLOCK, 0xFA START, 0xFB CONTINUE, 0xFC STOP):
//     a dedicated single-byte queue checked before every normal-message byte.
//     A realtime byte may therefore interleave BETWEEN the bytes of a longer
//     running/CC/pitch-bend message, which MIDI 1.0 explicitly permits.
//   * normal messages (1..3 bytes): one bounded slot per complete message.
//
// Overflow policy (bounded, no heap in the hot path):
//   * a normal enqueue that finds the queue full drops the oldest queued
//     normal message that has not started transmitting and is not a Note Off
//     (0x8n, or 0x9n with velocity 0). The dropped message leaves as a whole,
//     so message framing is never corrupted.
//   * if every queued normal message is a Note Off (or already in flight),
//     the NEW message is rejected instead; a queued Note Off is never dropped
//     to make room.
//   * a realtime enqueue that finds its queue full drops the new realtime
//     byte and counts it. The clock path itself never blocks.
//
// Timed release (clocked MIDI FX): a generated FX event carries the absolute
// grid deadline it belongs to (`enqueueAt`). `releaseDue(grid_now_ns)` moves
// every due event into the normal queue, ahead of ordinary not-yet-started
// messages but never ahead of a realtime byte. Realtime stays the only
// preemptive class, so a due FX event can never displace F8/FA/FB/FC: the
// clock alarm enqueues its transport byte first, then pumps this queue. The
// FX queue is a separate bounded, deadline-ordered array (no heap in the hot
// path); a realtime status is refused outright, and overflow never drops a
// Note Off.
#include <array>
#include <cstdint>

namespace mb {
class MidiTxScheduler {
 public:
  static constexpr std::uint32_t kNormalDepth = 32;
  static constexpr std::uint32_t kRealtimeDepth = 96;
  static constexpr std::uint32_t kTimedDepth = 32;
  static constexpr std::uint32_t kMaxMessageBytes = 3;

  // Queues one complete MIDI message. A one-byte realtime status (>= 0xF8)
  // takes the realtime path; everything else is a normal message.
  bool enqueue(const std::uint8_t* data, std::uint32_t size) {
    if (data == nullptr || size == 0 || size > kMaxMessageBytes) return false;
    if (size == 1 && data[0] >= 0xF8) return enqueueRealtime(data[0]);
    if (normal_count_ == kNormalDepth && !dropOldestSafe()) {
      ++rejected_;
      return false;
    }
    Slot& slot = normal_[normal_tail_];
    slot.size = static_cast<std::uint8_t>(size);
    slot.pos = 0;
    for (std::uint32_t i = 0; i < size; ++i) slot.data[i] = data[i];
    for (std::uint32_t i = size; i < kMaxMessageBytes; ++i) slot.data[i] = 0;
    normal_tail_ = (normal_tail_ + 1) % kNormalDepth;
    ++normal_count_;
    if (normal_count_ > max_normal_depth_) max_normal_depth_ = normal_count_;
    return true;
  }

  // Queues one normal message for release at absolute grid time
  // `deadline_ns`. A realtime status is refused: the clock/transport class is
  // owned by enqueueRealtime() so an FX event can never overtake the clock.
  bool enqueueAt(std::uint64_t deadline_ns, const std::uint8_t* data,
                 std::uint32_t size) {
    if (data == nullptr || size == 0 || size > kMaxMessageBytes) return false;
    if (size == 1 && data[0] >= 0xF8) return false;
    if (timed_count_ == kTimedDepth && !dropOldestSafeTimed()) {
      ++timed_rejected_;
      return false;
    }
    // Deadline order, stable for equal deadlines (generation order).
    std::uint32_t index = timed_count_;
    while (index > 0 && timed_[index - 1].deadline_ns > deadline_ns) {
      timed_[index] = timed_[index - 1];
      --index;
    }
    timed_[index].deadline_ns = deadline_ns;
    timed_[index].size = static_cast<std::uint8_t>(size);
    for (std::uint32_t i = 0; i < size; ++i) timed_[index].data[i] = data[i];
    for (std::uint32_t i = size; i < kMaxMessageBytes; ++i) timed_[index].data[i] = 0;
    ++timed_count_;
    if (timed_count_ > max_timed_depth_) max_timed_depth_ = timed_count_;
    return true;
  }

  // Promotes every timed message whose deadline has been reached. The caller
  // passes the current GRID time (the last emitted tick's deadline), not wall
  // time, so a due FX event is released only after the clock alarm has
  // enqueued that tick's F8 and can never precede it on the wire. Promoted
  // messages jump ahead of ordinary messages that have not started, keeping
  // bulk note/CC traffic from delaying a clock-derived event; realtime bytes
  // still win in nextByte(). Returns the number promoted.
  std::uint32_t releaseDue(std::uint64_t grid_now_ns) {
    std::uint32_t released = 0;
    while (timed_count_ != 0 && timed_[0].deadline_ns <= grid_now_ns) {
      if (normal_count_ == kNormalDepth && !dropOldestSafe()) break;
      std::uint32_t insert_at =
          (normal_count_ != 0 && normal_[normal_head_].pos != 0) ? 1u : 0u;
      insert_at += released;
      if (insert_at > normal_count_) insert_at = normal_count_;
      for (std::uint32_t i = normal_count_; i > insert_at; --i)
        normal_[(normal_head_ + i) % kNormalDepth] =
            normal_[(normal_head_ + i - 1) % kNormalDepth];
      Slot& slot = normal_[(normal_head_ + insert_at) % kNormalDepth];
      slot.size = timed_[0].size;
      slot.pos = 0;
      for (std::uint32_t i = 0; i < kMaxMessageBytes; ++i) slot.data[i] = timed_[0].data[i];
      normal_tail_ = (normal_tail_ + 1) % kNormalDepth;
      ++normal_count_;
      if (normal_count_ > max_normal_depth_) max_normal_depth_ = normal_count_;
      for (std::uint32_t i = 1; i < timed_count_; ++i) timed_[i - 1] = timed_[i];
      --timed_count_;
      ++released;
    }
    return released;
  }

  bool enqueueRealtime(std::uint8_t byte) {
    if (byte < 0xF8) return false;
    if (realtime_count_ == kRealtimeDepth) {
      ++realtime_dropped_;
      return false;
    }
    realtime_[realtime_tail_] = byte;
    realtime_tail_ = (realtime_tail_ + 1) % kRealtimeDepth;
    ++realtime_count_;
    if (realtime_count_ > max_realtime_depth_) max_realtime_depth_ = realtime_count_;
    return true;
  }

  // Next byte for the UART, or -1 when both classes are empty. Realtime bytes
  // always win, even mid normal message.
  int nextByte() {
    if (realtime_count_ != 0) {
      const std::uint8_t byte = realtime_[realtime_head_];
      realtime_head_ = (realtime_head_ + 1) % kRealtimeDepth;
      --realtime_count_;
      return byte;
    }
    if (normal_count_ == 0) return -1;
    Slot& slot = normal_[normal_head_];
    const std::uint8_t byte = slot.data[slot.pos++];
    if (slot.pos >= slot.size) {
      normal_head_ = (normal_head_ + 1) % kNormalDepth;
      --normal_count_;
    }
    return byte;
  }

  bool pending() const { return realtime_count_ != 0 || normal_count_ != 0; }
  std::uint32_t normalDepth() const { return normal_count_; }
  std::uint32_t realtimeDepth() const { return realtime_count_; }
  std::uint32_t maxNormalDepth() const { return max_normal_depth_; }
  std::uint32_t maxRealtimeDepth() const { return max_realtime_depth_; }
  std::uint32_t timedDepth() const { return timed_count_; }
  std::uint32_t maxTimedDepth() const { return max_timed_depth_; }
  // Timed messages dropped whole because the timed queue was full.
  std::uint64_t droppedTimed() const { return timed_dropped_; }
  // Timed messages refused because the timed queue held only Note Offs.
  std::uint64_t rejectedTimed() const { return timed_rejected_; }
  // Whole normal messages dropped under the overflow policy.
  std::uint64_t droppedNormal() const { return dropped_normal_; }
  // Realtime bytes dropped because the realtime queue was full.
  std::uint64_t droppedRealtime() const { return realtime_dropped_; }
  // New normal messages refused because no safe drop candidate existed.
  std::uint64_t rejectedNormal() const { return rejected_; }

 private:
  struct Slot {
    std::uint8_t data[kMaxMessageBytes]{};
    std::uint8_t size = 0;
    std::uint8_t pos = 0;
  };

  struct Timed {
    std::uint64_t deadline_ns = 0;
    std::uint8_t data[kMaxMessageBytes]{};
    std::uint8_t size = 0;
  };

  // A Note Off is never the overflow victim: dropping one would leave a note
  // sounding. Note-on with velocity 0 is the other legal Note Off spelling.
  static bool isNoteOffBytes(const std::uint8_t* data, std::uint8_t size) {
    if (size < 2) return false;
    const std::uint8_t status = data[0] & 0xF0;
    if (status == 0x80) return true;
    return status == 0x90 && size >= 3 && data[2] == 0;
  }

  static bool isNoteOff(const Slot& slot) { return isNoteOffBytes(slot.data, slot.size); }

  // Removes the oldest queued normal message that is safe to drop. Returns
  // false when every slot is a Note Off or the head message is partly sent.
  bool dropOldestSafe() {
    for (std::uint32_t i = 0; i < normal_count_; ++i) {
      const std::uint32_t index = (normal_head_ + i) % kNormalDepth;
      const Slot& slot = normal_[index];
      if (i == 0 && slot.pos != 0) continue;  // already on the wire
      if (isNoteOff(slot)) continue;
      for (std::uint32_t j = i; j + 1 < normal_count_; ++j) {
        normal_[(normal_head_ + j) % kNormalDepth] =
            normal_[(normal_head_ + j + 1) % kNormalDepth];
      }
      normal_tail_ = (normal_tail_ + kNormalDepth - 1) % kNormalDepth;
      --normal_count_;
      ++dropped_normal_;
      return true;
    }
    return false;
  }

  // Drops the oldest timed message that is safe to drop (not a Note Off).
  bool dropOldestSafeTimed() {
    for (std::uint32_t i = 0; i < timed_count_; ++i) {
      if (isNoteOffBytes(timed_[i].data, timed_[i].size)) continue;
      for (std::uint32_t j = i; j + 1 < timed_count_; ++j) timed_[j] = timed_[j + 1];
      --timed_count_;
      ++timed_dropped_;
      return true;
    }
    return false;
  }

  std::array<Timed, kTimedDepth> timed_{};
  std::uint32_t timed_count_ = 0;
  std::uint32_t max_timed_depth_ = 0;
  std::uint64_t timed_dropped_ = 0, timed_rejected_ = 0;

  std::array<Slot, kNormalDepth> normal_{};
  std::uint32_t normal_head_ = 0, normal_tail_ = 0, normal_count_ = 0;
  std::uint32_t max_normal_depth_ = 0;
  std::uint64_t dropped_normal_ = 0, rejected_ = 0;

  std::array<std::uint8_t, kRealtimeDepth> realtime_{};
  std::uint32_t realtime_head_ = 0, realtime_tail_ = 0, realtime_count_ = 0;
  std::uint32_t max_realtime_depth_ = 0;
  std::uint64_t realtime_dropped_ = 0;
};

}  // namespace mb
