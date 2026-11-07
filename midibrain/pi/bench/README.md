# bench/ — Studio Pi hardware measurements

Repeatable, read-only bench work for the MIDI Brain Pi core: MIDI clock jitter,
the (blocked) UMC1820 round trip and the USB-gadget fact-find. `REPORT.md` holds
the measured results and the corrections they force on the project record.

Everything here runs over `ssh magnus@studio`. Nothing in this directory changes
the repository, the Pi's storage, its power, its cables or `/boot/firmware`.

## Rules these scripts keep

- The sequencer tap (`tap/midi_seqtap.c`) subscribes READ-only: it never writes a
  MIDI port, never opens a PCM or rawmidi device and leaves no client behind.
- No script starts audio work while `fuser` reports an owner on `hw:1,0`
  (`run/05-audio-latency.sh` refuses with exit 3).
- `run/03-gadget-configfs-probe.sh` loads `libcomposite` and creates a configfs
  UAC2 gadget, then unloads `usb_f_uac2`, `u_audio`, `libcomposite` and removes
  the gadget in the same run; it prints a verification block at the end.
- Long captures run in the background on the Pi and are read back by the caller;
  no call is held open for the duration.

## Run

```sh
sh run/01-tap-clock.sh 600           # 10 min clock capture + jitter analysis
sh run/02-usb-gadget-facts.sh        # read-only UDC / gadget-stack inventory
sh run/03-gadget-configfs-probe.sh   # reversible UAC2 probe, self-cleaning
sh run/04-clock-model.sh 60          # real wake-up lateness + UsbClock model
sh run/05-audio-latency.sh           # refuses while hw:1,0 is owned
```

Results land in `logs/`: the raw capture is kept gzipped, `*-summary.txt` is the
human-readable statistics and `*.json` the same numbers machine-readably.

## Reading the numbers

- `app-emit` sources are the mnkybox process's own sequencer output ports: the
  Pi's schedule, no wire.
- `hw-arrival` sources are kernel hardware ports. A DIN round trip cannot be
  shorter than ~640 µs (one byte, 31.25 kbaud, both directions), so arrivals
  under that bound are ALSA-sequencer/USB echo, not cable measurements.
- Intervals longer than 1 s are transport gaps (a parked transport), reported
  separately and excluded from jitter statistics.

## Automated clock acceptance (`--bench-clock`)

`run/06-clock-acceptance.sh quick|full` drives the review's matrix against the
real device from the app itself. `quick` spends 180 s per tempo (20/60/120/180/
300 bpm, ~15 min total); `full` spends 2 h per tempo (~10 h, the owner's session).
The app must be stopped first (SPI link and audio are exclusive) and the run
writes no project or take: the driver refuses to start when a project has
unsaved changes.

What it does, all through existing app paths:

- sets each tempo through the same link `requestTempo` path as the UI
  (`AppController::benchSetTempoX100`, full 20-300 range);
- generates note-on/note-off/CC traffic on TRS OUT1 every 250 ms and churns the
  UI tabs every 2 s (MIDI + SPI + DAW/UI load);
- samples the app-side clock observation (tick frames, Pico-stamp arrival
  lateness, `clockSkipped`, link `bad`/`retries`/`noteOffDrops`/`overruns`)
  every 50 ms;
- reads the Pico USB CDC console (`MNKYBOX_PICO_CONSOLE`, default first
  `/dev/ttyACM*`) and parses the 1 Hz `[stat]` line before/after each tempo;
- prints timestamped `[bench] MARK iso=... mono_us=... event=tempo.begin|...`
  lines for logic-analyser alignment, and a results table shaped like the
  `clock_sim` matrix.

Long run over SSH (nohup-safe; the log path is printed at start):

```sh
ssh magnus@studio 'cd ~/src/midibrain/pi/bench && nohup sh run/06-clock-acceptance.sh full >/dev/null 2>&1 &'
```

The TRS-vs-USB row sets this run feeds are in `docs/clock-bench.md`. The
`[stat]` parser has its own host check: `make bench-test` (from `midibrain/`).
