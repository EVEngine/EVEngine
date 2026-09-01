#include "physics_editing/PhysicsEditingProvider.h"

#include <utility>

namespace eve::physics_editing {
namespace {
class PhysicsEditingFactory final : public IPhysicsEditingFactory {
public:
    std::unique_ptr<PhysicsColliderPublishingTarget> createCollider(
        std::string id, int dimensions, IPhysicsColliderRuntimeSink* sink) const override {
        return std::make_unique<PhysicsColliderPublishingTarget>(std::move(id), dimensions, sink);
    }
};
}  // namespace

editing::Result<editing::ProviderHandle> registerEditingProvider(
    editing::ExtensionProviderRegistry& registry) {
    editing::ExtensionDescriptor descriptor;
    descriptor.id              = "physics.editing";
    descriptor.schemaVersion   = 1;
    descriptor.requiredModules = {"physics"};
    return editing::registerStaticProvider(registry, std::move(descriptor),
                                           IPhysicsEditingFactory::capabilityId(),
                                           std::make_shared<PhysicsEditingFactory>());
}

}  // namespace eve::physics_editing
