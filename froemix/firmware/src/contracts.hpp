#pragma once
// Hardware contracts the firmware must honour, Rev 0.1 base board + euro board.
//
// Every constant names its source document. These are properties of the copper
// and the panel, not tuning values: changing one is a board change or a
// documented measurement, never a quick fix in the DSP. The values marked
// "derived" or "not qualified" are copied from a table or calculation and have
// NOT been measured on hardware - doc/FIRMWARE.md lists the bench work.

#include <cstddef>
#include <cstdint>

namespace froemix
{
namespace contracts
{
// ---------------------------------------------------------------------------
// Audio topology - doc/FRONTBOARD-INTERFACE.md §4, doc/EURO-BOARD.md §1
// ---------------------------------------------------------------------------
// Six channels at 48 kHz / one block:
//   0,1 = the base board's own codec (Seed WM8731, SAI1) -> ANALOG IN/OUT jacks
//   2..5 = the eurorack-pmod AK4619 (SAI2, 4-slot TDM) -> its 4 in / 4 out jacks
constexpr size_t kAudioChannels        = 6;
constexpr size_t kFroemixAudioChannels = 2;
constexpr size_t kPmodAudioChannels    = 4;
constexpr size_t kTdmSlots             = 4;
// The eurorack-pmod Daisy reference keeps the TDM block small; 32 frames is
// what its audio callback is built around.
constexpr size_t kAudioBlockSize = 32;
constexpr float  kSampleRateHz   = 48000.0f;

// J_AUDIO_IN_* -> Seed codec: 0.100 (x10 for unity at the jack)
constexpr float kAudioInGain  = 0.100f;
// Seed codec -> J_AUDIO_OUT_*: 3.300 (1 Vrms 0 dBFS = 3.27 Vrms at the jack)
constexpr float kAudioOutGain = 3.300f;
constexpr float kAudioInFullScaleVolts = 11.7f; // ±11.7 V in before the codec
// Both base-board audio chains are DC-coupled: an offset left on the codec
// output is amplified x3.3 and appears at the jack. Do not emit DC.
constexpr bool kAudioOutputMustBeDcFree = true;

// ---------------------------------------------------------------------------
// CV in - doc/FRONTBOARD-INTERFACE.md §3
//   V(adc) = kCvInOffset - kCvInScale * V(jack)
// ---------------------------------------------------------------------------
constexpr float kCvInScale  = 0.3010f; // V/V, simulated
constexpr float kCvInOffset = 1.6096f; // V at the ADC for 0 V at the jack
constexpr float kCvInRangeVolts = 5.0f; // declared ±5 V; ±10 V is not supported
// With the fitted 1 % resistors: ±2.02 % of scale and ±32.5 mV offset, so
// 1 V/oct tracking needs per-channel calibration (no firmware constant fixes it).
constexpr float kCvInToleranceScale  = 0.0202f;
constexpr float kCvInToleranceOffset = 0.0325f;

// ---------------------------------------------------------------------------
// CV out - doc/FRONTBOARD-INTERFACE.md §4, DESIGN-REVIEW R03
//   V(jack) = kCvOutGain * V(dac)      (125 us settle into 10 nF, C33/C34)
// ---------------------------------------------------------------------------
constexpr float kCvOutGain = 1.5121f; // V/V, load-independent into >=10k
constexpr float kCvOutFullScaleVolts = 4.99f;
// Fixed op-amp offset at the jack: calibratable, so it must be calibrated.
constexpr float kCvOutOffsetTolerance = 0.060f;
// HARD: the STM32H750 DAC output buffer stays DISABLED (0..VDDA range).
// With the buffer enabled the guaranteed range is 0.2..VDDA-0.2 V and the
// jack can never reach 0 V or full scale - no calibration can fix that.
constexpr bool kDacBufferEnabled = false;
// DAC channel -> jack. The nets are crossed on this board (main.tsx):
// Seed DAC_OUT2 (pad 29) feeds J_CV_OUT_1, Seed DAC_OUT1 (pad 30) feeds
// J_CV_OUT_2. Index 0 = jack 1, index 1 = jack 2.
constexpr size_t kCvOutJack1DacChannel = 2;
constexpr size_t kCvOutJack2DacChannel = 1;

// ---------------------------------------------------------------------------
// CD4051 mux - doc/FRONTBOARD-INTERFACE.md §1/§2, doc/EURO-BOARD.md §2/§7
// ---------------------------------------------------------------------------
constexpr size_t kMuxChannels     = 8;
constexpr size_t kMuxPotChannels  = 6;      // M_ADC_0..5: panel pots
constexpr size_t kMuxJoystickChannel = 6;   // M_ADC_6: 5-way ladder
constexpr size_t kMuxButtonChannel   = 7;   // M_ADC_7: MODE button
// The boards specify that firmware owns the channel-switch settle time and
// that no delay has been qualified (250 ohm Ron, worst-case source 25 kohm for
// a 100 k pot, 10 k pull-up on the released MODE node). Until M1 measures it,
// this stays false and the scan keeps libDaisy's mux round-robin timing.
constexpr bool kMuxSettleTimeQualified = false;

// ---------------------------------------------------------------------------
// Panel UI - doc/EURO-BOARD.md §2 (ladder) / §7 (firmware contract)
// Node voltage at P3V3_A = 3.30 V. Firmware decodes levels, not pins.
// ---------------------------------------------------------------------------
constexpr float kLadderIdle  = 3.30f;
constexpr float kLadderRight = 1.65f;
constexpr float kLadderLeft  = 1.05f;
constexpr float kLadderDown  = 0.60f;
constexpr float kLadderUp    = 0.30f;
constexpr float kLadderPress = 0.00f;
// Thresholds are simple midpoints of the table above - derived, not measured.
// The mux adds ~2 % and the closest pair (press/up) is ~370 ADC counts apart.
constexpr float kLadderIdleMin  = 2.40f;
constexpr float kLadderRightMin = 1.35f;
constexpr float kLadderLeftMin  = 0.82f;
constexpr float kLadderDownMin  = 0.45f;
constexpr float kLadderUpMin    = 0.15f;
// MODE button channel: pressed ~0 V, released pulled up to P3V3_A by R8.
constexpr float kButtonPressedMax = 1.00f;

// ---------------------------------------------------------------------------
// Sync - doc/FRONTBOARD-INTERFACE.md §4, DESIGN-REVIEW R04
// ---------------------------------------------------------------------------
// Q1 collector is pulled up to P3V3_A by R25, so a HIGH input at J_SYNC_IN
// reads LOW on the MCU pin.
constexpr bool  kSyncInInverted = true;
// 100k series into a 1M divider, so the jack threshold is ~0.72 V (unmeasured).
constexpr float kSyncInThresholdVolts = 0.72f;
// J_SYNC_OUT comes from a TL074 open-loop comparator + inverter (U11B/U11C),
// not a logic gate, and reaches ~5 V. Its maximum toggle rate is unmeasured.
constexpr bool kSyncOutRateQualified = false;

// ---------------------------------------------------------------------------
// Buses / devices
// ---------------------------------------------------------------------------
constexpr uint32_t kMidiBaudRate = 31250u;
// AK4619VN on the eurorack-pmod, 7-bit address 0x10.
constexpr uint8_t kAk4619I2cAddress = 0x20; // already shifted left (W bit 0)
// PC9635 16-channel LED driver on the eurorack-pmod, 7-bit address 0x05.
constexpr uint8_t kPc9635I2cAddress = 0x0A;

// ---------------------------------------------------------------------------
// Structural assertions: a mismatch here is a compile error, not a surprise.
// ---------------------------------------------------------------------------
static_assert(kFroemixAudioChannels + kPmodAudioChannels == kAudioChannels,
              "audio channel split must add up to 6");
static_assert(kTdmSlots == kPmodAudioChannels,
              "one TDM slot per eurorack-pmod channel");
static_assert(kMuxPotChannels < kMuxChannels, "pot channels must fit the mux");
static_assert(kMuxJoystickChannel < kMuxChannels, "joystick channel out of range");
static_assert(kMuxButtonChannel < kMuxChannels, "button channel out of range");
static_assert(kDacBufferEnabled == false, "DAC buffer must stay disabled");
static_assert(kAudioBlockSize <= 64, "TDM block size stays small");
} // namespace contracts
} // namespace froemix
