#pragma once

#include "ik.hpp"

namespace eve::ik {

/**
 * Script-facing 2D skeleton + pose state (ik::skeleton2d + ik::ecs2d).
 * Bone indices are stable after each createBone / topological refresh.
 * Root bone is always id 0.
 */
class Skeleton2D {
public:
    Skeleton2D();
    ~Skeleton2D() = default;

    Skeleton2D(const Skeleton2D &)            = delete;
    Skeleton2D &operator=(const Skeleton2D &) = delete;

    /** Create a child bone under parentId; returns new bone id. */
    int createBone(int parentId, float length = 1.f);

    int getBoneCount() const;
    int getRootId() const { return 0; }
    int getParent(int boneId) const;
    int getChildCount(int boneId) const;
    int getChild(int boneId, int childIndex) const;

    float getLength(int boneId) const;
    void  setLength(int boneId, float length);

    void  setPosition(int boneId, float x, float y);
    float getX(int boneId) const;
    float getY(int boneId) const;

    void  setOrientation(int boneId, float x, float y);
    float getOrientationX(int boneId) const;
    float getOrientationY(int boneId) const;

    /** Local hinge angle (radians) relative to parent forward. */
    void  setRotation(int boneId, float angle);
    float getRotation(int boneId) const;

    /** Joint limits for the single 2D hinge DOF. */
    void setConstraints(int boneId, float minAngle, float maxAngle);
    void clearConstraints(int boneId);
    bool hasConstraints(int boneId) const;

    void initStraightPose(float rootX = 0.f, float rootY = 0.f);
    void bind();
    void forwardKinematics();
    void updateRotations();

    float totalLengthTo(int boneId) const;

    ::ik::skeleton2d &native() { return sk_; }
    ::ik::ecs2d      &state() { return state_; }
    const ::ik::skeleton2d &native() const { return sk_; }
    const ::ik::ecs2d      &state() const { return state_; }

private:
    void ensureSorted();
    void ensureStateSize();
    ::ik::bone2d *boneAt(int boneId) const;
    void          requireBone(int boneId) const;

    ::ik::skeleton2d sk_;
    ::ik::ecs2d      state_;
};

}  // namespace eve::ik
