#pragma once
#include "common/Export.h"

#include "animation/AnimMath.h"
#include "common/Result.h"

#include <cstdint>

namespace eve::animation {

class AnimPose;

/**
 * @brief Per-axis translation locks for extracted root-motion deltas.
 * @details Locked axes are zeroed in the controller-facing delta. When
 * bake-into-pose is enabled, locked axes keep their animated translation in the
 * pose so vertical bob (or similar) can remain while horizontal motion drives
 * a capsule.
 */
enum class RootMotionLockAxes : std::uint8_t {
    None         = 0,
    X            = 1u << 0,
    Y            = 1u << 1,
    Z            = 1u << 2,
    HorizontalXZ = static_cast<std::uint8_t>(X) | static_cast<std::uint8_t>(Z),
    VerticalY    = Y,
    All          = static_cast<std::uint8_t>(X) | static_cast<std::uint8_t>(Y) | static_cast<std::uint8_t>(Z),
};

/** @brief Bitwise OR for RootMotionLockAxes masks. */
[[nodiscard]] constexpr RootMotionLockAxes operator|(RootMotionLockAxes a, RootMotionLockAxes b) noexcept {
    return static_cast<RootMotionLockAxes>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
}

/** @brief Bitwise AND for RootMotionLockAxes masks. */
[[nodiscard]] constexpr RootMotionLockAxes operator&(RootMotionLockAxes a, RootMotionLockAxes b) noexcept {
    return static_cast<RootMotionLockAxes>(static_cast<std::uint8_t>(a) & static_cast<std::uint8_t>(b));
}

/** @brief True when mask contains the given axis bit. */
[[nodiscard]] constexpr bool hasLock(RootMotionLockAxes mask, RootMotionLockAxes axis) noexcept {
    return (static_cast<std::uint8_t>(mask) & static_cast<std::uint8_t>(axis)) != 0;
}

/**
 * @brief Coordinate space used when publishing root-motion translation.
 * @details BoneLocal keeps the delta in the root bone's sampled local axes.
 * CharacterFacing rotates the planar XZ components by characterYaw around +Y so
 * forward matches Motion Matching: (sin(yaw), 0, cos(yaw)).
 */
enum class RootMotionApplySpace : std::uint8_t {
    BoneLocal        = 0,
    CharacterFacing  = 1,
};

/**
 * @brief Policy applied after raw root-bone delta extraction.
 * @ownership Value type; owned by AnimPlayer or the caller.
 * @thread Owner thread only; no callbacks.
 */
struct RootMotionPolicy {
    RootMotionLockAxes   lockAxes                 = RootMotionLockAxes::None;
    RootMotionApplySpace applySpace               = RootMotionApplySpace::BoneLocal;
    bool                 bakeTranslationIntoPose  = false;
    bool                 bakeRotationIntoPose     = false;
    bool                 lockRotation             = false;
    /** @brief Yaw in radians for CharacterFacing; ignored for BoneLocal. */
    float                characterYaw             = 0.f;
};

/**
 * @brief Validate policy fields.
 * @return Applied for a finite yaw and known enum values; InvalidArgument otherwise.
 */
[[nodiscard]] EVENGINE_API_WORLD eve::Result<void> validateRootMotionPolicy(const RootMotionPolicy& policy);

/**
 * @brief Filter a raw root-bone delta into a controller-facing delta.
 * @details Applies axis locks, optional rotation lock, then CharacterFacing yaw.
 * Does not modify any pose. Non-finite input components become zeroed components.
 */
[[nodiscard]] EVENGINE_API_WORLD TransformTRS applyRootMotionPolicy(const TransformTRS& rawDelta,
                                                                    const RootMotionPolicy& policy);

/**
 * @brief Remove extracted root motion from a pose when bake flags are set.
 * @param pose Mutable local pose; borrowed for this call only.
 * @param boneIndex Root-motion bone; must be in range.
 * @param previousRoot Local TRS of the root bone before this frame's sample.
 * @param policy Bake and lock flags; locked translation axes are not baked.
 * @return Applied, NoOp when nothing changes, or InvalidArgument for a bad bone.
 */
[[nodiscard]] EVENGINE_API_WORLD eve::Result<void> bakeRootMotionIntoPose(AnimPose&             pose,
                                                                          int                   boneIndex,
                                                                          const TransformTRS&   previousRoot,
                                                                          const RootMotionPolicy& policy);

}  // namespace eve::animation
