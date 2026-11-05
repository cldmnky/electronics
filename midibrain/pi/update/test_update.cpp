// Host tests for the Pico 2 firmware-update core. Everything here runs on a
// fake sysfs/mounts/dev tree and a switchable scripted Pico link; no test opens
// a real block device, mounts anything or writes outside its temp directory.
#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "link/link.hpp"
#include "update/bootsel.hpp"
#include "update/flash.hpp"
#include "update/uf2.hpp"
#include "update/update.hpp"

namespace {

namespace fs = std::filesystem;
using namespace mnky::update;

void pass(const char* name) { std::printf("PASS %s\n", name); }

class TempDir {
 public:
  TempDir() : path_(makePath()) {
    std::error_code ec;
    fs::create_directories(path_, ec);
    assert(!ec);
  }
  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path_, ec);
  }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;
  const fs::path& path() const { return path_; }

 private:
  static fs::path makePath() {
    static std::uint64_t serial = 0;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return fs::temp_directory_path() /
           ("mnky-update-test-" + std::to_string(stamp) + "-" + std::to_string(++serial));
  }
  fs::path path_;
};

void writeTextFile(const fs::path& path, const std::string& text) {
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(text.data(), static_cast<std::streamsize>(text.size()));
  assert(out.good());
}

void writeImageFile(const fs::path& path, const std::vector<std::uint8_t>& bytes) {
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
  assert(out.good());
}

std::vector<std::uint8_t> readFileBytes(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  assert(in.good());
  return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in),
                                   std::istreambuf_iterator<char>());
}

// --- UF2 fixture builder -----------------------------------------------------

using Block = std::array<std::uint8_t, kUf2BlockBytes>;

void setLe32(Block& block, std::size_t offset, std::uint32_t value) {
  for (int i = 0; i < 4; ++i) {
    block[offset + static_cast<std::size_t>(i)] =
        static_cast<std::uint8_t>(value >> (8 * i));
  }
}

void setVectorLe32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
  for (int i = 0; i < 4; ++i) {
    bytes[offset + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(value >> (8 * i));
  }
}

Block makeMainBlock(std::uint32_t blockNo, std::uint32_t blockCount, std::uint32_t address,
                    std::uint32_t family = kRp2350ArmSecureFamilyId,
                    std::uint32_t payloadSize = kRp2350PageBytes,
                    std::uint32_t flags = kUf2FlagFamilyIdPresent) {
  Block block{};
  setLe32(block, 0, kUf2MagicStart0);
  setLe32(block, 4, kUf2MagicStart1);
  setLe32(block, 8, flags);
  setLe32(block, 12, address);
  setLe32(block, 16, payloadSize);
  setLe32(block, 20, blockNo);
  setLe32(block, 24, blockCount);
  setLe32(block, 28, family);
  for (std::uint32_t i = 0; i < payloadSize; ++i) {
    block[kUf2PayloadOffset + i] = static_cast<std::uint8_t>((blockNo * 7 + i * 13) & 0xff);
  }
  setLe32(block, kUf2EndMagicOffset, kUf2MagicEnd);
  return block;
}

Block makeMarkerBlock(std::uint32_t address = kRp2350AbsBlockAddress) {
  Block block{};
  setLe32(block, 0, kUf2MagicStart0);
  setLe32(block, 4, kUf2MagicStart1);
  setLe32(block, 8, kUf2FlagFamilyIdPresent | kUf2FlagExtensionPresent);
  setLe32(block, 12, address);
  setLe32(block, 16, kRp2350AbsBlockPayloadBytes);
  setLe32(block, 20, 0);
  setLe32(block, 24, kRp2350AbsBlockCount);
  setLe32(block, 28, kRp2350AbsoluteFamilyId);
  for (std::uint32_t i = 0; i < kRp2350AbsBlockPayloadBytes; ++i) {
    block[kUf2PayloadOffset + i] = 0xef;
  }
  setLe32(block, kUf2PayloadOffset + kRp2350AbsBlockPayloadBytes,
          kRp2350ExtensionIgnoreBlock);
  setLe32(block, kUf2EndMagicOffset, kUf2MagicEnd);
  return block;
}

