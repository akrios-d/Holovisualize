// Holovisualize AR viewer
//
// No WebXR (iOS Safari doesn't support it) — instead: live camera feed as a
// full-screen <video> background, a Three.js scene on top whose camera
// rotation is driven directly by the device's orientation sensor. There's no
// real positional/6DoF tracking, so "AR" here means rotation-locked: tap the
// screen once to fix the point cloud's world transform relative to wherever
// you're facing at that moment, and it will appear to hold still as you turn
// the phone around it (it will NOT track floor position if you walk).
//
// Connects to the Holovisualize server exactly like the dashboard's Live
// view does (role=viewer over WebSocket, same MESH binary frame format).

import * as THREE from 'three';

// ─── Config from URL (?host=example.com:8080&session=demo) ───────────────────

const params = new URLSearchParams(location.search);
const wsHost = params.get('host') || `${location.hostname}:8080`;
const wsScheme = location.protocol === 'https:' ? 'wss' : 'ws';
const httpScheme = location.protocol === 'https:' ? 'https' : 'http';
const apiHost = params.get('apiHost') || `${location.hostname}:8082`; // HTTP dashboard port
const sessionKey = params.get('session') || 'demo';

// ─── DOM ───────────────────────────────────────────────────────────────────

const videoEl = document.getElementById('camera-feed');
const canvas = document.getElementById('gl-canvas');
const statusEl = document.getElementById('status');
const hintEl = document.getElementById('hint');
const startOverlay = document.getElementById('start-overlay');
const startBtn = document.getElementById('start-btn');

function setHint(text, show) {
  hintEl.textContent = text;
  hintEl.style.opacity = show ? '1' : '0';
}

// ─── MESH decode (matches server/include/MeshFrame.h) ────────────────────────
// [4] magic "MESH", [4] nVerts u32, [4] nTris u32,
// [nVerts*24] x,y,z,nx,ny,nz f32 (nx,ny,nz repurposed as r,g,b 0..1)

function decodeMesh(buf) {
  const dv = new DataView(buf);
  if (dv.getUint8(0) !== 77 || dv.getUint8(1) !== 69 || dv.getUint8(2) !== 83 || dv.getUint8(3) !== 72) return null;
  const nv = dv.getUint32(4, true);
  const positions = new Float32Array(nv * 3);
  const colors = new Float32Array(nv * 3);
  let o = 12;
  for (let i = 0; i < nv; i++) {
    positions[i * 3 + 0] = dv.getFloat32(o, true);
    positions[i * 3 + 1] = dv.getFloat32(o + 4, true);
    positions[i * 3 + 2] = dv.getFloat32(o + 8, true);
    colors[i * 3 + 0] = dv.getFloat32(o + 12, true);
    colors[i * 3 + 1] = dv.getFloat32(o + 16, true);
    colors[i * 3 + 2] = dv.getFloat32(o + 20, true);
    o += 24;
  }
  return { positions, colors };
}

// ─── Three.js scene ────────────────────────────────────────────────────────

const renderer = new THREE.WebGLRenderer({ canvas, alpha: true, antialias: true });
renderer.setPixelRatio(window.devicePixelRatio);
renderer.setSize(window.innerWidth, window.innerHeight);
renderer.setClearColor(0x000000, 0); // transparent — the <video> shows through

const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(60, window.innerWidth / window.innerHeight, 0.05, 50);

window.addEventListener('resize', () => {
  camera.aspect = window.innerWidth / window.innerHeight;
  camera.updateProjectionMatrix();
  renderer.setSize(window.innerWidth, window.innerHeight);
});

// Point cloud — geometry is replaced wholesale on each incoming frame.
// Custom shader (not PointsMaterial) so the server-defined bound box can
// clip points — same uBoundsMin/Max approach as the dashboard's Live view,
// kept in sync via the same /api/status the dashboard writes to.
const geometry = new THREE.BufferGeometry();
const material = new THREE.ShaderMaterial({
  uniforms: {
    uPointSize: { value: 0.02 },       // world-space size, metres
    uFocalPx:   { value: 400.0 },      // canvas.height/2 * projectionMatrix[5], updated on resize/render
    uBoundsMin: { value: new THREE.Vector3(-1e4, -1e4, -1e4) },
    uBoundsMax: { value: new THREE.Vector3( 1e4,  1e4,  1e4) },
  },
  vertexShader: `
    attribute vec3 color;
    uniform float uPointSize;
    uniform float uFocalPx;
    uniform vec3 uBoundsMin;
    uniform vec3 uBoundsMax;
    varying vec3 vColor;
    void main(){
      vec4 mvPosition = modelViewMatrix * vec4(position, 1.0);
      gl_Position = projectionMatrix * mvPosition;
      bool inBounds = all(greaterThanEqual(position, uBoundsMin)) && all(lessThanEqual(position, uBoundsMax));
      gl_PointSize = inBounds ? (uPointSize * uFocalPx / -mvPosition.z) : 0.0;
      vColor = color;
    }`,
  fragmentShader: `
    varying vec3 vColor;
    void main(){ gl_FragColor = vec4(vColor, 1.0); }`,
});
const points = new THREE.Points(geometry, material);
points.visible = false; // hidden until the first tap places it

// Anchor group — its transform is fixed once at tap time. The point cloud
// itself is a child, positioned relative to the anchor (so future frames
// just update the geometry, not the placement).
const anchor = new THREE.Group();
anchor.add(points);
scene.add(anchor);

let placed = false;

// ─── Device orientation → camera rotation ─────────────────────────────────
// Classic three.js DeviceOrientationControls algorithm.

