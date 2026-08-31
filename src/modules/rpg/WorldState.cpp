#include "rpg/WorldState.h"

#include "rpg/GameState.h"

#include <string>

namespace eve::rpg {
namespace {

constexpr std::size_t maxIdBytes = 256;

bool validId(std::string_view id) {
    if (id.empty() || id.size() > maxIdBytes) return false;
    for (unsigned char character : id)
        if (character < 0x20 || character == 0x7f) return false;
    return true;
}

std::string scope(std::string_view mapId) { return "world.object:" + std::string(mapId); }

eve::Result<void> invalid(std::string path) {
    return eve::Result<void>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::InvalidArgument, "world-state IDs must be non-empty, bounded, and control-free",
        std::move(path), {}, "rpg.world-state"));
}

}  // namespace

bool WorldState::isObjectConsumed(std::string_view mapId, std::string_view objectId) const {
    if (!gameState_ || !validId(mapId) || !validId(objectId)) return false;
    const std::string owningScope = scope(mapId);
    const std::string owningObjectId(objectId);
    return gameState_->hasSelfVariable(owningScope, owningObjectId) &&
           gameState_->getSelfVariable(owningScope, owningObjectId) != 0.0;
}

eve::Result<void> WorldState::consumeObject(std::string_view mapId, std::string_view objectId) {
    if (!validId(mapId)) return invalid("mapId");
    if (!validId(objectId)) return invalid("objectId");
    if (isObjectConsumed(mapId, objectId))
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::NoOp));
    gameState_->setSelfVariable(scope(mapId), std::string(objectId), 1.0);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> WorldState::resetObject(std::string_view mapId, std::string_view objectId) {
    if (!validId(mapId)) return invalid("mapId");
    if (!validId(objectId)) return invalid("objectId");
    if (!isObjectConsumed(mapId, objectId))
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::NoOp));
    gameState_->setSelfVariable(scope(mapId), std::string(objectId), 0.0);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

}  // namespace eve::rpg
