#include "ik/Solver2D.h"

#include "common/Exception.h"

namespace eve::ik {

void Solver2D::setKeepTrace(bool keep) {
    keepTrace_ = keep;
    solver_.setKeepTrace(keep);
}

bool Solver2D::getKeepTrace() const { return keepTrace_; }

void Solver2D::setMaxIterations(int iterations) {
    if (iterations < 0) {
        throw Exception("Solver2D.setMaxIterations: iterations must be >= 0");
    }
    maxIterations_ = iterations;
    solver_.setMaxIterations(static_cast<unsigned>(iterations));
}

int Solver2D::getMaxIterations() const { return maxIterations_; }

void Solver2D::setTolerance(float tol) {
    if (tol < 0.f) {
        throw Exception("Solver2D.setTolerance: tolerance must be >= 0");
    }
    tolerance_ = tol;
    solver_.setTolerance(tol);
}

float Solver2D::getTolerance() const { return tolerance_; }

void Solver2D::setForce(float force) {
    force_ = force;
    solver_.setForce(force);
}

float Solver2D::getForce() const { return force_; }

void Solver2D::clearTargets() { solver_.clearTargets(); }

void Solver2D::addTarget(int boneId, float x, float y, float weight) {
    if (boneId < 0) {
        throw Exception("Solver2D.addTarget: boneId must be >= 0");
    }
    solver_.addTarget(static_cast<unsigned>(boneId), ::ik::vec2{x, y}, weight);
}

int Solver2D::getTargetCount() const {
    return static_cast<int>(solver_.targets().size());
}

bool Solver2D::solve(Skeleton2D *skeleton) {
    if (!skeleton) {
        throw Exception("Solver2D.solve: skeleton is null");
    }
    return solver_.solve(skeleton->native(), skeleton->state());
}

void Solver2D::step(Skeleton2D *skeleton, float dt) {
    if (!skeleton) {
        throw Exception("Solver2D.step: skeleton is null");
    }
    solver_.step(skeleton->native(), skeleton->state(), dt);
}

int Solver2D::getTraceSize() const { return static_cast<int>(solver_.traceSize()); }

void Solver2D::clearTrace() { solver_.clearTrace(); }

}  // namespace eve::ik
