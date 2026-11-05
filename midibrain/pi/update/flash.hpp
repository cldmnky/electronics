#pragma once
// Copy a validated UF2 image to a positively identified RP2350 BOOTSEL volume
// and wait for the bootrom to take it and reboot.
//
// The bootrom exposes a 128 MiB virtual FAT16 volume; it flashes when a .uf2
// file lands on that filesystem. This module therefore writes one file inside
// the *verified mount point* only. It never raw-writes the block device, never
// mounts anything and never touches a path that was not produced by
// findBootsel()/findBootselAt() and re-verified immediately before the copy.
#include <cstdint>
#include <functional>
#include <string>

#include "bootsel.hpp"

namespace mnky::update {

struct FlashProgress {
  std::uint32_t blocksWritten{};
  std::uint32_t blocksTotal{};
  std::uint64_t bytesWritten{};
  std::uint64_t bytesTotal{};
};

using FlashProgressFn = std::function<void(const FlashProgress&)>;

struct FlashResult {
  bool ok{};
  std::string reason{};
  std::uint32_t blocksWritten{};
  std::uint64_t bytesWritten{};
  bool deviceGone{};
  std::string writtenFile{};
};

struct FlashTiming {
  // How often to re-check that the BOOTSEL drive is still present after the
  // copy; the drive disappearing is the bootrom's "image accepted, rebooting"
  // signal.
  std::uint64_t pollIntervalMs = 100;
  std::uint64_t disappearTimeoutMs = 30000;
};

// Test seams. Production callers use the 3-argument overload; the tests inject
// fake sysfs roots, a fake presence probe and a no-op sleep.
struct FlashHooks {
  BootselPaths paths{};
  std::function<bool(const BootselDevice&)> present;
  std::function<void(std::uint64_t)> sleepMs;
  FlashTiming timing{};
};

// Name of the file created on the BOOTSEL volume. The bootrom accepts any
// .uf2 file; a fixed name makes a retry overwrite the previous attempt.
inline constexpr char kFlashFileName[] = "midibrain.uf2";

FlashResult flashUf2(const BootselDevice& device, const std::string& uf2Path,
                     const FlashProgressFn& progress);

FlashResult flashUf2(const BootselDevice& device, const std::string& uf2Path,
                     const FlashProgressFn& progress, const FlashHooks& hooks);

}  // namespace mnky::update
