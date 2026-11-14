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
const readout = { files: [], meshes: 0, status: "starting", error: null }
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
scene.add(grid)
scene.add(new THREE.AxesHelper(40))

// The stack runs along SCENE Y (= CAD z = panel z, panel front at 0). The plane
// used to be (0,0,-1), i.e. scene Z, which is the board's own y axis: it sliced
// the boards lengthways instead of peeling the stack, and its -1 default hid
// everything. Keep panel z <= cut, and start above the whole stack.
const clipPlane = new THREE.Plane(new THREE.Vector3(0, -1, 0), 1e6)
renderer.clippingPlanes = [clipPlane]
const zcut = document.getElementById("zcut")
const applyCut = () => {
  const cut = Number(zcut.value)
  clipPlane.constant = Number.isFinite(cut) ? cut : 1e6
  readout.zCut = Number.isFinite(cut) ? cut : null
}
zcut.addEventListener("input", applyCut)

// CAD is Z-up in millimetres; three is Y-up. x->x, y->-z, z->y.
const toScene = (x, y, z) => new THREE.Vector3(x, z, -y)

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
    // boss is in board coordinates; scene x = board x, scene z = -board y
    const x = boss.x + spec.offsetX
    const z = -(boss.y + spec.offsetY)
    const h = spec.rearBoardFront - spec.moduleRear
    const spacer = new THREE.Mesh(new THREE.CylinderGeometry(CARRIER_D / 2, CARRIER_D / 2, h, 20), material)
    spacer.position.set(x, spec.moduleRear + h / 2, z)
    group.add(spacer)
    const pad = new THREE.Mesh(new THREE.CylinderGeometry(CARRIER_PAD_D / 2, CARRIER_PAD_D / 2, CARRIER_PAD_Z, 20), material)
    pad.position.set(x, spec.rearBoardFront - CARRIER_PAD_Z / 2, z)
    group.add(pad)
  }
  group.visible = true
  scene.add(group)
  modelGroups.push({ group, model: { label: "placeholder: display carrier + 4 spacers" } })
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
    // the export's +z points at the panel front while the panel frame counts z
    // rearward, so z is inverted and shifted until the board's parts-side face
    // lands on its documented panel z. Without this every model sat at its own
    // origin, which is why six boards used to overlap.
    group.scale.x = model.scaleX ?? 1
    group.scale.y = model.scaleY ?? 1
    group.position.y = model.positionY ?? 0
    if (model.align) {
      // Own-frame model: align on the loaded bbox. Centre it on the display
      // position and park its glass face on the panel-front plane, so the
      // screen points at the panel and the module reaches displayRearMost.
      // `scale.y` is applied first, so the scene-space bbox is already final.
      group.updateMatrixWorld(true)
      const b = new THREE.Box3().setFromObject(group)
      const c = b.getCenter(new THREE.Vector3())
      group.position.x += model.align.x - c.x
      group.position.z += -model.align.y - c.z
      group.position.y += model.align.frontSceneY - b.min.y
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
  buildCarrier(manifest.carrier)
  renderToggles()
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
  camera.position.copy(centre).add(new THREE.Vector3(radius * 0.9, radius * 0.75, radius * 1.1))
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
