#pragma once

#include "graphics/PrimitiveDrawList.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace eve::graphics {

/** @brief Resolved triangle vertex consumed by primitive GPU backends. */
struct PrimitiveTriangleVertex {
    glm::vec4     clipPosition{0.f};
    Color         color{1.f, 1.f, 1.f, 1.f};
    float         pathDistance   = 0.f;
    float         edgeCoordinate = 0.f;
    std::uint32_t commandIndex   = 0;
};

/** @brief One contiguous 3D pipeline/sort batch in the resolved vertex stream. */
struct ResolvedPrimitiveBatch3D {
    std::size_t         firstVertex = 0;
    std::size_t         vertexCount = 0;
    ScenePrimitivePaint paint;
    float               averageDepth = 0.f;
    std::size_t         sequence     = 0;
};

/** @brief One contiguous 2D blend/order batch in the resolved vertex stream. */
struct ResolvedPrimitiveBatch2D {
    std::size_t firstVertex = 0;
    std::size_t vertexCount = 0;
    BlendMode   blend       = BlendMode::Alpha;
    std::size_t sequence    = 0;
};

/** @brief Owning backend-neutral triangle stream for one primitive pass. */
struct ResolvedPrimitiveTriangles {
    std::vector<PrimitiveTriangleVertex>  vertices;
    std::vector<ResolvedPrimitiveBatch2D> batches2D;
    std::vector<ResolvedPrimitiveBatch3D> batches3D;
    PrimitiveDrawStatistics               statistics;
};

/**
 * @brief Resolves 2D stroke segment bodies into clip-space triangles.
 * @param canvas Source command recorder.
 * @param viewport Positive pixel dimensions of the current Canvas target.
 * @return Owning triangle stream; six vertices are emitted per non-zero segment.
 */
[[nodiscard]] ResolvedPrimitiveTriangles resolvePrimitiveStrokes2D(const PrimitiveCanvas2D& canvas,
                                                                   glm::ivec2               viewport);

/**
 * @brief Resolves 3D stroke segment bodies with near-plane clipping.
 * @return Owning clip-space triangles. Fully clipped and zero-length segments are omitted.
 */
[[nodiscard]] ResolvedPrimitiveTriangles resolvePrimitiveStrokes3D(const PrimitiveSceneCanvas3D& canvas);

}  // namespace eve::graphics
