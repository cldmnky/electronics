// RP2350 BOOTSEL drive identification. See bootsel.hpp for the positive rule.
#include "bootsel.hpp"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace mnky::update {
namespace {

namespace fs = std::filesystem;

constexpr std::size_t kMaxSignatureFileBytes = 4096;
constexpr int kMaxUsbAncestorDepth = 16;
constexpr std::uint64_t kBlockSectorBytes = 512;

std::string trim(std::string text) {
  const auto notSpace = [](unsigned char c) { return std::isspace(c) == 0; };
  text.erase(text.begin(), std::find_if(text.begin(), text.end(), notSpace));
  text.erase(std::find_if(text.rbegin(), text.rend(), notSpace).base(), text.end());
  return text;
}

bool readSmallTextFile(const fs::path& path, std::string& out) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return false;
  out.clear();
  char buffer[256];
  while (file && out.size() < kMaxSignatureFileBytes) {
    file.read(buffer, sizeof(buffer));
    out.append(buffer, static_cast<std::size_t>(file.gcount()));
  }
  return file.eof() || file.good() || out.size() >= kMaxSignatureFileBytes;
}

std::optional<std::uint64_t> readUnsignedFile(const fs::path& path, int base) {
  std::string text;
  if (!readSmallTextFile(path, text)) return std::nullopt;
  text = trim(std::move(text));
  if (text.empty()) return std::nullopt;
  errno = 0;
  char* end = nullptr;
  const unsigned long long value = std::strtoull(text.c_str(), &end, base);
  if (errno != 0 || end == text.c_str() || *end != '\0') return std::nullopt;
  return static_cast<std::uint64_t>(value);
}

// /proc/mounts escapes space, tab, newline and backslash as \ooo.
std::string decodeMountEscapes(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '\\' && i + 3 < text.size() && text[i + 1] >= '0' && text[i + 1] <= '3' &&
        text[i + 2] >= '0' && text[i + 2] <= '7' && text[i + 3] >= '0' && text[i + 3] <= '7') {
      const int value = (text[i + 1] - '0') * 64 + (text[i + 2] - '0') * 8 + (text[i + 3] - '0');
      out.push_back(static_cast<char>(value));
      i += 3;
    } else {
      out.push_back(text[i]);
    }
  }
  return out;
}

struct MountEntry {
  std::string source;
  std::string mountPoint;
  std::string fsType;
};

std::vector<MountEntry> readMounts(const std::string& path) {
  std::vector<MountEntry> entries;
  std::ifstream file(path);
  std::string line;
  while (std::getline(file, line)) {
    std::istringstream stream(line);
    std::string source;
    std::string mountPoint;
    std::string fsType;
    if (!(stream >> source >> mountPoint >> fsType)) continue;
    entries.push_back(
        MountEntry{decodeMountEscapes(source), decodeMountEscapes(mountPoint), fsType});
  }
  return entries;
}

fs::path canonicalOrSelf(const fs::path& path) {
  std::error_code ec;
  const fs::path canonical = fs::weakly_canonical(path, ec);
  return ec ? path : canonical;
}

struct UsbIdentity {
  std::uint16_t vendorId{};
  std::uint16_t productId{};
  std::string product;
  std::string serial;
};

std::optional<UsbIdentity> readUsbIdentity(const fs::path& deviceLink) {
  fs::path current = canonicalOrSelf(deviceLink);
  for (int depth = 0; depth < kMaxUsbAncestorDepth; ++depth) {
    const auto vendor = readUnsignedFile(current / "idVendor", 16);
    const auto product = readUnsignedFile(current / "idProduct", 16);
    if (vendor && product) {
      if (*vendor > 0xffffu || *product > 0xffffu) return std::nullopt;
      UsbIdentity identity;
      identity.vendorId = static_cast<std::uint16_t>(*vendor);
      identity.productId = static_cast<std::uint16_t>(*product);
      readSmallTextFile(current / "product", identity.product);
      identity.product = trim(std::move(identity.product));
      readSmallTextFile(current / "serial", identity.serial);
      identity.serial = trim(std::move(identity.serial));
      return identity;
    }
    if (!current.has_parent_path() || current.parent_path() == current) return std::nullopt;
    current = current.parent_path();
  }
  return std::nullopt;
}

bool sourceIsPartitionOf(const fs::path& source, const fs::path& devRoot,
                         const std::string& diskName) {
  const fs::path canonicalSource = canonicalOrSelf(source);
  const fs::path canonicalDevRoot = canonicalOrSelf(devRoot);
  if (canonicalSource.parent_path() != canonicalDevRoot) return false;
  const std::string leaf = canonicalSource.filename().string();
  if (leaf == diskName) return true;
  if (leaf.rfind(diskName, 0) != 0) return false;
  const std::string suffix = leaf.substr(diskName.size());
  if (suffix.empty()) return false;
  return std::all_of(suffix.begin(), suffix.end(),
                     [](unsigned char c) { return std::isdigit(c) != 0; });
}

