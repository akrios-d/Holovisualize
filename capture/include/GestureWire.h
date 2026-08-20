#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

// GEVT binary frame — sent by the capture client alongside HOLO point-cloud
// frames when a hand gesture is recognised. Consumed by the server, which
// transforms the camera-space position to world space (using the sensor's
// calibration transform) and feeds it into the same GestureEvent/effect
// pipeline that server-side mesh detectors use (see IGestureDetector.h).
//
// capture/ doesn't share headers with server/ (independent build targets,
// see server/include/Frame.h vs capture/include/PointCloud.h for the same
// pattern with HOLO), so HandGesture's numeric values are duplicated here —
// they MUST match GestureType in server/include/GestureEvent.h.
//
// Wire layout (little-endian):
//   [4]  magic      = "GEVT"
//   [1]  gestureType (u8, HandGesture)
//   [3]  padding
//   [4]  x (f32, camera-space metres)
//   [4]  y
//   [4]  z
//   [4]  confidence (f32, 0..1)
// The sidecar (capture/gesture_sidecar) uses MediaPipe's pretrained
// GestureRecognizer, which classifies 6 canned poses. Its "ILoveYou" sign
// (pinky+index+thumb extended) doesn't have a natural existing slot here,
// so it rides on the wire as Pinch — the closest unused value — see
// sidecar.py's GESTURE_MAP.
enum class HandGesture : uint8_t {
    None        = 0,
    Fist        = 3,   // MediaPipe: Closed_Fist
    OpenHand    = 4,   // MediaPipe: Open_Palm
    Pinch       = 5,   // MediaPipe: ILoveYou
    ThumbsUp    = 6,   // MediaPipe: Thumb_Up
    PointFinger = 7,   // MediaPipe: Pointing_Up
    Peace       = 8,   // MediaPipe: Victory
};

inline std::vector<uint8_t> encodeGestureEvent(HandGesture type, float x, float y, float z,
                                                float confidence) {
    std::vector<uint8_t> buf(24, 0);
    buf[0] = 'G'; buf[1] = 'E'; buf[2] = 'V'; buf[3] = 'T';
    buf[4] = static_cast<uint8_t>(type);
    std::memcpy(buf.data() + 8,  &x,          4);
    std::memcpy(buf.data() + 12, &y,          4);
    std::memcpy(buf.data() + 16, &z,          4);
    std::memcpy(buf.data() + 20, &confidence, 4);
    return buf;
}
