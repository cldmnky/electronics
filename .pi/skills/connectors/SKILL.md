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

## Verified jack pin maps (2026-11-13)

The owner decision is that the **PCB-mountable Thonkiconn family is the
preferred 3.5 mm connector**: `WQP518MA` (mono) for audio and CV, `WQP419GR`
(stereo) for TRS MIDI. Both are M6 × 0.5, so they suit the 6.2 mm panel hole.
What is verified about the pins, and what is not:

| Part | Drawing says | Verified pad geometry | Status |
| --- | --- | --- | --- |
| `WQP518MA` mono | 1 = sleeve, 2 = tip, 3 = switch | KiCad official `Jack_3.5mm_QingPu_WQP-PJ398SM_Vertical`: **S** at 0, **TN** at 3.1, **T** at 11.4 mm from the sleeve pad, barrel 6.48 mm in front of S. Sleeve nearest the panel, tip farthest | both projects match: froemix `FP_JACK_MONO` pad +6.48 = sleeve, +3.38 = switch, −4.92 = tip, barrel at origin ✓ |
| `WQP419GR` stereo | 1 = sleeve, 2 = tip, 3 = ring | **verified** via clacktronics/AudioJacks `Jack_3.5mm_QingPu_WQP-PJ366ST_Vertical`: S 6.48 / R 3.38 / T −4.92 mm from the barrel datum, all collinear, so the **middle pad is R** (on the mono it is TN). The same file set gives the mono the same three positions with TN in the middle | froemix and midibrain both place these pads already; the defect was the *pin numbering*, not the geometry. Label the pads S/R/T, not 1/2/3 |

The rule, stated once so it is not re-derived: the three pads are **collinear**,
**3.10 mm apart at one end and 8.30 mm at the other**, with the **sleeve at
6.48 mm from the barrel datum**, the **outermost pad**, the middle one being TN
(mono) or R (stereo) and the tip at 4.92 mm on the other side of the datum.
Sleeve is GND on every jack in both projects. (An earlier note in this file
claimed the sleeve was 3.1 mm off-axis; that was a misreading of Thonk's
drawing - the 3.10 there is the sleeve-to-switch spacing. Both Clacktronics
footprints agree with this and are the authority.)

For a jack of unknown provenance, settle it with the part in hand, not from the
drawing:

1. Meter the **barrel/thread** to each pad: the sleeve reads 0 Ω.
2. Plug in a TRS cable: plug **tip** to a pad (0 Ω), plug **ring** to a pad,
   plug **sleeve** to the sleeve pad. That identifies all three.
3. Only then decide whether the footprint or the netlist is wrong: on a board
   already at fab the copper is fixed, so the fix is wiring by function and a
   documented rev-B pad swap; on a board still in design, fix the footprint's
   `portHints` and its consumers together and re-run the project gate.

## Jack CAD status

CAD is in place (2026-11-13): `library/cad/jack-35-wqp518ma/` and
`library/cad/jack-35-wqp419gr/` each hold the Clacktronics KiCad footprint and
STEP model (MIT, (c) 2021 Barwise) with a `SOURCE.md` carrying the URL, the
pinned commit, the SHA-256 and an explicit "upstream says untested" caveat.

Thonk publishes the family datasheet (verified downloadable, 2026-11-13):
`https://www.thonk.co.uk/wp-content/uploads/2014/02/Thonkiconn_Jack_Datasheet.pdf`
— mono "PJ301M-12", one page, with the PCB layout, the 3 mm barrel-hole note and
an Eagle library link. The stereo page carries no drawing or 3D file.

Neither Thonkiconn has a footprint or STEP in `library/cad/` yet, so their
library rows stay `draft` and cannot be promoted to `preferred`: the gate needs
`footprint`, `step`, `cad_source`, `cad_license` and a **main-shop** SKU
(Electrokit / DigiKey / Conrad — Exploding Shed is not one). Note that KiCad's
official footprint references
`Connector_Audio.3dshapes/Jack_3.5mm_QingPu_WQP-PJ398SM_Vertical.step`, which
is **not shipped** with KiCad, so there is nothing to copy locally. Get the
model from the manufacturer or from Thonk's published CAD, record licence and
SHA-256 in `SOURCE.md`, or derive one from the drawing and say so explicitly -
never place a model whose origin, axis and body are unverified.

### Known 3D-model sources that do not fit

`https://github.com/elektrofon/eda-3d-models` (MIT, Elektrofon 2021) is worth
knowing about and worth checking before commissioning CAD, but as of 2026-11-13
it holds only:

- `WQP-PJ398SM.FCStd` - the **collinear** family member, in FreeCAD's native
  format. tscircuit needs STEP/OBJ, so it must be exported with FreeCAD first,
  and its pad layout is *not* the Thonkiconn layout (sleeve off-axis).
- `WQP-PJ399SM.blend` - a Git-LFS pointer (68 MB object), not a model.

So it cannot supply the preferred part's model. A valid STEP still has to come
from the vendor, from Thonk, or from a derivation made from the drawing and
labelled as such.
