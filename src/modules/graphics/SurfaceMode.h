#pragma once

namespace eve::graphics {

/** @brief How a 3D surface contributes to depth and color passes. */
enum class SurfaceMode {
    Opaque = 0,
    Masked = 1,
    Transparent = 2,
};

}  // namespace eve::graphics