std::vector<Block> makeImageBlocks(std::uint32_t mainBlocks, bool marker,
                                   std::uint32_t family = kRp2350ArmSecureFamilyId,
                                   std::uint32_t base = kPico2FlashBase,
                                   std::uint32_t payloadSize = kRp2350PageBytes) {
  std::vector<Block> blocks;
  if (marker) blocks.push_back(makeMarkerBlock());
  for (std::uint32_t i = 0; i < mainBlocks; ++i) {
    blocks.push_back(makeMainBlock(i, mainBlocks, base + i * payloadSize, family, payloadSize));
  }
  return blocks;
}

std::vector<std::uint8_t> flatten(const std::vector<Block>& blocks) {
  std::vector<std::uint8_t> bytes;
  bytes.reserve(blocks.size() * kUf2BlockBytes);
  for (const Block& block : blocks) bytes.insert(bytes.end(), block.begin(), block.end());
  return bytes;
}

std::vector<std::uint8_t> makeImage(std::uint32_t mainBlocks, bool marker = true,
                                    std::uint32_t family = kRp2350ArmSecureFamilyId,
                                    std::uint32_t base = kPico2FlashBase,
                                    std::uint32_t payloadSize = kRp2350PageBytes) {
  return flatten(makeImageBlocks(mainBlocks, marker, family, base, payloadSize));
}

// --- Fake sysfs/mounts/dev tree ---------------------------------------------

class FakeBootselSystem {
 public:
  FakeBootselSystem() {
    fs::create_directories(sysRoot() / "block");
    fs::create_directories(devRoot() / "disk" / "by-label");
    writeTextFile(mountsFile(), "");
  }

  BootselPaths paths() const {
    return BootselPaths{sysRoot().string(), mountsFile().string(), devRoot().string(),
                        byLabelDir().string()};
  }
  fs::path scratch() const { return root_.path() / "scratch"; }
  fs::path sysRoot() const { return root_.path() / "sys"; }
  fs::path devRoot() const { return root_.path() / "dev"; }
  fs::path mountsFile() const { return root_.path() / "mounts"; }
  fs::path byLabelDir() const { return devRoot() / "disk" / "by-label"; }

  void addUsbDevice(const std::string& name, const std::string& vendor,
                    const std::string& product, const std::string& usbProduct = "RP2350 Boot",
                    const std::string& serial = "E661000000000001") {
    const fs::path base = sysRoot() / "devices" / "usb" / name;
    fs::create_directories(base / "host0" / "target0:0:0");
    writeTextFile(base / "idVendor", vendor + "\n");
    writeTextFile(base / "idProduct", product + "\n");
    writeTextFile(base / "product", usbProduct + "\n");
    writeTextFile(base / "serial", serial + "\n");
  }

  void addBlockDevice(const std::string& disk, const std::string& usb, bool removable = true,
                      std::uint64_t sectors = kBootselVolumeBytes / 512) {
    const fs::path blockDir = sysRoot() / "block" / disk;
    fs::create_directories(blockDir);
    writeTextFile(blockDir / "removable", removable ? "1\n" : "0\n");
    writeTextFile(blockDir / "size", std::to_string(sectors) + "\n");
    const fs::path target =
        sysRoot() / "devices" / "usb" / usb / "host0" / "target0:0:0" / disk / "block" / disk;
    fs::create_directories(target);
    std::error_code ec;
    fs::create_directory_symlink(target, blockDir / "device", ec);
    assert(!ec);
  }

  void addDeviceNode(const std::string& name) { writeTextFile(devRoot() / name, ""); }

  void addMount(const std::string& source, const std::string& mountPoint,
                const std::string& fsType = "vfat") {
    std::ofstream out(mountsFile(), std::ios::app);
    out << source << " " << mountPoint << " " << fsType << " rw 0 0\n";
    assert(out.good());
  }

  fs::path addMountPoint(const std::string& name) const { return root_.path() / "media" / name; }

