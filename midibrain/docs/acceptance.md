# Verification and acceptance gates

## Current automated checks — design/software only

`make test` typechecks and builds/routes both prototype boards, checks display/control centre relationships and Pi hole-grid pitch, executes a host C++ framed-codec and synthetic switch debounce test, and runs simplified ngspice checks for MIDI input/output loop current and polarity, Pi/Pico power backfeed, reset and SPI-buffer enable. The SPICE models are behavioural/resistor-network checks, **not** datasheet-accurate or hardware measurements. Centre/box checks do **not** prove physical component envelopes, cable fits or thermal performance. No fab release is produced. Lockfile and exact direct tool dependencies support reproducibility; install using `bun install --frozen-lockfile`.

## Required before committing to parts / manufacture — not yet passed

- Validate the HDMI/USB display candidate with actual Pi 5, OS/kernel and display revision; retain SPI and verify display pogo-pin isolation, power and cable routing.
- Exact manufacturer mechanical and electrical drawings for every component and cable; keying, mating-view pinouts, rail hole positions and component tolerances; prototype/measure unfamiliar parts.
- Complete 3D envelope, top/rear/side views, board/support locations, display mounting, cooler/airflow, external and internal plugs and cable bend radii. Confirm 26HP/3U panel, 20HP base board and ≤110 mm installed depth without interference and with service access.
- Schematics for both optoisolated MIDI inputs, two MIDI drivers, power, programming isolation, controls, connectors and status; verify CA-033 current, polarity, pin mappings, off-state behaviour and worst-case components. ERC, pin/net tests, PCB route/DRC and independent schematic review.
- Enumerate exact BOM/assembly assignments, sourcing and substitutions, polarity marks, test points, clearances, silkscreen, soldering access and PCB stack. Review generated Gerbers, drill and CPL against intended factory capabilities and physical first article.
- Full-power USB-C supply and feedthrough tests: 5 A/PD negotiation as applicable, both orientations, drop/thermal/strain relief, no unintended backfeeding with Pi, Pico USB or display separately powered. Design and measure any backup supply; ≤1 s recording-loss target is not yet guaranteed.

## Hardware / system acceptance — not yet performed

- Continuity, isolation and current-limited first-power bring-up; all GPIO/reset sequencing and Pico USB-power cases; MIDI Type-A wiring and current-loop fault protection.
- Measured clock timing error/jitter and tempo across operating tempos and under CPU, disk, USB and UI load, with traceable measurement setup and error statistics. Test start/stop, routing/filter/merge, simultaneous port load and disconnect/recovery.
- Two-hour 18-channel 48 kHz / 24-bit continuous capture at load, zero xruns and bounded queues; verify all channel contents and file duration. Feed 16 synchronized analog inputs via UMC1820/ADA8200 ADAT plus synchronized S/PDIF stereo. Document master clock/source, cabling, ALSA channel map and clock-lock checks. Verify USB playback, device loss, metadata durability, RF64/segmentation and storage-full/restart handling. No software demo yet implements this recorder.
- Thermal soak and NVMe endurance/throughput in the closed rack at worst-case ambient; verify cooling noise/airflow and SSD stability. Physical controls, touch alignment, cable retention, depth, rail mounting and all assembly steps must be tested with first article.
