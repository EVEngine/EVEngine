#pragma once

#include "editing/EditingGizmo.h"
#include "procgen/mesh/MeshDeformationSession.h"

#include <string>

namespace eve::procgen_editing {

/** @brief Renderer-neutral runtime vertex-selection axis gizmo adapter. */
class MeshVertexAxisGizmoBuilder {
public:
    /** @brief Build three pickable axis arrows at the current selection center. */
    [[nodiscard]] editing::GizmoSnapshot build(const procgen::MeshDeformationSession& session,
                                               double                                 axisLength = 1.0) const;
    /** @brief Pick x, y, or z arrow using a normalized or non-normalized world ray. */
    [[nodiscard]] Result<std::string> pickAxisResult(const editing::GizmoSnapshot& snapshot, double originX,
                                                     double originY, double originZ, double directionX,
                                                     double directionY, double directionZ) const;
};

}  // namespace eve::procgen_editing
