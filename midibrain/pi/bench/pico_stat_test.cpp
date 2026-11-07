// Host test for the Pico `[stat]` parser used by --bench-clock.
#include "pico_stat.hpp"

#include <cassert>
#include <cstdio>
#include <string>

using mnky::bench::formatPicoStatDelta;
using mnky::bench::parsePicoStatLine;
using mnky::bench::PicoStat;

int main() {
  const std::string line =
      "[stat] link=1 bpm=120.00 run=0 ticks=345601 txq=0 rxpend=1/0 bad=2 dup=3 "
      "txov=0 rxov=4/5 mtx=6+7/8+9 fxq=10+11 fxdrop=12 mtxdrop=13 mtxrej=14 "
      "mtxrt=15 miss=16 spiabort=17 spibad=18 spigarb=19 spiok=20 spiak=21 spiov=22";
  PicoStat stat;
  assert(parsePicoStatLine(line, stat));
  assert(stat.valid);
  assert(stat.link == 1);
  assert(stat.bpm_x100 == 12000);
  assert(stat.ticks == 345601);
  assert(stat.rxpend1 == 1 && stat.rxpend2 == 0);
  assert(stat.rxov1 == 4 && stat.rxov2 == 5);
  assert(stat.mtx_normal == 13 && stat.mtx_realtime == 17);
  assert(stat.fxq == 21);
  assert(stat.spiabort == 17 && stat.spiov == 22);

  PicoStat second;
  assert(!parsePicoStatLine("[link] Pi HELLO session=deadbeef", second));
  assert(!parsePicoStatLine("[stat] spiok=x bpm=120.00", second));
  assert(!parsePicoStatLine("[stat] link=1", second));

  PicoStat after = stat;
  after.spiok += 100;
  after.spiabort += 2;
  const std::string delta = formatPicoStatDelta(stat, after);
  assert(delta.find("spiok=100") != std::string::npos);
  assert(delta.find("spiabort=2") != std::string::npos);
  assert(delta.find("miss=0") != std::string::npos);
  std::printf("pico_stat: parser + delta OK (%s)\n", delta.c_str());
  return 0;
}
