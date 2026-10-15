#pragma once
#include "common/Export.h"

#include "ik/ChainSolver.h"
#include "ik/Skeleton3D.h"

#include "ik.hpp"

namespace eve::ik {

/** @brief FABRIK solver for Skeleton3D. Script type: `Solver3D`. */
class EVENGINE_API_FOUNDATION Solver3D {
public:
    /** @brief Creates a 3D FABRIK solver with default tolerances. */
    Solver3D() = default;
    /** @brief Releases solver state. */
    ~Solver3D() = default;

    Solver3D(const Solver3D &)            = delete;
    Solver3D &operator=(const Solver3D &) = delete;

    /** @brief When true, records intermediate solve samples. */
    void setKeepTrace(bool keep);
    /** @brief Whether solve tracing is enabled. */
    bool getKeepTrace() const;

    /** @brief Maximum FABRIK iterations per solve. */
    void  setMaxIterations(int iterations);
    /** @brief Configured maximum FABRIK iterations. */
    int   getMaxIterations() const;
    /** @brief End-effector distance tolerance for success. */
    void  setTolerance(float tol);
    /** @brief Configured end-effector tolerance. */
    float getTolerance() const;
    /** @brief Soft-IK blend toward the target (0 = hard IK). */
    void  setForce(float force);
    /** @brief Configured soft-IK force. */
    float getForce() const;

    /** @brief Overall pose blend: 0 = keep the input pose, 1 = full solve (default). */
    void  setInfluence(float influence);
    /** @brief Pose blend weight (0 = keep input, 1 = full solve). */
    float getInfluence() const;

    /** @brief Removes all IK targets. */
    void clearTargets();
    /** @brief Adds an end-effector target for a bone. */
    void addTarget(int boneId, float x, float y, float z, float weight = 1.f);
    /** @brief Number of configured targets. */
    int  getTargetCount() const;

    /** @brief Runs FABRIK until tolerance or max iterations. @return True if all targets within tolerance. */
    bool solve(Skeleton3D *skeleton);
    /** @brief Runs a time-scaled FABRIK step (dt scales iteration effort). */
    void step(Skeleton3D *skeleton, float dt = 1.f);

    /**
     * @brief FABRIK solve restricted to the bone chain rootBoneId..tipBoneId.
     * The chain root stays pinned and bones outside the chain are untouched;
     * only targets on the chain participate. See ChainSolver.h for details.
     */
    bool solveChain(Skeleton3D *skeleton, int rootBoneId, int tipBoneId);
    /** @brief Time-scaled chain-scoped FABRIK step. */
    void stepChain(Skeleton3D *skeleton, int rootBoneId, int tipBoneId, float dt = 1.f);

    /**
     * @brief Pole vector (magnet / hint): pulls the middle joints of a chain toward a
     * point so the limb bends in the desired direction. Applies to solveChain /
     * stepChain; weight is clamped to [0, 1].
     */
    void  setPole(float x, float y, float z, float weight);
    /** @brief Disables the pole-vector hint. */
    void  clearPole();
    /** @brief True when a pole vector is configured. */
    bool  hasPole() const;
    /** @brief Configured pole-vector weight in [0,1]. */
    float getPoleWeight() const;

    /**
     * @brief Tip orientation override: after solving, blends the tip bone's local
     * yaw/pitch toward the given angles (weight in [0, 1]). Mirrors Godot's
     * override_tip_basis / Unity's target rotation weight.
     */
    void  setTipRotation(int boneId, float yaw, float pitch, float weight);
    /** @brief Clears tip orientation override. */
    void  clearTipRotation();
    /** @brief Configured tip rotation blend weight. */
    float getTipRotationWeight() const;

    /** @brief Number of recorded trace samples. */
    int  getTraceSize() const;
    /** @brief Clears recorded solve trace samples. */
    void clearTrace();

private:
    bool solveChainImpl(Skeleton3D *skeleton, int rootBoneId, int tipBoneId,
                        const detail::ChainOptions<3> &options);
    void applyTipRotation(Skeleton3D *skeleton);

    ::ik::solver3d solver_;
    bool           keepTrace_     = false;
    int            maxIterations_ = 16;
    float          tolerance_     = 1e-3f;
    float          force_         = 0.f;
    float          influence_     = 1.f;

    bool       usePole_     = false;
    ::ik::vec3 pole_{};
    float      poleWeight_  = 1.f;

    int   tipBoneId_     = -1;
    float tipYaw_        = 0.f;
    float tipPitch_      = 0.f;
    float tipRotWeight_  = 0.f;
};

}  // namespace eve::ik
