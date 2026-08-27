#include "rts/RTSSystems.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace eve::rts {
namespace {

template <typename T>
Result<T> failure(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path)));
}

template <typename T>
Result<T> failureFrom(const Status& status) {
    return Result<T>::failure(status);
}

bool finitePosition(WorldPosition position) {
    return std::isfinite(position.x) && std::isfinite(position.y);
}

bool sameHandle(const ecs::EntityHandle& left, const ecs::EntityHandle& right) {
    return left.table == right.table && left.type == right.type && left.id == right.id &&
           left.generation == right.generation;
}

Result<std::optional<OrderRecord>> readCurrent(OrderComponent& orders) {
    auto current = orders.current();
    if (current) return Result<std::optional<OrderRecord>>::success(std::move(current).takeValue());
    if (current.code() == StatusCode::NotFound) {
        current.ignore("RTS entity has no active order");
        return Result<std::optional<OrderRecord>>::success(std::nullopt,
                                                            Status::success(StatusCode::NoOp));
    }
    return failureFrom<std::optional<OrderRecord>>(current.status());
}

Result<std::size_t> advanceEffects(RTSEffectComponent& effects, const SimulationStep& step) {
    auto advanced = effects.advance(step);
    if (!advanced) return failureFrom<std::size_t>(advanced.status());
    const auto result = std::move(advanced).takeValue();
    return Result<std::size_t>::success(
        result.settled, Status::success(result.settled == 0 ? StatusCode::NoOp
                                                             : StatusCode::Applied));
}

}  // namespace

std::span<const SystemContract> systemContracts() noexcept {
    static constexpr SystemContract contracts[] = {
        {"rts.command_fan_out",
         "ecs::View<Unit, Unit::Identity, Unit::Orders>",
         "Unit::Identity; selection handles; formation input",
         "Unit::Orders",
         "none; use ecs::ScopedDefer if future code creates/removes entities/components",
         "none; caller owns command receipt/event publication",
         "input.command"},
        {"rts.motion",
         "ecs::View<Unit, Unit::Identity, Unit::Motion, Unit::Orders>",
         "Unit::Identity; Unit::Orders",
         "Unit::Motion",
         "none",
         "none",
         "simulation.movement"},
        {"rts.order_action",
         "ecs::View<Unit, Unit::Identity, Unit::Orders, Unit::Action>",
         "Unit::Identity; Unit::Action; active OrderRecord",
         "Unit::Orders; generic order lifecycle state",
         "none",
         "delegated to IRTSActionExecutor/ActionRuntime",
         "simulation.action"},
        {"rts.building_production",
         "ecs::View<Building, Building::Identity, Building::Production>",
         "Building::Identity; SimulationStep",
         "Building::Production",
         "none",
         "delegated to production::WorkQueue",
         "simulation.production"},
        {"rts.effects.unit",
         "ecs::View<Unit, Unit::Identity, Unit::Effects>",
         "Unit::Identity; SimulationStep",
         "Unit::Effects",
         "none",
         "delegated to effects::EffectContainer",
         "simulation.effects"},
        {"rts.effects.building",
         "ecs::View<Building, Building::Identity, Building::Effects>",
         "Building::Identity; SimulationStep",
         "Building::Effects",
         "none",
         "delegated to effects::EffectContainer",
         "simulation.effects"},
    };
    return {contracts, sizeof(contracts) / sizeof(contracts[0])};
}

Result<void> FormationSpec::validate() const {
    if (!std::isfinite(spacing) || spacing <= 0.0f)
        return failure<void>(DiagnosticCode::InvalidArgument,
                             "formation spacing must be finite and positive", "spacing");
    if (columns < 0)
        return failure<void>(DiagnosticCode::InvalidArgument,
                             "formation columns must be non-negative", "columns");
    switch (kind) {
    case FormationKind::Line:
    case FormationKind::Grid:
    case FormationKind::Wedge:
        return Result<void>::success(Status::success(StatusCode::Applied));
    }
    return failure<void>(DiagnosticCode::InvalidArgument, "formation kind is invalid", "kind");
}

