# Gesture sidecar

Small local Python process that runs MediaPipe's
[GestureRecognizer](https://ai.google.dev/edge/mediapipe/solutions/vision/gesture_recognizer)
task, which classifies 6 pretrained hand poses. `capture.exe` **spawns this
as a child process** and talks to it over its stdin/stdout pipes — see
[`capture/include/GestureClient.h`](../include/GestureClient.h) for the
C++ side and `sidecar.py`'s docstring for the wire protocol.

This used to be a TCP socket on `127.0.0.1:9001`. Switched to pipes after a
Windows machine reliably had every such loopback connection's teardown
aborted (`WinError 10053`) by something never conclusively identified —
ruled out Defender real-time protection, ASR rules, Smart App Control,
endianness, and every connection-lifecycle variant tried; a minimal
from-scratch C++ Winsock client reproduced it against this same script
while an equivalent Python client never once failed. Anonymous pipes don't
touch the network stack, sidestepping whatever that was.

The model bundle (`gesture_recognizer.task`, ~8 MB) is downloaded
automatically to this folder on first run — not committed to git.

This is a separate process, not embedded C++, because MediaPipe's C++ API
requires a Bazel build — not worth pulling into this project's CMake/vcpkg
setup for one feature. The Python package is free and open source
(Apache 2.0), no license cost.

## Why RGB, not depth

Microsoft's own Kinect skeletal tracking (Kinect for Windows SDK) works
directly on the depth image, which is lighting-independent. MediaPipe's
public models are RGB-based, so this sidecar inherits the same
underexposed/dark-room fragility already documented in
[`capture/README.md`](../README.md) for the point cloud's color capture —
front lighting helps a lot. No mature depth-only pretrained hand-pose model
exists as a practical alternative.

## Setup

```bash
cd capture/gesture_sidecar
python -m venv .venv
.venv\Scripts\activate      # Windows
pip install -r requirements.txt
```

That's it — no need to run `sidecar.py` yourself. `capture.exe` looks for
the venv at `capture/gesture_sidecar/.venv/Scripts/python.exe` (relative to
its own exe path) and spawns `sidecar.py` automatically the first time it
needs a gesture, as long as the "Detect hand gestures" checkbox is on
(it's on by default). Its stderr (model-load logs, `recognize()` timings)
stays attached to capture.exe's own console. If the venv isn't set up,
gesture detection just silently does nothing — it's best-effort and never
blocks or breaks streaming.

## Gestures → effects

Recognised gestures are sent to the server as `GEVT` frames and trigger the
same effect pipeline as the (currently unimplemented) mesh-based
`IGestureDetector`s — mappings live in `server/src/main.cpp`'s
`hub.onSessionCreated(...)` block:

| MediaPipe gesture      | Wire type     | Effect                  |
|-------------------------|---------------|--------------------------|
| Closed_Fist              | Fist          | Ice spikes               |
| Open_Palm                | OpenHand      | Explosion                |
| ILoveYou (🤟)            | Pinch         | Heart (spawned object)   |
| Thumb_Up                 | ThumbsUp      | Star (spawned object)    |
| Pointing_Up               | PointFinger   | Sphere (spawned object)  |
| Victory (✌️)             | Peace         | Crystal (spawned object) |

`Thumb_Down` and `None` aren't mapped to anything.

Only one gesture is recognised per frame (single hand, highest-confidence
pose wins) and events are edge-triggered — holding a gesture fires the
effect once, not once per frame.
