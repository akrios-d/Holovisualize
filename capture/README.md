# capture

C++ capture client. Reads depth + colour from a sensor (currently Kinect v2 via
libfreenect2), processes the data, and streams a point cloud to the server over
WebSocket.

## Architecture

```
KinectV2Sensor  (ISensor implementation)
    │ libfreenect2 — depth + colour + registration
    ▼
Pipeline
    ├── filterFlyingPixels()   — remove depth-edge artefacts
    ├── segmentByDepth()       — keep [200 mm, 2500 mm]
    └── generatePointCloud()   — project pixels to 3-D
         │
         ▼
Sender (IXWebSocket client)
    │ serialise → HOLO binary frame
    ▼
  ws://server:8080/ws?session=KEY&role=producer&sensor=ID
```

## Prerequisites

| Tool | Notes |
|---|---|
| CMake ≥ 3.16 | https://cmake.org |
| vcpkg | https://github.com/microsoft/vcpkg |
| libfreenect2 | cross-platform Kinect v2 driver |
| IXWebSocket | WebSocket client |
| OpenCV 4 + contrib | ArUco marker detection for calibration |

**Kinect v2 on Windows** — libfreenect2 needs the WinUSB driver instead of the
official Kinect SDK driver. Install [Zadig](https://zadig.akeo.ie/), select
**KinectSensor**, choose **WinUSB**, click Install Driver.

> ⚠️ This makes the official Kinect SDK apps stop working. Reinstall from Device
> Manager to switch back.

## Build

```bat
cd capture

# 1. Install dependencies
C:\vcpkg\vcpkg install libfreenect2 ixwebsocket opencv4[contrib]

# 2. Configure + build
cmake -B build -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake
cmake --build build --config Release
```

Binary: `build\Release\capture.exe`

## Usage

### Stream mode

```bat
capture.exe [host:port] [session] [sensor_id] [--preview] [--calibrate]
```

| Argument | Default | Description |
|---|---|---|
| `host:port` | `localhost:8080` | Server address |
| `session` | `demo` | Session key — same on all clients |
| `sensor_id` | `sensor0` | Unique ID for this sensor |

```bat
# Local
capture.exe localhost:8080 demo sensor0

# Remote server
capture.exe 192.168.1.10:8080 demo sensor0
```

### Scene filters

Pass one filter flag to control what gets streamed:

| Flag | Description |
|---|---|
| _(none)_ | Stream everything the sensor captures |
| `--filter=body` | Keep only points inside a human-body bounding box (±0.7 m lateral, 0–2.1 m height) |
| `--filter=background` | Capture 30 background frames on startup, then stream only foreground (moving objects / people) |

```bat
# Body only
capture.exe localhost:8080 demo sensor0 --filter=body

# Background subtraction
capture.exe localhost:8080 demo sensor0 --filter=background

# Both — body bounds applied after background subtraction
capture.exe localhost:8080 demo sensor0 --filter=background --filter=body
```

> `--filter=background` requires ~1 second of empty-scene frames on startup.
> Keep the area clear until "Background model ready." appears in the console.

### Preview mode

Add `--preview` to open two live OpenCV windows alongside streaming:

- **Color (aligned)** — colour frame registered to depth space (512×424)
- **Depth** — false-colour depth heatmap (blue = near, red = far)

Press **Q** or **ESC** in either window to stop.

```bat
capture.exe localhost:8080 demo sensor0 --preview
```

> The preview adds roughly 1–2 ms per frame (OpenCV `imshow` is non-blocking).
> It is purely local — nothing extra is sent to the server.

### Calibration mode

Run this before streaming when using 2+ sensors. Point the sensor at the
printed ArUco marker (ID 0, 5 cm side) and the transform is automatically sent
to the server.

```bat
capture.exe 192.168.1.10:8080 demo sensor0 --calibrate
```

The calibration transform is sent as a WebSocket text message (JSON) to the
server on the same URL:
```json
{"type":"calibrate","transform":[m00,m01,...,m33]}
```

Single-sensor setups: skip calibration — the server defaults to the identity
transform.

## Pipeline settings

Edit these in `src/main.cpp` before the capture loop:

```cpp
pipeline.filterEnabled      = true;   // flying-pixels filter
pipeline.filterThresholdMm  = 20.0f;  // depth discontinuity threshold
pipeline.segmentMinDepthMm  = 200.0f;
pipeline.segmentMaxDepthMm  = 2500.0f;
```

## Wire format (HOLO)

```
Offset      Size  Type    Description
----------  ----  ------  ---------------
0           4     u8[4]   Magic: "HOLO"
4           4     u32 LE  N = point count
8 + i*16    4     f32 LE  Point[i].X (m)
12 + i*16   4     f32 LE  Point[i].Y (m)
16 + i*16   4     f32 LE  Point[i].Z (m)
20 + i*16   1     u8      Point[i].R
21 + i*16   1     u8      Point[i].G
22 + i*16   1     u8      Point[i].B
23 + i*16   1     u8      (padding)
```

## Possible improvements

### Drop opencv4 from the build (faster CI)

`opencv4[contrib]` is only used by `BackgroundSubtractorFilter` (MOG2) and the
ArUco calibration path. It is by far the heaviest dependency (~40 min to compile
from source in CI). Two independent improvements:

- **Background subtraction without OpenCV** — replace MOG2 with a simple
  rolling-average depth-map model (maintain a per-pixel mean + threshold). No
  accuracy loss for the Kinect use-case; saves the entire OpenCV compile.
- **ArUco without `contrib`** — OpenCV 4.8+ ships ArUco in the main `calib3d`
  module. Switch to `opencv4` (no `[contrib]` feature) and update the
  `#include` paths; calibration continues to work.

Either change cuts the `capture` CI job from ~60 min to ~10 min.

## Adding a new sensor

1. Create a class in `include/sensors/` and `src/sensors/` that implements `ISensor`.
2. Instantiate it in `main.cpp` instead of `KinectV2Sensor`.

The rest of the pipeline is sensor-agnostic.
