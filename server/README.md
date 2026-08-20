# server

C++ WebSocket/UDP relay server. Receives point clouds from one or more
capture clients, merges them, and broadcasts a MESH binary frame to all
connected viewers at up to ~30 fps — over WebSocket, KCP/UDP, or both at
once, whichever fits the consumer.

## Architecture

```
Capture clients (producers)
    │  ws://server:8080/ws?session=KEY&role=producer&sensor=ID
    │  binary HOLO frames
    ▼
SessionModelView::updateCloud()
    │  per-sensor latest cloud + calibration transform
    ▼  (30 fps timer — Hub::tick())
SessionModelView::buildFrame()
    │  point-cloud passthrough by default (see note below) → MESH binary frame
    ▼
SessionViewController::tick()
    ├─▶ KCP/UDP consumers (native: preview.exe)      — udp://server:8081
    └─▶ WS viewers (browser: dashboard, viewer/ AR)  — ws://server:8080/ws?session=KEY&role=viewer
```

Point cloud passthrough vs. Marching Cubes: `SessionModelView::buildFrame()`
currently streams the transformed points straight through as loose vertices
(`nTris = 0`) instead of voxelising + Marching Cubes. The isosurface path
(`VoxelGrid`, `marchingCubes()`) is still in the codebase but disabled —
`VoxelGrid::fill()`'s density is normalised by the single highest-density
voxel, which collapses everything else below the isosurface threshold when
any region has an outlier-dense splat (e.g. a hand passing near the
`segmentMinDepthMm` cutoff). Re-enabling it needs that normalisation fixed
(percentile-based or per-voxel clamped, not raw global max) first.

The HTTP dashboard (`:8082` by default) is also served by this process —
see "Dashboard / HTTP API" below.

## Prerequisites

| Tool | Notes |
|---|---|
| CMake ≥ 3.16 | https://cmake.org |
| vcpkg | https://github.com/microsoft/vcpkg |
| IXWebSocket | WebSocket server + client |

## Build (native)

```bash
cd server

# 1. Install dependencies
vcpkg install ixwebsocket

# 2. Configure + build
cmake -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release -j$(nproc)

# 3. Run
./build/server [port]   # default port: 8080
```

## Build + run with Docker

```bash
# From the Holovisualize root:
docker compose up --build

# Or build the image directly from server/:
docker build -t holovisualize-server ./server
docker run -p 8080:8080 holovisualize-server
```

## WebSocket protocol

### Producers (capture clients)

Connect to:
```
ws://host:8080/ws?session=KEY&role=producer&sensor=SENSOR_ID
```

- **Optional first text message** — calibration JSON:
  ```json
  {"type":"calibrate","transform":[m00,m01,...,m33]}
  ```
  16 floats, row-major 4×4 camera-to-world transform.

- **Binary messages** — HOLO point cloud frames (see capture README for format).

### Consumers (viewers)

Two transports, same MESH frame either way — pick based on the client:

