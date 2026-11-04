# Stage C2: app under Xvfb, five screens, UI-driven take (parent-run)

The delegated C2 child timed out with nothing delivered, so the parent ran
this round directly over SSH. Method and findings below — the fix loop
(`ui-fix` child) starts from here.

## Method (read this before driving the UI again)

- App window is **800×480 at root offset (112, 60)** in the 1024×600 Xvfb:
  JUCE centers it. Root-coordinate clicks MISS small controls by that offset
  (arm clicks missed by 5 px, START by 15 px — the C2 child never diagnosed
  this). Always use code-derived geometry from `RecorderScreen::resized()` /
  `MainComponent` tab bar with `xdotool ... --window <id>`: coordinates belong
  to `mousemove`, NOT to `click` (`mousemove --window $W x y click 1`).
- Proven geometry (window-relative): tabs y≈20 (root x 210/320/430/540 worked
  by width luck); arm row y=112 (x 49/136/... cell 87); START/STOP x=81 y=200.
- Better next time: run Xvfb at **800x480** so root == window coords.
- Window id: `xdotool search --name mnkybox` (was 2097164). App left running
  for the fix loop? No — killed after the take; relaunch per C1 method.

## Per-screen verdicts (screenshots/01–05, 1024×600 Xvfb)

- Mixer: renders, meters live. FAIL: 18 strips overflow — IN 09 clipped,
  MASTER overlaps the strip area, labels clipped ("ML/SO/AR"). Needs banking
  (1–9/10–18) or narrower strips; critical for the 800×480 target.
- Routing: renders, matrix + filters + transpose all legible. Minor: header
  has UTF-8 mojibake ("ROUTING Â· source Â ...").
- FX: renders, real mnkygain loaded, 4 params with values. Minor: SEND B
  placeholder has mojibake ("no plugin â¬ pick a .clap").
- Recorder: renders, live BPM/tick, 18 arms, disk/drops. Minor: small button
  + status line mojibake ("â(t", "recording take Â· ...").
- System: renders, link/clock/transport/audio/MIDI/FX status + log tail. OK.

## UI-driven take (06-recording, 07-take-done) — PROVEN

Armed ch 1+2 → START TAKE → 34.1 s → STOP TAKE, all through UI clicks:
`take-20261104T032956Z` = ch00/ch01 mono + master stereo, 48 kHz float32,
rms 0.021/0.001/0.045 (ch1 is emulator noise at 0.015 by design, not a bug),
drops 0, bar:beat counter live (REC 003:2:10), take list entry live.

## Fix list for pi/app (no app-code fix yet — this stage was measure-only)

1. Mixer banking/overflow at ≤1024 px: strips must fit or bank; MASTER must
   not overlap strips; no clipped labels at 800×480. (`screens/MixerScreen.*`)
2. UTF-8 → ASCII in all UI strings: grep `[^ -~]` under pi/app; known spots
   are the routing header, recorder button/status, FX placeholder. JUCE/X11
   font fallback mangles these codepoints on the Linux target.
3. Recorder take list goes stale after stop (row showed 00:00/0 B while the
   status line correctly showed 3 files/00:34): rescan on stop.
   (`screens/RecorderScreen.*` takeStateChanged/scanTakes path.)
4. Test loop must run Xvfb at 800x480 and click `--window`-relative from
   code-derived geometry (this file's Method section).

## Open risks

- Touch input itself (tap vs click) is untested — XTEST clicks only.
- 800×480 target layout unverified (all shots are 1024×600).
- Audio/MIDI timing unmeasured, as before.
