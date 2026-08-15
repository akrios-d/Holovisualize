# server

C++ WebSocket server. Receives point clouds from one or more capture clients,
merges them, generates a closed mesh via Marching Cubes, and broadcasts the
mesh to all connected viewers at ~30 fps.

## Architecture

```
Capture clients (producers)
    │  ws://server:8080/ws?session=KEY&role=producer&sensor=ID
    │  binary HOLO frames
    ▼
Session::updateCloud()
    │  per-sensor latest cloud + calibration transform
    ▼  (30 fps timer)
VoxelGrid::fill()
    │  Gaussian splatting → [0,1] occupancy grid (128³)
    ▼
marchingCubes()
    │  isosurface extraction at level 0.5
    │  flat-shaded normals
    ▼
encodeMesh() → MESH binary frame
    ▼
Viewers / preview (consumers)
    ws://server:8080/ws?session=KEY&role=consumer
```

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

### Consumers (viewers / preview)

Connect to:
```
ws://host:8080/ws?session=KEY&role=consumer
```

Receives binary MESH frames:

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

## Tuning

| Parameter | Default | Description |
|---|---|---|
| Voxel resolution | 128³ | Controls mesh quality vs latency. 128 → ~5–10 ms Marching Cubes on a modern CPU. Change in `Session` constructor call in `main.cpp`. |
| ISO level | 0.5 | Isosurface threshold. Raise to shrink the mesh, lower to expand. Change in `Session::process()`. |
| Gaussian splat radius | 2 voxels | Controls surface smoothness vs accuracy. See `VoxelGrid::fill()`. |

## Replacing the Go server

The old Go server files (`go.mod`, `go.sum`, `main.go`, `internal/`, `hub/`)
are still present in this folder but unused. They can be deleted once the C++
server is confirmed working.
