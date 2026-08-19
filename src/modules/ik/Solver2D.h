#pragma once

#include "ik/ChainSolver.h"
#include "ik/Skeleton2D.h"

#include "ik.hpp"

namespace eve::ik {

/** @brief FABRIK solver for Skeleton2D. Script type: `Solver2D`. */
class Solver2D {
public:
    Solver2D() = default;
    ~Solver2D() = default;

    Solver2D(const Solver2D &)            = delete;
    Solver2D &operator=(const Solver2D &) = delete;

    void setKeepTrace(bool keep);
    bool getKeepTrace() const;

    void  setMaxIterations(int iterations);
    int   getMaxIterations() const;
    void  setTolerance(float tol);
    float getTolerance() const;
    void  setForce(float force);
    float getForce() const;

    /** @brief Overall pose blend: 0 = keep the input pose, 1 = full solve (default). */
    void  setInfluence(float influence);
    float getInfluence() const;

    void clearTargets();
    void addTarget(int boneId, float x, float y, float weight = 1.f);
    int  getTargetCount() const;

    /** @brief Returns true if every target is within tolerance. */
    bool solve(Skeleton2D *skeleton);
    void step(Skeleton2D *skeleton, float dt = 1.f);

    /**
     * @brief FABRIK solve restricted to the bone chain rootBoneId..tipBoneId.
     * The chain root stays pinned and bones outside the chain are untouched;
     * only targets on the chain participate. See ChainSolver.h for details.
     */
    bool solveChain(Skeleton2D *skeleton, int rootBoneId, int tipBoneId);
    void stepChain(Skeleton2D *skeleton, int rootBoneId, int tipBoneId, float dt = 1.f);

    int  getTraceSize() const;
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
