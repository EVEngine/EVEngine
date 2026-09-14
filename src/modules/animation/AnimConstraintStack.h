#pragma once

#include <vector>

namespace eve::animation {

class AnimPose;
class AnimSkeleton;

/**
 * @brief Ordered main-thread post-process stack with aim, limb IK and foot planting.
 *
 * The skeleton is borrowed and must outlive the stack. Targets are owning values.
 */
class AnimConstraintStack {
public:
    /** @brief Construct for a non-null borrowed skeleton. */
    explicit AnimConstraintStack(AnimSkeleton* skeleton);
    AnimConstraintStack(const AnimConstraintStack&)            = default;
    AnimConstraintStack& operator=(const AnimConstraintStack&) = default;

    /** @brief Rebind the borrowed skeleton; changing it clears old index constraints. */
    void setSkeleton(AnimSkeleton* skeleton);
    /**
     * @brief Return the borrowed skeleton, or null after explicit unbinding.
     * @ownership Borrowed; ownership remains with the animation source.
     * @lifetime Valid until setSkeleton(), skeleton destruction, or stack destruction.
     */
    AnimSkeleton* getSkeleton() const { return skeleton_; }

    /** @brief Remove all constraints. */
    void clear() { constraints_.clear(); }
    /** @brief Return constraint count. */
    int getCount() const { return static_cast<int>(constraints_.size()); }
    /** @brief Append a weighted bone aim constraint. */
    void addAim(int bone, float targetX, float targetY, float targetZ, float weight = 1.f);
    /** @brief Append a weighted two-bone IK constraint. */
    void addTwoBoneIK(int root, int mid, int tip, float targetX, float targetY, float targetZ,
                      float weight = 1.f);
    /** @brief Plant a foot at groundY + soleOffset and orient its local +Z toward the ground normal. */
    void addFootIK(int hip, int knee, int foot, float groundY, float normalX = 0.f, float normalY = 1.f,
                   float normalZ = 0.f, float soleOffset = 0.f, float weight = 1.f);
    /** @brief Apply constraints in insertion order and refresh world transforms. Main-thread only. */
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
