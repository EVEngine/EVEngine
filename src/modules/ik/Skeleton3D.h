#pragma once

#include "ik.hpp"

namespace eve::ik {

/**
 * @brief Script-facing 3D skeleton + pose state (ik::skeleton3d + ik::ecs3d).
 * Local angles are yaw/pitch in the parent bone frame.
 */
class Skeleton3D {
public:
    Skeleton3D();
    ~Skeleton3D() = default;

    Skeleton3D(const Skeleton3D &)            = delete;
    Skeleton3D &operator=(const Skeleton3D &) = delete;

    int createBone(int parentId, float length = 1.f);

    int getBoneCount() const;
    int getRootId() const { return 0; }
    int getParent(int boneId) const;
    int getChildCount(int boneId) const;
    int getChild(int boneId, int childIndex) const;

    float getLength(int boneId) const;
    void  setLength(int boneId, float length);

    void  setPosition(int boneId, float x, float y, float z);
    float getX(int boneId) const;
    float getY(int boneId) const;
    float getZ(int boneId) const;

    void  setOrientation(int boneId, float x, float y, float z);
    float getOrientationX(int boneId) const;
    float getOrientationY(int boneId) const;
    float getOrientationZ(int boneId) const;

    void  setRotation(int boneId, float yaw, float pitch);
    float getRotationYaw(int boneId) const;
    float getRotationPitch(int boneId) const;

    void setConstraints(int boneId, float minYaw, float minPitch, float maxYaw,
                        float maxPitch);
    void clearConstraints(int boneId);
    bool hasConstraints(int boneId) const;

    void initStraightPose(float rootX = 0.f, float rootY = 0.f, float rootZ = 0.f);
    void bind();
    void forwardKinematics();
    void updateRotations();

    float totalLengthTo(int boneId) const;

    ::ik::skeleton3d &native() { return sk_; }
    ::ik::ecs3d      &state() { return state_; }
    const ::ik::skeleton3d &native() const { return sk_; }
    const ::ik::ecs3d      &state() const { return state_; }

private:
    void ensureSorted();
    void ensureStateSize();
    ::ik::bone3d *boneAt(int boneId) const;
    void          requireBone(int boneId) const;

    ::ik::skeleton3d sk_;
    ::ik::ecs3d      state_;
};

}  // namespace eve::ik
