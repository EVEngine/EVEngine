#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

namespace eve::physics {

/**
 * @brief Regular 3D signed-distance grid for terrain and voxel collision queries.
 * Negative samples are solid, positive samples are empty. Samples are stored at
 * grid vertices and queried with trilinear interpolation.
 */
class DistanceField3D {
public:
    DistanceField3D(int width, int height, int depth, float cellSize, float originX,
                    float originY, float originZ, float outsideDistance);

    /** @brief Replace every sample in x-fastest, then y, then z order. */
    void setDistances(const std::vector<float> &distances);
    /** @brief Set every grid sample to the same finite distance. */
    void fill(float distance);
    /**
     * @brief Atomically replace a grid subregion in x-fastest, then y, then z order.
     * @param x First grid-vertex X index.
     * @param y First grid-vertex Y index.
     * @param z First grid-vertex Z index.
     * @param width Number of vertices along X; must be positive and in bounds.
     * @param height Number of vertices along Y; must be positive and in bounds.
     * @param depth Number of vertices along Z; must be positive and in bounds.
     * @param distances Exactly width*height*depth finite samples.
     */
    void setDistanceRegion(int x, int y, int z, int width, int height, int depth,
                           const std::vector<float> &distances);
    /** @brief Fill an in-bounds grid subregion with one finite signed distance. */
    void fillRegion(int x, int y, int z, int width, int height, int depth, float distance);
    /** @brief Set a signed-distance sample at a grid vertex. */
    void setDistance(int x, int y, int z, float distance);
    /** @brief Read a grid vertex; out-of-range vertices return the outside distance. */
    float getDistance(int x, int y, int z) const;
    /** @brief Trilinearly sample the field in world coordinates. */
    float sample(float x, float y, float z) const;

    /** @brief Number of grid vertices along the X axis. */
    int getWidth() const { return width_; }
    /** @brief Number of grid vertices along the Y axis. */
    int getHeight() const { return height_; }
    /** @brief Number of grid vertices along the Z axis. */
    int getDepth() const { return depth_; }
    /** @brief Total number of signed-distance samples. */
    int getSampleCount() const { return static_cast<int>(distances_.size()); }
    /** @brief World-space distance between adjacent grid vertices. */
    float getCellSize() const { return cellSize_; }
    /** @brief World-space X coordinate of grid vertex (0,0,0). */
    float getOriginX() const { return originX_; }
    /** @brief World-space Y coordinate of grid vertex (0,0,0). */
    float getOriginY() const { return originY_; }
    /** @brief World-space Z coordinate of grid vertex (0,0,0). */
    float getOriginZ() const { return originZ_; }
    /** @brief Distance returned for world samples outside the grid. */
    float getOutsideDistance() const { return outsideDistance_; }
    /** @brief Monotonic wrapping counter incremented once after each successful mutation. */
    unsigned int getRevision() const { return revision_; }

    /** @brief Estimate the normalized outward field gradient at a world position. */
    void sampleNormal(float x, float y, float z);
    /** @brief X component of the most recently sampled collision normal. */
    float getNormalX() const { return normalX_; }
    /** @brief Y component of the most recently sampled collision normal. */
    float getNormalY() const { return normalY_; }
    /** @brief Z component of the most recently sampled collision normal. */
    float getNormalZ() const { return normalZ_; }

