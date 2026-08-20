"""Gesture sidecar — MediaPipe GestureRecognizer driven over stdin/stdout.

capture.exe spawns this as a child process (see capture/include/GestureClient.h)
and talks to it over its stdin/stdout pipes rather than a network socket.
That switch happened after a Windows machine reliably had every loopback
TCP connection between capture.exe and an earlier socket-based version of
this script aborted (WinError 10053) by something never conclusively
identified — ruled out: Defender real-time protection, ASR rules, Smart App
Control, endianness, and every connection-lifecycle variant tried. A
minimal from-scratch C++ Winsock client reproduced the abort against this
same script while an equivalent Python client never once failed, which is
what pointed at "native exe <-> loopback socket" specifically rather than
anything about this protocol or MediaPipe itself. Anonymous pipes don't
touch the network stack, sidestepping whatever that was.

Uses MediaPipe's Tasks API (mediapipe>=0.10 replaced the old
`mp.solutions.hands` Solutions API entirely — it no longer exists).
GestureRecognizer ships a pretrained classifier for 6 canned gestures
("Closed_Fist", "Open_Palm", "Pointing_Up", "Thumb_Up", "Thumb_Down",
"Victory", "ILoveYou") — no hand-rolled finger-angle heuristic needed.
The model bundle (~8 MB) is downloaded once to gesture_recognizer.task on
first run.

Protocol (length-prefixed, little-endian u32 length prefixes) — identical
framing to the old socket version, just carried over stdin/stdout now:
  Request  (capture -> sidecar, on stdin):  raw RGB bytes, width*height*3,
           row-major. Width/height aren't sent — this sidecar is only ever
           used with the Kinect's registered depth-space frame (512x424),
           hardcoded below to keep the protocol trivial.
  Response (sidecar -> capture, on stdout): JSON, e.g.
           {"hands": [{"gesture": "Fist", "confidence": 0.87,
                       "wrist_x": 0.51, "wrist_y": 0.44}]}
           or {"hands": []} if no hand/no recognised gesture. Only one hand
           is reported — matches the single external gesture position the
           server's effect pipeline expects.

IMPORTANT: stdout carries the binary protocol — never print() to it. All
logging goes to stderr (capture.exe keeps the child's stderr attached to
its own console, so these are still visible during development).
"""

import json
import os
import struct
import sys
import time
import urllib.request

if sys.platform == "win32":
    import msvcrt
    # Windows opens stdin/stdout in text mode by default, which translates
    # \n<->\r\n and treats 0x1A as EOF — both would silently corrupt the
    # binary framing. Force raw binary mode before anything touches them.
    msvcrt.setmode(sys.stdin.fileno(), os.O_BINARY)
    msvcrt.setmode(sys.stdout.fileno(), os.O_BINARY)

import mediapipe as mp
import numpy as np
from mediapipe.tasks.python import vision
from mediapipe.tasks.python.core.base_options import BaseOptions

FRAME_W = 512
FRAME_H = 424
FRAME_BYTES = FRAME_W * FRAME_H * 3

MODEL_PATH = os.path.join(os.path.dirname(__file__), "gesture_recognizer.task")
MODEL_URL = ("https://storage.googleapis.com/mediapipe-models/gesture_recognizer/"
             "gesture_recognizer/float16/latest/gesture_recognizer.task")

# MediaPipe's canned gesture names -> this project's HandGesture wire vocabulary
# (see capture/include/GestureWire.h). "ILoveYou" (pinky+index+thumb extended)
# doesn't have a natural existing slot, so it rides on the wire as "Pinch" —
# the closest unused GestureType — see GestureWire.h's comment.
GESTURE_MAP = {
    "Closed_Fist": "Fist",
    "Open_Palm":   "OpenHand",
    "Pointing_Up": "PointFinger",
    "Thumb_Up":    "ThumbsUp",
    "Victory":     "Peace",
    "ILoveYou":    "Pinch",
}


def log(msg):
    print(msg, file=sys.stderr, flush=True)


def ensure_model():
    if os.path.exists(MODEL_PATH):
        return
    log(f"[sidecar] downloading gesture model to {MODEL_PATH} ...")
    urllib.request.urlretrieve(MODEL_URL, MODEL_PATH)
    log("[sidecar] model downloaded.")


def read_exact(stream, n):
    buf = bytearray()
    while len(buf) < n:
        chunk = stream.read(n - len(buf))
        if not chunk:
            return None  # parent closed stdin — normal shutdown
        buf.extend(chunk)
    return bytes(buf)


def main():
    ensure_model()
    options = vision.GestureRecognizerOptions(
        base_options=BaseOptions(model_asset_path=MODEL_PATH),
        running_mode=vision.RunningMode.IMAGE,
        num_hands=1,
        min_hand_detection_confidence=0.6,
        min_tracking_confidence=0.5,
    )

    stdin = sys.stdin.buffer
    stdout = sys.stdout.buffer

    with vision.GestureRecognizer.create_from_options(options) as recognizer:
        log(f"[sidecar] ready (frame size {FRAME_W}x{FRAME_H})")

        while True:
            len_prefix = read_exact(stdin, 4)
            if len_prefix is None:
                log("[sidecar] stdin closed — exiting")
                return
            (payload_len,) = struct.unpack("<I", len_prefix)
            if payload_len != FRAME_BYTES:
                log(f"[sidecar] protocol mismatch: got len {payload_len}, expected {FRAME_BYTES}")
                return

            raw = read_exact(stdin, payload_len)
            if raw is None:
                return

            # mp.Image infers width/height from a numpy array's shape (h, w, 3).
            frame = np.frombuffer(raw, dtype=np.uint8).reshape(FRAME_H, FRAME_W, 3)
            mp_image = mp.Image(image_format=mp.ImageFormat.SRGB, data=frame)

            t0 = time.time()
            result = recognizer.recognize(mp_image)
            log(f"[sidecar] recognize() took {time.time() - t0:.2f}s")

            response = {"hands": []}
            if result.gestures and result.hand_landmarks:
                top = result.gestures[0][0]  # highest-confidence category, first hand
                wire_name = GESTURE_MAP.get(top.category_name)
                if wire_name is not None:
                    wrist = result.hand_landmarks[0][0]  # landmark 0 = wrist
                    response["hands"].append({
                        "gesture": wire_name,
                        "confidence": top.score,
                        "wrist_x": wrist.x,
                        "wrist_y": wrist.y,
                    })

            body = json.dumps(response).encode("utf-8")
            stdout.write(struct.pack("<I", len(body)) + body)
            stdout.flush()


if __name__ == "__main__":
    main()
