# Printed front-panel parts: NAV keytop and value knob

Two 3D-printed parts for the MIDI Brain front panel, generated from parametric
Python: an **NAV keytop** for the ALPS Alpine SKRHABE010 5-way switch (U5) and a
**value knob** for the Bourns PEC11R-4015F-S0024 encoder (U6).

Status: **PROVISIONAL printed parts, not released drawings.** Every fit number
below is either sourced (datasheet / `lib/layout.ts` / `mechanical/layout.json`)
or an explicit ASSUMPTION that the first article has to measure. Nothing here is
an acceptance measurement, and nothing here changes a PCB or panel rule.

```
mechanical/printed/
  printed_geometry.py     pure geometry + the design rules + the STL inspector (no CAD)
  nav_keytop.py           CadQuery builder for the keytop -> nav-keytop.stl / .step
  knob.py                 CadQuery builder for the knob   -> knob.stl / .step
  test_printed_parts.py   the rules, their mutation tests and the artefact checks
  nav-keytop.stl/.step    generated, committed
  knob.stl/.step          generated, committed
  README.md               this file
```

## How to build, test and regenerate

```bash
# CadQuery is NOT in the system python; use a local venv (never the system one)
uv venv /tmp/cq-venv
uv pip install --python /tmp/cq-venv/bin/python cadquery

# regenerate both artefacts (each run rebuilds, self-validates, exports and re-reads)
/tmp/cq-venv/bin/python mechanical/printed/nav_keytop.py
/tmp/cq-venv/bin/python mechanical/printed/knob.py

# tests: the pure-geometry and STL checks run anywhere, the rebuild tests need the venv
cd mechanical/printed && python3 -m unittest test_printed_parts -v
cd mechanical/printed && /tmp/cq-venv/bin/python -m unittest test_printed_parts -v
```

`make test` does not pick this directory up (it is not a package and the suite is
standalone); run the commands above.

The four generated artefacts are committed even though the repository-wide
`.gitignore` excludes `*.stl` and `*.step`: the test suite re-reads these exact
files, so a checkout without them cannot run the artefact and rebuild checks.
They were added with `git add -f`; regenerating them rewrites the files (the STEP
header carries a creation timestamp), and the tests compare geometry, not bytes.

## Sources

| input | value | source |
| --- | --- | --- |
| ALPS SKRHABE010 body height above the PCB | 1.8 mm | SKRH series catalog, product-list row "7.35 x 7.5 x 1.8" (SHA-256 `83a37124...`, "Update 2510") |
| ALPS overall height above the mounting surface | 5.0 mm | same catalog, drawing No.1 side view |
| ALPS stem diameter | 3.0 mm | same catalog, drawing No.1 |
| ALPS "keytop fitting area" | 1.7 mm max | same catalog, drawing No.1 vertical callout |
| ALPS travel (4-direction / centre push) | 0.25 / 0.15 mm | same catalog, drawing No.1 |
| PEC11R body to mounting surface | 6.5 mm | Bourns PEC11R datasheet, SHA-256 `ac88f657...` (the hash recorded in `docs/decisions.md`) |
| PEC11R M7 x 0.75 thread length | 7.0 mm | same datasheet |
| PEC11R shaft length / diameter | 15.0 / 6.0 mm | same datasheet |
| PEC11R shaft flat depth "C" | 0.5 mm (plane 2.50 mm from the axis) | same datasheet |
| PEC11R supplied hardware | "one flat washer and one mounting nut" (not dimensioned) | same datasheet |
| panel thickness, panel front/rear, front-board top | 1.6, 0.0, 1.6, 10.0 mm | `mechanical/layout.json` (`panel`, `provisionalZ`) |
| control aperture (NAV and VALUE ENCODER) | 7.2 mm | `mechanical/layout.json` `panelApertures`, cross-checked against `PANEL_CUTOUTS.control` in `lib/layout.ts` |
| display opening right edge | 43.25 mm | `lib/layout.ts` `DISPLAY_CONCEPT.x` + `FRONT_DISPLAY_OPENING.width` (parsed, cross-checked against the two numbers the source prints beside them) |
| knob column and body diameter | x = 52, D12.5 | `lib/layout.ts` `RIGHT_COLUMN` and the owner-approved "knob body (D12.5 at x = 52)" note |

Z datum everywhere: the panel FRONT face is Z = 0 and +Z points REARWARD, the
same datum `lib/layout.ts` and `mechanical/layout.json` use. The panel occupies
Z 0..1.6, the front board's top face is Z 10.0, and the clear gap between them is
8.4 mm.

## Part 1: NAV keytop (`nav-keytop.stl`)

