// Assert-style acceptance tests for the native audio device layer (no
// framework): sample-format conversion exactness, device id/enumeration
// helpers, the 18-in/20-out routing contract (DevicePump -> Engine ->
// DeviceOut -> interleaved device frames) and a fake period-based PCM device.
//
// Build (standalone):
//   clang++ -std=c++20 -Wall -Wextra -Werror -I. test_device_backend.cpp
//   device_backend.cpp engine.cpp take_player.cpp click.cpp recorder.cpp
//   -o /tmp/t_backend -pthread
//
// The fake PCM models the period/buffer contract both real backends share:
// 3 periods of 128 frames, interleaved S32_LE, partial writes at the end.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "device_backend.hpp"
#include "engine.hpp"

namespace {

using namespace mnky::audio;

int gFailures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            ++gFailures;                                                     \
        }                                                                    \
    } while (0)

constexpr double kCenterGain = 0.70710678118654752440;

void writeLe32(unsigned char* bytes, std::uint32_t value) {
    bytes[0] = static_cast<unsigned char>(value & 0xFFu);
    bytes[1] = static_cast<unsigned char>((value >> 8) & 0xFFu);
    bytes[2] = static_cast<unsigned char>((value >> 16) & 0xFFu);
    bytes[3] = static_cast<unsigned char>((value >> 24) & 0xFFu);
}

std::uint32_t readLe32(const unsigned char* bytes) {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8) |
           (static_cast<std::uint32_t>(bytes[2]) << 16) |
           (static_cast<std::uint32_t>(bytes[3]) << 24);
}

// ---------------------------------------------------------------------------
// A period-based fake PCM: 3 periods of `period` frames, interleaved, written
// and read in whole periods like snd_pcm with a 3-period buffer. It shares the
// exact encode/decode helpers the PipeWire and ALSA backends use.
class FakePcm {
public:
    FakePcm(int channels, int period, int periods = 3)
        : channels_(channels), period_(period), periods_(periods) {
        bytes_.resize(static_cast<std::size_t>(period_) * periods_ * channels_ * 4, 0);
    }

    int periodFrames() const { return period_; }
    int bufferFrames() const { return period_ * periods_; }
    int channels() const { return channels_; }

    int writePeriod(const float* interleaved, SampleFormat format) {
        if (count_ == periods_) return 0;
        unsigned char bytes[4 * kMaxDeviceOutputs * 512];
        const int stride = frameBytes(format, channels_);
        encodeInterleaved(interleaved, channels_, format, period_, bytes);
        const std::size_t offset =
            static_cast<std::size_t>((head_ + count_) % periods_) * period_ * stride;
        std::memcpy(bytes_.data() + offset, bytes,
                    static_cast<std::size_t>(stride) * period_);
        ++count_;
        return period_;
    }

    int readPeriod(float* interleaved, SampleFormat format) {
        if (count_ == 0) return 0;
        const int stride = frameBytes(format, channels_);
        unsigned char bytes[4 * kMaxDeviceOutputs * 512];
        const std::size_t offset = static_cast<std::size_t>(head_) * period_ * stride;
        std::memcpy(bytes, bytes_.data() + offset,
                    static_cast<std::size_t>(stride) * period_);
        decodeInterleaved(bytes, format, channels_, period_, interleaved);
        head_ = (head_ + 1) % periods_;
        --count_;
        return period_;
    }

    int bufferedPeriods() const { return count_; }

private:
    int channels_ = 2;
    int period_ = kBlockSize;
    int periods_ = 3;
    int head_ = 0;
    int count_ = 0;
    std::vector<unsigned char> bytes_;
};

