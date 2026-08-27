#include "physics/TargetingLineOfSightAdapter.h"

#include "physics/World3D.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace eve::physics {
namespace {

template <class T>
eve::Result<T> failure(eve::DiagnosticCode code, std::string message) {
    return eve::Result<T>::failure(eve::Diagnostic::error(code, std::move(message)));
}

}  // namespace

eve::Result<void> TargetingLineOfSightAdapter::addWorld(World3D* world) {
    if (!world)
        return failure<void>(eve::DiagnosticCode::InvalidArgument,
                             "Physics LOS cannot register a null World3D");
    if (!world->isValid())
        return failure<void>(eve::DiagnosticCode::NotFound,
                             "Physics LOS cannot register an invalid World3D");

    for (auto it = worlds_.begin(); it != worlds_.end();) {
        if (!*it || !(*it)->isValid()) {
            it = worlds_.erase(it);
            continue;
        }
        if (*it == world)
            return eve::Result<void>::success(eve::Status::success(eve::StatusCode::NoOp));
        return failure<void>(eve::DiagnosticCode::Conflict,
                             "Physics LOS already has a different active World3D");
    }
    worlds_.push_back(world);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> TargetingLineOfSightAdapter::removeWorld(World3D* world) {
    if (!world)
        return failure<void>(eve::DiagnosticCode::InvalidArgument,
                             "Physics LOS cannot remove a null World3D");
    const auto before = worlds_.size();
    worlds_.erase(std::remove(worlds_.begin(), worlds_.end(), world), worlds_.end());
    const auto code = before == worlds_.size() ? eve::StatusCode::NoOp : eve::StatusCode::Applied;
    return eve::Result<void>::success(eve::Status::success(code));
}

eve::Result<sensing::LineOfSightResult> TargetingLineOfSightAdapter::query(
    const sensing::TargetLocation& from, const sensing::TargetLocation& to) const {
    const auto* fromPoint = std::get_if<sensing::WorldPoint>(&from);
    const auto* toPoint = std::get_if<sensing::WorldPoint>(&to);
    if (!fromPoint || !toPoint || !fromPoint->isValid() || !toPoint->isValid() ||
        fromPoint->space() != sensing::CoordinateSpace::World3D ||
        toPoint->space() != sensing::CoordinateSpace::World3D)
        return failure<sensing::LineOfSightResult>(
            eve::DiagnosticCode::Unsupported,
            "Physics LOS supports only two valid World3D points in the same space");

    if (!std::isfinite(fromPoint->x()) || !std::isfinite(fromPoint->y()) ||
        !std::isfinite(fromPoint->z()) || !std::isfinite(toPoint->x()) ||
        !std::isfinite(toPoint->y()) || !std::isfinite(toPoint->z()))
        return failure<sensing::LineOfSightResult>(eve::DiagnosticCode::InvalidArgument,
                                                   "Physics LOS points must be finite");

    World3D* activeWorld = nullptr;
    for (World3D* world : worlds_) {
        if (!world || !world->isValid()) continue;
        if (activeWorld)
            return failure<sensing::LineOfSightResult>(
                eve::DiagnosticCode::Conflict,
                "Physics LOS has more than one valid active World3D");
        activeWorld = world;
    }
    if (!activeWorld)
        return failure<sensing::LineOfSightResult>(
            eve::DiagnosticCode::Unsupported,
            "Physics LOS has no valid active World3D");

    activeWorld->rayCast(fromPoint->x(), fromPoint->y(), fromPoint->z(), toPoint->x(), toPoint->y(),
                         toPoint->z());
    if (activeWorld->hasRayHit())
        return eve::Result<sensing::LineOfSightResult>::success(
            sensing::LineOfSightResult{false, std::nullopt});
    return eve::Result<sensing::LineOfSightResult>::success(
        sensing::LineOfSightResult{true, std::nullopt});
}

}  // namespace eve::physics
