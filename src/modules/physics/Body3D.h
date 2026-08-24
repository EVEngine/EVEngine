#pragma once

#include <string>

#include <box3d/id.h>

namespace eve::physics {

class World3D;
class Shape3D;

/**
 * @brief 3D rigid body (Box3D) in meter-space coordinates (+Y up by convention).
 * Owned by a World3D; create shapes with newBoxShape/newSphereShape/newCapsuleShape.
 */
class Body3D {
public:
    /** @brief Internal: wraps a Box3D body (use World3D::newBody). */
    Body3D(World3D *world, b3BodyId bodyId, int id);
    ~Body3D();

    Body3D(const Body3D &)            = delete;
    Body3D &operator=(const Body3D &) = delete;

    /** @brief Stable id used by collision events. */
    int getId() const { return id_; }

    /** @brief Position in meters. */
    void  setPosition(float x, float y, float z);
    float getX() const;
    float getY() const;
    float getZ() const;

    /** @brief Orientation as quaternion (x, y, z, w). */
    void  setRotation(float qx, float qy, float qz, float qw);
    float getRotX() const;
    float getRotY() const;
    float getRotZ() const;
    float getRotW() const;

    /** @brief Linear velocity in m/s. */
    void  setLinearVelocity(float vx, float vy, float vz);
    float getLinearVelocityX() const;
    float getLinearVelocityY() const;
    float getLinearVelocityZ() const;

    /** @brief Body mass in kg. */
    float getMass() const;

    /** @brief Angular velocity in rad/s. */
    void  setAngularVelocity(float wx, float wy, float wz);
    float getAngularVelocityX() const;
    float getAngularVelocityY() const;
    float getAngularVelocityZ() const;

    /** @brief Force applied at the center of mass. */
    void applyForce(float fx, float fy, float fz);
    /** @brief Force applied at a world position. */
    void applyForceAt(float fx, float fy, float fz, float x, float y, float z);
    /** @brief Instantaneous linear impulse. */
    void applyLinearImpulse(float ix, float iy, float iz);
    /** @brief Instantaneous angular impulse. */
    void applyAngularImpulse(float ix, float iy, float iz);

    /** @brief "static" | "kinematic" | "dynamic". */
    void        setType(const std::string &bodyType);
    std::string getType() const;

    /** @brief Lock all angular axes (Box3D motion locks). */
    void setFixedRotation(bool fixed);
    bool isFixedRotation() const;

    /** @brief Disables/enables the body and its shapes. */
    void setActive(bool active);
    bool isActive() const;

    /** @brief CCD bullet mode. */
    void setBullet(bool bullet);
    bool isBullet() const;

    /** @brief Wakes / sleeps the body manually. */
    void setAwake(bool awake);
    bool isAwake() const;

    /**
     * @brief Box extents are full width/height/depth in meters (same convention as
     * Body.newRectangleFixture). Density kg/m³.
     */
    Shape3D *newBoxShape(float width, float height, float depth, float density = 1.f,
                         float friction = 0.2f, float restitution = 0.f);
    Shape3D *newSphereShape(float radius, float density = 1.f, float friction = 0.2f,
                            float restitution = 0.f);
    /** @brief Capsule along local Y; height is distance between hemisphere centers. */
    Shape3D *newCapsuleShape(float height, float radius, float density = 1.f,
                             float friction = 0.2f, float restitution = 0.f);

    /** @brief Destroys the body inside its world. */
    void destroy();

    /** @brief Owning world / raw Box3D body id. */
    World3D  *getWorld() { return world_; }
    b3BodyId  raw() const { return bodyId_; }
    /** @brief True while the underlying Box3D body is still alive. */
    bool      isValid() const;

    /** @brief Internal: marks the wrapper invalid after world destruction. */
    void invalidate();

private:
    friend class World3D;
    friend class Shape3D;

    World3D *world_  = nullptr;
    b3BodyId bodyId_{};
    int      id_ = 0;
};

}  // namespace eve::physics
