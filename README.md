# Holovisualize

Real-time holographic call system. One or more depth sensors capture a person
in 3D, stream the point cloud to a C++ server that generates a closed mesh via
Marching Cubes, and any connected viewer renders it live.

```
[Kinect v2]  ──── C++ capture client ────▶  filter → segment → point cloud
                                                     │  HOLO binary (WebSocket)
                                                     ▼
                                         [C++ server — port 8080]
                                          voxelise → Marching Cubes → mesh
                                                     │  MESH binary (WebSocket)
                       ┌─────────────────────────────┴──────────────────────┐
                       ▼                                                     ▼
                [preview]                                             [Unity / Three.js]
            OpenGL desktop window                                  holographic viewer
            (local diagnostic)
```

## Project structure

```
Holovisualize/
├── capture/     C++  — sensor capture, pipeline, WebSocket sender (fork: see capture/README.md)
├── server/      C++  — relay server, dashboard + live view, KCP/WS broadcast
├── preview/     C++  — local OpenGL preview window (like LiveScan3D)
└── viewer/      JS   — Three.js phone AR viewer (camera + orientation, tap to place)
```

Each sub-project has its own README with full build + usage instructions.

---

## Quick start

### 1. Start the server

**Docker (recommended for production):**
```bash
docker compose up --build
```

**Native build:**
```bash
cd server
vcpkg install ixwebsocket
cmake -B build -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
      -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
./build/server
```

Expected output:
```
Holovisualize server listening on :8080
  WebSocket: ws://0.0.0.0:8080/ws?session=KEY&role=producer&sensor=ID
  WebSocket: ws://0.0.0.0:8080/ws?session=KEY&role=consumer
```

---

### 2. Build the capture client

```bat
cd capture
C:\vcpkg\vcpkg install libfreenect2 ixwebsocket opencv4[contrib]
cmake -B build -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake
cmake --build build --config Release
```

---

### 3. (Optional) Calibrate — 2+ sensors only

Print the ArUco marker (ID 0, 5 cm side):

```python
import cv2
marker = cv2.aruco.generateImageMarker(
    cv2.aruco.getPredefinedDictionary(cv2.aruco.DICT_4X4_250), 0, 500)
cv2.imwrite("marker_id0.png", marker)
```

Place the marker where all sensors can see it simultaneously. Then on each
sensor machine:

```bat
capture.exe SERVER:8080 demo sensor0 --calibrate
```

Expected:
```
[Calibration] Done. Translation: (0.12, -0.85, 1.43) m
[Calibration] Transform sent to server.
```

---

### 4. Stream

```bat
capture.exe SERVER:8080 demo sensor0
```

The server merges all sensor streams, runs Marching Cubes, and broadcasts the
mesh at ~30 fps.

---

### 5. Connect a viewer

There are three ways to view a session — same MESH binary frame either way,
different transport:

**Dashboard (built into the server, easiest):** open
`http://SERVER:8082/`, pick the session from the "Live view" dropdown.
WebGL point-cloud viewer with orbit camera, WASD fly movement, and
point-size/bound-box controls — no separate build needed, it's served
directly by `server.exe`.

**Native preview (`preview.exe`, KCP/UDP — for diagnostics):**
```bash
cd preview
vcpkg install ixwebsocket glfw3
cmake -B build ...
./build/preview SERVER 8081 demo
```

**Phone AR (`viewer/`, WebSocket):** see `viewer/README.md` — camera feed +
device-orientation "AR" (no WebXR, works on iOS Safari too), connects with
`?host=SERVER:8080&session=demo`.

**Unity / custom clients:** connect over WebSocket with
`role=viewer`: `ws://SERVER:8080/ws?session=demo&role=viewer`, decode the
MESH binary frame to build a `Mesh` in real-time. (KCP/UDP is also available
for lower-overhead native clients — see `server/README.md` for both wire
protocols.)

---

## Multiple sensors

```bat
# Machine A — front sensor
capture.exe SERVER:8080 demo sensorFront

# Machine B — back sensor
capture.exe SERVER:8080 demo sensorBack
```

Run `--calibrate` on each before streaming (Step 3 above).

---

## Wire protocols

### HOLO (capture → server)

```
Offset      Size  Type    Description
----------  ----  ------  ---------------
0           4     u8[4]   Magic "HOLO"
4           4     u32 LE  N = point count
8 + i*16    4     f32 LE  Point[i].X  (metres)
12 + i*16   4     f32 LE  Point[i].Y
16 + i*16   4     f32 LE  Point[i].Z
20 + i*16   1     u8      Point[i].R
21 + i*16   1     u8      Point[i].G
22 + i*16   1     u8      Point[i].B
23 + i*16   1     u8      (padding)
```

### MESH (server → viewers)

```
Offset            Size       Type    Description
----------------  ---------  ------  ------------------
0                 4          u8[4]   Magic "MESH"
4                 4          u32 LE  nVerts
8                 4          u32 LE  nTris
12 + v*24         4×6        f32 LE  Vertex[v]: x,y,z,nx,ny,nz
12+nVerts*24+t*12 4×3        u32 LE  Triangle[t]: i0,i1,i2
```

Marching Cubes reconstruction is currently disabled by default (density
normalisation collapses under near-field outlier splats — see
`server/README.md`); `nTris` is `0` and points stream through as loose
vertices instead. In that mode `nx,ny,nz` carry RGB (0..1 each) instead of a
real normal, since raw points don't have one and the wire format didn't need
to change to add colour — see `SessionModelView::buildFrame()`.
