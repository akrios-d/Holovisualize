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

    std::vector<uint8_t> buf;
    buf.reserve(12 + nv * 24 + nTris * 12);

    // magic
    buf.push_back('M'); buf.push_back('E'); buf.push_back('S'); buf.push_back('H');

    // counts
    auto writeU32 = [&](uint32_t v) {
        buf.resize(buf.size() + 4);
        memcpy(buf.data() + buf.size() - 4, &v, 4);
    };
    auto writeF32 = [&](float v) {
        buf.resize(buf.size() + 4);
        memcpy(buf.data() + buf.size() - 4, &v, 4);
    };

    writeU32(nv);
    writeU32(nTris);

    for (const auto& v : mesh.vertices) {
        writeF32(v.x);  writeF32(v.y);  writeF32(v.z);
        writeF32(v.nx); writeF32(v.ny); writeF32(v.nz);
    }
    for (uint32_t idx : mesh.indices) {
        writeU32(idx);
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