Result<std::vector<WorldPosition>> FormationPlanner::plan(std::size_t count,
                                                          WorldPosition anchor,
                                                          const FormationSpec& spec) {
    auto valid = spec.validate();
    if (!valid) return failureFrom<std::vector<WorldPosition>>(valid.status());
    if (!finitePosition(anchor))
        return failure<std::vector<WorldPosition>>(DiagnosticCode::InvalidArgument,
                                                   "formation anchor must be finite", "anchor");

    std::vector<WorldPosition> result;
    result.reserve(count);
    if (count == 0)
        return Result<std::vector<WorldPosition>>::success(std::move(result),
                                                            Status::success(StatusCode::NoOp));

    const float spacing = spec.spacing;
    switch (spec.kind) {
    case FormationKind::Line: {
        const float center = static_cast<float>(count - 1) * 0.5f;
        for (std::size_t index = 0; index < count; ++index) {
            result.push_back({anchor.x + (static_cast<float>(index) - center) * spacing, anchor.y});
        }
        break;
    }
    case FormationKind::Grid: {
        const std::size_t columns = spec.columns > 0
                                        ? static_cast<std::size_t>(spec.columns)
                                        : static_cast<std::size_t>(std::ceil(std::sqrt(
                                              static_cast<double>(count))));
        if (columns == 0)
            return failure<std::vector<WorldPosition>>(DiagnosticCode::InvariantViolation,
                                                       "grid formation computed zero columns", "columns");
        const std::size_t rows = (count + columns - 1) / columns;
        const float columnCenter = static_cast<float>(columns - 1) * 0.5f;
        const float rowCenter = static_cast<float>(rows - 1) * 0.5f;
        for (std::size_t index = 0; index < count; ++index) {
            const std::size_t row = index / columns;
            const std::size_t column = index % columns;
            result.push_back({anchor.x + (static_cast<float>(column) - columnCenter) * spacing,
                              anchor.y + (static_cast<float>(row) - rowCenter) * spacing});
        }
        break;
    }
    case FormationKind::Wedge: {
        std::size_t row = 0;
        std::size_t rowStart = 0;
        for (std::size_t index = 0; index < count; ++index) {
            while (index >= rowStart + row + 1) {
                rowStart += row + 1;
                ++row;
            }
            const std::size_t slot = index - rowStart;
            const float rowCenter = static_cast<float>(row) * 0.5f;
            result.push_back({anchor.x + (static_cast<float>(slot) - rowCenter) * spacing,
                              anchor.y + static_cast<float>(row) * spacing});
        }
        break;
    }
    }
    return Result<std::vector<WorldPosition>>::success(std::move(result),
                                                        Status::success(StatusCode::Applied));
}

Result<FanOutReceipt> CommandFanOutSystem::fanOut(
    std::span<const ecs::EntityHandle> unitHandles, const CommandSpec& command,
    const FormationSpec& formation) {
    if (unitHandles.empty())
        return failure<FanOutReceipt>(DiagnosticCode::InvalidArgument,
                                      "RTS command fan-out requires at least one Unit", "selection.units");
    auto commandValid = command.validate();
    if (!commandValid) return failureFrom<FanOutReceipt>(commandValid.status());

    auto formationValid = formation.validate();
    if (!formationValid) return failureFrom<FanOutReceipt>(formationValid.status());
    auto targets = FormationPlanner::plan(unitHandles.size(), command.target, formation);
    if (!targets) return failureFrom<FanOutReceipt>(targets.status());
    auto plannedTargets = std::move(targets).takeValue();

    // The explicit View is the closure proof for this command boundary: a
    // Unit subclass is accepted by the Unit registry, while a Building root
    // cannot enter the selected set merely because it has an Identity field.
    std::vector<ecs::EntityHandle> visibleUnits;
    {
        auto view = ecs::View<Unit, Unit::Identity, Unit::Orders>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [identity, orders] = *it;
            (void)orders;
            Unit* unit = identity == nullptr
                             ? nullptr
                             : dynamic_cast<Unit*>(ecs::try_get(identity->self));
            if (unit != nullptr) visibleUnits.push_back(ecs::handle_of(unit));
        }
    }

    std::vector<Unit*> selected;
    selected.reserve(unitHandles.size());
    for (const auto& handle : unitHandles) {
        auto* entity = ecs::try_get(handle);
        auto* unit = entity == nullptr ? nullptr : dynamic_cast<Unit*>(entity);
        if (unit == nullptr)
            return failure<FanOutReceipt>(DiagnosticCode::StaleHandle,
                                          "RTS fan-out selection contains a stale or non-Unit handle",
                                          "selection.units");
        const auto liveHandle = ecs::handle_of(unit);
        const bool inView = std::any_of(
            visibleUnits.begin(), visibleUnits.end(), [&liveHandle](const auto& candidate) {
                return sameHandle(candidate, liveHandle);
            });
        if (!inView)
            return failure<FanOutReceipt>(DiagnosticCode::InvariantViolation,
                                          "RTS Unit selection is outside the declared View closure",
                                          "selection.units");
        selected.push_back(unit);
    }

    FanOutReceipt receipt;
    receipt.requested = selected.size();
    receipt.orderIds.reserve(selected.size());
    for (std::size_t index = 0; index < selected.size(); ++index) {
        auto order = selected[index]->orders()->values.enqueue(command, static_cast<int>(index));
        if (!order) {
            const Status status = order.status();
            for (std::size_t rollback = 0; rollback < receipt.orderIds.size(); ++rollback) {
                auto cancelled = selected[rollback]->orders()->values.cancel(
                    receipt.orderIds[rollback], "command fan-out rollback");
                cancelled.ignore("best-effort fan-out rollback");
            }
            return Result<FanOutReceipt>::failure(status);
        }
        receipt.orderIds.push_back(std::move(order).takeValue());
    }
    receipt.accepted = receipt.orderIds.size();
    return Result<FanOutReceipt>::success(std::move(receipt), Status::success(StatusCode::Applied));
}

