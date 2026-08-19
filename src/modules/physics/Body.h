#pragma once

#include <string>

class b2Body;

namespace eve::physics {

class World;
class Fixture;

class Body {
public:
    Body(World *world, b2Body *body, int id);
    ~Body();

    Body(const Body &)            = delete;
    Body &operator=(const Body &) = delete;

    int getId() const { return id_; }

    void  setPosition(float x, float y);
    float getX() const;
    float getY() const;

    void  setAngle(float radians);
    float getAngle() const;

    void  setLinearVelocity(float vx, float vy);
    float getLinearVelocityX() const;
    float getLinearVelocityY() const;
    float getLinearSpeed() const;
    float getMass() const;
    float getWorldCenterX() const;
    float getWorldCenterY() const;

    void  setAngularVelocity(float omega);
    float getAngularVelocity() const;

    void applyForce(float fx, float fy);
    void applyForceAt(float fx, float fy, float x, float y);
    void applyLinearImpulse(float ix, float iy);
    void applyAngularImpulse(float impulse);

    void        setType(const std::string &bodyType);
    std::string getType() const;

    void setFixedRotation(bool fixed);
    bool isFixedRotation() const;

    void setActive(bool active);
    bool isActive() const;

    void setBullet(bool bullet);
    bool isBullet() const;

    void setAwake(bool awake);
    bool isAwake() const;

    /** width/height in pixels; density kg/m² (Box2D units). */
    Fixture *newRectangleFixture(float width, float height, float density = 1.f,
                                 float friction = 0.2f, float restitution = 0.f);
    /** Rectangle fixture with a local pixel-space offset from the body origin. */
    Fixture *newRectangleFixtureAt(float width, float height, float offsetX, float offsetY,
                                   float density = 1.f, float friction = 0.2f,
                                   float restitution = 0.f);
    Fixture *newCircleFixture(float radius, float density = 1.f, float friction = 0.2f,
                              float restitution = 0.f);

    void destroy();

    World  *getWorld() { return world_; }
    b2Body *raw() { return body_; }
    const b2Body *raw() const { return body_; }

    void invalidate();

private:
    friend class World;
    friend class Fixture;

    World  *world_ = nullptr;
    b2Body *body_  = nullptr;
    int     id_    = 0;
};

}  // namespace eve::physics
