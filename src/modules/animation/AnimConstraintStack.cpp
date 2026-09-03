#include "animation/AnimConstraintStack.h"

#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "common/Exception.h"

#include <cmath>

namespace eve::animation {

AnimConstraintStack::AnimConstraintStack(AnimSkeleton* skeleton) : skeleton_(skeleton) {
    if (!skeleton_) throw Exception("AnimConstraintStack: skeleton is null");
}

void AnimConstraintStack::setSkeleton(AnimSkeleton* skeleton) {
    if (skeleton_ == skeleton) return;
    skeleton_ = skeleton;
    clear();
}

void AnimConstraintStack::addAim(int bone, float targetX, float targetY, float targetZ, float weight) {
    constraints_.push_back({Type::Aim, bone, -1, -1, targetX, targetY, targetZ, 0.f, 1.f, 0.f, 0.f,
                            clampf(weight, 0.f, 1.f)});
}

void AnimConstraintStack::addTwoBoneIK(int root, int mid, int tip, float targetX, float targetY, float targetZ,
                                       float weight) {
    constraints_.push_back({Type::TwoBone, root, mid, tip, targetX, targetY, targetZ, 0.f, 1.f, 0.f, 0.f,
                            clampf(weight, 0.f, 1.f)});
}

void AnimConstraintStack::addFootIK(int hip, int knee, int foot, float groundY, float normalX, float normalY,
                                    float normalZ, float soleOffset, float weight) {
    const float length = std::sqrt(normalX * normalX + normalY * normalY + normalZ * normalZ);
    if (length < 1e-6f) throw Exception("AnimConstraintStack.addFootIK: ground normal is zero");
    constraints_.push_back({Type::Foot, hip, knee, foot, 0.f, groundY, 0.f, normalX / length, normalY / length,
                            normalZ / length, soleOffset, clampf(weight, 0.f, 1.f)});
}

void AnimConstraintStack::apply(AnimPose* pose) const {
    if (!pose) throw Exception("AnimConstraintStack.apply: pose is null");
    if (!skeleton_) throw Exception("AnimConstraintStack.apply: skeleton is null");
    pose->computeWorld(skeleton_);
    for (const Constraint& constraint : constraints_) {
        if (constraint.type == Type::Aim) {
            pose->aimBone(skeleton_, constraint.a, constraint.x, constraint.y, constraint.z, constraint.weight);
        } else if (constraint.type == Type::TwoBone) {
            pose->solveTwoBoneIK(skeleton_, constraint.a, constraint.b, constraint.c, constraint.x, constraint.y,
                                 constraint.z, constraint.weight);
        } else {
            pose->computeWorld(skeleton_);
            const TransformTRS& foot = pose->world(constraint.c);
            const float targetY = constraint.y + constraint.offset;
            pose->solveTwoBoneIK(skeleton_, constraint.a, constraint.b, constraint.c, foot.px, targetY, foot.pz,
                                 constraint.weight);
            pose->computeWorld(skeleton_);
            const TransformTRS& planted = pose->world(constraint.c);
            pose->aimBone(skeleton_, constraint.c, planted.px + constraint.nx, planted.py + constraint.ny,
                          planted.pz + constraint.nz, constraint.weight);
        }
    }
    pose->computeWorld(skeleton_);
}

}  // namespace eve::animation
