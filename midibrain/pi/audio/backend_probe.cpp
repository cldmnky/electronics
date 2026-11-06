// mnky_audio_probe: native-backend evidence tool for the Pi (UMC1820).
//
// It drives the exact device layer the app uses (audio::DeviceBackend +
// DevicePump + Engine + Recorder + DeviceOut mapping) without the Qt UI, and
// prints measurable numbers instead of screenshots:
//
//   mnky_audio_probe list
//       Every PipeWire node and ALSA PCM with its REAL name and channel width.
//
//   mnky_audio_probe take DIR --backend pipewire|alsa
//       --output <node-or-id> --input <node-or-id> --seconds N
//       --pattern silence|ident|buses [--amplitude A] [--expect none|ident|buses]
//       --device-out 0,1,2,3,4,5
//       Opens output+capture, records an 18-channel take through
//       Engine::tap -> Recorder (one mono float WAV per strip), and analyses
//       each channel: peak, RMS, DC, the fundamental at the expected frequency
//       and THD (harmonics 2..10). `--pattern ident` writes a different sine to
//       every device output channel, `--pattern buses` writes the six engine
//       bus sines through the session deviceOut map. Capturing PipeWire's sink
//       monitor then proves, per channel, that the device mapping is exact.
//
// Exit code 0 only when every analysed channel matches the expectation.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "alsa_backend.hpp"
#include "device_backend.hpp"
#include "engine.hpp"
#include "pipewire_backend.hpp"
#include "recorder.hpp"
#include "wav.hpp"

namespace {

using namespace mnky::audio;
using namespace std::chrono_literals;

constexpr double kTwoPi = 6.28318530717958647692;

enum class Pattern { Silence, Ident, Buses };

struct Options {
  std::string command;
  std::string directory;
  std::string backend = "pipewire";
  std::string outputId;
  std::string inputId;
  int seconds = 5;
  Pattern pattern = Pattern::Silence;
  Pattern expect = Pattern::Silence;
  bool expectSet = false;
  double amplitude = 0.5;
  DeviceOut deviceOut;
};

double channelFrequency(int deviceChannel) {
  return 400.0 + 50.0 * static_cast<double>(deviceChannel);
}

double mainLFrequency() { return 1000.0; }
double mainRFrequency() { return 1500.0; }
double out34LFrequency() { return 2000.0; }
double out34RFrequency() { return 2500.0; }
double phonesLFrequency() { return 3000.0; }
double phonesRFrequency() { return 3500.0; }

const char* patternName(Pattern pattern) {
  switch (pattern) {
    case Pattern::Ident:
      return "ident";
    case Pattern::Buses:
      return "buses";
    case Pattern::Silence:
      break;
  }
  return "silence";
}

struct Expectation {
  bool hasFrequency = false;
  double frequency = 0.0;
  bool expectedSilent = false;
};

Expectation expectationFor(Pattern pattern, const DeviceOut& deviceOut, int channel) {
  Expectation expectation;
  switch (pattern) {
    case Pattern::Ident:
      expectation.hasFrequency = true;
      expectation.frequency = channelFrequency(channel);
      return expectation;
    case Pattern::Buses: {
      struct Bus {
        int channel;
        double frequency;
      };
      const Bus buses[6] = {{deviceOut.main[0], mainLFrequency()},
                            {deviceOut.main[1], mainRFrequency()},
                            {deviceOut.out34[0], out34LFrequency()},
                            {deviceOut.out34[1], out34RFrequency()},
                            {deviceOut.phones[0], phonesLFrequency()},
                            {deviceOut.phones[1], phonesRFrequency()}};
      expectation.expectedSilent = true;
      for (const Bus& bus : buses) {
        if (bus.channel == channel) {
          expectation.expectedSilent = false;
          expectation.hasFrequency = true;
          expectation.frequency = bus.frequency;
        }
      }
      return expectation;
    }
    case Pattern::Silence:
      break;
  }
  return expectation;
}

// The app-side counterpart: capture -> DevicePump -> Engine (18 strips) ->
// Recorder taps; the playback side writes the selected test pattern straight to
// the device frames through the same DeviceOut mapping helpers.
class ProbeClient final : public DeviceClient {
 public:
  ProbeClient(const Options& options, Engine& engine, Recorder& recorder)
      : options_(options), engine_(engine), recorder_(recorder) {
    phases_.assign(std::max(kMaxDeviceChannels, kMaxDeviceOutputs), 0.0);
    busPhases_.assign(6, 0.0);
  }

