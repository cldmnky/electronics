---
name: connectors
description: Choose, verify and reuse interconnect parts — board-to-board headers and sockets (2.00 mm and 2.54 mm), IDC ribbons and shrouded power headers, 3.5 mm Eurorack jacks, machined vs tulip sockets, and MCU debug headers. Use when picking a connector for a board, checking that two connectors actually mate across a known gap, ordering spares, or deciding whether an equivalent part can replace a dimension-critical one.
---

# Connectors and interconnects

Interconnect parts are where a correct schematic still produces a board that
cannot be assembled. Every rule below came from a drawing or a failed fit on this
repository's boards; keep them and add to them. Stock lives in `inventory.csv`
(see `INVENTORY.md`); reusable part identity lives in `library/components.csv`.

## The one question that decides everything: what is the gap?

Two mating connectors only work if the *pin tip* reaches past the *socket
entry*, with a usable wipe. Write the arithmetic down before ordering:

```
socket entry above board A = gap - socket body height
pin tip above board A      = male shoulder + male post
insertion (wipe)           = pin tip - socket entry     -> want 2.5 .. 4.1 mm
insulator clearance        = gap - shoulder - socket body -> want >= 0.2 mm
```

Worked example (froemix base↔euro, 11.0 mm clear gap):

| Part | Numbers | Result |
| --- | --- | --- |
| XKB `X4612WV-16I` male | 6 mm shoulder + 4 mm post = 10.0 mm tip | entry 6.7, insertion **3.3 mm** ✓ |
| `HC-PM200-4.3H` female | 4.3 mm body | clearance 0.7 mm ✓ |
| Harwin `M22-251` male | 4.0 mm post + 2.3 mm body = **6.3 mm** tip | **0.4 mm short of entering** ✗ |

So "2.00 mm header, 16 ways" is not enough information. The same pitch and pin
count exist in several length classes, and the short one wastes the whole build.

## Family rules that keep being true

**IDC is always two rows.** An insulation-displacement connector folds the ribbon
over two rows of tines, so "16-pin IDC" means **2×8**, and "12-pin PMOD" means
**2×6**. The ribbon is 1.27 mm pitch. A single-row 1×16 2.54 connector is a plain
pin header, never a ribbon connector. Eurorack power cables are 2×8; the 10-pin
(2×5) ones exist and must not be bought by mistake.

**Eurorack power polarity is destructive.** Pin 1 is −12 V and is the red stripe.
Generic IDC cables have no keying that prevents reversal: meter both ends 1:1,
mark pin 1, and check the rails with the MCU out before plugging it in.

**Solder by function, not by pad number.** Jack drawings and footprints disagree
about numbering: the XKB `PJ-301M` is 1 = sleeve / 2 = tip / 3 = switch, the
Thonkiconn `WQP419GR` is 1 = sleeve / 2 = tip / 3 = ring, while footprints in
this repo have been labelled 1 = tip. Identify the sleeve with a meter (barrel or
thread = 0 Ω to the sleeve terminal) and a cable in the socket, then wire by the
symbol on the drawing. A swapped tip/sleeve on a TRS MIDI socket turns Type A
into Type B; a swapped tip/switch can disconnect the signal whenever a plug is
inserted.

**Pin order traps on jacks.** Thonkiconn-style jacks are sleeve–tip–switch
(≈3.1 / 8.3 mm); the XKB PJ-3001/PJ-301M family is sleeve–tip–switch at
3.40 / 7.90 mm, i.e. the *middle* pin differs. A footprint drawn for one family
puts the signal on the wrong pad for the other. Either move the pads or bridge
the two relevant pads per jack — and note which in the assembly doc.

**Slots beat round holes for flat tabs, but only in the right direction.** A
1.0 × 0.2 mm blade fits a 1.2 × 2.0 mm oval in either orientation; the oval's
long axis should run along the axis whose spacing is uncertain (that is what
absorbs the tolerance).

**Machined vs tulip sockets.** Machined (turned) sockets are meant for round
component leads; 0.64 mm square header pins are a force fit and wear the clip.
Use tulip/dual-wipe sockets for square pins. Machined sockets are shorter
(2.79 mm body), which makes them the right choice when a display must sit close
to a panel.

**A shrouded header's `-K` is not an ARM key.** Samtec `FTSH-…-DV-K` has a
friction-lock wall, not the Cortex-M polarisation notch; a standard ARM 10-pin
ribbon plug can foul it. Prefer the plain `-DV` unless the cable is unkeyed.

**Debug connectors, three different things.** STDC14 (2×7, 1.27 mm) on ST-LINK
V3 probes; ARM Cortex 10-pin (2×5, 1.27 mm) on targets; Tag-Connect pogo
footprints. They need adapters between them, and the Seed's `J2` is an
*unpopulated SMD* pad field for a Samtec `FTSH-105-01-F-DV(-K)`.
Debug ≠ flashing: an STM32 whose application lives in external QSPI behind a
bootloader is flashed through that bootloader (DFU), and `mass_erase` over SWD
deletes it.

**Never cut a socket to get another pin count if a ready one exists.** It works,
but the cut end has no end wall, loses retention and looks like a bodge. Prefer
the exact count; keep 40-way strips for bench work.

## Which direction the knowledge flows

1. Datasheet or drawing → the numbers (body height, pin length, pad pattern,
   thread, pin order).
2. `inventory.csv` → what is on the shelf, with the trap written in `notes`.
3. `library/components.csv` → a part identity with CAD, provenance and a
   main-shop SKU; only then may a design use it.
4. The board's `AGENTS.md` / mechanical doc → the gap and the acceptance window.

Do not skip step 1 for a dimension-critical part. "Same pitch, same pin count"
has failed on this repo three times: a too-short male header across an 11 mm gap,
a vertical shrouded header on a right-angle footprint, and a 3-pin footprint on a
solder-lug jack.

## Checklist before adding a connector to a board

- [ ] Gap, socket body height, male shoulder + post, insertion and clearance
      written out; insertion inside 2.5–4.1 mm.
- [ ] Pad pattern from the drawing: pin order, spacings, slot sizes and
      orientation, any locating pegs (NPTH).
- [ ] Mating part is in `inventory.csv` or on order, in the same length class.
- [ ] Keying/polarity cannot be reversed, or is metered and marked.
- [ ] Ingress: the tail length clears the PCB thickness; the housing clears the
      neighbour and the panel.
- [ ] Library row created (`draft` until CAD exists), inventory row linked to the
      project and reference designator.
