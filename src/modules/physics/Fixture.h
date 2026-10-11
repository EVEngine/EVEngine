#pragma once
#include "common/Export.h"


#include <string>

class b2Fixture;

namespace eve::physics {

class Body;
class World;

/**
 * @brief 2D fixture: a shape attached to a Body with material + filter settings.
 * Also carries a string tag used by contact events.
 */
class EVENGINE_API_WORLD Fixture {
public:
    /** @brief Internal: wraps a Box2D fixture (use Body::new*Fixture). */
    Fixture(World *world, Body *body, b2Fixture *fixture);
    /** @brief Fixture. */
    ~Fixture();

    Fixture(const Fixture &)            = delete;
    Fixture &operator=(const Fixture &) = delete;

    /** @brief Sensor fixtures report contacts but never collide. */
    void setSensor(bool sensor);
    /** @brief True when sensor. */
    bool isSensor() const;

    /** @brief Material properties. */
    void  setFriction(float friction);
    /** @brief Returns the friction. */
    float getFriction() const;

    /** @brief Sets the restitution. */
    void  setRestitution(float restitution);
    /** @brief Returns the restitution. */
    float getRestitution() const;

    /** @brief Sets the density. */
    void  setDensity(float density);
    /** @brief Returns the density. */
    float getDensity() const;

    /** @brief Arbitrary string tag surfaced in begin/end contact events. */
    void setTag(const std::string &tag) { tag_ = tag; }
    /** @brief Returns the tag. */
    const std::string &getTag() const { return tag_; }

    /** @brief Collision filtering (Box2D category/mask bits, group index). */
    void setCategoryBits(int bits);
    /** @brief Returns the category bits. */
    int getCategoryBits() const;
    /** @brief Sets the mask bits. */
    void setMaskBits(int bits);
    /** @brief Returns the mask bits. */
    int getMaskBits() const;
    /** @brief Sets the group index. */
    void setGroupIndex(int index);
    /** @brief Returns the group index. */
    int getGroupIndex() const;

    /** @brief Id of the owning body. */
    int getBodyId() const;

    /**
     * @brief Returns the owning body, or null after invalidation.
     * @return Borrowed nullable Body pointer owned by the physics world.
     * @ownership Fixture does not own the body; callers must not delete it.
     * @lifetime Valid until body/world destruction; use a PhysicsBodyHandle across frames.
     * @thread Call on the owning physics thread.
     * @reentrancy The accessor invokes no callbacks and is invalid across world mutation.
     */
    Body *getBody() { return body_; }

    /** @brief Pixel-space point-in-fixture test (uses World meter). */
    bool testPoint(float x, float y) const;

    /** @brief Destroys the fixture inside its world. */
    void destroy();

    /**
     * @brief Exposes the underlying Box2D fixture for tightly-scoped backend integration.
     * @return Borrowed nullable backend pointer; callers must not delete or retain it across steps.
     * @ownership The Box2D world owns the fixture; Fixture is only its wrapper.
     * @lifetime Valid until Fixture::destroy(), body/world destruction, or invalidate().
     * @thread Call only on the owning physics thread.
     * @reentrancy Does not invoke callbacks and is invalid across world mutation.
     */
    b2Fixture *raw() { return fixture_; }
    /**
     * @brief Exposes the underlying Box2D fixture for read-only backend integration.
     * @return Borrowed nullable backend pointer.
     * @ownership The Box2D world owns the fixture; callers must not delete it.
     * @lifetime Valid until Fixture::destroy(), body/world destruction, or invalidate().
     * @thread Call only on the owning physics thread.
     * @reentrancy Does not invoke callbacks and is invalid across world mutation.
     */
    const b2Fixture *raw() const { return fixture_; }

    /** @brief Internal: marks the wrapper invalid after destruction. */
    void invalidate();

private:
    friend struct WorldSnapshotAccess;
    friend class World;
    friend class Body;

    World     *world_   = nullptr;
    Body      *body_    = nullptr;
    b2Fixture *fixture_ = nullptr;
    std::string tag_;
};

}  // namespace eve::physics