  // Adds a mounted partition plus the RP2350 label and INFO_UF2.TXT signature.
  fs::path addBootselVolume(const std::string& disk, const std::string& label = kBootselVolumeLabel,
                            const std::string& model = "Raspberry Pi RP2350",
                            bool withInfo = true) {
    const fs::path mount = addMountPoint(disk);
    fs::create_directories(mount);
    if (withInfo) {
      writeTextFile(mount / kBootselInfoFileName,
                    "UF2 Bootloader v1.1\nModel: " + model + "\nBoard-ID: RP2350\n");
    }
    const std::string partition = disk + "1";
    addDeviceNode(partition);
    addMount((devRoot() / partition).string(), mount.string());
    std::error_code ec;
    fs::create_symlink(fs::path("../..") / partition, byLabelDir() / label, ec);
    assert(!ec);
    return mount;
  }

  static void hardlinkPartition(const FakeBootselSystem& system, const std::string& from,
                                const std::string& to) {
    std::error_code ec;
    fs::create_hard_link(system.devRoot() / from, system.devRoot() / to, ec);
    assert(!ec);
  }

 private:
  TempDir root_;
};

void setupGoodBootsel(FakeBootselSystem& system, fs::path* mount = nullptr) {
  system.addUsbDevice("usb1", "2e8a", "000f");
  system.addBlockDevice("sda", "usb1");
  const fs::path added = system.addBootselVolume("sda");
  if (mount != nullptr) *mount = added;
}

// --- UF2 validation tests ----------------------------------------------------

void testUf2GoodImages() {
  const Uf2Validation plain = validateUf2Bytes(makeImage(4, false));
  assert(plain.ok);
  assert(plain.familyId == kRp2350ArmSecureFamilyId);
  assert(plain.mainBlocks == 4);
  assert(plain.totalBlocks == 4);
  assert(!plain.markerBlock);
  assert(plain.baseAddress == kPico2FlashBase);
  assert(plain.endAddress == kPico2FlashBase + 4 * kRp2350PageBytes);
  assert(plain.payloadBytes == 4 * kRp2350PageBytes);

  // The golden layout of firmware/pico/build/midibrain.uf2: one RP2350-E10
  // marker plus 157 contiguous RP2350 ARM-S pages starting at 0x10000000.
  const Uf2Validation golden = validateUf2Bytes(makeImage(157, true));
  assert(golden.ok);
  assert(golden.markerBlock);
  assert(golden.mainBlocks == 157);
  assert(golden.totalBlocks == 158);
  assert(golden.baseAddress == 0x10000000u);
  assert(golden.endAddress == 0x10009d00u);
  assert(golden.payloadBytes == 157 * 256);
  pass("uf2: good plain and golden midibrain layouts accepted");
}

void testUf2FileValidation() {
  TempDir temp;
  const fs::path good = temp.path() / "good.uf2";
  writeImageFile(good, makeImage(3, true));
  const Uf2Validation file = validateUf2(good.string());
  assert(file.ok);
  assert(file.mainBlocks == 3);

  const Uf2Validation missing = validateUf2((temp.path() / "missing.uf2").string());
  assert(!missing.ok);
  assert(missing.reason.find("cannot open") != std::string::npos);
  pass("uf2: file path validation and missing-file refusal");
}

void testUf2FamilyRefusals() {
  const auto refused = [](const std::vector<std::uint8_t>& image, const char* needle) {
    const Uf2Validation result = validateUf2Bytes(image);
    assert(!result.ok);
    assert(result.reason.find(needle) != std::string::npos);
  };

  refused(makeImage(3, true, kRp2040FamilyId), "RP2040 family 0xe48bff56");
  refused(makeImage(3, true, kRp2350RiscvFamilyId), "0xe48bff5a");
  refused(makeImage(3, true, kRp2350ArmNonSecureFamilyId), "0xe48bff5b");
  refused(makeImage(3, true, 0x12345678u), "unsupported");
  pass("uf2: RP2040, RISC-V, ARM-NS and unknown families refused");
}

