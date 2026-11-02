// Froemix front panel - SVG rendering for previews and print templates.
//
// Everything here draws from the BUILT circuit JSON, never from a second copy of
// the artwork: the preview, the 1:1 print template and the Gerbers therefore
// show the same geometry. No text element is used for printed artwork - the
// legends are already paths - and nothing loads a font, an image or a script,
// so a preview renders the same offline.
//
// Coordinates inside a panel group are panel millimetres (+Y up), so the group
// carries `scale(1 -1)` to land in SVG's +Y-down space. Stroke widths are in
// millimetres and survive that flip unchanged.

import type { PanelArtwork } from "./panel-art"
import { panelWindow } from "./panel-art"
import type { Pt } from "./panel-geometry"

type CjElement = Record<string, any>

export type PanelTheme = {
  background: string
  boardFill: string
  edge: string
  silk: string
  holeFill: string
  holeEdge: string
  windowFill: string
  hardware: string
  hardwareLabel: string
}

export const PANEL_THEMES: Record<"studio" | "print", PanelTheme> = {
  studio: {
    background: "#0b0b0c",
    boardFill: "#131316",
    edge: "#8a8a92",
    silk: "#f2f2f0",
    holeFill: "#050506",
    holeEdge: "#6f6f76",
    windowFill: "#040405",
    hardware: "#5e5e66",
    hardwareLabel: "#9a9aa2",
  },
  print: {
    background: "#ffffff",
    boardFill: "#ffffff",
    edge: "#000000",
    silk: "#000000",
    holeFill: "#ffffff",
    holeEdge: "#000000",
    windowFill: "#ffffff",
    hardware: "#8a8a8a",
    hardwareLabel: "#6a6a6a",
  },
}

const num = (v: number): string => {
  const s = v.toFixed(4)
  return s.replace(/\.?0+$/, "")
}

const pointsAttr = (points: Pt[]): string =>
  points.map((p) => `${num(p.x)},${num(p.y)}`).join(" ")

/** One closed polygon in panel coordinates. */
const polygon = (points: Pt[], fill: string, stroke: string, strokeWidth: number): string =>
  `<polygon points="${pointsAttr(points)}" fill="${fill}" stroke="${stroke}" stroke-width="${num(strokeWidth)}" />`

/**
 * The panel in panel coordinates: outline, holes, OLED window, printed artwork
 * and - optionally - the hardware keepout circles used by the checks.
 */
export const renderPanelContent = (
  circuitJson: CjElement[],
  opts: { theme: PanelTheme; hardware?: { keepouts: { id: string; x: number; y: number; radius: number }[] }; showHardware?: boolean },
): string => {
  const theme = opts.theme
  const board = circuitJson.find((e) => e.type === "pcb_board")
  const width: number = board?.width ?? 101.6
  const height: number = board?.height ?? 128.5
  const out: string[] = []

  out.push(
    `<rect x="${num(-width / 2)}" y="${num(-height / 2)}" width="${num(width)}" height="${num(height)}" rx="0" fill="${theme.boardFill}" stroke="${theme.edge}" stroke-width="0.15" />`,
  )

  for (const hole of circuitJson.filter((e) => e.type === "pcb_hole")) {
    const r = (hole.hole_diameter ?? 0) / 2
    out.push(
      `<circle cx="${num(hole.x)}" cy="${num(hole.y)}" r="${num(r)}" fill="${theme.holeFill}" stroke="${theme.holeEdge}" stroke-width="0.1" />`,
    )
  }

  for (const cut of circuitJson.filter((e) => e.type === "pcb_cutout")) {
    if (cut.shape === "polygon") {
      out.push(polygon(cut.points as Pt[], theme.windowFill, theme.holeEdge, 0.1))
    } else if (cut.shape === "rect") {
      const w = cut.width
      const h = cut.height
      const c = cut.center
      out.push(
        polygon(
          [
            { x: c.x - w / 2, y: c.y - h / 2 },
            { x: c.x + w / 2, y: c.y - h / 2 },
            { x: c.x + w / 2, y: c.y + h / 2 },
            { x: c.x - w / 2, y: c.y + h / 2 },
          ],
          theme.windowFill,
          theme.holeEdge,
          0.1,
        ),
      )
    }
  }

  if (opts.showHardware && opts.hardware) {
    for (const k of opts.hardware.keepouts) {
      out.push(
        `<circle cx="${num(k.x)}" cy="${num(k.y)}" r="${num(k.radius)}" fill="none" stroke="${theme.hardware}" stroke-width="0.08" stroke-dasharray="0.5 0.5" />`,
      )
    }
  }

  // This is a front-face preview; the build-identity print is on the rear.
  for (const path of circuitJson.filter((e) => e.type === "pcb_silkscreen_path" && (e.layer ?? "top") === "top")) {
    const route = (path.route ?? []) as Pt[]
    if (route.length < 2) continue
    const d = `M ${route.map((p) => `${num(p.x)} ${num(p.y)}`).join(" L ")}`
    out.push(
      `<path d="${d}" fill="none" stroke="${theme.silk}" stroke-width="${num(path.stroke_width ?? 0)}" stroke-linecap="round" stroke-linejoin="round" />`,
    )
  }

  return out.join("\n")
}

