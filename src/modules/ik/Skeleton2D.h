#pragma once
#include "common/Export.h"


#include "ik.hpp"

namespace eve::ik {

/**
 * @brief Script-facing 2D skeleton + pose state (ik::skeleton2d + ik::ecs2d).
 * Bone indices are stable after each createBone / topological refresh.
 * Root bone is always id 0.
 */
class EVENGINE_API_FOUNDATION Skeleton2D {
public:
    /** @brief Creates an empty 2D skeleton with a root bone. */
    Skeleton2D();
    /** @brief Releases skeleton and pose state. */
    ~Skeleton2D() = default;

    Skeleton2D(const Skeleton2D &)            = delete;
    Skeleton2D &operator=(const Skeleton2D &) = delete;

    /** @brief Create a child bone under parentId; returns new bone id. */
    int createBone(int parentId, float length = 1.f);

    /** @brief Number of bones currently in the skeleton. */
    int getBoneCount() const;
    /** @brief Root bone id (always 0). */
    int getRootId() const { return 0; }
    /** @brief Parent bone id, or -1 for the root. */
    int getParent(int boneId) const;
    /** @brief Number of direct children of boneId. */
    int getChildCount(int boneId) const;
    /** @brief Child bone id at childIndex under boneId. */
    int getChild(int boneId, int childIndex) const;

    /** @brief Rest length of the bone. */
    float getLength(int boneId) const;
    /** @brief Sets the rest length of the bone. */
    void  setLength(int boneId, float length);

    /** @brief Sets the world-space bone position. */
    void  setPosition(int boneId, float x, float y);
    /** @brief World-space X of the bone (or component). */
    float getX(int boneId) const;
    /** @brief World-space Y of the bone (or component). */
    float getY(int boneId) const;

    /** @brief Sets the bone forward orientation vector. */
    void  setOrientation(int boneId, float x, float y);
    /** @brief Orientation vector X. */
    float getOrientationX(int boneId) const;
    /** @brief Orientation vector Y. */
    float getOrientationY(int boneId) const;

    /** @brief Local hinge angle (radians) relative to parent forward. */
    void  setRotation(int boneId, float angle);
    /** @brief Local hinge angle in radians. */
    float getRotation(int boneId) const;

    /** @brief Joint limits for the single 2D hinge DOF. */
    void setConstraints(int boneId, float minAngle, float maxAngle);
    /** @brief Removes joint limits from the bone. */
    void clearConstraints(int boneId);
    /** @brief True if the bone has joint limits. */
    bool hasConstraints(int boneId) const;

    /** @brief Initializes a straight chain pose from the root. */
    void initStraightPose(float rootX = 0.f, float rootY = 0.f);
    /** @brief Binds the current pose as the rest pose. */
    void bind();
    /** @brief Updates world positions from local rotations. */
    void forwardKinematics();
    /** @brief Derives local rotations from world positions. */
    void updateRotations();

    /** @brief Sum of bone lengths from root to boneId inclusive. */
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
