#pragma once

#include <string>
#include <unordered_set>
#include <vector>

#include <box3d/id.h>

namespace eve::physics {

class Body3D;
class Shape3D;

/**
 * @brief Box3D rigid-body world. Script coordinates are meters (Box3D native),
 * unlike 2D World which uses pixels + Physics.setMeter.
 */
class World3D {
public:
    World3D(float gravityX, float gravityY, float gravityZ, bool sleep);
    ~World3D();

    World3D(const World3D &)            = delete;
    World3D &operator=(const World3D &) = delete;

    /** @brief Steps the simulation by dt seconds (default substep count). */
    void update(float dt);
    /** @brief Steps with an explicit substep count. */
    void updateFull(float dt, int subStepCount);

    /** @brief Gravity in m/s². */
    void  setGravity(float gx, float gy, float gz);
    float getGravityX() const;
    float getGravityY() const;
    float getGravityZ() const;

    /** @brief bodyType: "static" | "kinematic" | "dynamic". Position in meters. */
    Body3D *newBody(const std::string &bodyType, float x, float y, float z);

    /** @brief Destroys a body (null is ignored). */
    void destroyBody(Body3D *body);
    /** @brief Destroys the world and invalidates all wrappers. */
    void destroy();

    /**
     * @brief Closest raycast from (x1,y1,z1) to (x2,y2,z2) in meters.
     * Returns hit body id, or -1. Read hit details via getRayHit*.
     */
    int rayCast(float x1, float y1, float z1, float x2, float y2, float z2);
    bool  hasRayHit() const { return rayHitBodyId_ >= 0; }
    int   getRayHitBodyId() const { return rayHitBodyId_; }
    float getRayHitX() const { return rayHitX_; }
    float getRayHitY() const { return rayHitY_; }
    float getRayHitZ() const { return rayHitZ_; }
    float getRayHitNormalX() const { return rayHitNormalX_; }
    float getRayHitNormalY() const { return rayHitNormalY_; }
    float getRayHitNormalZ() const { return rayHitNormalZ_; }
    float getRayHitFraction() const { return rayHitFraction_; }

    /**
     * @brief Query shapes overlapping an AABB in meters (min/max corners).
     * Returns match count; read ids with getQueryBodyId(i).
     */
    int queryAABB(float minX, float minY, float minZ, float maxX, float maxY, float maxZ);
    /** @brief Number of bodies hit by the last queryAABB(). */
    int getQueryCount() const { return static_cast<int>(queryBodyIds_.size()); }
    /** @brief Body id of the i-th query hit. */
    int getQueryBodyId(int index) const;

    /** @brief True while the underlying Box3D world is alive. */
    bool      isValid() const;
    /** @brief Raw Box3D world id. */
    b3WorldId raw() const { return worldId_; }

    /** @brief Internal: wrapper teardown bookkeeping. */
    void forgetBody(Body3D *body);
    void forgetShape(Shape3D *shape);

    /** @brief Internal: next stable body id. */
    int nextBodyId();

    /** @brief Internal: collects Box3D contact events into the event buffers. */
    void emitContactEvents();

private:
    friend class Body3D;
    friend class Shape3D;

    b3WorldId worldId_{};
    bool      destroyed_ = false;
    int       nextId_    = 1;

    std::unordered_set<Body3D *>  bodies_;
    std::unordered_set<Shape3D *> shapes_;

    int   rayHitBodyId_   = -1;
    float rayHitX_        = 0.f;
    float rayHitY_        = 0.f;
    float rayHitZ_        = 0.f;
    float rayHitNormalX_  = 0.f;
    float rayHitNormalY_  = 0.f;
    float rayHitNormalZ_  = 0.f;
    float rayHitFraction_ = 0.f;

    std::vector<int> queryBodyIds_;
};

}  // namespace eve::physics
