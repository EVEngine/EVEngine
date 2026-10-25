#include <algorithm>
#include <cmath>
#include "common/Profile.h"
#include "rts/RTSSystemMovementInternal.h"
namespace eve::rts {
using systems_internal::distanceSquared;
using systems_internal::isSameHandle;
using systems_internal::readCurrent;
Result<std::size_t> WorkerAssignmentSystem::step() {
    EV_PROFILE_MODULE("rts", "WorkerAssignmentSystem::step");
    struct Candidate {
        ResourceNode* node     = nullptr;
        float         distance = 0.0f;
    };
    std::size_t assigned = 0;
    auto        workers  = ecs::View<Unit, Unit::Identity, Unit::Motion, Unit::Orders, Unit::Worker>();
    for (auto it = workers.begin(); it != workers.end(); ++it) {
        auto [identity, motion, orders, worker] = *it;
        auto* unit = identity == nullptr ? nullptr : dynamic_cast<Unit*>(ecs::try_get(identity->self));
        if (unit == nullptr || &*unit->identity() != identity) continue;
        if (!worker->autoAssign || worker->capacity <= 0.0f || worker->gatherRate <= 0.0f ||
            worker->resourceType.empty() || !orders->values.empty() || !worker->dropoff.isBound() ||
            worker->dropoff.isStale())
            continue;

        std::vector<Candidate> candidates;
        auto nodes = ecs::View<ResourceNode, ResourceNode::Identity, ResourceNode::Position, ResourceNode::Stock,
                               ResourceNode::Harvest>();
        for (auto nodeIt = nodes.begin(); nodeIt != nodes.end(); ++nodeIt) {
            auto [nodeIdentity, position, stock, harvest] = *nodeIt;
            auto* node =
                nodeIdentity == nullptr ? nullptr : dynamic_cast<ResourceNode*>(ecs::try_get(nodeIdentity->self));
            if (node == nullptr || stock->resourceType != worker->resourceType ||
                (!stock->infinite && stock->remaining <= 0.0f))
                continue;
            harvest->workers.erase(
                std::remove_if(harvest->workers.begin(), harvest->workers.end(),
                               [](const ecs::EntityHandle& handle) { return ecs::try_get(handle) == nullptr; }),
                harvest->workers.end());
            const auto handle     = ecs::handle_of(unit);
            const bool registered = std::any_of(harvest->workers.begin(), harvest->workers.end(),
                                                [&](const auto& existing) { return isSameHandle(existing, handle); });
            if (harvest->workers.size() >= harvest->capacity && !registered) continue;
            candidates.push_back({node, distanceSquared(motion->x, motion->y, position->x, position->y)});
        }
        std::sort(candidates.begin(), candidates.end(), [](const Candidate& left, const Candidate& right) {
            if (left.distance != right.distance) return left.distance < right.distance;
            return left.node->identity()->self.id < right.node->identity()->self.id;
        });
        if (candidates.empty()) continue;
        ResourceNode* node = candidates.front().node;
        auto          link = ResourceNodeLink::bind(ecs::handle_of(node));
        if (!link) return Result<std::size_t>::failure(link.status());
        worker->resourceNode = std::move(link).takeValue();
        const auto handle    = ecs::handle_of(unit);
        if (std::none_of(node->harvest()->workers.begin(), node->harvest()->workers.end(),
                         [&](const auto& existing) { return isSameHandle(existing, handle); }))
            node->harvest()->workers.push_back(handle);
        CommandSpec command;
        command.kind         = OrderKind::Gather;
        command.target       = {node->position()->x, node->position()->y};
        command.targetEntity = ecs::handle_of(node);
        auto queued          = orders->values.enqueue(command);
        if (!queued) return Result<std::size_t>::failure(queued.status());
        std::move(queued).takeValue();
        ++assigned;
    }
    return Result<std::size_t>::success(assigned,
                                        Status::success(assigned == 0 ? StatusCode::NoOp : StatusCode::Applied));
}

Result<std::size_t> MiningSystem::step(const SimulationStep& step, const ResourceCredit& credit) {
    EV_PROFILE_MODULE("rts", "MiningSystem::step");
    if (step.delta.nanoseconds() < 0 || !credit)
        return Result<std::size_t>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument,
                              "RTS mining requires non-negative time and a resource credit callback", "mining"));
    std::size_t processed = 0;
    auto        workers   = ecs::View<Unit, Unit::Identity, Unit::Motion, Unit::Orders, Unit::Worker>();
    for (auto it = workers.begin(); it != workers.end(); ++it) {
        auto [identity, motion, orders, worker] = *it;
        auto* unit = identity == nullptr ? nullptr : dynamic_cast<Unit*>(ecs::try_get(identity->self));
        if (unit == nullptr || &*unit->identity() != identity) continue;
        auto current = readCurrent(orders->values);
        if (!current) return Result<std::size_t>::failure(current.status());
        auto record = std::move(current).takeValue();
        if (!record) continue;

        if (record->kind == OrderKind::Gather && motion->arrived) {
            auto* node = dynamic_cast<ResourceNode*>(worker->resourceNode.resolve());
            if (node == nullptr) {
                auto failed = orders->values.fail(record->id, "resource node is stale");
                if (!failed) return Result<std::size_t>::failure(failed.status());
                worker->resourceNode.reset();
                continue;
            }
            const float room   = std::max(0.0f, worker->capacity - worker->cargo);
            float       amount = std::min(room, worker->gatherRate * static_cast<float>(step.delta.seconds()));
            if (!node->stock()->infinite) amount = std::min(amount, node->stock()->remaining);
            worker->cargo += amount;
            if (!node->stock()->infinite) node->stock()->remaining -= amount;
            if (worker->cargo >= worker->capacity || (!node->stock()->infinite && node->stock()->remaining <= 0.0f)) {
                auto* dropoff = dynamic_cast<Building*>(worker->dropoff.resolve());
                if (dropoff == nullptr) {
                    auto failed = orders->values.fail(record->id, "dropoff is stale");
                    if (!failed) return Result<std::size_t>::failure(failed.status());
                    continue;
                }
                auto completed = orders->values.complete(record->id);
                if (!completed) return Result<std::size_t>::failure(completed.status());
                CommandSpec returning;
                returning.kind         = OrderKind::ReturnCargo;
                returning.target       = {dropoff->placement()->worldX, dropoff->placement()->worldY};
                returning.targetEntity = ecs::handle_of(dropoff);
                auto queued            = orders->values.enqueue(returning);
                if (!queued) return Result<std::size_t>::failure(queued.status());
                std::move(queued).takeValue();
            }
            ++processed;
        } else if (record->kind == OrderKind::ReturnCargo && motion->arrived && worker->cargo >= 1.0f) {
            const auto whole = static_cast<std::int64_t>(std::floor(worker->cargo));
            auto       cost  = resource::CostSpec::single(worker->resourceType, whole);
            if (!cost) return Result<std::size_t>::failure(cost.status());
            auto credited = credit(*unit, cost.value());
            if (!credited) return Result<std::size_t>::failure(credited.status());
            worker->cargo -= static_cast<float>(whole);
            auto completed = orders->values.complete(record->id);
            if (!completed) return Result<std::size_t>::failure(completed.status());
            auto* node = dynamic_cast<ResourceNode*>(worker->resourceNode.resolve());
            if (node != nullptr && (node->stock()->infinite || node->stock()->remaining > 0.0f)) {
                CommandSpec gather;
                gather.kind         = OrderKind::Gather;
                gather.target       = {node->position()->x, node->position()->y};
                gather.targetEntity = ecs::handle_of(node);
                auto queued         = orders->values.enqueue(gather);
                if (!queued) return Result<std::size_t>::failure(queued.status());
                std::move(queued).takeValue();
            } else {
                worker->resourceNode.reset();
            }
            ++processed;
        }
    }
    return Result<std::size_t>::success(processed,
                                        Status::success(processed == 0 ? StatusCode::NoOp : StatusCode::Applied));
}

}  // namespace eve::rts
