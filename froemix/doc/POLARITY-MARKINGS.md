# Froemix Rev 0.2 — base-board polarity markings

Rev 0.2 adds bottom-side assembly markings to the base PCB: a dot at physical
pin 1 of U1, U2, U3, U11, U12, U13, U14, U15, Q1 and Q2; a separate bar on
the cathode side of D1, D3, D4 and D5. Board coordinates are maintained in
`tscircuit/lib/polarity-markers.ts`. The base regression test checks the built
marks, their association with physical pad 1, and clearance to pads, vias and
holes. These checks are not a substitute for reviewing the final manufactured
silkscreen and the assembly preview.

The shared product revision printed on the euro PCB and panel also advances to
0.2, so the set has one consistent identity. Their electrical circuits and
mechanical geometry are unchanged. The base change is silkscreen only; the
existing design, routing and component choices are unchanged.

**Existing Rev 0.1 production order:** do not replace its files or assume it
contains these new marks. For any future order, regenerate and review the entire
release and JLC placement preview together, including polarity for every
polarized part. Markers are an assembly aid, not an instruction to place a part
from the 3D view. The current imported 3D models can show incorrect pin-1 dots.
CAM may clip ink near solder-mask openings; inspect the final fab output and
confirm critical markings survive the manufacturer's processing.
