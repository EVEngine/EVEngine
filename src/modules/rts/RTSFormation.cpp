#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>
#include "rts/RTSSystemMovementInternal.h"

namespace eve::rts {
using systems_internal::distanceSquared;
using systems_internal::isFinitePosition;
using systems_internal::isSameHandle;

Result<void> FormationSpec::validate() const {
    if (!std::isfinite(spacing) || spacing <= 0.0f)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "formation spacing must be finite and positive", "spacing"));
    if (!std::isfinite(rotationRadians))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "formation rotation must be finite", "rotationRadians"));
    if (columns < 0)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "formation columns must be non-negative", "columns"));
    switch (kind) {
        case FormationKind::Line:
        case FormationKind::Grid:
        case FormationKind::Column:
        case FormationKind::Dispersed:
        case FormationKind::Wedge: return Result<void>::success(Status::success(StatusCode::Applied));
    }
    return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, "formation kind is invalid", "kind"));
}

Result<std::vector<WorldPosition>> FormationPlanner::plan(std::size_t count, WorldPosition anchor,
                                                          const FormationSpec& spec) {
    auto valid = spec.validate();
    if (!valid) return Result<std::vector<WorldPosition>>::failure(valid.status());
    if (!isFinitePosition(anchor))
        return Result<std::vector<WorldPosition>>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "formation anchor must be finite", "anchor"));

    std::vector<WorldPosition> result;
    result.reserve(count);
    if (count == 0)
        return Result<std::vector<WorldPosition>>::success(std::move(result), Status::success(StatusCode::NoOp));

    const float spacing = spec.spacing;
    switch (spec.kind) {
        case FormationKind::Line: {
            const float center = static_cast<float>(count - 1) * 0.5f;
            for (std::size_t index = 0; index < count; ++index) {
                result.push_back({anchor.x + (static_cast<float>(index) - center) * spacing, anchor.y});
            }
            break;
        }
        case FormationKind::Column: {
            const float center = static_cast<float>(count - 1) * 0.5f;
            for (std::size_t index = 0; index < count; ++index)
                result.push_back({anchor.x, anchor.y + (static_cast<float>(index) - center) * spacing});
            break;
        }
        case FormationKind::Dispersed: {
            result.push_back(anchor);
            for (std::size_t ring = 1; result.size() < count; ++ring) {
                const std::size_t slots  = 6 * ring;
                const double      radius = static_cast<double>(ring) * spacing * 1.5;
                for (std::size_t slot = 0; slot < slots && result.size() < count; ++slot) {
                    const double angle =
                        2.0 * std::numbers::pi * static_cast<double>(slot) / static_cast<double>(slots);
                    result.push_back({static_cast<float>(anchor.x + radius * std::cos(angle)),
                                      static_cast<float>(anchor.y + radius * std::sin(angle))});
                }
            }
            break;
        }
        case FormationKind::Grid: {
            const std::size_t columns =
                spec.columns > 0 ? static_cast<std::size_t>(spec.columns)
                                 : static_cast<std::size_t>(std::ceil(std::sqrt(static_cast<double>(count))));
            if (columns == 0)
                return Result<std::vector<WorldPosition>>::failure(Diagnostic::error(
                    DiagnosticCode::InvariantViolation, "grid formation computed zero columns", "columns"));
            const std::size_t rows         = (count + columns - 1) / columns;
            const float       columnCenter = static_cast<float>(columns - 1) * 0.5f;
            const float       rowCenter    = static_cast<float>(rows - 1) * 0.5f;
            for (std::size_t index = 0; index < count; ++index) {
                const std::size_t row    = index / columns;
                const std::size_t column = index % columns;
                result.push_back({anchor.x + (static_cast<float>(column) - columnCenter) * spacing,
                                  anchor.y + (static_cast<float>(row) - rowCenter) * spacing});
            }
            break;
        }
        case FormationKind::Wedge: {
            std::size_t row      = 0;
            std::size_t rowStart = 0;
            for (std::size_t index = 0; index < count; ++index) {
                while (index >= rowStart + row + 1) {
                    rowStart += row + 1;
                    ++row;
                }
                const std::size_t slot      = index - rowStart;
                const float       rowCenter = static_cast<float>(row) * 0.5f;
                result.push_back({anchor.x + (static_cast<float>(slot) - rowCenter) * spacing,
                                  anchor.y + static_cast<float>(row) * spacing});
            }
            break;
        }
    }
    if (spec.rotationRadians != 0.f) {
        const double c = std::cos(static_cast<double>(spec.rotationRadians));
        const double s = std::sin(static_cast<double>(spec.rotationRadians));
        for (auto& point : result) {
            const double x = static_cast<double>(point.x) - anchor.x;
            const double y = static_cast<double>(point.y) - anchor.y;
            point = {static_cast<float>(anchor.x + c * x - s * y), static_cast<float>(anchor.y + s * x + c * y)};
        }
    }
    if (std::any_of(result.begin(), result.end(), [](WorldPosition point) { return !isFinitePosition(point); }))
        return Result<std::vector<WorldPosition>>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Formation exceeds finite world coordinates", "formation"));
    return Result<std::vector<WorldPosition>>::success(std::move(result), Status::success(StatusCode::Applied));
}

