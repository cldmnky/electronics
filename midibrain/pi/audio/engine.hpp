#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "capacity.hpp"
#include "click.hpp"
#include "eq_insert.hpp"
#include "take_player.hpp"

namespace mnky::audio {

// Capacity taxonomy (kMaxDeviceInputs / kMaxTracks / kMaxChannels /
// kMaxInputs) is documented in capacity.hpp: 18 device inputs, 64 mixer
// tracks, 128 internal channels (two per stereo track).

/// Legacy FX send-bus count. The buses are retired from the signal path (see
/// Engine::process); the constant, Channel::send and the setFx()/setFxReturn()
/// API stay for the app's remaining references. New work belongs in the
/// per-track FX rack.
inline constexpr int kNumSends = 2;

/// Per-track FX rack: two series slots, FX1 -> FX2.
inline constexpr int kTrackFxSlots = 2;
/// Fixed ALSA processing block.
inline constexpr int kBlockSize = 128;

/// Where Engine::tap() reads a strip. `PostGain` is the historical
/// `in * gain` tap; `PreGain` is the raw device input and ignores the strip's
/// gain.
enum class TapPoint : uint8_t { PreGain, PostGain };

/// Per-track input monitoring. `Off` never passes live input to Main/sends
/// (take playback is unaffected); `Auto` passes live input when the track is
/// armed or no take is currently playing; `On` always passes live input so a
/// take and the live source can be layered intentionally; `InputOnly` (the MPC
/// "just monitoring" mode) always passes live input and never plays the
/// track's take. The take stack keeps running while it is suppressed, so
/// switching back to `Off`/`Auto`/`On` resumes at the position it would have
/// reached anyway. Mute/solo always win.
enum class MonitorMode : uint8_t { Off = 0, Auto = 1, On = 2, InputOnly = 3 };

/// Send-source stage for a destination. `PreFader` applies Channel::gain but
/// not Channel::vol and is pre-pan (mono-centred); `PostFader` applies both and
/// follows pan. Mute/solo still gate every live send. The `Fx1`/`Fx2`
/// selections are stored but inert: those global buses are retired.
enum class SendSource : uint8_t { PreFader = 0, PostFader = 1 };

/// Per-destination source selectors. `Rec` is stored for the recorder tap; the
/// recorder itself still uses Channel::tapPoint in this wave.
enum class SendDestination : uint8_t {
    Cue = 0,
    Fx1 = 1,
    Fx2 = 2,
    Out34 = 3,
    Rec = 4,
};
inline constexpr int kNumSendDestinations = 5;

/// OUT3&4 output format. `Stereo` follows each track's pan; `DualMono` sums
/// each OUT3&4 send before pan and writes the same mono mix to both outputs.
enum class Out34Mode : uint8_t { Stereo = 0, DualMono = 1 };

/// Per-strip output destination: where the strip's dry (post-fader, post-pan)
/// signal lands. Sends (FX1/FX2, CUE, OUT3&4) and the recorder tap are
/// independent of it; mute/solo and monitor mode still gate everything.
/// `Phones` is the CUE bus: the engine has no separate phones bus, the jack
/// plays CUE or a Main duplicate (`PhonesSource`), so a Phones-routed strip is
/// heard at the jack only while the phones source is CUE.
enum class OutputDestination : uint8_t {
    Main = 0,    // Main 1&2: master gain, main compressor and a Main click
    Out34 = 1,   // OUT3&4: uncompressed, follows Out34Mode
    Phones = 2,  // the CUE bus (phones plays it while PhonesSource is Cue)
    None = 3,    // no dry destination; sends and taps keep working
};
inline constexpr int kNumOutputDestinations = 4;

/// `setInputSource(index, kNoInputSource)` selects silence: the strip has no
/// live input at all, while its take voice and destination routing stay live.
inline constexpr int kNoInputSource = -1;

/// Phones monitor source: the CUE bus or a duplicate of Main 1&2 (including
/// the main compressor). OUT3&4 is never compressed.
enum class PhonesSource : uint8_t { Cue = 0, Main = 1 };

/// CUE send interpretation: per-track send level or a >0/0 toggle at unity.
enum class CueStyle : uint8_t { Level = 0, Toggle = 1 };

/// Main-bus detector. `Peak` is the instantaneous stereo-linked absolute peak;
/// `Rms` is sqrt of a 10 ms one-pole mean square (also stereo-linked).
enum class CompressorMode : uint8_t { Peak = 0, Rms = 1 };

/// Metronome gating. `RecordOnly` needs both a rolling transport and
/// Engine::setRecording(true); `OnPlay` needs a rolling transport.
enum class ClickMode : uint8_t { Off = 0, RecordOnly = 1, OnPlay = 2 };

/// Where the click is injected. Main click is added after the main compressor
/// so the metronome does not pump; OUT3&4 is never compressed.
enum class ClickOutput : uint8_t { Main = 0, Phones = 1, Out34 = 2 };

/// Main-bus compressor settings. Ranges match session v2:
/// threshDb -48..0, ratio 1..100, attackMs/releaseMs 0..1000,
/// makeupDb -36..+36.
struct MainCompSettings {
    bool on = false;
    float threshDb = -12.0f;
    float ratio = 2.0f;
    float attackMs = 10.0f;
    float releaseMs = 100.0f;
    float makeupDb = 0.0f;
    CompressorMode mode = CompressorMode::Peak;
    friend bool operator==(const MainCompSettings&,
                           const MainCompSettings&) = default;
};

/// Musical transport position exposed for punch-in/out decisions. `frame` is
/// the session sample frame derived from `tick`, the app tempo and the engine
/// sample rate. Measures and beats are 1-based; `beat` is the beat within the
/// 4/4 bar. `valid` is false when ticksPerBeat is 0.
struct TransportPosition {
    bool valid = false;
    bool rolling = false;
    std::uint64_t frame = 0;
    std::uint32_t tick = 0;
    std::uint32_t ticksPerBeat = 0;
    std::uint32_t tickInBeat = 0;
    std::uint32_t beat = 1;
    std::uint32_t measure = 1;
    int bpmX100 = 12000;
};

/// Physical device output mapping (session v2 `deviceOut`): each engine bus
/// pair is written to two 0-based device output channels, defaults Main = 0/1,
/// OUT3&4 = 2/3, phones = 4/5. The device backend (not the engine) applies the
/// map, so the engine only stores the selection; values are clamped to 0..31 and
/// targets beyond the opened device width are dropped there. DeviceOut lives
/// here because session::capture()/apply() forward it with the mixer state.
struct DeviceOut {
    int main[2] = {0, 1};
    int out34[2] = {2, 3};
    int phones[2] = {4, 5};
    friend bool operator==(const DeviceOut&, const DeviceOut&) = default;
};

/// Optional output buses for the extended process() call. A null pointer
/// leaves that side unwritten; Main is always the historical outL/outR pair.
struct OutputBuses {
    float* mainL = nullptr;
    float* mainR = nullptr;
    float* phonesL = nullptr;
    float* phonesR = nullptr;
    float* out34L = nullptr;
    float* out34R = nullptr;
};

/// Mixer strip for one input. `gain` is a linear pre-pan gain, `vol` is the
/// channel fader applied after `gain`; pre-fader sends use `gain` only and
/// post-fader sends use both. `pan` runs from 0 (hard left) through 0.5
/// (centre) to 1 (hard right) with a constant-power law. `send[s]` is the
/// retired FX bus level: it is stored but no longer reaches a bus; the
/// per-track FX rack (TrackFxSlot) is the FX path. `cueSend` feeds the
/// CUE/phones bus and `out34Send` feeds OUT3&4. `tapPoint` picks the
/// recorder/monitor tap; the default keeps the post-gain behavior. `monitor`
/// gates the live input contribution to Main/sends and, in InputOnly, the take
/// voice too; the recorder tap is independent of it.
struct Channel {
    float gain = 1.0f;
    float vol = 1.0f;
    float pan = 0.5f;
    bool mute = false;
    bool solo = false;
    bool arm = false;
    MonitorMode monitor = MonitorMode::Auto;
    float send[kNumSends] = {0.0f, 0.0f};
    float cueSend = 0.0f;
    float out34Send = 0.0f;
    TapPoint tapPoint = TapPoint::PostGain;
};

/// Stereo in-place FX unit hosted on a send bus. `ch` holds `nch` block pointers
/// of `n` frames each; the engine passes a 2-channel bus.
struct IEffect {
    virtual ~IEffect() = default;
    virtual void process(float* const* ch, int nch, int n) = 0;
    virtual const char* name() const = 0;
    /// Control-thread sample-rate notice; default ignores (stateless effects).
    /// Time-based effects re-allocate here. Engine forwards on setSampleRate.
    virtual void setSampleRate(double) {}
};

/// One per-track FX rack slot (session v2 `mixer.channels[].fxSlots[]`).
/// `id` is empty (no effect), a built-in id (`builtin.delay`,
/// `builtin.reverb`) or a CLAP plugin id; `params` are positional doubles in
/// the effect's stable order (delay 8, reverb 6). Missing entries keep the
/// current value (defaults after an id change), extra entries are ignored and
/// the built-in effects clamp their own ranges. `bypass` is a hard dry switch.
/// `wet` is the slot's dry/wet crossfade, 0..1: the slot output is
/// `(1 - wet) * dry + wet * processed`, so wet = 0 skips the DSP entirely
/// (bit-transparent) and wet = 1 is the processed signal. A non-built-in id
/// runs the IEffect attached with setTrackFxSlotEffect() and passes the strip
/// dry when nothing is attached.
struct TrackFxSlot {
    std::string id;
    bool bypass = false;
    float wet = 1.0f;
    std::vector<double> params;
    friend bool operator==(const TrackFxSlot&, const TrackFxSlot&) = default;
};

/// Legacy single-insert view of FX slot 0 (session v2
/// `mixer.channels[].insert`). Kept for the app/session code that has not
/// moved to the rack yet: setTrackInsert() writes slot 0 with wet = 1 and
/// trackInsert() reads slot 0 back. `wet` has no legacy field; a slot-0 wet
/// other than 1 is not representable through this view.
struct TrackInsert {
    std::string id;
    bool bypass = false;
    std::vector<double> params;
    friend bool operator==(const TrackInsert&, const TrackInsert&) = default;
};

/// One track's two FX slots, each with lazily created built-in delay/reverb
/// instances plus an external effect. Defined in engine.cpp; the engine
/// creates a rack on the first configured slot and keeps it until destruction,
/// so process() only ever loads a pointer.
class TrackFxRack;

/// Mixer core: gain/vol/pan, per-strip and main four-band EQ, mute/solo,
/// pre/post sends, FX returns, main compressor, click and monitor outputs.
/// Everything runs in the caller's (audio) thread and never allocates.
class Engine {
public:
    Engine();
    ~Engine();

