// midibrain mechanical view — a local STEP viewer for the mechanical models.
//
// Why a custom page: tscircuit's 3D view only draws parts that belong to a
// board circuit, so the panel, the display and the composed assembly never
// appear there. This page renders the real STEP files with OpenCascade
// (occt-import-js) and three.js, entirely offline once `npm install` has run.
//
// It is a viewing aid. Nothing here is a fit approval, and the models are
// vendor CAD (Waveshare) or generated placeholders (mechanical/step_assembly.py).
import * as THREE from "three"
import { OrbitControls } from "three/addons/controls/OrbitControls.js"

const statusEl = document.getElementById("status")
const readout = {
  files: [], meshes: 0, status: "starting", error: null, silk: null, encoders: [],
  encoderWarnings: [],
}
const modelGroups = []
window.__viewer = readout
window.__scene = { camera: null, controls: null, scene: null }

const setStatus = (text, isError = false) => {
  readout.status = text
  readout.error = isError ? text : null
  statusEl.textContent = text
  statusEl.classList.toggle("error", isError)
}

// ---------------------------------------------------------------- three scene
const wrap = document.getElementById("wrap")
const renderer = new THREE.WebGLRenderer({ antialias: true })
renderer.setPixelRatio(Math.min(devicePixelRatio, 2))
renderer.localClippingEnabled = true
wrap.appendChild(renderer.domElement)

const scene = new THREE.Scene()
scene.background = new THREE.Color(0x14161a)
const camera = new THREE.PerspectiveCamera(40, 1, 1, 5000)
const controls = new OrbitControls(camera, renderer.domElement)
controls.enableDamping = true

scene.add(new THREE.HemisphereLight(0xffffff, 0x30343c, 1.1))
const key = new THREE.DirectionalLight(0xffffff, 1.6)
key.position.set(160, 260, 220)
scene.add(key)
const fill = new THREE.DirectionalLight(0xffffff, 0.5)
fill.position.set(-220, -120, -160)
scene.add(fill)

const grid = new THREE.GridHelper(400, 40, 0x2a2f37, 0x232833)
// Floor, 6 mm under the 128.5 mm panel's bottom edge: at scene y 0 the grid and
// the axes would slice through the middle of the panel and draw over its face.
grid.position.y = -70
scene.add(grid)
const axes = new THREE.AxesHelper(40)
// On the floor AND clear of the panel footprint (x -100; the panel spans +-76):
// an axes line on scene z 0 is coplanar with the panel front face, so it paints
// straight over the silkscreen wherever the two overlap.
axes.position.set(-100, grid.position.y, 0)
scene.add(axes)

// The stack runs along SCENE Z: the panel front face is at scene z 0 and the
// stack recedes to negative z, so a camera on +z looks at the panel face. Keep
// panel z <= cut, which is scene z >= -cut, so the constant is the cut itself
// and the default 1e6 leaves everything visible.
const clipPlane = new THREE.Plane(new THREE.Vector3(0, 0, 1), 1e6)
renderer.clippingPlanes = [clipPlane]
const zcut = document.getElementById("zcut")
const applyCut = () => {
  const cut = Number(zcut.value)
  clipPlane.constant = Number.isFinite(cut) ? cut : 1e6
  readout.zCut = Number.isFinite(cut) ? cut : null
}
zcut.addEventListener("input", applyCut)

// STEP coordinates are millimetres in the PANEL frame: x right, y up, z
// REARWARD (into the rack, away from the operator). That frame is left-handed
// against a viewer whose +z points at you, so loading x/y/z straight into three
// is what made every front-face layout read mirrored from the rear. Mapping
// z -> -z turns it into the viewer's right-handed frame: the panel face lands
// at scene z 0, the stack recedes to -z, and a camera on +z shows screen-right
// = +x and screen-up = +y, i.e. the same way tscircuit's 3D view draws it.
// A model's own STEP z, though, is NOT the panel frame: it runs from the
// board's mid-plane out of its panel-facing side, i.e. the other way round, so
// the manifest negates it again (scaleZ = -1) and shifts it (positionZ).
const toScene = (x, y, z) => new THREE.Vector3(x, y, -z)

// ------------------------------------------------------------------ STEP load
const PALETTE = [0x8fb7d6, 0xd6b98f, 0x9fd6a4, 0xd69fb7, 0xc7c7d6, 0xd6d08f, 0x8fd6d0]

