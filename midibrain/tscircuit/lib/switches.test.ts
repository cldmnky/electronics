// The seven B3F-4055 tactiles, checked on the BUILT front circuit JSON.
//
// Source reading this file guards (Omron B3F datasheet, en-b3f.pdf, SHA-256
// be9cf69e5f43fb7689448a2097c19858e5643c0e2e49cd77d018881543e27a1f, page 6,
// 12 x 12 mm "Projected Plunger Type (without Ground Terminal)" column): the
// bottom view numbers the pads 4/2 in one column and 3/1 in the other, and the
// "Terminal Arrangement/Internal Connections (Top View)" drawing links 4-3
// along one side and 2-1 along the other, with the single normally-open
// contact between those two sides. So 1-2 and 3-4 are the internally common
// pairs, and the pairs are separate until the button is pressed.
//
// The consequence the board depends on: pin1 and pin3 must be the two OPPOSITE
// sides of the contact. Wiring pin1 to GND and pin3 to the signal therefore
// closes GND onto the signal when pressed; wiring pin1 and pin3 into the same
// common pair would instead short GND to the signal permanently.
//
// imports/B3F_4055.tsx does not set `internallyConnectedPins` (see the note
// there: on tscircuit 0.0.3169 that re-plans the autorouter and fails
// `tsci check pcb-style`). This file therefore checks the geometry and the
// wiring that the source reading requires, and that the model claims no
// permanent connection between the two pairs.
//
// Geometry and emitted copper only. Physical pair continuity on a fitted
// switch is acceptance item `control_actuator` and stays pending.
import { describe, expect, test } from "bun:test"
import { readFileSync } from "node:fs"
import { INTERCONNECT_NETS } from "./interconnect"

type Json = any

const front = JSON.parse(readFileSync("dist/front/circuit.json", "utf8")) as Json[]
const byType = (json: Json[], type: string) => json.filter(e => e.type === type)

/** Control net each switch's pin3 must carry. SW1-SW5 come from
 *  TRANSPORT_SWITCHES in front.circuit.tsx, SW6 from the SHIFT/EXIT control,
 *  SW7 is the panel power button (both contacts, no rail). */
const PIN3_NET: Record<string, string> = {
  SW1: "REC",
  SW2: "OVER_DUB",
  SW3: "STOP",
  SW4: "PLAY",
  SW5: "PLAY_START",
  SW6: "SHIFT_EXIT",
  SW7: "PWR_SW_B",
}
const PIN1_NET: Record<string, string> = {
  SW1: "GND",
  SW2: "GND",
  SW3: "GND",
  SW4: "GND",
  SW5: "GND",
  SW6: "GND",
  SW7: "PWR_SW_A",
}
const SWITCHES = Object.keys(PIN1_NET)

interface Pad {
  pin: number
  net: string
  x: number
  y: number
  doNotConnect: boolean
  pcbPortId: string
}

const sourcePorts = new Map(byType(front, "source_port").map((p: Json) => [p.source_port_id, p]))
const pcbPorts = new Map(byType(front, "pcb_port").map((p: Json) => [p.pcb_port_id, p]))
const netNames = new Map(
  byType(front, "source_net").map((n: Json) => [n.subcircuit_connectivity_map_key, n.name as string]),
)
const components = byType(front, "source_component")
const componentByName = new Map(components.map((c: Json) => [c.name, c]))

/** Pad number -> pad geometry, net and do-not-connect flag for one component. */
function padsOf(ref: string): Map<number, Pad> {
  const component = componentByName.get(ref)
  if (!component) throw new Error(`no source_component ${ref}`)
  const pads = new Map<number, Pad>()
  for (const pad of [...byType(front, "pcb_plated_hole"), ...byType(front, "pcb_smtpad")]) {
    const pcbComponent = byType(front, "pcb_component").find(
      (c: Json) => c.pcb_component_id === pad.pcb_component_id,
    )
    if (!pcbComponent || pcbComponent.source_component_id !== component.source_component_id) continue
    const port = pcbPorts.get(pad.pcb_port_id)
    const sourcePort = port ? sourcePorts.get(port.source_port_id) : undefined
    if (!sourcePort) continue
    pads.set(sourcePort.pin_number, {
      pin: sourcePort.pin_number,
      net: netNames.get(sourcePort.subcircuit_connectivity_map_key) ?? "",
      x: pad.x,
      y: pad.y,
      doNotConnect: sourcePort.do_not_connect === true,
      pcbPortId: pad.pcb_port_id,
    })
  }
  return pads
}

const close = (a: number, b: number) => Math.abs(a - b) < 1e-6

