#pragma once

#include "common/Result.h"
#include "image/UvPaintSession.h"
#include "procgen/MeshBuild.h"

#include <cstdint>
#include <memory>

namespace eve::image { class ImageData; }

namespace eve::procgen {
/**
 * @brief Integrates dynamic-mesh hit mapping with the canonical image UV-paint session.
 *
 * Owns a current mesh snapshot and delegates all pixel history to image::UvPaintSession;
 * it does not implement a second raster painter. Updating a deformed mesh preserves paint.
 */
class DynamicMeshUvPaintSession {
public:
    /** @brief Initialize owning mesh and RGBA8 image snapshots atomically. */
    [[nodiscard]] Result<void> initializeResult(const MeshBuild& mesh, const image::ImageData& image);
    /** @brief Replace only the dynamic mesh snapshot after deformation. */
    [[nodiscard]] Result<void> updateMeshResult(const MeshBuild& mesh);
    /** @brief Map a triangle hit to UV and commit one canonical circular paint transaction. */
    [[nodiscard]] Result<void> paintSurfacePointResult(int triangleIndex, float x, float y, float z,
                                                       float radiusPixels, float r, float g, float b, float a,
                                                       bool wrapU = false, bool wrapV = false);
    /** @brief Return an owning current pixel snapshot. */
    [[nodiscard]] Result<std::unique_ptr<image::ImageData>> currentImageResult() const;
    /** @brief Undo the last paint transaction without changing mesh topology. */
    [[nodiscard]] Result<void> undoResult();
    std::uint64_t meshRevision() const noexcept { return meshRevision_; }
    std::uint64_t paintRevision() const noexcept { return paint_.revision(); }
private:
    MeshBuild mesh_;
    image::UvPaintSession paint_;
    std::uint64_t meshRevision_ = 0;
    bool initialized_ = false;
};
}  // namespace eve::procgen
