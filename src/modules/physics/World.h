#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

class b2World;
class b2Body;
class b2Fixture;
class b2Contact;

namespace eve::graphics {
class Graphics;
}

namespace eve::physics {

class Body;
class Fixture;
class ContactRelay;
class DebugDraw;

class World {
public:
    World(float gravityX, float gravityY, bool sleep, float meter);
    ~World();

    World(const World &)            = delete;
    World &operator=(const World &) = delete;

    void update(float dt);
    void updateFull(float dt, int velocityIterations, int positionIterations);

    void  setGravity(float gx, float gy);
    float getGravityX() const;
    float getGravityY() const;

    void  setMeter(float pixelsPerMeter);
    float getMeter() const { return meter_; }

    /** bodyType: "static" | "kinematic" | "dynamic". x/y in pixels. */
    Body *newBody(const std::string &bodyType, float x, float y);

    void destroyBody(Body *body);
    void destroy();

    /** Optional: draw fixture AABBs via Graphics::drawSolidRect. */
    void drawDebug(graphics::Graphics *gfx);

    float toMeters(float pixels) const;
    float toPixels(float meters) const;

    b2World *raw() { return world_; }
    const b2World *raw() const { return world_; }

    void onBeginContact(b2Contact *contact);
    void onEndContact(b2Contact *contact);

    void forgetBody(Body *body);
    void forgetFixture(Fixture *fixture);

    int nextBodyId();

private:
    friend class Body;
    friend class Fixture;

    b2World      *world_ = nullptr;
    ContactRelay *relay_ = nullptr;
    DebugDraw    *draw_  = nullptr;
    float         meter_ = 30.f;
    int           nextId_ = 1;
    bool          destroyed_ = false;

    std::unordered_set<Body *>    bodies_;
    std::unordered_set<Fixture *> fixtures_;
};

}  // namespace eve::physics