async function loadStep(url, colourIndex) {
  const response = await fetch(url)
  if (!response.ok) throw new Error(`${url}: HTTP ${response.status}`)
  const buffer = new Uint8Array(await response.arrayBuffer())
  const occt = window.__occt
  const result = occt.ReadStepFile(buffer, {
    linearUnit: "millimeter",
    linearDeflectionType: "bounding_box_ratio",
    linearDeflection: 0.0008,
    angularDeflection: 0.4,
  })
  if (!result.success) throw new Error(`${url}: OpenCascade could not read the file`)

  const group = new THREE.Group()
  group.name = url
  const colour = PALETTE[colourIndex % PALETTE.length]
  const material = new THREE.MeshStandardMaterial({
    color: colour,
    metalness: 0.15,
    roughness: 0.62,
    side: THREE.DoubleSide,
    flatShading: false,
  })

  for (const mesh of result.meshes) {
    const position = mesh.attributes.position.array
    const geometry = new THREE.BufferGeometry()
    const vertices = new Float32Array((position.length / 3) * 3)
    for (let i = 0; i < position.length; i += 3) {
      const v = toScene(position[i], position[i + 1], position[i + 2])
      vertices[i] = v.x
      vertices[i + 1] = v.y
      vertices[i + 2] = v.z
    }
    geometry.setAttribute("position", new THREE.BufferAttribute(vertices, 3))
    if (mesh.index?.array?.length) geometry.setIndex(Array.from(mesh.index.array))
    geometry.computeVertexNormals()
    group.add(new THREE.Mesh(geometry, material))
  }

  const box = new THREE.Box3().setFromObject(group)
  return { group, box, materials: [material], meshes: result.meshes.length }
}

// ------------------------------- PEC11R encoder STEP datum (WORKAROUND)
// The vendor STEP behind U6 (Bourns PEC11R-4015F-S0024, LCSC C143789) draws its
// SEATING PLANE at model-local z -4.50 instead of on z 0: the large down-facing
// face spanning the 12.5 mm body is 4.50 mm below the model origin, while the
// -4.70 planes beside the terminals and the lugs that reach -8.20 belong to the
// terminal shoulders, not to the seat. tscircuit's STEP exporter drops
// `model_origin_position` completely, so the exported front.step carries the
// body 4.50 mm BELOW the front board's top face (seating face at scene z -15.10
// against a board top of -10.60) even though its own 3D view, which reads the
// same part through the OBJ importer, looks right. That is why
// tscircuit/imports/PEC11R_4015F_S0024.tsx keeps the vendor
// `modelOriginPosition`: changing it moves the 3D view and cannot move the STEP.
//
// So the viewer finds the defect by its SIGNATURE and lifts that one mesh. This
// is a WORKAROUND for an exporter defect, NOT a model fix, and it SELF-HEALS:
// if the signature is absent (exporter fixed upstream, or a re-datumed STEP is
// vendored) nothing is shifted. "Absent" is NOT evidence that the model is now
// right - it only means this viewer cannot identify the encoder in it, and the
// page says so. Exactly ONE mesh must match: more than one is ambiguous, and an
// ambiguous match is refused and shown on the page, not guessed. Do not turn
// this into an unconditional offset, and do not delete it while front.step still
// draws the body 4.5 mm low - see mechanical/viewer/README.md and docs/TODO.md.
//
// NOTHING IN THIS VIEWER PROVES THE ENCODER'S PHYSICAL FIT. It draws a STEP
// model; it does not measure a part, and a guard that passes only says the
// shifted MESH sits where the arithmetic expects.
const ENCODER_SHIFT_MM = 4.5 // seating plane -> board top face, measured off front.step
const ENCODER_SIGNATURE_BELOW_BOARD_MM = 4 // a mesh reaching this far behind the board...
const ENCODER_SIGNATURE_ABOVE_BOARD_MM = 8 // ...and this far in front of it is the encoder
const ENCODER_SEAT_TOL_MM = 0.05
// The shaft height the drawn STEP reaches after the shift, against the height the
// vendor's own L15 drawing dimensions. They DISAGREE and the difference is
// UNRESOLVED: the drawn shaft is 6.00 mm longer than the drawing's, so the mesh
// number is a property of this model, not a part measurement and not a fit.
const ENCODER_MESH_SHAFT_TIP_MM = 27.5 // bbox top above the board's top face, front.step
const ENCODER_DRAWING_SHAFT_TIP_MM = 21.5 // vendor L15 drawing, PCB top -> shaft tip
// A model's plate only counts as its board when its footprint matches the
// documented board. The assembly export can carry other 1.2-2.0 mm plates (the
// panel is one), and shifting a mesh against the wrong plate would move the
// encoder to a place nothing checked.
const BOARD_FOOTPRINT_TOL_MM = 1.5

function meshBox(mesh) {
  mesh.geometry.computeBoundingBox()
  return mesh.geometry.boundingBox.clone().applyMatrix4(mesh.matrixWorld)
}

