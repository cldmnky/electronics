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
