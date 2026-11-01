// Printable NON-ELECTRICAL actuator parts, millimetres. Not PCB footprints.
// Explicit shared-vertex triangle rings avoid CSG T-junctions in small STL parts.
// The gate checks manifold topology, bounds, volume and the blind stem pocket.
import { geometries, measurements } from "@jscad/modeling"
import { ACTUATORS } from "./mechanical"

type V = [number, number, number]
const N = 96
const { geom3 } = geometries
const circle = (diameter: number, z: number): V[] => Array.from({ length: N }, (_, i) => {
  const a = i * 2 * Math.PI / N
  return [diameter / 2 * Math.cos(a), diameter / 2 * Math.sin(a), z]
})
const square = (width: number, z: number): V[] => Array.from({ length: N }, (_, i) => {
  const a = i * 2 * Math.PI / N
  const c = Math.cos(a), s = Math.sin(a)
  const r = width / 2 / Math.max(Math.abs(c), Math.abs(s))
  return [r * c, r * s, z]
})
const strip = (a: V[], b: V[], reverse = false): V[][] => a.flatMap((p, i) => {
  const j = (i + 1) % N
  const triangles = [[p, a[j]!, b[j]!], [p, b[j]!, b[i]!]]
  return reverse ? triangles.map((t) => t.reverse()) : triangles
})
const lid = (ring: V[], reverse = false): V[][] => ring.map((p, i) => {
  const t: V[] = [[0, 0, p[2]], p, ring[(i + 1) % N]!]
  return reverse ? t.reverse() : t
})
const outside = (rings: V[][]): V[][] => rings.slice(1).flatMap((ring, i) => strip(rings[i]!, ring))

/** Z=0 rests on the existing K6 cap. Flange goes BEHIND the panel and retains
 * the sliding plunger. No riser, soldered pin extensions or switch preload. */
export const modePlunger = () => {
  const p = ACTUATORS.button
  const rings = [circle(p.flangeDiameter, 0), circle(p.flangeDiameter, p.flangeHeight),
    circle(p.barrelDiameter, p.flangeHeight), circle(p.barrelDiameter, p.extension)]
  return geom3.fromPoints([...outside(rings), ...lid(rings[0]!, true), ...lid(rings.at(-1)!)])
}

/** Z=0 is the pocket mouth, 2mm BELOW the top of the XUNPU stem. Retain the
 * square blind pocket with a tiny removable silicone dab AFTER dry fitting. */
export const navCap = () => {
  const p = ACTUATORS.joystick
  const top = p.pocketDepth + p.extension
  const rings = [circle(p.cupDiameter, 0), circle(p.cupDiameter, p.pocketDepth + p.cupRoof),
    circle(p.neckDiameter, p.pocketDepth + p.cupRoof), circle(p.neckDiameter, top - p.headHeight),
    circle(p.headDiameter, top - p.headHeight), circle(p.headDiameter, top)]
  const mouth = square(p.pocketWidth, 0)
  const roof = square(p.pocketWidth, p.pocketDepth)
  return geom3.fromPoints([...outside(rings), ...lid(rings.at(-1)!),
    ...strip(rings[0]!, mouth, true), ...strip(mouth, roof, true), ...lid(roof, true)])
}

export const mechanicalParts = () => [
  { name: "mode-plunger", geometry: modePlunger(), material: "translucent resin; sliding/captive, flange behind panel" },
  { name: "nav-cap", geometry: navCap(), material: "tough resin; square blind pocket; removable silicone retention" },
]
export const partMeasurements = () => mechanicalParts().map(({ name, geometry, material }) => ({
  name, material, bounds: measurements.measureBoundingBox(geometry), volumeMm3: measurements.measureVolume(geometry),
}))
