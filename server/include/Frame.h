#pragma once

#include <cstdint>
#include <vector>
#include <string_view>

// HOLO binary frame — produced by the capture client, consumed by the server.
//
// Wire layout (little-endian):
//   [4]      magic   = "HOLO"
//   [4]      N       = number of points (u32)
//   [N * 16] points:
//              x  (f32, metres)
//              y  (f32, metres)
//              z  (f32, metres)
//              r  (u8)
//              g  (u8)
//              b  (u8)
//              _  (u8, padding)

struct Point {
    float   x, y, z;
    uint8_t r, g, b;
};

using PointCloud = std::vector<Point>;

// Returns true and fills `out` on success.
// Returns false if the buffer is malformed or too short.
inline bool decodeHolo(const uint8_t* data, size_t len, PointCloud& out) {
    if (len < 8) return false;
    if (data[0] != 'H' || data[1] != 'O' || data[2] != 'L' || data[3] != 'O') return false;

    uint32_t n;
    memcpy(&n, data + 4, 4);

    if (len < 8 + static_cast<size_t>(n) * 16) return false;

    out.resize(n);
    const uint8_t* p = data + 8;
    for (uint32_t i = 0; i < n; ++i, p += 16) {
        memcpy(&out[i].x, p,     4);
        memcpy(&out[i].y, p + 4, 4);
        memcpy(&out[i].z, p + 8, 4);
        out[i].r = p[12];
        out[i].g = p[13];
        out[i].b = p[14];
        // p[15] = padding
    }
    return true;
}
