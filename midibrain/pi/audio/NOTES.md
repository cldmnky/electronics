# audio/ — engine + multitrack recorder (JUCE-free)

Works: `Engine` applies gain, constant-power pan (`L²+R² == gain²`), mute/solo
(mute wins; any solo narrows the mix), post-fader sends 0..1, in-place stereo
`IEffect` on each send bus, return levels, then master gain. `process()` never
allocates and clamps `nch`/`n` to `kMaxInputs`/`kBlockSize`; out-of-range
`setChannel`/`setFx`/`setFxReturn` calls are ignored (safe for a blind UI refresh).
Tap semantics: each strip has `Channel::tapPoint` (default `TapPoint::PostGain`,
which is the historical `in * gain`); `TapPoint::PreGain` taps the raw device
input so a gain change is reflected only in the post-gain tap. Both are pre-pan
and independent of mute/solo/master/monitor; the tap is always live input and
never take playback, zeroed for channels not supplied, valid only for the block
just processed and for 0..17 (else nullptr). Sends and dry path follow mute/solo.
Works: `Recorder::writeBlock()` copies into a 128-block mutex queue and never
touches disk; a background thread drains it. Full queue = counted drop.
Each take is `<dir>/take-<utc>/` with `chNN.wav` per armed input plus stereo
`master.wav`: float32 RIFF/WAVE, format code 3, `fmt `+`fact`+`data`, sizes patched
at close. `startTake(dir, armMask, sampleRate, masterArm = true)` keeps every
existing call site recording the master; `masterArm == false` suppresses
`master.wav` and the take reports only the channel files (frames still count).
`armMask == 0`, a second concurrent take, or a failed open returns false.
Not implemented (Pi follow-ups): ALSA/UMC1820 device capture, S/PDIF, Pi pinning,
48000 resample from 44100, disk-full policy (writes are best-effort), >4 GiB/file
(2 h master is ~2.8 GB, inside the RIFF limit but unverified), and true lock-free
queueing. Queue-overflow drops are counted but not deterministically tested.

## Engine wave 3: take playback, main comp, metronome, monitor buses

Works: `Channel` now has `vol` (post-gain fader), `cueSend`, `out34Send` and
`monitor` (off/auto/on). `gain` is always applied; pre-fader sends use `gain`
only and are pre-pan/mono, post-fader sends use `gain * vol` and follow pan.
Mute/solo still gate every send, preserving the historical FX-bus behaviour.
Monitor controls only the live input contribution to Main/sends: `Off` never
passes live input (the take voice keeps playing), `Auto` passes live input when
the track is armed or when no take is currently playing, and `On` always passes
live input (intentional take+live layering). A disarmed `Auto` track with a
rolling take therefore hears the take only, with no live doubling; if the
transport stops or the take ends, live input returns. Recorder taps are
unchanged: `tap()` always carries the live device input at its `tapPoint` and
never the take voice. Mute/solo overrides all monitor modes.
`SendDestination` selects a
source for `cue`, `fx1`, `fx2`, `out34` and the stored `rec` selector; process()
routes the first four (rec stays recorder-tap metadata in this wave).
`Out34Mode::Stereo` follows pan; `DualMono` writes the unpanned OUT3&4 send to
both jacks. OUT3&4 is never compressed.

`process(..., const OutputBuses&)` adds Phones and OUT3&4 to the historical
Main-only call. Phones is the CUE bus or a compressed Main duplicate
(`PhonesSource`), CUE send level/toggle is selected by `CueStyle`. FX returns
still land only on Main. Main runs `master` -> main compressor -> click;
OUT3&4 and CUE do not run the compressor.

Main compressor (`MainCompSettings`, peak/RMS, stereo-linked) clamps
threshold -48..0 dB, ratio 1..100, attack/release 0..1000 ms, makeup -36..+36 dB.
Peak is the instantaneous linked absolute peak; RMS is sqrt of a 10 ms one-pole
mean square. Gain reduction is smoothed per sample in dB with the attack/release
coefficients and makeup is applied after it. Off is a bit-exact bypass. A click
routed to Main is added after this stage so the metronome does not pump.

Metronome: `ClickMode` off/record-only/on-play, `ClickOutput` main/phones/out34,
`setClickGainDb` -96..+20, driven by the existing `ClickSource`. On-play needs a rolling
transport; record-only additionally needs `Engine::setRecording(true)`. The
click is rendered at unity and the dB gain is applied afterwards, so +20 dB is
not clamped to unity. Count-in bars, tempo/meter changes and sample-rate-aware
click pitch remain app follow-ups. Main routing injects after the compressor;
phones/main duplicate and OUT3&4 are written only to their selected bus.

Take playback (`TakePlayer`): a control thread opens a RIFF/WAVE take, requires
48 kHz and 32-bit IEEE float (format 3) or 16-bit PCM (format 1), mono or
stereo (stereo is downmixed `0.5*(L+R)`), then a reader thread streams the file
through a 8192-frame ring (about 171 ms at 48 kHz). Non-48 kHz, empty,
unsupported or unreadable files are refused with a reason string and a stderr
log line; files are never resampled. Selection is per track and ordered oldest
first: the last path is the newest, wins sample-for-sample on overlap, and older
takes resume where the newer file ends. Selection is a control-thread hand-off
(render pauses for the swap), starts at the engine's current transport frame,
and never falls back to live input. A disarmed strip with a selected take is
fed by the take (silence when stopped/ended); an armed strip ignores the take.
Monitor `Auto`/`Off`/`On` decides only whether the live input is summed into
that take path; `tap()` remains live-only for the recorder.
The reader thread keeps disk I/O out of `process()`; ring underruns and
selection swaps are counted/reported as silence, not as a resample or live leak.
Take playback assumes the file starts at session frame 0 and does not support a
locate after selection or a per-take offset (the session has no take-offset
field yet); re-select after a locate. Engine sample rates other than 48 kHz
refuse take selection and leave selected take strips silent.

Transport/punch source: `Engine::setTransport(rolling, tick, ticksPerBeat,
bpmX100)` publishes `TransportPosition` (valid, rolling, frame, tick, beat,
measure, tickInBeat). `frame = tick * sampleRate * 60 / (bpm * ticksPerBeat)`.
Punch state stays app-owned and the engine does not record punch boundaries.
Honest timing: an app polling in its audio callback first sees a measure at the
next block start, so a boundary is detected 0..128 frames late (0..2.67 ms at
48 kHz/128), plus the app's own dispatch latency; this is block-accurate, not
sample-accurate. Poll from the audio thread, not the UI timer, for the smallest
slop.

Session forwarding: `session::capture()` now reads the engine's cue/out34 send
levels, monitor mode, out34 mode, click mode/output/gain, main-comp settings,
phones source, cue style and the five send-source selectors; `apply()` writes
them back. The session JSON format/parser/emitter is unchanged (the v2 fields
already existed).
`Channel::vol` has no session v2 field yet and defaults to unity when a session
is applied.

Verified with: `c++ -std=c++20 -Wall -Wextra -Werror -I. test_audio_playback.cpp
engine.cpp take_player.cpp recorder.cpp click.cpp -o /tmp/t_play -pthread &&
/tmp/t_play`; the same build pattern for `test_audio_features.cpp` and
`test_click.cpp`. CTest owns the targets in `audio/CMakeLists.txt`.

## Engine wave 4: per-track take looping

Works: `TakeLoop {enabled, startBeats, endBeats}` per track, carried by the
session's `takes.loop` and exposed as `Engine::setTakeLoop()` /
`Engine::takeLoop()` (and `TakePlayer::setLoop()` / `loop()` underneath). Loop
points are session beats; the engine maps them to take frames with the current
transport tempo (`framesPerBeat = sampleRate * 60 / bpm`, e.g. 24000 frames per
beat at 120 bpm / 48 kHz, rounded to the nearest frame) and clamps them into the
selected stream (`startFrame` .. EOF). An enabled range shorter than 4 frames or
one that falls outside the take logs a reason and plays linearly instead of
inventing audio. `enabled` with `endBeats <= startBeats` is the whole-take rule:
each layer wraps at the end of its own selected stream. Changing the loop state
is a control-thread hand-off like take selection: it restarts that track's stack
at the loop start, and an unchanged state (and a cleared loop at an unchanged
tempo) is a no-op. `setTakes()`/`setActiveTake()` inherit the stored loop, so
loop-before-select works, and `clearTrack()` keeps the loop for the next
selection. The loop period is `round((endBeats - startBeats) * framesPerBeat)`
frames and is *not* re-anchored to the transport beat phase on each wrap: a
tempo change needs a re-apply (which restarts at the loop start), and the phase
is +/- 1 frame per restarted pass, i.e. accurate in length but not
sample-accurate beat sync. Honest limit: this is beat-mapped playback, not a
tempo-following loop.