    /// `n` must be <= kBlockSize. `in` holds `nch` (<= kMaxDeviceInputs)
    /// device channel pointers; missing or null channels are silent.
    /// `outL`/`outR` receive the
    /// main-bus result. This historical call writes only Main 1&2; the
    /// extended overload below also produces Phones and OUT3&4.
    void process(const float* const* in, int nch, int n, float* outL,
                 float* outR);
    /// Extended output routing. Each non-null pair is written; Main receives
    /// the compressor and a Main-routed click, Phones is either the CUE bus or
    /// a compressed Main duplicate, and OUT3&4 is the uncompressed per-track
    /// OUT3&4 bus.
    void process(const float* const* in, int nch, int n,
                 const OutputBuses& outputs);

    /// Mono tap of one track at its Channel::tapPoint, valid until the next
    /// process() call: the raw input for PreGain, `in * gain` for the default
    /// PostGain. The tap is the track's *selected* live source and never take
    /// playback; a stereo track is averaged to mono (`0.5 * (L + R)`), because
    /// the recorder writes one mono file per armed track. nullptr when `track`
    /// is outside 0..kMaxTracks-1. Mute, solo, pan, vol, monitor, the output
    /// destination and master do not affect the tap.
    const float* tap(int ch) const;

    /// Out-of-range indices are ignored, so a UI can push all 18 strips blindly.
    /// Strip state only: the input source, the pair flag and the output
    /// destination are separate routing state (setInputSource/setInputPair/
    /// setOutputDestination) and a setChannel call leaves them untouched, so a
    /// blind strip refresh cannot remap inputs.
    void setChannel(int index, Channel ch);
    /// Live strip state (session capture); out-of-range returns a default strip.
    Channel channel(int index) const {
        return index >= 0 && index < kMaxTracks ? mChannels[index] : Channel{};
    }
    /// Per-track CUE send level, clamped to 0..1 (NaN -> 0).
    void setCueSend(int index, float level);
    float cueSend(int index) const;
    /// Per-track OUT3&4 send level, clamped to 0..1 (NaN -> 0).
    void setOut34Send(int index, float level);
    float out34Send(int index) const;
    /// Per-track live-input monitoring mode; out-of-range values map to Auto.
    void setMonitorMode(int index, MonitorMode mode);
    MonitorMode monitorMode(int index) const;