void testUf2CorruptionRefusals() {
  const auto refused = [](const std::vector<std::uint8_t>& image, const char* needle) {
    const Uf2Validation result = validateUf2Bytes(image);
    assert(!result.ok);
    assert(result.reason.find(needle) != std::string::npos);
  };

  const std::vector<std::uint8_t> good = makeImage(3, true);
  std::vector<std::uint8_t> bad = good;
  bad[0] ^= 0xff;
  refused(bad, "start magic");

  bad = good;
  bad[bad.size() - 1] ^= 0xff;
  refused(bad, "end magic");

  bad = good;
  setVectorLe32(bad, kUf2BlockBytes + 8, kUf2FlagNotMainFlash);
  refused(bad, "family-ID flag");

  bad = good;
  setVectorLe32(bad, kUf2BlockBytes + 8, kUf2FlagFamilyIdPresent | kUf2FlagMd5Present);
  refused(bad, "unsupported flags");

  bad = good;
  setVectorLe32(bad, kUf2BlockBytes + 16, 128);
  refused(bad, "payload size");

  bad = good;
  setVectorLe32(bad, kUf2BlockBytes + 24, 5);
  refused(bad, "block count changed");

  bad = good;
  setVectorLe32(bad, kUf2BlockBytes + 20, 5);
  refused(bad, "out of order");

  bad = good;
  setVectorLe32(bad, 2 * kUf2BlockBytes + 12, kPico2FlashBase + 2 * kRp2350PageBytes);
  refused(bad, "continuity");

  refused(makeImage(2, false, kRp2350ArmSecureFamilyId, kPico2FlashBase + kRp2350PageBytes),
          "XIP base");

  bad = makeImage(3, false);
  bad.pop_back();
  refused(bad, "multiple of the 512-byte");

  refused({}, "empty");
  pass("uf2: magic, flags, payload, order, continuity and truncation refused");
}

void testUf2MarkerRefusals() {
  const auto refused = [](const std::vector<std::uint8_t>& image, const char* needle) {
    const Uf2Validation result = validateUf2Bytes(image);
    assert(!result.ok);
    assert(result.reason.find(needle) != std::string::npos);
  };

  std::vector<std::uint8_t> bad = makeImage(2, true);
  bad[kUf2PayloadOffset] = 0x00;  // marker fill must be 0xef
  refused(bad, "absolute block does not match");

  bad = makeImage(2, true);
  setVectorLe32(bad, 12, 0x11000000u);  // marker address
  refused(bad, "absolute block does not match");

  std::vector<Block> blocks = makeImageBlocks(2, true);
  blocks.insert(blocks.begin() + 1, makeMarkerBlock());
  refused(flatten(blocks), "second RP2350-E10 absolute block");

  blocks = makeImageBlocks(2, false);
  blocks.push_back(makeMarkerBlock());
  refused(flatten(blocks), "must precede");

  bad = makeImage(2, false);
  setVectorLe32(bad, 28, kRp2350AbsoluteFamilyId);
  refused(bad, "absolute block does not match");
  pass("uf2: absolute marker shape, order and count refused when wrong");
}

void testUf2SizeRefusals() {
  const Uf2Validation tooMany =
      validateUf2Bytes(makeImage(static_cast<std::uint32_t>(kMaxUf2Blocks) + 1, false));
  assert(!tooMany.ok);
  assert(tooMany.reason.find("too many UF2 blocks") != std::string::npos);

  const Uf2Validation pastFlash =
      validateUf2Bytes(makeImage(static_cast<std::uint32_t>(kMaxUf2Blocks), false));
  assert(!pastFlash.ok);
  assert(pastFlash.reason.find("exceeds the 4 MiB") != std::string::npos);
  pass("uf2: oversized image and past-flash-end image refused");
}

// --- BOOTSEL identification tests -------------------------------------------

