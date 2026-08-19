#pragma once

#include <string>

class b2Body;

namespace eve::physics {

class World;
class Fixture;

/**
 * @brief 2D rigid body (Box2D) in pixel-space coordinates.
 * Owned by a World; create shapes with newRectangleFixture/newCircleFixture.
 */
class Body {
public:
    /** @brief Internal: wraps a Box2D body (use World::newBody). */
    Body(World *world, b2Body *body, int id);
    ~Body();

    Body(const Body &)            = delete;
    Body &operator=(const Body &) = delete;

    /** @brief Stable id used by contact/impact events. */
    int getId() const { return id_; }

    /** @brief Position in pixels. */
    void  setPosition(float x, float y);
    float getX() const;
    float getY() const;

    /** @brief Rotation in radians. */
    void  setAngle(float radians);
    float getAngle() const;

    /** @brief Linear velocity in pixels/s. */
    void  setLinearVelocity(float vx, float vy);
    float getLinearVelocityX() const;
    float getLinearVelocityY() const;
    /** @brief Magnitude of the linear velocity. */
    float getLinearSpeed() const;
    /** @brief Mass in kilograms (Box2D units). */
    float getMass() const;
    /** @brief World center of mass in pixels. */
    float getWorldCenterX() const;
    float getWorldCenterY() const;

    /** @brief Angular velocity in radians/s. */
    void  setAngularVelocity(float omega);
    float getAngularVelocity() const;

    /** @brief Force (pixels/s² * kg) applied at the center of mass. */
    void applyForce(float fx, float fy);
    /** @brief Force applied at a world pixel position. */
    void applyForceAt(float fx, float fy, float x, float y);
    /** @brief Instantaneous linear impulse. */
    void applyLinearImpulse(float ix, float iy);
    /** @brief Instantaneous angular impulse. */
    void applyAngularImpulse(float impulse);

    /** @brief "static" | "kinematic" | "dynamic". */
    void        setType(const std::string &bodyType);
    std::string getType() const;

    /** @brief Locks rotation so the body cannot spin. */
    void setFixedRotation(bool fixed);
    bool isFixedRotation() const;

    /** @brief Disables/enables the body and its fixtures. */
    void setActive(bool active);
    bool isActive() const;

    /** @brief CCD bullet mode (recommended for fast small bodies). */
    void setBullet(bool bullet);
    bool isBullet() const;

    /** @brief Wakes / sleeps the body manually. */
    void setAwake(bool awake);
    bool isAwake() const;

    /** @brief width/height in pixels; density kg/m² (Box2D units). */
    Fixture *newRectangleFixture(float width, float height, float density = 1.f,
                                 float friction = 0.2f, float restitution = 0.f);
    /** @brief Rectangle fixture with a local pixel-space offset from the body origin. */
    Fixture *newRectangleFixtureAt(float width, float height, float offsetX, float offsetY,
                                   float density = 1.f, float friction = 0.2f,
                                   float restitution = 0.f);
    Fixture *newCircleFixture(float radius, float density = 1.f, float friction = 0.2f,
                              float restitution = 0.f);

    /** @brief Destroys the body inside its world. */
    void destroy();

    /** @brief Owning world / raw Box2D body. */
    World  *getWorld() { return world_; }
    b2Body *raw() { return body_; }
    const b2Body *raw() const { return body_; }

    /** @brief Internal: marks the wrapper invalid after world destruction. */
    void invalidate();

private:
    friend class World;
    friend class Fixture;

    World  *world_ = nullptr;
    b2Body *body_  = nullptr;
    int     id_    = 0;
};

}  // namespace eve::physics
