#pragma once

#include <glm/vec4.hpp>

namespace eve {

/** @brief Render-neutral RGBA color shared by graphics-facing modules. */
using Color = glm::vec4;

/** @brief Render-neutral 2D blend mode shared by graphics-facing modules. */
enum class BlendMode {
    Alpha = 0,
    Additive = 1,
    Opaque = 2,
};

}  // namespace eve