void testBootselGoodIdentification() {
  FakeBootselSystem system;
  fs::path mount;
  setupGoodBootsel(system, &mount);
  const BootselLookup lookup = findBootselAt(system.paths());
  assert(lookup.found);
  assert(lookup.reason.empty());
  assert(lookup.device.vendorId == kRaspberryPiVendorId);
  assert(lookup.device.productId == kRp2350BootProductId);
  assert(lookup.device.sizeBytes == kBootselVolumeBytes);
  assert(lookup.device.label == "RP2350");
  assert(lookup.device.product == "RP2350 Boot");
  assert(lookup.device.serial == "E661000000000001");
  assert(lookup.device.blockDevice == (fs::canonical(system.devRoot()) / "sda").string());
  assert(lookup.device.partition == (fs::canonical(system.devRoot()) / "sda1").string());
  assert(lookup.device.mountPoint == fs::canonical(mount).string());
  assert(describeBootsel(lookup.device).find("RP2350") != std::string::npos);

  std::string reason;
  assert(verifyBootselDevice(lookup.device, system.paths(), reason));
  assert(sameBootselDevice(lookup.device, lookup.device));
  pass("bootsel: positive identification and re-verification");
}

void testBootselRefusals() {
  const auto expectRefused = [](FakeBootselSystem& system, const char* needle) {
    const BootselLookup lookup = findBootselAt(system.paths());
    assert(!lookup.found);
    assert(!lookup.reason.empty());
    if (lookup.reason.find(needle) == std::string::npos) {
      std::fprintf(stderr, "expected '%s' in refusal reason: %s\n", needle,
                   lookup.reason.c_str());
    }
    assert(lookup.reason.find(needle) != std::string::npos);
  };

  {
    FakeBootselSystem system;
    system.addUsbDevice("usb1", "2e8a", "0003");  // RP2040 bootrom
    system.addBlockDevice("sda", "usb1");
    system.addBootselVolume("sda");
    expectRefused(system, "not the RP2350 bootloader");
  }
  {
    FakeBootselSystem system;
    system.addUsbDevice("usb1", "2e8a", "000f");
    system.addBlockDevice("sda", "usb1", true, (kBootselVolumeBytes / 512) * 2);
    system.addBootselVolume("sda");
    expectRefused(system, "does not match the RP2350 BOOTSEL");
  }
  {
    FakeBootselSystem system;
    system.addUsbDevice("usb1", "2e8a", "000f");
    system.addBlockDevice("sda", "usb1");
    system.addBootselVolume("sda", "RPI-RP2");
    expectRefused(system, "volume label");
  }
  {
    FakeBootselSystem system;
    system.addUsbDevice("usb1", "2e8a", "000f");
    system.addBlockDevice("sda", "usb1");
    system.addBootselVolume("sda", kBootselVolumeLabel, "Raspberry Pi RP2");
    expectRefused(system, "RP2350 model line");
  }
  {
    FakeBootselSystem system;
    system.addUsbDevice("usb1", "2e8a", "000f");
    system.addBlockDevice("sda", "usb1", false);
    system.addBootselVolume("sda");
    expectRefused(system, "no removable block devices");
  }
  {
    FakeBootselSystem system;
    system.addUsbDevice("usb1", "2e8a", "000f");
    system.addBlockDevice("sda", "usb1");
    system.addDeviceNode("sda1");
    expectRefused(system, "not mounted");
  }
  {
    FakeBootselSystem system;
    system.addUsbDevice("usb1", "2e8a", "000f");
    system.addBlockDevice("sda", "usb1");
    system.addDeviceNode("sda1");
    system.addMount((system.devRoot() / "sda1").string(), "/", "vfat");
    expectRefused(system, "root filesystem");
  }
  {
    FakeBootselSystem system;
    system.addUsbDevice("usb1", "2e8a", "000f");
    system.addBlockDevice("sda", "usb1");
    system.addBootselVolume("sda", kBootselVolumeLabel, "Raspberry Pi RP2350", false);
    expectRefused(system, "INFO_UF2.TXT");
  }
  {
    FakeBootselSystem system;
    system.addUsbDevice("usb1", "2e8a", "000f");
    system.addBlockDevice("sda", "usb1");
    system.addDeviceNode("sda1");
    const fs::path mount = system.addMountPoint("wrong");
    fs::create_directories(mount);
    writeTextFile(mount / kBootselInfoFileName,
                  "UF2 Bootloader v1.1\nModel: Raspberry Pi RP2350\n");
    system.addMount((system.devRoot() / "sda1").string(), mount.string());
    system.addDeviceNode("other");
    std::error_code ec;
    fs::create_symlink(system.devRoot() / "other", system.byLabelDir() / kBootselVolumeLabel, ec);
    assert(!ec);
    expectRefused(system, "does not point at");
  }
  {
    FakeBootselSystem system;
    BootselPaths paths = system.paths();
    paths.sysfsRoot = (system.scratch() / "missing-sys").string();
    const BootselLookup lookup = findBootselAt(paths);
    assert(!lookup.found);
    assert(lookup.reason.find("is not available") != std::string::npos);
  }
  pass("bootsel: lookalikes, wrong IDs/size/label/model/mount and missing sysfs refused");
}