  void renderDevice(float* interleaved, int frames, int channels) override {
    if (interleaved == nullptr || frames <= 0 || channels <= 0) return;
    recordCaptured(frames);
    generate(interleaved, frames, channels);
    renderedFrames_.fetch_add(static_cast<std::uint64_t>(frames));
  }

  void captureDevice(const float* interleaved, int frames, int channels) override {
    pump_.pushCapture(interleaved, frames, channels);
    capturedFrames_.fetch_add(static_cast<std::uint64_t>(frames));
  }

  std::uint64_t renderedFrames() const { return renderedFrames_.load(); }
  std::uint64_t capturedFrames() const { return capturedFrames_.load(); }

 private:
  void recordCaptured(int frames) {
    int offset = 0;
    while (offset < frames) {
      const int block = std::min(kBlockSize, frames - offset);
      float* planes[kMaxInputs] = {};
      const float* inputs[kMaxInputs] = {};
      for (int channel = 0; channel < kMaxInputs; ++channel) {
        planes[channel] = capturePlanes_[channel];
        inputs[channel] = capturePlanes_[channel];
      }
      pump_.readCapture(planes, block);
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
      engine_.process(inputs, kMaxInputs, block, buses);
      if (recorder_.recording()) {
        const float* taps[kMaxInputs] = {};
        for (int channel = 0; channel < kMaxInputs; ++channel) taps[channel] = engine_.tap(channel);
        recorder_.writeBlock(taps, mainL, mainR, block);
      }
      offset += block;
    }
  }

  static double advance(double& phase, double frequency, int frames) {
    const double step = kTwoPi * frequency / 48000.0;
    const double value = std::sin(phase);
    phase += step * static_cast<double>(frames);
    if (phase > kTwoPi) phase = std::fmod(phase, kTwoPi);
    return value;
  }

  void generate(float* interleaved, int frames, int channels) {
    switch (options_.pattern) {
      case Pattern::Silence:
        std::fill(interleaved, interleaved + static_cast<std::size_t>(frames) * channels,
                  0.0f);
        return;
      case Pattern::Ident: {
        // One exact sine per device channel, continuous across callbacks.
        for (int channel = 0; channel < channels; ++channel) {
          const double step = kTwoPi * channelFrequency(channel) / 48000.0;
          double phase = phases_[static_cast<std::size_t>(channel)];
          for (int frame = 0; frame < frames; ++frame) {
            interleaved[static_cast<std::size_t>(frame) * channels + channel] =
                static_cast<float>(options_.amplitude * std::sin(phase));
            phase += step;
            if (phase > kTwoPi) phase = std::fmod(phase, kTwoPi);
          }
          phases_[static_cast<std::size_t>(channel)] = phase;
        }
        return;
      }
      case Pattern::Buses: {
        DeviceOutSlot slots[kMaxDeviceChannels] = {};
        buildDeviceOutMap(options_.deviceOut, std::min(channels, kMaxDeviceChannels), slots);
        const double frequencies[6] = {mainLFrequency(),   mainRFrequency(),
                                       out34LFrequency(),  out34RFrequency(),
                                       phonesLFrequency(), phonesRFrequency()};
        int offset = 0;
        while (offset < frames) {
          const int block = std::min(kBlockSize, frames - offset);
          float mainL[kBlockSize] = {};
          float mainR[kBlockSize] = {};
          float out34L[kBlockSize] = {};
          float out34R[kBlockSize] = {};
          float phonesL[kBlockSize] = {};
          float phonesR[kBlockSize] = {};
          float* buses[6] = {mainL, mainR, out34L, out34R, phonesL, phonesR};
          for (int bus = 0; bus < 6; ++bus) {
            const double step = kTwoPi * frequencies[bus] / 48000.0;
            double phase = busPhases_[static_cast<std::size_t>(bus)];
            for (int frame = 0; frame < block; ++frame) {
              buses[bus][frame] =
                  static_cast<float>(options_.amplitude * std::sin(phase));
              phase += step;
              if (phase > kTwoPi) phase = std::fmod(phase, kTwoPi);
            }
            busPhases_[static_cast<std::size_t>(bus)] = phase;
          }
          mapBusesToDevice(mainL, mainR, out34L, out34R, phonesL, phonesR, slots, channels,
                           block, interleaved + static_cast<std::size_t>(offset) * channels);
          offset += block;
        }
        return;
      }
    }
  }

