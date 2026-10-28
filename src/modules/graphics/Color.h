#pragma once

#include "common/RenderTypes.h"

namespace eve::graphics {

/**
 * @brief RGBA color used by every graphics draw call.
 * Lives inside eve::graphics so including a graphics header cannot pollute
 * the global namespace. Kept in its own tiny header so modules that only need
 * the color type (e.g. map, particles) do not have to include Canvas.h.
 */
using Color = eve::Color;

}  // namespace eve::graphics
