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

**No `IGestureDetector` is actually registered anywhere.** The interface,
the event type, and every effect it could trigger all exist and work — but
`SessionModelView::addGestureDetector()` is never called, so
`SessionModelView::buildFrame()`'s gesture-detection step
(`for (auto& det : detectors_) ...`) always iterates an empty list. Every
`registerEffect(GestureType::X, ...)` call in `main.cpp` is currently dead
wiring until a detector that can actually emit that `GestureType` exists.

**No joint/skeletal data is available to detect gestures from.**
`libfreenect2` (see `capture/README.md`) is a raw-stream driver only — depth,
IR, colour, and depth↔colour registration, not Microsoft's proprietary body-
tracking algorithm (that only ships in the official, Windows-only, closed-
source Kinect for Windows SDK, which this project deliberately dropped in
favour of libfreenect2's cross-platform raw access — see git history,
"drop Kinect SDK/v1"). A future `IGestureDetector` has to work from the
merged `Mesh`/point cloud's raw geometry (bounding-box extent, centroid
position/velocity, point density) — there's no hand/finger/joint
segmentation to build on. `IGestureDetector.h`'s doc comment shows a minimal
working example this way (`ArmsRaised` via mesh centroid Y height) — that
class of gesture (whole-body pose, not fine hand shape) is what's realistic
without joint data.
