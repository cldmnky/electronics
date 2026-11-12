# Using `@tscircuit/common`

Source: <https://github.com/tscircuit/common> (MIT). Checked at package
version `0.0.69` on 2026-11-12. Pin the version you actually use; do not assume
the API is stable.

## What it is

A small collection of community-contributed, tscircuit-native boards and
subcircuits. It is **not** a component shop catalogue and it is not a
replacement for `library/components.csv`.

Exported subcircuits/components at the checked version (`index.tsx`):

- `ArduinoShield`
- `MicroModBoard`
- `RaspberryPiHatBoard`
- `XiaoBoard`, `XiaoReceiver`
- `ProMicroBoard`
- `ViaGridBoard`
- `Microcontroller_RP2040`
- `PowerBoost_MT3608`
- `AudioAmplifier3W_PAM8403`
- `CM5Receiver`
- `TscircuitLogo`

The repository also carries internal single-part wrappers (for example
`SS34`, `AO3401A`, `PAM8403DR_H`, `RP2040`, `TYPE_C_16PIN_2MD_073_`,
`W25Q16JVUXIQ`) that show the house style but are not exported from the
package root. Read the source file before relying on one.

## Why it matters for the library

It is the best in-repo reference for a tscircuit part wrapper:

- `pinLabels` maps package pins to datasheet names.
- `manufacturerPartNumber` records the exact part.
- `supplierPartNumbers={{ jlcpcb: ["C..."] }}` records the JLCPCB/LCSC
  assembly reference.
- `footprint` is either a footprinter string (for example
  `tssop16_p1.27mm_w3.38mm_pw0.35mm_pl2.02mm_pin1location(leftside,bottom)`)
  or an explicit JSX `<footprint>` with pads, silkscreen and courtyard.
- `cadModel` carries `objUrl`, `stepUrl`, `pcbRotationOffset` and
  `modelOriginPosition`.

That is the shape a `library/cad/<part-id>/footprint.tsx` should take when a
library part will be placed from tscircuit.

## How to use it

For a board-level subcircuit that already exists upstream:

```bash
cd <project>/tscircuit
bun add @tscircuit/common@0.0.69   # pin an exact version
```

```tsx
import { Microcontroller_RP2040 } from "@tscircuit/common"

return <Microcontroller_RP2040 name="MCU" pcbX={0} pcbY={0} />
```

Read the upstream README and the component source before wiring it in. Its
internal nets are private by default; use `exposedNets` or `exposeNets` to
connect the ones the parent board needs. Confirm the pin labels and the
exposed net names against the current source, not against a blog snippet.

For one library part, copy the **pattern**, not the package:

1. Create `library/cad/<part-id>/footprint.tsx` with the pin labels, exact
   footprint and `cadModel` for the part.
2. Download the STEP model into `library/cad/<part-id>/model.step`.
3. Put the upstream URL, package version, MIT notice and what you changed in
   `library/cad/<part-id>/SOURCE.md`.
4. Set `footprint = cad/<part-id>/footprint.tsx`, `step =
   cad/<part-id>/model.step`, `cad_source = <upstream URL>` and
   `cad_license = MIT` in the library row.
5. Verify the footprinter string/JSX and the STEP model against the exact
   datasheet before promoting the row. A wrapper copied from a similar part is
   a new, unverified footprint.

## Cautions

- `@tscircuit/common` is MIT-licensed. Keep the copyright/permission notice
  when you copy a substantial portion. A short wrapper inspired by the pattern
  still needs the source and version recorded.
- Its `jlcpcb` numbers are **not** DigiKey Sweden, Electrokit or Conrad Sweden
  article numbers. The library's main-shop columns are still required.
- Its `stepUrl` points at `modelcdn.tscircuit.com`. Download the file for the
  library; do not make a library part depend on a remote model at build time.
- The package moves quickly. A footprinter string that renders in one
  tscircuit version may change behavior in another; pin the version and run
  the project's own build/check.
- A common subcircuit is a design block, not an approved BOM. Check the exact
  parts, shop availability, footprint and assembly side before ordering.
