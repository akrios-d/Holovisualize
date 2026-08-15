# preview

Local desktop preview window — like the original LiveScan3D server preview.
Connects to the server as a consumer and renders the live mesh in real-time
using OpenGL 3.3 + GLFW.

This is optional: it's a diagnostic / debug tool to verify the server is
producing a correct mesh. Unity or the Three.js viewer are the production
viewers.

## Controls

| Input | Action |
|---|---|
| Left-drag | Orbit camera |
| Scroll | Zoom in/out |
| R | Reset camera |
| Q / Escape | Quit |

## Prerequisites

| Tool | Notes |
|---|---|
| CMake ≥ 3.16 | https://cmake.org |
| vcpkg | https://github.com/microsoft/vcpkg |
| IXWebSocket | WebSocket client |
| GLFW3 | Window + input |
| OpenGL | (system — already present on Windows/macOS/Linux) |

## Build

```bash
cd preview

# 1. Install dependencies
vcpkg install ixwebsocket glfw3

# 2. Configure + build
cmake -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release

# 3. Run — connect to a local server, session "demo"
./build/preview

# Custom URL
./build/preview "ws://192.168.1.10:8080/ws?session=demo&role=consumer"
```

## Title bar

The window title shows live stats:
```
Holovisualize Preview | connected | 28 fps | 14520 tris
```

## Notes

- The preview is **not** included in the Docker image — it requires a display
  and is meant for local development only.
- Normals are flat-shaded (per-triangle). Smooth shading would require vertex
  welding in Marching Cubes; the trade-off was skipped for real-time
  performance.
