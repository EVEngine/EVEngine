/**
 * @file TacticsLineOfSightAdapter.cpp
 * @brief Grid line of sight answered from a bound tactics board.
 */

#include "tactics/TacticsLineOfSightAdapter.h"

#include "common/Capability.h"
#include "common/Diagnostic.h"
#include "sensing/LineOfSightRouter.h"
#include "tactics/LineOfSight.h"
#include "tactics/TacticsTypes.h"

#include <utility>

namespace eve::tactics {
namespace {

template <typename T>
Result<T> failure(DiagnosticCode code, std::string message, std::string path) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path)));
}

/** @brief Handles are compared field by field: the ECS handle type defines no equality operator. */
bool sameHandle(const ecs::EntityHandle& left, const ecs::EntityHandle& right) noexcept {
    return left.table == right.table && left.type == right.type && left.id == right.id &&
           left.generation == right.generation;
}

/** @brief Resolve a bound handle to the battle's board, or null when it is stale. */
const BoardState* boundBoard(ecs::EntityHandle battle, bool bound) {
    if (!bound) return nullptr;
    // Resolving by handle each call is what makes a released battle safe: the ECS refuses a stale
    // generation, so a dangling board is never read.
    auto* resolved = dynamic_cast<Battle*>(ecs::try_get(battle));
    return resolved != nullptr ? &resolved->board()->value : nullptr;
}

/** @brief Translate a sensing location into a tactics cell, or refuse the space it uses. */
Result<Cell> toCell(const sensing::TargetLocation& location) {
    return std::visit(
        [](const auto& point) -> Result<Cell> {
            using Point = std::decay_t<decltype(point)>;
            if constexpr (std::is_same_v<Point, sensing::GridPoint>) {
                // A 2D grid point is layer zero; a 3D one keeps its z as the tactical layer, so a
                // layered board is reachable without converting between spaces.
                if (point.space() == sensing::CoordinateSpace::Grid2D) return Result<Cell>::success(Cell{point.x(), point.y(), 0});
                if (point.space() == sensing::CoordinateSpace::Grid3D)
                    return Result<Cell>::success(Cell{point.x(), point.y(), point.z()});
                return failure<Cell>(DiagnosticCode::Unsupported,
                                     "tactics line of sight answers grid coordinates only",
                                     "lineOfSight.space");
            } else {
                return failure<Cell>(DiagnosticCode::Unsupported,
                                     "tactics line of sight answers grid coordinates only",
                                     "lineOfSight.space");
            }
        },
        location);
}

}  // namespace

TacticsLineOfSightAdapter& tacticsLineOfSightAdapter() {
    static TacticsLineOfSightAdapter adapter;
    return adapter;
}

Result<void> TacticsLineOfSightAdapter::bindBattle(ecs::EntityHandle battle) {
    if (boundBoard(battle, true) == nullptr)
        return failure<void>(DiagnosticCode::StaleHandle, "line-of-sight battle binding is not a live battle", "battle");
    if (bound_ && sameHandle(battle_, battle)) return Result<void>::success(Status::success(StatusCode::NoOp));
    if (bound_)
        // One board at a time on purpose: with two bound boards the answer would depend on which
        // was bound last, exactly the order-dependence the router exists to remove.
        return failure<void>(DiagnosticCode::Conflict,
                             "another battle is already bound for grid line of sight", "battle");
    battle_ = battle;
    bound_  = true;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> TacticsLineOfSightAdapter::unbindBattle(ecs::EntityHandle battle) {
    if (!bound_ || !sameHandle(battle_, battle))
        return Result<void>::success(Status::success(StatusCode::NoOp));
    battle_ = {};
    bound_  = false;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<sensing::LineOfSightResult> TacticsLineOfSightAdapter::query(const sensing::TargetLocation& from,
                                                                   const sensing::TargetLocation& to) const {
    const BoardState* board = boundBoard(battle_, bound_);
    if (board == nullptr)
        return failure<sensing::LineOfSightResult>(
            DiagnosticCode::Unsupported, "no tactics battle is bound for grid line of sight", "lineOfSight.battle");
    auto fromCell = toCell(from);
    if (!fromCell) return Result<sensing::LineOfSightResult>::failure(fromCell.status());
    auto toCellValue = toCell(to);
    if (!toCellValue) return Result<sensing::LineOfSightResult>::failure(toCellValue.status());

    // The same policy the module's own range query uses: a registered sight policy when a project
    // provided one, otherwise the built-in grid rule. One authority for "what blocks sight".
    const ILineOfSightPolicy* registered = cap::query<ILineOfSightPolicy>();
    const ILineOfSightPolicy& policy = registered != nullptr ? *registered : *gridLineOfSightPolicy();
    auto visible = policy.visible(*board, fromCell.value(), toCellValue.value());
    if (!visible) return Result<sensing::LineOfSightResult>::failure(visible.status());

    sensing::LineOfSightResult result;
    result.visible = visible.value();
    // `blocker` is left empty on purpose: the grid policy answers visibility without naming what
    // blocked it, and reporting the target's occupant instead would be a different fact wearing
    // the same field. A policy that can identify the blocker is free to fill it in.
    return Result<sensing::LineOfSightResult>::success(std::move(result));
}

Result<void> registerTacticsLineOfSightProvider() {
    auto router = sensing::ensureLineOfSightRouter();
    if (!router) return Result<void>::failure(router.status());
    TacticsLineOfSightAdapter& adapter = tacticsLineOfSightAdapter();
    auto grid2d = router.value()->addProvider(sensing::CoordinateSpace::Grid2D, &adapter);
    if (!grid2d) return Result<void>::failure(grid2d.status());
    auto grid3d = router.value()->addProvider(sensing::CoordinateSpace::Grid3D, &adapter);
    if (!grid3d) return Result<void>::failure(grid3d.status());
    return Result<void>::success(Status::success(StatusCode::Applied));
}

}  // namespace eve::tactics
