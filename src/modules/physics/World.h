#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <vector>

class b2World;
class b2Body;
class b2Fixture;
class b2Contact;
class b2Manifold;
struct b2ContactImpulse;

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
    struct ContactEvent {
        int bodyAId = 0;
        int bodyBId = 0;
        std::string fixtureATag;
        std::string fixtureBTag;
    };

    struct ImpactEvent : ContactEvent {
        float pointX = 0.f;
        float pointY = 0.f;
        float normalX = 0.f;
        float normalY = 0.f;
        float relativeNormalSpeed = 0.f;
        float normalImpulse = 0.f;
        float tangentImpulse = 0.f;
    };
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

    /**
     * Closest raycast in pixel space from (x1,y1) to (x2,y2).
     * Returns hit body id, or -1. Read hit details via getRayHit*.
     */
    int rayCast(float x1, float y1, float x2, float y2);
    bool  hasRayHit() const { return rayHitBodyId_ >= 0; }
    int   getRayHitBodyId() const { return rayHitBodyId_; }
    float getRayHitX() const { return rayHitX_; }
    float getRayHitY() const { return rayHitY_; }
    float getRayHitNormalX() const { return rayHitNormalX_; }
    float getRayHitNormalY() const { return rayHitNormalY_; }
    /** Fraction along the segment [0,1] of the closest hit. */
    float getRayHitFraction() const { return rayHitFraction_; }

    /**
     * Query fixtures overlapping an axis-aligned box in pixel space (x,y,w,h).
     * Returns match count; read ids with getQueryBodyId(i).
     */
    int queryAABB(float x, float y, float w, float h);
    int getQueryCount() const { return static_cast<int>(queryBodyIds_.size()); }
    int getQueryBodyId(int index) const;

    int getBeginContactCount() const { return int(beginContacts_.size()); }
    int getBeginContactBodyAId(int index) const;
    int getBeginContactBodyBId(int index) const;
    std::string getBeginContactFixtureATag(int index) const;
    std::string getBeginContactFixtureBTag(int index) const;
    int getEndContactCount() const { return int(endContacts_.size()); }
    int getEndContactBodyAId(int index) const;
    int getEndContactBodyBId(int index) const;
    std::string getEndContactFixtureATag(int index) const;
    std::string getEndContactFixtureBTag(int index) const;

    int getImpactCount() const { return int(impacts_.size()); }
    int getImpactBodyAId(int index) const;
    int getImpactBodyBId(int index) const;
    std::string getImpactFixtureATag(int index) const;
    std::string getImpactFixtureBTag(int index) const;
    float getImpactPointX(int index) const;
    float getImpactPointY(int index) const;
    float getImpactNormalX(int index) const;
    float getImpactNormalY(int index) const;
    float getImpactRelativeNormalSpeed(int index) const;
    float getImpactNormalImpulse(int index) const;
    float getImpactTangentImpulse(int index) const;
    void clearContactEvents();

    float toMeters(float pixels) const;
    float toPixels(float meters) const;

    b2World *raw() { return world_; }
    const b2World *raw() const { return world_; }

    void onBeginContact(b2Contact *contact);
    void onEndContact(b2Contact *contact);
    void onPreSolve(b2Contact *contact, const b2Manifold *oldManifold);
    void onPostSolve(b2Contact *contact, const b2ContactImpulse *impulse);

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

    int   rayHitBodyId_   = -1;
    float rayHitX_        = 0.f;
    float rayHitY_        = 0.f;
    float rayHitNormalX_  = 0.f;
    float rayHitNormalY_  = 0.f;
    float rayHitFraction_ = 0.f;

    std::vector<int> queryBodyIds_;

    struct PreSolveData {
        float pointX = 0.f;
        float pointY = 0.f;
        float normalX = 0.f;
        float normalY = 0.f;
        float relativeNormalSpeed = 0.f;
    };
    std::unordered_map<b2Contact *, PreSolveData> preSolve_;
    std::vector<ContactEvent> beginContacts_;
    std::vector<ContactEvent> endContacts_;
    std::vector<ImpactEvent> impacts_;
};

}  // namespace eve::physics