**WebSocket** (browsers — dashboard, `viewer/` AR app, Unity, anything that
can't do raw UDP):
```
ws://host:8080/ws?session=KEY&role=viewer
```
Receive-only — the server pushes a binary MESH frame per tick, no messages
expected back. `role` must be exactly `producer` or `viewer`; anything else
(including the old `role=consumer`) gets closed with code `4002`.

**KCP/UDP** (native clients — `preview.exe`; lower overhead, no WS framing):
```
udp://host:8081
```
Register first with a plain (non-KCP) UDP packet: `"HVKC"` + null-terminated
session key. The server replies `"HVKC" + conv(u32 LE) + "OK\0\0"` (or
`"FAIL"` on an invalid key) — `conv` is then the KCP connection ID for all
following KCP traffic. See `SessionViewController::KcpConsumer` /
`preview/src/main.cpp` for a full client implementation.

```
Offset             Size        Type    Description
-----------------  ----------  ------  ------------------
0                  4           u8[4]   Magic: "MESH"
4                  4           u32 LE  nVerts
8                  4           u32 LE  nTris
12 + v*24          4           f32 LE  Vertex[v].x
16 + v*24          4           f32 LE  Vertex[v].y
20 + v*24          4           f32 LE  Vertex[v].z
24 + v*24          4           f32 LE  Vertex[v].nx
28 + v*24          4           f32 LE  Vertex[v].ny
32 + v*24          4           f32 LE  Vertex[v].nz
12+nVerts*24+t*12  4           u32 LE  Triangle[t].i0
...+4              4           u32 LE  Triangle[t].i1
...+8              4           u32 LE  Triangle[t].i2
```

> While Marching Cubes is disabled (see Architecture above), `nTris` is
> always `0` and `nx,ny,nz` carry RGB (0..1 each) instead of a real normal —
> raw points don't have one, so the field's repurposed rather than changing
> the wire format.

## Dashboard / HTTP API

`http://host:8082/` (port configurable, second CLI arg) serves a
self-contained dashboard: session list, per-sensor stats, and a "Live view"
WebGL point-cloud panel (orbit camera, WASD fly movement, bound-box + point-
size sliders).

- `GET /api/status` — JSON snapshot (ports, uptime, per-session stats,
  current `point_size`/`bounds`). CORS-open (`Access-Control-Allow-Origin: *`)
  since the `viewer/` AR app, hosted elsewhere (e.g. Vercel), needs to fetch
  it cross-origin.
- `POST /api/config` — body `{"pointSize":0.02,"minX":...,"maxX":...,...}`
  (all fields optional, only given ones are updated). Sets the
  server-authoritative point size and visual bound-box clip that every
  viewer reads from `/api/status` — the dashboard's own sliders call this on
  change. Persisted to `holovisualize_config.json` next to the executable
  and reloaded on startup, so it survives a restart.

## Tuning

| Parameter | Default | Description |
|---|---|---|
| Point size | 0.02 m | Live-adjustable — dashboard's "Live view" slider, `POST /api/config`, persisted across restarts. |
| Bound box | ±100 m (effectively off) | Same as point size — visual clip only, doesn't affect what's stored/broadcast. |
| Voxel resolution | 128³ | Only matters if Marching Cubes is re-enabled (see Architecture) — second CLI arg to `server.exe`. |
| ISO level | 0.5 | Isosurface threshold, only used if Marching Cubes is re-enabled. `SessionModelView::buildFrame()`. |
| Gaussian splat radius | 2 voxels | Same — `VoxelGrid::fill()`. |

## Gesture / effects system

`IGestureDetector` (`include/IGestureDetector.h`), `GestureType`/`GestureEvent`
(`include/GestureEvent.h`), and `EffectGenerator` + the concrete effects
under `include/effects/` (fire, lightning, shockwave, explosion, vortex,
ice spikes, black hole, spawned procedural shapes — sphere/cube/crystal/
torus/star, or an arbitrary `.obj` mesh) are all implemented and wired up
via `hub.onSessionCreated()` in `main.cpp`, which maps each `GestureType` to
an effect factory.

**No mesh-based `IGestureDetector` is registered.** The interface, the event
type, and every effect it could trigger all exist and work — but
`SessionModelView::addGestureDetector()` is never called, so
`SessionModelView::buildFrame()`'s mesh-detector step
(`for (auto& det : detectors_) ...`) always iterates an empty list.
`IGestureDetector.h`'s doc comment shows what a working one would look like
(`ArmsRaised` via mesh centroid Y height) — this is the right approach for
whole-body pose from raw geometry (no joint/skeletal data is available;
`libfreenect2` is a raw-stream driver only — depth, IR, colour, registration,
not Microsoft's proprietary body-tracking algorithm, see `capture/README.md`).
Nobody has needed a mesh-based gesture yet since hand gestures are covered
below.

**Hand gestures are fed in externally, not detected server-side.**
`capture/`'s [gesture sidecar](../capture/gesture_sidecar/) runs MediaPipe
Hands and classifies static poses (fist, open hand, thumbs up, peace, point,
pinch) from the RGB frame — something the server can't do from point-cloud
geometry alone. The capture client sends recognised gestures as `GEVT`
binary frames over the same WebSocket connection as `HOLO` point clouds;
`ISessionModel::pushGestureEvent()` (implemented in `SessionModelView`)
transforms the camera-space position to world space using that sensor's
calibration and merges it into the same event batch mesh detectors would
feed, right before `EffectGenerator::onGestures()`. So every
`registerEffect(GestureType::X, ...)` mapping in `main.cpp` for a
hand-gesture `GestureType` (`Fist`, `OpenHand`, `Pinch`, `ThumbsUp`,
`PointFinger`, `Peace`) is live — the others (motion/two-hand/full-body
types) stay dead wiring until a detector exists that can emit them.

Wire format — `GEVT` (little-endian), see `capture/include/GestureWire.h`
(encoder) / `decodeGestureEvent()` in `server/src/main.cpp` (decoder):
```
[4]  magic       = "GEVT"
[1]  gestureType (u8, GestureType)
[3]  padding
[4]  x (f32, camera-space metres)
[4]  y
[4]  z
[4]  confidence  (f32, 0..1)
```
