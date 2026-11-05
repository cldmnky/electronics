// pico-update: manual CLI for the Pico 2 firmware-update flow.
//
//   pico-update validate <image.uf2>
//   pico-update find
//   pico-update flash <image.uf2> --yes
//   pico-update update <image.uf2> --yes [--spidev /dev/spidev0.0]
//
// The copy step is destructive-capable, so `flash` and `update` refuse without
// --yes. `update` is the full flow: reboot the running Pico into BOOTSEL, wait
// for the verified volume, copy, then wait for the firmware to return.
#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "../link/link.hpp"
#include "bootsel.hpp"
#include "flash.hpp"
#include "uf2.hpp"
#include "update.hpp"

namespace {

using namespace mnky::update;

constexpr int kExitOk = 0;
constexpr int kExitError = 1;
constexpr int kExitRefused = 2;

constexpr std::uint64_t kLinkUpTimeoutMs = 5000;
constexpr std::uint64_t kBootselTimeoutMs = 15000;
constexpr std::uint64_t kPicoReturnTimeoutMs = 30000;

void usage(std::FILE* out) {
  std::fprintf(out,
               "pico-update - RP2350 BOOTSEL firmware update\n"
               "\n"
               "usage:\n"
               "  pico-update validate <image.uf2>\n"
               "  pico-update find\n"
               "  pico-update flash <image.uf2> --yes\n"
               "  pico-update update <image.uf2> --yes [--spidev DEVICE]\n"
               "\n"
               "  validate  check the UF2 family, block chain and size; writes nothing\n"
               "  find      report the positively identified BOOTSEL drive or the refusal\n"
               "  flash     copy an already-running Pico's image to the mounted volume\n"
               "  update    reboot a running Pico into BOOTSEL, copy, wait for it to return\n"
               "\n"
               "The BOOTSEL volume must already be mounted (udisks/systemd does this\n"
               "when the drive appears). --spidev defaults to /dev/spidev0.0.\n");
}

struct Options {
  std::string command;
  std::string file;
  std::string spidev{"/dev/spidev0.0"};
  bool yes = false;
  bool help = false;
};

bool parseOptions(int argc, char** argv, Options& options) {
  std::vector<std::string> positional;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      options.help = true;
    } else if (arg == "--yes") {
      options.yes = true;
    } else if (arg == "--spidev") {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "pico-update: --spidev needs a device path\n");
        return false;
      }
      options.spidev = argv[++i];
    } else if (!arg.empty() && arg[0] == '-') {
      std::fprintf(stderr, "pico-update: unknown option '%s'\n", arg.c_str());
      return false;
    } else {
      positional.push_back(arg);
    }
  }
  if (options.help) return true;
  if (positional.empty()) {
    std::fprintf(stderr, "pico-update: missing command\n");
    return false;
  }
  options.command = positional.front();
  if (positional.size() >= 2) options.file = positional[1];
  if (positional.size() > 2) {
    std::fprintf(stderr, "pico-update: too many arguments\n");
    return false;
  }
  if (options.command != "validate" && options.command != "find" &&
      options.command != "flash" && options.command != "update") {
    std::fprintf(stderr, "pico-update: unknown command '%s'\n", options.command.c_str());
    return false;
  }
  if (options.command != "find" && options.file.empty()) {
    std::fprintf(stderr, "pico-update: %s needs an image path\n", options.command.c_str());
    return false;
  }
  return true;
}

void printValidation(const Uf2Validation& validation) {
  if (!validation.ok) {
    std::fprintf(stderr, "REFUSED: %s\n", validation.reason.c_str());
    return;
  }
  std::printf("OK: RP2350 ARM-S (0x%08x), %u flash blocks + %s, %llu payload bytes, "
              "0x%08x..0x%08x\n",
              validation.familyId, validation.mainBlocks,
              validation.markerBlock ? "1 E10 marker block" : "no marker block",
              static_cast<unsigned long long>(validation.payloadBytes), validation.baseAddress,
              validation.endAddress);
}

int requireYes(const Options& options) {
  if (options.yes) return kExitOk;
  std::fprintf(stderr,
               "REFUSED: the copy step writes to the BOOTSEL drive. Re-run with --yes to "
               "confirm.\n");
  return kExitRefused;
}

