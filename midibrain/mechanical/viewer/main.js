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
const readout = { files: [], meshes: 0, status: "starting", error: null, silk: null }
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
    group.position.z = model.positionZ ?? 0
    if (model.align) {
      // Own-frame model: align on the loaded bbox. Centre it on the display
      // position and park its screen face on the panel-front plane (scene z 0),
      // so the screen points at the panel and the module reaches displayRearMost.
      // `scale.z` is applied first, so the scene-space bbox is already final.
      group.updateMatrixWorld(true)
      const b = new THREE.Box3().setFromObject(group)
      const c = b.getCenter(new THREE.Vector3())
      group.position.x += model.align.x - c.x
      group.position.y += model.align.y - c.y
      group.position.z += model.align.frontSceneZ - b.max.z
    }
    group.visible = model.visible !== false
    modelGroups.push({ group, model })
    scene.add(group)
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
  setStatus(`loaded ${manifest.models.length} file(s), ${loaded} solids`)
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
