#pragma once

#include "editing/EditingProtocol.h"

#include <array>
#include <string>
#include <vector>

namespace eve::editing {

/** @brief Renderer-neutral wire primitive for authoring viewport overlays. */
struct GizmoPrimitive {
    std::string id;
    std::string kind;
    std::array<double, 3> position{0.0, 0.0, 0.0};
    std::array<double, 3> size{0.0, 0.0, 0.0};
    std::array<double, 3> direction{0.0, 0.0, -1.0};
    std::array<double, 4> color{1.0, 1.0, 1.0, 1.0};
    double radius = 0.0;
    double length = 0.0;
    bool dashed = false;
};

/** @brief Immutable revision-tagged overlay snapshot consumed by any viewport renderer. */
struct GizmoSnapshot {
    Status status = Status::Failed;
    std::string target;
    Revision targetRevision = 0;
    std::vector<GizmoPrimitive> primitives;
    std::vector<Diagnostic> diagnostics;
};

}  // namespace eve::editing
