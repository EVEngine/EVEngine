#pragma once

class b2Fixture;

namespace eve::physics {

class Body;
class World;

class Fixture {
public:
    Fixture(World *world, Body *body, b2Fixture *fixture);
    ~Fixture();

    Fixture(const Fixture &)            = delete;
    Fixture &operator=(const Fixture &) = delete;

    void setSensor(bool sensor);
    bool isSensor() const;

    void  setFriction(float friction);
    float getFriction() const;

    void  setRestitution(float restitution);
    float getRestitution() const;

    void  setDensity(float density);
    float getDensity() const;

    Body *getBody() { return body_; }

    void destroy();

    b2Fixture *raw() { return fixture_; }
    const b2Fixture *raw() const { return fixture_; }

    void invalidate();

private:
    friend class World;
    friend class Body;

    World     *world_   = nullptr;
    Body      *body_    = nullptr;
    b2Fixture *fixture_ = nullptr;
};

}  // namespace eve::physics
