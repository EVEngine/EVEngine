#pragma once

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

    void clearTargets();
    void addTarget(int boneId, float x, float y, float z, float weight = 1.f);
    int  getTargetCount() const;

    bool solve(Skeleton3D *skeleton);
    void step(Skeleton3D *skeleton, float dt = 1.f);

    int  getTraceSize() const;
    void clearTrace();

private:
    ::ik::solver3d solver_;
    bool           keepTrace_     = false;
    int            maxIterations_ = 16;
    float          tolerance_     = 1e-3f;
    float          force_         = 0.f;
};

}  // namespace eve::ik
