#pragma once
// Orchestrator API for the Pico 2 firmware-update flow. The app's Tools UI
// binds to exactly these five entry points:
//
//   validateUf2(path)              -> Uf2Validation            (uf2.hpp)
//   findBootsel()                  -> BootselLookup            (bootsel.hpp)
//   rebootToBootsel(link)          -> bool
//   flashUf2(device, path, cb)     -> FlashResult              (flash.hpp)
//   waitForPico(link, timeoutMs)   -> WaitForPicoResult
//
// `link` is a mnky::link::PicoLink; every function is synchronous and blocks
// only for the documented timeout. The caller owns polling the link outside
// waitForPico().
#include <cstdint>
#include <string>
#include <vector>

#include "../link/link.hpp"
#include "bootsel.hpp"
#include "flash.hpp"
#include "uf2.hpp"

namespace mnky::update {

// Queues EV_REBOOT with mode 1 (USB BOOTSEL) on the link. Returns false when
// the frame could not be queued. No acknowledgement exists: the Pico resets
// into the bootrom, so the SPI link goes silent until the flashed firmware
// boots again. The caller must keep polling the link so the frame is sent.
bool rebootToBootsel(link::PicoLink& link);

struct WaitForPicoResult {
  bool ok{};
  std::string reason{};
  std::uint32_t session{};
  bool sessionChanged{};  // observed a new Pico session while waiting
  bool statusSeen{};      // EV_STATUS_REQ/EV_STATUS_RESP handshake completed
  mb::LinkStatus status{};
  // The wire protocol has no firmware-version field (docs/protocol.md 0x21 is
  // uptime/ticks/counters only), so no version string is invented here.
  bool firmwareVersionExposed{};
  std::string firmwareVersion{};
  // Non-tick events drained while waiting (the app should forward them to its
  // normal event path). Tick frames are dropped so a long wait cannot overflow
  // PicoLink's 64-entry event queue.
  std::vector<link::PicoLink::Event> events;
};

// Polls the link until the Pico completes the HELLO handshake, answers a fresh
// EV_STATUS_REQ, and looks like a returned Pico (a new session, or a link that
// was down when the call started). Returns ok=false with a reason on timeout.
WaitForPicoResult waitForPico(link::PicoLink& link, std::uint64_t timeoutMs);

}  // namespace mnky::update
