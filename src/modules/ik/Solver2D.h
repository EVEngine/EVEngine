#pragma once
#include "common/Export.h"

#include "ik/ChainSolver.h"
#include "ik/Skeleton2D.h"

#include "ik.hpp"

namespace eve::ik {

/** @brief FABRIK solver for Skeleton2D. Script type: `Solver2D`. */
class EVENGINE_API_FOUNDATION Solver2D {
public:
    /** @brief Creates a 2D FABRIK solver with default tolerances. */
    Solver2D() = default;
    /** @brief Releases solver state. */
    ~Solver2D() = default;

    Solver2D(const Solver2D &)            = delete;
    Solver2D &operator=(const Solver2D &) = delete;

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
    void addTarget(int boneId, float x, float y, float weight = 1.f);
    /** @brief Number of configured targets. */
    int  getTargetCount() const;

    /** @brief Returns true if every target is within tolerance. */
    bool solve(Skeleton2D *skeleton);
    /** @brief Runs a time-scaled FABRIK step (dt scales iteration effort). */
    void step(Skeleton2D *skeleton, float dt = 1.f);

    /**
     * @brief FABRIK solve restricted to the bone chain rootBoneId..tipBoneId.
     * The chain root stays pinned and bones outside the chain are untouched;
     * only targets on the chain participate. See ChainSolver.h for details.
     */
    bool solveChain(Skeleton2D *skeleton, int rootBoneId, int tipBoneId);
    /** @brief Time-scaled chain-scoped FABRIK step. */
    void stepChain(Skeleton2D *skeleton, int rootBoneId, int tipBoneId, float dt = 1.f);

    /** @brief Number of recorded trace samples. */
    int  getTraceSize() const;
    /** @brief Clears recorded solve trace samples. */
    void clearTrace();

private:
    bool solveChainImpl(Skeleton2D *skeleton, int rootBoneId, int tipBoneId,
                        const detail::ChainOptions<2> &options);

    ::ik::solver2d solver_;
    bool           keepTrace_     = false;
    int            maxIterations_ = 16;
    float          tolerance_     = 1e-3f;
    float          force_         = 0.f;
    float          influence_     = 1.f;
};

}  // namespace eve::ik