// ---------------------------------------------------------------------------
void testFormatExactness() {
    CHECK(bytesPerSample(SampleFormat::F32) == 4);
    CHECK(bytesPerSample(SampleFormat::S32) == 4);
    CHECK(bytesPerSample(SampleFormat::S24_3) == 3);
    CHECK(bytesPerSample(SampleFormat::S24_32) == 4);
    CHECK(bytesPerSample(SampleFormat::S16) == 2);
    CHECK(bytesPerSample(SampleFormat::Unknown) == 0);
    CHECK(std::string(sampleFormatName(SampleFormat::S24_3)) == "S24_3LE");
    CHECK(sampleFormatFromBytes(4) == SampleFormat::S32);
    CHECK(sampleFormatFromBytes(3) == SampleFormat::S24_3);
    CHECK(sampleFormatFromBytes(1) == SampleFormat::Unknown);

    // S32: the full negative rail is exactly -1.0, the positive rail is
    // 1 - 2^-31; encode clamps at unity without wrap-around.
    unsigned char bytes[4] = {};
    writeLe32(bytes, 0x80000000u);
    CHECK(decodeSample(bytes, SampleFormat::S32) == -1.0f);
    writeLe32(bytes, 0x7FFFFFFFu);
    // 0x7FFFFFFF / 2^31 = 0.9999999995, which rounds to exactly 1.0f in the
    // float engine; only the negative rail is exactly -1.0f.
    const float positiveRail = decodeSample(bytes, SampleFormat::S32);
    CHECK(positiveRail == 1.0f);
    encodeSample(bytes, SampleFormat::S32, 1.0f);
    CHECK(readLe32(bytes) == 0x7FFFFFFFu);
    encodeSample(bytes, SampleFormat::S32, -1.0f);
    CHECK(readLe32(bytes) == 0x80000001u);
    encodeSample(bytes, SampleFormat::S32, 12.0f);
    CHECK(readLe32(bytes) == 0x7FFFFFFFu);
    encodeSample(bytes, SampleFormat::S32, -12.0f);
    CHECK(readLe32(bytes) == 0x80000001u);
    encodeSample(bytes, SampleFormat::S32, std::numeric_limits<float>::quiet_NaN());
    CHECK(readLe32(bytes) == 0u);
    encodeSample(bytes, SampleFormat::S32, 0.5f);
    CHECK(std::abs(decodeSample(bytes, SampleFormat::S32) - 0.5f) < 1.0e-9f);
    encodeSample(bytes, SampleFormat::S32, -0.25f);
    CHECK(std::abs(decodeSample(bytes, SampleFormat::S32) + 0.25f) < 1.0e-9f);

    // S24_3 packed: sign extension from bit 23, no byte-stride confusion.
    encodeSample(bytes, SampleFormat::S24_3, 1.0f);
    CHECK(bytes[0] == 0xFF && bytes[1] == 0xFF && bytes[2] == 0x7F);
    encodeSample(bytes, SampleFormat::S24_3, -1.0f);
    CHECK(bytes[0] == 0x01 && bytes[1] == 0x00 && bytes[2] == 0x80);
    const unsigned char negativeFull[3] = {0x00, 0x00, 0x80};
    CHECK(decodeSample(negativeFull, SampleFormat::S24_3) == -1.0f);
    encodeSample(bytes, SampleFormat::S24_3, 0.5f);
    CHECK(std::abs(decodeSample(bytes, SampleFormat::S24_3) - 0.5f) < 1.0e-6f);

    // S24_32: 24-bit value in the low bits of a 32-bit word, sign-extended.
    encodeSample(bytes, SampleFormat::S24_32, -0.5f);
    CHECK(std::abs(decodeSample(bytes, SampleFormat::S24_32) + 0.5f) < 1.0e-6f);

    // S16: the classic 32768/32767 asymmetry.
    encodeSample(bytes, SampleFormat::S16, 1.0f);
    CHECK(bytes[0] == 0xFF && bytes[1] == 0x7F);
    encodeSample(bytes, SampleFormat::S16, -1.0f);
    CHECK(bytes[0] == 0x01 && bytes[1] == 0x80);
    const unsigned char s16Min[2] = {0x00, 0x80};
    CHECK(decodeSample(s16Min, SampleFormat::S16) == -1.0f);

    // F32: bit-exact pass-through, values above unity preserved, non-finite
    // samples silenced.
    encodeSample(bytes, SampleFormat::F32, 0.1f);
    CHECK(decodeSample(bytes, SampleFormat::F32) == 0.1f);
    encodeSample(bytes, SampleFormat::F32, 1.5f);
    CHECK(decodeSample(bytes, SampleFormat::F32) == 1.5f);
    encodeSample(bytes, SampleFormat::F32, -2.0f);
    CHECK(decodeSample(bytes, SampleFormat::F32) == -2.0f);
    encodeSample(bytes, SampleFormat::F32, std::numeric_limits<float>::infinity());
    CHECK(decodeSample(bytes, SampleFormat::F32) == 0.0f);

    // Interleaved helpers keep the channel order for both directions.
    float interleaved[6] = {0.25f, -0.25f, 0.5f, -0.5f, 1.0f, -1.0f};
    unsigned char encoded[6 * 4] = {};
    encodeInterleaved(interleaved, 2, SampleFormat::S24_3, 3, encoded);
    float decoded[6] = {};
    decodeInterleaved(encoded, SampleFormat::S24_3, 3, 2, decoded);
    CHECK(std::abs(decoded[0] - 0.25f) < 1.0e-6f);
    CHECK(std::abs(decoded[1] + 0.25f) < 1.0e-6f);
    CHECK(std::abs(decoded[3] + 0.5f) < 1.0e-6f);
    CHECK(std::abs(decoded[4] - 1.0f) < 1.0e-6f);
    CHECK(std::abs(decoded[5] + 1.0f) < 1.0e-6f);
}

