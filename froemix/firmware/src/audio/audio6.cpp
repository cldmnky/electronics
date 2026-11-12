#include "audio/audio6.hpp"

#include "contracts.hpp"
#include "daisy_core.h"
#include "pins.hpp"

namespace froemix
{
namespace
{
constexpr size_t kMaxBlock_ = Audio6::kMaxBlock;

// DMA buffers must sit in the non-cached SRAM section.
int32_t DMA_BUFFER_MEM_SECTION g_rx1[kMaxBlock_ * 2]; // SAI1: 2ch stereo
int32_t DMA_BUFFER_MEM_SECTION g_tx1[kMaxBlock_ * 2];
int32_t DMA_BUFFER_MEM_SECTION g_rx2[kMaxBlock_ * 4]; // SAI2: 4-slot TDM
int32_t DMA_BUFFER_MEM_SECTION g_tx2[kMaxBlock_ * 4];
} // namespace

Audio6* Audio6::instance_ = nullptr;

bool Audio6::Init(bool seed_rev_1_1)
{
    // ---------------------------------------------------------------------
    // SAI1 - the Seed's own WM8731. Pin map is the module-internal one (GPIOE
    // 2..6); the Seed revision decides which block transmits.
    // ---------------------------------------------------------------------
    daisy::SaiHandle::Config c1;
    c1.periph          = daisy::SaiHandle::Config::Peripheral::SAI_1;
    c1.sr              = daisy::SaiHandle::Config::SampleRate::SAI_48KHZ;
    c1.bit_depth       = daisy::SaiHandle::Config::BitDepth::SAI_24BIT;
    c1.a_sync          = daisy::SaiHandle::Config::Sync::MASTER;
    c1.b_sync          = daisy::SaiHandle::Config::Sync::SLAVE;
    c1.pin_config.mclk = daisy::Pin(daisy::PORTE, 2);
    c1.pin_config.fs   = daisy::Pin(daisy::PORTE, 4);
    c1.pin_config.sck  = daisy::Pin(daisy::PORTE, 5);
    c1.pin_config.sa   = daisy::Pin(daisy::PORTE, 6);
    c1.pin_config.sb   = daisy::Pin(daisy::PORTE, 3);
    if(seed_rev_1_1)
    {
        c1.a_dir = daisy::SaiHandle::Config::Direction::RECEIVE;
        c1.b_dir = daisy::SaiHandle::Config::Direction::TRANSMIT;
    }
    else
    {
        c1.a_dir = daisy::SaiHandle::Config::Direction::TRANSMIT;
        c1.b_dir = daisy::SaiHandle::Config::Direction::RECEIVE;
    }

    // ---------------------------------------------------------------------
    // SAI2 - eurorack-pmod AK4619, 4 x 32-bit TDM slots.
    //
    // H1 on the base board: pin 1 SDI (codec in) = D25, pin 3 SDO (codec out)
    // = D26, pin 5 LRCK = D27, pin 7 BICK = D28, pin 8 MCLK = D24. The Seed
    // therefore drives block B (b_dir TRANSMIT -> D25) and receives on block A
    // (a_dir RECEIVE -> D26); block B is the master, block A follows it.
    // ---------------------------------------------------------------------
    daisy::SaiHandle::Config c2;
    c2.periph          = daisy::SaiHandle::Config::Peripheral::SAI_2;
    c2.sr              = daisy::SaiHandle::Config::SampleRate::SAI_48KHZ;
    c2.bit_depth       = daisy::SaiHandle::Config::BitDepth::SAI_32BIT;
    c2.tdm_slots       = contracts::kTdmSlots; // local libDaisy patch
    c2.a_sync          = daisy::SaiHandle::Config::Sync::SLAVE;
    c2.b_sync          = daisy::SaiHandle::Config::Sync::MASTER;
    c2.a_dir           = daisy::SaiHandle::Config::Direction::RECEIVE;
    c2.b_dir           = daisy::SaiHandle::Config::Direction::TRANSMIT;
    c2.pin_config.mclk = pins::kPmodMclk;
    c2.pin_config.fs   = pins::kPmodLrck;
    c2.pin_config.sck  = pins::kPmodBick;
    c2.pin_config.sa   = pins::kPmodSdout; // codec -> Seed
    c2.pin_config.sb   = pins::kPmodSdin;  // Seed   -> codec

    if(sai1_.Init(c1) != daisy::SaiHandle::Result::OK)
        return false;
    if(sai2_.Init(c2) != daisy::SaiHandle::Result::OK)
        return false;

    blocksize_ = 0;
    cb_        = nullptr;
    for(size_t ch = 0; ch < kChannels; ++ch)
    {
        in_ptr_[ch]  = in_[ch];
        out_ptr_[ch] = out_[ch];
        for(size_t i = 0; i < kMaxBlock; ++i)
        {
            in_[ch][i]  = 0.0f;
            out_[ch][i] = 0.0f;
        }
    }
    return true;
}

bool Audio6::Start(size_t blocksize, Callback cb)
{
    if(cb == nullptr || blocksize == 0 || blocksize > kMaxBlock)
        return false;

    blocksize_ = blocksize;
    cb_        = cb;
    instance_  = this;

    for(size_t i = 0; i < kMaxBlock * 2; ++i)
    {
        g_rx1[i] = 0;
        g_tx1[i] = 0;
    }
    for(size_t i = 0; i < kMaxBlock * 4; ++i)
    {
        g_rx2[i] = 0;
        g_tx2[i] = 0;
    }

    // SAI2 is started untended: its DMA only advances GetOffset(); the samples
    // are moved inside SAI1's callback (Process()). Starting it first means the
    // TDM stream is already running when the first half-block is converted.
    if(sai2_.StartDma(g_rx2,
                      g_tx2,
                      blocksize_ * contracts::kPmodAudioChannels,
                      nullptr)
       != daisy::SaiHandle::Result::OK)
        return false;

    if(sai1_.StartDma(g_rx1,
                      g_tx1,
                      blocksize_ * contracts::kFroemixAudioChannels,
                      DmaCallback)
       != daisy::SaiHandle::Result::OK)
        return false;

    return true;
}

void Audio6::Stop()
{
    sai1_.StopDma();
    sai2_.StopDma();
}

float Audio6::SampleRate() const
{
    return contracts::kSampleRateHz;
}

void Audio6::DmaCallback(int32_t* in, int32_t* out, size_t size)
{
    if(instance_ != nullptr)
        instance_->Process(in, out, size);
}

void Audio6::Process(int32_t* in, int32_t* out, size_t size)
{
    const size_t frames = size / contracts::kFroemixAudioChannels;
    if(cb_ == nullptr || frames == 0 || frames > blocksize_)
        return;

    // Both SAIs run from the same PLL at the same rate, but their DMA halves
    // are not phase-locked. GetOffset() names the half of the TDM buffer that
    // the codec's DMA most recently *finished*, so reading and writing that
    // half is always safe; the price is at most one extra block of latency.
    const size_t offset  = sai2_.GetOffset();
    int32_t*     tdm_in  = g_rx2 + offset;
    int32_t*     tdm_out = g_tx2 + offset;

    for(size_t i = 0; i < frames; ++i)
    {
        // froemix codec: 24-bit stereo, interleaved L/R.
        in_[0][i] = s242f(in[i * 2 + 0]);
        in_[1][i] = s242f(in[i * 2 + 1]);
        // eurorack-pmod: one 32-bit word per TDM slot.
        for(size_t s = 0; s < contracts::kPmodAudioChannels; ++s)
            in_[contracts::kFroemixAudioChannels + s][i]
                = s322f(tdm_in[i * 4 + s]);
    }

    cb_(in_ptr_, out_ptr_, frames);

    for(size_t i = 0; i < frames; ++i)
    {
        out[i * 2 + 0] = f2s24(out_[0][i]);
        out[i * 2 + 1] = f2s24(out_[1][i]);
        for(size_t s = 0; s < contracts::kPmodAudioChannels; ++s)
            tdm_out[i * 4 + s]
                = f2s32(out_[contracts::kFroemixAudioChannels + s][i]);
    }
}
} // namespace froemix
