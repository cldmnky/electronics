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
const filesEl = document.getElementById("files")
const readout = { files: [], meshes: 0, status: "starting", error: null }
window.__viewer = readout

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

const clipPlane = new THREE.Plane(new THREE.Vector3(0, 0, -1), 1e6)
renderer.clippingPlanes = [clipPlane]
const zcut = document.getElementById("zcut")
const applyCut = () => {
  // keep everything with CAD z <= cut; a big number disables the cut
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


// ------------------------------------------------------- stack placeholders
// Boxes for the parts whose STEP we do not have (panel, front board, jacks,
// buttons) drawn from the documented dimensions, so the height relationships are
// visible without a circuit or a vendor file. Heights are the ones in
// tscircuit/lib/layout.ts (PROVISIONAL_Z) and the jack STEP bounding boxes:
//   panel front z=0, panel rear 1.60, front board top 19.05 (gap 17.45),
//   stereo jack top 15.50, mono jack 14.00, B3F+B32 cap top ~10.00.
// This is a sketch of the stack as documented, not measured geometry.
const STACK_MM = {
  panel: { x: 152.0, y: 128.5, z: 1.6, z0: 0.0 },
  frontBoard: { x: 128.0, y: 112.0, z: 1.6, z0: 17.45 },
  jack: { x: 10.0, y: 12.73, z: 15.5, z0: 0.0 },
  button: { x: 12.0, y: 12.0, z: 10.0, z0: 0.0 },
  jacks: [
    { x: -53, y: -48 }, { x: -37, y: -48 }, { x: -21, y: -48 }, { x: -5, y: -48 },
  ],
  buttons: [
    { x: -53, y: -30 }, { x: -37, y: -30 }, { x: -21, y: -30 }, { x: -5, y: -30 },
    { x: 11, y: -30 }, { x: 27, y: -30 }, { x: 43, y: -30 },
  ],
}

function boxMesh({ x, y, z, z0 }, colour, opacity = 0.28) {
  const geometry = new THREE.BoxGeometry(x, z, y)
  const material = new THREE.MeshStandardMaterial({
    color: colour,
    transparent: true,
    opacity,
    roughness: 0.8,
    metalness: 0.0,
    depthWrite: false,
  })
  const mesh = new THREE.Mesh(geometry, material)
  // scene: y is CAD z, so the box centre sits at z0 + z/2 in scene-Y
  mesh.position.set(0, z0 + z / 2, 0)
  return { mesh, material }
}

const stackGroup = new THREE.Group()
stackGroup.name = "stack placeholders"
stackGroup.visible = false

function buildStack() {
  const add = (spec, colour, at, opacity) => {
    const { mesh } = boxMesh(spec, colour, opacity)
    if (at) mesh.position.x = at.x, mesh.position.z = -at.y
    stackGroup.add(mesh)
    return mesh
  }
  add(STACK_MM.panel, 0x4a5568, null, 0.22)
  add(STACK_MM.frontBoard, 0x2f6f4f, null, 0.35)
  for (const at of STACK_MM.jacks) add(STACK_MM.jack, 0x8fb7d6, at, 0.45)
  for (const at of STACK_MM.buttons) add(STACK_MM.button, 0xd6d08f, at, 0.45)
  scene.add(stackGroup)
  return stackGroup
}

document.getElementById("stack").addEventListener("click", () => {
  stackGroup.visible = !stackGroup.visible
  readout.stackVisible = stackGroup.visible
  document.getElementById("stack").textContent = stackGroup.visible
    ? "hide stack boxes"
    : "show stack boxes"
})

// ------------------------------------------------------------------- manifest
async function main() {
  applyCut()
  buildStack()
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

  filesEl.textContent = manifest.models.map((m) => m.label).join(" · ")
  const union = new THREE.Box3()
  let loaded = 0

  for (const [index, model] of manifest.models.entries()) {
    setStatus(`loading ${model.label}…`)
    const url = new URL(model.url, modelBase)
    const { group, box, materials, meshes } = await loadStep(url.href, index)
    scene.add(group)
    union.union(box)
    loaded += meshes
    readout.files.push({
      label: model.label,
      url: model.url,
      meshes,
      box: {
        x: [+box.min.x.toFixed(2), +box.max.x.toFixed(2)],
        y: [+box.min.y.toFixed(2), +box.max.y.toFixed(2)],
        z: [+box.min.z.toFixed(2), +box.max.z.toFixed(2)],
      },
    })
    group.userData.materials = materials
  }

  readout.meshes = loaded
  fitCamera(union)
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
