# viewer

Two browser-based viewers, both JS/Three.js:

- **`index.html` (AR)** — live camera feed + device-orientation-driven camera,
  tap the screen to "place" the live point cloud in front of you. No WebXR
  (see below for why), so there's no real floor/plane detection — placement
  just fixes the point cloud's transform relative to wherever you're facing
  at that moment. Meant for phones (Android Chrome and iOS Safari both work).
- The server's own dashboard (`server/src/main.cpp`, served at
  `http://<server>:8082/`) has a second, simpler live-view point-cloud
  renderer built in — that one's plain WebGL with no Three.js dependency and
  no camera/AR, just an orbit camera. Use it for quick debugging; use this
  `viewer/` app for the AR experience.

## Why no WebXR

WebXR (the API that gives real camera-pose hit-testing — tap the screen and
get an accurate real-world floor position) only works in Chrome on Android.
**Safari on iOS has never shipped WebXR support.** Since the whole point of
this viewer is to work on a recipient's phone without knowing in advance
whether it's Android or iPhone, WebXR wasn't an option. Instead:

- Camera feed as a full-screen `<video>` background.
- `DeviceOrientationEvent` drives the Three.js camera's rotation (classic
  three.js `DeviceOrientationControls` algorithm, ported inline in
  `src/main.js` since that class was removed from three.js core).
- Tapping the screen fixes the point cloud's transform once, using the
  camera's *current* rotation — there's no real positional tracking, so the
  point cloud stays rotationally locked (turn your phone and it holds its
  place) but will not track a physical floor position if you walk around.

This is a deliberate trade-off for iOS compatibility, not a limitation to be
fixed later — a "real" hit-test AR experience on iOS would need a native app
(ARKit) or a paid WebAR SDK (8th Wall, Zappar), neither of which fit here.

## Prerequisites

| Tool | Notes |
|---|---|
| Node.js ≥ 20 | https://nodejs.org — LTS or newer. This machine's install: `winget install OpenJS.NodeJS.LTS`, or download the Windows `.msi` installer directly from nodejs.org and run it. |
| npm | Ships with Node.js — nothing separate to install. |

Verify after installing:
```bash
node --version
npm --version
```

## Build / run

```bash
cd viewer
npm install

# Local dev server (hot reload)
npm run dev

# Production build → dist/
npm run build
```

`npm run dev` serves at `http://localhost:5173`. **Camera and device-
orientation access require a secure context** — `https://` or exactly
`localhost`. Opening the dev server via a LAN IP (`http://192.168.x.x:5173`,
e.g. from a phone before you have HTTPS set up) will *not* prompt for camera
permission at all — `navigator.mediaDevices` is `undefined` outright on an
insecure origin, on every browser, iOS included with no override flag. Test
on the same PC via `localhost` first; test on a phone only once you have the
real deploy (Vercel + a tunnel to the local server — see below).

## Connecting to a server

URL query params configure everything — no build-time config, so the same
`dist/` build works against any server:

| Param | Default | Description |
|---|---|---|
| `?host=` | `<page host>:8080` | Server's WebSocket (producer/viewer) port |
| `?apiHost=` | `<page host>:8082` | Server's HTTP dashboard/API port |
| `?session=` | `demo` | Session key to view |

Example: `https://your-app.vercel.app/?host=my-tunnel.trycloudflare.com&session=demo`

Point size and the visual bound-box clip are **not** URL params — they're
fetched from the server's `/api/status` (and settable via the dashboard's
Live view sliders, which `POST /api/config`), so every viewer of a session
renders consistently without needing per-link tuning. See
`server/README.md` for that API.

## Deploying (Vercel)

This is a static site (`dist/` after `npm run build`) — Vercel's default
"other" framework preset (or explicitly: build command `npm run build`,
output directory `dist`) works with no server-side config. The C++ server
itself does **not** run on Vercel (it's a stateful process — persistent UDP
socket, session registry — Vercel is serverless/stateless only). Keep
`server.exe` running wherever it already runs (a local PC, a VPS, ...) and
expose it to the internet with a tunnel (Cloudflare Tunnel, ngrok, ...) so
the Vercel-hosted page can reach it over `wss://`/`https://` — a plain
`ws://`/`http://` origin gets blocked as mixed content once the viewer page
itself is served over HTTPS.
