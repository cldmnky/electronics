#include "hw/sync.hpp"

#include "contracts.hpp"
#include "pins.hpp"

namespace froemix
{
void SyncIo::Init()
{
    in_.Init(pins::kSyncIn, daisy::GPIO::Mode::INPUT, daisy::GPIO::Pull::NOPULL);
    out_.Init(pins::kSyncOut, daisy::GPIO::Mode::OUTPUT);
    out_.Write(0);
    last_pin_state_ = in_.Read();
}

bool SyncIo::Read()
{
    // Q1 inverts: jack high -> collector low.
    const bool pin_high = in_.Read();
    return contracts::kSyncInInverted ? !pin_high : pin_high;
}

void SyncIo::Write(bool high)
{
    out_.Write(high);
}
} // namespace froemix