void testDeviceIds() {
    const std::string pipewire = makeDeviceId(DeviceApi::PipeWire, "alsa_output.usb-UMC1820");
    CHECK(pipewire == "pipewire:alsa_output.usb-UMC1820");
    const std::string alsa = makeDeviceId(DeviceApi::Alsa, "hw:CARD=UMC1820,DEV=0");
    CHECK(alsa == "alsa:hw:CARD=UMC1820,DEV=0");
    DeviceApi api = DeviceApi::None;
    std::string node;
    CHECK(parseDeviceId(pipewire, &api, &node));
    CHECK(api == DeviceApi::PipeWire && node == "alsa_output.usb-UMC1820");
    CHECK(parseDeviceId(alsa, &api, &node));
    CHECK(api == DeviceApi::Alsa && node == "hw:CARD=UMC1820,DEV=0");
    CHECK(!parseDeviceId("YWxzYV9vdXRwdXQ=", &api, &node));
    CHECK(!parseDeviceId("jack:system", &api, &node));
    CHECK(std::string(deviceApiName(DeviceApi::PipeWire)) == "pipewire");
    CHECK(std::string(deviceApiName(DeviceApi::Alsa)) == "alsa");
}

void testDeviceOutMap() {
    DeviceOut defaults;
    CHECK(defaults.main[0] == 0 && defaults.main[1] == 1);
    CHECK(defaults.out34[0] == 2 && defaults.out34[1] == 3);
    CHECK(defaults.phones[0] == 4 && defaults.phones[1] == 5);

    DeviceOutSlot slots[kMaxDeviceChannels] = {};
    buildDeviceOutMap(defaults, 20, slots);
    CHECK(slots[0] == DeviceOutSlot::MainL);
    CHECK(slots[1] == DeviceOutSlot::MainR);
    CHECK(slots[2] == DeviceOutSlot::Out34L);
    CHECK(slots[3] == DeviceOutSlot::Out34R);
    CHECK(slots[4] == DeviceOutSlot::PhonesL);
    CHECK(slots[5] == DeviceOutSlot::PhonesR);
    for (int channel = 6; channel < 20; ++channel) CHECK(slots[channel] == DeviceOutSlot::None);

    // Session-selected pairs: Main on 4/5, OUT3&4 on 6/7, phones on 8/9.
    DeviceOut custom;
    custom.main[0] = 4;
    custom.main[1] = 5;
    custom.out34[0] = 6;
    custom.out34[1] = 7;
    custom.phones[0] = 8;
    custom.phones[1] = 9;
    buildDeviceOutMap(custom, 20, slots);
    CHECK(slots[4] == DeviceOutSlot::MainL && slots[5] == DeviceOutSlot::MainR);
    CHECK(slots[6] == DeviceOutSlot::Out34L && slots[7] == DeviceOutSlot::Out34R);
    CHECK(slots[8] == DeviceOutSlot::PhonesL && slots[9] == DeviceOutSlot::PhonesR);
    CHECK(slots[0] == DeviceOutSlot::None);

    // Two buses on one device channel: the first bus wins, the channel is not
    // double-fed.
    DeviceOut overlap;
    overlap.main[0] = 0;
    overlap.main[1] = 1;
    overlap.out34[0] = 0;
    overlap.out34[1] = 1;
    buildDeviceOutMap(overlap, 20, slots);
    CHECK(slots[0] == DeviceOutSlot::MainL);
    CHECK(slots[1] == DeviceOutSlot::MainR);

    // A map wider than the device drops the out-of-range channels.
    DeviceOut wide;
    wide.main[0] = 18;
    wide.main[1] = 19;
    wide.out34[0] = 20;
    wide.out34[1] = 21;
    buildDeviceOutMap(wide, 20, slots);
    CHECK(slots[18] == DeviceOutSlot::MainL);
    CHECK(slots[19] == DeviceOutSlot::MainR);
}

