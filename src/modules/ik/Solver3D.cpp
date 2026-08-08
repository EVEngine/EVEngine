#include "ik/Solver3D.h"

#include "common/Exception.h"

namespace eve::ik {

void Solver3D::setKeepTrace(bool keep) {
    keepTrace_ = keep;
    solver_.setKeepTrace(keep);
}

bool Solver3D::getKeepTrace() const { return keepTrace_; }

void Solver3D::setMaxIterations(int iterations) {
    if (iterations < 0) {
        throw Exception("Solver3D.setMaxIterations: iterations must be >= 0");
    }
    maxIterations_ = iterations;
    solver_.setMaxIterations(static_cast<unsigned>(iterations));
}

int Solver3D::getMaxIterations() const { return maxIterations_; }

void Solver3D::setTolerance(float tol) {
    if (tol < 0.f) {
        throw Exception("Solver3D.setTolerance: tolerance must be >= 0");
    }
    tolerance_ = tol;
    solver_.setTolerance(tol);
}

float Solver3D::getTolerance() const { return tolerance_; }

void Solver3D::setForce(float force) {
    force_ = force;
    solver_.setForce(force);
}

float Solver3D::getForce() const { return force_; }

void Solver3D::clearTargets() { solver_.clearTargets(); }

void Solver3D::addTarget(int boneId, float x, float y, float z, float weight) {
    if (boneId < 0) {
        throw Exception("Solver3D.addTarget: boneId must be >= 0");
    }
    solver_.addTarget(static_cast<unsigned>(boneId), ::ik::vec3{x, y, z}, weight);
}

int Solver3D::getTargetCount() const {
    return static_cast<int>(solver_.targets().size());
}

bool Solver3D::solve(Skeleton3D *skeleton) {
    if (!skeleton) {
        throw Exception("Solver3D.solve: skeleton is null");
    }
    return solver_.solve(skeleton->native(), skeleton->state());
}

void Solver3D::step(Skeleton3D *skeleton, float dt) {
    if (!skeleton) {
        throw Exception("Solver3D.step: skeleton is null");
    }
    solver_.step(skeleton->native(), skeleton->state(), dt);
}

int Solver3D::getTraceSize() const { return static_cast<int>(solver_.traceSize()); }

void Solver3D::clearTrace() { solver_.clearTrace(); }

}  // namespace eve::ik
