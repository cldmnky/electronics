#include "project_io.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "../../session/session.hpp"
#include "app_state.hpp"
#include "audio_glue.hpp"
#include "eq_model.hpp"
#include "fx_slot.hpp"
#include "link_client.hpp"
#include "logging.hpp"
#include "logic.hpp"
#include "midi_glue.hpp"
#include "mixer_model.hpp"
#include "performance_model.hpp"
#include "session_settings.hpp"

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
  // The session core captures only the fixed 3x3 ports. Replace that block with
  // the dynamic-aware snapshot: every per-device route (ids 6..31), all 32
  // channel masks/transposes and the identity map that keeps those routes bound
  // across replug order. `devices` includes dormant reservations, so an
  // unplugged device's routes survive a save too.
  snapshot.router = context.midi.captureSessionRouter();
  // EQ bands and X/Y performance positions live in the session fx section
  // (v2: Snapshot::fx.eq / fx.xy); they are app-owned, so they are filled here
  // rather than by session::capture().
  snapshot.fx.eq = context.eq.capture();
  snapshot.fx.xy = context.performance.capture();
  snapshot.transport.bpmX100 = context.state.tempoX100.load();
  snapshot.click.enabled = context.state.clickEnabled.load();
  snapshot.click.level = context.state.clickLevel.load();
  snapshot.click.bars = context.state.clickBars.load();
  snapshot.click.mode = context.state.clickMode.load();
  snapshot.click.output = context.state.clickOutput.load();
  snapshot.click.gainDb = context.state.clickGainDb.load();
  snapshot.punch = context.settings.punch;
  snapshot.cue = context.settings.cue;
  snapshot.sends = context.settings.sends;
  snapshot.mainComp = context.settings.mainComp;
  snapshot.isTemplate = context.settings.isTemplate;
  snapshot.mixer.out34Mode = context.mixer.out34Mode();
  // Copy by the session store's own width, never a literal: the mixer model
  // supports audio::kMaxTracks slots while the session store width follows the
  // session wave. Iterating its size means a widening needs no change here and
  // a narrower store is never over-run.
  const std::size_t sessionTrackSlots = snapshot.mixer.channels.size();
  for (std::size_t channel = 0; channel < sessionTrackSlots; ++channel) {
    const int track = static_cast<int>(channel);
    auto& out = snapshot.mixer.channels[channel];
    out.cueSend = context.mixer.cueSend(track);
    out.out34Send = context.mixer.out34Send(track);
    out.inCh = context.mixer.inCh(track);
    out.inPair = context.mixer.inPair(track);
    out.outDst = context.mixer.outDst(track);
    const auto insert = context.mixer.insert(track);
    out.insert.id = insert.id;
    out.insert.bypass = insert.bypass;
    out.insert.params = insert.params;
    if (channel < snapshot.takes.active.size()) {
      snapshot.takes.active[channel] = context.settings.activeTake(track);
    }
    if (channel < snapshot.takes.loop.size()) {
      snapshot.takes.loop[channel] = context.settings.takeLoop[channel];
    }
    if (channel < snapshot.takes.edit.size()) {
      snapshot.takes.edit[channel] = context.settings.takeEdit[channel];
    }
  }
  // Dynamic track list (session v2 mixer.tracks): the ordered slots the mixer
  // shows and their names. The per-slot configuration above is saved for every
  // slot, so a removed track's settings and takes survive in the project and a
  // re-add restores them.
  snapshot.mixer.tracks.clear();
  for (int position = 0; position < context.mixer.trackCount(); ++position) {
    const auto* track = context.mixer.trackAt(position);
    if (track == nullptr) continue;
    session::MixerState::TrackEntry entry;
    entry.slot = track->slot;
    entry.name = track->name;
    snapshot.mixer.tracks.push_back(entry);
  }
  snapshot.clock = context.settings.clock;
  // Device output routing (session v2 deviceOut): Main/OUT3&4/phones channel
  // pairs, chosen on the System screen. The engine half lands in
  // audio::Engine::setDeviceOut; the AudioGlue handoff is still missing, so the
  // pairs are carried by the session and not yet applied per block.
  snapshot.deviceOut = context.settings.deviceOut;
  // MIDI FX pipe chains are app-owned in MidiGlue. captureSessionRouter()
  // already attaches the chain of every enabled route; this pass adds a route
  // entry for a chain whose route is off (the session format can carry it; the
  // matrix on/off stays authoritative) and covers dormant per-device ids.
  for (int source = 0; source < midi::kMaxPorts; ++source) {
    for (int destination = 0; destination < midi::kMaxPorts; ++destination) {
      const auto from = static_cast<midi::Port>(source);
      const auto to = static_cast<midi::Port>(destination);
      if (!midi::isInput(from) || !midi::isOutput(to)) continue;
      auto pipes = context.midi.routePipes(from, to);
      if (pipes.empty()) continue;
      session::RouteState* target = nullptr;
      for (auto& route : snapshot.router.routes) {
        if (route.src == source && route.dst == destination) {
          target = &route;
          break;
        }
      }
      if (target == nullptr) {
        session::RouteState route;
        route.src = source;
        route.dst = destination;
        route.on = context.midi.routeEnabled(from, to);
        snapshot.router.routes.push_back(std::move(route));
        target = &snapshot.router.routes.back();
      }
      target->pipes = std::move(pipes);
    }
  }
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
  // The loaded store is the session's width; every index it carries is valid
  // for the mixer model (which is at least as wide).
  for (std::size_t index = 0; index < mixer.channels.size(); ++index) {
    const auto& channel = mixer.channels[index];
    context.mixer.setGainDb(index, gainToDb(channel.gain));
    // Session v2 has one gain field; the app-side trim is reset so the loaded
    // combined gain is not applied twice.
    context.mixer.setTrimDb(index, 0.0f);
    context.mixer.setPan(index, channel.pan);
    context.mixer.setMute(index, channel.mute);
    context.mixer.setSolo(index, channel.solo);
    context.mixer.setArm(index, channel.arm);
    for (int send = 0; send < audio::kNumSends; ++send) {
      context.mixer.setSend(index, send, channel.send[static_cast<std::size_t>(send)]);
    }
    context.mixer.setCueSend(index, channel.cueSend);
    context.mixer.setOut34Send(index, channel.out34Send);
    context.mixer.setMonitor(index, channel.monitor);
    context.mixer.setInCh(index, channel.inCh);
    context.mixer.setInPair(index, channel.inPair);
    context.mixer.setOutDst(index, channel.outDst);
    MixerModel::Insert insert;
    insert.id = channel.insert.id;
    insert.bypass = channel.insert.bypass;
    insert.params = channel.insert.params;
    context.mixer.setInsert(index, std::move(insert));
  }
  // The session reader defaults an absent "tracks" key to every slot (legacy
  // all-18); an empty vector here is therefore a genuinely empty project, not a
  // legacy one.
  std::vector<MixerModel::Track> tracks;
  tracks.reserve(mixer.tracks.size());
  for (const auto& entry : mixer.tracks) {
    tracks.push_back(MixerModel::Track{entry.slot, entry.name});
  }
  context.mixer.setTracks(std::move(tracks));
  context.mixer.setMasterDb(gainToDb(mixer.master));
  context.mixer.setOut34Mode(mixer.out34Mode);
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
  // Fail closed on a track slot the session store cannot carry: the reader
  // rejects an out-of-range slot, so writing the file would create a project
  // that cannot be loaded. This is a capacity seam, not a policy: when the
  // session store widens to audio::kMaxTracks the guard follows it.
  const auto snapshot = captureSnapshot(context);
  const std::size_t sessionTrackSlots = snapshot.mixer.channels.size();
  for (const auto& track : snapshot.mixer.tracks) {
    if (track.slot >= 0 && static_cast<std::size_t>(track.slot) < sessionTrackSlots) {
      continue;
    }
    error = "project: track slot " + std::to_string(track.slot + 1) +
            " is beyond the session store (" + std::to_string(sessionTrackSlots) +
            " slots); the session capacity widening is pending";
    return false;
  }
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

  // Apply the engine half into a scratch Engine and a throwaway Router; the
  // guarded MidiGlue load and the MixerModel sync below are what make the
  // router/mixer visible to the audio/MIDI threads, never a half-written live
  // object.
  audio::Engine scratch;
  SlotTargets targets(context);
  // The session core only knows the fixed 3x3 matrix; apply the engine half
  // through a throwaway Router, then hand the router block to the dynamic-aware
  // MidiGlue load: it binds per-device ids by identity, applies all 32 routes,
  // masks, transposes and pipe chains, and re-pushes every TRS->TRS chain to the
  // Pico (0x18/0x19) including an empty one, so no stale chain survives.
  auto scratchRouter = context.midi.routerSnapshot();
  session::apply(result.snapshot, scratchRouter, scratch, targets.pointers);
  context.midi.applySessionRouter(result.snapshot.router);
  applyMixer(context, result.snapshot.mixer);
  const int normalizedEqQ = context.eq.apply(result.snapshot.fx.eq);
  if (normalizedEqQ > 0) {
    // Session/engine Q can reach 24; the EQ screen and its CLAP effect stop at
    // 10, so name the normalization instead of silently returning a lower Q.
    log(std::string("eq: normalized ") + std::to_string(normalizedEqQ) +
        " loaded band(s) with Q > 10.0 to 10.0 (UI/CLAP range; engine accepts 24)");
  }
  context.performance.apply(result.snapshot.fx.xy);

  std::vector<midi::LearnBinding> bindings;
  for (const auto& entry : result.snapshot.midimap) {
    bindings.push_back(
        midi::LearnBinding{entry.cc, entry.ch, entry.target, entry.track});
  }
  context.midi.setLearnBindings(bindings);

  const int tempo = clampTempoX100(result.snapshot.transport.bpmX100);
  context.state.tempoX100.store(tempo);
  context.link.requestTempo(static_cast<std::uint16_t>(tempo));
  const auto& click = result.snapshot.click;
  context.state.clickMode.store(std::clamp(click.mode, 0, 2));
  context.state.clickEnabled.store(click.mode != 0);
  context.state.clickOutput.store(std::clamp(click.output, 0, 2));
  context.state.clickGainDb.store(std::clamp(click.gainDb, -96.0f, 20.0f));
  context.state.clickLevel.store(
      std::clamp(dbToGain(std::clamp(click.gainDb, -96.0f, 20.0f)), 0.0f, 1.0f));
  context.state.clickBars.store(click.bars);
  context.settings.punch = result.snapshot.punch;
  context.settings.cue = result.snapshot.cue;
  context.settings.sends = result.snapshot.sends;
  context.settings.mainComp = result.snapshot.mainComp;
  context.settings.isTemplate = result.snapshot.isTemplate;
  // The session store width is the snapshot's own; a narrower store leaves the
  // app-side slots above it at their defaults (they are not persisted yet).
  for (std::size_t channel = 0; channel < result.snapshot.takes.active.size(); ++channel) {
    const int track = static_cast<int>(channel);
    context.settings.setActiveTake(track, result.snapshot.takes.active[channel]);
    if (channel < result.snapshot.takes.loop.size()) {
      context.settings.takeLoop[channel] = result.snapshot.takes.loop[channel];
    }
    if (channel < result.snapshot.takes.edit.size()) {
      context.settings.takeEdit[channel] = result.snapshot.takes.edit[channel];
    }
  }
  context.settings.clock = result.snapshot.clock;
  context.settings.deviceOut = result.snapshot.deviceOut;
  context.audio.applyTakeLoops(context.settings);
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

  // MIDI FX pipe chains: type, bypass and every stored positional parameter.
  for (int source = 0; source < midi::kPortCount; ++source) {
    for (int destination = 0; destination < midi::kPortCount; ++destination) {
      const auto from = static_cast<midi::Port>(source);
      const auto to = static_cast<midi::Port>(destination);
      if (!midi::isInput(from) || !midi::isOutput(to)) continue;
      for (const auto& pipe : context.midi.routePipes(from, to)) {
        appendInt(out, "pfx", (source * 3 + destination) * 100 +
                                 static_cast<int>(pipe.type));
        appendInt(out, "pfb", pipe.bypass ? 1 : 0);
        for (const int param : pipe.params) appendInt(out, "pfp", param);
      }
    }
  }

  for (int index = 0; index < audio::kMaxTracks; ++index) {
    appendFloat(out, "g", context.mixer.gainDb(index));
    appendFloat(out, "tr", context.mixer.trimDb(index));
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

  // EQ: every band of the EQ store's tracks plus Main (type, gain, frequency,
  // Q). The store's own width keeps this correct when it widens.
  const auto eq = context.eq.capture();
  const int eqTracks = static_cast<int>(eq.tracks.size());
  for (int track = 0; track <= eqTracks; ++track) {
    for (int band = 0; band < 4; ++band) {
      const auto& entry = track >= eqTracks
                              ? eq.main[static_cast<std::size_t>(band)]
                              : eq.tracks[static_cast<std::size_t>(track)]
                                         [static_cast<std::size_t>(band)];
      appendInt(out, "eqt", entry.type);
      appendFloat(out, "eqg", entry.gainDb);
      appendFloat(out, "eqf", entry.freqHz);
      appendFloat(out, "eqq", entry.q);
    }
  }
  // X/Y performance positions. The reverb freeze macro is a gesture, not a
  // setting, so it is deliberately not in the fingerprint (it cannot be saved).
  const auto xy = context.performance.capture();
  appendFloat(out, "xydx", xy.delayX);
  appendFloat(out, "xydy", xy.delayY);
  appendFloat(out, "xyrx", xy.reverbX);
  appendFloat(out, "xyry", xy.reverbY);

  appendInt(out, "bpm", context.state.tempoX100.load());
  appendInt(out, "ce", context.state.clickEnabled.load() ? 1 : 0);
  appendFloat(out, "cl", context.state.clickLevel.load());
  appendInt(out, "cb", context.state.clickBars.load());
  appendInt(out, "cm", context.state.clickMode.load());
  appendInt(out, "co", context.state.clickOutput.load());
  appendFloat(out, "cg", context.state.clickGainDb.load());
  appendInt(out, "o34", context.mixer.out34Mode());
  for (int index = 0; index < audio::kMaxTracks; ++index) {
    appendFloat(out, "cue", context.mixer.cueSend(index));
    appendFloat(out, "o34s", context.mixer.out34Send(index));
    appendInt(out, "mon", context.mixer.monitor(index));
    out += "ta=" + context.settings.activeTake(index) + ";";
    appendInt(out, "in", context.mixer.inCh(index));
    appendInt(out, "ip", context.mixer.inPair(index) ? 1 : 0);
    appendInt(out, "od", context.mixer.outDst(index));
    const auto& loop = context.settings.takeLoop[static_cast<std::size_t>(index)];
    appendInt(out, "le", loop.enabled ? 1 : 0);
    appendFloat(out, "ls", loop.startBeats);
    appendFloat(out, "lx", loop.endBeats);
    // Non-destructive region edit (session v2 takes.edit): every field of every
    // ordered region, so the dirty dot covers an op and a project round-trips it.
    for (const auto& region : context.settings.takeEdit[static_cast<std::size_t>(index)]) {
      out += "re=" + region.take + ";";
      appendFloat(out, "ro", region.offsetBeats);
      appendFloat(out, "rl", region.lengthBeats);
      appendFloat(out, "rg", region.gainDb);
      appendFloat(out, "rfi", region.fadeInBeats);
      appendFloat(out, "rfo", region.fadeOutBeats);
      appendInt(out, "rn", region.normalize ? 1 : 0);
    }
  }
  appendInt(out, "ck1", context.settings.clock.trs[0].clock ? 1 : 0);
  appendInt(out, "ct1", context.settings.clock.trs[0].transport ? 1 : 0);
  appendInt(out, "ck2", context.settings.clock.trs[1].clock ? 1 : 0);
  appendInt(out, "ct2", context.settings.clock.trs[1].transport ? 1 : 0);
  appendInt(out, "cku", context.settings.clock.usb.clock ? 1 : 0);
  appendInt(out, "ctu", context.settings.clock.usb.transport ? 1 : 0);
  for (const auto& pair : {context.settings.deviceOut.main, context.settings.deviceOut.out34,
                           context.settings.deviceOut.phones}) {
    appendInt(out, "dl", pair[0]);
    appendInt(out, "dr", pair[1]);
  }
  for (const auto& point : {context.settings.punch.playFrom, context.settings.punch.in,
                            context.settings.punch.out}) {
    appendInt(out, "pe", point.enabled ? 1 : 0);
    appendInt(out, "pm", point.measure);
  }
  appendInt(out, "ph", context.settings.cue.phonesSrc);
  appendInt(out, "cs", context.settings.cue.style);
  appendInt(out, "sc", context.settings.sends.cue);
  appendInt(out, "sf1", context.settings.sends.fx1);
  appendInt(out, "sf2", context.settings.sends.fx2);
  appendInt(out, "so", context.settings.sends.out34);
  appendInt(out, "sr", context.settings.sends.rec);
  appendInt(out, "mcon", context.settings.mainComp.on ? 1 : 0);
  appendFloat(out, "mct", context.settings.mainComp.threshDb);
  appendFloat(out, "mcr", context.settings.mainComp.ratio);
  appendFloat(out, "mca", context.settings.mainComp.attackMs);
  appendFloat(out, "mcrl", context.settings.mainComp.releaseMs);
  appendFloat(out, "mcm", context.settings.mainComp.makeupDb);
  appendInt(out, "mcmd", context.settings.mainComp.mode);
  appendInt(out, "tmpl", context.settings.isTemplate ? 1 : 0);
  for (const auto& binding : context.midi.learnBindings()) {
    appendInt(out, "lc", binding.cc);
    appendInt(out, "lh", binding.ch);
    out += "lt=" + binding.target + ";";
  }
  return out;
}

}  // namespace mnky::app::project