// The board of a board export is the flat plate covering the most area; every
// part height is measured from its two faces. The largest such plate is NOT
// necessarily the board - the assembly export also carries the panel, and any
// plate in the 1.2-2.0 mm window could win - so the caller hands in the board's
// documented footprint and a plate that does not match it is refused instead of
// guessed at.
function boardFaces(group, board) {
  let slab = null
  group.traverse((object) => {
    if (!object.isMesh) return
    const box = meshBox(object)
    const thickness = box.max.z - box.min.z
    if (thickness < 1.2 || thickness > 2.0) return
    const width = box.max.x - box.min.x
    const height = box.max.y - box.min.y
    const area = width * height
    if (!slab || area > slab.area) {
      slab = { area, top: box.max.z, bottom: box.min.z, width, height, thickness }
    }
  })
  if (!slab) return { slab: null, reason: "no plate in the 1.2-2.0 mm slab window" }
  if (!board) return { slab, reason: null, checked: false }
  const matches =
    Math.abs(slab.width - board.width) <= BOARD_FOOTPRINT_TOL_MM &&
    Math.abs(slab.height - board.height) <= BOARD_FOOTPRINT_TOL_MM
  if (!matches) {
    return {
      slab,
      checked: true,
      matches: false,
      reason:
        `the largest plate is ${slab.width.toFixed(2)} x ${slab.height.toFixed(2)} mm, ` +
        `not the documented ${board.kind} board (${board.width} x ${board.height} mm)`,
    }
  }
  return { slab, checked: true, matches: true, reason: null }
}

// One visible place for anything this viewer must not hide: the encoder datum
// was refused or a guard failed. A console line alone is not enough - the page
// is what a person reads.
function warnOnPage(text) {
  readout.encoderWarnings.push(text)
  console.warn(`encoder datum: ${text}`)
  // The element lives in the HUD, NOT in #models: renderToggles() clears
  // #models, and a warning that disappears when the board list is drawn is
  // worse than no warning. It is created once and then only updated.
  let el = document.getElementById("encoder-warning")
  if (!el) {
    el = document.createElement("div")
    el.id = "encoder-warning"
    el.style.color = "#e2703a"
    el.style.maxWidth = "52ch"
    el.style.whiteSpace = "normal"
    el.style.fontSize = "12px"
    document.getElementById("hud").appendChild(el)
  }
  el.textContent = `viewer warning (${readout.encoderWarnings.length}): ` +
    readout.encoderWarnings.join("; ")
}

// The seating plane is the body's largest face turned toward the board. It is
// read off the drawn mesh rather than from a constant, so the guard measures the
// geometry that is actually on screen.
const faceA = new THREE.Vector3(), faceB = new THREE.Vector3(), faceC = new THREE.Vector3()
const faceAB = new THREE.Vector3(), faceAC = new THREE.Vector3(), faceN = new THREE.Vector3()
function seatingFaceZ(meshes) {
  const areas = new Map()
  for (const mesh of meshes) {
    const position = mesh.geometry.attributes.position
    const index = mesh.geometry.index
    const count = index ? index.count : position.count
    for (let i = 0; i < count; i += 3) {
      const a = index ? index.getX(i) : i
      const b = index ? index.getX(i + 1) : i + 1
      const c = index ? index.getX(i + 2) : i + 2
      faceA.fromBufferAttribute(position, a).applyMatrix4(mesh.matrixWorld)
      faceB.fromBufferAttribute(position, b).applyMatrix4(mesh.matrixWorld)
      faceC.fromBufferAttribute(position, c).applyMatrix4(mesh.matrixWorld)
      faceN.crossVectors(faceAB.subVectors(faceB, faceA), faceAC.subVectors(faceC, faceA))
      const length = faceN.length()
      if (length === 0 || faceN.z > -0.9 * length) continue // keep faces turned toward -scene z
      const z = Math.round(((faceA.z + faceB.z + faceC.z) / 3) * 50) / 50
      areas.set(z, (areas.get(z) ?? 0) + length / 2)
    }
  }
  let seat = null
  for (const [z, area] of areas) if (!seat || area > seat.area) seat = { z, area }
  return seat
}

