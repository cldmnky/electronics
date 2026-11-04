#include "project_io.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <system_error>

#include "../../session/session.hpp"
#include "app_state.hpp"
#include "audio_glue.hpp"
#include "fx_slot.hpp"
#include "link_client.hpp"
#include "logic.hpp"
#include "midi_glue.hpp"
#include "mixer_model.hpp"

namespace mnky::app::project {
namespace {

void appendInt(std::string& out, const char* tag, long long value) {
  char buffer[48] = {};
  std::snprintf(buffer, sizeof buffer, "%s=%lld;", tag, value);
  out += buffer;
}

void appendFloat(std::string& out, const char* tag, double value) {
  char buffer[64] = {};
  std::snprintf(buffer, sizeof buffer, "%s=%.7g;", tag, value);
  out += buffer;
}

std::filesystem::path musicDirectory() {
  std::filesystem::path home;
  if (const char* env = std::getenv("HOME")) home = env;
  if (home.empty()) return std::filesystem::temp_directory_path();
  return home / "Music";
}

std::int64_t modifiedMs(const std::filesystem::path& file) {
  std::error_code error;
  const auto time = std::filesystem::last_write_time(file, error);
  if (error) return 0;
  // file_clock is not convertible to system_clock by a standard C++20 cast in
  // every libc++/libstdc++ version, so anchor it to "now" instead. The value is
  // used for ordering and display only.
  const auto fileNow = decltype(time)::clock::now();
  const auto systemNow = std::chrono::system_clock::now();
  const auto delta = time - fileNow;
  const auto systemTime =
      systemNow + std::chrono::duration_cast<std::chrono::system_clock::duration>(delta);
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             systemTime.time_since_epoch())
      .count();
}

// Session FxSlotTarget over the app's CLAP slot. Only plugins that are already
// loaded or resolvable through the default plugin path can be restored; an
// unavailable plugin leaves the slot untouched (the session core's tolerant
// load contract).
class SlotTarget final : public session::FxSlotTarget {
 public:
  SlotTarget(const AppContext& context, int index)
      : context_(context), slot_(context.fx.slot(index)) {}

  std::string effectId() const override { return slot_.pluginId(); }

  bool loadEffect(const std::string& id) override {
    if (slot_.pluginId() == id) return true;
    const auto candidate = FxSlot::defaultPluginPath();
    if (candidate.empty()) return false;
    if (!slot_.load(candidate, context_.audio.sampleRate(), audio::kBlockSize)) return false;
    if (slot_.pluginId() == id) return true;
    slot_.unload();
    return false;
  }

  void clearEffect() override { slot_.unload(); }

  std::size_t paramCount() const override { return slot_.paramCount(); }
  double paramValue(std::size_t index) const override { return slot_.paramValue(index); }
  bool setParamValue(std::size_t index, double value) override {
    return slot_.setParamValue(index, value);
  }
  bool bypassed() const override { return slot_.bypassed(); }
  void setBypassed(bool bypass) override { slot_.setBypassed(bypass); }

 private:
  const AppContext& context_;
  FxSlot& slot_;
};

struct SlotTargets {
  explicit SlotTargets(const AppContext& context)
      : first(context, 0), second(context, 1), pointers{&first, &second} {}

  SlotTarget first;
  SlotTarget second;
  std::array<session::FxSlotTarget*, session::kFxBuses> pointers;
};

session::Snapshot captureSnapshot(const AppContext& context) {
  // Capture the mixer through a scratch Engine filled from the atomics the UI
  // writes, so the audio thread's Engine is never read from the GUI thread.
  audio::Engine scratch;
  context.mixer.applyTo(scratch);
  SlotTargets targets(context);
  auto snapshot = session::capture(context.midi.routerSnapshot(), scratch, targets.pointers);
  snapshot.transport.bpmX100 = context.state.tempoX100.load();
  snapshot.click.enabled = context.state.clickEnabled.load();
  snapshot.click.level = context.state.clickLevel.load();
  snapshot.click.bars = context.state.clickBars.load();
  for (const auto& binding : context.midi.learnBindings()) {
    session::MidiMapEntry entry;
    entry.cc = binding.cc;
    entry.ch = binding.ch;
    entry.target = binding.target;
    entry.track = binding.track;
    snapshot.midimap.push_back(entry);
  }
  return snapshot;
}

void applyMixer(const AppContext& context, const session::MixerState& mixer) {
  for (int index = 0; index < audio::kMaxInputs; ++index) {
    const auto& channel = mixer.channels[static_cast<std::size_t>(index)];
    context.mixer.setGainDb(index, gainToDb(channel.gain));
    context.mixer.setPan(index, channel.pan);
    context.mixer.setMute(index, channel.mute);
    context.mixer.setSolo(index, channel.solo);
    context.mixer.setArm(index, channel.arm);
    for (int send = 0; send < audio::kNumSends; ++send) {
      context.mixer.setSend(index, send, channel.send[static_cast<std::size_t>(send)]);
    }
  }
  context.mixer.setMasterDb(gainToDb(mixer.master));
}

}  // namespace

