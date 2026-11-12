# SPICE design checks

Run:

```sh
cd midibrain
make spice
```

The netlists are in `midibrain/spice/`. They use simplified behavioural/resistor-network models, not vendor SPICE models. They check design intent and polarity, not propagation delay, jitter, EMC, isolation voltage or thermal behaviour. No physical board was measured.

| Check | Result | Interpretation |
|---|---|---|
| MIDI OUT idle current | 0.000 mA | Generic PMOS off while UART TX idles high (simulation) |
| MIDI OUT active current | 5.087 mA | 33 Ω ring + 22 Ω tip + 220 Ω receiver + 1.9 V LED model (simulation) |
| MIDI OUT ring voltage, idle | 1.900 V | Ring pulled to the 1.9 V receiver model with the PMOS off (simulation) |
| MIDI OUT ring voltage, active | 3.131 V | Generic PMOS on: UART low holds the gate at 0.143 V (simulation) |
| MIDI IN loop current, nominal 5 V sender | 4.906 mA | Through the 220 Ω receiver resistor and the LED model; the weak-sender case is 4.248 mA (simulation) |
| MIDI IN receiver low | 0.000 V | Behavioural opto output held low with a 2.2 kΩ pull-up (simulation) |
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

- The MIDI input is modelled as a resistor/LED loop plus a behavioural opto output. It does not model the TLP2361 threshold, propagation delay, CTR, supply current or isolation.
- The MIDI output is a level-1 generic PMOS fitted to the AO3401A datasheet DC numbers (VTO -0.9 V, RDS(ON) about 85 mΩ in the linear region), NOT the AOS model: `spice/midi_out.cir` says so in its own header. It does not model gate charge, switching edges, temperature, the body diode or worst-case Vf. The netlist uses 33 Ω on the ring and 22 Ω on the tip, the values on the board.
- Every number in this table is a SIMULATION RESULT from the run logs (for example `midi_out.log`: i_loop_active 5.087e-3 A, v_ring_active 3.131 V, v_gate_active 0.1435 V). None is a hardware measurement.
- The power test models the external B5819W Schottky and an ideal Pico-internal VBUS/VSYS diode. It does not model USB PD, cable drop, Pico regulator behaviour or the Pi's 5 V source.
- The reset test uses a generic NMOS with Vto = 1.5 V. The 2N7002 threshold and gate drive margin must be checked against the datasheet.
- The SPI test checks only the OE divider. It does not model SN74LVC1G126 output impedance, partial-power-down, bus contention or SPI timing.
- **No timing, clock, audio-recording, thermal, EMC or mechanical acceptance is claimed.**