// Lift the encoder mesh only when the defect signature is present, then assert
// the result. Every outcome is one of four named states and each is visible:
//   "applied"        exactly one match, shifted, guard passed
//   "guard-failed"   exactly one match, shifted, guard FAILED (or no seat found)
//   "ambiguous"      more than one match - refused, no shift
//   "not-applicable" no match or no board - no shift, and NOT a failure
function correctEncoderDatum(group, model) {
  group.updateMatrixWorld(true)
  if (!model.board) {
    // The heuristic is only valid where the manifest says which board is in the
    // file. A reference/vendor STEP, the pi-panel or an unlabelled export must
    // never be shifted, so the correction is REFUSED rather than guessed.
    const refused = {
      model: model.label,
      status: "not-applicable",
      applied: false,
      note: "the manifest does not identify a front board in this file - encoder correction refused",
    }
    readout.encoders.push(refused)
    console.info(`encoder datum: ${model.label}: ${refused.note}`)
    return
  }
  if (model.board.carriesFrontBoard === false) {
    const refused = {
      model: model.label,
      status: "not-applicable",
      applied: false,
      note: "the manifest marks this file as carrying no front board - encoder correction refused",
    }
    readout.encoders.push(refused)
    console.info(`encoder datum: ${model.label}: ${refused.note}`)
    return
  }
  const found = boardFaces(group, model.board)
  const board = found.slab
  const entry = {
    model: model.label,
    status: "not-applicable",
    applied: false,
    boardChecked: found.checked ?? false,
    boardMatches: found.matches ?? null,
    boardTopZ: board ? +board.top.toFixed(2) : null,
    boardBottomZ: board ? +board.bottom.toFixed(2) : null,
    signatureMeshes: 0,
  }
  readout.encoders.push(entry)
  if (!board) {
    entry.note = `no board plate: ${found.reason}`
    return
  }
  if (found.checked && !found.matches) {
    entry.note = `no shift: ${found.reason}`
    warnOnPage(`${model.label}: ${found.reason}; no encoder datum shift applied`)
    return
  }
  const hits = []
  group.traverse((object) => {
    if (!object.isMesh) return
    const box = meshBox(object)
    if (box.min.z < board.bottom - ENCODER_SIGNATURE_BELOW_BOARD_MM &&
        box.max.z > board.top + ENCODER_SIGNATURE_ABOVE_BOARD_MM) hits.push({ mesh: object, box })
  })
  entry.signatureMeshes = hits.length
  if (hits.length === 0) {
    // No match is NOT a fix and NOT a failure: it means this viewer cannot
    // identify the encoder in this file, so it shifts nothing and says only that.
    entry.note = "no mesh matches the defect signature (0 of 1) - no shift applied; " +
      "this neither confirms nor clears the encoder model"
    console.info(`encoder datum: ${model.label}: ${entry.note}`)
    return
  }
  if (hits.length > 1) {
    // One encoder, so more than one match means the signature is no longer
    // specific. Do not guess: leave the model as exported and say so on the page.
    entry.status = "ambiguous"
    entry.note = `${hits.length} meshes match the signature, expected 1 - no shift applied`
    warnOnPage(`${model.label}: ${entry.note}`)
    return
  }
  const { mesh, box: before } = hits[0]
  const originalZ = mesh.position.z
  // Put the mesh back exactly as exported. A shift that cannot be checked must
  // not stay in the scene: a wrong shift is worse than the defect it hides.
  const restore = () => {
    mesh.position.z = originalZ
    mesh.updateMatrixWorld(true)
    entry.applied = false
    entry.restored = true
  }
  // The mesh lives in the model's group, whose z the manifest scales by -1, so a
  // SCENE +z is a LOCAL -z.
  mesh.position.z += ENCODER_SHIFT_MM / (model.scaleZ ?? 1)
  mesh.updateMatrixWorld(true)
  const after = meshBox(mesh)
  const seat = seatingFaceZ([mesh])
  if (!seat) {
    // No face turned toward the board: the guard cannot run, so it FAILS rather
    // than assume the shift was right, and the shift is REVERSED. Must not throw.
    restore()
    entry.status = "guard-failed"
    entry.note = "no seating face found after the shift - guard could not run, shift reverted"
    warnOnPage(`${model.label}: ${entry.note}`)
    return
  }
  const seatDelta = seat.z - board.top
  const guard = {
    seatOnBoardTop: Math.abs(seatDelta) <= ENCODER_SEAT_TOL_MM,
    shaftTowardPanel: after.max.z > board.top && after.max.z > seat.z,
    legsThroughBoard: after.min.z < board.bottom,
  }
  entry.ok = guard.seatOnBoardTop && guard.shaftTowardPanel && guard.legsThroughBoard
  if (!entry.ok) {
    // A guard failure means the shifted position is not proven: put the mesh back
    // and say so, rather than leave a wrong datum on the page.
    const failedBox = after
    restore()
    entry.before = { z: [+before.min.z.toFixed(2), +before.max.z.toFixed(2)] }
    entry.shifted = { z: [+failedBox.min.z.toFixed(2), +failedBox.max.z.toFixed(2)] }
    entry.seat = { z: +seat.z.toFixed(2), areaMm2: +seat.area.toFixed(1), deltaFromBoardTopMm: +seatDelta.toFixed(3) }
    entry.guard = guard
    entry.status = "guard-failed"
    entry.note = "encoder datum guard FAILED - shift reverted, model left as exported"
    warnOnPage(`${model.label}: ${entry.note} (seat delta ${seatDelta.toFixed(3)} mm)`)
    console.info(
      `encoder datum: GUARD FAILED in ${model.label}; shift reverted; ` +
      `guard ${JSON.stringify(guard)}; seat delta ${seatDelta.toFixed(3)} mm`)
    return
  }
  entry.applied = true
  entry.shiftMm = ENCODER_SHIFT_MM
  entry.before = { z: [+before.min.z.toFixed(2), +before.max.z.toFixed(2)] }
  entry.after = { z: [+after.min.z.toFixed(2), +after.max.z.toFixed(2)] }
  entry.seat = { z: +seat.z.toFixed(2), areaMm2: +seat.area.toFixed(1), deltaFromBoardTopMm: +seatDelta.toFixed(3) }
  entry.guard = guard
  entry.status = "applied"
  // The drawn shaft against the vendor drawing. Both numbers are reported and
  // the disagreement is named as UNRESOLVED: the mesh is not a measured part.
  entry.shaft = {
    meshAboveBoardMm: +(after.max.z - board.top).toFixed(2),
    drawingL15AboveBoardMm: ENCODER_DRAWING_SHAFT_TIP_MM,
    differenceMm: +((after.max.z - board.top) - ENCODER_DRAWING_SHAFT_TIP_MM).toFixed(2),
    status: "UNRESOLVED - the drawn shaft in this STEP is longer than the vendor's L15 dimension; " +
      "neither number is a measurement and neither is a fit result",
  }
  readout.encoderFit = {
    status: "UNRESOLVED",
    meshShaftTipAboveBoardMm: ENCODER_MESH_SHAFT_TIP_MM,
    drawingL15ShaftTipAboveBoardMm: ENCODER_DRAWING_SHAFT_TIP_MM,
    note: "the STEP model draws a 27.50 mm shaft tip where the vendor L15 drawing gives 21.50 mm; " +
      "the viewer shows the model, proves nothing about the part, and no physical fit is claimed",
  }
  console.info(
    `encoder datum: WORKAROUND +${ENCODER_SHIFT_MM.toFixed(2)} mm scene z in ${model.label}; ` +
    `board top ${board.top.toFixed(2)} bottom ${board.bottom.toFixed(2)}; ` +
    `mesh z ${before.min.z.toFixed(2)}..${before.max.z.toFixed(2)} -> ${after.min.z.toFixed(2)}..${after.max.z.toFixed(2)}; ` +
    `seating face ${seat.z.toFixed(2)} (${seatDelta.toFixed(3)} mm from the board top, ${seat.area.toFixed(1)} mm2); ` +
    `drawn shaft tip ${(after.max.z - board.top).toFixed(2)} mm above the board ` +
    `(vendor L15 drawing ${ENCODER_DRAWING_SHAFT_TIP_MM.toFixed(2)} mm - UNRESOLVED); ` +
    `legs ${(board.bottom - after.min.z).toFixed(2)} mm behind its far face; ` +
    `guard ${entry.ok ? "ok" : "FAILED"}`)
}


