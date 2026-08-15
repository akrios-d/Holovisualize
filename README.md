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
├── capture/     C++  — sensor capture, pipeline, WebSocket sender
├── server/      C++  — relay server, Marching Cubes, mesh broadcast
├── preview/     C++  — local OpenGL preview window (like LiveScan3D)
└── viewer/      JS   — Three.js web viewer (in progress)
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

**Local preview (optional — for diagnostics):**
```bash
cd preview
vcpkg install ixwebsocket glfw3
cmake -B build ...
./build/preview "ws://SERVER:8080/ws?session=demo&role=consumer"
```

**Web viewer:**
```
ws://SERVER:8080/ws?session=demo&role=consumer
```
Receives MESH binary frames — see `server/README.md` for the wire format.

**Unity:** Connect a WebSocket client to the same URL and decode the MESH frame
to build a `Mesh` component in real-time.

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