bool waitForLinkUp(mnky::link::PicoLink& link, std::uint64_t timeoutMs) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  while (std::chrono::steady_clock::now() < deadline) {
    link.poll();
    if (link.linkUp()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return link.linkUp();
}

BootselLookup waitForBootsel(mnky::link::PicoLink* link, std::uint64_t timeoutMs) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  BootselLookup lookup = findBootsel();
  while (!lookup.found && std::chrono::steady_clock::now() < deadline) {
    // Keep polling the link: the EV_REBOOT frame queued by rebootToBootsel()
    // is only transmitted by PicoLink::poll().
    if (link != nullptr) link->poll();
    lookup = findBootsel();
    if (!lookup.found) std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return lookup;
}

void printProgress(const FlashProgress& progress) {
  if (progress.blocksWritten == progress.blocksTotal || progress.blocksWritten % 16 == 0) {
    std::fprintf(stderr, "\rflash: %u/%u blocks (%llu/%llu bytes)", progress.blocksWritten,
                 progress.blocksTotal, static_cast<unsigned long long>(progress.bytesWritten),
                 static_cast<unsigned long long>(progress.bytesTotal));
    if (progress.blocksWritten == progress.blocksTotal) std::fprintf(stderr, "\n");
    std::fflush(stderr);
  }
}

int commandValidate(const Options& options) {
  const Uf2Validation validation = validateUf2(options.file);
  printValidation(validation);
  return validation.ok ? kExitOk : kExitRefused;
}

int commandFind() {
  const BootselLookup lookup = findBootsel();
  if (!lookup.found) {
    std::printf("NOT FOUND: %s\n", lookup.reason.c_str());
    return kExitRefused;
  }
  std::printf("FOUND: %s\n", describeBootsel(lookup.device).c_str());
  return kExitOk;
}

int commandFlash(const Options& options) {
  const Uf2Validation validation = validateUf2(options.file);
  if (!validation.ok) {
    printValidation(validation);
    return kExitRefused;
  }
  const int confirmed = requireYes(options);
  if (confirmed != kExitOk) return confirmed;

  const BootselLookup lookup = findBootsel();
  if (!lookup.found) {
    std::fprintf(stderr, "REFUSED: %s\n", lookup.reason.c_str());
    return kExitRefused;
  }
  std::printf("target: %s\n", describeBootsel(lookup.device).c_str());
  const FlashResult result = flashUf2(lookup.device, options.file, printProgress);
  if (!result.ok) {
    std::fprintf(stderr, "FAILED: %s\n", result.reason.c_str());
    return kExitError;
  }
  std::printf("copied '%.*s' (%u blocks, %llu bytes); BOOTSEL drive disappeared\n",
              static_cast<int>(result.writtenFile.size()), result.writtenFile.c_str(),
              result.blocksWritten, static_cast<unsigned long long>(result.bytesWritten));
  return kExitOk;
}

int commandUpdate(const Options& options) {
  const Uf2Validation validation = validateUf2(options.file);
  if (!validation.ok) {
    printValidation(validation);
    return kExitRefused;
  }
  const int confirmed = requireYes(options);
  if (confirmed != kExitOk) return confirmed;

  std::unique_ptr<mnky::link::LinuxSpidevTransport> transport;
  std::unique_ptr<mnky::link::PicoLink> link;
  try {
    transport = std::make_unique<mnky::link::LinuxSpidevTransport>(options.spidev.c_str());
    link = std::make_unique<mnky::link::PicoLink>(*transport);
  } catch (const std::exception& error) {
    std::fprintf(stderr, "note: SPI link unavailable (%s); continuing with an already-present "
                         "BOOTSEL drive\n",
                 error.what());
  }

  BootselLookup lookup = findBootsel();
  bool linkWasUp = false;
  if (!lookup.found && link) {
    if (waitForLinkUp(*link, kLinkUpTimeoutMs)) {
      linkWasUp = true;
      std::printf("rebooting the Pico into BOOTSEL (EV_REBOOT usbBoot=1)\n");
      if (!rebootToBootsel(*link)) {
        std::fprintf(stderr, "warning: could not queue EV_REBOOT\n");
      }
    } else {
      std::fprintf(stderr, "note: no Pico answered on the SPI link\n");
    }
    lookup = waitForBootsel(link.get(), kBootselTimeoutMs);
  }
  if (!lookup.found) {
    std::fprintf(stderr, "REFUSED: %s\n", lookup.reason.c_str());
    return kExitRefused;
  }

  std::printf("target: %s\n", describeBootsel(lookup.device).c_str());
  const FlashResult result = flashUf2(lookup.device, options.file, printProgress);
  if (!result.ok) {
    std::fprintf(stderr, "FAILED: %s\n", result.reason.c_str());
    return kExitError;
  }
  std::printf("copied '%.*s' (%u blocks, %llu bytes); BOOTSEL drive disappeared\n",
              static_cast<int>(result.writtenFile.size()), result.writtenFile.c_str(),
              result.blocksWritten, static_cast<unsigned long long>(result.bytesWritten));

  if (!link || !linkWasUp) return kExitOk;
  const WaitForPicoResult returned = waitForPico(*link, kPicoReturnTimeoutMs);
  if (!returned.ok) {
    std::fprintf(stderr, "FAILED: %s\n", returned.reason.c_str());
    return kExitError;
  }
  std::printf("Pico returned on the link (session 0x%08x, uptime %u s)\n", returned.session,
              returned.status.uptime_s);
  if (!returned.firmwareVersionExposed) {
    std::printf("firmware version: not exposed by the link protocol\n");
  }
  return kExitOk;
}

}  // namespace

int main(int argc, char** argv) {
  Options options;
  if (!parseOptions(argc, argv, options)) {
    usage(stderr);
    return kExitError;
  }
  if (options.help) {
    usage(stdout);
    return kExitOk;
  }
  if (options.command == "validate") return commandValidate(options);
  if (options.command == "find") return commandFind();
  if (options.command == "flash") return commandFlash(options);
  return commandUpdate(options);
}
