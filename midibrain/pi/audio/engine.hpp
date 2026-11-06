#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "click.hpp"
#include "eq_insert.hpp"
#include "take_player.hpp"

namespace mnky::audio {

/// UMC1820 + ADA8200 capture width: 16 analog inputs plus 2 S/PDIF channels.
inline constexpr int kMaxInputs = 18;
/// Auxiliary FX send buses fed from every channel.
inline constexpr int kNumSends = 2;
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
/// follows pan. Mute/solo still gate every send, matching the historical
/// behaviour of the FX buses.
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
/// (centre) to 1 (hard right) with a constant-power law. `send[s]` is an FX
/// bus level, `cueSend` feeds the CUE/phones bus and `out34Send` feeds OUT3&4.
/// `tapPoint` picks the recorder/monitor tap; the default keeps the post-gain
/// behavior. `monitor` gates the live input contribution to Main/sends and, in
/// InputOnly, the take voice too; the recorder tap is independent of it.
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

/// Mixer core: gain/vol/pan, per-strip and main four-band EQ, mute/solo,
/// pre/post sends, FX returns, main compressor, click and monitor outputs.
/// Everything runs in the caller's (audio) thread and never allocates.
class Engine {
public:
    Engine();

    /// `n` must be <= kBlockSize. `in` holds `nch` (<= kMaxInputs) channel
    /// pointers; missing or null channels are silent. `outL`/`outR` receive the
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

    /// Mono tap of one strip at its Channel::tapPoint, valid until the next
    /// process() call: the raw input for PreGain, `in * gain` for the default
    /// PostGain. The tap is the strip's *selected* live source and never take
    /// playback; a stereo pair is averaged to mono (`0.5 * (L + R)`), because
    /// the recorder writes one mono file per armed strip. nullptr when `ch` is
    /// outside 0..kMaxInputs-1. Mute, solo, pan, vol, monitor, the output
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
        return index >= 0 && index < kMaxInputs ? mChannels[index] : Channel{};
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

    // --- per-strip input source / output destination -----------------------
    /// Select strip `index`'s live input source: a device channel 0..17 (higher
    /// values clamp) or kNoInputSource (-1) for silence. The default is
    /// identity, strip N <- device N. The recorder tap carries the selected
    /// source instead of the strip's own device index; the take voice, the pan
    /// and the output destination keep applying to the strip, and a
    /// silent-source strip keeps playing its own take voice (the monitor rule:
    /// silence is about live input only).
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
    /// `send` is 0..kNumSends-1; nullptr detaches and passes the bus through.
    void setFx(int send, IEffect* fx);
    /// Control thread only. Forwards to attached FX (see IEffect) and to every
    /// EQ insert; engine processing itself is rate-agnostic except for the main
    /// compressor, click, EQ biquads and 48 kHz-only take playback.
    void setSampleRate(double sampleRate);
    /// Linear return level applied to the FX bus after process().
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
        return track >= 0 && track < kMaxInputs;
    }
    void processBlock(const float* const* in, int nch, int n,
                      const OutputBuses& outputs);
    void applyMainComp(float* left, float* right, int n);
    void updateCompressor();
    void renderClick(int n);

    Channel mChannels[kMaxInputs];
    // Routing state kept out of Channel so a default-constructed strip means
    // identity input and a Main destination (the historical behaviour).
    int mInputSource[kMaxInputs] = {};  // identity 0..17, set in the ctor
    bool mInputPair[kMaxInputs] = {};
    OutputDestination mOutputDestination[kMaxInputs] = {};
    IEffect* mFx[kNumSends] = {nullptr, nullptr};
    float mFxReturn[kNumSends] = {1.0f, 1.0f};
    float mMaster = 1.0f;
    float mTap[kMaxInputs][kBlockSize] = {};
    float mSendL[kNumSends][kBlockSize] = {};
    float mSendR[kNumSends][kBlockSize] = {};

    TakePlayer mTakePlayer;
    TakeLoop mTakeLoop[kMaxInputs]{};
    std::vector<TakeRegion> mTakeRegions[kMaxInputs];
    float mTakeBuffer[kMaxInputs][kBlockSize] = {};
    bool mTakeActive[kMaxInputs] = {};
    bool mTakePlaying[kMaxInputs] = {};
    float mCueL[kBlockSize] = {};
    float mCueR[kBlockSize] = {};
    float mOut34L[kBlockSize] = {};
    float mOut34R[kBlockSize] = {};
    float mOut34Mono[kBlockSize] = {};
    float mStripL[kBlockSize] = {};
    float mStripR[kBlockSize] = {};
    float mClickBuffer[kBlockSize] = {};
    ClickSource mClick;

    EqInsert mTrackEq[kMaxInputs];
    EqInsert mMainEq;

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
