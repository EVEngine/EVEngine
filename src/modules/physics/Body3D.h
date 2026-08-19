#pragma once

#include <string>

#include <box3d/id.h>

namespace eve::physics {

class World3D;
class Shape3D;

class Body3D {
public:
    Body3D(World3D *world, b3BodyId bodyId, int id);
    ~Body3D();

    Body3D(const Body3D &)            = delete;
    Body3D &operator=(const Body3D &) = delete;

    int getId() const { return id_; }

    void  setPosition(float x, float y, float z);
    float getX() const;
    float getY() const;
    float getZ() const;

    /** Orientation as quaternion (x, y, z, w). */
    void  setRotation(float qx, float qy, float qz, float qw);
    float getRotX() const;
    float getRotY() const;
    float getRotZ() const;
    float getRotW() const;

    void  setLinearVelocity(float vx, float vy, float vz);
    float getLinearVelocityX() const;
    float getLinearVelocityY() const;
    float getLinearVelocityZ() const;

    void  setAngularVelocity(float wx, float wy, float wz);
    float getAngularVelocityX() const;
    float getAngularVelocityY() const;
    float getAngularVelocityZ() const;

    void applyForce(float fx, float fy, float fz);
    void applyForceAt(float fx, float fy, float fz, float x, float y, float z);
    void applyLinearImpulse(float ix, float iy, float iz);
    void applyAngularImpulse(float ix, float iy, float iz);

    void        setType(const std::string &bodyType);
    std::string getType() const;

    /** Lock all angular axes (Box3D motion locks). */
    void setFixedRotation(bool fixed);
    bool isFixedRotation() const;

    void setActive(bool active);
    bool isActive() const;

    void setBullet(bool bullet);
    bool isBullet() const;

    void setAwake(bool awake);
    bool isAwake() const;

    /**
     * Box extents are full width/height/depth in meters (same convention as
     * Body.newRectangleFixture). Density kg/m³.
     */
    Shape3D *newBoxShape(float width, float height, float depth, float density = 1.f,
                         float friction = 0.2f, float restitution = 0.f);
    Shape3D *newSphereShape(float radius, float density = 1.f, float friction = 0.2f,
                            float restitution = 0.f);
    /** Capsule along local Y; height is distance between hemisphere centers. */
    Shape3D *newCapsuleShape(float height, float radius, float density = 1.f,
                             float friction = 0.2f, float restitution = 0.f);

    void destroy();

    World3D  *getWorld() { return world_; }
    b3BodyId  raw() const { return bodyId_; }
    bool      isValid() const;

    void invalidate();

private:
    friend class World3D;
    friend class Shape3D;

    World3D *world_  = nullptr;
    b3BodyId bodyId_{};
    int      id_ = 0;
};

}  // namespace eve::physics
