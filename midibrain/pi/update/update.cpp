// Link-facing half of the Pico firmware-update orchestrator. See update.hpp.
#include "update.hpp"

#include <chrono>
#include <thread>

namespace mnky::update {
namespace {

using Clock = std::chrono::steady_clock;

constexpr std::uint64_t kStatusRequestIntervalMs = 250;
constexpr std::uint64_t kPollPauseMs = 1;

bool statusDiffers(const mb::LinkStatus& a, const mb::LinkStatus& b) {
  return a.uptime_s != b.uptime_s || a.ticks != b.ticks || a.txq_depth != b.txq_depth ||
         a.txov != b.txov || a.rxov != b.rxov || a.flags != b.flags;
}

std::uint64_t elapsedMs(Clock::time_point start) {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count());
}

}  // namespace

bool rebootToBootsel(link::PicoLink& link) { return link.rebootPico(true); }

WaitForPicoResult waitForPico(link::PicoLink& link, std::uint64_t timeoutMs) {
  WaitForPicoResult result;
  const Clock::time_point start = Clock::now();
  const Clock::time_point deadline = start + std::chrono::milliseconds(timeoutMs);
  const std::uint32_t sessionAtStart = link.session();
  const bool upAtStart = link.linkUp();
  mb::LinkStatus statusAtStart{};
  const bool hadStatusAtStart = link.lastStatus(statusAtStart);
  bool requested = false;
  std::uint64_t lastRequestMs = 0;

  while (Clock::now() < deadline) {
    link.poll();

    link::PicoLink::Event event{};
    while (link.nextEvent(event)) {
      if (event.kind != link::PicoLink::Event::Tick) result.events.push_back(event);
    }

    if (link.linkUp()) {
      const std::uint64_t nowMs = elapsedMs(start);
      if (!requested || nowMs - lastRequestMs >= kStatusRequestIntervalMs) {
        if (link.statusRequest()) {
          requested = true;
          lastRequestMs = nowMs;
        }
      }
      mb::LinkStatus status{};
      if (requested && link.lastStatus(status) &&
          (status.flags & mb::STATUS_LINK_UP) != 0) {
        const bool sessionChanged = link.session() != sessionAtStart;
        // A cached answer from before this call is only accepted when the link
        // was down at entry or the Pico session changed; otherwise wait for a
        // response that differs from the entry snapshot. STATUS_LINK_UP above
        // rejects a response the Pico produced before it saw our HELLO.
        const bool fresh = !hadStatusAtStart || statusDiffers(status, statusAtStart);
        if (sessionChanged || !upAtStart || fresh) {
          result.ok = true;
          result.session = link.session();
          result.sessionChanged = sessionChanged;
          result.statusSeen = true;
          result.status = status;
          return result;
        }
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(kPollPauseMs));
  }

  result.ok = false;
  result.session = link.session();
  result.sessionChanged = link.session() != sessionAtStart;
  result.reason = "timed out after " + std::to_string(timeoutMs) +
                  " ms waiting for the Pico to return on the link";
  return result;
}

}  // namespace mnky::update