    /** @brief Test a sphere against the solid (distance <= radius). */
    bool checkSphere(float x, float y, float z, float radius);
    /** @brief Test a capsule defined by two sphere centers against the solid. */
    bool checkCapsule(float ax, float ay, float az, float bx, float by, float bz,
                      float radius);
    /**
     * @brief Sweep a sphere through the field and cache the earliest contact.
     * @param x Initial sphere-center X coordinate.
     * @param y Initial sphere-center Y coordinate.
     * @param z Initial sphere-center Z coordinate.
     * @param radius Sphere radius.
     * @param deltaX Sweep displacement along X.
     * @param deltaY Sweep displacement along Y.
     * @param deltaZ Sweep displacement along Z.
     * @return True when the swept sphere starts in or reaches the solid.
     */
    bool castSphere(float x, float y, float z, float radius, float deltaX, float deltaY,
                    float deltaZ);
    /**
     * @brief Sweep a capsule through the field and cache the earliest contact.
     * @return True when the swept capsule starts in or reaches the solid.
     */
    bool castCapsule(float ax, float ay, float az, float bx, float by, float bz, float radius,
                     float deltaX, float deltaY, float deltaZ);
    /**
     * @brief Resolve a capsule displacement against the field with sliding and depenetration.
     * The capsule coordinates are not stored; apply getMoverDeltaX/Y/Z to both endpoints.
     * @return True when solid geometry constrained or depenetrated the capsule.
     */
    bool moveCapsule(float ax, float ay, float az, float bx, float by, float bz, float radius,
                     float deltaX, float deltaY, float deltaZ);
    /** @brief Set the normalized reference-up direction used for ground classification. */
    void setMoverUp(float x, float y, float z);
    /** @brief Set the maximum walkable slope angle in degrees, clamped to [0, 89.9]. */
    void setMoverSlopeLimit(float degrees);
    /** @brief Set the non-negative separation retained from surfaces by moveCapsule. */
    void setMoverSkinWidth(float width);
    /** @brief Current mover separation from collision surfaces. */
    float getMoverSkinWidth() const { return moverSkinWidth_; }
    /** @brief Set the non-negative distance used to snap onto walkable ground after movement. */
    void setMoverGroundSnap(float distance);
    /** @brief Current post-move ground-snap distance. */
    float getMoverGroundSnap() const { return moverGroundSnap_; }
    /** @brief Set the maximum non-negative ledge height automatically climbed by moveCapsule. */
    void setMoverStepHeight(float height);
    /** @brief Current automatic step height; zero disables step climbing. */
    float getMoverStepHeight() const { return moverStepHeight_; }
    /** @brief Signed clearance from the last check (negative means penetration). */
    float getCollisionDistance() const { return collisionDistance_; }
    /** @brief X coordinate of the closest sampled point from the last collision check. */
    float getCollisionX() const { return collisionX_; }
    /** @brief Y coordinate of the closest sampled point from the last collision check. */
    float getCollisionY() const { return collisionY_; }
    /** @brief Z coordinate of the closest sampled point from the last collision check. */
    float getCollisionZ() const { return collisionZ_; }
    /** @brief X coordinate of the projected signed-distance-field surface point. */
    float getSurfaceX() const { return surfaceX_; }
    /** @brief Y coordinate of the projected signed-distance-field surface point. */
    float getSurfaceY() const { return surfaceY_; }
    /** @brief Z coordinate of the projected signed-distance-field surface point. */
    float getSurfaceZ() const { return surfaceZ_; }
    /** @brief X coordinate on the sphere or capsule surface along the contact normal. */
    float getShapeContactX() const { return shapeContactX_; }
    /** @brief Y coordinate on the sphere or capsule surface along the contact normal. */
    float getShapeContactY() const { return shapeContactY_; }
    /** @brief Z coordinate on the sphere or capsule surface along the contact normal. */
    float getShapeContactZ() const { return shapeContactZ_; }
    /** @brief Non-negative overlap depth from the most recent overlap, cast, or move query. */
    float getPenetrationDepth() const { return std::max(0.f, -collisionDistance_); }
    /** @brief Fraction in [0,1] of the requested displacement at the last cast hit. */
    float getCastFraction() const { return castFraction_; }
    /** @brief World-space travel distance at the last cast hit. */
    float getCastDistance() const { return castDistance_; }
    /** @brief Whether the most recent cast began overlapping the solid. */
    bool didCastStartInside() const { return castStartedInside_; }
    /** @brief Resolved X displacement from the most recent moveCapsule call. */
    float getMoverDeltaX() const { return moverDeltaX_; }
    /** @brief Resolved Y displacement from the most recent moveCapsule call. */
    float getMoverDeltaY() const { return moverDeltaY_; }
    /** @brief Resolved Z displacement from the most recent moveCapsule call. */
    float getMoverDeltaZ() const { return moverDeltaZ_; }
    /** @brief X component of the most recent blocking or recovery normal. */
    float getMoverNormalX() const { return moverNormalX_; }
    /** @brief Y component of the most recent blocking or recovery normal. */
    float getMoverNormalY() const { return moverNormalY_; }
    /** @brief Z component of the most recent blocking or recovery normal. */
    float getMoverNormalZ() const { return moverNormalZ_; }
    /** @brief Number of solver passes used by the most recent moveCapsule call. */
    int getMoverIterations() const { return moverIterations_; }
    /** @brief Whether the latest move encountered a normal within the walkable slope limit. */
    bool isMoverGrounded() const { return moverGrounded_; }
    /** @brief Greatest dot product between mover Up and a normal from the latest move. */
    float getMoverGroundDot() const { return moverGroundDot_; }
    /** @brief Whether the latest move encountered a non-walkable contact normal. */
    bool didMoverHitWall() const { return moverHitWall_; }

private:
    bool validIndex(int x, int y, int z) const;
    bool validRegion(int x, int y, int z, int width, int height, int depth) const;
    size_t index(int x, int y, int z) const;
    void setCollisionResult(float x, float y, float z, float radius);
    float capsuleClearance(float ax, float ay, float az, float bx, float by, float bz,
                           float radius, float &closestX, float &closestY, float &closestZ) const;
    void updateContactPoints(float x, float y, float z, float radius);
    void setCastResult(float fraction, float travelLength, float x, float y, float z,
                       float clearance, float radius, bool startedInside);

    int width_, height_, depth_;
    float cellSize_, originX_, originY_, originZ_, outsideDistance_;
    std::vector<float> distances_;
    float normalX_ = 0.f, normalY_ = 1.f, normalZ_ = 0.f;
    float collisionDistance_ = 0.f, collisionX_ = 0.f, collisionY_ = 0.f,
          collisionZ_ = 0.f;
    float surfaceX_ = 0.f, surfaceY_ = 0.f, surfaceZ_ = 0.f;
    float shapeContactX_ = 0.f, shapeContactY_ = 0.f, shapeContactZ_ = 0.f;
    float castFraction_ = 1.f, castDistance_ = 0.f;
    bool castStartedInside_ = false;
    float moverDeltaX_ = 0.f, moverDeltaY_ = 0.f, moverDeltaZ_ = 0.f;
    float moverNormalX_ = 0.f, moverNormalY_ = 1.f, moverNormalZ_ = 0.f;
    int moverIterations_ = 0;
    float moverUpX_ = 0.f, moverUpY_ = 1.f, moverUpZ_ = 0.f;
    float moverSlopeCos_ = 0.6427876f;
    float moverSkinWidth_ = 0.f;
    float moverGroundSnap_ = 0.f;
    float moverStepHeight_ = 0.f;
    float moverGroundDot_ = -1.f;
    bool moverGrounded_ = false;
    bool moverHitWall_ = false;
    bool moverStepping_ = false;
    unsigned int revision_ = 0;
};

}  // namespace eve::physics