bool sameFile(const fs::path& a, const fs::path& b) {
  std::error_code ec;
  const bool equivalent = fs::equivalent(a, b, ec);
  return !ec && equivalent;
}

bool volumeLabelMatches(const fs::path& byLabelDir, const fs::path& source,
                        std::string& reason) {
  const fs::path labelLink = byLabelDir / kBootselVolumeLabel;
  std::error_code ec;
  if (!fs::exists(labelLink, ec)) {
    reason = "volume label '" + std::string(kBootselVolumeLabel) + "' not found under " +
             byLabelDir.string();
    return false;
  }
  if (!sameFile(labelLink, source)) {
    reason = "volume label '" + std::string(kBootselVolumeLabel) + "' (" +
             canonicalOrSelf(labelLink).string() + ") does not point at the mount source " +
             canonicalOrSelf(source).string();
    return false;
  }
  return true;
}

bool infoSignatureMatches(const fs::path& mountPoint, std::string& reason) {
  std::string info;
  const fs::path infoPath = mountPoint / kBootselInfoFileName;
  if (!readSmallTextFile(infoPath, info)) {
    reason = "cannot read " + infoPath.string() + " from the mounted volume";
    return false;
  }
  if (info.find(kBootselInfoModelLine) == std::string::npos) {
    reason = infoPath.string() + " does not carry the RP2350 model line '" +
             std::string(kBootselInfoModelLine) + "'";
    return false;
  }
  return true;
}

std::string sizeText(std::uint64_t bytes) {
  return std::to_string(bytes) + " bytes (" + std::to_string(bytes / (1024 * 1024)) + " MiB)";
}

std::string refusalReason(const std::vector<std::string>& rejections) {
  if (rejections.empty()) return "no removable block devices under the sysfs block tree";
  std::string reason = "no RP2350 BOOTSEL drive: ";
  const std::size_t limit = std::min<std::size_t>(rejections.size(), 3);
  for (std::size_t i = 0; i < limit; ++i) {
    if (i != 0) reason += "; ";
    reason += rejections[i];
  }
  if (rejections.size() > limit) {
    reason += "; ... " + std::to_string(rejections.size() - limit) + " more candidate(s)";
  }
  return reason;
}

}  // namespace

