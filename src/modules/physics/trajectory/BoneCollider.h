#pragma once
#include "common/Export.h"

/**
 * @file BoneCollider.h
 * @brief Authoring definitions for bone-bound collision proxies (UE5 Physics Asset style).
 */

#include "common/AttachmentPoint.h"
#include "common/Result.h"

#include <cstdint>
#include <string>

namespace eve::physics::trajectory {

/** @brief Primitive used for one bone-bound collision proxy. */
enum class BoneColliderShapeKind : std::uint8_t { Sphere, Capsule };

/**
 * @brief Catalog entry that binds a sphere or capsule to named attachment points.
 *
 * Sphere samples `boneName` once with `localOffset`. Capsule either:
 * - samples the same bone twice with `localOffset ± (0, halfHeight, 0)` when
 *   `endBoneName` is empty (Physics Asset style), or
 * - samples `boneName`/`endBoneName` with their local offsets (weapon-blade /
 *   dual-socket sweep style, matching UE5 anim-notify traces).
 *
 * Filter bits mirror World3D query category/mask; they are applied only for the
 * duration of each sweep and do not mutate persistent world defaults.
 */
struct EVENGINE_API_DOMAINS BoneColliderDefinition {
    /** @brief Stable catalog id (e.g. weapon.blade, hand.L). */
    std::string colliderId;
    /** @brief Primary / start attachment name resolved by IAttachmentPointSource. */
    std::string boneName;
    /**
     * @brief Optional second attachment for dual-socket capsules.
     * Empty means a single-bone capsule axis along local +Y through the center.
     */
    std::string endBoneName;
    BoneColliderShapeKind kind = BoneColliderShapeKind::Sphere;
    /** @brief Non-negative finite collision radius in meters. */
    float radius = 0.f;
    /** @brief Half-length along local +Y for single-bone capsules; ignored otherwise. */
    float halfHeight = 0.f;
    /** @brief Local offset on `boneName` (sphere center or capsule start/center). */
    AttachmentPoint localOffset{};
    /** @brief Local offset on `endBoneName` for dual-socket capsules. */
    AttachmentPoint endLocalOffset{};
    /** @brief Query category bits applied during sweeps. */
    std::uint32_t categoryBits = 0xFFFFFFFFu;
    /** @brief Query mask bits applied during sweeps. */
    std::uint32_t maskBits = 0xFFFFFFFFu;
    /** @brief Optional body id ignored by sweeps (-1 = none). */
    int ignoredBodyId = -1;
    /** @brief Maximum World3D cast results retained per armed collider per frame. */
    int maxHits = 8;

    /**
     * @brief Reject empty ids, non-finite dimensions, or inconsistent shape fields.
     * @return Applied when valid; InvalidArgument otherwise.
     */
    [[nodiscard]] Result<void> validate() const;
};

}  // namespace eve::physics::trajectory
