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
