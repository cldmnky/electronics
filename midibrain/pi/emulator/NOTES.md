# emulator/ — headless backends + selftest (wave 2)

Works (verified headless on this macOS dev machine):

- `SynthBank` generates one deterministic mono voice (sine/noise/silent) per
  `audio::Engine` input channel and hands its block pointers to `process()`.
- `runScenario()` scripts a ~5 s session against the real core modules:
  `EmulatorTransport` + `PicoLink` handshake (Linked event, session id, boot
  tempo 120.00, 279 ticks, tempo echo 140.00, no LinkLost), MIDI loopback
  (injected note-on -> `nextEvent` -> `Router` -> TRS OUT 1 via `sendMidi` plus
  USB OUT, channel mask and 0xF8 pass), a 2 s two-armed take through `Engine` +
  `Recorder` (3 float32 WAVs, 96 000 frames, 0 drops), the FX send, and a WAV
  verify that parses RIFF/fmt/fact/data and checks sample content.

Stubbed / deliberate:

- `ClapEffect` is a local `audio::IEffect` -> `clap::Host` bridge (the app layer
  owns the production adapter). With no loadable module the FX stage runs a
  local `StubGain` and prints a loud `SKIP`; CTest stays green.
- No display, no JUCE, no ALSA/CoreAudio device, no RtMidi: nothing here proves
  clock accuracy, latency or hardware behaviour. Pi follow-up: same scenario on
  `LinuxSpidevTransport`, real ALSA I/O, measured tick jitter and drop rate.
