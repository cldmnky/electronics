// UF2 validation for the Pico 2 (RP2350) update flow. See uf2.hpp for the
// contract and the primary-source family IDs.
#include "uf2.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace mnky::update {
namespace {

std::uint32_t readLe32(const std::uint8_t* p) {
  return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
         (static_cast<std::uint32_t>(p[2]) << 16) |
         (static_cast<std::uint32_t>(p[3]) << 24);
}

std::string hex32(std::uint32_t value) {
  char buffer[11];
  std::snprintf(buffer, sizeof(buffer), "0x%08x", value);
  return buffer;
}

std::string blockError(std::size_t index, const std::string& text) {
  return "block " + std::to_string(index) + ": " + text;
}

bool allBytesAre(const std::uint8_t* data, std::size_t count, std::uint8_t value) {
  for (std::size_t i = 0; i < count; ++i) {
    if (data[i] != value) return false;
  }
  return true;
}

bool allBytesAreZero(const std::uint8_t* data, std::size_t count) {
  return allBytesAre(data, count, 0);
}

bool isRp2350Marker(const std::uint8_t* block) {
  const std::uint32_t flags = readLe32(block + 8);
  const std::uint32_t address = readLe32(block + 12);
  const std::uint32_t payload = readLe32(block + 16);
  const std::uint32_t blockNo = readLe32(block + 20);
  const std::uint32_t blockCount = readLe32(block + 24);
  const std::uint32_t family = readLe32(block + 28);
  if (family != kRp2350AbsoluteFamilyId) return false;
  if (flags != (kUf2FlagFamilyIdPresent | kUf2FlagExtensionPresent)) return false;
  if (address != kRp2350AbsBlockAddress) return false;
  if (payload != kRp2350AbsBlockPayloadBytes) return false;
  if (blockNo != 0 || blockCount != kRp2350AbsBlockCount) return false;
  const std::uint8_t* data = block + kUf2PayloadOffset;
  if (!allBytesAre(data, kRp2350AbsBlockPayloadBytes, 0xef)) return false;
  if (readLe32(data + kRp2350AbsBlockPayloadBytes) != kRp2350ExtensionIgnoreBlock)
    return false;
  // picotool zero-fills the rest of the 476-byte payload area; a payload byte
  // after the extension tag would be data the bootrom could interpret.
  return allBytesAreZero(data + kRp2350AbsBlockPayloadBytes + 4,
                         kUf2PayloadCapacity - kRp2350AbsBlockPayloadBytes - 4);
}

std::string describeFamily(std::uint32_t family) {
  switch (family) {
    case kRp2040FamilyId:
      return "RP2040 family 0xe48bff56";
    case kRp2350AbsoluteFamilyId:
      return "RP2350-E10 absolute family 0xe48bff57";
    case kRp2350ArmSecureFamilyId:
      return "RP2350 ARM-S family 0xe48bff59";
    case kRp2350RiscvFamilyId:
      return "RP2350 RISC-V family 0xe48bff5a";
    case kRp2350ArmNonSecureFamilyId:
      return "RP2350 ARM-NS family 0xe48bff5b";
    default:
      return "family " + hex32(family);
  }
}

Uf2Validation refuse(Uf2Validation result, std::string reason) {
  result.ok = false;
  result.reason = std::move(reason);
  return result;
}

}  // namespace

