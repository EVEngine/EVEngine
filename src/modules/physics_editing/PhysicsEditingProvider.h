#pragma once

#include "editing/EditingExtension.h"
#include "physics_editing/PhysicsTarget.h"

#include <memory>
#include <string>

namespace eve::physics_editing {

/** @brief Open capability for constructing collider authoring targets without editor type switches. */
class IPhysicsEditingFactory {
public:
    virtual ~IPhysicsEditingFactory() = default;
    /**
     * @brief Create an independently owned collider publishing target.
     * @param id Stable target identity.
     * @param dimensions Collider dimensionality accepted by the target.
     * @param sink Borrowed runtime sink that must outlive the returned target; null disables publication.
     * @return Newly allocated target owned by the caller.
     * @thread Main-thread only.
     */
    [[nodiscard]] virtual std::unique_ptr<PhysicsColliderPublishingTarget> createCollider(
        std::string id, int dimensions, IPhysicsColliderRuntimeSink* sink) const = 0;
    /** @brief Return the stable capability id used with ProviderLease::query(). */
    static editing::CapabilityId capabilityId() { return editing::CapabilityId("physics.editing.factory"); }
};

/**
 * @brief Publish the physics editing factory into a host-owned extension registry.
 * @param registry Host-owned registry that must outlive the provider handle and leases.
 * @return Generation-qualified provider handle or a structured registration failure.
 * @thread Main-thread registration path; acquired leases follow the descriptor affinity.
 */
[[nodiscard]] editing::Result<editing::ProviderHandle> registerEditingProvider(
    editing::ExtensionProviderRegistry& registry);

}  // namespace eve::physics_editing
