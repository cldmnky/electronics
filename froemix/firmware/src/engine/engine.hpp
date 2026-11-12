#pragma once
// The audio-engine seam.
//
// board/audio only decides *where* the six channels come from; an engine
// decides what happens to them. There is exactly one engine in this tree and it
// is silent - the DSP is milestone M3 (doc/FIRMWARE.md), so nothing here has to
// be guessed in advance.
//
// Contract for any engine (both are board properties, see contracts.hpp):
//   - six channels, 0/1 = the board's own ANALOG jacks, 2..5 = eurorack-pmod
//   - never leave DC on an output: both analogue chains are DC-coupled
//   - block size is contracts::kAudioBlockSize, sample rate 48 kHz
//   - no allocation, no blocking: this runs in the DMA callback

#include <cstddef>

namespace froemix
{
namespace engine
{
class Engine
{
  public:
    static constexpr size_t kChannels = 6;

    virtual ~Engine() {}

    virtual void Init(float sample_rate) = 0;
    virtual void Process(float* const* in, float* const* out, size_t size) = 0;
};

/** Zeroes every output. Used until a real engine exists, and as the reference
 ** implementation of the "no DC, no uninitialised buffer" rule. */
class Silent : public Engine
{
  public:
    void Init(float sample_rate) override;
    void Process(float* const* in, float* const* out, size_t size) override;
};
} // namespace engine
} // namespace froemix
