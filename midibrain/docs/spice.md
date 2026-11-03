# SPICE design checks

Run:

```sh
cd midibrain
make spice
```

The netlists are in `midibrain/spice/`. They use simplified behavioural/resistor-network models, not vendor SPICE models. They check design intent and polarity, not propagation delay, jitter, EMC, isolation voltage or thermal behaviour. No physical board was measured.

| Check | Result | Interpretation |
|---|---|---|
| MIDI OUT idle current | 0.000 mA | PNP is off while UART TX idles high |
| MIDI OUT active current | 5.558 mA | 22 Ω source + 33 Ω sink + 220 Ω receiver + 1.7 V LED model |
| MIDI OUT active ring voltage | 3.106 V | PNP source is on |
| MIDI IN loop current, active | 7.994 mA | Simplified 3.3 V source; actual current depends on the external transmitter and opto LED |
| MIDI IN receiver low | 0.660 V | Open-collector/behavioural opto output with 2.2 kΩ pull-up |
| MIDI IN idle current | 0.000 mA | No loop current |
| MIDI IN receiver high | 3.300 V | Pull-up returns to 3.3 V |
| Pi 5 V mode, VSYS | 4.706 V | B5819W model; Pi rail powers Pico VSYS |
| Pico USB mode, VSYS | 4.706 V | Pico USB rail powers VSYS |
| Pico USB backfeed into Pi | 1.000 µA | Simplified reverse leakage; no meaningful backfeed |
| Pi backfeed into Pico USB | 1.000 µA | Simplified reverse leakage; no meaningful backfeed |
| Pico RUN asserted | 0.001 V | 2N7002 pulls RUN low from Pi GPIO25 |
| Pico RUN released | 3.300 V | 10 kΩ pull-up; Pi off leaves Pico running |
| SPI buffers enabled with Pi | 3.297 V | SN74LVC1G126 OE high from Pi 3.3 V |
| SPI buffers disabled without Pi | 0.000 V | 100 kΩ pull-down forces OE low; MISO/READY outputs high-Z |

## Limitations

- The MIDI input is modelled as a resistor/LED loop plus a behavioural open-collector output. It does not model the TLP2362 threshold, propagation delay, CTR, supply current or isolation.
- The MIDI output uses a generic PNP model. It does not model the MMBT3906 saturation, thermal drift or worst-case Vf.
- The power test models the external B5819W Schottky and an ideal Pico-internal VBUS/VSYS diode. It does not model USB PD, cable drop, Pico regulator behaviour or the Pi's 5 V source.
- The reset test uses a generic NMOS with Vto = 1.5 V. The 2N7002 threshold and gate drive margin must be checked against the datasheet.
- The SPI test checks only the OE divider. It does not model SN74LVC1G126 output impedance, partial-power-down, bus contention or SPI timing.
- **No timing, clock, audio-recording, thermal, EMC or mechanical acceptance is claimed.**