    // --- per-track input source / output destination -----------------------
    /// Select track `index`'s live input source: a device channel 0..17
    /// (higher values clamp) or kNoInputSource (-1) for silence. Tracks
    /// 0..kMaxDeviceInputs-1 default to identity (track N <- device N);
    /// tracks without a device input (18..63) default to kNoInputSource. The
    /// recorder tap carries the selected source instead of the track's own
    /// device index; the take voice, the pan and the output destination keep
    /// applying to the track, and a silent-source track keeps playing its own
    /// take voice (the monitor rule: silence is about live input only).
    void setInputSource(int index, int deviceChannel);
    /// Selected device channel, kNoInputSource for a silent strip or when
    /// `index` is out of range.
    int inputSource(int index) const;
    /// Bind the strip's source as a stereo pair: L is `inputSource()`, R is the
    /// next device channel. The strip stays one strip - one arm/monitor/take
    /// set, per the manual - and `pan` becomes a stereo balance instead of a
    /// constant-power pan. An odd source clamps down to the even channel below
    /// it when the block renders (never in the stored selection), so capture()
    /// returns exactly what was applied.
    void setInputPair(int index, bool paired);
    bool inputPair(int index) const;
    /// Where the strip's dry signal goes; out-of-range values select Main.
    void setOutputDestination(int index, OutputDestination destination);
    /// Out-of-range strips read as Main.
    OutputDestination outputDestination(int index) const;

