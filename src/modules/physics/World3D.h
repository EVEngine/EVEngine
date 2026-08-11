#pragma once

#include <string>
#include <unordered_set>
#include <vector>

#include <box3d/id.h>

namespace eve::physics {

class Body3D;
class Shape3D;

/**
 * Box3D rigid-body world. Script coordinates are meters (Box3D native),
 * unlike 2D World which uses pixels + Physics.setMeter.
 */
class World3D {
public:
    World3D(float gravityX, float gravityY, float gravityZ, bool sleep);
    ~World3D();

    World3D(const World3D &)            = delete;
    World3D &operator=(const World3D &) = delete;

    void update(float dt);
    void updateFull(float dt, int subStepCount);

    void  setGravity(float gx, float gy, float gz);
    float getGravityX() const;
    float getGravityY() const;
    float getGravityZ() const;

    /** bodyType: "static" | "kinematic" | "dynamic". Position in meters. */
    Body3D *newBody(const std::string &bodyType, float x, float y, float z);

    void destroyBody(Body3D *body);
    void destroy();

    /**
     * Closest raycast from (x1,y1,z1) to (x2,y2,z2) in meters.
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
     * Query shapes overlapping an AABB in meters (min/max corners).
     * Returns match count; read ids with getQueryBodyId(i).
     */
    int queryAABB(float minX, float minY, float minZ, float maxX, float maxY, float maxZ);
    int getQueryCount() const { return static_cast<int>(queryBodyIds_.size()); }
    int getQueryBodyId(int index) const;

    bool      isValid() const;
    b3WorldId raw() const { return worldId_; }

    void forgetBody(Body3D *body);
    void forgetShape(Shape3D *shape);

    int nextBodyId();

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
