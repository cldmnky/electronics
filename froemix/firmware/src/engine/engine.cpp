#include "engine/engine.hpp"

namespace froemix
{
namespace engine
{
void Silent::Init(float sample_rate)
{
    (void)sample_rate;
}

void Silent::Process(float* const* in, float* const* out, size_t size)
{
    (void)in;
    for(size_t ch = 0; ch < kChannels; ++ch)
        for(size_t i = 0; i < size; ++i)
            out[ch][i] = 0.0f;
}
} // namespace engine
} // namespace froemix