```
        Z = -4.00  +---------+      head face (finger pad), 4.00 mm proud
                   |  D7.00  |
        Z =  0.00  +---------+------ PANEL FRONT
                   |  neck   |      panel, 1.60 thick
        Z =  1.60  +---------+------ PANEL REAR
                 +-+ D9.60  +-+      flange, 1.20 thick, bears on the panel rear
        Z =  2.80  +---------+------ flange rear (chamfered 45 deg)
                     | D5.60 |      shank in the 8.4 mm gap
        Z =  4.85   |  floor |      socket floor, 0.15 mm above the stem top
                     | D3.20 |      blind socket (chamfered mouth)
        Z =  5.00   = stem top of the switch
        Z =  6.35  +---------+      socket mouth, 1.85 mm above the switch body
```

| dimension | value | source |
| --- | --- | --- |
| head + neck diameter | 7.00 mm | aperture 7.2 - 0.20 sliding fit (ASSUMPTION) |
| aperture clearance | 0.20 mm diametral (0.10 per side) | ASSUMPTION, measured on the first article |
| head height above the panel front | 4.00 mm | ASSUMPTION inside the owner's 3-5 mm band |
| neck length | 1.60 mm | panel thickness (`layout.json`) |
| flange diameter | 9.60 mm | ASSUMPTION inside the owner's 9.0-10.0 mm band |
| flange thickness | 1.20 mm | ASSUMPTION |
| bearing ring on the panel rear | 1.20 mm radial | derived (flange - aperture) / 2 |
| shank diameter | 5.60 mm | ASSUMPTION (1.20 mm wall around the socket) |
| socket bore | 3.20 mm | stem 3.0 + 0.20 clearance (ASSUMPTION) |
| socket depth | 1.50 mm | ASSUMPTION, under the ALPS 1.7 mm keytop fitting area |
| socket floor Z | 4.85 mm | stem top 5.00 - 0.15 free play |
| axial free play at rest | 0.15 mm | ASSUMPTION inside the owner's 0.10-0.20 mm band |
| stem engagement in the socket | 1.35 mm | derived (mouth - stem top) |
| cap clearance to the switch body top | 1.85 mm | derived (switch body top Z 8.20) |
| overall length / widest diameter | 10.35 mm / 9.60 mm | derived |
| flange clearance to the nearest panel feature | 6.08 mm (SHIFT/EXIT button) | derived from `panelApertures` + `panelMountHoles` |

### Why the head is the same diameter as the neck

A one-piece panel cannot pass a part that is wider than the aperture at both
ends. The cap is therefore dropped through the 7.2 mm hole from the **rear**
(the head leads, 0.10 mm per side), and the flange - the last feature to enter -
stays outside and bears on the panel rear face.

### Assembly order (required)

1. Press the cap through the NAV aperture from the **rear** of the panel: head
   first. The flange stops it against the panel rear face; the head now stands
   4 mm proud of the panel front.
2. Lower the panel onto the front-board standoffs. The socket mouth leads over
   the switch stem, the stem enters the 1.5 mm socket, and the flange comes to
   rest on the panel rear.

The cap cannot fall through in service: forward, the flange bears on the panel;
rearward, the socket floor meets the stem top after 0.15 mm and the switch's own
travel. The 1.6 mm collar in the aperture carries side load, so the D3.0 stem
never sees a bending moment.

## Part 2: value knob (`knob.stl`)

```
   Z = -18.70  +-----------+   front face (0.80 chamfer), shaft tip 0.20 inside
               |  D12.50   |   16.00 mm tall body, 16 x R1.60 flutes 0.45 deep
               |  D6.20    |   through bore, D-flat 2.60 from the axis
               |  bore     |   radial D2.50 grub pilot (M3 x 0.5) at Z -8.70
   Z =  -2.70  +-----------+   rear face, 2.70 mm in front of the panel front
   Z =   0.00  =============   PANEL FRONT
   Z =  -2.50  ( assumed nut + washer stack, 2.50 mm )
   Z =  -3.50  =============   encoder bushing face, 3.50 mm proud
   Z = -18.50  =============   shaft tip (15.0 mm shaft)
```

