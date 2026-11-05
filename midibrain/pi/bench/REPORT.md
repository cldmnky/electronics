# Studio Pi bench: MIDI clock jitter, audio latency, USB-gadget facts

Read-only bench session against the Studio Pi 5 (`ssh magnus@studio`), 2026-11-05
22:28–23:20 CET. Everything under `midibrain/pi/bench/` is new; no other file in
the repository was modified, nothing was committed, and the Pi's storage, power,
cables and `/boot/firmware` were not touched.

Machine: Raspberry Pi 5 Model B Rev 1.1, 16 GB, Debian 13 trixie,
`6.18.34+rpt-rpi-2712`. Live application during the run:
`/home/magnus/bin/mnkybox --no-pico` (PID 39904, started 22:25 from a terminal,
Qt UI up). That process held `hw:1,0` exclusively for the whole capture window.
It exited and was restarted by the operator as PID 40935 at 23:05, after which
PipeWire (PID 1315) held `hw:1,0`.

## 1. What is actually connected (correction to the record)

`lsusb`, `/proc/asound/cards`, `/dev/snd/by-id`, `amidi -l`, `aconnect -i -o`:

| card | seq client | device | USB MIDI port |
|---|---|---|---|
| 0 | 16 `CRAVE` | Behringer CRAVE | `midiC0D0` |
| 1 | 20 `UMC1820` | Behringer UMC1820 | `midiC1D0` |
| 2 | 24 `Play` | Polyend Play | `midiC2D0` |
| 3 | 28 `drumlogue` | Korg drumlogue | `midiC3D0` |
| 4 | 32 `minilogue xd` | Korg minilogue xd | `midiC4D0` (+ `C4D1`) |

**Correction: the UMC1820 does expose MIDI.** `lsusb -v -d 1397:0503` shows
interface 4 with `bInterfaceClass 1` (Audio), `bInterfaceSubClass 3` (MIDI
Streaming) and two bulk endpoints of 512 bytes, and the kernel creates
`/dev/snd/midiC1D0` plus sequencer client 20 `UMC1820 MIDI 1`. So the earlier
claim of a MIDI port on the UMC1820 is supported at the USB level. What cannot
be claimed from this session is the *connector type or the cable* — that needs
eyes/hands on the unit, and this task forbids touching cables.

**But the UMC1820's MIDI IN is not wired to anything.** `/proc/asound/card1/midi0`
reports `Input 0 / Rx bytes : 0` for every sample taken across two hours, while
the same file's Tx counter climbed at 48 B/s. A loopback through the UMC is
therefore impossible today, independent of the audio-device conflict below.

## 2. MIDI clock jitter (acceptance item: ≤1 ms TRS clock error)

### Measurement path that actually exists

No Pico is attached, so the product's TRS jacks are absent and **the ≤1 ms TRS
acceptance item cannot be measured on this bench at all**. What can be measured
is the Pi-side USB clock mirror, and — because the live app fans every byte out
to every open output port while already-cabled device loops exist — the *arrival*
side of some of those bytes.

Tool: `tap/midi_seqtap.c`, a read-only ALSA-sequencer tap. It subscribes
(READ only) to every exported source port, timestamps each event with
`CLOCK_MONOTONIC` immediately after `snd_seq_event_input()`, and writes CSV.
It never writes to a port, never opens a PCM/rawmidi device and leaves nothing
behind when it exits. That is what made it safe next to the operator's running
app: no injected bytes, no routing changes.

Capture: 600 s, 441,692 events (`logs/06-tap-clock-600s.csv.gz`,
1.9 MB gzipped; summary in `logs/07-tap-clock-600s-summary.txt`, machine-readable
`logs/06-tap-clock-600s.json`). The Pi-side full CSV stayed in
`/tmp/midibrain-bench/tap/clock10m.csv` and is not committed.

### Result A — the app's own emission schedule (real timestamps, 13 output ports)

Each of the 13 app output ports emitted 23,655 clock bytes over the 493 s the
transport was rolling (the other 107 s were a parked transport, see below).

| statistic | per-port value |
|---|---|
| interval min / avg / max | 11.8 / 20.833 / 29.8 ms |
| \|interval − nominal\| avg | 540–585 µs |
| \|interval − nominal\| p95 | 967–1244 µs |
| \|interval − nominal\| p99 | 2.75–3.3 ms |
| \|interval − nominal\| max | 8.9–9.1 ms |
| mean-interval tempo error | +0.0004 % (limit ±0.1 %) |
| fan-out spread per burst (same byte across 13 ports) | avg 181 µs, p95 374 µs, max 6.26 ms |

