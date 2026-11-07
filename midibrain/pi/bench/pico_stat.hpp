#pragma once
// Parser for the Pico firmware's 1 Hz `[stat]` console line.
//
// The counters are printed on the Pico USB CDC console, not on the SPI link:
// the link carries frames and STATUS_RESP, and this project adds no protocol
// frame just to mirror a debug line. The bench driver reads the console and
// uses this parser; the app's DIAGNOSTICS page shows the link-side counters it
// already receives plus the latest line when bench mode captured one.
//
// Line shape (firmware/pico/main.cpp, keep in sync):
//   [stat] link=0 bpm=120.00 run=0 ticks=0 txq=0 rxpend=0/0 bad=0 dup=0
//   txov=0 rxov=0/0 mtx=0+0/0+0 fxq=0+0 fxdrop=0 mtxdrop=0 mtxrej=0 mtxrt=0
//   miss=0 spiabort=0 spibad=0 spigarb=0 spiok=0 spiak=0 spiov=0
#include <cstdint>
#include <map>
#include <string>
#include <string_view>

namespace mnky::bench {

struct PicoStat {
  bool valid = false;
  std::uint64_t link = 0;
  std::uint64_t run = 0;
  std::uint64_t ticks = 0;
  std::uint64_t txq = 0;
  std::uint64_t bad = 0;
  std::uint64_t dup = 0;
  std::uint64_t txov = 0;
  std::uint64_t miss = 0;
  std::uint64_t spiabort = 0;
  std::uint64_t spibad = 0;
  std::uint64_t spigarb = 0;
  std::uint64_t spiok = 0;
  std::uint64_t spiak = 0;
  std::uint64_t spiov = 0;
  std::uint64_t fxdrop = 0;
  std::uint64_t mtxdrop = 0;
  std::uint64_t mtxrej = 0;
  std::uint64_t mtxrt = 0;
  std::uint64_t mtx_normal = 0;
  std::uint64_t mtx_realtime = 0;
  std::uint64_t fxq = 0;
  std::uint64_t rxpend1 = 0;
  std::uint64_t rxpend2 = 0;
  std::uint64_t rxov1 = 0;
  std::uint64_t rxov2 = 0;
  // bpm is printed with two decimals; keep the hundredths as an integer so a
  // 20.00 vs 120.00 comparison never goes through a float.
  std::uint64_t bpm_x100 = 0;
  // Every key=value token, including fields added by a newer firmware. The
  // bench delta printer uses this map, so a new counter is never silently
  // dropped from the before/after table.
  std::map<std::string, std::string> fields;
};

// Accepts the composite tokens the firmware prints:
//   mtx=1+2/3+4  -> mtx_normal = 1+2 (both outputs), mtx_realtime = 3+4
//   fxq=5+6      -> fxq = 5+6
//   rxpend=1/0   -> rxpend1 = 1, rxpend2 = 0
//   rxov=7/8     -> rxov1 = 7, rxov2 = 8
// A malformed numeric token drops the whole line (returns false, out.valid is
// left false) instead of reporting a partial snapshot as healthy.
bool parsePicoStatLine(std::string_view line, PicoStat& out);

// One-line human diff of the counters that matter for a clock acceptance row:
// the SPI reliability counters and every drop counter. Fields absent from
// either side are skipped.
std::string formatPicoStatDelta(const PicoStat& before, const PicoStat& after);

}  // namespace mnky::bench
