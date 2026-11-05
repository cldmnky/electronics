#pragma once
// UF2 image validation for the Pico 2 (RP2350) firmware-update flow.
//
// The wire/image format and the family IDs are not redefined here: the values
// below are the RP2350 bootrom contract, cross-checked against
// pico-sdk 2.3.1 `src/common/boot_uf2_headers/include/boot/uf2.h` and the
// A4 bootrom source (github.com/raspberrypi/pico-bootrom-rp2350). The validator
// is deliberately strict: it accepts exactly what picotool emits for this
// project's `--family rp2350-arm-s --platform rp2350 --abs-block` build and
// refuses everything else, including RP2040 (0xe48bff56) and the RISC-V /
// ARM-non-secure RP2350 families.
#include <cstdint>
#include <span>
#include <string>

namespace mnky::update {

// UF2 block layout (512 bytes; little-endian header words). uf2.h.
inline constexpr std::uint32_t kUf2MagicStart0 = 0x0a324655u;
inline constexpr std::uint32_t kUf2MagicStart1 = 0x9e5d5157u;
inline constexpr std::uint32_t kUf2MagicEnd = 0x0ab16f30u;
inline constexpr std::uint32_t kUf2FlagNotMainFlash = 0x00000001u;
inline constexpr std::uint32_t kUf2FlagFamilyIdPresent = 0x00002000u;
inline constexpr std::uint32_t kUf2FlagMd5Present = 0x00004000u;
inline constexpr std::uint32_t kUf2FlagExtensionPresent = 0x00008000u;
inline constexpr std::uint32_t kUf2BlockBytes = 512;
inline constexpr std::uint32_t kUf2PayloadCapacity = 476;
inline constexpr std::uint32_t kUf2PayloadOffset = 32;
inline constexpr std::uint32_t kUf2EndMagicOffset = 508;

// RP2350 family IDs. 0xe48bff56 is RP2040 and must never be accepted here.
inline constexpr std::uint32_t kRp2040FamilyId = 0xe48bff56u;
inline constexpr std::uint32_t kRp2350AbsoluteFamilyId = 0xe48bff57u;
inline constexpr std::uint32_t kRp2350ArmSecureFamilyId = 0xe48bff59u;
inline constexpr std::uint32_t kRp2350RiscvFamilyId = 0xe48bff5au;
inline constexpr std::uint32_t kRp2350ArmNonSecureFamilyId = 0xe48bff5bu;

// RP2350-E10 absolute marker block emitted by picotool for `--abs-block`
// (elf2uf2.cpp `gen_abs_block`). It carries no flash payload; the bootrom uses
// its address as the E10 workaround marker.
inline constexpr std::uint32_t kRp2350AbsBlockAddress = 0x10ffff00u;
inline constexpr std::uint32_t kRp2350AbsBlockPayloadBytes = 256;
inline constexpr std::uint32_t kRp2350AbsBlockCount = 2;
inline constexpr std::uint32_t kRp2350ExtensionIgnoreBlock = 0x9957e304u;

// Pico 2 flash window and page size. picotool emits 256-byte UF2 payloads and
// the image starts at the XIP base; the Pico 2 has 4 MiB of flash.
inline constexpr std::uint32_t kRp2350PageBytes = 256;
inline constexpr std::uint32_t kPico2FlashBase = 0x10000000u;
inline constexpr std::uint64_t kPico2FlashBytes = 4ull * 1024ull * 1024ull;
inline constexpr std::uint32_t kMaxUf2Blocks =
    static_cast<std::uint32_t>(kPico2FlashBytes / kRp2350PageBytes) + 1;

// Result of validating one image. `reason` is empty only when `ok` is true.
struct Uf2Validation {
  bool ok{};
  std::string reason{};
  std::uint32_t familyId{};
  std::uint32_t mainBlocks{};
  std::uint32_t totalBlocks{};
  std::uint32_t baseAddress{};
  std::uint32_t endAddress{};
  std::uint64_t payloadBytes{};
  bool markerBlock{};
};

// Reads `path` and validates it. A missing/unreadable file is a refusal, never
// an exception.
Uf2Validation validateUf2(const std::string& path);

// Same validation over an in-memory image; used by the tests and by any caller
// that already holds the bytes.
Uf2Validation validateUf2Bytes(std::span<const std::uint8_t> image);

}  // namespace mnky::update
