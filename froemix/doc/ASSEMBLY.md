# Hand-assembly sheet — parts JLC does not mount

Everything in `froemix-cpl-jlc.csv` is machine-placed by JLCPCB's SMT line. This
sheet covers **everything else**: the 10 through-hole parts kept out of the CPL,
the Daisy Seed module (dual `doNotPlace`), and the bare-copper aids.

Silkscreen references: every part below has its refdes printed next to it on the
bottom silkscreen, and the board carries a mirrored legend
`HAND SOLDER: CN1-4 H1 J1 J2 / C24 C25 U16 + DAISY SEED MODULE` plus the
`FROEMIX` badge — all on the **bottom** (component) side, so they read correctly
when you flip the board over.

| Ref | Part | Package / footprint | Side | LCSC | Orientation cue |
| --- | --- | --- | --- | --- | --- |
| `CN1`, `CN2` | A2005WV-16P, 2.00 mm pin header, 1×16 | THT, 2.00 mm pitch | top | C225268 | pin 1 = **square pad** (left end, marked on silk) |
| `CN3` | HX PZ2.0-1x8P, 2.00 mm pin header | THT | top | C22438291 | pin 1 square pad |
| `CN4` | HX PZ2.0-1x7P, 2.00 mm pin header | THT | top | C22438290 | pin 1 square pad |
| `H1` | HC-PM254-8.5H, 2×6 2.54 mm right-angle (PMOD) | THT right-angle | bottom | C22373932 | body extends **+X**, toward the right board edge; pin 1 square |
| `J1` | 2×3 2.54 mm header (MIDI TRS) | THT | top | C65114 | pin 1 square |
| `J2` | 2×8 2.54 mm IDC box header (Eurorack power) | THT shrouded | bottom | C3406 | **key slot** must face away from the board edge; pin 1 square |
| `C24`, `C25` | 100 µF 50 V radial electrolytic, 8 × 12 mm | THT, 3.5 mm pitch | bottom | C2749 | **polarity**: `+` marked on silk; C25 is the negative-rail cap, its `+` faces **GND** |
| `U16` | B2405S-1WR3, isolated 24 V→5 V SIP module | THT, 4-pin SIP | bottom | C3031175 | pin 1 = the `-VIN` end; module stands **up** (≈10 mm tall) |
| `DaisySeed1` | Electrosmith Daisy Seed Rev4 module | 2 × 20 × 2.54 mm, 15.24 mm row spacing | bottom | — | pin 1 at the end marked on silk; USB port faces the **top** edge |
| `FID1–FID4` | fiducials | bare copper, 1 mm + 0.5 mm mask window | bottom | — | do nothing (no part) |
| `TP1–TP8` | test points | bare copper, 1.2 mm | bottom | — | do nothing (probe points) |

## Order of work

1. **Inspect the JLC return** — 100 SMT placements, 4 bare fiducials and 8 bare
   test points already on the board. Confirm the GND plane is visible on the
   bottom and the Daisy footprint is empty.
2. **Right-angle parts first** (they cannot be added once neighbours are tall):
   `H1` (bottom), then `J2` (bottom).
3. **Vertical THT**: `C24`, `C25` (watch polarity), `U16`, then the headers
   `CN1`–`CN4` and `J1`. Solder the two opposite corner pins of each header,
   check the header sits square, then do the rest.
4. **Daisy Seed last**, and only after the headers are flush: it is the part you
   least want to desolder. Fit it, tack two opposite corner pins, verify the
   board sits flat and the USB port lines up with the top edge, then solder all
   40 pins.
5. **Clean and inspect**: flux off, then a continuity pass on P5V / P3V3_D /
   P3V3_A / VCC / VEE / N10V_REF against the corresponding `TP` pads — every
   rail has a 1.2 mm test pad on the bottom side for exactly this.

## Notes

- **Two-sided build**: the machine-placed parts are on both faces (97 bottom,
  13 top). Handle the board by the edges; the bottom side carries the analog
  circuitry and the plane.
- **Iron settings**: 320–350 °C, chisel tip for the headers, fine tip for the
  Daisy's 2.54 mm pitch. Use leaded or lead-free per your supply; the board is
  ENIG, so either wets well.
- **Do not reflow the Daisy** unless you have a controlled profile: the module
  has its own connectors and an SD slot on the underside.
- **Heights to plan for**: Daisy ≈ 12 mm, `U16` ≈ 10 mm, `C24`/`C25` ≈ 12 mm,
  `J2` shroud ≈ 9 mm, `H1` ≈ 8.5 mm — all on the bottom, i.e. inside the rack
  cavity behind the panel. The top side is flat apart from the 2.00 mm headers
  (`CN1`–`CN4`) and the 2.54 mm `J1`.
- **Fiducials / test points are not parts**: nothing to fit, nothing in the BOM.
  If you probe them, use a fine spring pin; they are 1.2 mm bare copper.

## Verification after assembly

```bash
cd froemix
make erc netlist footprints parts   # design-side checks (unchanged by assembly)
make decoupling                     # cap -> IC routed copper distances
```

Then power up on a current-limited bench supply before fitting the euro board:
`+12 V` and `−12 V` at `J2`, expect ≈11.7 V rails after the Schottky drop, 5 V at
`TP1`, and 3.3 V at `TP2`/`TP3`.
