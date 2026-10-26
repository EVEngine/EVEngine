#pragma once
#include "common/Export.h"

#include <memory>

namespace eve::ik {

class Skeleton2D;

/** @brief FABRIK solver for Skeleton2D. Script type: `Solver2D`.
 * @note Public headers intentionally hide the underlying ik.hpp types (Pimpl).
 */
class EVENGINE_API_FOUNDATION Solver2D {
public:
    /** @brief Creates a 2D FABRIK solver with default tolerances. */
    Solver2D();
    /** @brief Releases solver state. */
    ~Solver2D();

    Solver2D(const Solver2D &)            = delete;
    Solver2D &operator=(const Solver2D &) = delete;

    /** @brief When true, records intermediate solve samples. */
    void setKeepTrace(bool keep);
    /** @brief Whether solve tracing is enabled. */
    bool getKeepTrace() const;

    /** @brief Maximum FABRIK iterations per solve. */
    void setMaxIterations(int iterations);
    /** @brief Configured maximum FABRIK iterations. */
    int getMaxIterations() const;
    /** @brief End-effector distance tolerance for success. */
    void setTolerance(float tol);
    /** @brief Configured end-effector tolerance. */
    float getTolerance() const;
    /** @brief Soft-IK blend toward the target (0 = hard IK). */
    void setForce(float force);
    /** @brief Configured soft-IK force. */
    float getForce() const;

    /** @brief Overall pose blend: 0 = keep the input pose, 1 = full solve (default). */
    void setInfluence(float influence);
    /** @brief Pose blend weight (0 = keep input, 1 = full solve). */
    float getInfluence() const;

    /** @brief Removes all IK targets. */
    void clearTargets();
    /** @brief Adds an end-effector target for a bone. */
    void addTarget(int boneId, float x, float y, float weight = 1.f);
    /** @brief Number of configured targets. */
    int getTargetCount() const;

    /** @brief Returns true if every target is within tolerance. */
    bool solve(Skeleton2D *skeleton);
    /** @brief Runs a time-scaled FABRIK step (dt scales iteration effort). */
    void step(Skeleton2D *skeleton, float dt = 1.f);

    /**
     * @brief FABRIK solve restricted to the bone chain rootBoneId..tipBoneId.
     * The chain root stays pinned and bones outside the chain are untouched;
     * only targets on the chain participate.
     */
    bool solveChain(Skeleton2D *skeleton, int rootBoneId, int tipBoneId);
    /** @brief Time-scaled chain-scoped FABRIK step. */
    void stepChain(Skeleton2D *skeleton, int rootBoneId, int tipBoneId, float dt = 1.f);

    /** @brief Number of recorded trace samples. */
    int getTraceSize() const;
    /** @brief Clears recorded solve trace samples. */
    void clearTrace();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    void requireChain(Skeleton2D *skeleton, int rootBoneId, int tipBoneId) const;
};

}  // namespace eve::ik
