# USB feedthroughs and strain relief

The three front USB ports stay **panel-mounted feedthrough cables**, not USB receptacles soldered to the front board. The front board is now a support structure: its top edge has a U-shaped opening for the feedthrough bodies, two bracket mounting holes, and a 3D-printable bracket for cable strain relief.

## Front panel layout

- LCD lowered to centre **y = 0 mm**; glass top **+42 mm**, bottom **−42 mm**.
- USB feedthrough centres at **y = 56 mm**:
  - USB-C power: x = −48 mm
  - UMC/audio USB-A: x = −13 mm
  - MIDI-hub USB-A: x = +22 mm
- Transport buttons at **y = −49.5 mm**, below the display.
- The LCD is mounted on the front board, not in a PCB cutout. The 3.5-inch glass occupies roughly y = −24…32; the USB relief notch runs from y = 48 to the board top edge at y = 56. The remaining 16 mm bridge (32–48) keeps the board connected.

## Selected parts

| Function | Candidate | Status |
|---|---|---|
| USB-A ×2 | [Adafruit 908](https://www.adafruit.com/product/908), [drawing](https://cdn-shop.adafruit.com/product-files/908/USB+A+Female.pdf) | USB 2.0, 340 mm. Flange 38.30 × 11.90 mm; opening **14.65 × 7.10 mm**; 2 × Ø3.5 on **30 mm** centres. |
| USB-C power | [SparkFun CAB-15455](https://www.sparkfun.com/products/15455), USB-C F panel → USB-C M | 305 mm, USB 2.0, **3 A**. Flange 26.4 × 8 mm; panel slot **12.6 × 7.6 mm**; two Ø3.2 on **18 mm** for a clamp. **Not 5 A.** |
| Internal USB-C cable | 0.3 m USB-C male-to-male, **5 A e-marked** | Exact MPN TBD. Keep it short to limit voltage drop. |
| Bracket | 3D-printed PETG `mechanical/feedthrough-bracket.stl` | 105 × 21 × 3 mm envelope, three 18 mm slots, two M2.5 mounting holes, four cable-tie holes. |
| Bracket hardware | 2 × M2.5 × 8 mm screws/nuts; 4 × 2.5 × 100 mm cable ties | Hand assembly. |

The Adafruit 4218/6069 round USB-C panel cables are **not** confirmed 5 A; do not use them for Pi 5 power without a current/PD test. Neutrik NAUSB-C is a documented 5 A feedthrough but its D-series flange is ~31 mm tall and does not fit the 14 mm top strip; using it would require moving the LCD/controls.

## Panel cutouts

Provisional, to be checked against purchased samples:

- USB-A, each: **14.65 × 7.10 mm** opening, 2 × **Ø3.5 mm** holes **30 mm** apart (Adafruit 908 drawing).
- USB-C: **12.6 × 7.6 mm** slot for SparkFun CAB-15455; two Ø3.2 holes on **18 mm** centres for the clamp. This cable is **3 A**, not 5 A.
- Leave at least 25–35 mm behind the panel for the cable tails and bend radius. The SparkFun 3 A cable cannot meet the Pi 5 5 A target; test or replace.
- Mount the feedthroughs from the front, with the bracket behind them; use the bracket's cable-tie holes to secure the tails before they reach the Pi.

## Front board changes

- Top relief notch: 86 × 8 mm at x = −12, y = 52 (opens to the top edge only; no display cutout).
- Bracket holes: (−60.5, 46.5) and (40, 46.5), Ø2.7 mm.
- The board has a top-edge opening for the USB feedthroughs; the 3D bracket bridges it. The LCD mounts on the solid front board at the asymmetric 58 × 49 mm holes.
- All USB signals remain in the panel feedthrough cables. No USB high-speed routing or USB-C PD breakout is on the PCB.

## Verification still required

- Buy one USB-C 5 A coupler and two Adafruit 908 cables; measure the actual body depths and panel cutouts.
- Print the bracket, fit it to the front board, and check that it clears the LCD rear/DSI cable and the front-panel rail.
- Test the Pi 5 with the official 5 V/5 A supply and the complete feedthrough/cable chain; check `vcgencmd get_throttled` under load.
- Do not order the panel or bracket until the sample fit and 5 A test pass.