void testBootselMultipleCandidatesRefused() {
  FakeBootselSystem system;
  system.addUsbDevice("usb1", "2e8a", "000f");
  system.addBlockDevice("sda", "usb1");
  system.addBlockDevice("sdb", "usb1");
  system.addBootselVolume("sda");
  // The second volume shares the by-label target inode (a hard link), so both
  // candidates pass every signature check and the scan must refuse ambiguity.
  FakeBootselSystem::hardlinkPartition(system, "sda1", "sdb1");
  const fs::path mount = system.addMountPoint("sdb");
  fs::create_directories(mount);
  writeTextFile(mount / kBootselInfoFileName,
                "UF2 Bootloader v1.1\nModel: Raspberry Pi RP2350\nBoard-ID: RP2350\n");
  system.addMount((system.devRoot() / "sdb1").string(), mount.string());

  const BootselLookup lookup = findBootselAt(system.paths());
  assert(!lookup.found);
  assert(lookup.reason.find("multiple RP2350 BOOTSEL drives") != std::string::npos);
  assert(lookup.reason.find("refusing") != std::string::npos);
  pass("bootsel: two identified drives are ambiguous and refused");
}

void testBootselIdentityChangeRefused() {
  FakeBootselSystem system;
  fs::path mount;
  setupGoodBootsel(system, &mount);
  BootselLookup lookup = findBootselAt(system.paths());
  assert(lookup.found);

  BootselDevice tampered = lookup.device;
  tampered.mountPoint = (system.scratch() / "other-mount").string();
  std::string reason;
  assert(!verifyBootselDevice(tampered, system.paths(), reason));
  assert(reason.find("identity changed") != std::string::npos);

  std::error_code ec;
  fs::remove(mount / kBootselInfoFileName, ec);
  assert(!ec);
  assert(!verifyBootselDevice(lookup.device, system.paths(), reason));
  assert(reason.find("no longer present") != std::string::npos);
  pass("bootsel: changed mount or missing signature refuses the write target");
}

// --- Flash tests ------------------------------------------------------------

struct FlashFixture {
  FakeBootselSystem system;
  BootselDevice device;
  fs::path mount;
  fs::path uf2Path;
  std::vector<std::uint8_t> image;

  FlashFixture() {
    setupGoodBootsel(system, &mount);
    const BootselLookup lookup = findBootselAt(system.paths());
    assert(lookup.found);
    device = lookup.device;
    image = makeImage(4, true);
    uf2Path = system.scratch() / "firmware.uf2";
    writeImageFile(uf2Path, image);
  }
};

