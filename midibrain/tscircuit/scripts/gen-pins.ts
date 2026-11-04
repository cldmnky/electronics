// Generates firmware/pico/pins.h from lib/pin-map.ts, the single source of
// the Pico GPIO allocation. Run after any pin-map change:
//   bun run scripts/gen-pins.ts          # regenerate
//   bun run scripts/gen-pins.ts --check  # fail if the committed file drifts
import { readFileSync, writeFileSync } from "node:fs"
import { PICO_GPIO } from "../lib/pin-map"

const target = new URL("../../firmware/pico/pins.h", import.meta.url)
const lines = PICO_GPIO.map(p => `#define MB_PIN_${p.signal} ${p.gpio}  // Pico physical pin ${p.pin}`)
const out =
  `// GENERATED from tscircuit/lib/pin-map.ts -- do not edit by hand.\n` +
  `// Regen: (cd tscircuit && bun run scripts/gen-pins.ts)\n` +
  `#pragma once\n` +
  lines.join("\n") + "\n"

if (process.argv.includes("--check")) {
  if (readFileSync(target, "utf8") !== out) {
    console.error("pins.h is stale vs lib/pin-map.ts; run: (cd tscircuit && bun run scripts/gen-pins.ts)");
    process.exit(1)
  }
  console.log("pins.h matches lib/pin-map.ts")
} else {
  writeFileSync(target, out)
  console.log(`wrote firmware/pico/pins.h (${PICO_GPIO.length} pins)`)
}
