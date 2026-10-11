#pragma once

#include "common/Result.h"
#include "procgen/PointSet.h"

#include <cstdint>
#include <string>
#include <vector>

namespace eve::procgen {

/** @brief Random transform interval used by one object build rule. */
struct ObjectTransformRange {
    float minPitch     = 0.f;
    float maxPitch     = 0.f;
    float minYaw       = 0.f;
    float maxYaw       = 0.f;
    float minRoll      = 0.f;
    float maxRoll      = 0.f;
    float minScaleX    = 1.f;
    float maxScaleX    = 1.f;
    float minScaleY    = 1.f;
    float maxScaleY    = 1.f;
    float minScaleZ    = 1.f;
    float maxScaleZ    = 1.f;
    bool  uniformScale = false;
};

/**
 * @brief Deterministic TileWorldCreator-style object build layer.
 *
 * Input and output are owned values. Each source point emits one parent object point;
 * child rules emit additional points carrying `asset`, `object_role` and `parent_index`
 * attributes. The layer never creates scene objects or retains input pointers.
 * @thread Affine; configure and build on the owning thread.
 */
class ObjectBuildLayer {
public:
    /** @brief Add a weighted asset candidate. Zero and negative weights are rejected. */
    [[nodiscard]] Result<void> addAsset(std::string asset, float weight);
    void                       clearAssets();

    /** @brief Set the named deterministic random stream seed. */
    void setSeed(std::uint32_t seed) noexcept;
    /** @brief Set fixed layer translation applied after source transforms. */
    void setLayerOffset(float x, float y, float z) noexcept;
    /** @brief Set fixed component-wise scale multiplier. Values must be non-zero. */
    [[nodiscard]] Result<void> setLayerScale(float x, float y, float z);
    /** @brief Set circular XZ position jitter radius. */
    [[nodiscard]] Result<void> setPositionRadius(float radius);
    /** @brief Configure independent random Euler and scale intervals for parent objects. */
    [[nodiscard]] Result<void> setRandomTransform(const ObjectTransformRange& range);
    /** @brief Update only the parent Euler randomization interval. */
    [[nodiscard]] Result<void> setRandomRotation(float minPitch, float maxPitch, float minYaw, float maxYaw,
                                                 float minRoll, float maxRoll);
    /** @brief Update only the parent scale randomization interval. */
    [[nodiscard]] Result<void> setRandomScale(float minX, float maxX, float minY, float maxY, float minZ, float maxZ,
                                              bool uniform);
    /** @brief Enable orientation toward the first matching N/E/S/W then diagonal orientation point. */
    [[nodiscard]] Result<void> setOrientation(float cellSize, float yawOffset, bool invert);
    void                       disableOrientation() noexcept;
    /** @brief Project output Y to `surface_lowest_y` or `surface_highest_y`, plus offset, when present. */
    void setPlaceOnTop(bool enabled, bool useLowest, float topOffset) noexcept;

    /**
     * @brief Add a child-spawn rule with its own deterministic transform stream.
     * @param asset Asset identifier written to emitted child points.
     * @param count Children emitted per parent.
     * @param radius Circular XZ scatter radius around the parent.
     * @param range Random rotation and scale interval.
     */
    [[nodiscard]] Result<void> addChildRule(std::string asset, int count, float radius,
                                            const ObjectTransformRange& range);
    /** @brief Script-friendly child rule using uniform scale and yaw ranges. */
    [[nodiscard]] Result<void> addChild(std::string asset, int count, float radius, float minScale, float maxScale,
                                        float minYaw, float maxYaw);
    void                       clearChildRules();

    /**
     * @brief Build attributed object points from source points.
     * @param source Placement points; their attributes and transforms are preserved for parents.
     * @param orientation Optional point layer used to derive cardinal orientation.
     * @return Complete result, or a diagnostic without mutating the layer or either input.
     * @cost O(P*A + P*C + P*O) where P is source points, A assets, C emitted children,
     *       and O orientation points when orientation is enabled.
     */
    [[nodiscard]] Result<PointSet> build(const PointSet& source, const PointSet* orientation = nullptr) const;
    /** @brief Serialize all layer settings using the versioned EVPCG object-layer schema. */
    [[nodiscard]] std::string serializeDefinition() const;
    /** @brief Atomically replace settings from schema version 1; unknown records are rejected. */
    [[nodiscard]] Result<void> deserializeDefinition(std::string_view definition);

private:
    struct Asset {
        std::string id;
        float       weight = 1.f;
    };
    struct ChildRule {
        std::string          asset;
        int                  count  = 0;
        float                radius = 0.f;
        ObjectTransformRange transform;
    };

    std::vector<Asset>     assets_;
    std::vector<ChildRule> children_;
    ObjectTransformRange   transform_;
    std::uint32_t          seed_                 = 1;
    float                  offsetX_              = 0.f;
    float                  offsetY_              = 0.f;
    float                  offsetZ_              = 0.f;
    float                  scaleX_               = 1.f;
    float                  scaleY_               = 1.f;
    float                  scaleZ_               = 1.f;
    float                  positionRadius_       = 0.f;
    bool                   orient_               = false;
    bool                   invertOrientation_    = false;
    float                  orientationCellSize_  = 1.f;
    float                  orientationYawOffset_ = 0.f;
    bool                   placeOnTop_           = false;
    bool                   useLowest_            = false;
    float                  topOffset_            = 0.f;
};

}  // namespace eve::procgen
