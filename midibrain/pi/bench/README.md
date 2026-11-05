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