  const Options& options_;
  Engine& engine_;
  Recorder& recorder_;
  DevicePump pump_;
  float capturePlanes_[kMaxInputs][kBlockSize] = {};
  std::vector<double> phases_;
  std::vector<double> busPhases_;
  std::atomic<std::uint64_t> renderedFrames_{0};
  std::atomic<std::uint64_t> capturedFrames_{0};
};

void printInventory() {
  const DeviceInventory inventory = enumerateDevices();
  std::printf("PipeWire compiled=%s ALSA compiled=%s\n",
              pipewireCompiled() ? "yes" : "no", alsaCompiled() ? "yes" : "no");
  std::printf("defaults: pipewire out=%d in=%d alsa out=%d in=%d\n",
              inventory.pipewireOutput ? 1 : 0, inventory.pipewireInput ? 1 : 0,
              inventory.alsaOutput ? 1 : 0, inventory.alsaInput ? 1 : 0);
  const auto print = [](const char* direction, const std::vector<DeviceInfo>& devices) {
    for (const auto& device : devices) {
      std::printf("  %-6s %-9s id=%s channels=%d rate=%d default=%d name=\"%s\"\n", direction,
                  deviceApiName(device.api), device.id.c_str(), device.channels,
                  device.sampleRate, device.isDefault ? 1 : 0, device.name.c_str());
    }
  };
  print("out", inventory.outputs);
  print("in", inventory.inputs);
}

const DeviceInfo* findDevice(const std::vector<DeviceInfo>& devices, const std::string& id) {
  for (const auto& device : devices) {
    if (device.id == id || device.node == id) return &device;
  }
  return nullptr;
}

DeviceApi apiFromName(const std::string& name) {
  return name == "alsa" ? DeviceApi::Alsa : DeviceApi::PipeWire;
}

struct ChannelAnalysis {
  double peak = 0.0;
  double rms = 0.0;
  double dc = 0.0;
  double fundamental = 0.0;
  double thd = 0.0;
  std::uint64_t frames = 0;
  bool ok = false;
};

ChannelAnalysis analyseChannel(const std::string& path, double expectedFrequency) {
  ChannelAnalysis result;
  WavInfo info;
  std::string error;
  if (!inspectTakeWav(path, info, error)) {
    std::printf("  cannot read %s: %s\n", path.c_str(), error.c_str());
    return result;
  }
  std::FILE* file = std::fopen(path.c_str(), "rb");
  if (file == nullptr) return result;
  std::fseek(file, static_cast<long>(info.dataOffset), SEEK_SET);
  std::vector<float> samples;
  samples.resize(static_cast<std::size_t>(info.frames()));
  std::size_t filled = 0;
  while (filled < samples.size()) {
    const int want = static_cast<int>(std::min<std::size_t>(kTakeDecodeChunkFrames,
                                                           samples.size() - filled));
    const int got = decodeTakeFrames(file, info.channels, static_cast<int>(info.bits / 8),
                                     info.floatSamples(), samples.data() + filled, want);
    if (got <= 0) break;
    filled += static_cast<std::size_t>(got);
  }
  std::fclose(file);
  samples.resize(filled);
  result.frames = filled;
  if (filled == 0) return result;

  double sum = 0.0;
  double sumSquares = 0.0;
  double peak = 0.0;
  for (const float sample : samples) {
    const double value = sample;
    sum += value;
    sumSquares += value * value;
    peak = std::max(peak, std::fabs(value));
  }
  result.peak = peak;
  result.rms = std::sqrt(sumSquares / static_cast<double>(filled));
  result.dc = sum / static_cast<double>(filled);

  if (expectedFrequency > 0.0) {
    // Hann-windowed Goertzel for the fundamental and harmonics 2..10: THD is
    // the RMS sum of the harmonics over the fundamental, in dB. Two artifacts
    // are removed, both proven on the Pi:
    //  - a rectangular window leaks the strong fundamental into the harmonic
    //    bins at roughly -1/delta (about -77 dB here);
    //  - a window that straddles the leading/trailing silence of the capture
    //    turns the signal onset into a step (about -66 dB of fake THD), so the
    //    analysis starts 0.2 s after the first non-zero sample.
    constexpr int kHarmonics = 10;
    std::size_t firstSignal = 0;
    while (firstSignal < samples.size() && std::fabs(samples[firstSignal]) < 1.0e-9) {
      ++firstSignal;
    }
    const std::size_t start = std::min(samples.size(), firstSignal + 9600);
    const std::size_t windowFrames = std::min<std::size_t>(
        48000, samples.size() > start ? samples.size() - start : 0);
    if (windowFrames < 1024) {
      result.ok = true;
      return result;
    }
    std::vector<double> window(windowFrames);
    for (std::size_t i = 0; i < windowFrames; ++i) {
      window[i] = 0.5 - 0.5 * std::cos(kTwoPi * static_cast<double>(i) /
                                        static_cast<double>(windowFrames > 1 ? windowFrames - 1 : 1));
    }
    double power[kHarmonics + 1] = {};
    for (int harmonic = 1; harmonic <= kHarmonics; ++harmonic) {
      const double omega = kTwoPi * expectedFrequency * harmonic / 48000.0;
      const double coeff = 2.0 * std::cos(omega);
      double s0 = 0.0;
      double s1 = 0.0;
      double s2 = 0.0;
      for (std::size_t i = 0; i < windowFrames; ++i) {
        s0 = static_cast<double>(samples[start + i]) * window[i] + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
      }
      const double real = s1 - s2 * std::cos(omega);
      const double imaginary = s2 * std::sin(omega);
      power[harmonic] = (real * real + imaginary * imaginary) /
                        static_cast<double>(filled * filled);
    }
    result.fundamental = std::sqrt(power[1]);
    double harmonics = 0.0;
    for (int harmonic = 2; harmonic <= kHarmonics; ++harmonic) harmonics += power[harmonic];
    result.thd = harmonics > 0.0 ? std::sqrt(harmonics) / std::max(result.fundamental, 1.0e-20)
                                 : 0.0;
  }
  result.ok = true;
  return result;
}

double toDb(double value) {
  return value > 0.0 ? 20.0 * std::log10(value) : -240.0;
}

void usage() {
  std::printf(
      "usage: mnky_audio_probe list\n"
      "       mnky_audio_probe take DIR --backend pipewire|alsa [--output ID] [--input ID]\n"
      "              [--seconds N] [--pattern silence|ident|buses] [--amplitude A]\n"
      "              [--expect none|ident|buses] [--device-out 0,1,2,3,4,5]\n");
}

Options parseOptions(int argc, char** argv) {
  Options options;
  if (argc < 2) return options;
  options.command = argv[1];
  for (int i = 2; i < argc; ++i) {
    const std::string argument = argv[i];
    const auto value = [&]() -> std::string {
      return i + 1 < argc ? argv[++i] : std::string();
    };
    if (argument == "list") {
      continue;
    } else if (argument == "--backend") {
      options.backend = value();
    } else if (argument == "--output") {
      options.outputId = value();
    } else if (argument == "--input") {
      options.inputId = value();
    } else if (argument == "--seconds") {
      options.seconds = std::max(1, std::atoi(value().c_str()));
    } else if (argument == "--amplitude") {
      options.amplitude = std::atof(value().c_str());
    } else if (argument == "--pattern") {
      const std::string pattern = value();
      options.pattern = pattern == "ident" ? Pattern::Ident
                        : pattern == "buses" ? Pattern::Buses
                                             : Pattern::Silence;
    } else if (argument == "--expect") {
      const std::string pattern = value();
      options.expectSet = true;
      options.expect = pattern == "ident" ? Pattern::Ident
                      : pattern == "buses" ? Pattern::Buses
                                           : Pattern::Silence;
    } else if (argument == "--device-out") {
      int values[6] = {};
      const int parsed = std::sscanf(value().c_str(), "%d,%d,%d,%d,%d,%d", &values[0],
                                     &values[1], &values[2], &values[3], &values[4],
                                     &values[5]);
      if (parsed == 6) {
        options.deviceOut.main[0] = values[0];
        options.deviceOut.main[1] = values[1];
        options.deviceOut.out34[0] = values[2];
        options.deviceOut.out34[1] = values[3];
        options.deviceOut.phones[0] = values[4];
        options.deviceOut.phones[1] = values[5];
      }
    } else if (argument == "--help" || argument == "-h") {
      options.command = "help";
    } else if (!argument.empty() && argument[0] == '-') {
      std::printf("unknown option: %s\n", argument.c_str());
      options.command = "help";
    } else if (options.directory.empty()) {
      options.directory = argument;
    }
  }
  return options;
}

}  // namespace

