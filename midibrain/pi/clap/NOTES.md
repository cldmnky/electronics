# clap/ — host core + example plugin (status)

Works (`test_clap`: 74 checks PASS, zero warnings on this Mac):
- `Host`: dlopen of a `.clap` file or bundle, factory create, init/activate/start_processing,
  per-block parameter latch, `FILE*`-stream state save/load, deactivate/destroy/deinit/dlclose.
- Missing params/state/audio-ports fail gracefully (`false` + `lastError()`); a plugin without
  audio-ports still loads, it just cannot be activated.
- Host callbacks: `clap.thread-check` (single-threaded host: both answers true), `clap.log`
  capture (`lastLog()`), `request_restart/process/callback` read-and-clear flags.
- `effects/mnkygain`: gain/drive/tone/mix stereo plugin with params/state/audio-ports.

Stubbed: no `audio::IEffect` shim (app team wraps `process(in, inCh, out, outCh, frames)`,
in-place allowed); no automation recording (plugin→host events are counted and dropped); one
main in + one main out port, ≤2 channels; `request_callback` only sets a flag (no event loop).

Header path: no `pi/third_party/clap/` yet, so builds used a CLAP 1.2.10 tree outside the repo
(`/tmp/clapinc/include`). `test/clap_stub` is a test-only ABI stand-in (never shipped); the same
test also passes against the real headers, and host+plugin were verified end to end (dlopen of
the built bundle, audio, state round-trip). Pi follow-ups: `-shared -fPIC` `.clap` build,
drive/tone CPU on the Pi 5, ALSA block sizes, repeated load/unload soak.