Result<std::size_t> MotionSystem::step(const SimulationStep& step) {
    if (step.delta.nanoseconds() < 0)
        return failure<std::size_t>(DiagnosticCode::InvalidArgument,
                                   "RTS motion step delta must be non-negative", "step.delta");
    const double deltaSeconds = step.delta.seconds();
    std::size_t processed = 0;
    auto view = ecs::View<Unit, Unit::Identity, Unit::Motion, Unit::Orders>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [identity, motion, orders] = *it;
        Unit* unit = identity == nullptr
                         ? nullptr
                         : dynamic_cast<Unit*>(ecs::try_get(identity->self));
        if (unit == nullptr)
            return failure<std::size_t>(DiagnosticCode::StaleHandle,
                                       "RTS motion identity link is stale", "unit.identity.self");
        if (!std::isfinite(motion->x) || !std::isfinite(motion->y) || !std::isfinite(motion->speed) ||
            !std::isfinite(motion->arrivalRadius) || motion->speed < 0.0f || motion->arrivalRadius < 0.0f)
            return failure<std::size_t>(DiagnosticCode::InvalidArgument,
                                       "RTS motion state must contain finite non-negative values", "unit.motion");

        auto current = readCurrent(orders->values);
        if (!current) return failureFrom<std::size_t>(current.status());
        auto record = std::move(current).takeValue();
        if (!record) continue;
        if (record->kind != OrderKind::Move) {
            motion->arrived = true;
            ++processed;
            continue;
        }

        const float dx = record->target.x - motion->x;
        const float dy = record->target.y - motion->y;
        const float distance = std::hypot(dx, dy);
        if (!std::isfinite(distance))
            return failure<std::size_t>(DiagnosticCode::InvariantViolation,
                                       "RTS motion target distance is non-finite", "order.target");
        if (distance <= motion->arrivalRadius) {
            motion->x = record->target.x;
            motion->y = record->target.y;
            motion->arrived = true;
        } else if (deltaSeconds > 0.0 && motion->speed > 0.0f) {
            const double travel = static_cast<double>(motion->speed) * deltaSeconds;
            if (!std::isfinite(travel))
                return failure<std::size_t>(DiagnosticCode::InvalidArgument,
                                           "RTS motion travel distance is non-finite", "unit.motion.speed");
            const float amount = static_cast<float>(std::min<double>(travel, distance));
            motion->x += dx / distance * amount;
            motion->y += dy / distance * amount;
            motion->arrived = static_cast<double>(amount) >= distance - motion->arrivalRadius;
            if (motion->arrived) {
                motion->x = record->target.x;
                motion->y = record->target.y;
            }
        } else {
            motion->arrived = false;
        }
        ++processed;
    }
    return Result<std::size_t>::success(processed, Status::success(StatusCode::Applied));
}