| dimension | value | source |
| --- | --- | --- |
| body diameter | 12.50 mm | owner-approved `lib/layout.ts` knob-body note |
| body height | 16.00 mm | ASSUMPTION, top of the owner's 12-16 mm band |
| bore | 6.20 mm through | shaft 6.0 + 0.20 clearance (ASSUMPTION) |
| bore D-flat | plane 2.60 mm from the axis, at 0 deg | shaft flat 2.50 + 0.10 clearance; angle is an ASSUMPTION |
| wall at the bore | 3.15 mm | derived |
| knob rear face | Z -2.70 (2.70 mm in front of the panel) | 2.50 assumed nut/washer + 0.20 air (ASSUMPTION) |
| knob front face | Z -18.70 | rear face - height |
| shaft tip cover | 0.20 mm inside the front face | derived (shaft tip Z -18.50) |
| grip | 16 x R1.60 flutes, 0.45 mm deep | ASSUMPTION; 22.5 deg pitch keeps full-radius lands on +X/+Y/-X/-Y |
| retention | radial D2.50 pilot (M3 x 0.5 tap drill) 3.65 mm long, on the flat's normal | ASSUMPTION (a press fit alone is not acceptable on a metal shaft) |
| set screw | M3 flat point, reaches 0.35 mm past the bore flat | derived |

### Clearance to the display (informational except the last row)

| reference edge | value | clearance from the knob's left edge (45.75 mm) |
| --- | --- | --- |
| front-board display OPENING right edge (`lib/layout.ts`, 43.25) | 43.25 mm | **2.50 mm** - the layout's own rule, asserted by the tests |
| module OUTLINE right edge (`layout.json` display block, -10 + 106/2) | 43.00 mm | 2.75 mm |
| display GLASS envelope (105.42 wide, `layout.json`) | 42.71 mm | 3.04 mm |

The committed export currently carries `display.x = -10.0`, i.e. it agrees with
`lib/layout.ts`. The clearance rule does not depend on that: it reads the
opening edge from the source, and `layout_export_notices()` reports a
`STALE EXPORT` notice whenever the export's display position disagrees with it.
The printed knob is unaffected either way - its diameter is fixed by the
owner-approved D12.5 note in `lib/layout.ts`, not by the display position.
`test_a_stale_export_does_not_move_the_part` and `LayoutSourceMutationTest` pin
both halves of that down.

## ASSUMPTIONS

Everything in this table is a print or assembly tolerance, not a datasheet
value. They are named constants in `printed_geometry.py` and listed in its
`ASSUMPTIONS` tuple; the first article measures them.

| assumption | value | why |
| --- | --- | --- |
| aperture sliding fit | 0.20 mm diametral (0.10 per side) | printed neck vs an FR-4 hole |
| socket bore clearance | 0.20 mm diametral over the D3.0 stem | slides on, little enough to hold |
| axial free play | 0.15 mm | the switch must not be pre-loaded at rest |
| socket depth | 1.50 mm | 0.2 mm margin under the ALPS 1.7 mm fitting area |
| flange diameter / thickness | 9.60 / 1.20 mm | inside the owner's 9-10 mm band; 1.20 mm bearing ring |
| shank diameter | 5.60 mm | 1.20 mm wall around the socket |
| head height / chamfer | 4.00 / 0.80 mm | inside the owner's 3-5 mm band; print and finger edge |
| flange rear chamfer | 0.60 mm 45 deg | the flange rear face is the only overhang in the print |
| socket mouth chamfer | 0.40 mm 45 deg | lead-in over the stem |
| minimum wall | 0.80 mm | 0.4 mm nozzle, two perimeters |
| supplied nut + washer stack | 2.50 mm | the datasheet only says they are supplied - MEASURE IT |
| knob rear air | 0.20 mm | air between nut and knob |
| knob height | 16.00 mm | 18.5 mm of shaft proud of the panel, minus the nut standoff |
| knob bore clearance | 0.20 mm diametral | slides on cold, grub screw takes the torque |
| knob bore mouth chamfer | 0.50 mm | lead-in onto the shaft |
| knob edge chamfers | 0.80 front / 0.40 rear | print and finger edge |
| shaft flat clocking | 0 deg (+X) | the encoder's rotation on the board is not in the sources |
| flute count / radius / depth | 16 / 1.60 / 0.45 mm | grip, and lands on both axes so the printed OD is 12.5 |
| grub screw / pilot | M3 flat point / D2.50 tap drill | metal-shaft retention |
| grub screw position | 6.00 mm from the rear face | reachable, on the flat's normal |
| knob-to-display minimum | 2.50 mm | the layout's own rule, `lib/layout.ts` |

## Print orientation and material

Both parts print **in one piece with no supports**, axis vertical.

**NAV keytop: socket mouth DOWN** (the D5.60/D3.20 annular face on the build
plate, the head at the top).

* the socket void opens onto the build plate, so there is nothing to support
  inside the bore;
* the flange's **bearing face** (which presses on the panel rear) ends up as a
  flat horizontal top layer - the best surface this part needs;
* the finger pad is the flat top layer of the print;
* the only overhangs are the 1.40 mm step where the flange starts (bridged from
  the D5.60 shank) and its 45 deg rear chamfer ring; the socket floor closes a
  D3.20 hole, bridged in one layer from the annulus on both sides;
