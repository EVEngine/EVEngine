#pragma once
#include <cmath>
#include "common/Diagnostic.h"
#include "physics/bake/collapse/BonePresentation.h"

namespace eve::physics::collapse_detail {
constexpr std::uint32_t MaxSamples     = 2000000;
constexpr std::uint32_t MaxRecipeBytes = 65536;
inline Diagnostic       invalid(const char* message) {
    return Diagnostic::error(DiagnosticCode::InvalidArgument, message, "collapseAnimation");
}
inline bool valid(const BonePresentation& b) {
    const float values[] = {b.halfExtentX, b.halfExtentY, b.halfExtentZ, b.worldX, b.worldY,
                            b.worldZ,      b.rotX,        b.rotY,        b.rotZ,   b.rotW};
    for (float v : values)
        if (!std::isfinite(v)) return false;
    const float norm = b.rotX * b.rotX + b.rotY * b.rotY + b.rotZ * b.rotZ + b.rotW * b.rotW;
    return b.halfExtentX > 0 && b.halfExtentY > 0 && b.halfExtentZ > 0 && std::isfinite(norm) && norm > 0.5f &&
           norm < 1.5f && static_cast<unsigned>(b.state) <= 2 && b.clusterId >= 0 && b.fractureLevel >= 0;
}
}  // namespace eve::physics::collapse_detail