    // --- physical device output map (session deviceOut) ---------------------
    /// Control-thread selection of which device output channels carry Main,
    /// OUT3&4 and the phones/CUE bus. Values are clamped to 0..31 (any sane
    /// multichannel interface); equal channels for two buses keep the first bus
    /// in the device map. Audio processing is unaffected, only the device frame
    /// mapping in DeviceBackend/DeviceOut helpers reads it.
    void setDeviceOut(const DeviceOut& deviceOut);
    /// Applied map; audio-thread safe (atomically loaded fields).
    DeviceOut deviceOut() const;

    /// Linear master gain; negative and NaN values are clamped to 0.
    void setMaster(float gain);
    /// Live master gain (session capture).
    float masterGain() const { return mMaster; }

    // --- four-band EQ inserts ---------------------------------------------
    /// Per-strip EQ band, post-gain and pre-fader. The recorder tap keeps its
    /// historical pre-EQ live signal, so a recording does not bake the strip
    /// EQ; every send stage and the dry destination do. `band` is 0..3 and an
    /// out-of-range track/band is ignored. Values are clamped to the session
    /// EQ contract: type 0..5, gain -24..+24 dB, frequency 20..20000 Hz,
    /// Q 0.1..24; non-finite values take the session defaults. Type 0 (None)
    /// bypasses the band; four None bands make the insert a bit-transparent
    /// no-op.
    void setTrackEqBand(int track, int band, const EqBand& value);
    /// Applied per-strip EQ band; out-of-range reads as the session default.
    EqBand trackEqBand(int track, int band) const;
    /// Main-bus EQ band. The main insert is post-compressor and post-click,
    /// the final Main 1&2 stage; Phones-as-Main carries it, OUT3&4 and CUE do
    /// not. Same clamps and None rule as the per-strip insert.
    void setMainEqBand(int band, const EqBand& value);
    EqBand mainEqBand(int band) const;