void testEngineDeviceOutClamp() {
    Engine engine;
    const DeviceOut defaults = engine.deviceOut();
    CHECK(defaults == DeviceOut{});
    DeviceOut out;
    out.main[0] = -7;
    out.main[1] = 99;
    out.out34[0] = 3;
    out.out34[1] = 4;
    out.phones[0] = 5;
    out.phones[1] = 6;
    engine.setDeviceOut(out);
    const DeviceOut applied = engine.deviceOut();
    CHECK(applied.main[0] == 0);
    CHECK(applied.main[1] == 31);
    CHECK(applied.out34[0] == 3 && applied.out34[1] == 4);
    CHECK(applied.phones[0] == 5 && applied.phones[1] == 6);
}

void testPump() {
    DevicePump pump;
    float* planes[kMaxInputs] = {};
    std::vector<std::array<float, kBlockSize>> storage(kMaxInputs);
    for (int channel = 0; channel < kMaxInputs; ++channel) planes[channel] = storage[channel].data();

    // Nothing captured yet: zero-filled planes, nothing copied.
    CHECK(pump.readCapture(planes, kBlockSize) == 0);
    CHECK(storage[0][0] == 0.0f && storage[17][127] == 0.0f);

    // 128 frames of a channel-ident ramp arrive in one capture block.
    std::vector<float> captured(static_cast<std::size_t>(kBlockSize) * kMaxInputs);
    for (int frame = 0; frame < kBlockSize; ++frame) {
        for (int channel = 0; channel < kMaxInputs; ++channel) {
            captured[static_cast<std::size_t>(frame) * kMaxInputs + channel] =
                static_cast<float>(channel) + static_cast<float>(frame) / 1000.0f;
        }
    }
    pump.pushCapture(captured.data(), kBlockSize, kMaxInputs);
    CHECK(pump.capturedFrames() == static_cast<std::uint64_t>(kBlockSize));
    CHECK(pump.capturedBlocks() == 1);
    CHECK(pump.readCapture(planes, kBlockSize) == kBlockSize);
    CHECK(storage[0][0] == 0.0f);
    CHECK(storage[17][0] == 17.0f);
    CHECK(std::abs(storage[5][127] - (5.0f + 0.127f)) < 1.0e-6f);

    // A partial capture block is zero-filled in front, matching the newest
    // frames to the end of the engine block.
    pump.pushCapture(captured.data(), 64, kMaxInputs);
    CHECK(pump.readCapture(planes, kBlockSize) == 64);
    CHECK(storage[0][0] == 0.0f);
    CHECK(storage[0][63] == 0.0f);
    CHECK(std::abs(storage[0][64] - 0.0f) < 1.0e-6f);
    CHECK(std::abs(storage[3][127] - (3.0f + 0.063f)) < 1.0e-6f);

    // A capture backlog larger than half the ring skips to the newest block
    // instead of growing latency.
    std::vector<float> backlog(static_cast<std::size_t>(kBlockSize) * kMaxInputs, 0.0f);
    for (int block = 0; block < (DevicePump::kRingFrames / kBlockSize) + 8; ++block) {
        pump.pushCapture(backlog.data(), kBlockSize, kMaxInputs);
    }
    CHECK(pump.readCapture(planes, kBlockSize) == kBlockSize);
    CHECK(pump.droppedFrames() > 0);
}