Uf2Validation validateUf2Bytes(std::span<const std::uint8_t> image) {
  Uf2Validation result;
  if (image.empty()) return refuse(result, "empty UF2 file");
  if (image.size() % kUf2BlockBytes != 0) {
    return refuse(result, "file size " + std::to_string(image.size()) +
                              " bytes is not a multiple of the 512-byte UF2 block");
  }
  const std::size_t totalBlocks = image.size() / kUf2BlockBytes;
  if (totalBlocks > kMaxUf2Blocks) {
    return refuse(result, "too many UF2 blocks: " + std::to_string(totalBlocks) +
                              " (maximum " + std::to_string(kMaxUf2Blocks) +
                              " for a 4 MiB Pico 2 image)");
  }

  bool haveMarker = false;
  bool haveMain = false;
  std::uint32_t mainBlockCount = 0;
  std::uint32_t declaredBlocks = 0;
  std::uint32_t expectedAddress = kPico2FlashBase;
  std::uint32_t baseAddress = 0;
  std::uint64_t payloadBytes = 0;

  for (std::size_t index = 0; index < totalBlocks; ++index) {
    const std::uint8_t* block = image.data() + index * kUf2BlockBytes;
    if (readLe32(block) != kUf2MagicStart0 || readLe32(block + 4) != kUf2MagicStart1) {
      return refuse(result, blockError(index, "bad UF2 start magic"));
    }
    if (readLe32(block + kUf2EndMagicOffset) != kUf2MagicEnd) {
      return refuse(result, blockError(index, "bad UF2 end magic"));
    }
    const std::uint32_t flags = readLe32(block + 8);
    const std::uint32_t address = readLe32(block + 12);
    const std::uint32_t payloadSize = readLe32(block + 16);
    const std::uint32_t blockNo = readLe32(block + 20);
    const std::uint32_t blockCount = readLe32(block + 24);
    const std::uint32_t family = readLe32(block + 28);

    if ((flags & kUf2FlagFamilyIdPresent) == 0) {
      return refuse(result, blockError(index, "family-ID flag is missing"));
    }

    if (family == kRp2350AbsoluteFamilyId) {
      if (haveMarker) {
        return refuse(result, blockError(index, "a second RP2350-E10 absolute block"));
      }
      if (haveMain) {
        return refuse(result,
                      blockError(index, "the absolute block must precede the flash blocks"));
      }
      if (index != 0) {
        return refuse(result, blockError(index, "the absolute block must be the first block"));
      }
      if (!isRp2350Marker(block)) {
        return refuse(result,
                      blockError(index,
                                 "absolute block does not match the RP2350-E10 marker shape "
                                 "(flags 0xa000, address 0x10ffff00, 256 x 0xef, extension tag)"));
      }
      haveMarker = true;
      continue;
    }

    if (flags != kUf2FlagFamilyIdPresent) {
      return refuse(result, blockError(index, "unsupported flags " + hex32(flags) +
                                                  " (only the family-ID flag is accepted)"));
    }
    if (family != kRp2350ArmSecureFamilyId) {
      if (family == kRp2040FamilyId) {
        return refuse(result, blockError(index, "RP2040 family 0xe48bff56 is not an RP2350 "
                                                 "image; refusing"));
      }
      if (family == kRp2350RiscvFamilyId || family == kRp2350ArmNonSecureFamilyId) {
        return refuse(result, blockError(index, describeFamily(family) +
                                                    " is not the RP2350 ARM-S image bootrom "
                                                    "entry; refusing"));
      }
      return refuse(result, blockError(index, "unsupported " + describeFamily(family) +
                                                  " (expected RP2350 ARM-S 0xe48bff59)"));
    }
    if (payloadSize != kRp2350PageBytes) {
      return refuse(result, blockError(index, "payload size " + std::to_string(payloadSize) +
                                                  " is not the RP2350 page size 256"));
    }
    if (blockCount == 0) {
      return refuse(result, blockError(index, "block count is zero"));
    }
    const std::size_t mainIndex = index - (haveMarker ? 1u : 0u);
    if (!haveMain) {
      haveMain = true;
      declaredBlocks = blockCount;
      baseAddress = address;
      if (address != kPico2FlashBase) {
        return refuse(result, blockError(index, "first flash block address " + hex32(address) +
                                                    " is not the Pico 2 XIP base 0x10000000"));
      }
      if (mainIndex != 0) {
        return refuse(result, blockError(index, "first flash block number is not zero"));
      }
    } else if (blockCount != declaredBlocks) {
      return refuse(result, blockError(index, "block count changed from " +
                                                  std::to_string(declaredBlocks) + " to " +
                                                  std::to_string(blockCount)));
    }
    if (blockNo != mainIndex) {
      return refuse(result, blockError(index, "flash block number " + std::to_string(blockNo) +
                                                  " is out of order (expected " +
                                                  std::to_string(mainIndex) + ")"));
    }
    if (address != expectedAddress) {
      return refuse(result, blockError(index, "flash address " + hex32(address) +
                                                  " breaks continuity (expected " +
                                                  hex32(expectedAddress) + ")"));
    }
    expectedAddress += payloadSize;
    ++mainBlockCount;
    payloadBytes += payloadSize;
  }

  if (!haveMain) return refuse(result, "no RP2350 main-flash blocks");
  if (mainBlockCount != declaredBlocks) {
    return refuse(result, "flash block count mismatch: found " +
                              std::to_string(mainBlockCount) + " of " +
                              std::to_string(declaredBlocks));
  }
  if (expectedAddress > kPico2FlashBase + kPico2FlashBytes) {
    return refuse(result, "image end " + hex32(expectedAddress) +
                              " exceeds the 4 MiB Pico 2 flash window");
  }
  if (payloadBytes == 0) return refuse(result, "image contains no flash payload");

  result.ok = true;
  result.familyId = kRp2350ArmSecureFamilyId;
  result.mainBlocks = mainBlockCount;
  result.totalBlocks = static_cast<std::uint32_t>(totalBlocks);
  result.baseAddress = baseAddress;
  result.endAddress = expectedAddress;
  result.payloadBytes = payloadBytes;
  result.markerBlock = haveMarker;
  return result;
}

Uf2Validation validateUf2(const std::string& path) {
  Uf2Validation result;
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) {
    const int saved = errno;
    return refuse(result, "cannot open UF2 '" + path + "': " + std::strerror(saved));
  }
  const std::streamoff size = file.tellg();
  if (size < 0) return refuse(result, "cannot determine the size of UF2 '" + path + "'");
  const std::uint64_t maxBytes =
      static_cast<std::uint64_t>(kMaxUf2Blocks) * kUf2BlockBytes;
  if (static_cast<std::uint64_t>(size) > maxBytes) {
    return refuse(result, "UF2 '" + path + "' is too large (" + std::to_string(size) +
                              " bytes; maximum " + std::to_string(maxBytes) + ")");
  }
  std::vector<std::uint8_t> image(static_cast<std::size_t>(size));
  file.seekg(0, std::ios::beg);
  if (!image.empty() &&
      !file.read(reinterpret_cast<char*>(image.data()), static_cast<std::streamsize>(image.size()))) {
    return refuse(result, "cannot read UF2 '" + path + "'");
  }
  return validateUf2Bytes(image);
}

}  // namespace mnky::update
