#pragma once
// Board bring-up: everything the base board + eurorack-pmod need before an
// audio engine can run. This file is the firmware's contract surface - it
// configures exactly the nets in pins.hpp and nothing else.
//
// Init order matters and is documented in doc/FIRMWARE.md:
//   1. Seed init (clocks, SDRAM, its own WM8731 codec)
//   2. release the Seed's SAI1 so Audio6 can own both SAIs
//   3. I2C1 (eurorack-pmod: AK4619 + PC9635)
//   4. ADC (CD4051 mux + 2 CV inputs), DAC (buffer DISABLED)
//   5. GPIO (sync, panel LED), MIDI, display
//   6. eurorack-pmod codec/LED init, then Audio6 (SAI1 + SAI2 TDM)

#include <cstddef>

#include "daisy_seed.h"

#include "audio/audio6.hpp"
#include "hw/controls.hpp"
#include "hw/cv.hpp"
#include "hw/display.hpp"
#include "hw/midi_io.hpp"
#include "hw/pmod.hpp"
#include "hw/sync.hpp"
#include "selftest.hpp"

namespace froemix
{
class Board
{
  public:
    struct Status
    {
        bool pmod_codec; // AK4619 answered and was configured
        bool pmod_leds;  // PC9635 accepted its register image
        bool audio;      // both SAIs initialised
        bool display;    // OLED transport initialised
    };

    bool Init();

    /** Start the audio stream at contracts::kAudioBlockSize. */
    bool StartAudio(Audio6::Callback cb);

    /** ~1 kHz service loop: controls, MIDI, diagnostics, LED. */
    void Loop();

    const Status& GetStatus() const { return status_; }

    daisy::DaisySeed& seed() { return seed_; }
    Audio6&           audio() { return audio_; }
    Controls&         controls() { return controls_; }
    CvIo&             cv() { return cv_; }
    SyncIo&           sync() { return sync_; }
    Pmod&             pmod() { return pmod_; }
    MidiIo&           midi() { return midi_; }
    Display&          display() { return display_; }
    Selftest&         selftest() { return selftest_; }

  private:
    daisy::DaisySeed seed_;
    daisy::I2CHandle i2c_;
    daisy::AdcHandle adc_;
    daisy::GPIO      panel_led_;

    Audio6   audio_;
    Controls controls_;
    CvIo     cv_;
    SyncIo   sync_;
    Pmod     pmod_;
    MidiIo   midi_;
    Display  display_;
    Selftest selftest_;

    Status status_;
};
} // namespace froemix