const svgDocument = (opts: {
  width: number
  height: number
  viewBox: string
  background: string
  content: string
  title: string
  transform?: string
  className?: string
}): string => `<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg"${opts.className ? ` class="${opts.className}"` : ""} width="${num(opts.width)}mm" height="${num(opts.height)}mm" viewBox="${opts.viewBox}" role="img" aria-label="${opts.title}">
  <title>${opts.title}</title>
  <rect x="${opts.viewBox.split(" ")[0]}" y="${opts.viewBox.split(" ")[1]}" width="${num(opts.width)}" height="${num(opts.height)}" fill="${opts.background}" />
  <g${opts.transform ? ` transform="${opts.transform}"` : ""}>
${opts.content}
  </g>
</svg>
`

/**
 * Standalone panel preview: the panel at 1:1 in SVG units, with the hardware
 * keepouts drawn as dashed illustrations.
 */
export const renderPanelPreviewSvg = (circuitJson: CjElement[], art: PanelArtwork): string => {
  const theme = PANEL_THEMES.studio
  const w = art.outline.width
  const h = art.outline.height
  const content = renderPanelContent(circuitJson, {
    theme,
    hardware: { keepouts: art.keepouts },
    showHardware: true,
  })
  return svgDocument({
    width: w,
    height: h,
    viewBox: `${num(-w / 2)} ${num(-h / 2)} ${num(w)} ${num(h)}`,
    background: theme.background,
    content,
    title: `Froemix 20HP faceplate - ${art.design} design, 101.6 x 128.5mm`,
    transform: "scale(1 -1)",
    className: "panel",
  })
}

/**
 * 1:1 print template: an A4 sheet with the panel at actual size and a
 * calibration bar, so the fit can be checked with real hardware after printing
 * at 100% with no scaling.
 */
