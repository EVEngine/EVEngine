#pragma once

#include "ik/ChainSolver.h"
#include "ik/Skeleton3D.h"

#include "ik.hpp"

namespace eve::ik {

/** FABRIK solver for Skeleton3D. Script type: `Solver3D`. */
class Solver3D {
public:
    Solver3D() = default;
    ~Solver3D() = default;

    Solver3D(const Solver3D &)            = delete;
    Solver3D &operator=(const Solver3D &) = delete;

    void setKeepTrace(bool keep);
    bool getKeepTrace() const;

    void  setMaxIterations(int iterations);
    int   getMaxIterations() const;
    void  setTolerance(float tol);
    float getTolerance() const;
    void  setForce(float force);
    float getForce() const;

    /** Overall pose blend: 0 = keep the input pose, 1 = full solve (default). */
    void  setInfluence(float influence);
    float getInfluence() const;

    void clearTargets();
    void addTarget(int boneId, float x, float y, float z, float weight = 1.f);
    int  getTargetCount() const;

    bool solve(Skeleton3D *skeleton);
    void step(Skeleton3D *skeleton, float dt = 1.f);

    /**
     * FABRIK solve restricted to the bone chain rootBoneId..tipBoneId.
     * The chain root stays pinned and bones outside the chain are untouched;
     * only targets on the chain participate. See ChainSolver.h for details.
     */
    bool solveChain(Skeleton3D *skeleton, int rootBoneId, int tipBoneId);
    void stepChain(Skeleton3D *skeleton, int rootBoneId, int tipBoneId, float dt = 1.f);

    /**
     * Pole vector (magnet / hint): pulls the middle joints of a chain toward a
     * point so the limb bends in the desired direction. Applies to solveChain /
     * stepChain; weight is clamped to [0, 1].
     */
    void  setPole(float x, float y, float z, float weight);
    void  clearPole();
    bool  hasPole() const;
    float getPoleWeight() const;

    /**
     * Tip orientation override: after solving, blends the tip bone's local
     * yaw/pitch toward the given angles (weight in [0, 1]). Mirrors Godot's
     * override_tip_basis / Unity's target rotation weight.
     */
    void  setTipRotation(int boneId, float yaw, float pitch, float weight);
    void  clearTipRotation();
    float getTipRotationWeight() const;

    int  getTraceSize() const;
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
