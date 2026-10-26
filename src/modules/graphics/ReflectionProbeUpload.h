#pragma once

/**
 * @file ReflectionProbeUpload.h
 * @brief Local-probe upload record kept off the high-fanout IGraphics3D header.
 *
 * Embeds complete `glm::vec3` members; call sites that set probes include this
 * header. `IGraphics3D` only forward-declares the type for a const-ref parameter.
 */

#include <glm/vec3.hpp>

namespace eve::graphics {

class Texture;

/** @brief ReflectionProbeUpload public API. */
struct ReflectionProbeUpload {
    static constexpr int kMaxProbes = 2;
    /** @brief Probe public API. */
    struct Probe {
        Texture*  cubemap = nullptr;
        glm::vec3 center{0.f};
        glm::vec3 extent{0.f};
        float     intensity     = 0.f;
        float     blendDistance = 0.f;
        int       priority      = 0;
    };
    Probe probes[kMaxProbes]{};
    int   count = 0;
};

}  // namespace eve::graphics
