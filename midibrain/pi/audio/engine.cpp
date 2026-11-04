#include "engine.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace mnky::audio {

static_assert(TakePlayer::kMaxTracks == kMaxInputs,
              "take playback track count must match the engine input count");

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
            return mode;
    }
    return MonitorMode::Auto;
}

}  // namespace

Engine::Engine() {
    // Identity input mapping: strip N listens to device N until reconfigured.
    for (int c = 0; c < kMaxInputs; ++c) mInputSource[c] = c;
    updateCompressor();
    mClick = ClickSource(mSampleRate);
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
    if (inputs > kMaxInputs) inputs = kMaxInputs;

    // Take playback is a 48 kHz float path. At another engine rate the take
    // strips stay selected but silent: no hidden resampling and no live-input
    // leak through a disarmed strip.
    float* takeBuffers[kMaxInputs] = {};
    for (int c = 0; c < kMaxInputs; ++c) takeBuffers[c] = mTakeBuffer[c];
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

    // Resolve each strip's live source for this block. A pair is always
    // (even, even+1): an odd selection clamps down and the top channel pairs
    // with the one below it, so the render never reads past kMaxInputs and the
    // stored selection stays exactly what the session applied.
    int sourceDevice[kMaxInputs] = {};
    bool sourcePaired[kMaxInputs] = {};
    for (int c = 0; c < kMaxInputs; ++c) {
        int device = mInputSource[c];
        const bool paired = mInputPair[c] && device >= 0;
        if (paired) device = std::min(device & ~1, kMaxInputs - 2);
        sourceDevice[c] = device;  // kNoInputSource stays -1: a silent strip
        sourcePaired[c] = paired;
    }

    bool anySolo = false;
    for (int c = 0; c < kMaxInputs; ++c) anySolo = anySolo || mChannels[c].solo;

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

    for (int c = 0; c < kMaxInputs; ++c) {
        const Channel& ch = mChannels[c];
        const int device = sourceDevice[c];
        const bool paired = sourcePaired[c];
        const float* liveL =
            (in != nullptr && device >= 0 && device < inputs) ? in[device]
                                                             : nullptr;
        const float* liveR =
            (in != nullptr && paired && device + 1 < inputs) ? in[device + 1]
                                                            : nullptr;
        const float* take =
            (!ch.arm && mTakeActive[c]) ? mTakeBuffer[c] : nullptr;
        const bool preGain = ch.tapPoint == TapPoint::PreGain;

        // The recorder/monitor tap always sees the strip's selected live source
        // at its tapPoint; a pair is averaged to mono. Monitor mode, the take
        // voice and the output destination never change it.
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

        if (!audible(ch, anySolo)) continue;
        bool liveAllowed = false;
        switch (ch.monitor) {
            case MonitorMode::Off:
                liveAllowed = false;
                break;
            case MonitorMode::On:
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
    if (ch < 0 || ch >= kMaxInputs) return nullptr;
    return mTap[ch];
}

void Engine::setChannel(int index, Channel ch) {
    if (index < 0 || index >= kMaxInputs) return;
    ch.cueSend = clamp01(ch.cueSend);
    ch.out34Send = clamp01(ch.out34Send);
    ch.monitor = sanitizeMonitor(ch.monitor);
    mChannels[index] = ch;
}

void Engine::setCueSend(int index, float level) {
    if (index < 0 || index >= kMaxInputs) return;
    mChannels[index].cueSend = clamp01(level);
}

float Engine::cueSend(int index) const {
    if (index < 0 || index >= kMaxInputs) return 0.0f;
    return mChannels[index].cueSend;
}

void Engine::setOut34Send(int index, float level) {
    if (index < 0 || index >= kMaxInputs) return;
    mChannels[index].out34Send = clamp01(level);
}

float Engine::out34Send(int index) const {
    if (index < 0 || index >= kMaxInputs) return 0.0f;
    return mChannels[index].out34Send;
}

void Engine::setMonitorMode(int index, MonitorMode mode) {
    if (index < 0 || index >= kMaxInputs) return;
    mChannels[index].monitor = sanitizeMonitor(mode);
}

MonitorMode Engine::monitorMode(int index) const {
    if (index < 0 || index >= kMaxInputs) return MonitorMode::Auto;
    return mChannels[index].monitor;
}

void Engine::setInputSource(int index, int deviceChannel) {
    if (index < 0 || index >= kMaxInputs) return;
    if (deviceChannel < 0) {
        mInputSource[index] = kNoInputSource;
        return;
    }
    mInputSource[index] =
        deviceChannel < kMaxInputs ? deviceChannel : kMaxInputs - 1;
}

int Engine::inputSource(int index) const {
    if (index < 0 || index >= kMaxInputs) return kNoInputSource;
    return mInputSource[index];
}

void Engine::setInputPair(int index, bool paired) {
    if (index < 0 || index >= kMaxInputs) return;
    mInputPair[index] = paired;
}

bool Engine::inputPair(int index) const {
    if (index < 0 || index >= kMaxInputs) return false;
    return mInputPair[index];
}

void Engine::setOutputDestination(int index,
                                  OutputDestination destination) {
    if (index < 0 || index >= kMaxInputs) return;
    const int value = static_cast<int>(destination);
    mOutputDestination[index] = value >= 0 && value < kNumOutputDestinations
                                    ? destination
                                    : OutputDestination::Main;
}

OutputDestination Engine::outputDestination(int index) const {
    if (index < 0 || index >= kMaxInputs) return OutputDestination::Main;
    return mOutputDestination[index];
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
    if (track < 0 || track >= kMaxInputs) {
        if (error != nullptr) *error = "track is out of range";
        return false;
    }
    if (!paths.empty() && !takeRateOk(mSampleRate, error)) return false;
    return mTakePlayer.setTakes(track, paths, mTransport.frame, error);
}

bool Engine::setTakeRegions(int track, const std::vector<TakeRegion>& regions,
                            std::string* error) {
    if (track < 0 || track >= kMaxInputs) {
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
    if (track < 0 || track >= kMaxInputs) return;
    mTakeRegions[track].clear();
    mTakePlayer.clearRegions(track);
}

std::vector<TakeRegion> Engine::takeRegions(int track) const {
    if (track < 0 || track >= kMaxInputs) return {};
    return mTakeRegions[track];
}

bool Engine::setActiveTake(int track, const std::string& path,
                           std::string* error) {
    if (track < 0 || track >= kMaxInputs) {
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
    if (track < 0 || track >= kMaxInputs) return;
    mTakePlayer.clearTrack(track);
}

void Engine::setTakeLoop(int track, const TakeLoop& loop) {
    if (track < 0 || track >= kMaxInputs) return;
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