// ------------------------------------------- display carrier + spacers (sketch)
// The 4.3" module hangs on a printed carrier bolted to the REAR board's front
// face on adjustable spacers - without them the module floats in the gap and
// reads as unmounted. There is no STEP for the carrier yet (open item
// `display_boss_pattern`), so this draws the documented geometry as a sketch:
// four spacer cylinders of displayStandoff.spacerLength between the module's
// rear plane and the rear board's front face, at the module's own boss pattern.
// Lengths and positions come from mechanical/layout.json; the diameters are
// placeholders and are named as such.
const CARRIER_D = 5.0 // placeholder: spacer OD
const CARRIER_PAD_D = 9.0 // placeholder: carrier pad footprint
const CARRIER_PAD_Z = 1.5 // placeholder: pad thickness at the rear-board face

function buildCarrier(spec) {
  if (!spec) return
  const group = new THREE.Group()
  group.name = "display carrier (placeholder)"
  const material = new THREE.MeshStandardMaterial({
    color: 0xb8b0a0, metalness: 0.1, roughness: 0.8,
  })
  for (const boss of spec.bosses) {
    // boss is in board coordinates; scene x/y = board x/y, and the board's z
    // (rearward) is scene -z. The spacers therefore stand along SCENE Z, in the
    // gap behind the module and in front of the rear board's own face.
    const x = boss.x + spec.offsetX
    const y = boss.y + spec.offsetY
    const h = spec.rearBoardFront - spec.moduleRear
    const spacer = new THREE.Mesh(new THREE.CylinderGeometry(CARRIER_D / 2, CARRIER_D / 2, h, 20), material)
    spacer.rotation.x = Math.PI / 2 // cylinder axis Y -> Z
    spacer.position.set(x, y, -(spec.moduleRear + h / 2))
    group.add(spacer)
    const pad = new THREE.Mesh(new THREE.CylinderGeometry(CARRIER_PAD_D / 2, CARRIER_PAD_D / 2, CARRIER_PAD_Z, 20), material)
    pad.rotation.x = Math.PI / 2
    pad.position.set(x, y, -(spec.rearBoardFront - CARRIER_PAD_Z / 2))
    group.add(pad)
  }
  group.visible = true
  scene.add(group)
  modelGroups.push({ group, model: { label: "placeholder: display carrier + 4 spacers" } })
}

