#pragma once

#include "common/Result.h"

#include <cstddef>
#include <string>

namespace eve::animation {

class AnimClip;
class AnimRetargetProfile;
class AnimSkeleton;
class AnimSkin;

/**
 * @brief Inputs for neural MeshRet-style skinned motion retarget.
 * @note All pointers are borrowed for the duration of ISmrNeuralRetarget::retarget only.
 * @thread Owner thread of the animation/tensor composition; no re-entry on targetClip.
 */
struct SmrNeuralRequest {
    const AnimClip*            sourceClip     = nullptr;
    AnimClip*                  targetClip     = nullptr;  ///< FK-retargeted clip; overwritten on success.
    const AnimSkeleton*        sourceSkeleton = nullptr;
    const AnimSkeleton*        targetSkeleton = nullptr;
    const AnimRetargetProfile* profile        = nullptr;
    const AnimSkin*            sourceSkin     = nullptr;  ///< Optional LBS skin for denser SCS.
    const AnimSkin*            targetSkin     = nullptr;
};

/**
 * @brief Owning diagnostics from one neural retarget call.
 */
struct SmrNeuralResult {
    int         framesWritten = 0;
    std::string backend;  ///< "onnx" or "tensor-meshret".
    std::size_t sensorCount = 0;
    std::size_t pairCount   = 0;
};

/**
 * @brief Optional MeshRet/SMR neural retarget provider (implemented by animation_tensor).
 *
 * Animation stays free of tensor includes; the L6 satellite registers this capability.
 * When absent or not ready, classical two-bone IK SMR remains the fallback.
 *
 * @ownership Provider is owned by AnimationTensor; revoked before destruction.
 * @thread Composition/owner thread only. No callbacks, script re-entry, or background work.
 */
class ISmrNeuralRetarget {
public:
    static constexpr const char* capabilityName = "animation.ISmrNeuralRetarget";

    virtual ~ISmrNeuralRetarget() = default;

    /** @brief Stable backend label describing the live execution path. */
    [[nodiscard]] virtual std::string name() const = 0;

    /**
     * @brief True when a model is loaded (ONNX path) or the builtin tensor MeshRet graph is usable.
     * @param profile Borrowed for the call only; nullptr means not ready.
     * @note Does not retain profile; safe to call without a clip.
     * @ownership Does not retain profile.
     * @lifetime profile must outlive this call.
     * @thread Owner thread.
     */
    [[nodiscard]] virtual bool isReady(const AnimRetargetProfile* profile) const = 0;

    /**
     * @brief Run MeshRet-style inference and rewrite targetClip local rotations.
     * @return Owning diagnostics, or structured failure without mutating targetClip.
     * @lifetime See SmrNeuralRequest; pointers borrowed for this call only.
     * @thread Owner thread; not re-entrant on targetClip.
     */
    [[nodiscard]] virtual Result<SmrNeuralResult> retarget(const SmrNeuralRequest& request) = 0;
};

}  // namespace eve::animation