* the bed contact is a 1.20 mm wide annulus (about 17 mm^2) - **use a 4 mm brim**
  and keep the first layer slow.

**Value knob: rear face DOWN** (the face with the bore mouth chamfer on the
plate, the front face at the top).

* every wall is vertical: the bore, the D-flat chord and the 16 flutes;
* no overhang anywhere; both end chamfers are 45 deg;
* the bore's mouth chamfer opens onto the plate, so the lead-in is clean;
* the front face is the last layer (visible, flat);
* the only bridge is the D2.50 grub pilot through the 3.65 mm wall; its top
  sags slightly, which is why it is **tapped M3 afterwards**.

**Material: PETG** (0.4 mm nozzle, 0.15-0.2 mm layers, 3 perimeters). PETG
because (a) the knob is clamped by an M3 set screw - PLA creeps and cracks at a
tapped hole, PETG is tougher and slightly compliant; (b) the keytop is a push fit
on a plastic stem and takes an impatient thumb, so a slightly compliant,
non-brittle material is the safe choice; (c) matte black PETG matches the panel.
Use ASA/ABS if the module will run warm for long; PLA is acceptable for a
fit-check prototype only.

## The first article must measure

NAV keytop:

1. the panel aperture (nominal 7.20) and the printed neck (nominal 7.00): the
   head and neck must slide in with 0.10 mm per side.
2. the printed socket bore (nominal 3.20) on the real D3.0 stem: it must push on
   and stay put; the ALPS keytop fitting is a friction fit, and this is the only
   axial retention in front of the flange.
3. the socket depth (1.50, limit 1.70) against the real stem, and the switch's
   moving collar above the 1.7 mm fitting area (not dimensioned in the catalog):
   nothing but the stem top may touch the socket floor.
4. the stem top height (nominal 5.00 above the board, so Z 5.00) and the
   resulting axial free play with the flange on the panel rear: target 0.15 mm,
   band 0.10-0.20 mm. Pre-load shows up as a switch that clicks without travel.
5. whether the cap stays on the stem when the panel is lifted off.
6. side-load feel: pushing the head at an angle must not tilt the stem (the
   1.6 mm collar in the aperture is what should take it).

Value knob:

1. the supplied nut + washer stack (assumed 2.50 mm). Everything axial follows
   it: with a thicker stack the knob must sit further forward and the 15.0 mm
   shaft tip can stand proud of a 16 mm knob.
2. the real shaft length and its tip position relative to the knob's front face
   (planned: 0.20 mm inside) and the shaft diameter at the flat.
3. the shaft flat's clocking. The bore flat and the grub screw are both at 0 deg
   (+X); if the encoder is clocked differently, change `KNOB_FLAT_ANGLE_DEG` and
   regenerate - do not rework the bore.
4. the printed bore fit and the tapped M3 pilot: does the set screw clamp the
   shaft without stripping the plastic, and does the knob run without wobble on
   the D-flat.
5. the assembled clearance to the display opening (planned 2.50 mm) - a plan
   view check; the knob itself is entirely in front of the panel.
6. rotational feel: no wobble, no axial end-float beyond the grub screw's play.

## Tool path and self-validation

Both artefacts were produced by **CadQuery** (2.8.0, in `/tmp/cq-venv`), not by a
fallback mesh writer. Each generator (a) derives every dimension, (b) applies
each cut and proves it removed material and left none in the cut volume, (c)
checks one valid watertight solid with one shell and the intended bounding box,
(d) probes named material/void points that prove each feature, (e) exports STL
and STEP, then (f) **re-reads both files**: the STL is parsed in pure Python
(edge counting: every edge used by exactly two triangles, consistent
orientation, one connected shell, bounding box, enclosed volume) and the STEP is
re-imported as one solid with the same volume.

| check | nav keytop | value knob |
| --- | --- | --- |
| body / shell | 1 / 1, valid BRep | 1 / 1, valid BRep |
| volume | 365.2 mm^3 | 1312.5 mm^3 |
| bounding box | x 47.20-56.80, y 7.20-16.80, z -4.00-6.35 | x 45.75-58.25, y 24.75-37.25, z -18.70--2.70 |
| cuts | 4 (socket, mouth chamfer, head chamfer, flange chamfer) | 21 (D bore, 16 flutes, grub pilot, 3 chamfers) |
| quoted feature probes | 10 | 12 |
| STL | 7556 triangles, 0 open / 0 non-manifold edges, 1 shell | 10032 triangles, 0 open / 0 non-manifold edges, 1 shell |
| STEP re-import | 1 solid, 365.2 mm^3 | 1 solid, 1312.5 mm^3 |
