#pragma once

#include <vector>

namespace eve::animation {

class AnimPose;
class AnimSkeleton;

/** @brief Ordered post-process constraint stack with aim, limb IK and foot planting. */
class AnimConstraintStack {
public:
    explicit AnimConstraintStack(AnimSkeleton* skeleton);

    void clear() { constraints_.clear(); }
    int getCount() const { return static_cast<int>(constraints_.size()); }
    void addAim(int bone, float targetX, float targetY, float targetZ, float weight = 1.f);
    void addTwoBoneIK(int root, int mid, int tip, float targetX, float targetY, float targetZ,
                      float weight = 1.f);
    /** @brief Plant a foot at groundY + soleOffset and orient its local +Z toward the ground normal. */
    void addFootIK(int hip, int knee, int foot, float groundY, float normalX = 0.f, float normalY = 1.f,
                   float normalZ = 0.f, float soleOffset = 0.f, float weight = 1.f);
    /** @brief Apply constraints in insertion order and refresh world transforms. */
    void apply(AnimPose* pose) const;

private:
    enum class Type { Aim, TwoBone, Foot };
    struct Constraint {
        Type type = Type::Aim;
        int a = -1, b = -1, c = -1;
        float x = 0.f, y = 0.f, z = 0.f;
        float nx = 0.f, ny = 1.f, nz = 0.f;
        float offset = 0.f, weight = 1.f;
    };
    AnimSkeleton* skeleton_ = nullptr;
    std::vector<Constraint> constraints_;
};

}  // namespace eve::animation
