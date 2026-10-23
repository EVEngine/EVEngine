#pragma once
#include "graphics/RenderSystem3D.h"

namespace eve::graphics::detail {
/** @brief Resolve the value-owned scene world matrix or the local TRS on the render thread. */
[[nodiscard]] EVENGINE_API_BACKENDS glm::mat4 modelFromTransform(const Renderable3D::Transform3D& transform);
/** @brief Conservative sphere stretch; preserves culling under scene-parent shear. */
[[nodiscard]] EVENGINE_API_BACKENDS float sphereStretch(const Renderable3D::Transform3D& transform);
}  // namespace eve::graphics::detail
