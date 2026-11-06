// Scripted headless emulator session (see scenario.hpp for the stage contract).
#include "scenario.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <thread>
#include <utility>

#include "../audio/engine.hpp"
#include "../audio/recorder.hpp"
#include "../clap/host.hpp"
#include "../link/link.hpp"
#include "../midi/clock.hpp"
#include "../midi/router.hpp"
#include "synth.hpp"

namespace mnky::emulator {
namespace {

constexpr double kPi = 3.14159265358979323846;
// EmulatorTransport advances 1 ms of device time per SPI exchange and PicoLink
// paces exchanges >= 1 ms apart, so this is the ~5 s scripted session.
constexpr std::uint64_t kSessionExchanges = 5000;
// Tempo round trip through SET_TEMPO / EV_TEMPO, 140.00 bpm.
constexpr std::uint16_t kScriptedTempoX100 = 14000;
// Record stage length; SynthBank::kSampleRate * kTakeSeconds is a whole number
// of audio::kBlockSize blocks (750), so the frame count is exact.
constexpr int kTakeSeconds = 2;
constexpr int kTakeBlocks = kTakeSeconds * static_cast<int>(SynthBank::kSampleRate) /
                            audio::kBlockSize;
// Armed inputs of the take: a sine on input 1 and noise on input 2, so the WAV
// checks can tell the two files apart by content.
constexpr int kSineChannel = 0;
constexpr int kNoiseChannel = 1;
constexpr float kSineHz = 440.0f;
constexpr float kSineAmplitude = 0.5f;
constexpr float kNoiseAmplitude = 0.2f;

// First failure wins: later assertions are usually cascades of it.
class Checks {
 public:
  void require(bool condition, std::string message) {
    if (!condition && !failed_) {
      failed_ = true;
      message_ = std::move(message);
    }
  }
  bool failed() const { return failed_; }
  const std::string& message() const { return message_; }

 private:
  bool failed_ = false;
  std::string message_;
};

StageResult makeStage(std::string name, const Checks& checks, std::string passDetail) {
  StageResult result;
  result.name = std::move(name);
  if (checks.failed()) {
    result.status = StageStatus::Fail;
    result.detail = checks.message();
  } else {
    result.status = StageStatus::Pass;
    result.detail = std::move(passDetail);
  }
  return result;
}

std::string hex32(std::uint32_t value) {
  char buffer[16] = {};
  std::snprintf(buffer, sizeof buffer, "0x%08x", value);
  return buffer;
}

std::string bpmText(std::uint16_t bpmX100) {
  char buffer[24] = {};
  std::snprintf(buffer, sizeof buffer, "%.2f bpm", static_cast<double>(bpmX100) / 100.0);
  return buffer;
}

std::string numberText(double value, int decimals) {
  char buffer[32] = {};
  std::snprintf(buffer, sizeof buffer, "%.*f", decimals, value);
  return buffer;
}

double rmsOf(const float* samples, int n) {
  if (samples == nullptr || n <= 0) return 0.0;
  double sum = 0.0;
  for (int i = 0; i < n; ++i) sum += static_cast<double>(samples[i]) * samples[i];
  return std::sqrt(sum / static_cast<double>(n));
}

// Fills `frames` samples of a continuous sine and advances `phase` with it.
void fillSine(float* out, int frames, float amplitude, double hz, double& phase) {
  const double step = 2.0 * kPi * hz / static_cast<double>(SynthBank::kSampleRate);
  for (int i = 0; i < frames; ++i) {
    out[i] = static_cast<float>(static_cast<double>(amplitude) * std::sin(phase));
    phase += step;
    if (phase >= 2.0 * kPi) phase -= 2.0 * kPi;
  }
}

// ---------------------------------------------------------------------------
// Link session: pump the emulated Pico and drain what PicoLink decoded
// ---------------------------------------------------------------------------
struct LinkLog {
  std::uint64_t ticks = 0;
  std::uint64_t linked = 0;
  std::uint64_t lost = 0;
  std::uint64_t tempoEvents = 0;
  std::uint64_t stateEvents = 0;
  std::uint64_t controlEvents = 0;
  std::uint32_t lastTick = 0;
  std::uint16_t lastTempoX100 = 0;
  bool ticksAdvance = true;
  /// PicoLink exchange count when the Linked event arrived.
  std::uint64_t linkedAtExchange = 0;
  std::vector<link::PicoLink::Event> midiIn;
};

void drainEvents(link::PicoLink& pico, LinkLog& log) {
  link::PicoLink::Event event{};
  while (pico.nextEvent(event)) {
    switch (event.kind) {
      case link::PicoLink::Event::MidiIn:
        log.midiIn.push_back(event);
        break;
      case link::PicoLink::Event::Control:
        ++log.controlEvents;
        break;
      case link::PicoLink::Event::Tempo:
        log.lastTempoX100 = static_cast<std::uint16_t>(event.u32a);
        ++log.tempoEvents;
        break;
      case link::PicoLink::Event::Tick:
        if (log.ticks != 0 && event.u32a <= log.lastTick) log.ticksAdvance = false;
        log.lastTick = event.u32a;
        ++log.ticks;
        break;
      case link::PicoLink::Event::State:
        ++log.stateEvents;
        break;
      case link::PicoLink::Event::Linked:
        if (log.linked == 0) log.linkedAtExchange = pico.stats().tx;
        ++log.linked;
        break;
      case link::PicoLink::Event::LinkLost:
        ++log.lost;
        break;
    }
  }
}

// Runs `exchanges` paced SPI exchanges (PicoLink enforces >= 1 ms between them)
// while draining events. Returns false when the wall-clock guard trips, which
// only happens if the link stops exchanging frames.
bool pump(link::PicoLink& pico, std::uint64_t exchanges, LinkLog& log) {
  const std::uint64_t target = pico.stats().tx + exchanges;
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(static_cast<std::int64_t>(exchanges * 4 + 500));
  while (pico.stats().tx < target) {
    pico.poll();
    drainEvents(pico, log);
    if (std::chrono::steady_clock::now() > deadline) {
      drainEvents(pico, log);
      return false;
    }
    // poll() paces itself at >= 1 ms; the nap keeps the loop from spinning a
    // core while it waits for the next slot.
    std::this_thread::sleep_for(std::chrono::microseconds(200));
  }
  drainEvents(pico, log);
  return true;
}

// ---------------------------------------------------------------------------
// FX: audio::IEffect shim over mnky::clap::Host plus a local stub gain
// ---------------------------------------------------------------------------
// The app layer owns the real Engine<->Host adapter; the emulator needs the same
// two-line bridge to put a CLAP effect on an Engine send bus.
class ClapEffect final : public audio::IEffect {
 public:
  explicit ClapEffect(clap::Host& host) : mHost(host) {}
  void process(float* const* ch, int nch, int n) override {
    if (ch == nullptr || nch < 2 || n <= 0) return;
    // mnkygain is stereo in-place, exactly what the Engine send bus provides.
    (void)mHost.process(ch, 2, ch, 2, static_cast<std::uint32_t>(n));
  }
  const char* name() const override { return "clap-effect"; }

