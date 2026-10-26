#include "ik/Solver2D.h"
#include "ik/Skeleton2D.h"
#include "ik/Skeleton3D.h"
#include "ik/Solver3D.h"
#include "ik/detail/SkeletonImpl.h"

#include "common/Exception.h"

#include "ik.hpp"

#include <utility>

namespace eve::ik {

struct Solver2D::Impl {
    ::ik::solver2d solver;
    bool           keepTrace     = false;
    int            maxIterations = 16;
    float          tolerance     = 1e-3f;
    float          force         = 0.f;
};

Solver2D::Solver2D() : impl_(std::make_unique<Impl>()) {}
Solver2D::~Solver2D() = default;

void Solver2D::setKeepTrace(bool keep) {
    impl_->keepTrace = keep;
    impl_->solver.setKeepTrace(keep);
}

bool Solver2D::getKeepTrace() const { return impl_->keepTrace; }

void Solver2D::setMaxIterations(int iterations) {
    if (iterations < 0) {
        throw Exception("Solver2D.setMaxIterations: iterations must be >= 0");
    }
    impl_->maxIterations = iterations;
    impl_->solver.setMaxIterations(static_cast<unsigned>(iterations));
}

int Solver2D::getMaxIterations() const { return impl_->maxIterations; }

void Solver2D::setTolerance(float tol) {
    if (tol < 0.f) {
        throw Exception("Solver2D.setTolerance: tolerance must be >= 0");
    }
    impl_->tolerance = tol;
    impl_->solver.setTolerance(tol);
}

float Solver2D::getTolerance() const { return impl_->tolerance; }

void Solver2D::setForce(float force) {
    impl_->force = force;
    impl_->solver.setForce(force);
}

float Solver2D::getForce() const { return impl_->force; }

void Solver2D::setInfluence(float influence) {
    if (influence < 0.f || influence > 1.f) {
        throw Exception("Solver2D.setInfluence: influence must be in [0, 1]");
    }
    impl_->solver.setInfluence(influence);
}

float Solver2D::getInfluence() const { return impl_->solver.getInfluence(); }

void Solver2D::clearTargets() { impl_->solver.clearTargets(); }

void Solver2D::addTarget(int boneId, float x, float y, float weight) {
    if (boneId < 0) {
        throw Exception("Solver2D.addTarget: boneId must be >= 0");
    }
    impl_->solver.addTarget(static_cast<unsigned>(boneId), ::ik::vec2{x, y}, weight);
}

int Solver2D::getTargetCount() const { return static_cast<int>(impl_->solver.targets().size()); }

bool Solver2D::solve(Skeleton2D *skeleton) {
    if (!skeleton) {
        throw Exception("Solver2D.solve: skeleton is null");
    }
    skeleton->impl_->ensureSorted();
    skeleton->impl_->ensureStateSize();
    return impl_->solver.solve(skeleton->impl_->sk, skeleton->impl_->state);
}

void Solver2D::step(Skeleton2D *skeleton, float dt) {
    if (!skeleton) {
        throw Exception("Solver2D.step: skeleton is null");
    }
    skeleton->impl_->ensureSorted();
    skeleton->impl_->ensureStateSize();
    impl_->solver.step(skeleton->impl_->sk, skeleton->impl_->state, dt);
}

void Solver2D::requireChain(Skeleton2D *skeleton, int rootBoneId, int tipBoneId) const {
    if (!skeleton) {
        throw Exception("Solver2D.solveChain: skeleton is null");
    }
    if (rootBoneId < 0 || tipBoneId < 0) {
        throw Exception("Solver2D.solveChain: bone ids must be >= 0");
    }
    const int count = skeleton->getBoneCount();
    if (rootBoneId >= count || tipBoneId >= count) {
        throw Exception("Solver2D.solveChain: bone id out of range");
    }
    if (rootBoneId == tipBoneId) {
        throw Exception("Solver2D.solveChain: root and tip must differ");
    }
    int  b     = tipBoneId;
    bool found = false;
    while (b >= 0) {
        if (b == rootBoneId) {
            found = true;
            break;
        }
        b = skeleton->getParent(b);
    }
    if (!found) {
        throw Exception("Solver2D.solveChain: tipBoneId is not a descendant of rootBoneId");
    }
}

bool Solver2D::solveChain(Skeleton2D *skeleton, int rootBoneId, int tipBoneId) {
    requireChain(skeleton, rootBoneId, tipBoneId);
    skeleton->impl_->ensureSorted();
    skeleton->impl_->ensureStateSize();
    return impl_->solver.solveChain(skeleton->impl_->sk, skeleton->impl_->state, static_cast<unsigned>(rootBoneId),
                                    static_cast<unsigned>(tipBoneId));
}

void Solver2D::stepChain(Skeleton2D *skeleton, int rootBoneId, int tipBoneId, float dt) {
    requireChain(skeleton, rootBoneId, tipBoneId);
    skeleton->impl_->ensureSorted();
    skeleton->impl_->ensureStateSize();
    impl_->solver.stepChain(skeleton->impl_->sk, skeleton->impl_->state, static_cast<unsigned>(rootBoneId),
                            static_cast<unsigned>(tipBoneId), dt);
}

int Solver2D::getTraceSize() const { return static_cast<int>(impl_->solver.traceSize()); }

void Solver2D::clearTrace() { impl_->solver.clearTrace(); }

// ---------------------------------------------------------------------------
// Solver3D
// ---------------------------------------------------------------------------

struct Solver3D::Impl {
    ::ik::solver3d solver;
    bool           keepTrace     = false;
    int            maxIterations = 16;
    float          tolerance     = 1e-3f;
    float          force         = 0.f;
};

Solver3D::Solver3D() : impl_(std::make_unique<Impl>()) {}
Solver3D::~Solver3D() = default;

void Solver3D::setKeepTrace(bool keep) {
    impl_->keepTrace = keep;
    impl_->solver.setKeepTrace(keep);
}

bool Solver3D::getKeepTrace() const { return impl_->keepTrace; }

void Solver3D::setMaxIterations(int iterations) {
    if (iterations < 0) {
        throw Exception("Solver3D.setMaxIterations: iterations must be >= 0");
    }
    impl_->maxIterations = iterations;
    impl_->solver.setMaxIterations(static_cast<unsigned>(iterations));
}

int Solver3D::getMaxIterations() const { return impl_->maxIterations; }

void Solver3D::setTolerance(float tol) {
    if (tol < 0.f) {
        throw Exception("Solver3D.setTolerance: tolerance must be >= 0");
    }
    impl_->tolerance = tol;
    impl_->solver.setTolerance(tol);
}

float Solver3D::getTolerance() const { return impl_->tolerance; }

void Solver3D::setForce(float force) {
    impl_->force = force;
    impl_->solver.setForce(force);
}

float Solver3D::getForce() const { return impl_->force; }

void Solver3D::setInfluence(float influence) {
    if (influence < 0.f || influence > 1.f) {
        throw Exception("Solver3D.setInfluence: influence must be in [0, 1]");
    }
    impl_->solver.setInfluence(influence);
}

float Solver3D::getInfluence() const { return impl_->solver.getInfluence(); }

void Solver3D::clearTargets() { impl_->solver.clearTargets(); }

void Solver3D::addTarget(int boneId, float x, float y, float z, float weight) {
    if (boneId < 0) {
        throw Exception("Solver3D.addTarget: boneId must be >= 0");
    }
    impl_->solver.addTarget(static_cast<unsigned>(boneId), ::ik::vec3{x, y, z}, weight);
}

int Solver3D::getTargetCount() const { return static_cast<int>(impl_->solver.targets().size()); }

bool Solver3D::solve(Skeleton3D *skeleton) {
    if (!skeleton) {
        throw Exception("Solver3D.solve: skeleton is null");
    }
    skeleton->impl_->ensureSorted();
    skeleton->impl_->ensureStateSize();
    return impl_->solver.solve(skeleton->impl_->sk, skeleton->impl_->state);
}

void Solver3D::step(Skeleton3D *skeleton, float dt) {
    if (!skeleton) {
        throw Exception("Solver3D.step: skeleton is null");
    }
    skeleton->impl_->ensureSorted();
    skeleton->impl_->ensureStateSize();
    impl_->solver.step(skeleton->impl_->sk, skeleton->impl_->state, dt);
}

void Solver3D::requireChain(Skeleton3D *skeleton, int rootBoneId, int tipBoneId) const {
    if (!skeleton) {
        throw Exception("Solver3D.solveChain: skeleton is null");
    }
    if (rootBoneId < 0 || tipBoneId < 0) {
        throw Exception("Solver3D.solveChain: bone ids must be >= 0");
    }
    const int count = skeleton->getBoneCount();
    if (rootBoneId >= count || tipBoneId >= count) {
        throw Exception("Solver3D.solveChain: bone id out of range");
    }
    if (rootBoneId == tipBoneId) {
        throw Exception("Solver3D.solveChain: root and tip must differ");
    }
    int  b     = tipBoneId;
    bool found = false;
    while (b >= 0) {
        if (b == rootBoneId) {
            found = true;
            break;
        }
        b = skeleton->getParent(b);
    }
    if (!found) {
        throw Exception("Solver3D.solveChain: tipBoneId is not a descendant of rootBoneId");
    }
}

bool Solver3D::solveChain(Skeleton3D *skeleton, int rootBoneId, int tipBoneId) {
    requireChain(skeleton, rootBoneId, tipBoneId);
    skeleton->impl_->ensureSorted();
    skeleton->impl_->ensureStateSize();
    return impl_->solver.solveChain(skeleton->impl_->sk, skeleton->impl_->state, static_cast<unsigned>(rootBoneId),
                                    static_cast<unsigned>(tipBoneId));
}

void Solver3D::stepChain(Skeleton3D *skeleton, int rootBoneId, int tipBoneId, float dt) {
    requireChain(skeleton, rootBoneId, tipBoneId);
    skeleton->impl_->ensureSorted();
    skeleton->impl_->ensureStateSize();
    impl_->solver.stepChain(skeleton->impl_->sk, skeleton->impl_->state, static_cast<unsigned>(rootBoneId),
                            static_cast<unsigned>(tipBoneId), dt);
}

void Solver3D::setPole(float x, float y, float z, float weight) { impl_->solver.setPole(::ik::vec3{x, y, z}, weight); }

void Solver3D::clearPole() { impl_->solver.clearPole(); }

bool Solver3D::hasPole() const { return impl_->solver.hasPole(); }

float Solver3D::getPoleWeight() const { return impl_->solver.getPoleWeight(); }

void Solver3D::setTipRotation(int boneId, float yaw, float pitch, float weight) {
    if (boneId < 0) {
        throw Exception("Solver3D.setTipRotation: boneId must be >= 0");
    }
    impl_->solver.setTipRotation(static_cast<unsigned>(boneId), ::ik::vec2{yaw, pitch}, weight);
}

void Solver3D::clearTipRotation() { impl_->solver.clearTipRotation(); }

float Solver3D::getTipRotationWeight() const { return impl_->solver.getTipRotationWeight(); }

int Solver3D::getTraceSize() const { return static_cast<int>(impl_->solver.traceSize()); }

void Solver3D::clearTrace() { impl_->solver.clearTrace(); }

}  // namespace eve::ik
