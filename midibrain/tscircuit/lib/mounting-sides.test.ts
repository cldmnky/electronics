// Mounting sides and power-button net isolation, measured on the BUILT JSON.
//
// Owner requirements this file guards:
//  * the Pico module sits on the SAME rear-board side as the Raspberry Pi, i.e.
//    opposite J_FRONT: the sockets' bodies and their insertion direction must
//    both face the Pi side, and J_FRONT must be the only body on the other side;
//  * J_PWR is a switch-only contact pair: its two nets may reach SW7 and J_PWR
//    and nothing else (no rail, no ground, no interconnect pin).
//
// This is emitted-geometry evidence only. It does NOT prove that the Pico
// module, its sockets, the Pi or any plug physically clear each other: those
// remain pending (acceptance items `interconnect_mate`, `zstack`,
// `cooler_hat_ssd`, `power_header_mount`).
import { describe, expect, test } from "bun:test"
import { readFileSync } from "node:fs"
import { PROBE_KEEPOUTS } from "./probe-points"
import { FRONT_INTERCONNECT, REAR_INTERCONNECT } from "./layout"

type Json = any

const read = (p: string) => JSON.parse(readFileSync(p, "utf8")) as Json[]
const front = read("dist/front/circuit.json")
const rear = read("dist/index/circuit.json")
const byType = (json: Json[], type: string) => json.filter(e => e.type === type)

const source = (json: Json[], name: string) =>
  byType(json, "source_component").find((s: Json) => s.name === name)

const pcb = (json: Json[], name: string) => {
  const src = source(json, name)
  if (!src) throw new Error(`no source_component ${name}`)
  const p = byType(json, "pcb_component").find(
    (c: Json) => c.source_component_id === src.source_component_id,
  )
  if (!p) throw new Error(`no pcb_component ${name}`)
  return p
}

/** Pad → net, through the port chain the builder emits. */
function netOfPad(json: Json[]) {
  const sourcePorts = new Map(byType(json, "source_port").map((p: Json) => [p.source_port_id, p]))
  const pcbPorts = new Map(byType(json, "pcb_port").map((p: Json) => [p.pcb_port_id, p]))
  const netNames = new Map(
    byType(json, "source_net").map((n: Json) => [n.subcircuit_connectivity_map_key, n.name as string]),
  )
  return (pcbPortId: string): string => {
    const port = pcbPorts.get(pcbPortId) as Json | undefined
    const sp = port ? (sourcePorts.get(port.source_port_id) as Json | undefined) : undefined
    return (sp && netNames.get(sp.subcircuit_connectivity_map_key)) ?? ""
  }
}

/** Every pad of a board with its component name, pin and net. */
function allPads(json: Json[]) {
  const components = byType(json, "pcb_component")
  const sources = byType(json, "source_component")
  const sourceOf = (id: string) => sources.find((s: Json) => s.source_component_id === id)
  const nameOf = (pcbId: string) => {
    const c = components.find((x: Json) => x.pcb_component_id === pcbId)
    return (c && sourceOf(c.source_component_id)?.name) ?? "?"
  }
  const net = netOfPad(json)
  const out: { ref: string; pin: string; net: string; layer?: string }[] = []
  for (const t of ["pcb_plated_hole", "pcb_smtpad"]) {
    for (const pad of byType(json, t)) {
      const hints = (pad.port_hints ?? []) as string[]
      out.push({
        ref: nameOf(pad.pcb_component_id),
        pin: hints[hints.length - 1] ?? "",
        net: net(pad.pcb_port_id),
        layer: pad.layer,
      })
    }
  }
  return out
}