Wrap method: **seam fade**. The reader thread wraps *inside* the take buffer
(`fseek` back to the loop start, `produceFrames()`), so the ring always carries
one continuous stream and the audio thread never seeks, never allocates and
never sees a gap or a silent block at the seam. The last and the first
`loopSeamFade(loopFrames)` frames of every pass (64 frames = 1.33 ms at 48 kHz,
capped at a quarter of the loop) fade linearly to exactly zero, so both sides of
the seam are silent and the wrap cannot click whatever the material is. Stated
tradeoff: a ~2.7 ms level dip per wrap on sustained material, and the loop's
first/last frames are attenuated. Rejected alternatives: a *hard wrap* is
beat-exact and the simplest but clicks whenever the material is discontinuous
there; a *zero-crossing nudge* avoids that click but moves the wrap off the beat
grid, so the loop length drifts against the transport; a *true crossfade* needs
material outside the loop range to fade against, which a whole-take loop does
not have (and the head-replay variant shortens the period by the fade length).

Looping composes with the existing rules instead of forking them. It applies to
every layer of the track; a looping newest layer never runs out of frames, so it
keeps winning sample-for-sample and older layers stay muted while the loop runs
(loop-off returns to today's linear newest-wins playback). Monitor modes are
untouched: a looping take reports "playing" while the transport rolls, so
`Auto` stays take-only with no live doubling across wraps, `Off` keeps the take
voice without live input, `On` still layers live input, an armed strip still
ignores the take, and a stopped transport still falls back to live monitoring.
`tap()` stays the live input. Looping is playback-only and deliberately **not
coupled to punch-in recording**: it never starts or stops the recorder, never
changes the recorder source and does not read or write `PunchState`/punch
points; an armed strip ignores the take and its loop exactly as before.

Session forwarding: `session::capture()` reads the per-track loop into
`snapshot.takes.loop` and `session::apply()` pushes it back through
`Engine::setTakeLoop()`; the JSON format/parser/emitter is unchanged (2636ecc
already emitted `takes.loop`) and `takes.active` stays app-owned.
Verified with: CTest target `test_audio_playback` (new cases: beat-mapped range
loop wrap timing against a known-length ramp take, whole-take loop including
`start == end`, disabled and out-of-range loops staying linear, loop +
newest-wins overlap, loop + monitor off/auto/on), plus the existing
test_audio_features/test_click/test_audio/test_fx targets and the root build.

## Engine wave 5: per-track input source and output destination

Works: each strip has an **input source** (`Engine::setInputSource(index,
device)`: a device channel 0..17, `kNoInputSource` = -1 for silence; default
identity, strip N <- device N) and an **output destination**
(`Engine::setOutputDestination(index, OutputDestination::Main | Out34 | Phones |
None)`; default Main). Both are routing state, not `Channel` fields: a `Channel`
value stays the historical mixer strip, so a blind `setChannel` refresh cannot
remap an input and a default-constructed strip still means identity + Main.
`inputSource()`/`inputPair()`/`outputDestination()` read them back (out-of-range
strips read as no source / false / Main).

Takes, taps and monitor compose with the source instead of forking it. The
recorder tap is the *selected* live source at `tapPoint` and never take
playback, so a strip remapped to device 5 records device 5; a selected source
that the block does not supply taps silence. Take playback belongs to the strip,
not to the device: a remapped strip keeps its own take stack, a strip whose
source is -1 keeps playing its take, and the take voice enters the same strip
path, so it follows the strip's pan and destination. That is the existing
monitor rule read literally — monitor mode and the source select are about live
input only.

Stereo pair: `Engine::setInputPair(index, true)` makes the strip claim `inCh` +
`inCh + 1`. **Pairing widens one strip; it does not gang two strips**, which is
what the manual's "a stereo pair shares monitor, arm and take state; pan spreads
the pair" requires: one strip already has exactly one arm, monitor and take set
(and one take stack, with the unchanged per-strip newest-wins layering), so
nothing new has to be shared or kept in sync. Odd `inCh` clamps *down* at render
time (5 binds 4/5) and the top channel pairs with the one below it (17 binds
16/17), so the render never reads past the 18 device channels; the stored
selection is never rewritten, so capture() returns exactly what apply() wrote.
A silent (-1) source cannot be paired. Pan becomes a **stereo balance**: both
sides at unity in the centre (a centred pair passes through unchanged), the side
turned away from fading to zero at the extreme, no boost — a mono strip keeps
the constant-power law bit-exactly. The recorder tap of a pair is its mono
average `0.5 * (L + R)`, and the take voice (mono) is centre-summed into both
sides before the balance, so it is positioned by the pan but never becomes
stereo. Gap by name: a paired strip is recorded and played back **mono** (one
WAV per armed strip); stereo take files per pair need a 2-channel tap and a
stereo take format, which does not exist yet.

Output destination: `Main` is the historical dry path — master, main
compressor, Main click — and carries the FX returns; `Out34` adds the dry strip
signal to the OUT3&4 bus at unity post-fader (following `Out34Mode`, so dual mono
sums it unpanned into both jacks) and is never compressed; `Phones` adds it to
the CUE bus; `None` drops the dry signal while sends and taps keep working.
Sends (FX1/FX2, `cueSend`, `out34Send`) are independent of the destination, and
mute/solo gate both the dry route and the sends. Gaps by name: (1) the engine has
**no separate phones bus** — Phones is the CUE bus, the jack plays CUE or a
compressed Main duplicate (`PhonesSource`), so a Phones-routed strip is
inaudible at the jack while `PhonesSource::Main` is selected; (2) `Rec` stays
recorder-tap metadata, so there is no per-strip record destination. Nothing was
invented to paper over either.

Session forwarding: `capture()` writes `mixer.channels[N].inCh/inPair/outDst`
from the engine (identity / false / Main for a fresh engine, i.e. identity until
the app grows a source picker) and `apply()` pushes them back through
`setInputSource`/`setInputPair`/`setOutputDestination`; the JSON
format/parser/emitter is unchanged (fecf8bb). Known consequence of the frozen
format: `inCh` defaults to 0 when the key is absent-but-writable, so a session
file saved between fecf8bb and this wave (all channels `"inCh":0`) means "every
strip on device 0" when applied. capture() now writes identity per channel, so
files saved by this build are correct; old files need a re-save.

Verified with: CTest target `test_audio_routing` (identity defaults and
getters, strip 2 <- device 5 including the tap and unsupplied-channel silence,
-1 silence under monitor On, pair binding / odd clamp / top-channel clamp /
silent pair, the balance law against the unchanged mono pan, all four
destinations including OUT3&4 dual mono and mute/solo gating, monitor + pair,
take voice + tap following the strip), plus the untouched test_audio/test_fx and
the test_audio_features/test_click/test_audio_playback targets and the root
build.

## Engine wave 6: waveform-editor backend (region render, normalize, peaks)

Works: the take voice can play a **non-destructive region edit** instead of the
selected take stack. `TakePlayer::setRegions()` / `Engine::setTakeRegions()`
take the session's ordered region list (`takes.edit`, format frozen at 43130e8):
each entry is `take`, `offsetBeats`, `lengthBeats` (0 = to the end of the take),
`gainDb` (-96..+12), `fadeInBeats`, `fadeOutBeats` and `normalize`. Beats map to
take frames with `framesPerBeat = sampleRate * 60 / bpm`, rounded to the nearest
frame, resolved **once at apply time** (like the loop: re-apply after a tempo
change). Regions play in list order as one edited stream: region 0, then region
1, and so on, so the list is the track's composition. Files are never rewritten;
render-to-new-take is out of scope (a later op), and `takes.active` stays
app-owned (applying an edit never selects a take).

Gain and fades run **in the reader thread**, not in `process()`: the edited stream
has its own ring and reader thread (the layer stack's pattern) and each produced
frame is multiplied by `gain * fadeIn * fadeOut` before it is published, where
`fadeIn(i) = min(1, i / fadeInFrames)` and
`fadeOut(i) = min(1, (frames - 1 - i) / fadeOutFrames)`. Both ramps are exactly 0
at their region edge and exactly 1 at their inner end, and they multiply, so two
fades that cover the whole region give a V and never exceed unity. The audio
thread only copies ring frames (`render()` gained one region-mode branch; the
layer path is unchanged), so `process()` does no allocation, no file I/O and no
per-sample envelope work. Underruns still render as counted silence.

Normalize is **one peak pass per region at load/apply time** on the control
thread (chunked read of just that region's frames, mono downmix). The gain is
`kNormalizePeakDbfs = -1 dBFS` (`0.891251`) divided by the region peak, so a
normalised region lands one decibel below full scale instead of clipping the
next stage; it multiplies with `gainDb` (`gainDb +6.02` on a normalised region is
exactly 2x). A silent region has no peak to scale to and keeps its gain; a NaN
sample is ignored by the peak so it can never poison the stream. Peak values
above full scale (a hot float WAV) attenuate, as they should.

Clamping is **logged, never fatal** (`mnky audio: take edit: ...` on stderr):
a region that starts at or past the end of its take is skipped, a length longer
than the rest of the take is clamped to the take end, a fade longer than the
region is clamped to the region, and a region shorter than one frame is skipped.
An edit whose every region clamps away stays an explicit, logged silence (empty
stream, `playing == false`, live input still passes under monitor `Auto`) rather
than falling back to the unedited take - playing material the user removed would
be worse. Unreadable or non-48 kHz takes **refuse the whole list** with a reason
and leave the previous state untouched, matching `setTakes()`: a partially
applied edit would silently drop audio. An edit list that resolves to zero
frames still reports the track as take-fed, so it cannot leak live input into a
take path.

Composition rules are reused, not forked. A non-empty edit replaces the track's
take voice; the take stack is kept and resumes on
`clearRegions()`/`clearTakeRegions()` (`clearTrack()` drops both, and
`setActiveTake()` while an edit is applied restarts the edit, because the edit is
that track's voice). For a track **without** an edit nothing changed at all:
newest-wins layering, monitor `Off`/`Auto`/`On`, the recorder tap and the arm rule
are bit-identical to wave 3/4 (the untouched `test_audio_playback` covers them),
and an edit on one track never touches another track's stack. The loop now wraps
the **edited** stream: `endBeats <= startBeats` loops the whole edited material
(so a 6000-frame edit wraps at 6000, not at the raw file's end), a beat-range loop
maps over the edited stream from the transport frame that was current at apply,
and the seam keeps its 64-frame declick ramp (`loopSeamFade`).

Peak scanner (`peaks.hpp`/`peaks.cpp`, no engine dependency): `PeakScanner::start()`
scans a take on its **own thread** into 256-frame min/max buckets
(`kPeakBucketFrames`, 5.3 ms at 48 kHz) plus a coarse level that folds every
`kPeakCoarseFactor = 64` buckets (~341 ms) so a zoomed-out view needs no
reduction. The scanned signal is the take voice's mono downmix
(`0.5 * (L + R)`), so the drawing matches playback. Progress is a throttled
atomic fraction (~100 updates plus 0 and 1, delivered on the worker thread) with
a completion callback; the finished summary is handed over with one release
store of an immutable pointer and the scanner keeps every published generation,
so a paint pass can never dangle and never takes a lock. The sidecar is
`<take>.wav.peaks` next to the WAV: a 56-byte little-endian header (magic `MNPK`,
version, bucket size, coarse factor, source frames/bytes/mtime, bucket and coarse
counts) followed by the min/max pairs. It is written through `<sidecar>.tmp` +
rename, so an interrupted write cannot pass as valid. `readPeaks()` accepts a
sidecar only when magic, version, bucket configuration and the WAV's **size and
mtime** all still match; anything else (missing, truncated, stale, other config)
means "rescan" and reports the reason. A newer scan first tries the sidecar and
publishes it without decoding audio. A missing, stale or unwritable sidecar costs
the UI its overview only: `summary()` stays `nullptr` (the placeholder case) and
take playback/region render are untouched - they never read peaks.
`cancel()` drops a scan without publishing or calling back.

Honest limits: an applied edit is not re-anchored when the tempo changes (re-apply,
the loop's rule); the edited stream always starts at its first region, so a
selection's transport frame is only the beat origin for the loop window and
playback is not sample-locked to the transport; a full-speed render loop can
outrun the 8192-frame ring and produce counted underruns (the test suite paces
itself like a callback, real-time consumption never does); `.peaks` freshness
uses the platform file-time tick, so the cache is machine-local and a same-size
same-timestamp rewrite is trusted; the coarse level is a plain min/max fold (no
RMS); and the UI wiring of the scanner (drawing, zoom levels) is app-side and not
part of this wave. The shared WAV reader moved to `wav.hpp` (header-only, so the
session standalone build needs no extra translation unit) and is the one spelling
of the accepted take format for the player, the normalize pass and the scanner.

Session forwarding: `session::apply()` converts `snapshot.takes.edit` to
`audio::TakeRegion` and calls `Engine::setTakeRegions()` (empty -> `clearTakeRegions()`),
and `session::capture()` reads `Engine::takeRegions()` back, so a saved project
round-trips its edit. The JSON format, parser and emitter are unchanged (43130e8
already had them). Known frozen-format quirk: `decodeTakes()` returns early when
`takes.active` is absent, so a hand-written file needs an `active` object before
its `edit` is read; the emitter always writes `"active":{}`, so files saved by
this build round-trip. A refused edit (missing or non-48 kHz take) is tolerant
like the FX slots: apply() logs the reason and keeps the previous state.

Verified with: CTest targets `test_take_edit` (region window/gain, fade ramps,
length 0, per-region normalize, clamp/skip reasons including the captured stderr
line, refusal of a 44.1 kHz take, loop wrapping the edited stream for the
whole-material and beat-range rules, stack/monitor composition through the engine,
tempo-dependent beat mapping) and `test_peaks` (bucket values, partial last
bucket, coarse fold, stereo downmix, sidecar round-trip, size and mtime
invalidation, worker-thread progress/completion, missing-file and non-48 kHz
failures), plus the untouched `test_audio`/`test_fx`/`test_audio_features`/
`test_click`/`test_audio_playback`/`test_audio_routing` targets and the root
build. Direct lines:
`c++ -std=c++20 -Wall -Wextra -Werror -I. test_take_edit.cpp engine.cpp take_player.cpp recorder.cpp peaks.cpp click.cpp -o /tmp/t_edit -pthread &&
/tmp/t_edit`, and the same for `test_peaks.cpp peaks.cpp recorder.cpp engine.cpp take_player.cpp click.cpp`.

## ClickSource (`click.hpp`, JUCE-free)

Works: `setEnabled`, `setLevel` (clamped 0..1, NaN/negative = 0) and
`render(monoOut, n, tick, ticksPerBeat, rolling)` exactly as the app contract
requires. A pip is a sine with a 30 ms exponential decay (tau = 6 ms, hard cut at
30 ms): 2 kHz on beat 0 of each 4/4 bar, 1 kHz on the other beats. `render()`
overwrites `monoOut`; it is silent (zeros) unless enabled and rolling, and a
stopped/disabled source cuts the tail. A repeated tick never retriggers; the
first call must be on a beat boundary. No allocation, denormal-safe envelope.
Beat placement is quantised to one block (2.7 ms at 48 kHz/128) because the
signature carries no sample-accurate tick phase. The 4/4 accent assumption
matches `app/logic.hpp` (`kBeatsPerBar = 4`).

Pi follow-ups: an explicit bar length and a sample-rate setter would let the app
change meter/rate without rebuilding the source. The wave-3 engine hosts a
ClickSource internally (modes/output/gain), but `AudioGlue::renderBlock` still
renders its own click, so the app-team wiring to the engine-side metronome is
pending.

Verified with: CTest targets `test_audio_playback`, `test_audio_features`,
`test_click` and the root build's `test_audio` (all exit 0, macOS clang). The
old direct `test_audio.cpp` command now also needs `engine.cpp take_player.cpp
click.cpp` because Engine owns those members.

## FX suite (`fx/`, wave 2)

Works: an instance-owned `Registry` (deliberately no singleton and no static
registration) maps stable ids to creators; `list()` returns browser metadata
`{id,manufacturer,type,category,name}` sorted by category/name. Builtins are
`eq`, `comp`, `delay`, `reverb`, all stereo `audio::IEffect`; bypass leaves the
caller's bus bit-exact. Every effect is denormal-flushed and silence-stable.
Params (stable indices, documented in each effect header):
- `eq`     `[low_db, peak_db, peak_hz, peak_q, high_db]` (shelves 120 Hz / 6 kHz)
- `comp`   `[threshold_db, ratio, attack_ms, release_ms, makeup_db]` (peak-linked)
- `delay`  `[time_ms, feedback, mix]` plus `tempoSyncedMs(bpm, beats)`
- `reverb` `[predelay_ms, decay, damping, mix]` (Schroeder/Moorer)
All params use a 20 ms block `Ramp`; `Param::value` is the control-thread shadow.
State JSON is exactly `{"id":"eq","v":2,"bypass":false,"params":[...]}`
(fixed key order, positional params in stable index order; `v` is the
 effect's `kVersion`: eq/delay/reverb are v2, comp is v1); `Effect::applyState()` rejects id/version/
count mismatch, clamps values, and snaps ramps so a restore does not sweep.
Time-based FX allocate their delay lines in the constructor at the default
rate and re-allocate on `setSampleRate()`; `Engine::setSampleRate()` forwards
to attached FX. Without this a fresh Delay was dead silent until a *changed*
rate arrived (`mSize <= 2` guard) — found by the delay timing tests.
Pi follow-ups: engine strip/fader/send/return ramps (left unchanged because the
existing engine tests pin instantaneous values), ramped bypass, delay-time
automation, reverb modulation/early reflections, CLAP-wrapper registration, and
per-effect oversampling.

## Engine wave 7: per-strip and main four-band EQ inserts

Works: every one of the 18 strips and the main bus has its own four-band EQ
insert (`audio::EqInsert`, `eq_insert.hpp`), fed from the session band model
(type 0 none, 1 low cut, 2 low shelf, 3 parametric, 4 high shelf, 5 high cut;
gain/frequency/Q shared). The coefficient math is not a second implementation:
the shipped FX-suite biquad was lifted verbatim into `fx/biquad.hpp`, which both
`fx::Eq` and `EqInsert` use. `fx::Eq` keeps its FX parameter surface and 20 ms
ramps; the engine inserts snap their coefficients on the control-thread setter
(no per-block sin/cos/pow on the audio thread, and no zipper smoothing yet —
matching the instantaneous gain/vol/pan setters).
`Engine::setTrackEqBand(track, band, EqBand)` / `trackEqBand()` and
`setMainEqBand(band, EqBand)` / `mainEqBand()` are the public seam. Values are
clamped to the session EQ contract: type 0..5, frequency 20..20000 Hz, Q
0.1..24; gain uses the EQ's ±24 dB operating range (the session parser does not
clamp gain, so a hand-edited out-of-range value is normalised here). Non-finite
values fall back to the session defaults (0 dB, 1000 Hz, Q 1.0); a default
insert is four None bands, which is exactly the session default `EqBand`.
Out-of-range tracks/bands are ignored and read as the default band.
`Engine::setSampleRate()` forwards the rate to all 19 inserts (8 kHz..192 kHz,
as the FX suite accepts) and rebuilds active coefficients; the take player's
48 kHz rule is unchanged.

Placement decisions:

* **Per-strip EQ is post-gain, pre-fader and post-tap.** It processes the full
  strip voice (live input plus take playback) after `Channel::gain` and before
  `Channel::vol`, pan and every send. Pre-fader sends therefore carry gain + EQ
  (not vol/pan), post-fader sends and the dry destination carry gain + EQ +
  vol/pan, and the FX/CUE/OUT3&4 matrices all see the same insert.
  The recorder/monitor `tap()` deliberately stays **pre-EQ**: `tap()` is the
  historical live-only signal (`in * gain` for the default `PostGain`, raw input
  for `PreGain`), independent of takes and mute. Making the tap post-EQ would
  need a second mono filter state per strip because the tap excludes the take
  voice while the insert includes it; that would roughly double the insert
  count/cost and split one band setting across two states. So recordings remain
  dry of strip EQ while every send and the dry output follow it. This is the
  documented tradeoff of the "pre-tap or post-tap" decision, not a hidden
  behaviour: a take recorded with EQ active will not have the EQ baked in.
* **Main EQ is post-compressor and post-click, the final Main 1&2 stage.** It
  runs after `applyMainComp()` and after a Main-routed metronome click, before
  the Phones copy and output writes. So the compressor detector never sees the
  EQ (an EQ boost cannot change gain reduction), the metronome follows the main
  EQ, Phones-as-Main duplicates the EQ'd Main, and OUT3&4 plus the CUE bus are
  never EQ'd. Master gain stays upstream, as before.

`EqInsert::process()` is a no-op when all four bands are None, so an
unconfigured strip/main insert is bit-transparent and costs no samples. A band
with type None is skipped inside an otherwise active chain. Changing a band's
type resets that band's state on both channels, so re-enabling a band cannot
ring with stale history; gain/frequency/Q changes keep the state (standard
biquad retuning).

Session forwarding: `session::capture()` reads every strip band and the main
bands from the engine into `snapshot.fx.eq`, and `apply()` writes them back
through `setTrackEqBand()`/`setMainEqBand()`. The JSON format, parser and
emitter are unchanged (51ff907 already carried `fx.eq`); this wave only fills
the existing DSP seam. A fresh engine captures as the session default EQ, so
the session `apply`/`capture` equality tests stay exact. The app's `EqModel`
still owns the edit surface and its own `fx::Eq` instances; project save/load
already applies the same snapshot to both, but live EQ-grid edits are not yet
forwarded into the engine insert — that app wiring is the remaining follow-up,
not a claim of this wave.

CPU honesty (not a benchmark suite, not a Pi measurement): the added
`test_audio_eq` prints a repeated-block timing. On the macOS dev host with
Apple clang `-O2`, 19 inserts × 4 active bands × 128-frame blocks measured
about **35 us/block** (270–280 ns/frame, ~650–670 cycles/frame if referred to
a 2.4 GHz clock) for the inserts alone, and about **39 us/block** (~300 ns/frame,
~720–740 cycles/frame) for the whole engine with all 18 strip inserts and the
main insert active. That is roughly 1.3–1.5% of a 48 kHz/128-frame block on this
host; the Pi 5 number is unmeasured. The `-O0` standalone timing is much
slower (~146 us/block) because the biquad chain is not inlined well; quote the
optimised figure only. Inactive inserts cost no biquad work (the all-None
branch), but the strip staging pass still runs.

Verified with: new CTest target `test_audio_eq` (shelf/cut/parametric shapes
spot-checked against `fx::Eq` configured with the same values, type-None
bit-transparency including denormals, per-strip send/dry/tap placement, main
post-compressor ordering and Main-only routing, session clamps and sample-rate
forwarding, timing print), plus the untouched `test_audio`/`test_fx`/
`test_audio_features`/`test_click`/`test_audio_playback`/`test_audio_routing`/
`test_take_edit`/`test_peaks` targets and `test_session`. Direct line:
`clang++ -std=c++20 -Wall -Wextra -Werror -I. test_audio_eq.cpp engine.cpp
take_player.cpp click.cpp fx/eq.cpp fx/effect.cpp fx/params.cpp -o /tmp/t_eq
-pthread && /tmp/t_eq`.

## Engine wave 8: native audio backend (PipeWire + ALSA, Qt Multimedia removed)

Works: `device_backend.hpp` is the device I/O seam. A `DeviceBackend` opens one
playback and (optionally) one capture endpoint, converts device frames to and
from **48 kHz interleaved float32**, and calls `DeviceClient::renderDevice()` /
`captureDevice()` from one serialised backend thread. The requested period is
`kBlockSize` (128 frames); a backend that negotiated more splits it, because the
engine block is a hard ceiling. Implementations:

- `pipewire_backend.cpp` — two SPA audio streams on one `pw_main_loop` thread
  via libpipewire. Playback is `PW_DIRECTION_OUTPUT` (a producing stream),
  capture is `PW_DIRECTION_INPUT` (a consuming stream); the node names are
  `mnkybox` and `mnkybox-capture`. `node.latency` requests `128/48000`, the
  reported period is the frames the graph actually hands the process callback,
  and the stream is connected with the target node's real channel positions.
  Playback prefers F32_LE and capture S32_LE. Proxies are created and destroyed
  on the loop thread (PipeWire 1.4 rejects `pw_stream_set_active`/`destroy` from
  another thread).
- `alsa_backend.cpp` — `snd_pcm` duplex on one poll thread, `hw:` first and
  `plughw:` as the format fallback, interleaved S32_LE/F32_LE/S24_3LE/S24_LE/
  S16_LE, 128-frame period with a 3-period buffer, xrun recovery counted, and a
  1.5 s EBUSY retry so switching from PipeWire to ALSA does not fall back to the
  generator while the graph releases the card.
- `device_backend.cpp` — exact sample conversion (a 24-bit *packed* S24_3LE and
  a 24-bit-in-32-bit S24_32 are distinct types, never a `reinterpret_cast`), the
  `DeviceOut` slot map, and `DevicePump` (18-channel capture ring -> engine
  plane pointers; it skips ahead instead of growing latency).
- Enumeration reports the REAL names: PipeWire `node.name`/`node.description`/
  `audio.channels`/`audio.position` (the last two from the bound node's info,
  not the registry global), ALSA card id + PCM name + probed channel count.
  PipeWire links channel-by-channel through the positions; without them a
  multichannel node is linked stereo-first. That was the Qt/Pulse bug.

`Engine::setDeviceOut()/deviceOut()` now holds the session `deviceOut` map
(defaults Main 0/1, OUT3&4 2/3, phones 4/5, clamped 0..31) so
`session::capture()`/`apply()` forward it with the rest of the mixer state; the
device frame mapping is applied by the glue/probe, not by the DSP.

`app/audio_backend.hpp` keys: `default` (PipeWire, then ALSA, then emulator),
`pipewire`, `alsa`, `device` (the id carries the API: `pipewire:<node>` /
`alsa:<pcm>`), `emulator`. The System device list shows both APIs with real
names and the per-device USB MIDI names (RtMidi port names grouped by client,
e.g. `UMC1820`), not one "USB" label.

Measured on the studio Pi 5 / UMC1820 (2026-11-05, Debian 13, PipeWire 1.4.2,
`mnky_audio_probe`, take files under `/tmp/probe-*`):

- Enumeration: `pipewire:alsa_output…multichannel-output` 20 ch,
  `…multichannel-input` 18 ch, `alsa:hw:CARD=UMC1820,DEV=0` 20/18 ch, all 48 kHz.
- ALSA duplex: 20 out `S24_3LE` + 18 in `S32_LE`, period 128, buffer 384,
  240,000 frames in 5.00 s, **0 xruns**; 18 `chNN.wav` + `master.wav`. Channel
  peaks matched a raw `arecord -f S32_LE -c 18` capture of the same inputs
  (e.g. ch3/ch4 at 0.0/-0.1 dBFS from the overdriven external source, ch9/10
  digital zero) - the conversion adds nothing.
- PipeWire duplex: 20 out F32_LE + 18 in S32_LE, period 128, 192,384 frames in
  4.00 s (= 48,096 frames/s, real time), **0 xruns**; the app itself ran
  `--no-pico --screenshots-live` with 3,190 engine blocks and 0 xruns.
- Playback mapping (sink monitor captured with 20 AUX channels): the default
  `deviceOut` puts 1.0/1.5/2.0/2.5/3.0/3.5 kHz on device channels 1..6 and
  nothing on 7..20; `deviceOut 2,3,4,5,6,7` moves the same six tones to
  channels 3..8 and leaves 1/2/9.. silent.
- Per-channel ident sine at unity on all 18 capture channels: peak
  `1.000000`, RMS `0.707107`, 0 samples above unity, THD **-156 … -175 dB**,
  inter-channel crosstalk **-161.6 dB** (steady 1 s window).
- `session::capture()`/`apply()` deviceOut forwarding verified on the Pi with a
  one-off program: default -> apply {6,7,8,9,10,11} -> capture reads it back,
  `{-5,99}` clamps to `{0,31}`.

Distortion root cause: the old path was structurally stereo. Qt Multimedia
opened the UMC1820 as a 2-channel `QAudioSink`/`QAudioSource` (Pulse device
model), so engine channels 3..20 never left the app and Pulse's channelmix
handled whatever reached the device; the sample format was whatever Qt/Pulse
negotiated (UMC playback is S24_3LE, capture S32_LE) and the glue only knew
float/Int16. Native ALSA/PipeWire streams plus the position-aware stream config
remove both the stereo-first device model and the implicit conversion. The
remaining 0 dBFS peaks measured on inputs 3/4 come from the external source
overdriving the UMC ADC, not from the app (the raw arecord capture shows the
same). Two measurement artifacts are documented in `backend_probe.cpp` so they
are not mistaken for distortion: a rectangular Goertzel window leaks the
fundamental into the harmonic bins (~-77 dB) and a window straddling the start
of playback creates a step (~-66 dB); both are removed (Hann + steady window).

Limits: there is **no analog loopback** on the UMC here (a -20 dBFS 1 kHz tone
on OUT1/2 was measured at -45 dB or lower on every input), so converter THD
cannot be measured; the THD figures above are digital (app stream -> UMC sink
node/monitor), and the UMC ADC path is evidenced by peak/RMS equality with raw
ALSA. The app has no headless record trigger (`main.cpp` is outside this
module), so the 18-channel take is produced by `mnky_audio_probe`, which drives
the identical `DeviceBackend` + `DevicePump` + `Engine` + `Recorder` path. ALSA
device ids contain commas, so a hand-edited `mnkybox.conf` must quote them
(`outputDevice="alsa:hw:CARD=UMC1820,DEV=0"`); QSettings writes that quoting
itself.

Verified with: `test_device_backend` (CTest; format exactness for S32/S24_3/
S24_32/S16/F32 including full-scale rails and NaN, device id round-trip, the
DeviceOut slot map incl. overlap and width truncation, the pump's zero-fill and
drop, the 18-in -> strips -> 20-out path through a real `Engine` at 64/128/256/
512/1024-frame periods, and a fake 3-period PCM device) plus the untouched
audio/session targets and `mnkybox --unittests` (404 checks). Pi:
`ctest` 18/18 and `--unittests` 404/404; `mnky_audio_probe list`, `take` and the
`screenshots-live` runs above.

## Engine wave 9: per-track FX insert (one instance per track)

Works: every strip has its own insert slot — `TrackInsert {id, bypass, params}`
with `Engine::setTrackInsert()` / `trackInsert()` (session capture) and
`setTrackInsertEffect()` for an externally owned effect. `id` is empty (none),
`builtin.delay`, `builtin.reverb` or a CLAP plugin id; `params` are positional
doubles in the effect's stable order (delay 8: `time_ms, feedback, mix,
beat_mode, division, ping_pong, bp_cutoff_hz, bp_width_oct`; reverb 6:
`predelay_ms, decay, damping, mix, model, size`). This is the per-track instance
path. (Superseded by engine wave 11: the per-track insert is now FX slot 0 of
a two-slot rack, and the global FX1/FX2 send buses are retired from the signal
path. The legacy single-insert API remains as a slot-0 wrapper.)

**Placement (proven by signal injection in `test_audio_insert.cpp`).** The
strip order is `live input (+ take voice) -> gain -> four-band EQ -> INSERT ->
vol (fader) -> pan -> destination and sends`:
- post-EQ: the insert input is bit-identical to the EQ-only engine output, and
  a +6 dB low-shelf is present at the insert;
- pre-fader: a fader move (vol) scales the insert *output* and leaves its input
  bit-identical;
- pre-sends: FX1 (pre- and post-fader source), CUE and OUT3&4 all carry the
  insert output (a wet-only 1 ms delay puts the impulse at frame 48 on every
  send, never the dry impulse at frame 0);
- the recorder/monitor tap stays **pre-insert** (and pre-EQ): `tap()` is the
  strip's selected live source at its `tapPoint`, so a recording does not bake
  the insert — the same policy the EQ insert already documents.

**Instances and cost.** Each track lazily creates one `fx::Delay` and one
`fx::Reverb` on its first configured insert and keeps both for the engine's
lifetime (an id change re-initializes only that track: DSP state cleared,
parameters back to their defaults, the new `params` applied and ramps snapped).
18 independent instances are therefore possible: the delay line is 2 s stereo
float (768 KB at 48 kHz) and the reverb allocates its maximum-size network
(~180 KB), so all 18 tracks having used both costs ~17 MB. An empty id detaches
the insert: no DSP runs, and the only cost is one atomic pointer load per track
per block. `bypass` returns before any processing, so the strip is bit-exact
(the tests compare whole buffers against a never-inserted engine). Same-id
parameter writes go through `Effect::setParam()`: 20 ms ramps, no reset, the
delay line / reverb tail survives (tested). The app re-applies its mixer
snapshot every block; an unchanged `{id,bypass,params}` is a no-op and a value
that already is the effect's target is skipped, so a running ramp is never
retargeted. Like the EQ and the sends, the insert runs only for strips that
pass the mute/solo and monitor/take gates; a gated strip freezes its insert
tail instead of leaking it into the sends.

**CPU probe (this macOS/Apple-silicon host, RelWithDebInfo build, 48 kHz,
64-frame blocks; budget 1333 us/block).** Measured by `test_audio_insert` with
18 strips fed noise; the probe runs with the same optimization flags as the
library, so an unoptimized build measures several times higher:

| insert | N=1 | N=6 | N=18 |
| --- | --- | --- | --- |
| none | — | — | 3.8-4.4 us/block |
| builtin.delay | 4.4-4.7 us (0.33-0.36%) | 6.0-6.1 us (0.45%) | 9.4-9.7 us (0.70-0.73%) |
| builtin.reverb | 4.7-5.0 us (0.35-0.37%) | 8.2-8.4 us (0.61-0.62%) | 15.8-16.2 us (1.18-1.22%) |

Marginal per-instance cost is 0.28-0.31 us/block (delay) and 0.65-0.66 us/block
(reverb). All 18 tracks, the architectural limit, cost 0.7% (delay) or 1.2%
(reverb) of the 48k/64 block budget on this host; extrapolating at 50% of the
budget gives ~2100-2400 delay or ~1000 reverb instances, so the track count,
not the CPU, is the limit here. This is **not** a Pi 5 number: the target must
re-measure with the same probe (same build type) before any limit is claimed.

**CLAP gap (named).** The engine does not load CLAP modules. `mnky_audio` is
the lower layer (`mnky_clap` links `mnky_audio`, and the existing
`clap::Host`/`app::FxSlot` adapter is GUI-thread-owned and single-threaded by
contract), so a per-track CLAP insert would invert the dependency and need 18
host instances with a per-block try-lock — unproven within the block budget
above. The engine therefore accepts and persists a CLAP id and its parameter
vector but runs only the effect attached with `setTrackInsertEffect()`; with
nothing attached the strip passes dry. Wiring one app `FxSlot` per track (load,
parameter mapping, session state) remains the app layer's follow-up.

**Session.** `mixer.channels[].insert` forwards both ways at the engine seam:
`session::apply()` calls `Engine::setTrackInsert()` for all 18 slots and
`session::capture()` reads `Engine::trackInsert()` back (clamped/stepped values
as the DSP holds them). The JSON shape is the session team's
(`{"id","bypass","params"}`, empty id = none).

Verified with: `test_audio_insert` (per-id dry/processed paths, two-track
independence, bypass bit-transparency, hot parameter ramps and delay-line
survival, id-change re-initialization scoped to one track, the placement
injections above, empty-id zero processing, the external seam, sample-rate
re-allocation, CPU probe) plus the untouched `test_audio`, `test_fx`,
`test_audio_eq` and the rest of the audio CTest suite.

## Engine wave 10: 64-track / 128-channel capacity and the Pi budget

Works: the engine's capacity is the product target — **64 mixer tracks**, each
mono or stereo, **128 internal channels** (track t owns channels 2t and 2t+1).
`capacity.hpp` is the single source of the taxonomy: `kMaxDeviceInputs` (18:
UMC1820 16 analog + 2 S/PDIF, the width `process()` reads), `kMaxTracks` (64),
`kMaxChannels` (128) and the legacy `kMaxInputs` (18, the device width every
existing device/tap call site and test already means). Every per-track array
(`Channel`, input selection/pair, output destination, EQ band settings, insert,
take selection/loop/region, take voice, recorder tap) is `kMaxTracks`; the
per-channel arrays are the input map (`mChannelDevice[kMaxChannels]`: device
input per internal channel, -1 for the unused right channel of a mono track)
and the EQ filter state (two channels inside each of the 64 `EqInsert`s).
Tracks 0..17 default to the identity device input; tracks 18..63 default to
`kNoInputSource` (silent until a take or an explicit source is selected), so a
fresh engine is the historical 18-strip behaviour plus 46 idle tracks.

Skip-inactive-tracks: a track with no live source and no take voice is skipped
before any per-sample work (EQ, insert, pan, sends and the destination are all
bypassed); its tap is zeroed once when it loses its source (`mTapLive`) rather
than every block. Measures 1.9 us per 64-frame block for all 64 tracks inactive
— including configured EQ, sends and 64 configured reverb inserts — versus
240 us for the full active mixing path.

Recorder: the arm mask is `uint64_t` (one bit per track, up to 64) and
`writeBlock()` reads only armed tracks, so an existing 18-entry tap array stays
safe. The take layout is unchanged: one mono `chNN.wav` per armed track (the
tap is the track's mono/averaged live source) plus `master.wav`. Take playback
(`TakePlayer::kMaxTracks = 64`) follows the engine capacity.

**Pi 5 budget (studio Pi, Cortex-A76, 2.4 GHz, g++ 14, RelWithDebInfo, pinned
core 2, 1500 blocks/row, 3 runs; budget 1333 us at 64 frames, 2667 us at 128
frames).** `mnky_audio_bench` (built by the audio module) prints the same table
on any target:

| scenario (48 kHz) | 64-frame block | 128-frame block |
| --- | --- | --- |
| 18 stereo active, gain+pan+4-band EQ+sends | 69-73 us (5.2-5.4%) | 150-158 us (5.6-5.9%) |
| 64 stereo active, gain+pan+4-band EQ+sends | 240-253 us (18-19%) | 525-553 us (20-21%) |
| 64 stereo active, gain+pan+sends (no EQ) | 62-65 us (4.6-4.9%) | 121-127 us (4.5-4.8%) |
| 64 mono active, gain+pan+4-band EQ+sends | 238-251 us (18-19%) | 504-532 us (19-20%) |
| 64 inactive (skip path, EQ+sends+inserts configured) | 1.9-2.0 us (0.1-0.2%) | 2.9-3.1 us (0.1%) |
| 64 stereo active + 32 reverb inserts | 531-592 us (40-44%) | 1247-1335 us (47-50%) |
| 64 stereo active + 64 reverb inserts | 837-897 us (63-67%) | 1963-2117 us (74-79%) |
| 64 stereo active + 64 delay inserts | 341-364 us (26-27%) | 749-800 us (28-30%) |

So the **64-frame block fits the product target with the scalar path**: the
full 64-stereo-track mixing path (gain + balance/pan + 4 bands + all sends) uses
18-19% of one core, and even all 64 tracks carrying a reverb insert fits at
63-67% (32 reverbs: 40-44%; 64 delays: 26-27%). The 128-frame block also fits
but is relatively worse (20-21% mixing, 74-79% with 64 reverbs), so the
recommendation is to keep `kBlockSize = 128` as the engine maximum and run
**64-frame device periods** (the UMC1820 period is configurable; the engine
already accepts any n <= 128). Nothing measured exceeds the 64-frame budget:
the insert ceiling is 64 simultaneous reverbs (63-67% of one core), with ~32
reverbs (40-44%) as the comfortable limit; delay inserts are cheap (64 =
26-27%). NEON was **not** implemented: the parent's condition ("NEON biquad if
the scalar budget misses") is not met — the scalar mixing path has ~5x headroom
and the EQ is 176 us of the 240 us total, so a vector biquad would buy headroom
the product does not need at 64 tracks.

Migration status: the engine and the audio module are at 64 tracks; `pi/session`
and `pi/app` still size their channel/mixer arrays from the legacy `kMaxInputs`
(18) until their workers migrate to `kMaxTracks`, so the shipped session format
and UI still address 18 tracks. The recorder's `adoptRecordedTake` path lives in
`pi/app` and still takes a 32-bit mask (app worker).

Verified with: `test_audio_insert` (new capacity checks: taxonomy static
asserts, default device mapping for 64 tracks, track 63 as an independent
stereo pair, inactive-tap zeroing, recorder 64-bit arm mask writing ch63.wav,
insert CPU probe at N = 1/18/64) plus the audio CTest suite on macOS and on the
studio Pi (11/11 both), and `mnky_audio_bench` on the Pi (table above).

## Engine wave 11: per-track FX rack (two slots), global send buses retired

Works: every strip owns a two-slot FX rack, **FX1 -> FX2 in series**, with the
old per-track insert's placement: `live input (+ take voice) -> gain -> 4-band
EQ -> FX1 -> FX2 -> vol (fader) -> pan -> destination/CUE/OUT3&4`. The
recorder/monitor tap stays upstream (pre-EQ, pre-rack), so a recording does not
bake the rack. The global FX1/FX2 send buses are **retired from the signal
path**; the app-facing symbols (`kNumSends`, `Channel::send`, `setFx()`,
`setFxReturn()`, `SendDestination::Fx1/Fx2`) still compile and store values but
no bus is fed, no bus effect runs and no return reaches Main/the CUE/OUT3&4.

Per slot (`audio::TrackFxSlot`, `audio::kTrackFxSlots = 2`):

| field | type | contract |
| --- | --- | --- |
| `id` | string | empty = none; `builtin.delay` / `builtin.reverb` / CLAP id |
| `bypass` | bool | hard dry switch, bit-transparent (returns before any buffer touch) |
| `wet` | float | 0..1 dry/wet crossfade, clamped; NaN -> 1; kept on an empty slot |
| `params` | vector<double> | effect's stable order (delay 8, reverb 6); extra entries ignored, built-ins clamp |

`Engine::setTrackFxSlot(track, slot, slotState)` / `trackFxSlot(track, slot)`
are the rack API; `Engine::setTrackFxSlotEffect(track, slot, IEffect*)` is the
per-slot external/CLAP seam. The deprecated single-insert API is a **slot-0
wrapper**: `setTrackInsert()` writes FX1 with `wet = 1`, `trackInsert()` reads
FX1, `setTrackInsertEffect()` attaches FX1. The wave-9 tests therefore keep
their exact expected audio (wet 1 = the old in-place insert, bit for bit).

**Wet math (series, dry always present).** Each slot's effect writes its own
output in place; the engine then writes

    out = dry + wet * (processed - dry)  =  (1 - wet) * dry + wet * processed

per sample, so `wet = 0` skips the DSP entirely (bit-transparent, also for an
empty slot or `bypass`) and `wet = 1` is the processed signal with no extra
copy. `0 < wet < 1` uses the engine's two block scratch buffers and one 20 ms
parameter ramp inside the effect still applies. FX2 sees FX1's crossfaded
output: the crossfade is per slot, in series, not a parallel bus.

**Instances and cost.** A rack is created on the first configured slot of a
track; inside a slot the selected built-in (`fx::Delay` or `fx::Reverb`) is
created on its first selection and kept for the engine's lifetime. All
allocation happens in the control-thread setter, never in `process()`. An empty
id costs one rack-pointer load per track plus one kind atomic per slot; `wet =
0` and `bypass` cost the same and run no DSP (proven: a never-configured rack
and both slots bypassed/wet-0 compare bit-identical). An id change re-initializes
only that slot (DSP state cleared, defaults, then params, ramps snapped); a
same-id parameter write is hot (20 ms ramp, delay/reverb tail survives, other
slot untouched). Sample-rate changes forward to both built-in instances and the
external effect. Memory is lazy: 768 KB per used 48 kHz delay and ~260 KB per
used reverb; the 64-track / 64-delay + 64-reverb worst case is ~66 MB; a slot
that has used both types keeps both (~131 MB for every slot in both types).

**CPU on the studio Pi 5 (Cortex-A76, RelWithDebInfo, `nice`/`ionice`, 1000
blocks/row, 48 kHz; budget 64 frames = 1333 us, 128 frames = 2667 us).**
`mnky_audio_bench` (full 64-stereo-track engine: gain/pan/EQ/sends) and
`test_audio_insert`'s rack probe (no EQ/sends, mono strips; keep the difference
in mind) both measured on the target:

| scenario | 64-frame block | 128-frame block |
| --- | --- | --- |
| 64 stereo active, gain+pan+EQ+sends (rack empty) | 220-222 us (16.5-16.7%) | 478-479 us (17.9%) |
| 64 active + 32 reverb FX1 slots | 506-507 us (38.0%) | 1171-1173 us (43.9-44.0%) |
| 64 active + 64 reverb FX1 slots | 797-798 us (59.8%) | 1864 us (69.9%) |
| 64 active + 64 delay FX1 slots | 318 us (23.9%) | 697 us (26.1%) |
| **64 active + 64 delay FX1 + 64 reverb FX2** | **934-935 us (70.0-70.1%)** | **2149-2156 us (80.6-80.8%)** |
| 64 active + 64 delay FX1 + 64 delay FX2 | 439-440 us (32.9-33.0%) | 974 us (36.5%) |
| 64 inactive + 128 configured FX slots (skip path) | 1.6 us (0.1%) | 2.5 us (0.1%) |
| probe: 64 tracks x (delay FX1 + reverb FX2), no EQ/sends | 706.8 us (53.0%) | 1471.0 us (55.2%) |
| probe: same, empty rack | 43.6 us (3.3%) | 84.5 us (3.2%) |

The owner's worst case (64 delays + 64 reverbs simultaneously) **fits** on the
Pi at both block sizes but the 128-frame figure leaves only ~19% headroom
(80.6%). **Recommended maximum: 32 simultaneous reverbs** (with per-track
delays) — 38.0% / 43.9-44.0% of the block budget; 48 reverbs is a 64-frame-only
ceiling (~57% before other inserts), and 64 reverbs is possible but not a
comfortable product limit. Reverb marginal cost measured ~9.2 us/block at 64
frames and ~26 us/block at 128 frames; delay ~2.3 us/block at 64 frames. The
skip path is unchanged: 128 configured slots on inactive tracks cost the same
as an empty engine.

**CLAP gap (named, not implemented in this wave).** Per-track CLAP **hosting**
is absent: `mnky_audio` is the lower layer (`mnky_clap` links it), so the
engine cannot own a `clap::Host`; the app-side adapter (`FxManager`/`FxSlot` in
`pi/app`) is GUI-thread-owned and currently attaches only to the retired send
buses. The engine side is ready: `setTrackFxSlotEffect(track, slot, effect)`
runs an externally owned `IEffect` per slot, forwards sample-rate changes and
honours `bypass`/`wet`; the engine persists a CLAP id and its parameter vector
verbatim and passes the strip dry while nothing is attached (tested). The
emulator's `fx-clap` stage now drives the real `mnkygain` CLAP plugin through
FX1 and measures +12 dB -> 2.204x, -24 dB -> 0.063x, so the seam itself is
proven; what is missing is product ownership: the app must keep one
`clap::Host`/adapter per track slot (up to 128) and prove its block cost. The
UI must keep the per-slot CLAP option greyed until that app wiring lands.

**Session.** `session::apply()` forwards `mixer.channels[i].fxSlots[0..1]` to
`Engine::setTrackFxSlot()` and `capture()` reads both slots back (built-in
values clamped as the DSP holds them, wet clamped); the deprecated
`mixer.channels[i].insert` is captured as the legacy FX1 view. Decoding a file
without `fxSlots` migrates the old `insert` to FX1 and the old `send[i]` levels
to each slot's `wet` (session worker's decoder); an in-memory snapshot that
only carries the old `insert` still applies it to FX1 for compatibility.

**Retired-bus test treatment.** The retirement necessarily changed three
existing checks, each updated in place to assert the new truth rather than
deleted: `test_audio.cpp` "sends and FX" (bus effect must not be called, no
return), `test_audio_features.cpp` FX pre/post source block (send levels inert),
`test_fx.cpp` engine sample-rate probe (now drives the rack FX1 instead of the
retired bus), and `test_audio_insert.cpp` placement pre-sends (FX1/FX2 proven
inert; CUE/OUT3&4 still prove the post-rack send placement). `pi/emulator`'s
`fx-clap` self-test was retargeted to FX1 with supervisor approval (test-only
change) so it tests the product path instead of the retired bus.

Verified with: `test_audio_insert` (series order by impulse/probe, delay->reverb
audibility, per-slot wet math, bypass bit-transparency, hot params, id-switch
isolation, empty/zero-wet skip, per-slot external seam, legacy insert equals FX1
bit-for-bit, sample-rate re-allocation, CPU probe) plus `test_session`
(fxSlots apply/capture and legacy end-to-end), the audio CTest suite on macOS
(11/11) and the studio Pi (11/11), `mnky_audio_bench` on the Pi (table above),
and the root CTest suite (21/21 after the supervisor-approved, test-only
emulator retarget from the retired send bus to FX1).

## DAW engine wave: REC SOURCE, region render, song clips, RENDER SONG

**REC SOURCE (`Channel::recSource`, session wire 0/1/2).** The recorder tap is
now a per-track choice of where in the live-input chain `Engine::tap()` reads.
The tap stays LIVE-INPUT-ONLY: take and clip audio never enter a recording (no
bounce-into-self; the explicit render paths below are how material is baked).

| value | tap point | what it runs |
| --- | --- | --- |
| 0 `PRE` (default) | post-gain, pre-EQ | the historical tap; `Channel::tapPoint` still selects PreGain (raw) vs PostGain (default). Bit-identical to the old engine. |
| 1 `POST` | post-EQ + fader, pre-FX-rack, pre-pan | live input through gain, the strip EQ mirrored into a dedicated `EqInsert`, then `vol`. |
| 2 `POSTFX` | post-FX rack + fader | POST plus this track's FX1 -> FX2 rack, wet included, from a dedicated tap rack. |

POST/POSTFX cannot reuse the strip's EQ/rack buffers: those carry the take/clip
voice too, and feeding the strip EQ twice would double-advance its biquad state.
The engine therefore keeps a mirrored live-only EQ per track (`mTapEq`) and,
for POSTFX, a second `TrackFxRack` (`mTapFxRack`) configured from the same slot
state. Cost is paid only by tracks that select POST/POSTFX and have a live
input; PRE runs the old loop verbatim. Built-in delay/reverb get independent tap
instances. An app-owned CLAP slot has one `IEffect*` seam, so POSTFX reuses
that instance in both chains - a named limitation, not a silent one. `setChannel`
and `setRecSource` sanitize out-of-range values to PRE. Mute/solo/pan/monitor/
master still do not affect the tap; gain, EQ, rack and `vol` do, which is the
point of the three settings. Session `MixerChannelState::recSource` is forwarded
by `apply()` through `Channel` and captured back from `Engine::recSource()`.

**RENDER REGION TO TRACK (`TakePlayer::renderRegionToTake`,
`Engine::renderRegionToTake`).** Bakes one `TakeRegion` to a new 48 kHz mono
float WAV named `NN-<name>-<NNN>.wav`: `NN` is track+1 (two digits), the name is
the app's `sanitizeTrackName` spelling (lowercase ASCII, dashes, <= 20 chars)
and the number is the next free three-digit counter for that track/name in the
directory. Offset/length/gain/fades/normalize use exactly the region-edit
resolver, so a rendered take matches what the editor previews; the normalize
peak is -1 dBFS (`kNormalizePeakDbfs`). Writes happen on the calling (control)
thread and a failed write leaves no partial file.

**SONG clip scheduler.** `Engine::setSongMode(true)` swaps the take voice to the
per-track song lanes; `false` leaves the legacy Active-take/loops path exactly
as it was (and the song readers idle). `Engine::setSongClips(track, clips)` /
`TakePlayer::setSongClips(...)` resolve beat fields to 48 kHz frames at the
engine's current tempo and start frame, sort by `startBeats`, drop `muted`
clips from the rendered lane (kept for capture), refuse an unreadable or
non-48 kHz take for the whole lane, and log/clamp overlaps. A clip's play span
ends at its region end, or at the next clip's start when `loop` repeats it in
place; a trailing loop clip plays its region once because nothing defines a
stop. The 5 ms `kSongAutoFadeSeconds` declick is applied at a span's start and
end when the clip does not set a fade; a loop seam uses the same 64-frame ramp
as take looping.

Each lane with clips owns one reader thread that fills an absolute-song-frame
ring (`kRingFrames`, ~85 ms lookahead); the audio thread only copies from the
ring. Gaps are consumed like audio, so the reader pre-produces the next clip
and a clip boundary never needs a seek. A transport jump asks the reader for a
seek and renders one aligned block of silence. Lanes are immutable objects in a
4-slot pool published by one atomic pointer; a slot is rewritten only after
`waitSongQuiescent()`, so an app edit while playing never stops the audio path
and takes effect within one ring lookahead. The mixer follows: `active` is true
while a track has a rendered lane (the clip voice owns the strip) and `playing`
only while a clip covers the position, so a gap still passes live input per the
monitor mode and an empty lane contributes silence only.

**RENDER SONG (`Engine::renderSong(path, lengthBars, out)`).** Writes a stereo
48 kHz float master WAV of `lengthBars` bars (4/4, so `bars * 4 * framesPerBeat`
frames). It is not a separate mix engine: every block goes through the ordinary
strip path with live input silent and the clip voice injected as the take voice,
so the file includes per-track gain, EQ, FX1/FX2 rack, fader, pan, destination
routing, mute/solo, master gain, the main compressor and the Main EQ exactly as
the console is set. A strip routed to OUT3&4 or Phones does not reach the main
bus and therefore is not in the file - that is the current routing, stated
honestly. Arm keeps its usual meaning (an armed strip suppresses its clip
voice). The click is forced off for the render and restored afterwards: a
metronome is not mix content. The main compressor IS included because it is a
Main-bus insert; the file is what the master output sounds like. The render is
control-thread only (audio callback stopped/paused), refuses a non-48 kHz
engine, a non-positive bar count, or a lane resolved at a different tempo
(re-apply the clips at the current tempo, exactly like `setTakeRegions`).

**Session seam (narrow exception).** `session::apply()` forwards
`mixer.channels[i].recSource` through the strip and re-applies `song.clips` per
track with `Engine::setSongClips()`; `capture()` reads `Engine::recSource()` and
`Engine::songClips()` back. `song.sections`, `song.lengthBars` and `song.grid`
stay app-owned. The engine resolves beats with its current tempo, so the app
must apply the transport tempo before (or re-apply) a loaded song. The
session.hpp function comments that still call recSource/song app-owned predate
this seam and are being updated by the session owner.

**Tests and CPU.** `test_daw_song` covers the tap matrix at all three points
(the PRE bit-identical default, EQ/fader placement, rack placement, mute
independence, and a loud take proving the live-only invariant), region renders
(offset, gain, fade ramps, normalize, naming), the scheduler (boundary timing,
5 ms declick max step, loop-in-place, muted, empty, edit-while-running with no
silent block), RENDER SONG (levels through fader/rack/pan, mute, length, click
excluded), a clip-voice CPU probe and a tap-chain CPU probe.

Studio Pi 5 (Cortex-A76, RelWithDebInfo, `nice -n 19`/`ionice -c3`, 64-frame
blocks, budget 1333 us; three runs, load 1.0-1.3 from other build users):

| scenario (audio-thread cost only) | us/block | % of 64-frame budget |
| --- | --- | --- |
| 1 clip lane | 2.1-2.3 | 0.16-0.17% |
| 8 clip lanes | 5.6-5.9 | 0.42-0.45% |
| 64 clip lanes | 35-56 | 2.7-4.2% |
| 64 clip lanes, clip boundary on every block | 36-38 | 2.7-2.9% |
| 64 live taps, REC SOURCE PRE | 11-12 | 0.8-0.9% |
| 64 live taps, POST (mirrored 4-band EQ) | 67 | 5.0% |
| 64 live taps, POSTFX + one delay per track | 149-151 | 11.2-11.3% |

The clip numbers include only the audio thread (ring copy, covering-clip lookup
and transport advance); the reader threads and disk are outside the callback
and must be budgeted separately. The boundary-every-block case is not more
expensive on the audio thread because the reader resolves the clip before the
samples land in the ring. Host (macOS arm64) numbers were 1.0-1.2 / 2.8-3.0 /
8.9-9.4 us for 1/8/64 clip lanes and 1.7 / 31 / 51 us for the PRE/POST/POSTFX
tap probes; quote the Pi table, not this host, for product limits.
