# audio/ — engine + multitrack recorder (JUCE-free)

Works: `Engine` applies gain, constant-power pan (`L²+R² == gain²`), mute/solo
(mute wins; any solo narrows the mix), post-fader sends 0..1, in-place stereo
`IEffect` on each send bus, return levels, then master gain. `process()` never
allocates and clamps `nch`/`n` to `kMaxInputs`/`kBlockSize`; out-of-range
`setChannel`/`setFx`/`setFxReturn` calls are ignored (safe for a blind UI refresh).
Tap semantics: `tap(ch)` is post-gain mono (`in * gain`), pre-pan and independent
of mute/solo/master, zeroed for channels not supplied, valid only for the block
just processed and for 0..17 (else nullptr). Sends and dry path follow mute/solo.
Works: `Recorder::writeBlock()` copies into a 128-block mutex queue and never
touches disk; a background thread drains it. Full queue = counted drop.
Each take is `<dir>/take-<utc>/` with `chNN.wav` per armed input plus stereo
`master.wav`: float32 RIFF/WAVE, format code 3, `fmt `+`fact`+`data`, sizes patched
at close. `armMask == 0`, a second concurrent take, or a failed open returns false.
Not implemented (Pi follow-ups): ALSA/UMC1820 device capture, S/PDIF, Pi pinning,
48000 resample from 44100, disk-full policy (writes are best-effort), >4 GiB/file
(2 h master is ~2.8 GB, inside the RIFF limit but unverified), and true lock-free
queueing. Queue-overflow drops are counted but not deterministically tested.
Verified with: `c++ -std=c++20 -Wall -Wextra -Werror -I. test_audio.cpp engine.cpp recorder.cpp -o /tmp/t_audio -pthread && /tmp/t_audio` (macOS clang, exit 0).

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
