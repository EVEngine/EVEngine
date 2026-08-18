#include "ik/Solver3D.h"

#include "common/Exception.h"

#include <algorithm>

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

void Solver3D::setInfluence(float influence) {
    if (influence < 0.f || influence > 1.f) {
        throw Exception("Solver3D.setInfluence: influence must be in [0, 1]");
    }
    influence_ = influence;
}

float Solver3D::getInfluence() const { return influence_; }

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
    ::ik::ecs3d &state = skeleton->state();
    std::vector<::ik::vec3> original;
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
    applyTipRotation(skeleton);
    return reached;
}

void Solver3D::step(Skeleton3D *skeleton, float dt) {
    if (!skeleton) {
        throw Exception("Solver3D.step: skeleton is null");
    }
    ::ik::ecs3d &state = skeleton->state();
    std::vector<::ik::vec3> original;
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
    applyTipRotation(skeleton);
}

bool Solver3D::solveChain(Skeleton3D *skeleton, int rootBoneId, int tipBoneId) {
    detail::ChainOptions<3> options;
    options.tolerance = tolerance_;
    options.max_iterations = static_cast<unsigned>(maxIterations_);
    options.force = force_;
    options.influence = influence_;
    options.use_pole = usePole_;
    options.pole = pole_;
    options.pole_weight = poleWeight_;
    return solveChainImpl(skeleton, rootBoneId, tipBoneId, options);
}

void Solver3D::stepChain(Skeleton3D *skeleton, int rootBoneId, int tipBoneId, float dt) {
    detail::ChainOptions<3> options;
    options.tolerance = tolerance_;
    options.max_iterations = 1;
    options.influence = influence_;
    options.use_pole = usePole_;
    options.pole = pole_;
    options.pole_weight = poleWeight_;
    float f = force_;
    if (f <= 0.f) {
        f = std::min(1.f, std::max(0.f, dt));
    } else {
        f = std::min(1.f, f * std::max(0.f, dt));
    }
    options.force = f;
    solveChainImpl(skeleton, rootBoneId, tipBoneId, options);
}

bool Solver3D::solveChainImpl(Skeleton3D *skeleton, int rootBoneId, int tipBoneId,
                              const detail::ChainOptions<3> &options) {
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
        throw Exception("Solver3D.solveChain: tipBoneId is not a descendant of rootBoneId");
    }
    const bool reached =
        detail::solveChain<3>(skeleton->native(), skeleton->state(),
                              static_cast<unsigned>(rootBoneId),
                              static_cast<unsigned>(tipBoneId), solver_.targets(), options);
    applyTipRotation(skeleton);
    return reached;
}

void Solver3D::setPole(float x, float y, float z, float weight) {
    pole_ = ::ik::vec3{x, y, z};
    poleWeight_ = std::min(1.f, std::max(0.f, weight));
    usePole_ = true;
}

void Solver3D::clearPole() {
    usePole_ = false;
    poleWeight_ = 1.f;
}

bool Solver3D::hasPole() const { return usePole_; }

float Solver3D::getPoleWeight() const { return usePole_ ? poleWeight_ : 0.f; }

void Solver3D::setTipRotation(int boneId, float yaw, float pitch, float weight) {
    if (boneId < 0) {
        throw Exception("Solver3D.setTipRotation: boneId must be >= 0");
    }
    tipBoneId_ = boneId;
    tipYaw_ = yaw;
    tipPitch_ = pitch;
    tipRotWeight_ = std::min(1.f, std::max(0.f, weight));
}

void Solver3D::clearTipRotation() {
    tipBoneId_ = -1;
    tipYaw_ = 0.f;
    tipPitch_ = 0.f;
    tipRotWeight_ = 0.f;
}

float Solver3D::getTipRotationWeight() const { return tipRotWeight_; }

void Solver3D::applyTipRotation(Skeleton3D *skeleton) {
    if (!skeleton || tipRotWeight_ <= 0.f || tipBoneId_ < 0) {
        return;
    }
    ::ik::skeleton3d &sk = skeleton->native();
    if (sk.bones().empty()) {
        sk.topologicalSort();
    }
    if (static_cast<size_t>(tipBoneId_) >= sk.bones().size()) {
        throw Exception("Solver3D.applyTipRotation: bone id out of range");
    }
    ::ik::bone3d *b = sk.bones()[static_cast<size_t>(tipBoneId_)];
    if (!b->parent) {
        return; // orienting the root would fight the pinned chain root
    }
    ::ik::ecs3d &state = skeleton->state();
    const float  w = tipRotWeight_;
    ::ik::vec2   rot = b->rotation(state);
    rot[0] = rot[0] + ::ik::wrap_angle(tipYaw_ - rot[0]) * w;
    rot[1] = rot[1] + ::ik::wrap_angle(tipPitch_ - rot[1]) * w;
    b->rotation(state) = rot;

    const ::ik::vec3 *gp = nullptr;
    ::ik::vec3        gp_pos;
    if (b->parent->parent) {
        gp_pos = b->parent->parent->position(state);
        gp = &gp_pos;
    }
    const ::ik::vec3 parent_fwd = ::ik::detail::parent_forward_of(
        b->parent->orientation(state), b->parent->position(state), gp);
    b->orientation(state) = ::ik::detail::direction_from_local_angles<3>(parent_fwd, rot);
}

int Solver3D::getTraceSize() const { return static_cast<int>(solver_.traceSize()); }

void Solver3D::clearTrace() { solver_.clearTrace(); }

}  // namespace eve::ik