BootselLookup findBootselAt(const BootselPaths& paths) {
  BootselLookup result;
  const fs::path blockRoot = fs::path(paths.sysfsRoot) / "block";
  std::error_code ec;
  if (!fs::is_directory(blockRoot, ec)) {
    result.reason = "sysfs block tree " + blockRoot.string() + " is not available";
    return result;
  }

  const std::vector<MountEntry> mounts = readMounts(paths.mountsFile);
  std::vector<BootselDevice> candidates;
  std::vector<std::string> rejections;

  fs::directory_iterator iterator(blockRoot, ec);
  if (ec) {
    result.reason = "cannot list " + blockRoot.string() + ": " + ec.message();
    return result;
  }
  for (const fs::directory_entry& entry : iterator) {
    if (!entry.is_directory(ec)) continue;
    const std::string diskName = entry.path().filename().string();
    const std::string removable = [&entry] {
      std::string text;
      readSmallTextFile(entry.path() / "removable", text);
      return trim(std::move(text));
    }();
    if (removable != "1") continue;  // never a candidate: NVMe/SD/system disks

    const auto sectors = readUnsignedFile(entry.path() / "size", 10);
    if (!sectors || *sectors > UINT64_MAX / kBlockSectorBytes) {
      rejections.push_back(diskName + ": unreadable block size");
      continue;
    }
    const std::uint64_t sizeBytes = *sectors * kBlockSectorBytes;

    const auto identity = readUsbIdentity(entry.path() / "device");
    if (!identity) {
      rejections.push_back(diskName + ": no USB identity");
      continue;
    }
    if (identity->vendorId == 0 || identity->productId == 0) {
      rejections.push_back(diskName + ": malformed USB identity");
      continue;
    }
    if (identity->vendorId != kRaspberryPiVendorId ||
        identity->productId != kRp2350BootProductId) {
      char id[16];
      std::snprintf(id, sizeof(id), "%04x:%04x", static_cast<unsigned>(identity->vendorId),
                    static_cast<unsigned>(identity->productId));
      rejections.push_back(diskName + ": USB " + id + " is not the RP2350 bootloader 2e8a:000f");
      continue;
    }
    if (sizeBytes != kBootselVolumeBytes) {
      rejections.push_back(diskName + ": volume size " + sizeText(sizeBytes) +
                           " does not match the RP2350 BOOTSEL " +
                           sizeText(kBootselVolumeBytes));
      continue;
    }

    std::vector<MountEntry> diskMounts;
    for (const MountEntry& mount : mounts) {
      if (sourceIsPartitionOf(mount.source, paths.devRoot, diskName)) {
        diskMounts.push_back(mount);
      }
    }
    if (diskMounts.empty()) {
      rejections.push_back(diskName + ": not mounted (mount the RP2350 volume first)");
      continue;
    }
    if (diskMounts.size() > 1) {
      rejections.push_back(diskName + ": multiple partitions are mounted; ambiguous");
      continue;
    }

    const MountEntry& mount = diskMounts.front();
    const fs::path mountPoint(mount.mountPoint);
    if (!mountPoint.is_absolute() || mountPoint == mountPoint.root_path()) {
      rejections.push_back(diskName + ": refusing to use the root filesystem as a BOOTSEL volume");
      continue;
    }
    if (mount.fsType != "vfat" && mount.fsType != "msdos") {
      rejections.push_back(diskName + ": mounted filesystem '" + mount.fsType +
                           "' is not the BOOTSEL FAT volume");
      continue;
    }
    if (!fs::is_directory(mountPoint, ec)) {
      rejections.push_back(diskName + ": mount point " + mountPoint.string() +
                           " is not a directory");
      continue;
    }

    std::string reason;
    if (!volumeLabelMatches(fs::path(paths.byLabelDir), fs::path(mount.source), reason)) {
      rejections.push_back(diskName + ": " + reason);
      continue;
    }
    if (!infoSignatureMatches(mountPoint, reason)) {
      rejections.push_back(diskName + ": " + reason);
      continue;
    }

    BootselDevice device;
    device.blockDevice = (canonicalOrSelf(fs::path(paths.devRoot)) / diskName).string();
    device.partition = canonicalOrSelf(fs::path(mount.source)).string();
    device.mountPoint = canonicalOrSelf(mountPoint).string();
    device.label = kBootselVolumeLabel;
    device.product = identity->product;
    device.serial = identity->serial;
    device.vendorId = identity->vendorId;
    device.productId = identity->productId;
    device.sizeBytes = sizeBytes;
    candidates.push_back(std::move(device));
  }

  if (candidates.size() > 1) {
    std::string names;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
      if (i != 0) names += ", ";
      names += candidates[i].blockDevice;
    }
    result.reason = "multiple RP2350 BOOTSEL drives found (" + names +
                    "); refusing because the target is ambiguous";
    return result;
  }
  if (candidates.empty()) {
    result.reason = refusalReason(rejections);
    return result;
  }
  result.found = true;
  result.device = std::move(candidates.front());
  return result;
}

BootselLookup findBootsel() {
#ifndef __linux__
  BootselLookup result;
  result.reason =
      "BOOTSEL detection needs Linux sysfs (/sys/block, /proc/mounts); this host has no sysfs scan";
  return result;
#else
  return findBootselAt(BootselPaths{});
#endif
}

bool sameBootselDevice(const BootselDevice& a, const BootselDevice& b) {
  return a.blockDevice == b.blockDevice && a.partition == b.partition &&
         a.mountPoint == b.mountPoint && a.label == b.label && a.serial == b.serial &&
         a.vendorId == b.vendorId && a.productId == b.productId && a.sizeBytes == b.sizeBytes;
}

bool verifyBootselDevice(const BootselDevice& device, const BootselPaths& paths,
                         std::string& reason) {
  const BootselLookup current = findBootselAt(paths);
  if (!current.found) {
    reason = "refusing to write: the identified BOOTSEL drive is no longer present (" +
             current.reason + ")";
    return false;
  }
  if (!sameBootselDevice(current.device, device)) {
    reason = "refusing to write: the BOOTSEL identity changed (expected " + device.blockDevice +
             " on " + device.mountPoint + ", current " + current.device.blockDevice + " on " +
             current.device.mountPoint + ")";
    return false;
  }
  return true;
}

std::string describeBootsel(const BootselDevice& device) {
  std::ostringstream text;
  text << device.blockDevice << " (" << device.partition << ") mounted at " << device.mountPoint
       << ", label " << device.label;
  char usb[16];
  std::snprintf(usb, sizeof(usb), "%04x:%04x", static_cast<unsigned>(device.vendorId),
                static_cast<unsigned>(device.productId));
  text << ", USB " << usb;
  if (!device.product.empty()) text << " '" << device.product << "'";
  text << ", " << sizeText(device.sizeBytes);
  return text.str();
}

}  // namespace mnky::update
