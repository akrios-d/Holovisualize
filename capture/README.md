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
| libfreenect2 | cross-platform Kinect v2 driver — see note below |
| IXWebSocket | WebSocket client |
| OpenCV 4 + contrib | ArUco marker detection for calibration |

**Kinect v2 on Windows** — libfreenect2 needs the WinUSB driver instead of the
official Kinect SDK driver. Install [Zadig](https://zadig.akeo.ie/), select
**KinectSensor**, choose **WinUSB**, click Install Driver.

> ⚠️ **No joint/skeletal tracking.** This is the trade-off for dropping the
> official Kinect SDK (see git history, "drop Kinect SDK/v1") in favour of
> libfreenect2: raw depth/IR/colour streams and depth↔colour registration
> only, none of Microsoft's proprietary body-tracking algorithm. Anything
> that wants gesture/pose recognition (see "Gesture / effects system" in
> `server/README.md`) has to work from the point cloud's raw geometry —
> there's no hand/finger/joint data to build on.

> ⚠️ This makes the official Kinect SDK apps stop working. Reinstall from Device
> Manager to switch back.

> ⚠️ **libfreenect2 dependency is a local machine path, not vcpkg — and that
> local checkout is a fork.**
> The libfreenect2 fetched by vcpkg's port (v0.2.1) detects GLFW3 via
> pkg-config, which silently fails on Windows, so it always builds without
> OpenGL support (CPU-only depth decoding, ~10-12 fps instead of ~60-100).
> `capture/CMakeLists.txt` points at a locally built, OpenGL-enabled
> libfreenect2 checkout at `C:/libfreenect2` (GLFW3 found via `find_package`
> instead of pkg-config) instead of going through vcpkg.
>
> That checkout is
> [akrios-d/libfreenect2](https://github.com/akrios-d/libfreenect2), a fork
> of upstream `OpenKinect/libfreenect2`, because it also adds
> `GLJpegRgbPacketProcessor` — a custom RGB JPEG decoder that does entropy
> (Huffman) decode on the CPU via libjpeg-turbo's raw-coefficient API and
> IDCT + chroma upsampling + colour conversion on the GPU via OpenGL
> fragment shaders, instead of the stock CPU-only `TurboJpegRgbPacketProcessor`
> every Windows pipeline variant otherwise falls back to (see "GPU-accelerated
> RGB (JPEG) decode" below). Not yet upstreamed as a PR.
>
> CI (`.github/workflows/ci.yml`) doesn't have `C:/libfreenect2` on the
> runner, so `capture/CMakeLists.txt` falls back to vcpkg's libfreenect2
> (`CpuPacketPipeline`, no GPU decode) when the local path isn't present —
> the CI build still passes, just without the fork's speedup.

## Build

```bat
cd capture

# 1. Install dependencies (libfreenect2 excluded — see note above)
C:\vcpkg\vcpkg install ixwebsocket opencv4[contrib]

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

> Running `capture.exe` with no `--headless`/`--calibrate` flag opens an
> ImGui GUI instead — host/session/sensor fields, a depth-range slider
> (min/max mm, live-adjustable while streaming), a background-subtraction
> checkbox, a 4-panel preview (Color/Depth/IR/Registered), and Connect &
> Stream / Preview Only / Disconnect / Test Server buttons. That's the
> normal way to run it day-to-day; the CLI flags below are for headless/
> scripted use.
>
> The GUI follows the Socket Pattern (see `server/include/ISession.h` for
> the original C++ adaptation this mirrors): `ICaptureApp.h` declares
> `ICaptureModel`/`ICaptureView`/`ICaptureController`, `CaptureModelView`
> owns the pipeline/sensor/network side, `CaptureViewController` owns the
> GLFW window/ImGui/preview textures side. Neither knows the other's
> concrete type — only the three interfaces.

### Scene filters

| Flag | Description |
|---|---|
| _(none)_ | Stream everything the sensor captures |
| `--filter=background` | Capture 30 background frames on startup, then stream only foreground (moving objects / people) |

```bat
# Background subtraction, headless
capture.exe localhost:8080 demo sensor0 --headless --filter=background
```

> `--filter=background` requires ~1 second of empty-scene frames on startup.
> Keep the area clear until "Background model ready." appears in the console.
>
> There used to be a `--filter=body` (fixed spatial bounding-box crop) too —
> removed. A fixed box in one sensor's local, uncalibrated space is a poor
> proxy for "just the subject"; that kind of spatial crop belongs after
> multi-sensor fusion instead. It's now a visual-only bound-box clip on the
> viewer side (dashboard's Live view / the AR `viewer/` app), driven by
> server-side settings under `/api/config` — see `server/README.md`.

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

`Pipeline::segmentMinDepthMm`/`segmentMaxDepthMm` (default 200–2500mm) are
the GUI's "Depth Range" sliders — live-adjustable while streaming, no
rebuild needed (the slider's `onDepthRangeChanged()` calls straight into
`CaptureModelView::setDepthRange()`, which writes into the pipeline
directly). `filterEnabled`/`filterThresholdMm` (flying-pixels filter)
aren't exposed in the UI yet — still edit those in `src/main.cpp`/
`CaptureModelView.cpp` before building:

```cpp
pipeline.filterEnabled      = true;   // flying-pixels filter
pipeline.filterThresholdMm  = 20.0f;  // depth discontinuity threshold
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

### GPU-accelerated RGB (JPEG) decode

`RgbPacketProcessor` selection in libfreenect2 is independent of the depth
pipeline choice — every pipeline variant (CPU, OpenGL, OpenCL, CUDA) still
decodes colour via `getDefaultRgbPacketProcessor()`, which on Windows is
always `TurboJpegRgbPacketProcessor` (CPU, libjpeg-turbo). There's no
GPU-decode option built in for Windows (`VaapiRgbPacketProcessor` is
Linux/VAAPI-only, `TegraJpegRgbPacketProcessor` is Jetson-only).

Measured in this session: `TurboJpegRgbPacketProcessor`'s logged `avg. time`
times only `tjDecompress2()` itself (wall-clock, packet already fully
received) — so slow readings (100-800ms vs. a normal ~15-20ms) aren't USB
data loss, they're this thread getting starved while the GPU-decoded depth
pipeline and the rest of the app compete for CPU/GPU time in the same
process. Confirms as CPU contention: RGB-only capture (`Protonect.exe gl
-nodepth`) holds a steady ~60 fps with nothing else running.

**nvJPEG (NVIDIA CUDA hardware JPEG decode) — tried, abandoned.** A custom
`NvJpegRgbPacketProcessor` was implemented and wired into
`packet_pipeline.cpp`, but every nvJPEG decode API (one-shot, pipelined, and
combined) fails on this machine's RTX 50-series + CUDA 13.3 combination —
confirmed environmental (not Kinect-specific) via a standalone non-Kinect
JPEG repro. The code is still in the fork
([akrios-d/libfreenect2](https://github.com/akrios-d/libfreenect2)) behind
`ENABLE_NVJPEG` (default `OFF`) in case a future driver/CUDA update fixes it,
but it's not the active path.

**What's actually running: `GLJpegRgbPacketProcessor`.** Entropy (Huffman)
decode is inherently serial, so it stays on the CPU via libjpeg-turbo's raw
coefficient API (`jpeg_read_coefficients()` — distinct from the simplified
`tjDecompress2()` TurboJPEG wrapper); it exposes the raw quantized DCT
coefficient blocks before IDCT. IDCT, chroma upsampling, and YCbCr→RGB
colour conversion — the parallelisable part — run on the GPU as OpenGL
fragment shader passes (`src/shader/jpeg_idct.fs`,
`src/shader/jpeg_color.fs`), the same GPGPU pattern libfreenect2's own depth
pipeline already uses (`sampler2DRect` textures, FBO + fullscreen-quad
passes). Enabled by default (`ENABLE_GLJPEG`), tried first in
`getDefaultRgbPacketProcessor()`, with `TurboJpegRgbPacketProcessor` as the
fallback if `good()` fails.

Caveat found in this session: decode throughput alone is fine (~100Hz
isolated), but running inside `capture.exe` with its own window open — even
with the preview panel hidden — showed real contention between this
decoder's independent GL context and the UI window's `glfwSwapBuffers`
vsync wait (fixed on the `capture` side by disabling vsync and capping the
UI redraw loop manually — see `capture/src/CaptureViewController.cpp`).
Running headless (`capture.exe --headless`, no window at all) doesn't hit
this at all.

## Adding a new sensor

1. Create a class in `include/sensors/` and `src/sensors/` that implements `ISensor`.
2. Instantiate it in `main.cpp` instead of `KinectV2Sensor`.

The rest of the pipeline is sensor-agnostic.
