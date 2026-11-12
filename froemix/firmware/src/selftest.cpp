#include "selftest.hpp"

#include <cstdio>

#include "board.hpp"
#include "contracts.hpp"

namespace froemix
{
namespace
{
// One report per second at the ~1 kHz service rate.
constexpr uint32_t kTicksPerReport = 1000;
constexpr size_t   kLineMax        = 24;
constexpr size_t   kPayloadMax     = 60; // SendSysEx buffer allows 61

uint8_t Percent(bool value)
{
    return value ? 1u : 0u;
}

uint8_t Scale01(float value)
{
    if(value <= 0.0f)
        return 0;
    if(value >= 1.0f)
        return 99;
    return static_cast<uint8_t>(value * 99.0f + 0.5f);
}

uint8_t ScaleCv(float volts)
{
    // ±5 V -> 0..99 with 50 as 0 V.
    float normalised = volts / contracts::kCvInRangeVolts; // -1..1
    if(normalised < -1.0f)
        normalised = -1.0f;
    if(normalised > 1.0f)
        normalised = 1.0f;
    return static_cast<uint8_t>((normalised + 1.0f) * 49.5f + 0.5f);
}
} // namespace

void Selftest::Init(Board* board)
{
    board_   = board;
    ticks_   = 0;
    reports_ = 0;
}

void Selftest::Tick()
{
    if(board_ == nullptr)
        return;
    if(++ticks_ < kTicksPerReport)
        return;
    ticks_ = 0;
    Report();
    ++reports_;
}

void Selftest::Report()
{
    Board&    board  = *board_;
    const auto& flags = board.GetStatus();

    const uint8_t pot0 = Scale01(board.controls().Pot(0));
    const uint8_t pot1 = Scale01(board.controls().Pot(1));
    const uint8_t pot2 = Scale01(board.controls().Pot(2));
    const uint8_t pot3 = Scale01(board.controls().Pot(3));
    const uint8_t pot4 = Scale01(board.controls().Pot(4));
    const uint8_t pot5 = Scale01(board.controls().Pot(5));
    const uint8_t joy  = static_cast<uint8_t>(board.controls().Joy());
    const uint8_t btn  = Percent(board.controls().ButtonDown());
    const uint8_t cv1  = ScaleCv(board.cv().ReadIn(0));
    const uint8_t cv2  = ScaleCv(board.cv().ReadIn(1));
    const uint8_t sync = Percent(board.sync().Read());
    const uint8_t fl
        = static_cast<uint8_t>((Percent(flags.pmod_codec) << 0)
                               | (Percent(flags.pmod_leds) << 1)
                               | (Percent(flags.audio) << 2)
                               | (Percent(flags.display) << 3));

    char payload[kPayloadMax + 1];
    const int written = snprintf(payload,
                                sizeof(payload),
                                "T%lu,P%d%d%d%d%d%d,J%u,B%u,C%u,%u,S%u,F%X",
                                static_cast<unsigned long>(reports_),
                                pot0,
                                pot1,
                                pot2,
                                pot3,
                                pot4,
                                pot5,
                                joy,
                                btn,
                                cv1,
                                cv2,
                                sync,
                                fl);
    if(written <= 0 || static_cast<size_t>(written) > kPayloadMax)
        return;
    board.midi().SendSysEx(reinterpret_cast<const uint8_t*>(payload),
                          static_cast<size_t>(written));

    // Same content, condensed, on the panel display (ASCII only).
    char line[kLineMax];
    board.display().Clear();
    board.display().Text(0, 0, "froemix m0 selftest");
    snprintf(line,
             sizeof(line),
             "pmod%s audio%s",
             flags.pmod_codec ? "+" : "-",
             flags.audio ? "+" : "-");
    board.display().Text(0, 12, line);
    snprintf(line, sizeof(line), "pot %d%d%d %d%d%d", pot0, pot1, pot2, pot3,
             pot4, pot5);
    board.display().Text(0, 24, line);
    snprintf(line, sizeof(line), "cv %2d %2d s%d b%d", cv1, cv2, sync, btn);
    board.display().Text(0, 36, line);
    snprintf(line, sizeof(line), "midi rx %lu",
             static_cast<unsigned long>(board.midi().RxCount()));
    board.display().Text(0, 48, line);
    board.display().Update();
}
} // namespace froemix