**The ≤1 ms criterion is not met by the USB mirror.** p95 is 0.97–1.24 ms
depending on the port and the tail is an order of magnitude over budget.
The cause is visible in the source: `LinkClient::runNoPico()` emits from a 1 ms
poll loop (`app/link_client.cpp:353`), so a clock deadline can only be observed
on a 1 ms grid, and Qt/UI contention adds wake-up lateness on top. Tempo
accuracy is not the problem; scheduling granularity is.

### Result B — attribution of the arrival streams (why only one is a wire path)

| kernel port | clock bytes | arrival − burst start (min/avg/p95/max) | reading |
|---|---|---|---|
| 14:0 `Midi Through` | 23,652 | −1982 / −11 / −2 / 0 µs | software loopback (ALSA port echo) |
| 16:0 `CRAVE` | 23,655 | 0 / 162 / 223 / 5834 µs | sub-frame, so **not** a DIN round trip (one byte needs ≥320 µs at 31.25 kbaud); sequencer/USB echo |
| 28:0 `drumlogue` | 23,655 | 8 / 1890 / 2823 / 7637 µs | clock count equals the app's emission count exactly ⇒ these bytes are the app's, returned through a device path |
| 32:1 `minilogue xd` | 28,753 | 12 / 4778 / 5425 / 10708 µs | kept clocking at 48/s through the app's 107 s stop ⇒ the device's own clock, **not** the app's bytes |

Only the drumlogue path is a genuine app-byte round trip (≈1.9 ms average,
p95 2.8 ms). Its cable topology cannot be established without touching cables,
and it is not the product's TRS path, so it is reported as context, not as the
acceptance measurement. The sub-frame CRAVE and Midi Through streams are
excluded precisely because no MIDI wire can be that fast.

### Result C — transport gap, not jitter

One 107.2 s hole appears in every app stream. It is a deliberate transport stop:
`0xFC` (STOP) at t+42.9 s, two more STOPs at t+123.9/124.3 s, `0xFA` (START) at
t+150.1 s. Gaps above 1 s are excluded from the interval statistics above and
reported separately in the JSON.

### Result D — `UsbClock::latencyUs()`