std::filesystem::path root() {
  auto directory = musicDirectory() / "mnkybox-projects";
  std::error_code error;
  std::filesystem::create_directories(directory, error);
  return directory;
}

std::vector<Entry> list() {
  std::vector<Entry> entries;
  std::error_code error;
  const auto rootDirectory = root();
  for (std::filesystem::directory_iterator it(rootDirectory, error), end; it != end;
       it.increment(error)) {
    if (error) break;
    std::error_code typeError;
    if (!it->is_directory(typeError)) continue;
    const auto file = it->path() / kProjectFileName;
    std::error_code fileError;
    if (!std::filesystem::is_regular_file(file, fileError)) continue;
    Entry entry;
    entry.name = it->path().filename().string();
    entry.directory = it->path();
    entry.modifiedMs = modifiedMs(file);
    entries.push_back(entry);
  }
  std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
    if (a.modifiedMs != b.modifiedMs) return a.modifiedMs > b.modifiedMs;
    return a.name > b.name;
  });
  return entries;
}

bool save(const AppContext& context, const std::filesystem::path& directory,
          std::string& error) {
  error.clear();
  std::error_code createError;
  std::filesystem::create_directories(directory, createError);
  if (createError) {
    error = "cannot create " + directory.string();
    return false;
  }
  const auto file = directory / kProjectFileName;
  const auto snapshot = captureSnapshot(context);
  if (!session::save(file.string(), snapshot)) {
    error = "cannot write " + file.string();
    return false;
  }
  return true;
}

bool load(const AppContext& context, const std::filesystem::path& directory,
          std::string& error) {
  error.clear();
  const auto file = directory / kProjectFileName;
  const auto result = session::load(file.string());
  if (!result.ok) {
    error = result.error;
    return false;
  }

  // Apply into a router copy and a scratch Engine: the guarded MidiGlue swap and
  // the MixerModel sync below are what make the load visible to the audio/MIDI
  // threads, never a half-written live object.
  auto router = context.midi.routerSnapshot();
  audio::Engine scratch;
  SlotTargets targets(context);
  session::apply(result.snapshot, router, scratch, targets.pointers);
  context.midi.replaceRouter(router);
  applyMixer(context, result.snapshot.mixer);

  std::vector<midi::LearnBinding> bindings;
  for (const auto& entry : result.snapshot.midimap) {
    bindings.push_back(
        midi::LearnBinding{entry.cc, entry.ch, entry.target, entry.track});
  }
  context.midi.setLearnBindings(bindings);

  const int tempo = clampTempoX100(result.snapshot.transport.bpmX100);
  context.state.tempoX100.store(tempo);
  context.link.requestTempo(static_cast<std::uint16_t>(tempo));
  context.state.clickEnabled.store(result.snapshot.click.enabled);
  context.state.clickLevel.store(result.snapshot.click.level);
  context.state.clickBars.store(result.snapshot.click.bars);
  return true;
}

std::string fingerprint(const AppContext& context) {
  std::string out;
  out.reserve(4096);

  const auto router = context.midi.routerSnapshot();
  for (int source = 0; source < midi::kPortCount; ++source) {
    for (int destination = 0; destination < midi::kPortCount; ++destination) {
      if (router.routeEnabled(static_cast<midi::Port>(source),
                              static_cast<midi::Port>(destination))) {
        appendInt(out, "r", source * 16 + destination);
      }
    }
    appendInt(out, "c", router.channelMask(static_cast<midi::Port>(source)));
    appendInt(out, "t", router.transpose(static_cast<midi::Port>(source)));
  }

  for (int index = 0; index < audio::kMaxInputs; ++index) {
    appendFloat(out, "g", context.mixer.gainDb(index));
    appendFloat(out, "p", context.mixer.pan(index));
    appendInt(out, "m", context.mixer.mute(index) ? 1 : 0);
    appendInt(out, "s", context.mixer.solo(index) ? 1 : 0);
    appendInt(out, "a", context.mixer.arm(index) ? 1 : 0);
    for (int send = 0; send < audio::kNumSends; ++send) {
      appendFloat(out, "x", context.mixer.send(index, send));
    }
  }
  appendFloat(out, "mg", context.mixer.masterDb());
  for (int slot = 0; slot < session::kFxBuses; ++slot) {
    auto& fx = context.fx.slot(slot);
    out += "fx=";
    out += fx.pluginPath();
    appendInt(out, "by", fx.bypassed() ? 1 : 0);
    const auto count = fx.paramCount();
    for (std::size_t param = 0; param < count; ++param) {
      appendFloat(out, "pa", fx.paramValue(param));
    }
  }

  appendInt(out, "bpm", context.state.tempoX100.load());
  appendInt(out, "ce", context.state.clickEnabled.load() ? 1 : 0);
  appendFloat(out, "cl", context.state.clickLevel.load());
  appendInt(out, "cb", context.state.clickBars.load());
  for (const auto& binding : context.midi.learnBindings()) {
    appendInt(out, "lc", binding.cc);
    appendInt(out, "lh", binding.ch);
    out += "lt=" + binding.target + ";";
  }
  return out;
}

}  // namespace mnky::app::project
