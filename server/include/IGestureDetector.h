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
// Minimal example — detect when any vertex is above 1.8 m (raised hand):
//
//   class RaisedHandDetector : public IGestureDetector {
//   public:
//       std::vector<GestureEvent> detect(const Mesh& mesh,
//                                        uint64_t nowMs) override {
//           for (auto& v : mesh.vertices)
//               if (v.y > 1.8f)
//                   return {{ GestureType::OpenHand, {v.x,v.y,v.z}, 0.9f, nowMs }};
//           return {};
//       }
//       const char* name() const override { return "Raised Hand"; }
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
