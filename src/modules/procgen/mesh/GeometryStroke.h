#pragma once

#include "common/Result.h"
#include "procgen/MeshBuild.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace eve::procgen {

/** @brief One input sample owned by a procedural geometry stroke. */
struct GeometryStrokePoint {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
};

/**
 * @brief Runtime/editor-neutral session that turns an input trail into mesh geometry.
 *
 * The session is synchronous, owner-thread-only, and retains only copied point values.
 * Callers own input timing and raycasting. Mutations increment a monotonic revision and
 * failures leave the point sequence unchanged.
 */
class GeometryStroke {
public:
    /** @brief Select quad, triangularPrism, or cube cross-section generation. */
    [[nodiscard]] Result<void> setShapeResult(std::string_view shape);
    /** @brief Select spatial input or planar input constrained to planeY. */
    [[nodiscard]] Result<void> setInputSpaceResult(std::string_view inputSpace, float planeY = 0.f);
    /** @brief Set positive stroke width and depth. */
    [[nodiscard]] Result<void> setSizeResult(float width, float depth);
    /** @brief Set the non-negative minimum accepted distance between input points. */
    [[nodiscard]] Result<void> setMinimumSpacingResult(float spacing);
    /** @brief Append a finite point; returns false when minimum spacing rejects it. */
    [[nodiscard]] Result<bool> addPointResult(float x, float y, float z);
    /** @brief Remove the most recently accepted point. */
    [[nodiscard]] Result<void> undoResult();
    /** @brief Clear all input points. */
    void clear() noexcept;
    /** @brief Build an owning triangle mesh without mutating the stroke. */
    [[nodiscard]] Result<MeshBuild> buildMeshResult() const;

    /** @brief Return accepted point count. */
    [[nodiscard]] int pointCount() const noexcept { return static_cast<int>(points_.size()); }
    /** @brief Return the selected cross-section name. */
    [[nodiscard]] std::string_view shape() const noexcept { return shape_; }
    /** @brief Return the selected input-space name. */
    [[nodiscard]] std::string_view inputSpace() const noexcept { return inputSpace_; }
    /** @brief Return monotonic mutation revision. */
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

private:
    std::vector<GeometryStrokePoint> points_;
    std::string                      shape_      = "quad";
    std::string                      inputSpace_ = "spatial";
    float                            planeY_     = 0.f;
    float                            width_      = 0.25f;
    float                            depth_      = 0.25f;
    float                            spacing_    = 0.02f;
    std::uint64_t                    revision_   = 0;
};

}  // namespace eve::procgen
