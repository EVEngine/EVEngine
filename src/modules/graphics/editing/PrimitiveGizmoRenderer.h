#pragma once

#include "common/Result.h"
#include "editing/EditingGizmo.h"
#include "graphics/PrimitiveTypes.h"

namespace eve::graphics {
class IGraphics3D;
}

namespace eve::graphics_editing {

/** @brief Converts renderer-neutral editor gizmos into one atomic primitive draw submission. */
class PrimitiveGizmoRenderer {
public:
    /**
     * @brief Validate, record and submit one immutable gizmo snapshot.
     * @param snapshot Borrowed editor snapshot retained only for this call.
     * @param context Current camera value snapshot; no Camera or Scene pointer is retained.
     * @param graphics Borrowed active 3D pass backend.
     * @return Primitive workload statistics, or a diagnostic before backend submission.
     * @thread Render-thread affine.
     * @reentrancy Invokes no script or caller callback.
     */
    [[nodiscard]] eve::Result<graphics::PrimitiveDrawStatistics> render(const editing::GizmoSnapshot&     snapshot,
                                                                        const graphics::SceneDrawContext& context,
                                                                        graphics::IGraphics3D& graphics) const;
};

}  // namespace eve::graphics_editing
