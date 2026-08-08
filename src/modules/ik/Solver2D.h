#pragma once

#include "ik/Skeleton2D.h"

#include "ik.hpp"

namespace eve::ik {

/** FABRIK solver for Skeleton2D. Script type: `Solver2D`. */
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

    void clearTargets();
    void addTarget(int boneId, float x, float y, float weight = 1.f);
    int  getTargetCount() const;

    /** Returns true if every target is within tolerance. */
    bool solve(Skeleton2D *skeleton);
    void step(Skeleton2D *skeleton, float dt = 1.f);

    int  getTraceSize() const;
    void clearTrace();

private:
    ::ik::solver2d solver_;
    bool           keepTrace_     = false;
    int            maxIterations_ = 16;
    float          tolerance_     = 1e-3f;
    float          force_         = 0.f;
};

}  // namespace eve::ik