describe("the Pico module sits on the Pi side of the rear board", () => {
  test("every rear-board body is on the Pi side except J_FRONT", () => {
    const bottom = byType(rear, "pcb_component").filter((c: Json) => c.layer === "bottom")
    const top = byType(rear, "pcb_component").filter((c: Json) => c.layer === "top")
    expect(top.length).toBe(1)
    expect(pcb(rear, "J_FRONT")).toBe(top[0]!)
    expect(bottom.map((c: Json) => c.pcb_component_id)).toContain(
      pcb(rear, "J_PICO_L").pcb_component_id,
    )
    expect(bottom.map((c: Json) => c.pcb_component_id)).toContain(
      pcb(rear, "J_PICO_R").pcb_component_id,
    )
    expect(bottom.map((c: Json) => c.pcb_component_id)).toContain(pcb(rear, "J_PI").pcb_component_id)
  })

  test("the socket bodies and insertion directions face the Pi, J_FRONT faces the front board", () => {
    for (const name of ["J_PICO_L", "J_PICO_R", "J_PI"]) {
      const c = pcb(rear, name)
      // The module plugs in from the Pi side, so the socket opens rearward.
      expect({ name, layer: c.layer, insertion: c.insertion_direction }).toEqual({
        name, layer: "bottom", insertion: "from_below",
      })
    }
    const jf = pcb(rear, "J_FRONT")
    expect({ layer: jf.layer, insertion: jf.insertion_direction }).toEqual({
      layer: "top", insertion: "from_above",
    })
    // The emitted courtyard follows the body's layer, so assembly artwork and
    // the CPL side cannot disagree with the model orientation.
    const courtyards = byType(rear, "pcb_courtyard_rect").filter(
      (c: Json) => c.pcb_component_id === pcb(rear, "J_PICO_L").pcb_component_id ||
        c.pcb_component_id === pcb(rear, "J_PICO_R").pcb_component_id,
    )
    expect(courtyards.map((c: Json) => c.layer)).toEqual(["bottom", "bottom"])
  })

  test("the two 1 x 20 sockets are the Pico's 17.78 mm row pair, rotated to the module", () => {
    const l = pcb(rear, "J_PICO_L")
    const r = pcb(rear, "J_PICO_R")
    const gap = Math.abs(r.footprint_origin.x - l.footprint_origin.x)
    expect(Math.round(gap * 1000) / 1000).toBe(17.78) // 0.7 in module row spacing
    expect(Math.round(l.footprint_origin.y * 1000) / 1000).toBe(0)
    expect(Math.round(r.footprint_origin.y * 1000) / 1000).toBe(0)
    // 270 / 90 deg makes each single row run along Y, pin 1 at the documented end.
    expect({ l: l.rotation, r: r.rotation }).toEqual({ l: 270, r: 90 })
    for (const c of [l, r]) {
      expect(c.width === undefined || c.width < 4).toBe(true)
    }
  })

  test("the Pico and J_FRONT plan envelopes do not overlap (plan geometry only)", () => {
    const pico = PROBE_KEEPOUTS.pico_module!
    const halfLength = 40.52 / 2 // J_FRONT body length along Y at 90 deg
    const halfWidth = 8.8 / 2
    const x1 = REAR_INTERCONNECT.x - halfWidth
    const x2 = REAR_INTERCONNECT.x + halfWidth
    const y1 = REAR_INTERCONNECT.y - halfLength
    const y2 = REAR_INTERCONNECT.y + halfLength
    const overlap = x1 < pico.x2 && x2 > pico.x1 && y1 < pico.y2 && y2 > pico.y1
    expect(overlap).toBe(false)
    // State the gap rather than inferring a fitted clearance.
    const gapX = Math.max(pico.x1 - x2, x1 - pico.x2)
    expect(gapX).toBeGreaterThan(0)
  })

  test("J_FRONT is on the rear board's front-facing side, opposite the Pi side", () => {
    // The male's solder tails land on the Pi side: that is the known pending
    // tail/spacer condition recorded in lib/layout.ts PROVISIONAL_DEPTH.
    expect(pcb(rear, "J_FRONT").layer).toBe("top")
    expect(REAR_INTERCONNECT).toEqual(FRONT_INTERCONNECT)
  })
})

describe("the panel power button stays a floating switch pair", () => {
  const pads = allPads(front)
  const pwr = pads.filter(p => p.net.startsWith("PWR_"))
  const rails = new Set(["GND", "P3V3", "P5V", "VSYS", "PI_3V3"])

  test("only SW7 and J_PWR touch the two PWR_SW nets", () => {
    expect(new Set(pwr.map(p => p.ref))).toEqual(new Set(["SW7", "J_PWR"]))
    expect(new Set(pwr.map(p => p.net))).toEqual(new Set(["PWR_SW_A", "PWR_SW_B"]))
    // Two contacts per net: one switch contact and one header pin.
    for (const net of ["PWR_SW_A", "PWR_SW_B"]) {
      expect(pwr.filter(p => p.net === net).length).toBe(2)
    }
  })

  test("no PWR_SW pad is on a rail, ground or an interconnect pin", () => {
    for (const p of pwr) {
      expect(rails.has(p.net)).toBe(false)
    }
    const interconnectNets = new Set(allPads(rear).filter(p => p.ref === "J_FRONT").map(p => p.net))
    for (const p of pwr) {
      expect(interconnectNets.has(p.net)).toBe(false)
    }
  })

  test("J_PWR is still the 2-pin Pi remote-power header with the switch contacts", () => {
    const jpwr = pcb(front, "J_PWR")
    const pads = allPads(front).filter(p => p.ref === "J_PWR")
    expect(pads.length).toBe(2)
    expect(new Set(pads.map(p => p.net))).toEqual(new Set(["PWR_SW_A", "PWR_SW_B"]))
    // No power part, no regulator: the header carries the switch pair only.
    expect(source(front, "J_PWR")?.ftype).toBe("simple_connector")
    expect(jpwr.do_not_place).toBe(false)
  })
})
