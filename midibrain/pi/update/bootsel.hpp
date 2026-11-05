#pragma once
// Positive identification of the RP2350 BOOTSEL mass-storage drive.
//
// The updater is destructive-capable, so detection is a whitelist, not a
// heuristic. A device is only reported when every one of these matches:
//   * a removable block disk under <sysfs>/block (the Pi's NVMe/SD are not
//     removable and are skipped before any identity is considered),
//   * the USB ancestor exposes the RP2350 bootrom VID:PID 2e8a:000f
//     (pico-bootrom-rp2350 src/nsboot/nsboot_usb_client.h),
//   * the disk reports the bootrom's fixed 128 MiB virtual volume size
//     (src/nsboot/usb_virtual_disk.h VOLUME_SIZE),
//   * exactly one partition of that disk is mounted as vfat/msdos at a
//     non-root mount point,
//   * /dev/disk/by-label/RP2350 points at that mount source (the bootrom's
//     DEFAULT_VOLUME_LABEL in src/nsboot/usb_virtual_disk.c),
//   * the mounted volume carries INFO_UF2.TXT with the bootrom model line
//     "Model: Raspberry Pi RP2350" (src/nsboot/info_uf2_txt.h).
//
// Any ambiguity (two candidates, a lookalike, an unreadable signature) is a
// refusal with a reason. The precondition that the volume is mounted by the
// system is deliberate: this module never mounts or raw-writes a block device.
#include <cstdint>
#include <string>

namespace mnky::update {

inline constexpr std::uint16_t kRaspberryPiVendorId = 0x2e8a;
inline constexpr std::uint16_t kRp2350BootProductId = 0x000f;
inline constexpr std::uint64_t kBootselVolumeBytes = 128ull * 1024ull * 1024ull;
inline constexpr char kBootselVolumeLabel[] = "RP2350";
inline constexpr char kBootselInfoFileName[] = "INFO_UF2.TXT";
inline constexpr char kBootselInfoModelLine[] = "Model: Raspberry Pi RP2350";

// A subset of the production filesystem roots, so tests can point the scanner
// at a fake /sys, /proc/mounts and /dev tree without touching real disks.
struct BootselPaths {
  std::string sysfsRoot{"/sys"};
  std::string mountsFile{"/proc/mounts"};
  std::string devRoot{"/dev"};
  std::string byLabelDir{"/dev/disk/by-label"};
};

// An identified, mounted BOOTSEL volume. `mountPoint` is the only path the
// flasher may write to; `blockDevice`/`partition` are the identity evidence.
struct BootselDevice {
  std::string blockDevice;  // whole disk, e.g. /dev/sda
  std::string partition;    // mounted source, e.g. /dev/sda1
  std::string mountPoint;   // verified vfat mount of that partition
  std::string label;        // "RP2350"
  std::string product;      // USB product string, diagnostic only
  std::string serial;       // USB serial, diagnostic and re-identification
  std::uint16_t vendorId{};
  std::uint16_t productId{};
  std::uint64_t sizeBytes{};  // whole-disk size from sysfs
};

struct BootselLookup {
  bool found{};
  BootselDevice device{};
  std::string reason{};  // empty only when found
};

// Production scan (Linux sysfs/mounts); refuses with a reason elsewhere.
BootselLookup findBootsel();

// Testable scan over alternate roots. Works on any host that can provide a
// fake tree; it never opens a device node.
BootselLookup findBootselAt(const BootselPaths& paths);

// True when `a` and `b` describe the same identified drive (block device,
// mounted partition, mount point, USB serial and size).
bool sameBootselDevice(const BootselDevice& a, const BootselDevice& b);

// Re-runs the full identification and requires it to still describe `device`.
// The flasher uses this immediately before writing; a changed/missing drive or
// signature is a hard refusal.
bool verifyBootselDevice(const BootselDevice& device, const BootselPaths& paths,
                         std::string& reason);

// One-line human-readable summary for the CLI/UI.
std::string describeBootsel(const BootselDevice& device);

}  // namespace mnky::update
