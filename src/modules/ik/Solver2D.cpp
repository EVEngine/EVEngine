#include "ik/Solver2D.h"

#include "common/Exception.h"

#include <algorithm>

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

void Solver2D::setInfluence(float influence) {
    if (influence < 0.f || influence > 1.f) {
        throw Exception("Solver2D.setInfluence: influence must be in [0, 1]");
    }
    influence_ = influence;
}

float Solver2D::getInfluence() const { return influence_; }

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
    ::ik::ecs2d &state = skeleton->state();
    std::vector<::ik::vec2> original;
    if (influence_ < 1.f) {
        original = state.position_;
    }
    const bool reached = solver_.solve(skeleton->native(), state);
    if (influence_ < 1.f) {
        const float t = influence_;
        for (size_t i = 0; i < state.position_.size(); ++i) {
            state.position_[i] = original[i] + (state.position_[i] - original[i]) * t;
        }
        skeleton->native().update_rotations(state);
    }
    return reached;
}

void Solver2D::step(Skeleton2D *skeleton, float dt) {
    if (!skeleton) {
        throw Exception("Solver2D.step: skeleton is null");
    }
    ::ik::ecs2d &state = skeleton->state();
    std::vector<::ik::vec2> original;
    if (influence_ < 1.f) {
        original = state.position_;
    }
    solver_.step(skeleton->native(), skeleton->state(), dt);
    if (influence_ < 1.f) {
        const float t = influence_;
        for (size_t i = 0; i < state.position_.size(); ++i) {
            state.position_[i] = original[i] + (state.position_[i] - original[i]) * t;
        }
        skeleton->native().update_rotations(state);
    }
}

bool Solver2D::solveChain(Skeleton2D *skeleton, int rootBoneId, int tipBoneId) {
    detail::ChainOptions<2> options;
    options.tolerance = tolerance_;
    options.max_iterations = static_cast<unsigned>(maxIterations_);
    options.force = force_;
    options.influence = influence_;
    return solveChainImpl(skeleton, rootBoneId, tipBoneId, options);
}

void Solver2D::stepChain(Skeleton2D *skeleton, int rootBoneId, int tipBoneId, float dt) {
    detail::ChainOptions<2> options;
    options.tolerance = tolerance_;
    options.max_iterations = 1;
    options.influence = influence_;
    float f = force_;
    if (f <= 0.f) {
        f = std::min(1.f, std::max(0.f, dt));
    } else {
        f = std::min(1.f, f * std::max(0.f, dt));
    }
    options.force = f;
    solveChainImpl(skeleton, rootBoneId, tipBoneId, options);
}

bool Solver2D::solveChainImpl(Skeleton2D *skeleton, int rootBoneId, int tipBoneId,
                              const detail::ChainOptions<2> &options) {
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
    int b = tipBoneId;
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
    return detail::solveChain<2>(skeleton->native(), skeleton->state(),
                                 static_cast<unsigned>(rootBoneId),
                                 static_cast<unsigned>(tipBoneId), solver_.targets(), options);
}

int Solver2D::getTraceSize() const { return static_cast<int>(solver_.traceSize()); }

void Solver2D::clearTrace() { solver_.clearTrace(); }

}  // namespace eve::ik