int main(int argc, char** argv) {
  Options options = parseOptions(argc, argv);
  if (options.command == "help" || options.command.empty() ||
      (options.command == "take" && options.directory.empty())) {
    usage();
    return options.command == "help" ? 0 : 2;
  }
  if (options.command == "list") {
    printInventory();
    return 0;
  }
  if (options.command != "take") {
    usage();
    return 2;
  }
  if (!options.expectSet) options.expect = options.pattern;

  const DeviceApi api = apiFromName(options.backend);
  if (!deviceApiCompiled(api)) {
    std::printf("backend %s is not compiled into this build\n", options.backend.c_str());
    return 2;
  }
  const DeviceInventory inventory = enumerateDevices();
  const auto& outputs = inventory.outputs;
  const auto& inputs = inventory.inputs;
  const DeviceInfo* output = options.outputId.empty()
                                 ? nullptr
                                 : findDevice(outputs, options.outputId);
  const DeviceInfo* input = options.inputId.empty() ? nullptr : findDevice(inputs, options.inputId);
  if (!options.outputId.empty() && output == nullptr) {
    std::printf("output \"%s\" not found; run `mnky_audio_probe list`\n",
                options.outputId.c_str());
    return 2;
  }
  if (!options.inputId.empty() && input == nullptr) {
    std::printf("input \"%s\" not found; run `mnky_audio_probe list`\n",
                options.inputId.c_str());
    return 2;
  }
  if (output == nullptr) {
    // Widest output of the requested API: the multichannel interface.
    for (const auto& device : outputs) {
      if (device.api != api) continue;
      if (output == nullptr || device.channels > output->channels) output = &device;
    }
  }

  DeviceConfig config;
  config.sampleRate = 48000.0;
  config.blockFrames = kBlockSize;
  config.outputId = output != nullptr ? output->node : std::string();
  config.inputId = input != nullptr ? input->node : std::string();
  config.outputChannels = output != nullptr ? output->channels : 0;
  config.inputChannels = input != nullptr ? input->channels : 0;
  if (config.outputChannels <= 0) config.outputChannels = kMaxDeviceOutputs;
  if (input != nullptr && config.inputChannels <= 0) config.inputChannels = kMaxInputs;

  std::printf("probe: backend=%s output=%s input=%s pattern=%s amplitude=%.2f expect=%s\n",
              options.backend.c_str(), config.outputId.empty() ? "default" : config.outputId.c_str(),
              config.inputId.empty() ? "none" : config.inputId.c_str(),
              patternName(options.pattern), options.amplitude,
              patternName(options.expect));

  Engine engine;
  for (int strip = 0; strip < kMaxInputs; ++strip) {
    Channel channel;
    channel.gain = 1.0f;
    channel.pan = 0.5f;
    channel.arm = true;  // armed strips always monitor, like a recording take
    engine.setChannel(strip, channel);
    engine.setInputSource(strip, strip);
    engine.setOutputDestination(strip, OutputDestination::Main);
  }
  engine.setDeviceOut(options.deviceOut);
  Recorder recorder;
  ProbeClient client(options, engine, recorder);

  std::string error;
  auto backend = createBackend(api, &error);
  if (backend == nullptr) {
    std::printf("backend create failed: %s\n", error.c_str());
    return 2;
  }
  if (!backend->open(config, &client, &error)) {
    std::printf("open failed: %s\n", error.c_str());
    return 1;
  }
  // One bit per track: the probe records the device-input tracks 0..17.
  const std::uint64_t armMask = (std::uint64_t{1} << kMaxDeviceInputs) - 1;
  if (!recorder.startTake(options.directory, armMask, 48000, true)) {
    std::printf("recorder startTake failed for %s\n", options.directory.c_str());
    backend->close();
    return 1;
  }
  const auto started = std::chrono::steady_clock::now();
  if (!backend->start(&error)) {
    std::printf("start failed: %s\n", error.c_str());
    (void)recorder.stopTake();
    backend->close();
    return 1;
  }
  std::printf("opened: %s\n", backend->description().c_str());
  std::this_thread::sleep_for(std::chrono::seconds(options.seconds));
  backend->stop();
  const auto take = recorder.stopTake();
  const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
  std::printf("rendered frames=%llu captured frames=%llu xruns=%llu wall=%.2fs\n",
              static_cast<unsigned long long>(client.renderedFrames()),
              static_cast<unsigned long long>(client.capturedFrames()),
              static_cast<unsigned long long>(backend->xruns()), wall);
  const int channels = backend->outputChannels();
  const int inputChannels = backend->inputChannels();
  const int period = backend->periodFrames();
  const double rate = backend->sampleRate();
  backend->close();
  std::printf("negotiated: %d out / %d in, %.0f Hz, period %d frames, take %s (%d files, %llu frames)\n",
              channels, inputChannels, rate, period, take.path.c_str(), take.files,
              static_cast<unsigned long long>(take.frames));

  // Analyse channel 1..18 of the take. Recorded mono files are chNN.wav.
  const int analyseChannels = std::min(config.inputChannels > 0 ? config.inputChannels : kMaxInputs,
                                       kMaxInputs);
  int failures = 0;
  std::printf("%-4s %-26s %-26s %-10s %-10s %-10s %-10s\n", "ch", "expected", "measured", "peak dB",
              "rms dB", "dc", "thd dB");
  for (int channel = 0; channel < analyseChannels; ++channel) {
    // The Recorder writes one mono WAV per armed strip: ch00.wav..ch17.wav.
    char name[32];
    std::snprintf(name, sizeof name, "ch%02d.wav", channel);
    const std::string path = take.path + "/" + name;
    const Expectation expectation = expectationFor(options.expect, options.deviceOut, channel);
    const ChannelAnalysis analysis =
        analyseChannel(path, expectation.hasFrequency ? expectation.frequency : 0.0);
    if (!analysis.ok) {
      ++failures;
      std::printf("%-4d MISSING %s\n", channel + 1, path.c_str());
      continue;
    }
    char expectedText[64] = "-";
    if (options.expect == Pattern::Buses) {
      std::snprintf(expectedText, sizeof expectedText, "silent");
    }
    if (expectation.hasFrequency) {
      std::snprintf(expectedText, sizeof expectedText, "%7.1f Hz mag %.1f dB",
                    expectation.frequency, toDb(analysis.fundamental));
    }
    char measuredText[64] = "";
    if (expectation.hasFrequency) {
      std::snprintf(measuredText, sizeof measuredText, "peak %+.4f (%.1f dBFS)", analysis.peak,
                    toDb(analysis.peak));
    } else {
      std::snprintf(measuredText, sizeof measuredText, "peak %+.4f (%.1f dBFS)", analysis.peak,
                    toDb(analysis.peak));
    }
    std::printf("%-4d %-26s %-26s %-10.2f %-10.2f %-10.5f %-10.2f\n", channel + 1, expectedText,
                measuredText, toDb(analysis.peak), toDb(analysis.rms), analysis.dc,
                toDb(analysis.thd));
    if (options.expect == Pattern::Ident) {
      if (analysis.peak < 0.3) {
        std::printf("  ch%02d: expected a %.1f Hz sine, peak is too low\n", channel + 1,
                    expectation.frequency);
        ++failures;
      }
      if (analysis.thd > -80.0) {
        std::printf("  ch%02d: THD %.1f dB is worse than -80 dB\n", channel + 1,
                    toDb(analysis.thd));
        ++failures;
      }
      if (analysis.peak > 1.0 + 1.0e-6) {
        std::printf("  ch%02d: peak %.6f exceeds unity\n", channel + 1, analysis.peak);
        ++failures;
      }
    } else if (options.expect == Pattern::Buses) {
      if (expectation.expectedSilent) {
        if (analysis.peak > 0.01) {
          std::printf("  ch%02d: expected silence, peak %.6f\n", channel + 1, analysis.peak);
          ++failures;
        }
      } else {
        if (analysis.peak < 0.3) {
          std::printf("  ch%02d: expected a %.1f Hz sine, peak is too low\n", channel + 1,
                      expectation.frequency);
          ++failures;
        }
        if (analysis.thd > -80.0) {
          std::printf("  ch%02d: THD %.1f dB is worse than -80 dB\n", channel + 1,
                      toDb(analysis.thd));
          ++failures;
        }
      }
    }
  }
  const bool duplex = inputChannels > 0;
  if (duplex && client.renderedFrames() < static_cast<std::uint64_t>(rate) * 0.9 * options.seconds) {
    std::printf("rendered %llu frames in %d s: the render callback stalled\n",
                static_cast<unsigned long long>(client.renderedFrames()), options.seconds);
    ++failures;
  }
  std::printf("%s\n", failures == 0 ? "PASS audio probe" : "FAIL audio probe");
  return failures == 0 ? 0 : 1;
}