In `--no-pico` mode the app sets `status_.latencyUs = 0.0` unconditionally
(`LinkClient::pumpNoPicoClock()`, "free-run has no Pico stamp, so the discipline
diagnostics have no value"), so the measured distribution over the 600 s run is
exactly {0.0}. It cannot be exercised without a Pico.

`model/clock_model.cpp` therefore measures the two things that *can* be
measured on this hardware and then runs the repository's own `UsbClock`
(`midi/clock.cpp`) against the measured lateness. Label: the wake-up numbers are
real, the `UsbClock` numbers are model-internal.

| measurement (60 s, live app running) | n | avg | p95 | p99 | max |
|---|---|---|---|---|---|
| absolute 20.833 ms deadline wake lateness | 2,880 | 55.8 µs | 60 µs | 75 µs | 868 µs |
| 1 ms poll-cadence wake lateness | 60,000 | 49.1 µs | 55 µs | 60 µs | 4718 µs |
| model free-run emission late vs its own deadline | 2,876 | 56 µs | 73 µs | 133 µs | 420 µs |

The kernel timer and the model are at 50–70 µs; the app's measured 0.5–1.2 ms p95
and 9 ms max are application-level, not kernel-level.

## 3. Audio latency (optional): BLOCKED by the live audio owner

Not measured, and no tone was played. Evidence:

- 22:28–22:52 — `fuser -v /dev/snd/pcm*` shows PID 39904 (`mnkybox`) holding
  `pcmC1D0c` and `pcmC1D0p`; `/proc/asound/card1/pcm0p/sub0/status` `owner_pid`
  pointed at the app.
- from 23:05 — PipeWire PID 1315 holds both UMC PCM devices (`state: RUNNING`,
  `owner_pid 1315`) with the restarted `mnkybox` (PID 40935) on top.
- `amidi`/rawmidi opens on `hw:0..4` also fail with `Device or resource busy`
  because the sequencer kernel client owns those substreams.

The hard rule for this task is not to disturb an audio device another process is
using, so round-trip measurement and the "is a TRS loopback cable plugged in?"
tone test were both skipped. `run/05-audio-latency.sh` refuses to run while any
process holds `hw:1,0`; that guard path is the only part exercised here.

## 4. USB gadget fact-find: the hardware cannot do it

Evidence chain:

1. `/sys/class/udc` is empty (0 entries) at every sample, before and after the
   probe run (`logs/06-usb-gadget-facts.log`, `logs/09-gadget-configfs-probe.log`).
2. The device tree has no USB peripheral controller node — only `rp1_*` nodes
   plus the PCIe-attached RP1 xHCI host (`logs/07-usb-gadget-dt-configtxt.log`).
3. `/boot/firmware/config.txt` enables OTG only for other boards:
   `[cm4] otg_mode=1` and `[cm5] dtoverlay=dwc2,dr_mode=host`. Neither section
   applies to a Pi 5 Model B, so no peripheral controller is configured. The USB-C
   port on this board is the power input; it was not touched.
4. The kernel is not the obstacle: `CONFIG_USB_GADGET=y`,
   `CONFIG_USB_LIBCOMPOSITE=m`, `CONFIG_USB_CONFIGFS=m`,
   `CONFIG_USB_CONFIGFS_F_UAC2=y`, `CONFIG_USB_F_UAC2=m`, `CONFIG_USB_DWC2=y`
   (dual role), `CONFIG_USB_DWC3=y`.
5. Reversible probe (`run/03-gadget-configfs-probe.sh`): with `libcomposite`
   loaded and configfs mounted, a UAC2 gadget tree was created successfully
   (`functions/uac2.bench`, `configs/c.1`, 48 kHz, 2 capture + 2 playback
   channels) and binding failed exactly as predicted — no UDC exists to accept a
   name. Teardown verified: gadget directory gone, `usb_f_uac2`, `u_audio` and
   `libcomposite` unloaded, `/sys/class/udc` unchanged (configfs was already
   mounted by systemd since 2026-10-07 and was left alone).

**Verdict:** a 2-in/2-out class-compliant USB audio device is not achievable on
the Pi 5's own ports by any software configuration. `docs/USER-MANUAL.md` §15
("class-compliant 2-in/2-out USB audio device on its data port") does not
describe this hardware; it needs a separate USB-audio codec on the base board or
an amendment to the manual. That is a product decision, not something this bench
run may decide.

## 5. Incident observed during the session (not caused by this work)

At kernel time 90164 s (≈22:52, about seven minutes after the capture ended) the
minilogue xd (`usb 3-1.4.3`) logged a flood of `urb status -32` and then
`USB disconnect, device number 9`; card 4 disappeared and did not return
(`logs/08-app-exit-and-device-state.log`). The tap had already exited, it never
writes to a port, and no cable or power was touched here. The app instance under
measurement (PID 39904) also exited; the operator started a new one at 23:05.

Separately worth recording: with the app fanning every input to every output, an
already-cabled MIDI loop turns the app into a MIDI Thru and re-injects its own
clock (card 0 and card 2 Rx counters rose faster than the 48 B/s emission rate).
`docs/USER-MANUAL.md` §17 says "Thru is not provided" — true for the Pico's TRS
routing, but the USB fan-out behaves like one when a cable loops back.

## 6. Files

```
bench/tap/midi_seqtap.c          read-only ALSA seq tap (build: gcc -O2 ... -lasound)
bench/tap/clock_jitter.py        interval / jitter / burst-attribution statistics
bench/model/clock_model.cpp      real Pi wake-up lateness + UsbClock model probe
bench/run/01-tap-clock.sh        repeat the tap capture (default 600 s)
bench/run/02-usb-gadget-facts.sh read-only gadget/UDC/kernel-config inventory
bench/run/03-gadget-configfs-probe.sh reversible configfs UAC2 probe + teardown
bench/run/04-clock-model.sh      build and run the model harness
bench/run/05-audio-latency.sh    guarded round-trip measurement (refuses while busy)
bench/logs/                      raw logs, capture summary and JSON
```

Reproduce:

```sh
sh run/01-tap-clock.sh 600      # capture + analysis into logs/
sh run/03-gadget-configfs-probe.sh
sh run/04-clock-model.sh 60
```

## 7. Limits of these results

- Nothing here is a TRS/clock measurement of the product: the TRS hardware is on
  the Pico, which is not attached. The ≤1 ms acceptance item stays **pending**.
- The capture ran against a live operator session (Qt UI, transport stops,
  audio device in use, a USB device unplug). That is realistic load, not a quiet
  bench, and it is not reproducible on demand.
- Arrival attribution depends on clock-count and sub-frame reasoning, not on a
  known cable map. The drumlogue round trip is real but its path is unproven.
- The audio round-trip measurement was never executed; `run/05-audio-latency.sh`
  has only been exercised in its refusal path.
- The `UsbClock` numbers from `clock_model` are model output fed with measured
  latency, not Pico behaviour.
