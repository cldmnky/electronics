# Button-cap fit specimens — print four, measure once, decide by evidence

Four STLs, each **the standard printed B3F cap with one number changed**: the
3.85 mm locator recess becomes 3.80 / 3.85 / 3.90 / 4.00 mm. Everything else
(11.80 mm plate, 0.60 mm recess depth, 0.80 mm proud height, 4.70 mm part
height) is identical, so a printed specimen compares directly with the standard
part.

```bash
/tmp/cq-venv/bin/python mechanical/printed/button_cap.py --specimens
```

Writes `button-cap-specimen-3.80.stl` … `-4.00.stl` here. The files are
**generated, NOT committed** (the repository ignores `*.stl`), and they are not
installed hardware: regenerate them instead of copying them around.

Reproducibility and dependencies:

* source: `mechanical/printed/button_cap.py` + `printed_geometry.py`, which read
  `tscircuit/lib/layout.ts` and `mechanical/layout.json` — no dimension is typed
  in this directory;
* tool: CadQuery in a private venv (`uv venv /tmp/cq-venv && uv pip install
  --python /tmp/cq-venv/bin/python cadquery`), never the system Python;
* `printed_geometry.py` and `button_cap.py` ARE in the release fingerprint, so a
  change to the generator stales accepted evidence; the specimen STLs are not,
  because they are not part of the module.

## There is no sourced UPPER locator bound

The only sourced number is the plunger: **3.8 ± 0.1 mm** (page 3), i.e. 3.7–3.9.
A locator smaller than 3.9 would interfere with a worst-case plunger, so 3.90 is
the largest size with a sourced basis for *clearing* the tip — and this set
reaches 4.00 only to show direction, not as a recommendation. **No source gives a
retention force, an interference, or a snap geometry**, so no upper design bound
is claimed: above ~3.9 the recess only loses grip, and nothing here says how much
grip there ever was. Choose by measurement, and do not treat any size as a
retaining feature.

## What is and is not known

* The B3F-4055 plunger is **3.8 ±0.1 mm square** and its face is **7.3 ±0.2 mm**
  above the board (Omron B3F datasheet, SHA-256 `be9cf69e...`).
* The recess on the 3.85 mm specimen is therefore **0.025 mm per side** at the
  nominal plunger — **nominal clearance, not a snap, not a detent, not a
  retaining feature**. There is no interference in the design.
* FDM holes usually print **undersize**, so the printed recess will be tighter
  than nominal by an unknown amount. That is exactly why 3.80 needs to be in the
  set.
* The switch's **moving collar** around the plunger, and the body top below the
  plunger face, are **not dimensioned** on the recorded page. The 0.60 mm locator
  ring's clearance at rest and through the press travel is unverified.
* The B3F-4@@5 **pretravel is 0.3 +0.2/−0.1 mm to the operating point** (page 3).
  **Overtravel and total travel are not published there**, so no clearance beyond
  the operating point may be claimed.
* No cap is retained by a snap. Whether a cap stays on the plunger when the panel
  is lifted is unknown until someone measures it.

## Record the results here (do not fill in from expectation)

| specimen | recess nominal | measured print (calipers) | pushes on? | sits flat? | stays on when lifted? | clicks before flush? | pre-load at rest? | notes |
|---|---|---|---|---|---|---|---|---|
| 3.80 | 3.80 mm | | | | | | | |
| 3.85 | 3.85 mm | | | | | | | |
| 3.90 | 3.90 mm | | | | | | | |
| 4.00 | 4.00 mm | | | | | | | |

Run every row **on each of SW1–SW7**: the switches are seven separate parts, and
one loose plunger is enough to make a cap fall off.

## The trials, and what counts as a pass

| trial | method | pass criterion |
|---|---|---|
| push-on | press the recess onto the plunger by hand | goes on without tools, no forcing, no shaving |
| straightness | look across the cap top against the panel | no visible tilt, no rock |
| lift | lift the panel 10 mm | the cap must not end up carried into the aperture; record which way it goes |
| pre-load at rest | leave the cap on for 10 min, then press it | the switch must not click on its own, and the first press must move the plunger |
| travel | press slowly to flush with the panel front | the switch must click BEFORE the cap is flush |
| rattle | shake the panel | no rattle inside the 12.25 mm aperture |
| print calibration | calipers, 3 places per face | record actual vs nominal; do not adjust the model from one print |

## What to do with the result

* Pick the **smallest** recess that goes on by hand and does not rattle. A tighter
  recess is the only retention the printed cap has.
* If none of the four stays on when the panel is lifted, **report it** rather than
  changing the shape: the 12.25 mm cutout cannot accept a skirt, and adding one
  would be a new design needing its own review.
* If every specimen rattles, the plunger is smaller than its band, or the print is
  undersize beyond 0.15 mm: record which, then re-print rather than re-model.
* Nothing in this table closes `control_actuator`. That item needs the installed
  cap height and the press travel recorded as well.