void testEngineRouting() {
    // 18 device channels -> 18 strips -> three buses -> 20 device outputs.
    Engine engine;
    DevicePump pump;
    DeviceOut deviceOut;
    DeviceOutSlot slots[kMaxDeviceChannels] = {};
    buildDeviceOutMap(deviceOut, 20, slots);

    Channel main = engine.channel(0);
    main.gain = 1.0f;
    main.pan = 0.5f;
    engine.setChannel(0, main);
    engine.setInputSource(0, 0);
    engine.setOutputDestination(0, OutputDestination::Main);

    Channel out34 = engine.channel(1);
    out34.gain = 1.0f;
    out34.pan = 0.5f;
    engine.setChannel(1, out34);
    engine.setInputSource(1, 1);
    engine.setOutputDestination(1, OutputDestination::Out34);

    Channel phones = engine.channel(2);
    phones.gain = 1.0f;
    phones.pan = 0.5f;
    engine.setChannel(2, phones);
    engine.setInputSource(2, 2);
    engine.setOutputDestination(2, OutputDestination::Phones);

    // Remapped strip: strip 5 listens to device channel 10.
    Channel remapped = engine.channel(5);
    remapped.gain = 1.0f;
    remapped.pan = 0.5f;
    engine.setChannel(5, remapped);
    engine.setInputSource(5, 10);
    engine.setOutputDestination(5, OutputDestination::Out34);

    // The other 14 strips are live inputs on Main by default; silence them so
    // every device channel's content has exactly one source.
    for (int strip = 0; strip < kMaxInputs; ++strip) {
        if (strip == 0 || strip == 1 || strip == 2 || strip == 5) continue;
        engine.setInputSource(strip, kNoInputSource);
    }

    // Push a fixed level per device channel, then render one engine block
    // exactly like AudioGlue does.
    std::vector<float> captured(static_cast<std::size_t>(kBlockSize) * kMaxInputs);
    for (int frame = 0; frame < kBlockSize; ++frame) {
        for (int channel = 0; channel < kMaxInputs; ++channel) {
            captured[static_cast<std::size_t>(frame) * kMaxInputs + channel] =
                channel == 0 ? 0.25f : static_cast<float>(channel) / 18.0f;
        }
    }
    pump.pushCapture(captured.data(), kBlockSize, kMaxInputs);

    float planes[kMaxInputs][kBlockSize] = {};
    float* planePointers[kMaxInputs] = {};
    for (int channel = 0; channel < kMaxInputs; ++channel) planePointers[channel] = planes[channel];
    CHECK(pump.readCapture(planePointers, kBlockSize) == kBlockSize);

    const float* inputs[kMaxInputs] = {};
    for (int channel = 0; channel < kMaxInputs; ++channel) inputs[channel] = planes[channel];

    float mainL[kBlockSize] = {};
    float mainR[kBlockSize] = {};
    float out34L[kBlockSize] = {};
    float out34R[kBlockSize] = {};
    float phonesL[kBlockSize] = {};
    float phonesR[kBlockSize] = {};
    OutputBuses buses;
    buses.mainL = mainL;
    buses.mainR = mainR;
    buses.out34L = out34L;
    buses.out34R = out34R;
    buses.phonesL = phonesL;
    buses.phonesR = phonesR;
    engine.process(inputs, kMaxInputs, kBlockSize, buses);

    std::vector<float> deviceFrames(static_cast<std::size_t>(kBlockSize) * 20, -5.0f);
    mapBusesToDevice(mainL, mainR, out34L, out34R, phonesL, phonesR, slots, 20,
                     kBlockSize, deviceFrames.data());

    const float center = static_cast<float>(kCenterGain);
    CHECK(std::abs(deviceFrames[0] - 0.25f * center) < 1.0e-6f);
    CHECK(std::abs(deviceFrames[1] - 0.25f * center) < 1.0e-6f);
    // OUT3&4 carries strip 1 (device channel 1) and strip 5 (device 10).
    CHECK(std::abs(deviceFrames[2] - ((1.0f + 10.0f) / 18.0f) * center) < 1.0e-6f);
    CHECK(std::abs(deviceFrames[3] - ((1.0f + 10.0f) / 18.0f) * center) < 1.0e-6f);
    CHECK(std::abs(deviceFrames[4] - (2.0f / 18.0f) * center) < 1.0e-6f);
    CHECK(std::abs(deviceFrames[5] - (2.0f / 18.0f) * center) < 1.0e-6f);
    // Device channels without a bus are silent, not stale buffer content.
    for (int channel = 6; channel < 20; ++channel) {
        CHECK(deviceFrames[channel] == 0.0f);
        CHECK(deviceFrames[static_cast<std::size_t>(kBlockSize - 1) * 20 + channel] == 0.0f);
    }
    // Main L/R differ when the strip is panned hard right.
    Channel panned = engine.channel(0);
    panned.pan = 1.0f;
    engine.setChannel(0, panned);
    engine.process(inputs, kMaxInputs, kBlockSize, buses);
    mapBusesToDevice(mainL, mainR, out34L, out34R, phonesL, phonesR, slots, 20,
                     kBlockSize, deviceFrames.data());
    CHECK(std::abs(deviceFrames[0]) < 1.0e-6f);
    CHECK(std::abs(deviceFrames[1] - 0.25f) < 1.0e-6f);

    // Silent strip source (-1) removes only that strip from its bus.
    engine.setInputSource(1, kNoInputSource);
    engine.process(inputs, kMaxInputs, kBlockSize, buses);
    mapBusesToDevice(mainL, mainR, out34L, out34R, phonesL, phonesR, slots, 20,
                     kBlockSize, deviceFrames.data());
    CHECK(std::abs(deviceFrames[2] - (10.0f / 18.0f) * center) < 1.0e-6f);
    CHECK(std::abs(deviceFrames[3] - (10.0f / 18.0f) * center) < 1.0e-6f);
}

