# audio/ — engine + multitrack recorder (JUCE-free)

Works: `Engine` applies gain, constant-power pan (`L²+R² == gain²`), mute/solo
(mute wins; any solo narrows the mix), post-fader sends 0..1, in-place stereo
`IEffect` on each send bus, return levels, then master gain. `process()` never
allocates and clamps `nch`/`n` to `kMaxInputs`/`kBlockSize`; out-of-range
`setChannel`/`setFx`/`setFxReturn` calls are ignored (safe for a blind UI refresh).
Tap semantics: each strip has `Channel::tapPoint` (default `TapPoint::PostGain`,
which is the historical `in * gain`); `TapPoint::PreGain` taps the raw device
input so a gain change is reflected only in the post-gain tap. Both are pre-pan
and independent of mute/solo/master, zeroed for channels not supplied, valid
only for the block just processed and for 0..17 (else nullptr). Sends and dry
path follow mute/solo.
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
change meter/rate without rebuilding the source; the click is not yet mixed into
`AudioGlue::renderBlock` (app team wires it).

Verified with: `c++ -std=c++20 -Wall -Wextra -Werror -I. test_audio.cpp engine.cpp recorder.cpp -o /tmp/t_audio -pthread && /tmp/t_audio`,
`test_audio_features.cpp` + `test_click.cpp` (same flags; click needs only
`click.cpp`), all exit 0 (macOS clang).

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
