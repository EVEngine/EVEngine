#include "common/EntitySpatialResolver.h"

#include "common/Capability.h"

#include <optional>

namespace eve {

Result<EntitySpatialPose> resolveEntitySpatialPose(ecs::EntityHandle entity, std::string_view bone) {
    std::optional<EntitySpatialPose> pose;
    std::optional<Status>            failure;
    cap::forEachUntil<IEntitySpatialProvider>([&](IEntitySpatialProvider* provider) {
        auto resolved = provider->resolve(entity, bone);
        if (resolved) {
            pose = std::move(resolved).takeValue();
            return true;
        }
        if (resolved.status().code() != StatusCode::NotFound) {
            failure = resolved.status();
            return true;
        }
        return false;
    });
    if (pose) return Result<EntitySpatialPose>::success(std::move(*pose));
    if (failure) return Result<EntitySpatialPose>::failure(std::move(*failure));
    return Result<EntitySpatialPose>::failure(
        Diagnostic::error(DiagnosticCode::NotFound, "no spatial provider owns the entity", "entity"));
}

}  // namespace eve