Result<FanOutReceipt> CommandFanOutSystem::fanOut(std::span<const ecs::EntityHandle> unitHandles,
                                                  const CommandSpec& command, const FormationSpec& formation) {
    if (unitHandles.empty())
        return Result<FanOutReceipt>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "RTS command fan-out requires at least one Unit", "selection.units"));
    auto commandValid = command.validate();
    if (!commandValid) return Result<FanOutReceipt>::failure(commandValid.status());

    auto formationValid = formation.validate();
    if (!formationValid) return Result<FanOutReceipt>::failure(formationValid.status());

    // The explicit View is the closure proof for this command boundary: a
    // Unit subclass is accepted by the Unit registry, while a Building root
    // cannot enter the selected set merely because it has an Identity field.
    std::vector<ecs::EntityHandle> visibleUnits;
    {
        auto view = ecs::View<Unit, Unit::Identity, Unit::Orders>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [identity, orders] = *it;
            (void)orders;
            Unit* unit = identity == nullptr ? nullptr : dynamic_cast<Unit*>(ecs::try_get(identity->self));
            if (unit != nullptr) visibleUnits.push_back(ecs::handle_of(unit));
        }
    }

    std::vector<Unit*> selected;
    selected.reserve(unitHandles.size());
    float largestRadius = 0.f, secondRadius = 0.f;
    for (const auto& handle : unitHandles) {
        auto* entity = ecs::try_get(handle);
        auto* unit   = entity == nullptr ? nullptr : dynamic_cast<Unit*>(entity);
        if (unit == nullptr)
            return Result<FanOutReceipt>::failure(
                Diagnostic::error(DiagnosticCode::StaleHandle,
                                  "RTS fan-out selection contains a stale or non-Unit handle", "selection.units"));
        const auto liveHandle = ecs::handle_of(unit);
        const bool inView = std::any_of(visibleUnits.begin(), visibleUnits.end(), [&liveHandle](const auto& candidate) {
            return isSameHandle(candidate, liveHandle);
        });
        if (!inView)
            return Result<FanOutReceipt>::failure(
                Diagnostic::error(DiagnosticCode::InvariantViolation,
                                  "RTS Unit selection is outside the declared View closure", "selection.units"));
        if (std::find(selected.begin(), selected.end(), unit) != selected.end())
            return Result<FanOutReceipt>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "Formation selection contains duplicate units", "selection.units"));
        const float radius = unit->crowd()->radius;
        if (!std::isfinite(radius) || radius <= 0.f || !isFinitePosition({unit->motion()->x, unit->motion()->y}))
            return Result<FanOutReceipt>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument,
                                  "Formation unit geometry must be finite with positive radius", "selection.units"));
        if (radius >= largestRadius) {
            secondRadius  = largestRadius;
            largestRadius = radius;
        } else
            secondRadius = std::max(secondRadius, radius);
        selected.push_back(unit);
    }

    FormationSpec resolved = formation;
    if (selected.size() > 1) resolved.spacing = std::max(resolved.spacing, largestRadius + secondRadius);
    auto targets = FormationPlanner::plan(selected.size(), command.target, resolved);
    if (!targets) return Result<FanOutReceipt>::failure(targets.status());
    auto plannedTargets = std::move(targets).takeValue();
    if (std::any_of(plannedTargets.begin(), plannedTargets.end(), [](WorldPosition p) { return !isFinitePosition(p); }))
        return Result<FanOutReceipt>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Formation exceeds finite world coordinates", "formation"));

    struct AvailableSlot {
        WorldPosition position{};
        int           index = -1;
    };
    std::vector<AvailableSlot> available;
    available.reserve(plannedTargets.size());
    for (std::size_t index = 0; index < plannedTargets.size(); ++index)
        available.push_back({plannedTargets[index], static_cast<int>(index)});
    std::vector<std::size_t> assignmentOrder(selected.size());
    for (std::size_t index = 0; index < selected.size(); ++index) assignmentOrder[index] = index;
    std::sort(assignmentOrder.begin(), assignmentOrder.end(), [&](std::size_t left, std::size_t right) {
        const auto  leftMotion    = selected[left]->motion();
        const auto  rightMotion   = selected[right]->motion();
        const float leftDistance  = distanceSquared(leftMotion->x, leftMotion->y, command.target.x, command.target.y);
        const float rightDistance = distanceSquared(rightMotion->x, rightMotion->y, command.target.x, command.target.y);
        if (leftDistance != rightDistance) return leftDistance > rightDistance;
        return selected[left]->identity()->self.id < selected[right]->identity()->self.id;
    });
    std::vector<AvailableSlot> assignedSlots(selected.size());
    for (const std::size_t selectedIndex : assignmentOrder) {
        const auto motion = selected[selectedIndex]->motion();
        auto best = std::min_element(available.begin(), available.end(), [&](const auto& left, const auto& right) {
            const float leftDistance  = distanceSquared(motion->x, motion->y, left.position.x, left.position.y);
            const float rightDistance = distanceSquared(motion->x, motion->y, right.position.x, right.position.y);
            return leftDistance != rightDistance ? leftDistance < rightDistance : left.index < right.index;
        });
        assignedSlots[selectedIndex] = *best;
        available.erase(best);
    }

    // Greedy placement can reserve a nearby unit's slot for a distant unit,
    // creating unnecessary crossings. Relax pair assignments before issuing
    // orders; keep both the original slot identity and position together.
    // A bounded number of sweeps preserves quadratic command-admission cost.
    const auto travelDistance = [&](std::size_t unitIndex, WorldPosition target) {
        const auto motion = selected[unitIndex]->motion();
        return std::hypot(static_cast<double>(motion->x) - target.x, static_cast<double>(motion->y) - target.y);
    };
    for (int sweep = 0; sweep < 4; ++sweep) {
        bool changed = false;
        for (std::size_t first = 0; first < assignmentOrder.size(); ++first) {
            const auto a = assignmentOrder[first];
            for (std::size_t second = first + 1; second < assignmentOrder.size(); ++second) {
                const auto   b = assignmentOrder[second];
                const double before =
                    travelDistance(a, assignedSlots[a].position) + travelDistance(b, assignedSlots[b].position);
                const double after =
                    travelDistance(a, assignedSlots[b].position) + travelDistance(b, assignedSlots[a].position);
                if (after + 1e-7 * std::max(1.0, before) < before) {
                    std::swap(assignedSlots[a], assignedSlots[b]);
                    changed = true;
                }
            }
        }
        if (!changed) break;
    }

    FanOutReceipt receipt;
    receipt.requested = selected.size();
    receipt.orderIds.reserve(selected.size());
    std::vector<OrderComponent::Snapshot> previous;
    std::vector<std::uint64_t>            previousEpochs;
    previous.reserve(selected.size());
    for (Unit* unit : selected) {
        auto snapshot = unit->orders()->values.snapshotState();
        if (!snapshot) return Result<FanOutReceipt>::failure(snapshot.status());
        previous.push_back(std::move(snapshot).takeValue());
        previousEpochs.push_back(unit->orders()->values.membershipEpoch_);
    }
    for (std::size_t index = 0; index < selected.size(); ++index) {
        CommandSpec assigned = command;
        assigned.target      = assignedSlots[index].position;
        auto order = assigned.append ? selected[index]->orders()->values.enqueue(assigned, assignedSlots[index].index)
                                     : selected[index]->orders()->values.replace(assigned, assignedSlots[index].index);
        if (!order) {
            const Status status = order.status();
            for (std::size_t rollback = 0; rollback < selected.size(); ++rollback) {
                auto& component = selected[rollback]->orders()->values;
                auto  restored  = component.restoreState(previous[rollback]);
                if (!restored) return Result<FanOutReceipt>::failure(restored.status());
                component.membershipEpoch_ = previousEpochs[rollback];
            }
            return Result<FanOutReceipt>::failure(status);
        }
        receipt.orderIds.push_back(std::move(order).takeValue());
    }
    receipt.accepted = receipt.orderIds.size();
    return Result<FanOutReceipt>::success(std::move(receipt), Status::success(StatusCode::Applied));
}

}  // namespace eve::rts
