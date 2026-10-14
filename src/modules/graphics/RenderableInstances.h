#pragma once
#include "graphics/RenderSystem3D.h"
namespace eve::graphics::detail {
/** @brief Validate instanced renderable. */
[[nodiscard]] Result<void> validateInstancedRenderable(const Renderable3D::MeshRenderer& renderer);
}
