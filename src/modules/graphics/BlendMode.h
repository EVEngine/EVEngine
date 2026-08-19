#pragma once

namespace eve::graphics {

/** @brief 2D quad blend mode (drawn in draw order within a layer). */
enum class BlendMode {
    Alpha = 0,     // SrcAlpha / OneMinusSrcAlpha (default)
    Additive = 1,  // SrcAlpha / One (emissive VFX)
    Opaque = 2,    // no blending
};

}  // namespace eve::graphics