void testFlashSuccess() {
  FlashFixture fixture;
  bool present = true;
  std::uint32_t lastBlocks = 0;
  std::uint32_t finalTotal = 0;
  bool monotonic = true;
  FlashHooks hooks;
  hooks.paths = fixture.system.paths();
  hooks.present = [&present](const BootselDevice&) { return present; };
  hooks.sleepMs = [](std::uint64_t) {};
  hooks.timing.pollIntervalMs = 0;
  hooks.timing.disappearTimeoutMs = 1000;

  const FlashProgressFn progress = [&](const FlashProgress& update) {
    assert(update.blocksWritten >= lastBlocks);
    monotonic = monotonic && update.blocksWritten >= lastBlocks;
    lastBlocks = update.blocksWritten;
    finalTotal = update.blocksTotal;
    if (update.blocksWritten == update.blocksTotal) present = false;
  };
  const FlashResult result =
      flashUf2(fixture.device, fixture.uf2Path.string(), progress, hooks);
  assert(result.ok);
  assert(result.deviceGone);
  assert(result.blocksWritten == 5);
  assert(result.reason.empty());
  assert(monotonic);
  assert(lastBlocks == 5);
  assert(finalTotal == 5);

  const fs::path destination = fixture.mount / kFlashFileName;
  assert(fs::canonical(fixture.mount) == fs::path(fixture.device.mountPoint));
  assert(result.writtenFile == (fs::path(fixture.device.mountPoint) / kFlashFileName).string());
  assert(readFileBytes(destination) == fixture.image);
  assert(fs::exists(fixture.mount / kBootselInfoFileName));
  pass("flash: copies every block, progress monotonic, drive disappearance required");
}

void testFlashRefusals() {
  FlashFixture fixture;
  FlashHooks hooks;
  hooks.paths = fixture.system.paths();
  hooks.present = [](const BootselDevice&) { return false; };
  hooks.sleepMs = [](std::uint64_t) {};
  hooks.timing.disappearTimeoutMs = 0;

  // Invalid image: refusal happens before the volume is touched.
  const fs::path badImage = fixture.system.scratch() / "rp2040.uf2";
  writeImageFile(badImage, makeImage(2, true, kRp2040FamilyId));
  const FlashResult bad = flashUf2(fixture.device, badImage.string(), nullptr, hooks);
  assert(!bad.ok);
  assert(bad.reason.find("refusing to flash") != std::string::npos);
  assert(!fs::exists(fixture.mount / kFlashFileName));

  // Tampered mount point: the re-verification must reject it and write nowhere.
  BootselDevice wrongMount = fixture.device;
  const fs::path other = fixture.system.scratch() / "other";
  fs::create_directories(other);
  wrongMount.mountPoint = other.string();
  const FlashResult changed =
      flashUf2(wrongMount, fixture.uf2Path.string(), nullptr, hooks);
  assert(!changed.ok);
  assert(changed.reason.find("identity changed") != std::string::npos);
  assert(!fs::exists(other / kFlashFileName));

  // Missing signature after identification: no write.
  std::error_code ec;
  fs::remove(fixture.mount / kBootselInfoFileName, ec);
  assert(!ec);
  const FlashResult gone =
      flashUf2(fixture.device, fixture.uf2Path.string(), nullptr, hooks);
  assert(!gone.ok);
  assert(gone.reason.find("no longer present") != std::string::npos);
  assert(!fs::exists(fixture.mount / kFlashFileName));
  pass("flash: invalid image, changed mount and missing signature refuse before writing");
}

void testFlashNotConfirmedRefused() {
  FlashFixture fixture;
  FlashHooks hooks;
  hooks.paths = fixture.system.paths();
  hooks.present = [](const BootselDevice&) { return true; };
  hooks.sleepMs = [](std::uint64_t) {};
  hooks.timing.disappearTimeoutMs = 0;
  const FlashResult result =
      flashUf2(fixture.device, fixture.uf2Path.string(), nullptr, hooks);
  assert(!result.ok);
  assert(!result.deviceGone);
  assert(result.reason.find("still present") != std::string::npos);
  assert(fs::exists(fixture.mount / kFlashFileName));
  pass("flash: a drive that never disappears is not reported as a completed flash");
}

