#pragma once
#include "common/Export.h"


#include "ik.hpp"

namespace eve::ik {

/**
 * @brief Script-facing 3D skeleton + pose state (ik::skeleton3d + ik::ecs3d).
 * Local angles are yaw/pitch in the parent bone frame.
 */
class EVENGINE_API_FOUNDATION Skeleton3D {
public:
    /** @brief Creates an empty 3D skeleton with a root bone. */
    Skeleton3D();
    /** @brief Releases skeleton and pose state. */
    ~Skeleton3D() = default;

    Skeleton3D(const Skeleton3D &)            = delete;
    Skeleton3D &operator=(const Skeleton3D &) = delete;

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
    void  setPosition(int boneId, float x, float y, float z);
    /** @brief World-space X of the bone (or component). */
    float getX(int boneId) const;
    /** @brief World-space Y of the bone (or component). */
    float getY(int boneId) const;
    /** @brief World-space Z of the bone (or component). */
    float getZ(int boneId) const;

    /** @brief Sets the bone forward orientation vector. */
    void  setOrientation(int boneId, float x, float y, float z);
    /** @brief Orientation vector X. */
    float getOrientationX(int boneId) const;
    /** @brief Orientation vector Y. */
    float getOrientationY(int boneId) const;
    /** @brief Orientation vector Z. */
    float getOrientationZ(int boneId) const;

    /** @brief Sets local hinge/yaw-pitch rotation (radians). */
    void  setRotation(int boneId, float yaw, float pitch);
    /** @brief Local yaw in radians. */
    float getRotationYaw(int boneId) const;
    /** @brief Local pitch in radians. */
    float getRotationPitch(int boneId) const;

    /** @brief Sets joint angle limits for the bone. */
    void setConstraints(int boneId, float minYaw, float minPitch, float maxYaw,
                        float maxPitch);
    /** @brief Removes joint limits from the bone. */
    void clearConstraints(int boneId);
    /** @brief True if the bone has joint limits. */
    bool hasConstraints(int boneId) const;

    /** @brief Initializes a straight chain pose from the root. */
    void initStraightPose(float rootX = 0.f, float rootY = 0.f, float rootZ = 0.f);
    /** @brief Binds the current pose as the rest pose. */
    void bind();
    /** @brief Updates world positions from local rotations. */
    void forwardKinematics();
    /** @brief Derives local rotations from world positions. */
    void updateRotations();

    /** @brief Sum of bone lengths from root to boneId inclusive. */
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
