#include "engine.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "fx/delay.hpp"
#include "fx/reverb.hpp"

namespace mnky::audio {

static_assert(TakePlayer::kMaxTracks == kMaxTracks,
              "take playback track count must match the engine track count");
static_assert(kMaxChannels == 2 * kMaxTracks,
              "the per-channel map covers two channels per track");

namespace {

constexpr float kHalfPi = 1.57079632679489661923f;
constexpr double kSilenceDb = -240.0;

// Constant-power pan: gainL^2 + gainR^2 == 1 for every position. The `!(p >= 0)`
// form maps NaN to hard left instead of propagating it.
void constantPowerPan(float pan, float& gainL, float& gainR) {
    float p = pan;
    if (!(p >= 0.0f)) p = 0.0f;
    if (p > 1.0f) p = 1.0f;
    const float angle = p * kHalfPi;
    gainL = std::cos(angle);
    gainR = std::sin(angle);
}

// Stereo balance for a paired strip. Both sides stay at unity in the centre, so
// a centred pair passes through unchanged, and the side turned away from is
// attenuated to silence at the extreme. Unlike the constant-power pan it never
// boosts a side and it does not narrow the pair.
void balancePan(float pan, float& gainL, float& gainR) {
    float p = pan;
    if (!(p >= 0.0f)) p = 0.0f;
    if (p > 1.0f) p = 1.0f;
    gainL = std::min(2.0f * (1.0f - p), 1.0f);
    gainR = std::min(2.0f * p, 1.0f);
}

// Mute always wins; any active solo narrows the mix to soloed strips.
bool audible(const Channel& ch, bool anySolo) {
    if (ch.mute) return false;
    return !anySolo || ch.solo;
}

float clamp01(float value) {
    if (!(value >= 0.0f)) return 0.0f;
    return value > 1.0f ? 1.0f : value;
}

float clampFinite(float value, float fallback, float low, float high) {
    if (!std::isfinite(value)) return fallback;
    return std::clamp(value, low, high);
}

double dbToLinear(double db) { return std::pow(10.0, db / 20.0); }

float flushDenormal(float value) {
    return value > -1.0e-20f && value < 1.0e-20f ? 0.0f : value;
}

// Take playback and region edits are a 48 kHz float path: they never resample,
// so both selectors refuse at another engine rate with the same reason.
bool takeRateOk(double sampleRate, std::string* error) {
    if (sampleRate == 48000.0) return true;
    if (error != nullptr) *error = "take playback requires a 48000 Hz engine";
    std::fprintf(stderr,
                 "mnky audio: take refused: engine rate %.0f Hz is not "
                 "48000 Hz\n",
                 sampleRate);
    return false;
}

MonitorMode sanitizeMonitor(MonitorMode mode) {
    switch (mode) {
        case MonitorMode::Off:
        case MonitorMode::Auto:
        case MonitorMode::On:
        case MonitorMode::InputOnly:
            return mode;
    }
    return MonitorMode::Auto;
}

// Insert kinds published by TrackInsertChain. Plain ints so the audio thread
// reads one atomic word, not an enum whose storage the ABI leaves open.
constexpr int kInsertNone = 0;
constexpr int kInsertDelay = 1;
constexpr int kInsertReverb = 2;
constexpr int kInsertExternal = 3;

int insertKindForId(const std::string& id) {
    if (id == "builtin.delay") return kInsertDelay;
    if (id == "builtin.reverb") return kInsertReverb;
    return kInsertExternal;  // any other id: the attached CLAP-seam effect
}

// Defaults of an effect's own parameter list, in stable index order. An id
// change re-initializes the instance from these, so a stale setting from an
// earlier selection cannot leak into the new one.
std::vector<double> insertDefaults(const fx::Effect& effect) {
    const std::vector<fx::Param>& params = effect.params();
    std::vector<double> values(params.size());
    for (std::size_t index = 0; index < params.size(); ++index) {
        values[index] = params[index].def;
    }
    return values;
}

// Id change: clear the DSP state and restore the effect's own defaults, then
// apply the given parameters. applyState() snaps every ramp, so the newly
// selected instance is audible from its first block instead of sweeping from
// the settings of an earlier selection; non-finite entries are dropped and
// leave the default in place.
void reinitialize(fx::Effect& effect, const std::vector<double>& params) {
    fx::EffectState fresh;
    fresh.id = effect.id();
    fresh.version = effect.version();
    fresh.params = insertDefaults(effect);
    const std::size_t given = std::min(params.size(), fresh.params.size());
    for (std::size_t index = 0; index < given; ++index) {
        if (std::isfinite(params[index])) fresh.params[index] = params[index];
    }
    fresh.bypass = false;  // bypass is the engine's chain-level switch
    (void)effect.applyState(fresh);
    effect.reset();
}

}  // namespace

// One track's insert DSP. Both built-in instances exist for the track's whole
// life (created on the first configured insert), so switching id never
// allocates or frees on the audio path; the selected kind is one atomic word
// and the parameters live in the effect objects, exactly like the send-bus
// effects the app drives from the control thread.
class TrackInsertChain final {
public:
    explicit TrackInsertChain(double sampleRate) {
        mDelay.setSampleRate(sampleRate);
        mReverb.setSampleRate(sampleRate);
    }

