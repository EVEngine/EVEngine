#pragma once

#include "animation/AnimSmrNeural.h"
#include "animation/tensor/SmrFeatures.h"
#include "animation/tensor/SmrMeshRetNet.h"
#include "animation/tensor/SmrOnnxRunner.h"

#include <string>
#include <vector>

namespace eve::animation {

/**
 * @brief Capability provider selecting ONNX MeshRet or builtin tensor MeshRet.
 * @ownership Owned by AnimationTensor; registered via cap::provide until revoke.
 * @thread Owner/composition thread only; no callbacks while holding locks.
 */
class SmrNeuralProvider final : public ISmrNeuralRetarget {
public:
    SmrNeuralProvider() = default;

    [[nodiscard]] std::string name() const override;

    /**
     * @brief True when ONNX is loadable for the profile path, or builtin MeshRet is usable.
     * @param profile Borrowed for the call only; nullptr means not ready.
     * @ownership Does not retain profile.
     * @lifetime profile must outlive this call.
     * @thread Owner thread.
     */
    [[nodiscard]] bool isReady(const AnimRetargetProfile* profile) const override;

    /**
     * @brief Run inference and rewrite request.targetClip local rotations.
     * @lifetime See SmrNeuralRequest; pointers borrowed for this call only.
     * @thread Owner thread; not re-entrant on targetClip.
     */
    [[nodiscard]] Result<SmrNeuralResult> retarget(const SmrNeuralRequest& request) override;

private:
    mutable SmrOnnxRunner onnx_;
    mutable std::string   loadedPath_;
    mutable bool          modelLoaded_ = false;
    SmrMeshRetNet         net_{};

    [[nodiscard]] Result<void>            ensureOnnx(const AnimRetargetProfile& profile) const;
    [[nodiscard]] Result<SmrNeuralResult> applyRot6d(const SmrNeuralRequest& request, const SmrFeatureBatch& features,
                                                     const std::vector<float>& rot6d, const std::string& backend) const;
};

}  // namespace eve::animation