Result<std::size_t> OrderActionSystem::step(const SimulationStep& step,
                                            IRTSActionExecutor& executor) {
    if (step.delta.nanoseconds() < 0)
        return failure<std::size_t>(DiagnosticCode::InvalidArgument,
                                   "RTS action step delta must be non-negative", "step.delta");
    std::size_t processed = 0;
    auto view = ecs::View<Unit, Unit::Identity, Unit::Orders, Unit::Action>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [identity, orders, action] = *it;
        (void)action;
        Unit* unit = identity == nullptr
                         ? nullptr
                         : dynamic_cast<Unit*>(ecs::try_get(identity->self));
        if (unit == nullptr)
            return failure<std::size_t>(DiagnosticCode::StaleHandle,
                                       "RTS action identity link is stale", "unit.identity.self");
        auto current = readCurrent(orders->values);
        if (!current) return failureFrom<std::size_t>(current.status());
        auto record = std::move(current).takeValue();
        if (!record) continue;

        auto executed = executor.execute(*unit, *record, step);
        if (!executed) return failureFrom<std::size_t>(executed.status());
        const auto outcome = std::move(executed).takeValue();
        if (outcome.disposition == ActionDisposition::Completed) {
            auto completed = orders->values.complete(record->id);
            if (!completed) return failureFrom<std::size_t>(completed.status());
        }
        ++processed;
    }
    return Result<std::size_t>::success(processed, Status::success(StatusCode::Applied));
}

Result<std::size_t> BuildingProductionSystem::step(const SimulationStep& step) {
    if (step.delta.nanoseconds() < 0)
        return failure<std::size_t>(DiagnosticCode::InvalidArgument,
                                   "RTS production step delta must be non-negative", "step.delta");
    std::size_t processed = 0;
    auto view = ecs::View<Building, Building::Identity, Building::Production>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [identity, production] = *it;
        Building* building = identity == nullptr
                                 ? nullptr
                                 : dynamic_cast<Building*>(ecs::try_get(identity->self));
        if (building == nullptr)
            return failure<std::size_t>(DiagnosticCode::StaleHandle,
                                       "RTS production identity link is stale", "building.identity.self");
        auto advanced = production->values.advance(step);
        if (!advanced) return failureFrom<std::size_t>(advanced.status());
        advanced.value();
        ++processed;
    }
    return Result<std::size_t>::success(processed, Status::success(StatusCode::Applied));
}

Result<std::size_t> EffectSystem::step(const SimulationStep& step) {
    if (step.delta.nanoseconds() < 0)
        return failure<std::size_t>(DiagnosticCode::InvalidArgument,
                                   "RTS effects step delta must be non-negative", "step.delta");
    std::size_t processed = 0;
    {
        auto view = ecs::View<Unit, Unit::Identity, Unit::Effects>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [identity, effects] = *it;
            Unit* unit = identity == nullptr
                             ? nullptr
                             : dynamic_cast<Unit*>(ecs::try_get(identity->self));
            if (unit == nullptr)
                return failure<std::size_t>(DiagnosticCode::StaleHandle,
                                           "RTS unit effects identity link is stale", "unit.identity.self");
            auto advanced = advanceEffects(effects->values, step);
            if (!advanced) return failureFrom<std::size_t>(advanced.status());
            processed += std::move(advanced).takeValue();
        }
    }
    {
        auto view = ecs::View<Building, Building::Identity, Building::Effects>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [identity, effects] = *it;
            Building* building = identity == nullptr
                                     ? nullptr
                                     : dynamic_cast<Building*>(ecs::try_get(identity->self));
            if (building == nullptr)
                return failure<std::size_t>(DiagnosticCode::StaleHandle,
                                           "RTS building effects identity link is stale",
                                           "building.identity.self");
            auto advanced = advanceEffects(effects->values, step);
            if (!advanced) return failureFrom<std::size_t>(advanced.status());
            processed += std::move(advanced).takeValue();
        }
    }
    return Result<std::size_t>::success(processed, Status::success(StatusCode::Applied));
}

}  // namespace eve::rts
