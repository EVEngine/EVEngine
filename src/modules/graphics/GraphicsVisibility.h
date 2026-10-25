#pragma once

/**
 * @file GraphicsVisibility.h
 * @brief Entity-ID / isolated visibility capture kept off Graphics.h.
 *
 * `EntityIdDraw` embeds complete glm matrix/color members. Editors and scene
 * visibility tools include this header; the high-fanout Graphics facade only
 * exposes `visibilityCapture()`.
 */

#include "common/Result.h"

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <vector>

namespace eve::image {
class ImageData;
}

namespace eve::graphics {

class Mesh;

/** @brief One mesh contribution to an entity-ID or visibility capture pass. */
struct EntityIdDraw {
    Mesh*     mesh = nullptr;
    glm::mat4 model{1.f};
    glm::vec4 idColor{0.f, 0.f, 0.f, 1.f};
};

/** @brief Owning isolated raster evidence; depth is Vulkan NDC in [0,1]. */
struct VisibilityPixels {
    int                        width = 0, height = 0;
    std::vector<std::uint32_t> ids;
    std::vector<float>         depth;
};

/**
 * @brief Offscreen entity-ID and isolated visibility capture surface.
 * @remarks Queried through `Graphics::visibilityCapture()`; absent backends return null.
 */
class IVisibilityCapture {
public:
    /** @brief Releases IVisibilityCapture resources. */
    virtual ~IVisibilityCapture() = default;

    /**
     * @brief Per-pixel mesh entity-ID pass into an offscreen target, then CPU readback.
     * @ownership Result transfers to caller. Draw inputs are borrowed only for this call.
     * @lifetime Returned ImageData is owned by the caller; draws are valid only for the call.
     */
    [[nodiscard]] virtual image::ImageData* renderEntityIdMask(const std::vector<EntityIdDraw>& draws,
                                                               const glm::mat4& viewProj, int w, int h) = 0;

    /**
     * @brief Render opaque draw IDs and float depth into private targets and wait for readback.
     * @ownership Draw meshes are borrowed for this synchronous call; output owns its storage.
     * @thread Render owner thread only. No callbacks or scene mutations.
     * @cost GPU raster and blocking readback proportional to pixels and triangles per requested pass.
     */
    [[nodiscard]] virtual eve::Result<VisibilityPixels> captureVisibilityPixels(const std::vector<EntityIdDraw>& draws,
                                                                                const glm::mat4& viewProj, int w,
                                                                                int h) = 0;
};

}  // namespace eve::graphics
