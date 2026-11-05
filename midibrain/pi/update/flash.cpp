// Copy-to-BOOTSEL implementation. See flash.hpp for the safety contract.
#include "flash.hpp"

#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <thread>

#include <fcntl.h>
#include <unistd.h>

#include "uf2.hpp"

namespace mnky::update {
namespace {

namespace fs = std::filesystem;

FlashResult failure(std::string reason) {
  FlashResult result;
  result.ok = false;
  result.reason = std::move(reason);
  return result;
}

bool writeAll(int fd, const std::uint8_t* data, std::size_t size, std::string& reason) {
  std::size_t written = 0;
  while (written < size) {
    const ssize_t chunk = ::write(fd, data + written, size - written);
    if (chunk < 0) {
      if (errno == EINTR) continue;
      reason = "write failed: " + std::string(std::strerror(errno));
      return false;
    }
    if (chunk == 0) {
      reason = "write returned zero bytes";
      return false;
    }
    written += static_cast<std::size_t>(chunk);
  }
  return true;
}

bool defaultPresent(const BootselDevice& device, const BootselPaths& paths) {
  const BootselLookup current = findBootselAt(paths);
  return current.found && sameBootselDevice(current.device, device);
}

}  // namespace

FlashResult flashUf2(const BootselDevice& device, const std::string& uf2Path,
                     const FlashProgressFn& progress) {
  return flashUf2(device, uf2Path, progress, FlashHooks{});
}

FlashResult flashUf2(const BootselDevice& device, const std::string& uf2Path,
                     const FlashProgressFn& progress, const FlashHooks& hooks) {
  // 1. The image must be a strict RP2350 ARM-S UF2 before anything is touched.
  const Uf2Validation validation = validateUf2(uf2Path);
  if (!validation.ok) return failure("refusing to flash: " + validation.reason);

  // 2. Re-run the full positive identification immediately before writing. The
  //    device object passed in is evidence of a previous scan, never authority.
  std::string reason;
  if (!verifyBootselDevice(device, hooks.paths, reason)) return failure(reason);

  const fs::path mountPoint(device.mountPoint);
  std::error_code ec;
  if (!mountPoint.is_absolute() || mountPoint == mountPoint.root_path()) {
    return failure("refusing to write: mount point '" + mountPoint.string() +
                   "' is not a usable BOOTSEL volume");
  }
  if (!fs::is_directory(mountPoint, ec)) {
    return failure("refusing to write: mount point '" + mountPoint.string() +
                   "' is not a directory");
  }
  const fs::path destination = mountPoint / kFlashFileName;
  if (destination.parent_path() != mountPoint) {
    return failure("refusing to write outside the verified mount point");
  }

  // 3. Capacity check on the mounted volume (the bootrom volume is 128 MiB;
  //    the image is a few MiB). A failure here means the mount is not the
  //    volume the identification promised.
  std::error_code sizeError;
  const std::uintmax_t sourceBytes = fs::file_size(uf2Path, sizeError);
  if (sizeError) {
    return failure("cannot read the size of '" + uf2Path + "': " + sizeError.message());
  }
  const fs::space_info space = fs::space(mountPoint, ec);
  if (ec) {
    return failure("cannot read free space on '" + mountPoint.string() + "': " + ec.message());
  }
  if (space.available < sourceBytes) {
    return failure("refusing to write: the BOOTSEL volume has " +
                   std::to_string(space.available) + " bytes free but the image needs " +
                   std::to_string(sourceBytes));
  }

  // 4. Copy the image block by block into the verified mount. O_EXCL prevents a
  //    pre-existing symlink/file from redirecting the write.
  fs::remove(destination, ec);
  if (ec) {
    return failure("cannot clear the previous '" + destination.string() + "': " + ec.message());
  }
  const int fd = ::open(destination.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
                        0644);
  if (fd < 0) {
    return failure("cannot create '" + destination.string() + "': " +
                   std::string(std::strerror(errno)));
  }

  std::ifstream source(uf2Path, std::ios::binary);
  if (!source) {
    const int saved = errno;
    ::close(fd);
    return failure("cannot reopen '" + uf2Path + "' for the copy: " + std::string(std::strerror(saved)));
  }

  FlashResult result;
  result.writtenFile = destination.string();
  std::array<std::uint8_t, kUf2BlockBytes> block{};
  bool writeFailed = false;
  while (!writeFailed) {
    source.read(reinterpret_cast<char*>(block.data()),
                static_cast<std::streamsize>(block.size()));
    const std::streamsize got = source.gcount();
    if (got == 0) break;
    if (got != static_cast<std::streamsize>(block.size())) {
      reason = "source file ended inside a UF2 block after validation";
      writeFailed = true;
      break;
    }
    if (!writeAll(fd, block.data(), block.size(), reason)) {
      writeFailed = true;
      break;
    }
    ++result.blocksWritten;
    result.bytesWritten += block.size();
    if (progress) {
      FlashProgress update;
      update.blocksWritten = result.blocksWritten;
      update.blocksTotal = validation.totalBlocks;
      update.bytesWritten = result.bytesWritten;
      update.bytesTotal = static_cast<std::uint64_t>(validation.totalBlocks) * kUf2BlockBytes;
      progress(update);
    }
  }
  if (writeFailed) {
    ::close(fd);
    return failure(reason);
  }
  if (result.blocksWritten != validation.totalBlocks) {
    ::close(fd);
    return failure("copied " + std::to_string(result.blocksWritten) + " of " +
                   std::to_string(validation.totalBlocks) + " UF2 blocks");
  }
  // The bootrom can reboot as soon as the last UF2 block arrives, so fsync or
  // close may fail with ENODEV/EIO on a flash that actually succeeded. Keep the
  // error for diagnostics and let the disappearance poll decide.
  std::string postCopyError;
  if (::fsync(fd) != 0) {
    postCopyError = "fsync failed: " + std::string(std::strerror(errno));
  }
  if (::close(fd) != 0 && postCopyError.empty()) {
    postCopyError = "close failed: " + std::string(std::strerror(errno));
  }

  // 5. The bootrom self-flashes and reboots; the volume disappearing is the
  //    only completion signal available without the link. Never report success
  //    while the drive is still there.
  const auto present = hooks.present ? hooks.present
                                     : std::function<bool(const BootselDevice&)>(
                                           [&hooks](const BootselDevice& target) {
                                             return defaultPresent(target, hooks.paths);
                                           });
  const auto sleep = hooks.sleepMs ? hooks.sleepMs : [](std::uint64_t ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
  };
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(hooks.timing.disappearTimeoutMs);
  bool gone = false;
  do {
    gone = !present(device);
    if (gone) break;
    if (std::chrono::steady_clock::now() >= deadline) break;
    sleep(hooks.timing.pollIntervalMs);
  } while (std::chrono::steady_clock::now() < deadline);

  result.deviceGone = gone;
  if (!gone) {
    result.ok = false;
    result.reason = "copied " + std::to_string(result.blocksWritten) +
                    " UF2 blocks but the BOOTSEL drive is still present after " +
                    std::to_string(hooks.timing.disappearTimeoutMs) +
                    " ms; the flash is not confirmed";
    if (!postCopyError.empty()) result.reason += " (" + postCopyError + ")";
    return result;
  }
  result.ok = true;
  return result;
}

}  // namespace mnky::update
