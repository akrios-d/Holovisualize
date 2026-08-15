#pragma once

#include "GestureEvent.h"

#include <cstdint>
#include <vector>

// Forward declaration
struct Mesh;

// Socket Pattern — gesture detector port.
//
// Implement this interface to add a new gesture recogniser.
// The server calls detect() once per merged mesh frame and collects all events.
//
// Minimal example — detect ArmsRaised when the mesh centroid Y exceeds 1.6 m:
//
//   class ArmsRaisedDetector : public IGestureDetector {
//   public:
//       std::vector<GestureEvent> detect(const Mesh& mesh,
//                                        uint64_t nowMs) override {
//           if (mesh.vertices.empty()) return {};
//           float sumY = 0;
//           for (auto& v : mesh.vertices) sumY += v.y;
//           float cy = sumY / mesh.vertices.size();
//           if (cy > 1.6f)
//               return {{ GestureType::ArmsRaised, {0,cy,0}, {}, 0.85f, nowMs }};
//           return {};
//       }
//       const char* name() const override { return "Arms Raised"; }
//   };
class IGestureDetector {
public:
    virtual ~IGestureDetector() = default;

    // Analyse `mesh` and return zero or more gesture events.
    // `nowMs` is the current server time in milliseconds.
    virtual std::vector<GestureEvent> detect(const Mesh& mesh,
                                             uint64_t nowMs) = 0;

    virtual const char* name() const = 0;

    bool enabled = true;
};
