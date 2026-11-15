# Local-only part CAD

This directory holds 3D bodies that may be used for a **local render only** and
must never be committed. The repository is public, and some vendors do not grant
redistribution.

## `rkjxt1f42001.step`

- Part: ALPS Alpine RKJXT1F42001 4-direction stick switch (U5).
- File: the owner-supplied **16-solid alternate STEP**, not the ALPS vendor
  single-solid file. SHA-256
  `24d6b9e11b8d8c59f97125d3e65a39cd0e1573fd43e1b9c091272566be1aeae1`.
- Units and frame: millimetres; origin on the shaft axis at the PCB seating
  plane; +z away from the PCB toward the panel; body 17.2 x 17.2 mm, z
  -3.236…17.400 mm. It is placed at the footprint origin, so it needs no
  rotation or offset in the board export.
- Why it is here: `library/cad/rkjxt1f42001/SOURCE.md` records that the ALPS
  vendor STEP, this alternate STEP and the SamacSys models all lack a
  redistribution grant. None of them may be committed, and the library row
  stays `draft`.
- How it is used: this is an **optional override**, not the default. The default
  render uses the library's authored, Apache-2.0 STEP at
  `library/cad/rkjxt1f42001/model.step`; `make mechanical-steps` passes that
  path through `MIDIBRAIN_NAV_STEP`. To compare against this local alternate
  file, override the path:

```bash
make mechanical-steps NAV_STEP=../mechanical/local-cad/rkjxt1f42001.step
```
  `front.circuit.tsx` uses the STEP as U5's `cadModel` when
  `MIDIBRAIN_NAV_STEP` is set; the body then travels inside `front.step` and
  `assembly.step`, so the mechanical viewer shows it with the board. Without
  the override, the export still succeeds and the default authored model (or,
  in the browser 3D view, the inline JSCAD envelope) is used.

`tsci export -f step` reads that path directly. tscircuit's **browser** 3D view
fetches a URL instead, so it needs the file over HTTP:

```bash
make local-cad-serve                       # terminal 1
make mechanical-steps \
  NAV_STEP=http://127.0.0.1:8765/rkjxt1f42001.step   # terminal 2
```

The printed NAV keytop is not in this directory: it is the committed envelope
in `tscircuit/front.circuit.tsx`, built from `mechanical/layout.json`, and it
appears in `front.step` with the switch.
