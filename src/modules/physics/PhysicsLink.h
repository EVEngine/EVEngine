#pragma once

/**
 * @file PhysicsLink.h
 * @brief Typed composition link from a gameplay entity to a physics body.
 *
 * A gameplay entity owns this value as a component; it does not inherit from
 * Body, Body3D, or any physics type. The handles are process-local and must be
 * rebuilt from persistent identity during restore or hot reload.
 */

#include "common/Result.h"
#include "physics/PhysicsHandles.h"

namespace eve::physics {

class Body;
class Body3D;
class Joint3D;
class Shape3D;
class World;
class World3D;

/**
 * @brief Generation-qualified relationship between one world and one body.
 *
 * `isValid()` only checks the encoded handle values. `resolve()` checks owner
 * identity and occupancy, so it is the required stale-handle check after body
 * destruction, world destruction, restore, or hot reload.
 */
struct PhysicsLink {
    /** @brief Process-local owning-world handle. */
    PhysicsWorldHandle world = PhysicsWorldHandle::invalid();
    /** @brief Process-local solver-body handle. */
    PhysicsBodyHandle body = PhysicsBodyHandle::invalid();

    /** @brief Whether both encoded handle values are non-invalid. */
    [[nodiscard]] bool isValid() const noexcept {
        return world.isValid() && body.isValid();
    }

    /**
     * @brief Validates and constructs a link from explicit runtime handles.
     * @return A link candidate; ownership is verified by resolve().
     */
    [[nodiscard("check the link construction outcome")]]
    static eve::Result<PhysicsLink> attach(PhysicsWorldHandle world,
                                            PhysicsBodyHandle body);

    /** @brief Creates a link from a live 2D body. */
    [[nodiscard("check the link construction outcome")]]
    static eve::Result<PhysicsLink> fromBody(const Body& body);
    /** @brief Creates a link from a live 3D body. */
    [[nodiscard("check the link construction outcome")]]
    static eve::Result<PhysicsLink> fromBody(const Body3D& body);

    /** @brief Resolves this link against a 2D world or returns StaleHandle. */
    [[nodiscard("check stale-link resolution")]]
    eve::Result<Body*> resolve(World& world) const;
    /** @brief Resolves this link against a 3D world or returns StaleHandle. */
    [[nodiscard("check stale-link resolution")]]
    eve::Result<Body3D*> resolve(World3D& world) const;
};

/**
 * @brief Generation-qualified relationship between a World3D and one shape.
 *
 * This link is stale when the shape is destroyed or its owning world is gone;
 * it never stores a raw pointer and is not persistent data.
 */
struct PhysicsShapeLink {
    PhysicsWorldHandle world = PhysicsWorldHandle::invalid();
    PhysicsShapeHandle shape = PhysicsShapeHandle::invalid();

    /** @brief Whether both encoded handle values are non-invalid. */
    [[nodiscard]] bool isValid() const noexcept {
        return world.isValid() && shape.isValid();
    }
    /** @brief Constructs a shape link candidate; ownership is checked by resolve(). */
    [[nodiscard("check the shape-link construction outcome")]]
    static eve::Result<PhysicsShapeLink> attach(PhysicsWorldHandle world,
                                                PhysicsShapeHandle shape);
    /** @brief Creates a link from a live shape. */
    [[nodiscard("check the shape-link construction outcome")]]
    static eve::Result<PhysicsShapeLink> fromShape(const Shape3D& shape);
    /** @brief Resolves a live shape or returns StaleHandle. */
    [[nodiscard("check stale shape-link resolution")]]
    eve::Result<Shape3D*> resolve(World3D& world) const;
};

/**
 * @brief Generation-qualified relationship between a World3D and one joint.
 *
 * Joint links are invalidated before either attached body or the world is
 * destroyed, so both destruction orders resolve as StaleHandle.
 */
struct PhysicsJointLink {
    PhysicsWorldHandle world = PhysicsWorldHandle::invalid();
    PhysicsJointHandle joint = PhysicsJointHandle::invalid();

    /** @brief Whether both encoded handle values are non-invalid. */
    [[nodiscard]] bool isValid() const noexcept {
        return world.isValid() && joint.isValid();
    }
    /** @brief Constructs a joint link candidate; ownership is checked by resolve(). */
    [[nodiscard("check the joint-link construction outcome")]]
    static eve::Result<PhysicsJointLink> attach(PhysicsWorldHandle world,
                                                PhysicsJointHandle joint);
    /** @brief Creates a link from a live joint. */
    [[nodiscard("check the joint-link construction outcome")]]
    static eve::Result<PhysicsJointLink> fromJoint(const Joint3D& joint);
    /** @brief Resolves a live joint or returns StaleHandle. */
    [[nodiscard("check stale joint-link resolution")]]
    eve::Result<Joint3D*> resolve(World3D& world) const;
};

}  // namespace eve::physics
