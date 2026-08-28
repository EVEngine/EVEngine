#pragma once

#include "physics/PhysicsHandles.h"

#include <string>
#include <vector>

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
    Body(World *world, b2Body *body, int id, PhysicsBodyHandle runtimeHandle);
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

    /**
     * @brief Creates a rectangle fixture in pixel-space units.
     * @return Borrowed nullable fixture owned by this body/world.
     * @ownership The Box2D world owns the fixture; callers must destroy it through this API.
     * @lifetime Valid until fixture/body/world destruction; do not retain across structural mutation.
     * @thread Call on the owning physics thread.
     * @reentrancy Creation does not invoke user callbacks; do not re-enter world mutation while using the result.
     */
    Fixture *newRectangleFixture(float width, float height, float density = 1.f,
                                 float friction = 0.2f, float restitution = 0.f);
    /**
     * @brief Creates an offset rectangle fixture in pixel-space units.
     * @return Borrowed nullable fixture owned by this body/world.
     * @ownership The Box2D world owns the fixture; callers must destroy it through this API.
     * @lifetime Valid until fixture/body/world destruction; do not retain across structural mutation.
     * @thread Call on the owning physics thread.
     * @reentrancy Creation does not invoke user callbacks; do not re-enter world mutation while using the result.
     */
    Fixture *newRectangleFixtureAt(float width, float height, float offsetX, float offsetY,
                                   float density = 1.f, float friction = 0.2f,
                                   float restitution = 0.f);
    /**
     * @brief Creates a circular fixture in pixel-space units.
     * @return Borrowed nullable fixture owned by this body/world.
     * @ownership The Box2D world owns the fixture; callers must destroy it through this API.
     * @lifetime Valid until fixture/body/world destruction; do not retain across structural mutation.
     * @thread Call on the owning physics thread.
     * @reentrancy Creation does not invoke user callbacks; do not re-enter world mutation while using the result.
     */
    Fixture *newCircleFixture(float radius, float density = 1.f, float friction = 0.2f,
                              float restitution = 0.f);
    /**
     * @brief Create a bounded convex polygon fixture from packed local pixel-space XY vertices.
     * @param vertices Three to eight finite convex vertices as x0,y0,x1,y1,... .
     * @param density Mass density.
     * @param friction Surface friction.
     * @param restitution Surface restitution.
     * @return Borrowed fixture owned by the world.
     * @throws eve::Exception when the body is dead or vertices are malformed/degenerate.
     */
    Fixture *newPolygonFixture(const std::vector<float> &vertices, float density = 1.f,
                               float friction = 0.2f, float restitution = 0.f);
    /**
     * @brief Create an open chain or closed loop from packed local pixel-space XY vertices.
     * @param vertices At least two finite vertices for a chain or three for a loop.
     * @param loop Whether to connect the final vertex back to the first.
     * @param friction Surface friction.
     * @param restitution Surface restitution.
     * @return Borrowed fixture owned by the world.
     * @throws eve::Exception when the body is dead or vertices are malformed/degenerate.
     */
    Fixture *newChainFixture(const std::vector<float> &vertices, bool loop = false,
                             float friction = 0.2f, float restitution = 0.f);

    /** @brief Destroys the body inside its world. */
    void destroy();

    /** @brief True while both the wrapper and its owning Box2D world are live. */
    [[nodiscard]] bool isValid() const noexcept { return world_ != nullptr && body_ != nullptr; }

    /**
     * @brief Returns the process-local solver handle used by PhysicsLink.
     * @return Generation-qualified handle; it becomes stale on destruction.
     */
    [[nodiscard]] PhysicsBodyHandle runtimeHandle() const noexcept { return runtimeHandle_; }

    /**
     * @brief Returns the owning world, or null after invalidation.
     * @return Borrowed nullable World pointer; ownership remains with the world registry.
     * @ownership Body does not own the world and callers must not delete it.
     * @lifetime Valid until world destruction; use PhysicsWorldHandle for cross-frame identity.
     * @thread Call on the owning physics thread.
     * @reentrancy The accessor invokes no callbacks and is not valid across world mutation.
     */
    World  *getWorld() { return world_; }
    /**
     * @brief Returns the owning world as a read-only borrowed pointer.
     * @return Borrowed nullable World pointer.
     * @ownership Body does not own the world; callers must not delete it.
     * @lifetime Valid until world destruction; use PhysicsWorldHandle for cross-frame identity.
     * @thread Call on the owning physics thread.
     * @reentrancy The accessor invokes no callbacks and is not valid across world mutation.
     */
    const World *getWorld() const { return world_; }
    /**
     * @brief Exposes the underlying Box2D body for tightly-scoped backend integration.
     * @return Borrowed nullable backend pointer; never transfer or store it across simulation steps.
     * @ownership Box2D World owns the body; Body is only its wrapper.
     * @lifetime Valid until Body::destroy(), world destruction, or invalidate().
     * @thread Call only on the owning physics thread.
     * @reentrancy Does not invoke callbacks; callers must not mutate the world re-entrantly.
     */
    b2Body *raw() { return body_; }
    /**
     * @brief Exposes the underlying Box2D body for read-only backend integration.
     * @return Borrowed nullable backend pointer.
     * @ownership Box2D World owns the body; callers must not delete it.
     * @lifetime Valid until Body::destroy(), world destruction, or invalidate().
     * @thread Call only on the owning physics thread.
     * @reentrancy Does not invoke callbacks and is invalid across world mutation.
     */
    const b2Body *raw() const { return body_; }

    /** @brief Internal: marks the wrapper invalid after world destruction. */
    void invalidate();

private:
    friend class World;
    friend class Fixture;

    World  *world_ = nullptr;
    b2Body *body_  = nullptr;
    int     id_    = 0;
    PhysicsBodyHandle runtimeHandle_ = PhysicsBodyHandle::invalid();
};

}  // namespace eve::physics
