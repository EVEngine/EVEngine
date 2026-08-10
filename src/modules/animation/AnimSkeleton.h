#pragma once

#include "animation/AnimMath.h"

#include <string>
#include <vector>

namespace eve::animation {

/**
 * 3D bone hierarchy + bind-pose local TRS for skeletal animation.
 * Independent of ik::Skeleton3D (FABRIK). Script type: `AnimSkeleton`.
 */
class AnimSkeleton {
public:
    AnimSkeleton() = default;
    ~AnimSkeleton() = default;

    AnimSkeleton(const AnimSkeleton &)            = delete;
    AnimSkeleton &operator=(const AnimSkeleton &) = delete;

    /**
     * Append a bone. parentIndex = -1 for root.
     * @return bone index
     */
    int addBone(const std::string &name, int parentIndex = -1);

    int         getBoneCount() const { return static_cast<int>(bones_.size()); }
    std::string getBoneName(int boneIndex) const;
    int         findBone(const std::string &name) const;
    int         getParent(int boneIndex) const;

    void setBindPosition(int boneIndex, float x, float y, float z);
    void setBindRotation(int boneIndex, float x, float y, float z, float w);
    void setBindScale(int boneIndex, float x, float y, float z);

    float getBindPositionX(int boneIndex) const;
    float getBindPositionY(int boneIndex) const;
    float getBindPositionZ(int boneIndex) const;
    float getBindRotationX(int boneIndex) const;
    float getBindRotationY(int boneIndex) const;
    float getBindRotationZ(int boneIndex) const;
    float getBindRotationW(int boneIndex) const;
    float getBindScaleX(int boneIndex) const;
    float getBindScaleY(int boneIndex) const;
    float getBindScaleZ(int boneIndex) const;

    const TransformTRS &bindLocal(int boneIndex) const;

    /** Fill pose locals with bind pose. */
    void applyBindPose(class AnimPose *pose) const;

private:
    struct Bone {
        std::string  name;
        int          parent = -1;
        TransformTRS bind;
    };

    void               requireBone(int boneIndex) const;
    std::vector<Bone>  bones_;
};

}  // namespace eve::animation
