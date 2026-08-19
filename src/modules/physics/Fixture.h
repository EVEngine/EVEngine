#pragma once

#include <string>

class b2Fixture;

namespace eve::physics {

class Body;
class World;

/**
 * @brief 2D fixture: a shape attached to a Body with material + filter settings.
 * Also carries a string tag used by contact events.
 */
class Fixture {
public:
    /** @brief Internal: wraps a Box2D fixture (use Body::new*Fixture). */
    Fixture(World *world, Body *body, b2Fixture *fixture);
    ~Fixture();

    Fixture(const Fixture &)            = delete;
    Fixture &operator=(const Fixture &) = delete;

    /** @brief Sensor fixtures report contacts but never collide. */
    void setSensor(bool sensor);
    bool isSensor() const;

    /** @brief Material properties. */
    void  setFriction(float friction);
    float getFriction() const;

    void  setRestitution(float restitution);
    float getRestitution() const;

    void  setDensity(float density);
    float getDensity() const;

    /** @brief Arbitrary string tag surfaced in begin/end contact events. */
    void setTag(const std::string &tag) { tag_ = tag; }
    const std::string &getTag() const { return tag_; }

    /** @brief Collision filtering (Box2D category/mask bits, group index). */
    void setCategoryBits(int bits);
    int getCategoryBits() const;
    void setMaskBits(int bits);
    int getMaskBits() const;
    void setGroupIndex(int index);
    int getGroupIndex() const;

    /** @brief Id of the owning body. */
    int getBodyId() const;

    /** @brief Owning body. */
    Body *getBody() { return body_; }

    /** @brief Pixel-space point-in-fixture test (uses World meter). */
    bool testPoint(float x, float y) const;

    /** @brief Destroys the fixture inside its world. */
    void destroy();

    /** @brief Raw Box2D fixture. */
    b2Fixture *raw() { return fixture_; }
    const b2Fixture *raw() const { return fixture_; }

    /** @brief Internal: marks the wrapper invalid after destruction. */
    void invalidate();

private:
    friend class World;
    friend class Body;

    World     *world_   = nullptr;
    Body      *body_    = nullptr;
    b2Fixture *fixture_ = nullptr;
    std::string tag_;
};

}  // namespace eve::physics