// --------------------------------------------------------------- panel silk
// A STEP carries surfaces, not printed ink, so the panel's silkscreen exists
// only in tscircuit's board data. `make mechanical-steps` exports the panel's
// top-layer PCB SVG alongside the STEPs and it is mapped here as a TEXTURE:
// JLCPCB's silkscreen is flat ink on the FR-4, so extruding it into geometry
// would draw a part that is not on the panel. The plane sits a twentieth of a
// millimetre in FRONT of the panel face (scene z 0, which is where the panel
// STEP's front surface is by construction) and writes no depth, so it cannot
// z-fight its own substrate.
const SILK_STANDOFF = 0.05 // mm in front of the panel front face
const SILK_PX_PER_MM = 12 // 2.6x the exported drawing (4.6 px/mm), so 5 mm text stays crisp

function keepSilkOnly(svg) {
  // The export draws the board edge, the cutouts and the drills too; the STEP
  // already carries those as real holes, and only ink belongs on the panel face.
  const board = svg.querySelector("[data-type='pcb_boundary']")
  const crop = board
    ? ["x", "y", "width", "height"].map((a) => Number(board.getAttribute(a)))
    : [0, 0, Number(svg.getAttribute("width")), Number(svg.getAttribute("height"))]
  for (const el of svg.querySelectorAll("[data-type]")) {
    if (!el.getAttribute("data-type").startsWith("pcb_silkscreen_")) el.remove()
  }
  return crop
}

async function buildSilk(spec, base) {
  const response = await fetch(new URL(spec.url, base), { cache: "no-store" })
  if (!response.ok) throw new Error(`${spec.url}: HTTP ${response.status}`)
  // The exported SVG embeds its own TscircuitAlphabet font as a data URL, so the
  // raster keeps the drawn glyphs instead of the browser's fallback face.
  const svg = new DOMParser().parseFromString(await response.text(), "image/svg+xml").documentElement
  const [cx, cy, cw, ch] = keepSilkOnly(svg)
  const image = new Image()
  image.src = "data:image/svg+xml;charset=utf-8," +
    encodeURIComponent(new XMLSerializer().serializeToString(svg))
  await image.decode()
  const canvas = document.createElement("canvas")
  canvas.width = Math.round(spec.width * SILK_PX_PER_MM)
  canvas.height = Math.round(spec.height * SILK_PX_PER_MM)
  canvas.getContext("2d").drawImage(image, cx, cy, cw, ch, 0, 0, canvas.width, canvas.height)

  const texture = new THREE.CanvasTexture(canvas)
  texture.colorSpace = THREE.SRGBColorSpace
  texture.anisotropy = renderer.capabilities.getMaxAnisotropy()
  const plane = new THREE.Mesh(
    // The panel is centred on panel x = y = 0; CanvasTexture flips the image
    // rows (flipY), so the artwork's top row lands on the plane's +y side.
    new THREE.PlaneGeometry(spec.width, spec.height),
    new THREE.MeshBasicMaterial({ map: texture, transparent: true, depthWrite: false }),
  )
  const group = new THREE.Group()
  group.name = "panel silkscreen (texture)"
  group.position.z = SILK_STANDOFF
  group.add(plane)
  scene.add(group)
  modelGroups.push({ group, model: { label: "silk (panel artwork, texture)" } })
  readout.silk = `panel silkscreen texture, ${spec.width} x ${spec.height} mm`
}

// ------------------------------------------------------------- model toggles
// Loading six solids at once is unreadable, and the STACK_MM placeholder boxes
// that used to live here were a sketch of the pre-respin stack (128 mm board at
// z 17.45). Both are gone: the manifests now carry real placement computed from
// mechanical/layout.json, and the stack is built up by ticking models.
function renderToggles() {
  const host = document.getElementById("models")
  host.textContent = ""
  if (!modelGroups.length) return
  for (const { group, model } of modelGroups) {
    const label = document.createElement("label")
    label.className = "toggle"
    const input = document.createElement("input")
    input.type = "checkbox"
    input.checked = group.visible
    input.addEventListener("change", () => {
      group.visible = input.checked
      const entry = readout.files.find((f) => f.label === model.label)
      if (entry) entry.visible = input.checked
      const shown = modelGroups.filter((g) => g.group.visible)
      if (shown.length) {
        const box = new THREE.Box3()
        for (const g of shown) box.union(new THREE.Box3().setFromObject(g.group))
        fitCamera(box)
      }
    })
    label.appendChild(input)
    label.appendChild(document.createTextNode(" " + model.label.replace(/^\w+: /, "")))
    host.appendChild(label)
  }
}

