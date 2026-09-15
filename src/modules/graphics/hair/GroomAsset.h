#pragma once

#include "common/Result.h"
#include "graphics/hair/StrandsDatas.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace eve::graphics::hair {

/** @brief Geometry representation selected by a groom LOD (UE GeometryType analogue). */
enum class Representation : uint8_t { Strands = 0, Cards = 1, Meshes = 2, None = 3 };

/** @brief One LOD entry for a groom group. */
struct GroomLod {
    /** @brief Switch to this LOD when screen size falls below this value (0..1). */
    float screenSize = 1.f;
    Representation representation = Representation::Strands;
    /** @brief Keep this fraction of curves when baking ribbons (1 = all). */
    float curveFraction = 1.f;
    /** @brief Extra thickness scale applied when curves are decimated. */
    float thicknessScale = 1.f;
};

/**
 * @brief One groom group (UE hair group analogue: bangs / scalp / beard …).
 * @ownership Owned by `GroomAsset`.
 */
struct GroomGroup {
    std::string name;
    uint32_t groupId = 0;
    StrandsDatas strands;
    /**
     * @brief Optional simulation / interpolation guides (may be empty).
     * When empty, callers can derive guides via `extractGuides(strands, …)`.
     */
    StrandsDatas guides;
    std::vector<GroomLod> lods;
};

/**
 * @brief Shared CPU groom container (UE `UGroomAsset` analogue).
 *
 * Phase 2 stores strands plus LOD tables / cluster cull. Geometric Cards
 * meshes are owned by `graphics/HairCards` (separate PR); Meshes later.
 *
 * @thread Affine to the caller; not synchronized.
 * @ownership Instance may copy groups on `setAsset`; asset remains caller-owned.
 */
class GroomAsset {
public:
    /**
     * @brief Append a group; fails when name is empty or groupId collides.
     * @ownership `group` is moved into the asset on success.
     */
    [[nodiscard]] Result<void> addGroup(GroomGroup group);

    [[nodiscard]] size_t groupCount() const { return groups_.size(); }
    [[nodiscard]] const GroomGroup *groupAt(size_t index) const;
    [[nodiscard]] const GroomGroup *findGroupById(uint32_t groupId) const;

    void clear();

    /** @brief Validate every group strands buffer and LOD table. */
    [[nodiscard]] Result<void> validate() const;

private:
    std::vector<GroomGroup> groups_;
};

/**
 * @brief Pick the LOD index for a given screen size (largest screenSize ≤ query).
 * @return 0 when `lods` is empty.
 */
[[nodiscard]] size_t selectLodIndex(const std::vector<GroomLod> &lods, float screenSize);

}  // namespace eve::graphics::hair
