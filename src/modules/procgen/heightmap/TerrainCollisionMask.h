#pragma once

#include <memory>

#include "common/Result.h"

namespace eve::procgen {
class Heightmap;
enum class TerrainMaskBlend;

/** @brief Pcg baked collision-mask source categories and their inversion convention. */
enum class TerrainCollisionMaskType { RadiusTree, RadiusTag, LayerGameObject, LayerTree };

/**
 * @brief Own an ordered Pcg-style collision-mask stack made from caller-baked scalar rasters.
 * Geometry queries and cache invalidation remain with the caller; every added raster is copied and no scene object,
 * callback, renderer resource, clock or RNG survives a call.
 */
class EVENGINE_API_DOMAINS TerrainCollisionMaskStack {
public:
    TerrainCollisionMaskStack();
    ~TerrainCollisionMaskStack();
    TerrainCollisionMaskStack(TerrainCollisionMaskStack&&) noexcept;
    TerrainCollisionMaskStack& operator=(TerrainCollisionMaskStack&&) noexcept;
    TerrainCollisionMaskStack(const TerrainCollisionMaskStack&) = delete;
    TerrainCollisionMaskStack& operator=(const TerrainCollisionMaskStack&) = delete;
    /** @brief Copy one finite baked mask and append its active/invert metadata. */
    [[nodiscard]] Result<int> addLayer(const Heightmap& mask, TerrainCollisionMaskType type, bool active, bool invert);
    /** @brief Remove every owned layer. */
    void clear();
    /** @brief Return retained layer count, including inactive layers. */
    [[nodiscard]] int getLayerCount() const noexcept;
    /**
     * @brief Combine active layers, apply a strength curve, then blend with input atomically.
     * Target and input share dimensions; layer resolutions may differ and use Clamp/Bilinear sampling.
     */
    [[nodiscard]] Result<int> apply(Heightmap& target, const Heightmap& input, const Heightmap& curve,
                                    TerrainMaskBlend mode) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace eve::procgen