 private:
  clap::Host& mHost;
};

// Used only when no CLAP module can be loaded: a fixed 0.25 gain with the same
// IEffect contract, so the send path is still exercised (with a loud SKIP).
class StubGain final : public audio::IEffect {
 public:
  explicit StubGain(float gain) : mGain(gain) {}
  void process(float* const* ch, int nch, int n) override {
    if (ch == nullptr || n < 0) return;
    for (int channel = 0; channel < nch && channel <= 1; ++channel) {
      if (ch[channel] == nullptr) continue;
      for (int i = 0; i < n; ++i) ch[channel][i] *= mGain;
    }
  }
  const char* name() const override { return "stub-gain"; }

 private:
  float mGain = 1.0f;
};

// Output RMS / input RMS of one steady sine block through `effect`. Two blocks
// are processed so a filter's start-up transient lands in the discarded first
// one; the phase runs continuously across them.
float fxGainRatio(audio::IEffect& effect) {
  const int n = audio::kBlockSize;
  float source[2][audio::kBlockSize] = {};
  double phase = 0.0;
  fillSine(source[0], n, kSineAmplitude, kSineHz, phase);
  fillSine(source[1], n, kSineAmplitude, kSineHz, phase);

  float left[audio::kBlockSize] = {};
  float right[audio::kBlockSize] = {};
  float* buses[2] = {left, right};
  for (int block = 0; block < 2; ++block) {
    for (int i = 0; i < n; ++i) {
      left[i] = source[block][i];
      right[i] = source[block][i];
    }
    effect.process(buses, 2, n);
  }
  const double inputRms = rmsOf(source[1], n);
  if (inputRms <= 0.0) return 0.0f;
  return static_cast<float>(rmsOf(left, n) / inputRms);
}

// Master RMS of the same sine through Engine with `effect` in per-track FX
// slot 0 (fully wet). A null effect is the reference: the slot is configured
// but has no attached effect, so the Engine passes the strip dry. The retired
// global FX send buses are no longer a signal path.
float engineMasterRms(audio::IEffect* effect) {
  const int n = audio::kBlockSize;
  SynthBank synth;
  synth.setVoice(0, SynthBank::Voice::Sine, kSineHz, kSineAmplitude);

  audio::Engine engine;
  audio::Channel strip;
  strip.gain = 1.0f;
  strip.pan = 0.5f;
  engine.setChannel(0, strip);
  engine.setTrackFxSlotEffect(0, 0, effect);
  audio::TrackFxSlot slot;
  slot.id = "emulator.fx-probe";
  slot.wet = 1.0f;
  engine.setTrackFxSlot(0, 0, slot);
  engine.setMaster(1.0f);

  float outL[audio::kBlockSize] = {};
  float outR[audio::kBlockSize] = {};
  synth.render(n);
  engine.process(synth.inputs(), synth.activeChannels(), n, outL, outR);
  return static_cast<float>(rmsOf(outL, n));
}

// ---------------------------------------------------------------------------
// WAV verification: header/chunk walk plus sample-content statistics
// ---------------------------------------------------------------------------
struct WavFileInfo {
  bool ok = false;
  std::string problem;
  std::uint16_t format = 0;
  std::uint16_t channels = 0;
  std::uint32_t sampleRate = 0;
  std::uint16_t bits = 0;
  std::uint32_t factFrames = 0;
  std::uint32_t dataBytes = 0;
  std::vector<float> samples;  // interleaved, `channels` per frame
};

std::uint16_t readLe16(const unsigned char* p) {
  return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

std::uint32_t readLe32(const unsigned char* p) {
  return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
         (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

WavFileInfo readWav(const std::string& path) {
  WavFileInfo info;
  std::FILE* file = std::fopen(path.c_str(), "rb");
  if (file == nullptr) {
    info.problem = "cannot open " + path;
    return info;
  }
  std::fseek(file, 0, SEEK_END);
  const long size = std::ftell(file);
  std::fseek(file, 0, SEEK_SET);
  std::vector<unsigned char> bytes(size > 0 ? static_cast<std::size_t>(size) : 0u);
  const std::size_t read = bytes.empty() ? 0 : std::fread(bytes.data(), 1, bytes.size(), file);
  std::fclose(file);
  if (read != bytes.size()) {
    info.problem = "short read on " + path;
    return info;
  }
  if (bytes.size() < 56) {
    info.problem = path + " is shorter than a 56-byte float WAV header";
    return info;
  }
  if (std::memcmp(bytes.data(), "RIFF", 4) != 0) {
    info.problem = path + " does not start with RIFF";
    return info;
  }
  if (readLe32(&bytes[4]) != static_cast<std::uint32_t>(bytes.size() - 8)) {
    info.problem = path + " RIFF size does not match the file size";
    return info;
  }
  if (std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
    info.problem = path + " is not a WAVE file";
    return info;
  }

  std::size_t dataOffset = 0;
  bool haveFmt = false;
  bool haveData = false;
  std::size_t offset = 12;
  while (offset + 8 <= bytes.size()) {
    const unsigned char* id = &bytes[offset];
    const std::uint32_t chunkBytes = readLe32(&bytes[offset + 4]);
    const std::size_t body = offset + 8;
    if (body + chunkBytes > bytes.size()) {
      info.problem = path + " has a truncated chunk";
      return info;
    }
    if (std::memcmp(id, "fmt ", 4) == 0) {
      if (chunkBytes < 16) {
        info.problem = path + " has a short fmt chunk";
        return info;
      }
      info.format = readLe16(&bytes[body]);
      info.channels = readLe16(&bytes[body + 2]);
      info.sampleRate = readLe32(&bytes[body + 4]);
      info.bits = readLe16(&bytes[body + 14]);
      haveFmt = true;
    } else if (std::memcmp(id, "fact", 4) == 0) {
      if (chunkBytes < 4) {
        info.problem = path + " has a short fact chunk";
        return info;
      }
      info.factFrames = readLe32(&bytes[body]);
    } else if (std::memcmp(id, "data", 4) == 0) {
      dataOffset = body;
      info.dataBytes = chunkBytes;
      haveData = true;
    }
    offset = body + chunkBytes + (chunkBytes & 1u);
  }
  if (!haveFmt || !haveData) {
    info.problem = path + " is missing its fmt or data chunk";
    return info;
  }
  if (info.channels == 0 || info.bits != 32 || info.dataBytes % (info.channels * 4u) != 0) {
    info.problem = path + " has an inconsistent data/frame layout";
    return info;
  }
  const std::uint32_t frames = info.dataBytes / (info.channels * 4u);
  if (info.factFrames != frames) {
    info.problem = path + " fact frame count does not match the data chunk";
    return info;
  }
  info.samples.resize(info.dataBytes / sizeof(float));
  std::memcpy(info.samples.data(), &bytes[dataOffset], info.dataBytes);
  info.ok = true;
  return info;
}

int countSignChanges(const std::vector<float>& samples, int channel, int channels) {
  int changes = 0;
  bool havePrevious = false;
  bool previousPositive = false;
  for (std::size_t i = static_cast<std::size_t>(channel); i < samples.size();
       i += static_cast<std::size_t>(channels)) {
    const bool positive = samples[i] >= 0.0f;
    if (havePrevious && positive != previousPositive) ++changes;
    previousPositive = positive;
    havePrevious = true;
  }
  return changes;
}

double channelRms(const std::vector<float>& samples, int channel, int channels) {
  double sum = 0.0;
  std::size_t count = 0;
  for (std::size_t i = static_cast<std::size_t>(channel); i < samples.size();
       i += static_cast<std::size_t>(channels)) {
    sum += static_cast<double>(samples[i]) * samples[i];
    ++count;
  }
  if (count == 0) return 0.0;
  return std::sqrt(sum / static_cast<double>(count));
}

// ---------------------------------------------------------------------------
// Stage: link handshake (+ the tail of the 5 s session: tick stream, tempo echo)
// Stage: MIDI loopback inject -> PicoLink::nextEvent -> Router -> capture
// ---------------------------------------------------------------------------
struct LinkStages {
  StageResult handshake;
  StageResult loopback;
};

LinkStages runLinkStages() {
  Checks handshakeChecks;
  Checks loopbackChecks;
  LinkLog log;

  link::EmulatorTransport transport;
  link::PicoLink pico(transport);

  // 1) Handshake: PicoLink repeats HELLO as idle fill, the emulated Pico answers
  // ACK + transport state + tempo, which is the whole link-up contract.
  pump(pico, 50, log);
  handshakeChecks.require(pico.linkUp(), "link did not come up within 50 SPI exchanges (no ACK)");
  handshakeChecks.require(log.linked == 1,
                          "expected exactly one Linked event, got " + std::to_string(log.linked));
  handshakeChecks.require(pico.session() == transport.session(),
                          "session mismatch: PicoLink has " + hex32(pico.session()) +
                              ", emulated Pico has " + hex32(transport.session()));
  handshakeChecks.require(pico.piSession() != 0, "Pi session id is zero (must be nonzero)");
  handshakeChecks.require(log.stateEvents >= 1, "no transport state frame from the emulated Pico");
  handshakeChecks.require(log.tempoEvents >= 1 && log.lastTempoX100 == link::kTempoDefaultX100,
                          "expected the 120.00 bpm boot tempo, got " +
                              bpmText(log.lastTempoX100));
  handshakeChecks.require(transport.ledMask() == 0, "emulated Pico reported a stale LED mask");

  // 2) MIDI loopback: TRS IN 1 note-on -> nextEvent -> Router -> TRS OUT 1
  // (emitted through PicoLink::sendMidi into the emulated Pico's UART) + USB out.
  midi::Router router;
  router.setRoute(midi::Port::TrsIn1, midi::Port::TrsOut1, true);
  router.setRoute(midi::Port::TrsIn1, midi::Port::UsbOut, true);

  struct Delivered {
    midi::Port port;
    midi::MidiEvent event;
  };
  std::vector<Delivered> delivered;
  const auto deliver = [&](midi::Port dst, const midi::MidiEvent& ev) {
    delivered.push_back(Delivered{dst, ev});
    if (dst == midi::Port::TrsOut1) {
      // The real emit path: MSP frame -> emulated Pico UART.
      (void)pico.sendMidi(0, ev.data, ev.size);
    }
  };

  const std::uint8_t injected[3] = {0x90, 0x3C, 0x64};  // channel 1 note on, C4, vel 100
  loopbackChecks.require(transport.injectMidi(0, injected, 3),
                         "EmulatorTransport::injectMidi rejected the scripted note");
  pump(pico, 60, log);

  link::PicoLink::Event received{};
  bool sawInjected = false;
  for (const auto& event : log.midiIn) {
    if (event.u32a == 0 && event.n == 3 && event.a[0] == injected[0] &&
        event.a[1] == injected[1] && event.a[2] == injected[2]) {
      received = event;
      sawInjected = true;
    }
  }
  loopbackChecks.require(sawInjected,
                         "injected TRS IN 1 note-on never surfaced as a PicoLink MidiIn event");

  midi::MidiEvent note{};
  note.source = midi::Port::TrsIn1;
  note.size = static_cast<std::uint8_t>(sawInjected ? received.n : 3);
  for (int i = 0; i < 3; ++i) note.data[i] = sawInjected ? received.a[i] : injected[i];
  note.stampUs = static_cast<std::uint64_t>(sawInjected ? received.u32b : 0);
  router.route(note, deliver);
  pump(pico, 20, log);

  bool deliveredToTrsOut = false;
  bool deliveredToUsbOut = false;
  for (const auto& entry : delivered) {
    const bool matching = entry.event.size == 3 && entry.event.data[0] == 0x90 &&
                          entry.event.data[1] == 0x3C && entry.event.data[2] == 0x64;
    deliveredToTrsOut = deliveredToTrsOut || (entry.port == midi::Port::TrsOut1 && matching);
    deliveredToUsbOut = deliveredToUsbOut || (entry.port == midi::Port::UsbOut && matching);
  }
  loopbackChecks.require(deliveredToTrsOut, "Router did not deliver the note to TRS OUT 1");
  loopbackChecks.require(deliveredToUsbOut, "Router did not deliver the note to USB OUT");

  bool trsOutDelivered = false;
  for (const auto& sent : transport.sentMidi()) {
    if (sent.port == 0 && sent.size == 3 && sent.data[0] == injected[0] &&
        sent.data[1] == injected[1] && sent.data[2] == injected[2]) {
      trsOutDelivered = true;
    }
  }
  loopbackChecks.require(trsOutDelivered,
                         "TRS OUT 1 deliver did not reach the emulated Pico UART (MIDI_SEND)");

  // Channel filter: mask 0x0001 keeps channel 1 only, realtime passes always.
  router.setChannelMask(midi::Port::TrsIn1, 0x0001);
  delivered.clear();
  midi::MidiEvent otherChannel{};
  otherChannel.source = midi::Port::TrsIn1;
  otherChannel.size = 3;
  otherChannel.data[0] = 0x91;  // channel 2
  otherChannel.data[1] = 0x40;
  otherChannel.data[2] = 0x7F;
  router.route(otherChannel, deliver);
  loopbackChecks.require(delivered.empty(), "channel mask 0x0001 let a channel-2 note through");

  midi::MidiEvent realtime{};
  realtime.source = midi::Port::TrsIn1;
  realtime.size = 1;
  realtime.data[0] = midi::kClockByte;
  router.route(realtime, deliver);
  loopbackChecks.require(delivered.size() == 2,
                         "realtime 0xF8 must reach both TRS OUT 1 and USB OUT, got " +
                             std::to_string(delivered.size()));
  router.setChannelMask(midi::Port::TrsIn1, midi::Router::kAllChannels);

  // 3) Tail of the session: tempo round trip, then the rest of the 5 s so the
  // tick stream and the link state are exercised over the whole session.
  loopbackChecks.require(pico.setTempo(kScriptedTempoX100),
                         "PicoLink::setTempo refused the scripted tempo");
  const std::uint64_t exchanged = pico.stats().tx;
  const std::uint64_t remaining = kSessionExchanges > exchanged ? kSessionExchanges - exchanged : 0;
  const bool sessionCompleted = pump(pico, remaining + 10, log);
  handshakeChecks.require(sessionCompleted,
                          "the SPI session stalled before 5 s of emulated device time");
  handshakeChecks.require(log.lost == 0, "link reported LinkLost during the session");
  handshakeChecks.require(pico.linkUp(), "link dropped at the end of the session");
  handshakeChecks.require(log.ticksAdvance, "Pico tick numbers were not monotonic");
  handshakeChecks.require(log.ticks >= 100,
                          "expected >= 100 clock ticks over 5 s, got " + std::to_string(log.ticks));
  handshakeChecks.require(log.tempoEvents >= 2 && log.lastTempoX100 == kScriptedTempoX100,
                          "tempo round trip failed: expected echo " +
                              bpmText(kScriptedTempoX100) + ", got " +
                              bpmText(log.lastTempoX100));
  const double sessionSeconds = static_cast<double>(pico.stats().tx) *
                                static_cast<double>(link::EmulatorTransport::kTransactionUs) /
                                1'000'000.0;

  LinkStages stages;
  stages.handshake = makeStage(
      "link-handshake", handshakeChecks,
      "link up after " + std::to_string(log.linkedAtExchange) + " exchanges, session " +
          hex32(pico.session()) + ", " + std::to_string(log.ticks) + " ticks over " +
          numberText(sessionSeconds, 2) + " s, tempo echo " + bpmText(log.lastTempoX100) +
          ", stats tx=" + std::to_string(pico.stats().tx) +
          " rx=" + std::to_string(pico.stats().rx) +
          " dup=" + std::to_string(pico.stats().dup) +
          " bad=" + std::to_string(pico.stats().bad));
  stages.loopback = makeStage(
      "midi-loopback", loopbackChecks,
      "note-on 90 3C 64: TRS IN 1 -> TRS OUT 1 (MIDI_SEND port 0) + USB OUT, "
      "channel-2 note filtered, 0xF8 passed");
  return stages;
}

// ---------------------------------------------------------------------------
// Stage: record a two-armed-channel take through Engine + Recorder
// ---------------------------------------------------------------------------
struct RecordRound {
  StageResult stage;
  std::string takeDir;
};

RecordRound runRecordStage(const ScenarioOptions& options) {
  RecordRound round;
  Checks checks;
  if (options.workDir.empty()) {
    checks.require(false, "no work directory for the take");
    round.stage = makeStage("record-take", checks, "");
    return round;
  }

  SynthBank synth;
  synth.setVoice(kSineChannel, SynthBank::Voice::Sine, kSineHz, kSineAmplitude);
  synth.setVoice(kNoiseChannel, SynthBank::Voice::Noise, 0.0f, kNoiseAmplitude);

  audio::Engine engine;
  audio::Channel sineStrip;
  sineStrip.gain = 1.0f;
  sineStrip.pan = 0.5f;
  audio::Channel noiseStrip;
  noiseStrip.gain = 1.0f;
  noiseStrip.pan = 0.5f;
  engine.setChannel(kSineChannel, sineStrip);
  engine.setChannel(kNoiseChannel, noiseStrip);
  engine.setMaster(0.8f);

  audio::Recorder recorder;
  const std::uint32_t armMask = (1u << kSineChannel) | (1u << kNoiseChannel);
  checks.require(recorder.startTake(options.workDir, armMask, SynthBank::kSampleRate),
                 "Recorder::startTake failed for " + options.workDir);

  float outL[audio::kBlockSize] = {};
  float outR[audio::kBlockSize] = {};
  const float* taps[audio::kMaxInputs] = {};
  for (int block = 0; block < kTakeBlocks; ++block) {
    synth.render(audio::kBlockSize);
    engine.process(synth.inputs(), synth.activeChannels(), audio::kBlockSize, outL, outR);
    for (int channel = 0; channel < audio::kMaxInputs; ++channel) taps[channel] = engine.tap(channel);
    recorder.writeBlock(taps, outL, outR, audio::kBlockSize);
    // The writer thread needs to stay ahead of the 128-block queue; a nap every
    // 8 blocks is far more than it needs and keeps the drop count at zero.
    if (block % 8 == 7) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  const audio::Recorder::Take take = recorder.stopTake();

  const std::uint64_t expectedFrames =
      static_cast<std::uint64_t>(kTakeBlocks) * static_cast<std::uint64_t>(audio::kBlockSize);
  checks.require(!take.path.empty(), "stopTake() returned no take path");
  checks.require(take.files == 3,
                 "expected 3 closed WAVs (2 armed inputs + master), got " +
                     std::to_string(take.files));
  checks.require(take.frames == expectedFrames,
                 "expected " + std::to_string(expectedFrames) + " frames, got " +
                     std::to_string(take.frames));
  checks.require(take.drops == 0,
                 "recorder dropped " + std::to_string(take.drops) + " blocks");
  checks.require(!take.path.empty() && std::filesystem::is_directory(take.path),
                 "take directory " + take.path + " does not exist");

  round.takeDir = take.path;
  round.stage = makeStage("record-take", checks,
                          std::to_string(take.files) + " WAVs, " + std::to_string(take.frames) +
                              " frames at " + std::to_string(SynthBank::kSampleRate) +
                              " Hz, 0 drops, arms 0x1 + 0x2, " + take.path);
  return round;
}

// ---------------------------------------------------------------------------
// Stage: FX pass through the real mnkygain (or the stub gain + SKIP)
// ---------------------------------------------------------------------------
StageResult runFxStage(const ScenarioOptions& options) {
  Checks checks;
  StageResult result;

  // FX slot 0 with no attached effect is the reference the FX has to move.
  const float referenceRms = engineMasterRms(nullptr);

  const bool haveModule = !options.pluginPath.empty() && std::filesystem::exists(options.pluginPath);
  if (!haveModule) {
    const std::string reason =
        options.pluginPath.empty()
            ? "no CLAP module path (pass argv[1] or MNKY_SELFTEST_CLAP_PLUGIN)"
            : "CLAP module not found at " + options.pluginPath;
    StubGain stub(0.25f);
    const float stubRatio = fxGainRatio(stub);
    const float stubMasterRms = engineMasterRms(&stub);
    checks.require(stubRatio > 0.20f && stubRatio < 0.30f,
                   "stub gain measured " + numberText(stubRatio, 3) + " (expected ~0.250)");
    checks.require(std::fabs(stubMasterRms - referenceRms) > 0.02f,
                   "stub gain in FX slot 0 did not change the Engine master "
                   "output");
    result.name = "fx-clap";
    result.status = checks.failed() ? StageStatus::Fail : StageStatus::Skip;
    result.detail = reason + "; stub-gain 0.25 measured " + numberText(stubRatio, 3) +
                    " (master rms " + numberText(stubMasterRms, 3) + " vs " +
                    numberText(referenceRms, 3) + " FX1-slot reference)";
    return result;
  }

  clap::Host host;
  checks.require(host.load(options.pluginPath),
                 "clap::Host::load failed for " + options.pluginPath + ": " + host.lastError());
  checks.require(host.loaded(), "clap::Host reported no plugin instance after load");
  checks.require(host.activate(static_cast<double>(SynthBank::kSampleRate), audio::kBlockSize),
                 "clap::Host::activate failed: " + host.lastError());

  const std::vector<clap::Param> params = host.params();
  checks.require(params.size() >= 4,
                 "expected the 4 mnkygain parameters, got " + std::to_string(params.size()));
  std::uint32_t gainId = CLAP_INVALID_ID;
  for (const auto& param : params) {
    if (param.name == "Gain") gainId = param.id;
  }
  checks.require(gainId != CLAP_INVALID_ID, "no parameter named \"Gain\" on the loaded module");

  ClapEffect effect(host);
  float boostRatio = 0.0f;
  float cutRatio = 0.0f;
  if (gainId != CLAP_INVALID_ID) {
    checks.require(host.setParam(gainId, 12.0), "setParam(Gain, +12 dB) failed");
    boostRatio = fxGainRatio(effect);
    checks.require(host.setParam(gainId, -24.0), "setParam(Gain, -24 dB) failed");
    cutRatio = fxGainRatio(effect);
    checks.require(boostRatio > 1.5f,
                   "Gain +12 dB gave a " + numberText(boostRatio, 3) +
                       "x output/input ratio (expected > 1.5x)");
    checks.require(cutRatio < 0.15f,
                   "Gain -24 dB gave a " + numberText(cutRatio, 3) +
                       "x output/input ratio (expected < 0.15x)");
  }

  checks.require(host.setParam(gainId, 12.0), "setParam(Gain, +12 dB) restore failed");
  const float fxMasterRms = engineMasterRms(&effect);
  checks.require(std::fabs(fxMasterRms - referenceRms) > 0.02f,
                 "mnkygain in FX slot 0 did not change the Engine master "
                 "output");

  result.name = "fx-clap";
  if (checks.failed()) {
    result.status = StageStatus::Fail;
    result.detail = checks.message();
  } else {
    result.status = StageStatus::Pass;
    result.detail = "mnkygain " + host.id() + " (" + std::to_string(params.size()) +
                    " params): gain +12 dB -> " + numberText(boostRatio, 3) + "x, -24 dB -> " +
                    numberText(cutRatio, 3) + "x, Engine master rms " +
                    numberText(fxMasterRms, 3) + " vs " + numberText(referenceRms, 3) +
                    " FX1-slot reference";
  }
  return result;
}

// ---------------------------------------------------------------------------
// Stage: verify the recorded WAVs (headers, frames, sample content)
// ---------------------------------------------------------------------------
StageResult runWavStage(const std::string& takeDir) {
  Checks checks;
  if (takeDir.empty()) {
    checks.require(false, "no take directory from the record stage");
    return makeStage("wav-verify", checks, "");
  }

  const std::filesystem::path dir(takeDir);
  const WavFileInfo sine = readWav((dir / "ch00.wav").string());
  const WavFileInfo noise = readWav((dir / "ch01.wav").string());
  const WavFileInfo master = readWav((dir / "master.wav").string());
  const std::uint32_t expectedFrames = static_cast<std::uint32_t>(kTakeBlocks) * audio::kBlockSize;

  checks.require(sine.ok, "ch00.wav: " + sine.problem);
  checks.require(noise.ok, "ch01.wav: " + noise.problem);
  checks.require(master.ok, "master.wav: " + master.problem);
  if (checks.failed()) return makeStage("wav-verify", checks, "");

  // Headers: 32-bit IEEE float, 48 kHz, mono channel takes and a stereo master.
  checks.require(sine.format == 3 && noise.format == 3 && master.format == 3,
                 "expected WAVE format 3 (IEEE float) in the take files");
  checks.require(sine.bits == 32 && noise.bits == 32 && master.bits == 32,
                 "expected 32-bit samples in the take files");
  checks.require(sine.channels == 1 && noise.channels == 1, "armed channel takes must be mono");
  checks.require(master.channels == 2, "master.wav must be stereo");
  checks.require(sine.sampleRate == SynthBank::kSampleRate &&
                     noise.sampleRate == SynthBank::kSampleRate &&
                     master.sampleRate == SynthBank::kSampleRate,
                 "sample rate is not " + std::to_string(SynthBank::kSampleRate) + " Hz");
  checks.require(sine.factFrames == expectedFrames && noise.factFrames == expectedFrames &&
                     master.factFrames == expectedFrames,
                 "expected " + std::to_string(expectedFrames) + " frames in every take file");

  // Sample content: the sine file must look like a 440 Hz sine at 0.5 peak and
  // the noise file like uniform noise at 0.2 amplitude.
  const double sineRms = channelRms(sine.samples, 0, 1);
  const int sineCrossings = countSignChanges(sine.samples, 0, 1);
  const double noiseRms = channelRms(noise.samples, 0, 1);
  const int noiseCrossings = countSignChanges(noise.samples, 0, 1);
  const int expectedSineCrossings = 2 * static_cast<int>(kSineHz) * kTakeSeconds;
  checks.require(sineRms > 0.28 && sineRms < 0.45,
                 "ch00.wav rms " + numberText(sineRms, 3) + " (expected ~0.354 for a 0.5 sine)");
  checks.require(sineCrossings > expectedSineCrossings - 60 &&
                     sineCrossings < expectedSineCrossings + 60,
                 "ch00.wav has " + std::to_string(sineCrossings) + " zero crossings (expected ~" +
                     std::to_string(expectedSineCrossings) + " for 440 Hz)");
  checks.require(noiseRms > 0.08 && noiseRms < 0.15,
                 "ch01.wav rms " + numberText(noiseRms, 3) +
                     " (expected ~0.115 for 0.2 uniform noise)");
  checks.require(noiseCrossings > 20000,
                 "ch01.wav has only " + std::to_string(noiseCrossings) +
                     " zero crossings; it does not look like noise");

  const double masterLeftRms = channelRms(master.samples, 0, 2);
  const double masterRightRms = channelRms(master.samples, 1, 2);
  checks.require(masterLeftRms > 0.05 && masterRightRms > 0.05,
                 "master.wav is silent or mono: L rms " + numberText(masterLeftRms, 3) +
                     ", R rms " + numberText(masterRightRms, 3));

  return makeStage("wav-verify", checks,
                   "3 float32 WAVs, " + std::to_string(expectedFrames) + " frames: ch00 rms " +
                       numberText(sineRms, 3) + " with " + std::to_string(sineCrossings) +
                       " crossings (~440 Hz), ch01 rms " + numberText(noiseRms, 3) + " with " +
                       std::to_string(noiseCrossings) + " crossings, master L/R rms " +
                       numberText(masterLeftRms, 3) + "/" + numberText(masterRightRms, 3));
}

}  // namespace

ScenarioReport runScenario(const ScenarioOptions& options) {
  ScenarioReport report;
  LinkStages linkStages = runLinkStages();
  report.stages.push_back(std::move(linkStages.handshake));
  report.stages.push_back(std::move(linkStages.loopback));

  RecordRound record = runRecordStage(options);
  report.stages.push_back(std::move(record.stage));
  report.stages.push_back(runFxStage(options));
  report.stages.push_back(runWavStage(record.takeDir));
  return report;
}

bool ScenarioReport::passed() const { return failedCount() == 0; }

int ScenarioReport::failedCount() const {
  int failed = 0;
  for (const auto& stage : stages) {
    if (stage.status == StageStatus::Fail) ++failed;
  }
  return failed;
}

int ScenarioReport::skippedCount() const {
  int skipped = 0;
  for (const auto& stage : stages) {
    if (stage.status == StageStatus::Skip) ++skipped;
  }
  return skipped;
}

}  // namespace mnky::emulator
