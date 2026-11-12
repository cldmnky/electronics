// Froemix firmware - M0 scaffold.
//
// What exists today (doc/FIRMWARE.md): board bring-up, the six-channel audio
// path (base-board codec + eurorack-pmod TDM), the CV/sync/control primitives
// with the board contracts as constants, and the M1 self-test report.
//
// The only engine in the tree is engine::Silent: the two codecs and their DMA
// run, nothing is synthesised, and no DC can reach a DC-coupled output. Real
// DSP is milestone M3 (doc/FIRMWARE.md).

#include "board.hpp"
#include "contracts.hpp"
#include "engine/engine.hpp"
#include "sys/system.h"

namespace
{
froemix::Board           g_board;
froemix::engine::Silent  g_engine;

void AudioCallback(float* const* in, float* const* out, size_t size)
{
    g_engine.Process(in, out, size);
}
} // namespace

int main(void)
{
    g_board.Init();
    g_engine.Init(froemix::contracts::kSampleRateHz);
    g_board.StartAudio(AudioCallback);

    while(true)
    {
        g_board.Loop();
        daisy::System::Delay(1);
    }
}