// ------------------------------------------------------------------- manifest
async function main() {
  applyCut()
  if (!window.occtimportjs) throw new Error("occt-import-js did not load")
  setStatus("loading OpenCascade…")
  window.__occt = await window.occtimportjs({
    locateFile: (file) => new URL(`./node_modules/occt-import-js/dist/${file}`, import.meta.url).href,
  })

  const manifestUrl = new URL("./models.json", import.meta.url)
  const response = await fetch(manifestUrl, { cache: "no-store" })
  if (!response.ok) throw new Error(`models.json: HTTP ${response.status} (run make mechanical-view)`)
  const manifest = await response.json()
  if (!manifest.models?.length) throw new Error("models.json lists no STEP files")
  // Model paths are relative to the server root (the midibrain directory), not
  // to this file: the manifest carries "base" for that. Without it, fall back to
  // two levels up from mechanical/viewer/.
  const modelBase = new URL(manifest.base ?? "../../", manifestUrl)

  readout.manifest = manifest.models.map((m) => m.label)
  const union = new THREE.Box3()
  let loaded = 0
  let readout_placed = null

  for (const [index, model] of manifest.models.entries()) {
    setStatus(`loading ${model.label}…`)
    const url = new URL(model.url, modelBase)
    const { group, box, materials, meshes } = await loadStep(url.href, index)
    // Placement comes from the manifest, computed from mechanical/layout.json:
    // the export's +z points out of the board's panel-facing side, the opposite
    // of the panel frame, so z is negated once more and shifted until that face
    // lands at its documented panel z (scene z = -panel z). Without this every
    // model sat at its own origin, which is why six boards used to overlap.
    group.scale.x = model.scaleX ?? 1
    group.scale.z = model.scaleZ ?? 1
    // A printed part (the seven button caps) is already in the panel frame, so
    // it carries an x/y move onto its opening and no z transform at all: one
    // STEP file, one copy per panel opening.
    group.position.x = model.positionX ?? 0
    group.position.y = model.positionY ?? 0
    group.position.z = model.positionZ ?? 0
    if (model.align) {
      // Own-frame model: align on the loaded bbox, centre it on the display
      // position, and park the bbox FRONT on frontSceneZ. SCENE Z RUNS NEGATIVE
      // REARWARD (scene z = -panel z), so the glass datum +0.60 is scene -0.60.
      // For the VERIFIED vendor STEP the bbox front is the touch/frame plane,
      // 1.90 mm in front of the glass, so the manifest's frontSceneZ is +1.30;
      // for any other STEP the 1.90 mm offset is NOT applied and the bbox front
      // is aligned to the glass, which the manifest marks as unproven.
      // `scale.z` is applied first, so the scene-space bbox is already final.
      group.updateMatrixWorld(true)
      const b = new THREE.Box3().setFromObject(group)
      const c = b.getCenter(new THREE.Vector3())
      group.position.x += model.align.x - c.x
      group.position.y += model.align.y - c.y
      group.position.z += model.align.frontSceneZ - b.max.z
      group.updateMatrixWorld(true)
      // Read back the PLACED box: b was measured before position.z moved.
      const placed = new THREE.Box3().setFromObject(group)
      readout.displayAlignment = {
        model: model.label,
        modelVerified: model.align.modelVerified,
        bboxFrontIsGlass: model.align.bboxFrontIsGlass,
        glassPanelZ: model.align.glassPanelZ,
        glassSceneZ: model.align.glassSceneZ,
        frameAheadOfGlassMm: model.align.frameAheadOfGlassMm,
        placedBboxFrontSceneZ: +placed.max.z.toFixed(2),
        requestedFrontSceneZ: model.align.frontSceneZ,
        orientation: model.align.orientation,
        placementStatus: model.align.placementStatus,
        fitClaim: "none - this is a drawn model in a viewer, not a fit",
      }
      console.info(
        `display placement: ${model.label} bbox front -> scene z ${model.align.frontSceneZ} ` +
        `(glass datum panel z ${model.align.glassPanelZ}, scene z ${model.align.glassSceneZ}); ` +
        `${model.align.placementStatus}; orientation ${model.align.orientation.status}`)
    }
    group.visible = model.visible !== false
    modelGroups.push({ group, model })
    scene.add(group)
    // Before the placed box is read out, so the readout and the camera show the
    // corrected position (see correctEncoderDatum).
    correctEncoderDatum(group, model)
    // Fit on the PLACED box: `box` from loadStep is in the file's own frame.
    group.updateMatrixWorld(true)
    readout_placed = new THREE.Box3().setFromObject(group)
    if (group.visible) union.union(readout_placed)
    loaded += meshes
    readout.files.push({
      label: model.label,
      url: model.url,
      meshes,
      visible: model.visible !== false,
      panelTopZ: model.panelTopZ ?? null,
      box: {
        x: [+readout_placed.min.x.toFixed(2), +readout_placed.max.x.toFixed(2)],
        y: [+readout_placed.min.y.toFixed(2), +readout_placed.max.y.toFixed(2)],
        z: [+readout_placed.min.z.toFixed(2), +readout_placed.max.z.toFixed(2)],
      },
    })
    group.userData.materials = materials
  }

  readout.meshes = loaded
  if (manifest.silk) {
    try {
      await buildSilk(manifest.silk, modelBase)
    } catch (error) {
      // The silk is an aid drawn on top of the mechanical models: a missing
      // export must not blank the stack, so say what is missing and carry on.
      readout.silk = `unavailable: ${error.message}`
      console.warn(`panel silkscreen ${readout.silk}`)
    }
  }
  buildCarrier(manifest.carrier)
  renderToggles()
  if (!modelGroups.some((g) => g.group.name === "panel silkscreen (texture)")) {
    // No silk block in the manifest, or its SVG did not load. Say so rather than
    // let a panel with no printing read as "this panel has no silkscreen".
    const hint = document.createElement("span")
    hint.className = "hint"
    hint.textContent = "silk: none loaded (make mechanical-steps)"
    document.getElementById("models").appendChild(hint)
  }
  fitCamera(union)
  window.__scene.camera = camera
  window.__scene.scene = scene
  window.__scene.controls = controls
  // Three separate things are reported, and none of them is a fit approval:
  // a guard that failed, an encoder the viewer refused to identify (ambiguous),
  // and the drawn-shaft disagreement. `not-applicable` is not a failure.
  const failed = readout.encoders.filter((e) => e.status === "guard-failed")
  const ambiguous = readout.encoders.filter((e) => e.status === "ambiguous")
  // A refused board match is not a guard failure, but it IS something a reader
  // must see: the encoder was left where the file put it.
  const refused = readout.encoders.filter((e) => e.boardMatches === false)
  const parts = [`loaded ${manifest.models.length} file(s), ${loaded} solids`]
  if (failed.length) parts.push(`ENCODER DATUM GUARD FAILED: ${failed.map((e) => e.model).join(", ")}`)
  if (ambiguous.length) parts.push(`ENCODER IDENTITY AMBIGUOUS: ${ambiguous.map((e) => e.model).join(", ")}`)
  if (refused.length) parts.push(`BOARD NOT IDENTIFIED, encoder left as exported: ${refused.map((e) => e.model).join(", ")}`)
  if (readout.encoderFit?.status === "UNRESOLVED") {
    parts.push("drawn shaft 27.50 mm vs vendor L15 21.50 mm is UNRESOLVED - no fit claimed")
  }
  setStatus(parts.join(" | "), failed.length > 0)
  window.__viewer.ready = true
}

