#pragma once

#include <string>

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

    void setTag(const std::string &tag) { tag_ = tag; }
    const std::string &getTag() const { return tag_; }

    void setCategoryBits(int bits);
    int getCategoryBits() const;
    void setMaskBits(int bits);
    int getMaskBits() const;
    void setGroupIndex(int index);
    int getGroupIndex() const;

    int getBodyId() const;

    Body *getBody() { return body_; }

    /** Pixel-space point-in-fixture test (uses World meter). */
    bool testPoint(float x, float y) const;

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
    std::string tag_;
};

}  // namespace eve::physics