describe("B3F-4055 internal contact pairs and wiring", () => {
  test("all seven switches are present with four pads each", () => {
    for (const ref of SWITCHES) {
      expect([...padsOf(ref).keys()].sort()).toEqual([1, 2, 3, 4])
    }
  })

  test("pads sit in the drawing's two common-pair rows: 1-2 and 3-4", () => {
    // The footprint must keep the datasheet's top-view arrangement: pins 1 and
    // 2 share one long side (the internally common pair), pins 3 and 4 the
    // other. Positions are separate per switch, so only the relation is fixed.
    for (const ref of SWITCHES) {
      const p = padsOf(ref)
      const [p1, p2, p3, p4] = [p.get(1)!, p.get(2)!, p.get(3)!, p.get(4)!]
      expect({ ref, pair12SameRow: close(p1.y, p2.y) }).toEqual({ ref, pair12SameRow: true })
      expect({ ref, pair34SameRow: close(p3.y, p4.y) }).toEqual({ ref, pair34SameRow: true })
      expect({ ref, rowsDiffer: close(p1.y, p3.y) }).toEqual({ ref, rowsDiffer: false })
      expect({ ref, column12: close(p1.x, p2.x) }).toEqual({ ref, column12: false })
      expect({ ref, column13: close(p1.x, p3.x) }).toEqual({ ref, column13: true })
    }
  })

  test("the two pairs are never permanently connected in the model", () => {
    // The switch is normally open: no source-level internal connection may
    // join a pad of the 1-2 pair to a pad of the 3-4 pair.
    const internal = byType(front, "source_component_internal_connection")
    for (const ref of SWITCHES) {
      const pads = padsOf(ref)
      const portIds = new Set([...pads.values()].map(p => p.pcbPortId))
      const sourcePortIds = new Set(
        [...pads.values()]
          .map(p => pcbPorts.get(p.pcbPortId)?.source_port_id)
          .filter(Boolean),
      )
      const touching = internal.filter((ic: Json) =>
        (ic.source_port_ids as string[]).some(id => sourcePortIds.has(id)),
      )
      expect({ ref, declaredGroups: touching.length }).toEqual({ ref, declaredGroups: 0 })
      expect(portIds.size).toBe(4)
      // The two contact sides carry different nets, so closing the button is
      // the only way to join them.
      expect({ ref, distinctSides: pads.get(1)!.net === pads.get(3)!.net }).toEqual({
        ref,
        distinctSides: false,
      })
    }
  })

  test("SW1-SW6 wire pin1 to GND and pin3 to their control net", () => {
    for (const ref of ["SW1", "SW2", "SW3", "SW4", "SW5", "SW6"]) {
      const p = padsOf(ref)
      expect({ ref, pin1: p.get(1)!.net, pin3: p.get(3)!.net }).toEqual({
        ref,
        pin1: PIN1_NET[ref],
        pin3: PIN3_NET[ref],
      })
    }
  })

  test("the duplicated contacts 2 and 4 carry no net and are declared no-connect", () => {
    for (const ref of SWITCHES) {
      const p = padsOf(ref)
      for (const pin of [2, 4]) {
        expect({ ref, pin, net: p.get(pin)!.net, dnc: p.get(pin)!.doNotConnect }).toEqual({
          ref,
          pin,
          net: "",
          dnc: true,
        })
      }
    }
  })

  test("no trace reaches the duplicated contacts", () => {
    const guard = new Set(
      SWITCHES.flatMap(ref => [2, 4].map(pin => padsOf(ref).get(pin)!.pcbPortId)),
    )
    const touching = byType(front, "pcb_trace").filter((tr: Json) =>
      (tr.connectsTo as string[]).some(id => guard.has(id)),
    )
    expect(touching.length).toBe(0)
  })

  test("SW7 stays a normally-open pair between PWR_SW_A and PWR_SW_B, away from rails", () => {
    const p = padsOf("SW7")
    expect({ pin1: p.get(1)!.net, pin3: p.get(3)!.net }).toEqual({
      pin1: "PWR_SW_A",
      pin3: "PWR_SW_B",
    })
    const rails = new Set(["GND", "P3V3", "P5V", "VSYS", "PI_3V3"])
    // The direct front/rear link's net table is the authority for "reaches the
    // interconnect": SW7 must not land on any of its 26 conductors.
    const interconnect = new Set(Object.values(INTERCONNECT_NETS).map(n => n.replace("net.", "")))
    for (const pin of [1, 3]) {
      const net = p.get(pin)!.net
      expect({ pin, onRail: rails.has(net) }).toEqual({ pin, onRail: false })
      expect({ pin, onInterconnect: interconnect.has(net) }).toEqual({ pin, onInterconnect: false })
    }
  })
})