    void setSampleRate(double sampleRate) {
        mDelay.setSampleRate(sampleRate);
        mReverb.setSampleRate(sampleRate);
        if (IEffect* effect = mExternal.load(std::memory_order_acquire)) {
            effect->setSampleRate(sampleRate);
        }
    }

    void setExternal(IEffect* effect) {
        mExternal.store(effect, std::memory_order_release);
    }

    void select(int kind) {
        mKind.store(kind, std::memory_order_release);
    }

    void setBypass(bool bypass) {
        mBypass.store(bypass, std::memory_order_relaxed);
    }

    fx::Delay& delay() { return mDelay; }
    fx::Reverb& reverb() { return mReverb; }

    /// In-place stereo insert. Bypass and None return before touching the
    /// buffers, so an inactive insert is bit-transparent and costs one load.
    void process(float* left, float* right, int n) {
        if (n <= 0 || left == nullptr || right == nullptr) return;
        if (mBypass.load(std::memory_order_relaxed)) return;
        const int kind = mKind.load(std::memory_order_relaxed);
        if (kind == kInsertNone) return;
        float* buses[2] = {left, right};
        if (kind == kInsertDelay) {
            mDelay.process(buses, 2, n);
        } else if (kind == kInsertReverb) {
            mReverb.process(buses, 2, n);
        } else if (IEffect* effect = mExternal.load(std::memory_order_acquire)) {
            effect->process(buses, 2, n);
        }
    }

private:
    fx::Delay mDelay;
    fx::Reverb mReverb;
    std::atomic<IEffect*> mExternal{nullptr};
    std::atomic<int> mKind{kInsertNone};
    std::atomic<bool> mBypass{false};
};

Engine::Engine() {
    // Identity input mapping for the device inputs: track N listens to device
    // N until reconfigured. Tracks without a device input (18..63) start with
    // no live source; their audio comes from takes or an explicit selection.
    for (int c = 0; c < kMaxTracks; ++c) {
        mInputSource[c] = c < kMaxDeviceInputs ? c : kNoInputSource;
    }
    updateCompressor();
    mClick = ClickSource(mSampleRate);
}

Engine::~Engine() {
    // The audio thread is stopped before the engine is destroyed (the
    // caller's contract), so the published chains can be freed here.
    for (auto& slot : mInsertChain) {
        delete slot.load(std::memory_order_relaxed);
    }
}

void Engine::process(const float* const* in, int nch, int n, float* outL,
                     float* outR) {
    if (outL == nullptr || outR == nullptr || n <= 0) return;
    OutputBuses outputs;
    outputs.mainL = outL;
    outputs.mainR = outR;
    process(in, nch, n, outputs);
}

void Engine::process(const float* const* in, int nch, int n,
                     const OutputBuses& outputs) {
    if (n <= 0) return;
    if (outputs.mainL == nullptr && outputs.mainR == nullptr &&
        outputs.phonesL == nullptr && outputs.phonesR == nullptr &&
        outputs.out34L == nullptr && outputs.out34R == nullptr) {
        return;
    }
    processBlock(in, nch, n, outputs);
}

void Engine::processBlock(const float* const* in, int nch, int n,
                          const OutputBuses& outputs) {
    const int frames = n < kBlockSize ? n : kBlockSize;
    int inputs = nch < 0 ? 0 : nch;
    if (inputs > kMaxDeviceInputs) inputs = kMaxDeviceInputs;

    // Take playback is a 48 kHz float path. At another engine rate the take
    // strips stay selected but silent: no hidden resampling and no live-input
    // leak through a disarmed strip.
    float* takeBuffers[kMaxTracks] = {};
    for (int c = 0; c < kMaxTracks; ++c) takeBuffers[c] = mTakeBuffer[c];
    mTakePlayer.render(mTransport.rolling && mSampleRate == 48000.0, frames,
                       takeBuffers, mTakeActive, mTakePlaying);

    float accL[kBlockSize] = {};
    float accR[kBlockSize] = {};
    for (int s = 0; s < kNumSends; ++s) {
        for (int i = 0; i < frames; ++i) {
            mSendL[s][i] = 0.0f;
            mSendR[s][i] = 0.0f;
        }
    }
    for (int i = 0; i < frames; ++i) {
        mCueL[i] = 0.0f;
        mCueR[i] = 0.0f;
        mOut34L[i] = 0.0f;
        mOut34R[i] = 0.0f;
        mOut34Mono[i] = 0.0f;
    }

    // Resolve each track's live source into the per-channel map for this
    // block. Track t owns internal channels 2t (primary/left) and 2t+1
    // (secondary/right). A pair is always (even, even+1): an odd selection
    // clamps down and the top channel pairs with the one below it, so the
    // render never reads past kMaxDeviceInputs and the stored selection stays
    // exactly what the session applied. kNoInputSource (-1) stays silent.
    for (int c = 0; c < kMaxTracks; ++c) {
        int device = mInputSource[c];
        const bool paired = mInputPair[c] && device >= 0;
        if (paired) device = std::min(device & ~1, kMaxDeviceInputs - 2);
        mChannelDevice[2 * c] = device;
        mChannelDevice[2 * c + 1] = paired ? device + 1 : kNoInputSource;
    }

    bool anySolo = false;
    for (int c = 0; c < kMaxTracks; ++c) {
        anySolo = anySolo || mChannels[c].solo;
    }

    // The per-destination send stages are block state, not per-sample state.
    bool sendPre[kNumSends] = {};
    for (int s = 0; s < kNumSends; ++s) {
        sendPre[s] =
            mSendSource[static_cast<int>(SendDestination::Fx1) + s] ==
            SendSource::PreFader;
    }
    const bool cuePre =
        mSendSource[static_cast<int>(SendDestination::Cue)] ==
        SendSource::PreFader;
    const bool out34Pre =
        mSendSource[static_cast<int>(SendDestination::Out34)] ==
        SendSource::PreFader;
    const bool dualMono = mOut34Mode == Out34Mode::DualMono;

    for (int c = 0; c < kMaxTracks; ++c) {
        const Channel& ch = mChannels[c];
        const int deviceL = mChannelDevice[2 * c];
        const int deviceR = mChannelDevice[2 * c + 1];
        const bool paired = deviceR >= 0;
        const float* liveL =
            (in != nullptr && deviceL >= 0 && deviceL < inputs) ? in[deviceL]
                                                               : nullptr;
        const float* liveR =
            (in != nullptr && paired && deviceR < inputs) ? in[deviceR]
                                                          : nullptr;
        const bool hasLive = liveL != nullptr || liveR != nullptr;
        // InputOnly suppresses the take voice only at output: the take player
        // below still renders and advances this track, so a later switch back
        // to Off/Auto/On resumes exactly where playback would have been.
        const float* take =
            (!ch.arm && mTakeActive[c] &&
             ch.monitor != MonitorMode::InputOnly)
                ? mTakeBuffer[c]
                : nullptr;
        const bool preGain = ch.tapPoint == TapPoint::PreGain;

        // The recorder/monitor tap always sees the track's selected live source
        // at its tapPoint; a pair is averaged to mono. Monitor mode, the take
        // voice and the output destination never change it. A track that just
        // lost its live source zeroes the tap once (see mTapLive).
        if (hasLive) {
            for (int i = 0; i < frames; ++i) {
                float mono = 0.0f;
                if (paired) {
                    if (liveL != nullptr) mono += 0.5f * liveL[i];
                    if (liveR != nullptr) mono += 0.5f * liveR[i];
                } else if (liveL != nullptr) {
                    mono = liveL[i];
                }
                mTap[c][i] = preGain ? mono : mono * ch.gain;
            }
            mTapLive[c] = true;
        } else if (mTapLive[c]) {
            for (int i = 0; i < frames; ++i) mTap[c][i] = 0.0f;
            mTapLive[c] = false;
        }

        // Skip-inactive fast path: no live source and no take voice means the
        // track produces nothing, so the EQ, insert, pan and send matrix are
        // skipped entirely (an empty insert included). With 64 tracks and 18
        // device inputs this is what keeps an idle project cheap.
        if (!hasLive && take == nullptr) continue;

        if (!audible(ch, anySolo)) continue;
        bool liveAllowed = false;
        switch (ch.monitor) {
            case MonitorMode::Off:
                liveAllowed = false;
                break;
            case MonitorMode::On:
                liveAllowed = true;
                break;
            case MonitorMode::InputOnly:
                // "Just monitoring": the live input is the strip's only voice.
                liveAllowed = true;
                break;
            case MonitorMode::Auto:
                // Armed tracks always monitor; a disarmed track drops the
                // live feed only while its take voice is actually playing.
                liveAllowed = ch.arm || !mTakePlaying[c];
                break;
        }
        if (!liveAllowed && take == nullptr) continue;

        float gainL = 0.0f;
        float gainR = 0.0f;
        if (paired) {
            balancePan(ch.pan, gainL, gainR);
        } else {
            constantPowerPan(ch.pan, gainL, gainR);
        }
        const float cueLevel =
            mCueStyle == CueStyle::Toggle ? (ch.cueSend > 0.0f ? 1.0f : 0.0f)
                                          : ch.cueSend;
        const OutputDestination destination = mOutputDestination[c];
        EqInsert& eq = mTrackEq[c];
        // Post-gain, pre-fader insert staged ahead of the send matrix. The
        // take voice is part of this path (it is the strip's voice), while the
        // recorder tap above stays live-only and pre-EQ.
        for (int i = 0; i < frames; ++i) {
            float left = 0.0f;
            float right = 0.0f;
            if (liveAllowed) {
                if (liveL != nullptr) left += liveL[i];
                if (paired) {
                    if (liveR != nullptr) right += liveR[i];
                } else {
                    right = left;  // a mono source feeds both sides
                }
            }
            if (take != nullptr) {
                // The take voice is mono, so it joins the strip in the centre
                // of a pair and is positioned by the same pan law.
                left += take[i];
                right += take[i];
            }
            mStripL[i] = left * ch.gain;
            mStripR[i] = right * ch.gain;
        }
        if (eq.active()) eq.process(mStripL, mStripR, frames);
        // Per-track FX insert: post-EQ, pre-fader (vol) and pre-sends, so every
        // send stage and the dry destination hear the processed signal while
        // the recorder tap above stays pre-insert (the tapPoint policy).
        if (TrackInsertChain* insert =
                mInsertChain[c].load(std::memory_order_relaxed)) {
            insert->process(mStripL, mStripR, frames);
        }

        for (int i = 0; i < frames; ++i) {
            // Unpanned pre-fader stage (mono sum: the pair average plus the
            // take voice once) and the post-fader sides that follow pan.
            const float mono = 0.5f * (mStripL[i] + mStripR[i]);
            const float preFader = mono;
            const float postL = mStripL[i] * ch.vol;
            const float postR = mStripR[i] * ch.vol;
            const float postMono = mono * ch.vol;

            switch (destination) {
                case OutputDestination::Main:
                    accL[i] += postL * gainL;
                    accR[i] += postR * gainR;
                    break;
                case OutputDestination::Out34:
                    if (dualMono) {
                        mOut34Mono[i] += postMono;
                    } else {
                        mOut34L[i] += postL * gainL;
                        mOut34R[i] += postR * gainR;
                    }
                    break;
                case OutputDestination::Phones:
                    mCueL[i] += postL * gainL;
                    mCueR[i] += postR * gainR;
                    break;
                case OutputDestination::None:
                    break;
            }

            for (int s = 0; s < kNumSends; ++s) {
                const float level = ch.send[s];
                mSendL[s][i] += (sendPre[s] ? preFader : postL * gainL) * level;
                mSendR[s][i] += (sendPre[s] ? preFader : postR * gainR) * level;
            }

            mCueL[i] += (cuePre ? preFader : postL * gainL) * cueLevel;
            mCueR[i] += (cuePre ? preFader : postR * gainR) * cueLevel;

            if (dualMono) {
                mOut34Mono[i] += (out34Pre ? preFader : postMono) * ch.out34Send;
            } else {
                mOut34L[i] +=
                    (out34Pre ? preFader : postL * gainL) * ch.out34Send;
                mOut34R[i] +=
                    (out34Pre ? preFader : postR * gainR) * ch.out34Send;
            }
        }
    }

    for (int s = 0; s < kNumSends; ++s) {
        if (mFx[s] != nullptr) {
            float* buses[2] = {mSendL[s], mSendR[s]};
            mFx[s]->process(buses, 2, frames);
        }
        const float ret = mFxReturn[s];
        for (int i = 0; i < frames; ++i) {
            accL[i] += mSendL[s][i] * ret;
            accR[i] += mSendR[s][i] * ret;
        }
    }

    for (int i = 0; i < frames; ++i) {
        accL[i] = flushDenormal(accL[i] * mMaster);
        accR[i] = flushDenormal(accR[i] * mMaster);
    }
    applyMainComp(accL, accR, frames);

    renderClick(frames);
    const float clickGain = static_cast<float>(dbToLinear(mClickGainDb));
    const bool clickOn =
        mClickMode != ClickMode::Off && mTransport.rolling &&
        (mClickMode != ClickMode::RecordOnly || mRecording);
    if (clickOn) {
        if (mClickOutput == ClickOutput::Main) {
            for (int i = 0; i < frames; ++i) {
                accL[i] += mClickBuffer[i] * clickGain;
                accR[i] += mClickBuffer[i] * clickGain;
            }
        }
    }

    // Main-bus EQ: post-compressor and post-click, the final Main 1&2 stage.
    // Phones-as-Main duplicates it; OUT3&4 and the CUE bus never see it.
    if (mMainEq.active()) mMainEq.process(accL, accR, frames);

    // Phones: CUE by default, or the compressed Main duplicate. A Main-routed
    // click is already in accL/R, so a Main duplicate carries it; a
    // Phones-routed click is added once here.
    if (mPhonesSource == PhonesSource::Main) {
        for (int i = 0; i < frames; ++i) {
            mCueL[i] = accL[i];
            mCueR[i] = accR[i];
        }
    }
    if (clickOn && mClickOutput == ClickOutput::Phones) {
        for (int i = 0; i < frames; ++i) {
            mCueL[i] += mClickBuffer[i] * clickGain;
            mCueR[i] += mClickBuffer[i] * clickGain;
        }
    }
    if (clickOn && mClickOutput == ClickOutput::Out34) {
        if (mOut34Mode == Out34Mode::DualMono) {
            for (int i = 0; i < frames; ++i) {
                mOut34Mono[i] += mClickBuffer[i] * clickGain;
            }
        } else {
            for (int i = 0; i < frames; ++i) {
                mOut34L[i] += mClickBuffer[i] * clickGain;
                mOut34R[i] += mClickBuffer[i] * clickGain;
            }
        }
    }
    if (mOut34Mode == Out34Mode::DualMono) {
        for (int i = 0; i < frames; ++i) {
            mOut34L[i] = mOut34Mono[i];
            mOut34R[i] = mOut34Mono[i];
        }
    }

    for (int i = 0; i < frames; ++i) {
        if (outputs.mainL != nullptr) outputs.mainL[i] = accL[i];
        if (outputs.mainR != nullptr) outputs.mainR[i] = accR[i];
        if (outputs.phonesL != nullptr) outputs.phonesL[i] = mCueL[i];
        if (outputs.phonesR != nullptr) outputs.phonesR[i] = mCueR[i];
        if (outputs.out34L != nullptr) outputs.out34L[i] = mOut34L[i];
        if (outputs.out34R != nullptr) outputs.out34R[i] = mOut34R[i];
    }
}

void Engine::applyMainComp(float* left, float* right, int n) {
    if (!mMainComp.on) {
        mCompressorGainDb = 0.0;
        mCompressorRmsSq = 0.0;
        return;
    }
    const double threshold = mMainComp.threshDb;
    const double ratio = mMainComp.ratio;
    const double makeup = mMainComp.makeupDb;
    for (int i = 0; i < n; ++i) {
        double level = 0.0;
        if (mMainComp.mode == CompressorMode::Rms) {
            const double leftSample = left[i];
            const double rightSample = right[i];
            const double meanSquare =
                0.5 * (leftSample * leftSample + rightSample * rightSample);
            mCompressorRmsSq =
                mCompressorRmsCoeff * mCompressorRmsSq +
                (1.0 - mCompressorRmsCoeff) * meanSquare;
            level = std::sqrt(std::max(mCompressorRmsSq, 0.0));
        } else {
            level = std::max(std::fabs(static_cast<double>(left[i])),
                             std::fabs(static_cast<double>(right[i])));
        }
        const double levelDb =
            level > 0.0 ? 20.0 * std::log10(std::max(level, 1.0e-12))
                        : kSilenceDb;
        const double over = levelDb - threshold;
        const double target =
            over > 0.0 ? over * (1.0 - 1.0 / ratio) : 0.0;
        const double coeff = target > mCompressorGainDb
                                 ? mCompressorAttackCoeff
                                 : mCompressorReleaseCoeff;
        mCompressorGainDb =
            coeff * mCompressorGainDb + (1.0 - coeff) * target;
        const float gain =
            static_cast<float>(dbToLinear(makeup - mCompressorGainDb));
        left[i] = flushDenormal(left[i] * gain);
        right[i] = flushDenormal(right[i] * gain);
    }
}

void Engine::updateCompressor() {
    const double rate = mSampleRate > 0.0 ? mSampleRate : 48000.0;
    const auto coefficient = [rate](float milliseconds) {
        if (!(milliseconds > 0.0f)) return 0.0;
        return std::exp(-1.0 / (rate * (milliseconds / 1000.0)));
    };
    mCompressorAttackCoeff = coefficient(mMainComp.attackMs);
    mCompressorReleaseCoeff = coefficient(mMainComp.releaseMs);
    mCompressorRmsCoeff = std::exp(-1.0 / (rate * 0.010));
}

void Engine::renderClick(int n) {
    const bool gate = mClickMode != ClickMode::Off && mTransport.rolling &&
                      (mClickMode != ClickMode::RecordOnly || mRecording);
    if (!gate) {
        mClick.setEnabled(false);
        return;
    }
    mClick.setEnabled(true);
    mClick.setLevel(1.0f);
    mClick.render(mClickBuffer, n, mTransport.tick, mTransport.ticksPerBeat,
                  mTransport.rolling);
}

const float* Engine::tap(int ch) const {
    if (ch < 0 || ch >= kMaxTracks) return nullptr;
    return mTap[ch];
}

void Engine::setChannel(int index, Channel ch) {
    if (index < 0 || index >= kMaxTracks) return;
    ch.cueSend = clamp01(ch.cueSend);
    ch.out34Send = clamp01(ch.out34Send);
    ch.monitor = sanitizeMonitor(ch.monitor);
    mChannels[index] = ch;
}

void Engine::setCueSend(int index, float level) {
    if (index < 0 || index >= kMaxTracks) return;
    mChannels[index].cueSend = clamp01(level);
}

float Engine::cueSend(int index) const {
    if (index < 0 || index >= kMaxTracks) return 0.0f;
    return mChannels[index].cueSend;
}

void Engine::setOut34Send(int index, float level) {
    if (index < 0 || index >= kMaxTracks) return;
    mChannels[index].out34Send = clamp01(level);
}

float Engine::out34Send(int index) const {
    if (index < 0 || index >= kMaxTracks) return 0.0f;
    return mChannels[index].out34Send;
}

void Engine::setMonitorMode(int index, MonitorMode mode) {
    if (index < 0 || index >= kMaxTracks) return;
    mChannels[index].monitor = sanitizeMonitor(mode);
}

MonitorMode Engine::monitorMode(int index) const {
    if (index < 0 || index >= kMaxTracks) return MonitorMode::Auto;
    return mChannels[index].monitor;
}

void Engine::setInputSource(int index, int deviceChannel) {
    if (index < 0 || index >= kMaxTracks) return;
    if (deviceChannel < 0) {
        mInputSource[index] = kNoInputSource;
        return;
    }
    mInputSource[index] =
        deviceChannel < kMaxDeviceInputs ? deviceChannel
                                         : kMaxDeviceInputs - 1;
}

int Engine::inputSource(int index) const {
    if (index < 0 || index >= kMaxTracks) return kNoInputSource;
    return mInputSource[index];
}

void Engine::setInputPair(int index, bool paired) {
    if (index < 0 || index >= kMaxTracks) return;
    mInputPair[index] = paired;
}

bool Engine::inputPair(int index) const {
    if (index < 0 || index >= kMaxTracks) return false;
    return mInputPair[index];
}

void Engine::setOutputDestination(int index,
                                  OutputDestination destination) {
    if (index < 0 || index >= kMaxTracks) return;
    const int value = static_cast<int>(destination);
    mOutputDestination[index] = value >= 0 && value < kNumOutputDestinations
                                    ? destination
                                    : OutputDestination::Main;
}

OutputDestination Engine::outputDestination(int index) const {
    if (index < 0 || index >= kMaxTracks) return OutputDestination::Main;
    return mOutputDestination[index];
}

void Engine::setDeviceOut(const DeviceOut& deviceOut) {
    const int pairs[3][2] = {{deviceOut.main[0], deviceOut.main[1]},
                             {deviceOut.out34[0], deviceOut.out34[1]},
                             {deviceOut.phones[0], deviceOut.phones[1]}};
    for (int bus = 0; bus < 3; ++bus) {
        for (int side = 0; side < 2; ++side) {
            const int channel = pairs[bus][side];
            mDeviceOut[bus][side].store(channel < 0 ? 0 : (channel > 31 ? 31 : channel),
                                        std::memory_order_relaxed);
        }
    }
}

DeviceOut Engine::deviceOut() const {
    DeviceOut result;
    result.main[0] = mDeviceOut[0][0].load(std::memory_order_relaxed);
    result.main[1] = mDeviceOut[0][1].load(std::memory_order_relaxed);
    result.out34[0] = mDeviceOut[1][0].load(std::memory_order_relaxed);
    result.out34[1] = mDeviceOut[1][1].load(std::memory_order_relaxed);
    result.phones[0] = mDeviceOut[2][0].load(std::memory_order_relaxed);
    result.phones[1] = mDeviceOut[2][1].load(std::memory_order_relaxed);
    return result;
}

void Engine::setMaster(float gain) {
    mMaster = gain > 0.0f ? gain : 0.0f;
}

void Engine::setTrackEqBand(int track, int band, const EqBand& value) {
    if (!trackInRange(track)) return;
    mTrackEq[track].setBand(band, value);
}

EqBand Engine::trackEqBand(int track, int band) const {
    if (!trackInRange(track)) return EqBand{};
    return mTrackEq[track].band(band);
}

void Engine::setMainEqBand(int band, const EqBand& value) {
    mMainEq.setBand(band, value);
}

EqBand Engine::mainEqBand(int band) const { return mMainEq.band(band); }

void Engine::setFx(int send, IEffect* fx) {
    if (send < 0 || send >= kNumSends) return;
    mFx[send] = fx;
}

void Engine::setTrackInsert(int track, const TrackInsert& insert) {
    if (!trackInRange(track)) return;
    TrackInsert& state = mInsertState[track];
    // The app re-applies its mixer snapshot every block; an unchanged insert
    // must cost nothing (and must not restart a parameter ramp).
    if (state == insert) return;
    if (insert.id.empty()) {
        if (TrackInsertChain* chain =
                mInsertChain[track].load(std::memory_order_acquire)) {
            chain->select(kInsertNone);
        }
        state = TrackInsert{};
        return;
    }

    TrackInsertChain* chain =
        mInsertChain[track].load(std::memory_order_acquire);
    if (chain == nullptr) {
        // First configured insert on this track: the only allocation this
        // control-thread call ever does, and never on the audio path.
        chain = new TrackInsertChain(mSampleRate);
        mInsertChain[track].store(chain, std::memory_order_release);
    }

    const int kind = insertKindForId(insert.id);
    const bool idChanged = state.id != insert.id;
    if (idChanged) {
        chain->select(kind);
        state.id = insert.id;
        state.params.clear();
        if (kind == kInsertDelay) {
            reinitialize(chain->delay(), insert.params);
        } else if (kind == kInsertReverb) {
            reinitialize(chain->reverb(), insert.params);
        }
    }
    chain->setBypass(insert.bypass);
    state.bypass = insert.bypass;

    if (kind == kInsertDelay || kind == kInsertReverb) {
        fx::Effect& effect = kind == kInsertDelay
                                 ? static_cast<fx::Effect&>(chain->delay())
                                 : static_cast<fx::Effect&>(chain->reverb());
        const int count = static_cast<int>(effect.params().size());
        if (!idChanged) {
            // Hot parameter application: the effect ramps from its current
            // value over 20 ms and keeps its delay line / reverb tail. A value
            // that already is the effect's target is skipped, so a repeated
            // per-block apply never retargets a ramp that is still running.
            const int given = static_cast<int>(insert.params.size());
            for (int index = 0; index < count && index < given; ++index) {
                const double value =
                    insert.params[static_cast<std::size_t>(index)];
                if (effect.param(index) == value) continue;
                (void)effect.setParam(index, value);
            }
        }
        // Capture reads back the values the DSP actually holds (the effect
        // clamps ranges and rounds stepped selectors).
        state.params.resize(static_cast<std::size_t>(count));
        for (int index = 0; index < count; ++index) {
            state.params[static_cast<std::size_t>(index)] = effect.param(index);
        }
    } else {
        // The CLAP seam owns its own parameter table; the engine echoes the
        // session values so a load/save round-trip does not drop them.
        state.params = insert.params;
    }
}

TrackInsert Engine::trackInsert(int track) const {
    if (!trackInRange(track)) return TrackInsert{};
    return mInsertState[track];
}

void Engine::setTrackInsertEffect(int track, IEffect* effect) {
    if (!trackInRange(track)) return;
    TrackInsertChain* chain =
        mInsertChain[track].load(std::memory_order_acquire);
    if (chain == nullptr) {
        if (effect == nullptr) return;
        chain = new TrackInsertChain(mSampleRate);
        mInsertChain[track].store(chain, std::memory_order_release);
    }
    chain->setExternal(effect);
}

void Engine::setFxReturn(int send, float gain) {
    if (send < 0 || send >= kNumSends) return;
    mFxReturn[send] = gain;
}

void Engine::setSampleRate(double sampleRate) {
    for (auto* fx : mFx) {
        if (fx != nullptr) fx->setSampleRate(sampleRate);
    }
    for (EqInsert& eq : mTrackEq) eq.setSampleRate(sampleRate);
    mMainEq.setSampleRate(sampleRate);
    for (auto& slot : mInsertChain) {
        if (TrackInsertChain* chain = slot.load(std::memory_order_acquire)) {
            chain->setSampleRate(sampleRate);
        }
    }
    if (!(sampleRate > 0.0) || !std::isfinite(sampleRate)) return;
    if (sampleRate == mSampleRate) return;
    mSampleRate = sampleRate;
    mClick = ClickSource(sampleRate);
    updateCompressor();
}

void Engine::setSendSource(SendDestination destination, SendSource source) {
    const int index = static_cast<int>(destination);
    if (index < 0 || index >= kNumSendDestinations) return;
    mSendSource[index] =
        source == SendSource::PreFader ? SendSource::PreFader
                                       : SendSource::PostFader;
}

SendSource Engine::sendSource(SendDestination destination) const {
    const int index = static_cast<int>(destination);
    if (index < 0 || index >= kNumSendDestinations) return SendSource::PostFader;
    return mSendSource[index];
}

void Engine::setOut34Mode(Out34Mode mode) {
    mOut34Mode = mode == Out34Mode::DualMono ? Out34Mode::DualMono
                                             : Out34Mode::Stereo;
}

void Engine::setPhonesSource(PhonesSource source) {
    mPhonesSource =
        source == PhonesSource::Main ? PhonesSource::Main : PhonesSource::Cue;
}

void Engine::setCueStyle(CueStyle style) {
    mCueStyle =
        style == CueStyle::Toggle ? CueStyle::Toggle : CueStyle::Level;
}

void Engine::setMainComp(const MainCompSettings& settings) {
    MainCompSettings next;
    next.on = settings.on;
    next.threshDb = clampFinite(settings.threshDb, -12.0f, -48.0f, 0.0f);
    next.ratio = clampFinite(settings.ratio, 2.0f, 1.0f, 100.0f);
    next.attackMs = clampFinite(settings.attackMs, 10.0f, 0.0f, 1000.0f);
    next.releaseMs = clampFinite(settings.releaseMs, 100.0f, 0.0f, 1000.0f);
    next.makeupDb = clampFinite(settings.makeupDb, 0.0f, -36.0f, 36.0f);
    next.mode = settings.mode == CompressorMode::Rms ? CompressorMode::Rms
                                                     : CompressorMode::Peak;
    if (!next.on || next.mode != mMainComp.mode) {
        mCompressorGainDb = 0.0;
        mCompressorRmsSq = 0.0;
    }
    mMainComp = next;
    updateCompressor();
}

void Engine::setClickMode(ClickMode mode) {
    switch (mode) {
        case ClickMode::Off:
        case ClickMode::RecordOnly:
        case ClickMode::OnPlay:
            mClickMode = mode;
            return;
    }
    mClickMode = ClickMode::Off;
}

void Engine::setClickOutput(ClickOutput output) {
    switch (output) {
        case ClickOutput::Main:
        case ClickOutput::Phones:
        case ClickOutput::Out34:
            mClickOutput = output;
            return;
    }
    mClickOutput = ClickOutput::Main;
}

void Engine::setClickGainDb(float gainDb) {
    mClickGainDb = clampFinite(gainDb, 0.0f, -96.0f, 20.0f);
}

void Engine::setTransport(bool rolling, std::uint32_t tick,
                          std::uint32_t ticksPerBeat, int bpmX100) {
    mTransport.rolling = rolling;
    mTransport.tick = tick;
    mTransport.ticksPerBeat = ticksPerBeat;
    mTransport.bpmX100 = bpmX100 > 0 ? bpmX100 : 12000;
    mTransport.valid = ticksPerBeat > 0;
    if (!mTransport.valid) {
        mTransport.tickInBeat = 0;
        mTransport.beat = 1;
        mTransport.measure = 1;
        mTransport.frame = 0;
        return;
    }
    const std::uint32_t beats = tick / ticksPerBeat;
    mTransport.tickInBeat = tick % ticksPerBeat;
    mTransport.measure = beats / 4 + 1;
    mTransport.beat = beats % 4 + 1;
    const double bpm = static_cast<double>(mTransport.bpmX100) / 100.0;
    const double framesPerTick =
        mSampleRate * 60.0 / (bpm * static_cast<double>(ticksPerBeat));
    mTransport.frame = static_cast<std::uint64_t>(
        std::llround(static_cast<double>(tick) * framesPerTick));
}

bool Engine::setActiveTakes(int track, const std::vector<std::string>& paths,
                            std::string* error) {
    if (track < 0 || track >= kMaxTracks) {
        if (error != nullptr) *error = "track is out of range";
        return false;
    }
    if (!paths.empty() && !takeRateOk(mSampleRate, error)) return false;
    return mTakePlayer.setTakes(track, paths, mTransport.frame, error);
}

bool Engine::setTakeRegions(int track, const std::vector<TakeRegion>& regions,
                            std::string* error) {
    if (track < 0 || track >= kMaxTracks) {
        if (error != nullptr) *error = "track is out of range";
        return false;
    }
    if (regions.empty()) {
        clearTakeRegions(track);
        return true;
    }
    if (!takeRateOk(mSampleRate, error)) return false;
    const double bpm = static_cast<double>(mTransport.bpmX100) / 100.0;
    const double framesPerBeat = bpm > 0.0 ? mSampleRate * 60.0 / bpm : 0.0;
    if (!mTakePlayer.setRegions(track, regions, framesPerBeat, mTransport.frame,
                                error)) {
        return false;
    }
    mTakeRegions[track] = regions;
    return true;
}

void Engine::clearTakeRegions(int track) {
    if (track < 0 || track >= kMaxTracks) return;
    mTakeRegions[track].clear();
    mTakePlayer.clearRegions(track);
}

std::vector<TakeRegion> Engine::takeRegions(int track) const {
    if (track < 0 || track >= kMaxTracks) return {};
    return mTakeRegions[track];
}

bool Engine::setActiveTake(int track, const std::string& path,
                           std::string* error) {
    if (track < 0 || track >= kMaxTracks) {
        if (error != nullptr) *error = "track is out of range";
        return false;
    }
    if (path.empty()) {
        clearActiveTake(track);
        return true;
    }
    return setActiveTakes(track, std::vector<std::string>{path}, error);
}

void Engine::clearActiveTake(int track) {
    if (track < 0 || track >= kMaxTracks) return;
    mTakePlayer.clearTrack(track);
}

void Engine::setTakeLoop(int track, const TakeLoop& loop) {
    if (track < 0 || track >= kMaxTracks) return;
    const double bpm = static_cast<double>(mTransport.bpmX100) / 100.0;
    const double framesPerBeat = bpm > 0.0 ? mSampleRate * 60.0 / bpm : 0.0;
    mTakeLoop[track] = loop;
    mTakePlayer.setLoop(track, loop, framesPerBeat);
}

int Engine::activeTakeCount(int track) const {
    return mTakePlayer.takeCount(track);
}

std::string Engine::activeTake(int track) const {
    const int count = mTakePlayer.takeCount(track);
    if (count <= 0) return {};
    return mTakePlayer.takePath(track, count - 1);
}

}  // namespace mnky::audio
