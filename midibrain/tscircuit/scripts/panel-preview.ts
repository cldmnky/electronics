import { writeFileSync } from "node:fs"
import { DISPLAY_CONCEPT, FRONT_CONCEPT, LCD_WINDOW, PANEL, PANEL_LCD_CUTOUT } from "../lib/layout"

// Illustrative artwork only. Display front bounding box is a vendor-published
// nominal value; all other marks are symbolic, not physical footprints.
const esc = (s: string) => s.replace(/&/g, "&amp;").replace(/</g, "&lt;")
const X = (x: number) => x + PANEL.width / 2
const Y = (y: number) => PANEL.height / 2 - y
const d = DISPLAY_CONCEPT, dx = X(d.x), dy = Y(d.y)
const c = PANEL_LCD_CUTOUT, cx = X(c.x), cy = Y(c.y)
const w = LCD_WINDOW, wx = X(w.x), wy = Y(w.y)
const marks = FRONT_CONCEPT.map(p => {
  const x = X(p.x), y = Y(p.y)
  let shape: string
  if (p.kind === "cable") shape = `<rect x="${x-5.5}" y="${y-2}" width="11" height="4" rx="0.8"/>`
  else if (p.id === "TEMPO") shape = `<circle cx="${x}" cy="${y}" r="5.5"/><path d="M ${x} ${y-4.5} v 2.2" fill="none" stroke="#e0eae3" stroke-width="0.5"/>`
  else if (p.id === "NAV") shape = `<rect x="${x-5}" y="${y-5}" width="10" height="10" rx="0.7"/><circle cx="${x}" cy="${y}" r="0.8"/>`
  else if (p.kind === "button") shape = `<rect x="${x-3.2}" y="${y-2.6}" width="6.4" height="5.2" rx="0.5"/>`
  else shape = `<circle cx="${x}" cy="${y}" r="${p.kind === "led" ? 1 : p.kind === "jack" ? 3.2 : 3}"/>`
  const labelY = y + (p.id === "TEMPO" || p.id === "NAV" ? 8 : p.kind === "led" ? 4 : 5)
  return `<g fill="#344940" stroke="#a3b7aa" stroke-width="0.3">${shape}</g><text x="${x}" y="${labelY}" text-anchor="middle" font-size="1.55" fill="#d5e4da">${esc(p.id)}</text>`
}).join("\n")
const svg = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${PANEL.width + 16} ${PANEL.height + 20}">
<title>mnkybox 26HP 3.5-inch DSI concept — not a cutting template</title>
<rect width="100%" height="100%" fill="#e8e8e4"/>
<text x="8" y="5" font-family="sans-serif" font-size="2.4" font-weight="bold" fill="#49544e">mnkybox — 26HP FRONT CONCEPT</text>
<g transform="translate(8 8)" font-family="sans-serif">
<rect width="${PANEL.width}" height="${PANEL.height}" rx="0.7" fill="#19211e"/>
<rect x="${cx-c.width/2}" y="${cy-c.height/2}" width="${c.width}" height="${c.height}" fill="#080f0c" stroke="#829a8b" stroke-width="0.35"/>
<rect x="${wx-w.width/2}" y="${wy-w.height/2}" width="${w.width}" height="${w.height}" fill="#112820"/>
<text x="${dx}" y="${dy-2}" text-anchor="middle" fill="#b8e5cf" font-size="2.4">3.5-inch 800 × 480 DSI / I²C TOUCH</text>
<text x="${dx}" y="${dy+2}" text-anchor="middle" fill="#86bba1" font-size="1.8">PANEL CUTOUT 89.2 × 56.3 — ILLUSTRATIVE</text>
${marks}
</g>
<text x="8" y="${PANEL.height+14}" font-family="sans-serif" font-size="2.1" fill="#9b4234">ILLUSTRATIVE ONLY — NOT A CUTTING TEMPLATE OR FIT APPROVAL</text>
</svg>\n`
writeFileSync("../assets/front-panel.svg", svg)
console.log("Wrote ../assets/front-panel.svg (illustrative only)")