    // --- per-track FX rack (two series slots) -----------------------------
    /// One rack per track, placed post-EQ, pre-fader and pre-sends (the old
    /// per-track insert home; see NOTES.md, "Engine wave 11"). The slots run
    /// in series, slot 0 then slot 1, each with its own dry/wet crossfade. An
    /// empty id detaches a slot: no DSP runs, the block costs one atomic
    /// pointer load plus one kind load, and the wet value is kept (the UI mix
    /// knob and the legacy send-level migration store it before an effect is
    /// assigned). A built-in id switches that slot's instance (an id change
    /// re-initializes only that slot: DSP state cleared, parameters back to
    /// defaults, then `params` applied) while a
    /// same-id call applies the parameters hot through the effect's 20 ms
    /// ramps, with no reset. `bypass` and `wet == 0` are hard dry switches and
    /// leave the caller's buffers bit-exact. `wet` is clamped to 0..1 (NaN ->
    /// 1). The recorder tap is upstream of the rack, so a recording does not
    /// bake it. Out-of-range tracks/slots are ignored.
    void setTrackFxSlot(int track, int slot, const TrackFxSlot& fxSlot);
    /// Applied slot state (session capture); out-of-range reads as none.
    TrackFxSlot trackFxSlot(int track, int slot) const;
    /// CLAP attachment seam per slot, parallel to setFx(): `effect` runs while
    /// the slot's id is neither empty nor a built-in id. The engine does not
    /// load CLAP modules itself (dependency direction: mnky_clap links
    /// mnky_audio), so the app owns the host, loading, parameter mapping and
    /// lifetime; the engine forwards setSampleRate() and bypass, nothing else.
    void setTrackFxSlotEffect(int track, int slot, IEffect* effect);

    /// Legacy single-insert API: writes/reads FX slot 0 with wet = 1. Kept
    /// while the app and session move to setTrackFxSlot()/trackFxSlot().
    void setTrackInsert(int track, const TrackInsert& insert);
    TrackInsert trackInsert(int track) const;
    /// Legacy attachment seam for FX slot 0 (see setTrackFxSlotEffect).
    void setTrackInsertEffect(int track, IEffect* effect);

    /// Retired global FX send bus: the engine keeps the parameter and the
    /// attached-effect pointer so the app still compiles, but process() no
    /// longer feeds or returns the bus. Use the per-track FX rack.
    void setFx(int send, IEffect* fx);
    /// Control thread only. Forwards to attached FX (see IEffect), every EQ
    /// insert and every track FX slot instance; engine processing itself is
    /// rate-agnostic except for the main compressor, click, EQ biquads and
    /// 48 kHz-only take playback.
    void setSampleRate(double sampleRate);
    /// Retired global FX send bus return level (stored, not applied).
    void setFxReturn(int send, float gain);

    // --- routing / monitor configuration ----------------------------------
    void setSendSource(SendDestination destination, SendSource source);
    SendSource sendSource(SendDestination destination) const;
    void setOut34Mode(Out34Mode mode);
    Out34Mode out34Mode() const { return mOut34Mode; }
    void setPhonesSource(PhonesSource source);
    PhonesSource phonesSource() const { return mPhonesSource; }
    void setCueStyle(CueStyle style);
    CueStyle cueStyle() const { return mCueStyle; }

    // --- main-bus compressor ----------------------------------------------
    /// Values are clamped to the documented ranges; NaN falls back to the
    /// corresponding default.
    void setMainComp(const MainCompSettings& settings);
    const MainCompSettings& mainComp() const { return mMainComp; }

