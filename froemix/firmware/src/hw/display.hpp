#pragma once
// 1.3" 128x64 SPI OLED on CN4 (front board).
//
// Wiring, from the board: CS = D7, SCK = D8, DC = D9, MOSI = D10, RES = D15.
// D9 is the Seed's SPI1 MISO pad, used here as a plain GPIO, so the SPI is
// configured TX-only and its MISO pin is left unconnected (libDaisy's default
// OLED transport already does exactly this).
//
// The panel is the only human-readable output besides the LED, so keep the text
// ASCII (repo convention: no font-dependent glyphs).

#include <cstdint>

#include "hid/disp/oled_display.h"
#include "dev/oled_ssd130x.h"

namespace froemix
{
class Display
{
  public:
    using Device = daisy::OledDisplay<daisy::SSD130x4WireSpi128x64Driver>;

    bool Init();

    void Clear();
    void Text(int x, int y, const char* text);
    void Update();

    bool Ready() const { return ready_; }

  private:
    Device device_;
    bool   ready_ = false;
};
} // namespace froemix
