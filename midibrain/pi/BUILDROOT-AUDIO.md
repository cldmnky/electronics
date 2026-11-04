# PipeWire audio stack: app ↔ image contract

The product audio stack is **PipeWire**. The app (this tree, Qt 6 QML) talks to
it exclusively through **Qt Multimedia's PulseAudio backend** (`pipewire-pulse`
on the image). There is deliberately no JACK-API and no ALSA-direct code in
the app; ALSA remains as a local fallback only.

## What the Buildroot image must provide

- `pipewire`, `pipewire-pulse` (PulseAudio-protocol compat — this is what the
  app connects to), `wireplumber` (session manager; **required**, without it
  there is no routing, no default nodes, no policy), `pipewire-alsa`
  (for ALSA-only tools and as a fallback path).
- pipewire-jack (drop-in for external JACK clients): recommended, not required
  by the app. Inter-app routing happens in the PipeWire graph (`qpwgraph`,
  `pw-link`), not in our UI.
- Qt6 built with PulseAudio support, plus `qt6base`, `qt6declarative`, `qt6multimedia`,
  `qt6wayland` and the QML import tree the app ships against.
- **Build order (cheap now, painful later): the PulseAudio *client* development
  files must be staged BEFORE `qt6multimedia` configures.** Qt probes for the
  PulseAudio headers at its own build time and silently drops the Pulse backend
  plugin when they are absent — the result is an image where the app starts
  but no PulseAudio backend exists at runtime. Depending on the provider that
  means selecting `pipewire-pulse` (preferred: it also satisfies the runtime
  `libpulse` the plugin links against) or upstream `libpulse`/`pulseaudio` dev
  packages first, then building Qt. Verify in the finished image that the
  multimedia plugin directory contains the Pulse backend plugin
  (e.g. `libqtmultimediapulse*` under the Qt `plugins/multimedia` dir); if it
  is missing, the Qt build ran without the client lib and must be redone —
  there is no runtime fix.
- Systemd user units (or equivalent autostart) for `pipewire`, `pipewire-pulse`
  and `wireplumber`; `XDG_RUNTIME_DIR` set for the app user.
- A default sink/source policy that survives reboot (the UMC1820 class device
  when present, onboard otherwise); 48 kHz base rate.

## What the app guarantees in return

- Connects as an ordinary PulseAudio client (visible under its own app name in
  graph tools); never assumes a specific node name — it enumerates devices at
  every start and falls back to the platform default.
- Self-describes buffer needs; honors the server quantum (does not demand a
  period size like a JACK client would).
- No hard dependency on `jackd`, `pipewire-jack`, or any daemon binary name.

## Provenance

Verified in a Debian guest against real `pipewire` + `pipewire-pulse` +
`wireplumber` packages (see `pi/qemu/` notes); re-verify on the image with
`pw-cli list-objects`, `pactl list short sinks`, and an app boot log showing
the PulseAudio backend connected.
