#include "hw/display.hpp"

#include "pins.hpp"
#include "util/oled_fonts.h"

namespace froemix
{
bool Display::Init()
{
    Device::Config cfg;

    // SPI1, TX only. libDaisy's default OLED transport already matches this
    // board for SCLK/MOSI/CS and for DC (PB4 == seed::D9); only the reset pin
    // and the "no MISO" choice are board specific.
    auto& spi             = cfg.driver_config.transport_config.spi_config;
    spi.periph            = daisy::SpiHandle::Config::Peripheral::SPI_1;
    spi.pin_config.sclk   = pins::kOledSclk;
    spi.pin_config.mosi   = pins::kOledMosi;
    spi.pin_config.nss    = pins::kOledCs;
    spi.pin_config.miso   = daisy::Pin(daisy::PORTX, 0); // unused on this board
    cfg.driver_config.transport_config.pin_config.dc    = pins::kOledDc;
    cfg.driver_config.transport_config.pin_config.reset = pins::kOledReset;

    device_.Init(cfg);
    device_.Fill(false);
    device_.Update();
    ready_ = true;
    return true;
}

void Display::Clear()
{
    if(!ready_)
        return;
    device_.Fill(false);
}

void Display::Text(int x, int y, const char* text)
{
    if(!ready_ || text == nullptr)
        return;
    device_.SetCursor(x, y);
    device_.WriteString(text, Font_6x8, true);
}

void Display::Update()
{
    if(!ready_)
        return;
    device_.Update();
}
} // namespace froemix