export const renderPrintTemplateSvg = (circuitJson: CjElement[], art: PanelArtwork): string => {
  const theme = PANEL_THEMES.print
  const pageW = 210
  const pageH = 297
  const w = art.outline.width
  const h = art.outline.height
  const panelX = (pageW - w) / 2
  const panelY = 20
  const content = renderPanelContent(circuitJson, { theme })

  const rulerY = panelY + h + 22
  const bar: string[] = []
  const barX = 20
  bar.push(
    `<line x1="${barX}" y1="${rulerY}" x2="${barX + 100}" y2="${rulerY}" stroke="#000" stroke-width="0.3" />`,
  )
  for (let mm = 0; mm <= 100; mm += 10) {
    const long = mm % 50 === 0
    bar.push(
      `<line x1="${barX + mm}" y1="${rulerY}" x2="${barX + mm}" y2="${rulerY - (long ? 4 : 2)}" stroke="#000" stroke-width="0.25" />`,
    )
  }
  bar.push(
    `<text x="${barX}" y="${rulerY + 6}" font-family="monospace" font-size="3.2" fill="#000">0</text>`,
    `<text x="${barX + 50}" y="${rulerY + 6}" text-anchor="middle" font-family="monospace" font-size="3.2" fill="#000">50</text>`,
    `<text x="${barX + 100}" y="${rulerY + 6}" text-anchor="end" font-family="monospace" font-size="3.2" fill="#000">100 mm</text>`,
    `<text x="${barX}" y="${rulerY + 14}" font-family="monospace" font-size="3.2" fill="#000">PRINT AT 100% - NO FIT TO PAGE. Measure the bar before checking parts.</text>`,
    `<text x="${barX}" y="${rulerY + 20}" font-family="monospace" font-size="3.2" fill="#000">Froemix 20HP faceplate ${w} x ${h}mm - design ${art.design} - holes and outline at actual size.</text>`,
  )

  // panel content is drawn around (0, 0), so the translation is the panel's own
  // centre: the faceplate then occupies x panelX..panelX+w, y panelY..panelY+h
  const body = `
    <g transform="translate(${num(panelX + w / 2)} ${num(panelY + h / 2)}) scale(1 -1)">
${content}
    </g>
${bar.join("\n    ")}`

  return svgDocument({
    width: pageW,
    height: pageH,
    viewBox: `0 0 ${num(pageW)} ${num(pageH)}`,
    background: theme.background,
    content: body,
    title: `Froemix 20HP faceplate - 1:1 print template (${art.design})`,
  })
}

/** Self-contained HTML preview: the same SVG inline, no external resources. */
export const renderPreviewHtml = (svg: string, art: PanelArtwork, notes: string): string => `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8" />
<meta name="viewport" content="width=device-width, initial-scale=1" />
<title>Froemix 20HP faceplate - ${art.design}</title>
<style>
  :root { color-scheme: dark; }
  * { box-sizing: border-box; }
  html, body { max-width: 100%; overflow-x: hidden; }
  body { margin: 0; padding: 16px; background: #0b0b0c; color: #e6e6e2;
         font: 14px/1.5 ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
         overflow-wrap: anywhere; }
  h1 { font-size: 16px; font-weight: 600; letter-spacing: 0.06em; margin: 0 0 4px; }
  p { color: #9a9aa2; margin: 0 0 16px; max-width: 720px; }
  .panel { display: block; width: 100%; max-width: min(100%, 420px); height: auto;
           margin: 0 auto; box-shadow: 0 0 0 1px #2a2a2e; }
  dl { display: grid; grid-template-columns: max-content minmax(0, 1fr); gap: 2px 12px;
       margin: 16px 0 0; max-width: 720px; }
  dt { color: #74747c; }
  dd { margin: 0; min-width: 0; }
</style>
</head>
<body>
<h1>FROEMIX 20HP faceplate - ${art.design}</h1>
<p>${art.design === "nord" ? "Restrained studio direction (recommended default, not an owner selection)." : "Bolder technical direction: output brackets carry the weight."} White ink on black mask, 2mm FR4. The panel drawing below is generated from the built circuit JSON, so it is the same geometry that is exported for JLCPCB. Dashed circles are installed hardware keepouts, drawn as an illustration only.</p>
${svg}
<dl>
  <dt>outline</dt><dd>${art.outline.width} x ${art.outline.height}mm, ${art.outline.thickness}mm FR4</dd>
  <dt>window</dt><dd>${art.window.width} x ${art.window.height}mm at (${art.window.centre.x}, ${art.window.centre.y}), R${art.window.radius} corners</dd>
  <dt>strokes</dt><dd>minimum ${Math.min(...art.elements.map((e) => e.strokeWidth))}mm</dd>
  <dt>copper</dt><dd>none on either side</dd>
</dl>
<p>${notes}</p>
</body>
</html>
`