const cameraTarget = new THREE.Vector3()
function fitCamera(box) {
  if (box.isEmpty()) return
  const centre = box.getCenter(new THREE.Vector3())
  const size = box.getSize(new THREE.Vector3())
  const radius = Math.max(size.x, size.y, size.z)
  controls.target.copy(centre)
  // Stand in FRONT of the panel (+z, because the stack recedes to -z) with the
  // viewer's up along +y: the panel face is then what you see, unmirrored. The
  // panel is 152 x 128.5 mm and the vertical field is 40 degrees, so the
  // stand-off has to reach ~1.6 radii for the whole panel plus the stack behind
  // it to stay in frame (0.97 radii framed the middle of the panel and nothing
  // else; 1.3 still cut its top and bottom edges off).
  camera.position.copy(centre).add(new THREE.Vector3(radius * 0.55, radius * 0.42, radius * 1.45))
  camera.up.set(0, 1, 0)
  camera.near = radius / 200
  camera.far = radius * 40
  camera.updateProjectionMatrix()
  controls.update()
  cameraTarget.copy(centre)
}

document.getElementById("fit").addEventListener("click", () => {
  const box = new THREE.Box3()
  scene.traverse((o) => {
    if (o.isMesh && o.parent?.name) box.expandByObject(o.parent)
  })
  if (!box.isEmpty()) fitCamera(box)
})

let wireframe = false
document.getElementById("wire").addEventListener("click", () => {
  wireframe = !wireframe
  scene.traverse((o) => {
    if (o.isMesh) o.material.wireframe = wireframe
  })
})

function resize() {
  const { clientWidth, clientHeight } = wrap
  renderer.setSize(clientWidth, clientHeight)
  camera.aspect = clientWidth / Math.max(clientHeight, 1)
  camera.updateProjectionMatrix()
}
addEventListener("resize", resize)
resize()

renderer.setAnimationLoop(() => {
  controls.update()
  renderer.render(scene, camera)
})

main().catch((error) => setStatus(`error: ${error.message}`, true))
