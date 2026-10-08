#pragma once
#include "common/Export.h"


#include "animation/AnimMath.h"

#include <string>
#include <vector>

namespace eve::animation {

/**
 * @brief 3D bone hierarchy + bind-pose local TRS for skeletal animation.
 * Independent of ik::Skeleton3D (FABRIK). Script type: `AnimSkeleton`.
 */
class EVENGINE_API_WORLD AnimSkeleton {
public:
    /** @brief Anim skeleton. */
    AnimSkeleton() = default;
    /** @brief Anim skeleton. */
    ~AnimSkeleton() = default;

    AnimSkeleton(const AnimSkeleton &)            = delete;
    AnimSkeleton &operator=(const AnimSkeleton &) = delete;

    /**
     * @brief Append a bone. parentIndex = -1 for root.
     * @return bone index
     */
    int addBone(const std::string &name, int parentIndex = -1);

    /** @brief Returns the bone count. */
    int         getBoneCount() const { return static_cast<int>(bones_.size()); }
    /** @brief Returns the bone name. */
    std::string getBoneName(int boneIndex) const;
    /** @brief Finds bone. */
    int         findBone(const std::string &name) const;
    /** @brief Returns the parent. */
    int         getParent(int boneIndex) const;

    /** @brief Sets the bind position. */
    void setBindPosition(int boneIndex, float x, float y, float z);
    /** @brief Sets the bind rotation. */
    void setBindRotation(int boneIndex, float x, float y, float z, float w);
    /** @brief Sets the bind scale. */
    void setBindScale(int boneIndex, float x, float y, float z);

    /** @brief Returns the bind position x. */
    float getBindPositionX(int boneIndex) const;
    /** @brief Returns the bind position y. */
    float getBindPositionY(int boneIndex) const;
    /** @brief Returns the bind position z. */
    float getBindPositionZ(int boneIndex) const;
    /** @brief Returns the bind rotation x. */
    float getBindRotationX(int boneIndex) const;
    /** @brief Returns the bind rotation y. */
    float getBindRotationY(int boneIndex) const;
    /** @brief Returns the bind rotation z. */
    float getBindRotationZ(int boneIndex) const;
    /** @brief Returns the bind rotation w. */
    float getBindRotationW(int boneIndex) const;
    /** @brief Returns the bind scale x. */
    float getBindScaleX(int boneIndex) const;
    /** @brief Returns the bind scale y. */
    float getBindScaleY(int boneIndex) const;
    /** @brief Returns the bind scale z. */
    float getBindScaleZ(int boneIndex) const;

    /**
     * @brief Set the highest animation LOD at which this bone is sampled (0 = full-detail only).
     * Descendants can use lower limits than gameplay-critical roots and effectors.
     */
    void setBoneLodLimit(int boneIndex, int highestLod);
    /** @brief Highest LOD at which the bone is sampled; defaults to all LODs. */
    int getBoneLodLimit(int boneIndex) const;

    /** @brief Binds local. */
    const TransformTRS &bindLocal(int boneIndex) const;

    /** @brief Fill pose locals with bind pose. */
    void applyBindPose(class AnimPose *pose) const;

private:
    struct Bone {
        std::string  name;
        int          parent = -1;
        TransformTRS bind;
        int          lodLimit = 0x7fffffff;
    };

    void               requireBone(int boneIndex) const;
    std::vector<Bone>  bones_;
};

}  // namespace eve::animation