    // --- metronome --------------------------------------------------------
    void setClickMode(ClickMode mode);
    ClickMode clickMode() const { return mClickMode; }
    void setClickOutput(ClickOutput output);
    ClickOutput clickOutput() const { return mClickOutput; }
    /// -96..+20 dB; NaN falls back to 0 dB. The click is rendered at unity and
    /// this gain is applied afterwards, so positive gains are not clamped away.
    void setClickGainDb(float gainDb);
    float clickGainDb() const { return mClickGainDb; }
    /// Record transport state for ClickMode::RecordOnly. The app owns the
    /// recorder and must push this once per record start/stop.
    void setRecording(bool recording) { mRecording = recording; }
    bool recording() const { return mRecording; }

    // --- transport / punch position source --------------------------------
    /// `tick` is the app's absolute tick at the start of the next block.
    /// `frame` in the resulting position is derived from bpmX100, sample rate
    /// and ticksPerBeat. Punch decisions should be made in the audio callback:
    /// a measure boundary is observed at the next block start, i.e. up to one
    /// kBlockSize late (2.67 ms at 48 kHz).
    void setTransport(bool rolling, std::uint32_t tick,
                      std::uint32_t ticksPerBeat, int bpmX100 = 12000);
    const TransportPosition& transportPosition() const { return mTransport; }

    // --- take playback ----------------------------------------------------
    /// Control-thread take selection. Files must be 48 kHz WAV; a refusal
    /// leaves the previous selection untouched and writes a logged reason to
    /// `error`. Takes are ordered oldest-first in `paths`; the last one wins
    /// where ranges overlap. Playback starts at the current transport frame.
    bool setActiveTakes(int track, const std::vector<std::string>& paths,
                        std::string* error = nullptr);
    bool setActiveTake(int track, const std::string& path,
                       std::string* error = nullptr);
    void clearActiveTake(int track);
    int activeTakeCount(int track) const;
    /// Newest (highest-priority) take path, empty when the track has none.
    std::string activeTake(int track) const;
    /// Per-track take loop in session beats (control thread). Playback-only:
    /// an armed strip ignores the take, `tap()` stays live and punch-in is not
    /// coupled to looping. `endBeats <= startBeats` loops the whole active
    /// take. The window is mapped to take frames with the current transport
    /// tempo, so re-apply it after a tempo change; a loop that does not fit
    /// the take plays linearly. Out-of-range tracks are ignored.
    void setTakeLoop(int track, const TakeLoop& loop);
    TakeLoop takeLoop(int track) const {
        return trackInRange(track) ? mTakeLoop[track] : TakeLoop{};
    }
    /// Control-thread non-destructive region edit for `track`; an empty list
    /// clears it. While an edit is applied the track's take voice plays the
    /// ordered region list - offset/length in session beats mapped with the
    /// current tempo, per-region gain, linear fade in/out in beats, optional
    /// normalize - instead of the selected take stack, which is kept and
    /// resumes on clearTakeRegions(). Everything is resolved here at apply time
    /// (including the one normalize peak pass per region), so process() only
    /// copies frames that are already gained. Re-apply after a tempo change,
    /// exactly like setTakeLoop(). A non-48 kHz engine, an out-of-range track or
    /// an unreadable/non-48 kHz take refuses with a reason and leaves the
    /// previous state untouched.
    bool setTakeRegions(int track, const std::vector<TakeRegion>& regions,
                        std::string* error = nullptr);
    void clearTakeRegions(int track);
    /// The applied region list; empty when the track plays its take stack.
    std::vector<TakeRegion> takeRegions(int track) const;
    /// Ring samples that were not ready in time and rendered as silence.
    std::uint64_t takeUnderruns() const { return mTakePlayer.underruns(); }

private:
    static bool trackInRange(int track) {
        return track >= 0 && track < kMaxTracks;
    }
    void processBlock(const float* const* in, int nch, int n,
                      const OutputBuses& outputs);
    void applyMainComp(float* left, float* right, int n);
    void updateCompressor();
    void renderClick(int n);