void testBufferSizesAndFakePcm() {
    // The same capture block rendered at every period size the System screen
    // offers, chunked to the engine's 128-frame ceiling, must land identically
    // in the device frame buffer.
    for (const int period : {64, 128, 256, 512, 1024}) {
        Engine engine;
        DevicePump pump;
        DeviceOutSlot slots[kMaxDeviceChannels] = {};
        buildDeviceOutMap(DeviceOut{}, 20, slots);

        Channel strip = engine.channel(0);
        strip.gain = 1.0f;
        strip.pan = 0.5f;
        engine.setChannel(0, strip);
        engine.setInputSource(0, 0);
        engine.setOutputDestination(0, OutputDestination::Main);

        std::vector<float> captured(static_cast<std::size_t>(period) * kMaxInputs);
        for (int frame = 0; frame < period; ++frame) {
            for (int channel = 0; channel < kMaxInputs; ++channel) {
                captured[static_cast<std::size_t>(frame) * kMaxInputs + channel] =
                    channel == 0 ? 0.5f : 0.0f;
            }
        }
        pump.pushCapture(captured.data(), period, kMaxInputs);

        std::vector<float> deviceFrames(static_cast<std::size_t>(period) * 20, -1.0f);
        int offset = 0;
        int blocks = 0;
        while (offset < period) {
            const int block = std::min(kBlockSize, period - offset);
            float planes[kMaxInputs][kBlockSize] = {};
            float* planePointers[kMaxInputs] = {};
            const float* inputs[kMaxInputs] = {};
            for (int channel = 0; channel < kMaxInputs; ++channel) {
                planePointers[channel] = planes[channel];
                inputs[channel] = planes[channel];
            }
            pump.readCapture(planePointers, block);
            float mainL[kBlockSize] = {};
            float mainR[kBlockSize] = {};
            float out34L[kBlockSize] = {};
            float out34R[kBlockSize] = {};
            float phonesL[kBlockSize] = {};
            float phonesR[kBlockSize] = {};
            OutputBuses buses;
            buses.mainL = mainL;
            buses.mainR = mainR;
            buses.out34L = out34L;
            buses.out34R = out34R;
            buses.phonesL = phonesL;
            buses.phonesR = phonesR;
            engine.process(inputs, kMaxInputs, block, buses);
            mapBusesToDevice(mainL, mainR, out34L, out34R, phonesL, phonesR, slots, 20,
                             block, deviceFrames.data() + static_cast<std::size_t>(offset) * 20);
            offset += block;
            ++blocks;
        }
        CHECK(offset == period);
        CHECK(blocks == (period + kBlockSize - 1) / kBlockSize);
        for (int frame = 0; frame < period; ++frame) {
            const std::size_t base = static_cast<std::size_t>(frame) * 20;
            CHECK(std::abs(deviceFrames[base] - 0.5f * static_cast<float>(kCenterGain)) <
                  1.0e-6f);
            CHECK(std::abs(deviceFrames[base + 1] - 0.5f * static_cast<float>(kCenterGain)) <
                  1.0e-6f);
            CHECK(deviceFrames[base + 6] == 0.0f);
        }
    }

    // Fake PCM: 3 periods of 128 frames, written/read in whole periods with the
    // negotiated S24_3LE/S32_LE formats.
    for (const SampleFormat format : {SampleFormat::S32, SampleFormat::S24_3, SampleFormat::F32}) {
        FakePcm pcm(20, kBlockSize);
        CHECK(pcm.bufferFrames() == 3 * kBlockSize);
        float frames[kBlockSize * 20] = {};
        for (int frame = 0; frame < kBlockSize; ++frame) {
            for (int channel = 0; channel < 20; ++channel) {
                frames[frame * 20 + channel] =
                    static_cast<float>(channel) / 20.0f - 0.5f;
            }
        }
        CHECK(pcm.writePeriod(frames, format) == kBlockSize);
        CHECK(pcm.writePeriod(frames, format) == kBlockSize);
        CHECK(pcm.writePeriod(frames, format) == kBlockSize);
        CHECK(pcm.writePeriod(frames, format) == 0);  // buffer full: the backend polls
        float readBack[kBlockSize * 20] = {};
        CHECK(pcm.readPeriod(readBack, format) == kBlockSize);
        CHECK(pcm.readPeriod(readBack, format) == kBlockSize);
        CHECK(pcm.readPeriod(readBack, format) == kBlockSize);
        CHECK(pcm.readPeriod(readBack, format) == 0);
        CHECK(std::abs(readBack[0] + 0.5f) < 1.0e-6f);
        CHECK(std::abs(readBack[19] - 0.45f) < 1.0e-6f);
        CHECK(std::abs(readBack[127 * 20 + 10] - 0.0f) < 1.0e-6f);
    }
}

}  // namespace

int main() {
    testFormatExactness();
    testDeviceIds();
    testDeviceOutMap();
    testEngineDeviceOutClamp();
    testPump();
    testEngineRouting();
    testBufferSizesAndFakePcm();

    if (gFailures != 0) {
        std::printf("%d device backend check(s) failed\n", gFailures);
        return 1;
    }
    std::printf("PASS audio device backend: formats, routing, pump, fake PCM\n");
    return 0;
}
