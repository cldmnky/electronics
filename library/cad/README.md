# CAD assets for the component library

`library/components.csv` may point at a footprint and a STEP model for each
part. The paths are relative to `library/`, so `cad/step/model.step` means
`library/cad/step/model.step`. The tool enforces the path, the file and the
STEP header; it does **not** prove that the geometry fits the ordered part.

## Layout

```text
cad/
├── footprints/               shared generic footprints
│   ├── R_Axial_DIN0207_L6.3mm_D2.5mm_P7.62mm_Horizontal.kicad_mod
│   └── PinHeader_1x40_P2.54mm_Vertical.kicad_mod
├── step/                     matching shared 3D models
│   ├── R_Axial_DIN0207_L6.3mm_D2.5mm_P7.62mm_Horizontal.step
│   └── PinHeader_1x40_P2.54mm_Vertical.step
└── <part-id>/                vendor- or part-specific assets
    ├── footprint.kicad_mod   or footprint.tsx / footprint.json
    ├── model.step            or model.stp
    └── SOURCE.md
```

Use a shared file only when the same package really is interchangeable across
the parts that point at it. When in doubt, use a per-part folder. Do not
overwrite an existing shared asset for a new part; add a new one with a name
that states the package and dimensions.

Allowed footprint files: KiCad `.kicad_mod`, tscircuit `.tsx`, or a tscircuit
`.json` footprint. Allowed STEP files: `.step` or `.stp`. The tool checks the
`ISO-10303-21` header, so an OBJ, WRL or a renamed file is rejected.

## tscircuit-native footprints

A `.tsx` footprint is the right file when the part will be placed from
tscircuit. Write a small wrapper in the same style as
[`@tscircuit/common`](https://github.com/tscircuit/common) (MIT): pin labels,
an explicit footprint (a footprinter string or a JSX `<footprint>`), the
manufacturer part number, supplier numbers and a `cadModel` with `objUrl` and
`stepUrl`.

```tsx
import type { ChipProps } from "@tscircuit/props"

const pinLabels = { pin1: ["VCC"], pin2: ["GND"] } as const

export const ExamplePart = (props: ChipProps<typeof pinLabels>) => (
  <chip
    pinLabels={pinLabels}
    manufacturerPartNumber="EXAMPLE-1"
    footprint="dip8_w7.62mm"
    cadModel={{
      stepUrl: "https://example.invalid/EXAMPLE-1.step",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: 0, y: 0, z: 0 },
    }}
    {...props}
  />
)
```

Do not import the whole `@tscircuit/common` package into the library just to
get one part. Copy the small wrapper pattern, keep the MIT notice and record
the source/version in `SOURCE.md`. Check that the footprinter string or JSX
matches the datasheet; a wrapper is not proof of fit. Download the STEP model
from `stepUrl` (or the manufacturer) into `cad/<part-id>/model.step`; a
library part must not depend on a remote model at build time.

`@tscircuit/common` is a collection of common boards/subcircuits, not a shop
catalogue. Its `supplierPartNumbers.jlcpcb` entries are JLCPCB/LCSC
references, not DigiKey/Electrokit/Conrad article numbers. Record those in the
library separately. The code is MIT, but a `stepUrl` model may come from
EasyEDA or a vendor and carry its own terms; record the model license
separately from the code license.

## Where to get CAD

Try these in order, and record which one you used:

1. **The manufacturer's product page or CAD portal.** This is the only source
   that is tied to the exact MPN. Check whether the download is the exact
   package/variant, not a family page.
2. **The shop's product page** (DigiKey Sweden, Electrokit, Conrad Sweden) if
   it offers CAD for the exact article. DigiKey often links manufacturer CAD;
   Electrokit sometimes links a datasheet or a 3D model; Conrad may offer a
   download behind the product page.
3. **The KiCad official libraries** for genuinely generic THT packages. On
   macOS the installed libraries are under
   `/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints` and
   `/Applications/KiCad/KiCad.app/Contents/SharedSupport/3dmodels`. Copy the
   footprint and the matching `.step` (not the `.wrl`) into `cad/`. KiCad
   libraries are licensed CC-BY-SA 4.0 with the KiCad library exception; record
   that in `cad_license` and keep the attribution in `SOURCE.md`.
4. **Author the footprint from the datasheet** when no trustworthy CAD exists.
   Record every dimension you used and its datasheet page/figure in
   `SOURCE.md`. A footprint you authored is still `draft` until a person
   checks it against the physical part or a reliable drawing.

Never copy a vendor's CAD file into the repository when the license forbids
redistribution. Record the URL in `cad_source`, keep the part `draft`, and use
the file locally if the license allows that. Do not commit whole datasheets or
vendor catalogs; record the datasheet URL.

## `SOURCE.md` template

Every per-part folder and every shared asset must have provenance. For a
shared asset, put one `SOURCE.md` beside it (for example
`cad/footprints/R_Axial_DIN0207...SOURCE.md`) and reference it from the rows.
A `preferred` or `alternate` row fails `check` when neither the footprint nor
the step file has a `SOURCE.md` beside it. Use this template:

```markdown
# <part-id> CAD provenance

- Part: <manufacturer> <mpn>
- Datasheet: <URL> (retrieved YYYY-MM-DD)
- Footprint: <URL or local KiCad library path> (retrieved YYYY-MM-DD)
- STEP: <URL or local KiCad library path> (retrieved YYYY-MM-DD)
- License: <SPDX or vendor terms; state whether redistribution is allowed>
- SHA-256: footprint <hash>, step <hash>
- Verified: pin numbering, polarity, pitch, pads, drills, courtyard, body,
  origin/axis, units, orientation
- Notes: <variant, package, deviations, what is still unverified>
```

Compute hashes with:

```bash
shasum -a 256 library/cad/<part-id>/footprint.kicad_mod library/cad/<part-id>/model.step
```

## Verification checklist

A footprint is not ready because it opens. Check all of these against the
datasheet for the exact ordered part:

- Pin numbering and pin 1 location; polarity marks for diodes, LEDs and
  electrolytic capacitors.
- Pitch, pad shape/size, drill diameter, annular ring and pad numbering.
- Courtyard, body outline, height and any keep-out or panel cutout.
- Through-hole lead spacing and lead diameter; the hole must accept the lead
  with the fab's tolerance.
- For connectors: the mated pair, stack height, insertion depth, retention and
  tail projection.
- For modules and displays: connector position, FPC/header orientation, glass
  or body overhang, mounting holes and cable clearance.
- STEP model: same body, same origin, same axis, same units (mm), no scaling
  or rotation surprise. Open it in a viewer and compare the envelope.
- License: the file may be redistributed, or it stays out of the repository.

Record what you checked and what you could not check. "Not verified" is a
valid result and keeps the row `draft`.
