#pragma once

#include <cstdint>
#include <vector>
#include <cstring>

// MESH binary frame — produced by the server, consumed by viewers / preview.
//
// Wire layout (little-endian):
//   [4]          magic   = "MESH"
//   [4]          nVerts  (u32)
//   [4]          nTris   (u32)
//   [nVerts*24]  vertices: x,y,z (f32) + nx,ny,nz (f32)
//   [nTris*12]   triangles: i0,i1,i2 (u32)

struct Vertex {
    float x, y, z;
    float nx, ny, nz;
};

struct Mesh {
    std::vector<Vertex>   vertices;
    std::vector<uint32_t> indices;   // triplets
};

inline std::vector<uint8_t> encodeMesh(const Mesh& mesh) {
    const uint32_t nv = static_cast<uint32_t>(mesh.vertices.size());
    const uint32_t ni = static_cast<uint32_t>(mesh.indices.size());
    const uint32_t nTris = ni / 3;

    // Sized once up front — repeatedly resize()-ing by 4 bytes at a time
    // (the previous approach) reallocates on every single field, which is
    // fine for a few-thousand-vertex marching-cubes mesh but catastrophic
    // for a raw ~90k-point cloud frame (500k+ reallocations/frame).
    std::vector<uint8_t> buf(12 + static_cast<size_t>(nv) * 24 + static_cast<size_t>(nTris) * 12);
    uint8_t* p = buf.data();

    memcpy(p, "MESH", 4); p += 4;
    memcpy(p, &nv,    4); p += 4;
    memcpy(p, &nTris, 4); p += 4;

    for (const auto& v : mesh.vertices) {
        memcpy(p, &v.x,  4); p += 4;
        memcpy(p, &v.y,  4); p += 4;
        memcpy(p, &v.z,  4); p += 4;
        memcpy(p, &v.nx, 4); p += 4;
        memcpy(p, &v.ny, 4); p += 4;
        memcpy(p, &v.nz, 4); p += 4;
    }
    for (uint32_t idx : mesh.indices) {
        memcpy(p, &idx, 4); p += 4;
    }
    return buf;
}

// Decode a MESH frame (used by preview / Unity client).
// Returns false if malformed.
inline bool decodeMesh(const uint8_t* data, size_t len, Mesh& out) {
    if (len < 12) return false;
    if (data[0]!='M'||data[1]!='E'||data[2]!='S'||data[3]!='H') return false;

    uint32_t nv, nTris;
    memcpy(&nv,    data + 4, 4);
    memcpy(&nTris, data + 8, 4);

    size_t expected = 12 + (size_t)nv * 24 + (size_t)nTris * 12;
    if (len < expected) return false;

    out.vertices.resize(nv);
    const uint8_t* p = data + 12;
    for (uint32_t i = 0; i < nv; ++i, p += 24) {
        memcpy(&out.vertices[i].x,  p,      4);
        memcpy(&out.vertices[i].y,  p + 4,  4);
        memcpy(&out.vertices[i].z,  p + 8,  4);
        memcpy(&out.vertices[i].nx, p + 12, 4);
        memcpy(&out.vertices[i].ny, p + 16, 4);
        memcpy(&out.vertices[i].nz, p + 20, 4);
    }
    out.indices.resize((size_t)nTris * 3);
    for (uint32_t i = 0; i < nTris * 3; ++i, p += 4) {
        memcpy(&out.indices[i], p, 4);
    }
    return true;
}
