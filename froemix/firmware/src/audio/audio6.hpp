#pragma once
// Six-channel audio for the base board + eurorack-pmod.
//
//   channels 0,1 : Seed WM8731 on SAI1, stereo I2S 24-bit, the board's own
//                  ANALOG IN / ANALOG OUT jacks (gain 0.100 / 3.300, see
//                  contracts.hpp)
//   channels 2..5: AK4619VN on SAI2, 4-slot TDM, the eurorack-pmod's 4 in /
//                  4 out jacks
//
// Why this is not daisy::AudioHandle:
//   libDaisy's AudioHandle has a stub for two SAI peripherals ("Data will be
//   filled externally") and no TDM at all, so it cannot describe 2 + 4
//   channels. This class owns the two SaiHandles and does the conversion the
//   way the eurorack-pmod reference does, without patching hid/audio.cpp.
//   doc/FIRMWARE.md explains the dependency patch and the bench work.

#pragma once

#include <cstddef>
#include <cstdint>

#include "per/sai.h"

namespace froemix
{
class Audio6
{
  public:
    static constexpr size_t kChannels = 6; // 2 froemix + 4 eurorack-pmod
    static constexpr size_t kMaxBlock = 64;

    /** Non-interleaved, like daisy::AudioHandle::AudioCallback. */
    using Callback = void (*)(float* const* in, float* const* out, size_t size);

    /** Configure SAI1 + SAI2. `seed_rev_1_1` selects the SAI1 block direction
     ** (the Seed's codec wiring differs between board revisions; the Seed
     ** reports it, see DaisySeed::CheckBoardVersion()).
     */
    bool Init(bool seed_rev_1_1);

    /** Start both DMA streams. `blocksize` is in frames per callback. */
    bool Start(size_t blocksize, Callback cb);

    void Stop();

    size_t Blocksize() const { return blocksize_; }
    float  SampleRate() const;

    /** The TDM peripheral, exposed for the bring-up checks (sync/latency). */
    daisy::SaiHandle& PmodSai() { return sai2_; }

  private:
    static void DmaCallback(int32_t* in, int32_t* out, size_t size);
    void        Process(int32_t* in, int32_t* out, size_t size);

    static Audio6* instance_;

    daisy::SaiHandle sai1_;
    daisy::SaiHandle sai2_;
    size_t           blocksize_;
    Callback         cb_;
    float            in_[kChannels][kMaxBlock];
    float            out_[kChannels][kMaxBlock];
    float*           in_ptr_[kChannels];
    float*           out_ptr_[kChannels];
};
} // namespace froemix