void testFlashOverwritesPreviousCopy() {
  FlashFixture fixture;
  writeTextFile(fixture.mount / kFlashFileName, "previous attempt");
  bool present = true;
  FlashHooks hooks;
  hooks.paths = fixture.system.paths();
  hooks.present = [&present](const BootselDevice&) { return present; };
  hooks.sleepMs = [](std::uint64_t) {};
  const FlashProgressFn progress = [&present](const FlashProgress& update) {
    if (update.blocksWritten == update.blocksTotal) present = false;
  };
  const FlashResult result =
      flashUf2(fixture.device, fixture.uf2Path.string(), progress, hooks);
  assert(result.ok);
  assert(readFileBytes(fixture.mount / kFlashFileName) == fixture.image);
  pass("flash: a retry replaces the previous file without touching other files");
}

// --- Orchestration test -----------------------------------------------------

class SwitchablePico final : public mnky::link::ISpiTransport {
 public:
  bool present = true;
  mnky::link::EmulatorTransport pico;

  bool transfer(const std::uint8_t out[32], std::uint8_t in[32]) override {
    if (!present) return false;
    return pico.transfer(out, in);
  }
};

bool pumpUntil(mnky::link::PicoLink& link, const std::function<bool()>& predicate,
               std::uint64_t timeoutMs) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  while (std::chrono::steady_clock::now() < deadline) {
    link.poll();
    if (predicate()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return predicate();
}

void testFullUpdateOrchestration() {
  FlashFixture fixture;
  SwitchablePico transport;
  mnky::link::PicoLink link(transport);

  assert(pumpUntil(link, [&link] { return link.linkUp(); }, 2000));
  const std::uint32_t sessionBeforeBootsel = link.session();

  const Uf2Validation validation = validateUf2(fixture.uf2Path.string());
  assert(validation.ok);

  assert(rebootToBootsel(link));
  assert(pumpUntil(link, [&transport] { return transport.pico.usbBootRequested(); }, 2000));
  assert(transport.pico.rebootRequested());

  // BOOTSEL: the application link is gone while the bootrom volume is present.
  transport.present = false;
  bool bootselPresent = true;
  FlashHooks hooks;
  hooks.paths = fixture.system.paths();
  hooks.present = [&bootselPresent](const BootselDevice&) { return bootselPresent; };
  hooks.sleepMs = [](std::uint64_t) {};
  const FlashProgressFn progress = [&bootselPresent](const FlashProgress& update) {
    if (update.blocksWritten == update.blocksTotal) bootselPresent = false;
  };
  const FlashResult flash =
      flashUf2(fixture.device, fixture.uf2Path.string(), progress, hooks);
  assert(flash.ok);
  assert(flash.deviceGone);

  // The flashed firmware boots: a new Pico session answers on the link.
  transport.pico.reboot();
  transport.present = true;
  const WaitForPicoResult returned = waitForPico(link, 2000);
  assert(returned.ok);
  assert(returned.sessionChanged);
  assert(returned.session != sessionBeforeBootsel);
  assert(returned.statusSeen);
  assert(returned.status.flags & mb::STATUS_LINK_UP);
  assert(!returned.firmwareVersionExposed);
  assert(returned.firmwareVersion.empty());

  // A Pico that never comes back is a timeout, not a success with stale status.
  transport.present = false;
  const WaitForPicoResult timeout = waitForPico(link, 50);
  assert(!timeout.ok);
  assert(timeout.reason.find("timed out") != std::string::npos);
  pass("orchestration: validate -> reboot BOOTSEL -> copy -> verify -> Pico returns");
}

}  // namespace

int main() {
  testUf2GoodImages();
  testUf2FileValidation();
  testUf2FamilyRefusals();
  testUf2CorruptionRefusals();
  testUf2MarkerRefusals();
  testUf2SizeRefusals();

  testBootselGoodIdentification();
  testBootselRefusals();
  testBootselMultipleCandidatesRefused();
  testBootselIdentityChangeRefused();

  testFlashSuccess();
  testFlashRefusals();
  testFlashNotConfirmedRefused();
  testFlashOverwritesPreviousCopy();

  testFullUpdateOrchestration();

  std::printf("PASS all update tests\n");
  return 0;
}