const zee = new THREE.Vector3(0, 0, 1);
const euler = new THREE.Euler();
const q0 = new THREE.Quaternion();
const q1 = new THREE.Quaternion(-Math.sqrt(0.5), 0, 0, Math.sqrt(0.5)); // -PI/2 around X

let deviceOrientation = null;
let screenOrientationAngle = (screen.orientation && screen.orientation.angle) || window.orientation || 0;

window.addEventListener('orientationchange', () => {
  screenOrientationAngle = (screen.orientation && screen.orientation.angle) || window.orientation || 0;
});

function updateCameraFromOrientation() {
  if (!deviceOrientation) return;
  const alpha = THREE.MathUtils.degToRad(deviceOrientation.alpha || 0);
  const beta = THREE.MathUtils.degToRad(deviceOrientation.beta || 0);
  const gamma = THREE.MathUtils.degToRad(deviceOrientation.gamma || 0);
  const orient = THREE.MathUtils.degToRad(screenOrientationAngle || 0);

  euler.set(beta, alpha, -gamma, 'YXZ');
  camera.quaternion.setFromEuler(euler);
  camera.quaternion.multiply(q1);
  camera.quaternion.multiply(q0.setFromAxisAngle(zee, -orient));
}

// ─── Tap to place ──────────────────────────────────────────────────────────
// No real depth sensing — "place" just means: fix the anchor at a point a
// couple of metres in front of wherever the camera is currently facing, in
// the camera's current rotation frame. From then on the anchor stays put in
// this pseudo-world while the camera keeps rotating with the device.

canvas.addEventListener('pointerdown', () => {
  const forward = new THREE.Vector3(0, 0, -1).applyQuaternion(camera.quaternion);
  anchor.position.copy(forward.multiplyScalar(2.2));
  anchor.quaternion.copy(camera.quaternion);
  placed = true;
  points.visible = true;
  setHint('', false);
});

// ─── Server config (point size, etc.) — server-authoritative, not a local
// control, so every viewer of this session renders consistently and you
// only ever have to tune it in one place. ─────────────────────────────────

async function fetchServerConfig() {
  try {
    const res = await fetch(`${httpScheme}://${apiHost}/api/status`);
    const d = await res.json();
    if (typeof d.point_size === 'number' && d.point_size > 0) {
      material.uniforms.uPointSize.value = d.point_size;
    }
    if (d.bounds) {
      material.uniforms.uBoundsMin.value.set(d.bounds.minX, d.bounds.minY, d.bounds.minZ);
      material.uniforms.uBoundsMax.value.set(d.bounds.maxX, d.bounds.maxY, d.bounds.maxZ);
    }
  } catch (e) {
    // Keep the built-in default if the status endpoint is unreachable.
  }
}

// ─── WebSocket — role=viewer, same protocol as the dashboard ─────────────────

let ws = null;
let frameCount = 0, lastFpsAt = performance.now(), fps = 0;
let pointCount = 0;

function connect() {
  const url = `${wsScheme}://${wsHost}/ws?session=${encodeURIComponent(sessionKey)}&role=viewer`;
  ws = new WebSocket(url);
  ws.binaryType = 'arraybuffer';
  ws.onopen = () => { statusEl.textContent = `connected — ${sessionKey}`; };
  ws.onclose = () => { statusEl.textContent = `disconnected — retrying...`; setTimeout(connect, 2000); };
  ws.onerror = () => { ws.close(); };
  ws.onmessage = (ev) => {
    const decoded = decodeMesh(ev.data);
    if (!decoded) return;
    pointCount = decoded.positions.length / 3;
    geometry.setAttribute('position', new THREE.BufferAttribute(decoded.positions, 3));
    geometry.setAttribute('color', new THREE.BufferAttribute(decoded.colors, 3));
    geometry.computeBoundingSphere();

    frameCount++;
    const now = performance.now();
    if (now - lastFpsAt > 1000) {
      fps = frameCount * 1000 / (now - lastFpsAt);
      frameCount = 0; lastFpsAt = now;
    }
    statusEl.textContent = `${sessionKey} — ${fps.toFixed(1)} fps — ${pointCount.toLocaleString()} pts`;
  };
}

// ─── Start flow (camera + orientation permission need a user gesture) ────────

async function start() {
  // navigator.mediaDevices only exists in a secure context (HTTPS, or
  // localhost) — on a plain http:// LAN address (e.g. from a phone before
  // the tunnel is set up) it's undefined entirely, not just permission-denied.
  if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) {
    throw new Error('Câmera indisponível — precisa de HTTPS (ou localhost). Acesse via túnel/Vercel, não pelo IP local.');
  }

  // iOS 13+ gates DeviceOrientationEvent behind an explicit permission prompt.
  if (typeof DeviceOrientationEvent !== 'undefined' && typeof DeviceOrientationEvent.requestPermission === 'function') {
    const res = await DeviceOrientationEvent.requestPermission();
    if (res !== 'granted') throw new Error('Orientation permission denied');
  }

  const stream = await navigator.mediaDevices.getUserMedia({
    video: { facingMode: 'environment' },
    audio: false,
  });
  videoEl.srcObject = stream;
  await videoEl.play();

  window.addEventListener('deviceorientation', (e) => { deviceOrientation = e; });

  startOverlay.style.display = 'none';
  setHint('Toque na tela para posicionar', true);

  fetchServerConfig();
  connect();
  renderer.setAnimationLoop(render);
}

startBtn.addEventListener('click', () => {
  start().catch((err) => {
    startOverlay.innerHTML = `<p style="color:#f87171">Não foi possível iniciar: ${err.message}</p>`;
  });
});

function render() {
  updateCameraFromOrientation();
  material.uniforms.uFocalPx.value = renderer.domElement.height * 0.5 * camera.projectionMatrix.elements[5];
  renderer.render(scene, camera);
}