    Channel mChannels[kMaxTracks];
    // Routing state kept out of Channel so a default-constructed strip means
    // identity input and a Main destination (the historical behaviour).
    // Tracks 0..17 default to the identity device input; tracks without a
    // device input (18..63) default to kNoInputSource (silent until a take or
    // an explicit source is selected).
    int mInputSource[kMaxTracks] = {};
    bool mInputPair[kMaxTracks] = {};
    OutputDestination mOutputDestination[kMaxTracks] = {};
    IEffect* mFx[kNumSends] = {nullptr, nullptr};
    float mFxReturn[kNumSends] = {1.0f, 1.0f};
    float mMaster = 1.0f;
    float mTap[kMaxTracks][kBlockSize] = {};
    // Per-channel input map: track t uses internal channels 2t and 2t+1; the
    // second is kNoInputSource for a mono track. Rebuilt from the per-track
    // selection at the top of every block.
    int mChannelDevice[kMaxChannels] = {};
    // True while a track's tap holds live data, so an inactive track zeroes it
    // once instead of every block (the skip-inactive fast path).
    bool mTapLive[kMaxTracks] = {};
    float mSendL[kNumSends][kBlockSize] = {};
    float mSendR[kNumSends][kBlockSize] = {};

    TakePlayer mTakePlayer;
    TakeLoop mTakeLoop[kMaxTracks]{};
    std::vector<TakeRegion> mTakeRegions[kMaxTracks];
    float mTakeBuffer[kMaxTracks][kBlockSize] = {};
    bool mTakeActive[kMaxTracks] = {};
    bool mTakePlaying[kMaxTracks] = {};
    float mCueL[kBlockSize] = {};
    float mCueR[kBlockSize] = {};
    float mOut34L[kBlockSize] = {};
    float mOut34R[kBlockSize] = {};
    float mOut34Mono[kBlockSize] = {};
    float mStripL[kBlockSize] = {};
    float mStripR[kBlockSize] = {};
    float mClickBuffer[kBlockSize] = {};
    ClickSource mClick;

    // One EqInsert per track: the band settings are per track, the biquad
    // state is per internal channel (two per insert), so 64 inserts hold the
    // 128 channel filter states the taxonomy describes.
    EqInsert mTrackEq[kMaxTracks];
    EqInsert mMainEq;

    // Per-track FX rack: the applied {id,bypass,wet,params} per slot is
    // control-thread state (capture reads it), the rack pointer is published
    // once per track and only read by the audio thread. mFxDryL/R is the
    // wet<1 crossfade scratch (only touched when 0 < wet < 1).
    TrackFxSlot mFxSlotState[kMaxTracks][kTrackFxSlots];
    std::atomic<TrackFxRack*> mFxRack[kMaxTracks] = {};
    float mFxDryL[kBlockSize] = {};
    float mFxDryR[kBlockSize] = {};

    // Device output map: control thread writes, the audio thread loads each
    // block. Atomic so a session load cannot tear a pair mid-block.
    std::atomic<int> mDeviceOut[3][2] = {
        {std::atomic<int>(0), std::atomic<int>(1)},
        {std::atomic<int>(2), std::atomic<int>(3)},
        {std::atomic<int>(4), std::atomic<int>(5)}};
    SendSource mSendSource[kNumSendDestinations] = {
        SendSource::PostFader, SendSource::PostFader, SendSource::PostFader,
        SendSource::PostFader, SendSource::PostFader};
    Out34Mode mOut34Mode = Out34Mode::Stereo;
    PhonesSource mPhonesSource = PhonesSource::Cue;
    CueStyle mCueStyle = CueStyle::Level;
    MainCompSettings mMainComp;
    double mCompressorGainDb = 0.0;  // positive attenuation in dB
    double mCompressorRmsSq = 0.0;
    double mCompressorAttackCoeff = 0.0;
    double mCompressorReleaseCoeff = 0.0;
    double mCompressorRmsCoeff = 0.0;

    ClickMode mClickMode = ClickMode::Off;
    ClickOutput mClickOutput = ClickOutput::Main;
    float mClickGainDb = 0.0f;
    bool mRecording = false;

    TransportPosition mTransport;
    double mSampleRate = 48000.0;
};

}  // namespace mnky::audio
