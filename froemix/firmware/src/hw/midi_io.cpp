#include "hw/midi_io.hpp"

#include "contracts.hpp"
#include "pins.hpp"

namespace froemix
{
namespace
{
// Non-commercial SysEx ID for bring-up traffic. Replace with a registered ID
// before this firmware ships SysEx to anything but a bench tool.
constexpr uint8_t kSysExManufacturerId = 0x7D;
constexpr uint8_t kSysExStart          = 0xF0;
constexpr uint8_t kSysExEnd            = 0xF7;
} // namespace

void MidiIo::Init()
{
    daisy::MidiUartHandler::Config cfg;
    cfg.transport_config.periph = daisy::UartHandler::Config::Peripheral::USART_1;
    cfg.transport_config.rx     = pins::kMidiRx;
    cfg.transport_config.tx     = pins::kMidiTx;

    midi_.Init(cfg);
    midi_.StartReceive();
}

void MidiIo::Poll()
{
    // Both directions are Type A on this board, so the bytes are already in the
    // order the parser expects.
    midi_.Listen();
    while(midi_.HasEvents())
    {
        const daisy::MidiEvent event = midi_.PopEvent();
        last_status_ = static_cast<uint8_t>(event.type);
        ++rx_count_;
    }
}

void MidiIo::SendSysEx(const uint8_t* payload, size_t size)
{
    uint8_t buffer[64];
    if(size + 3 > sizeof(buffer))
        return;

    buffer[0] = kSysExStart;
    buffer[1] = kSysExManufacturerId;
    for(size_t i = 0; i < size; ++i)
        buffer[2 + i] = payload[i] & 0x7F; // SysEx data bytes are 7-bit
    buffer[2 + size] = kSysExEnd;

    midi_.SendMessage(buffer, size + 3);
}
} // namespace froemix
