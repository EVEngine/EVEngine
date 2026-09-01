#include "npc_ai/SmartObject.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace eve::npc_ai {
namespace {
template <class T>
Result<T> smartFailure(DiagnosticCode code, std::string message) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), {}, {}, "npc_ai.smart_object"));
}

bool finitePosition(const std::array<double, 3>& position) {
    return std::all_of(position.begin(), position.end(), [](double value) { return std::isfinite(value); });
}
}  // namespace

Result<SmartObjectHandle> SmartObjectWorld::registerObject(SmartObjectDefinition definition) {
    if (definition.logicalId.empty() || definition.slots.empty())
        return smartFailure<SmartObjectHandle>(DiagnosticCode::InvalidArgument,
                                               "smart object logical id and slots are required");
    if (objectsByLogicalId_.contains(definition.logicalId))
        return smartFailure<SmartObjectHandle>(DiagnosticCode::AlreadyExists, "smart object logical id already exists");
    std::set<std::string> slotIds;
    for (auto& slot : definition.slots) {
        if (slot.id.empty() || slot.activity.empty() || !finitePosition(slot.position) ||
            !slotIds.insert(slot.id).second)
            return smartFailure<SmartObjectHandle>(
                DiagnosticCode::InvalidArgument,
                "smart object slots require unique ids, activities and finite positions");
        std::sort(slot.tags.begin(), slot.tags.end());
        if (std::adjacent_find(slot.tags.begin(), slot.tags.end()) != slot.tags.end())
            return smartFailure<SmartObjectHandle>(DiagnosticCode::InvalidArgument,
                                                   "smart object slot tags must be unique");
    }
    std::uint32_t index;
    if (freeObjects_.empty()) {
        index = static_cast<std::uint32_t>(objects_.size());
        objects_.push_back({});
    } else {
        index = freeObjects_.back();
        freeObjects_.pop_back();
    }
    const SmartObjectHandle handle(index, objects_[index].generation);
    const std::string       logicalId = definition.logicalId;
    objects_[index].value             = Object{std::move(definition), {}};
    objectsByLogicalId_.emplace(logicalId, handle);
    return Result<SmartObjectHandle>::success(handle);
}

Result<std::reference_wrapper<SmartObjectWorld::Object>> SmartObjectWorld::resolveObject(SmartObjectHandle handle) {
    if (!handle.isValid() || handle.index() >= objects_.size())
        return smartFailure<std::reference_wrapper<Object>>(DiagnosticCode::StaleHandle,
                                                            "smart object handle is stale");
    auto& slot = objects_[handle.index()];
    if (!slot.value || slot.generation != handle.generation())
        return smartFailure<std::reference_wrapper<Object>>(DiagnosticCode::StaleHandle,
                                                            "smart object handle is stale");
    return Result<std::reference_wrapper<Object>>::success(std::ref(*slot.value));
}

Result<std::reference_wrapper<const SmartObjectWorld::Object>> SmartObjectWorld::resolveObject(
    SmartObjectHandle handle) const {
    if (!handle.isValid() || handle.index() >= objects_.size())
        return smartFailure<std::reference_wrapper<const Object>>(DiagnosticCode::StaleHandle,
                                                                  "smart object handle is stale");
    const auto& slot = objects_[handle.index()];
    if (!slot.value || slot.generation != handle.generation())
        return smartFailure<std::reference_wrapper<const Object>>(DiagnosticCode::StaleHandle,
                                                                  "smart object handle is stale");
    return Result<std::reference_wrapper<const Object>>::success(std::cref(*slot.value));
}

Result<std::reference_wrapper<SmartObjectWorld::Claim>> SmartObjectWorld::resolveClaim(SmartObjectClaimHandle handle) {
    if (!handle.isValid() || handle.index() >= claims_.size())
        return smartFailure<std::reference_wrapper<Claim>>(DiagnosticCode::StaleHandle, "smart object claim is stale");
    auto& slot = claims_[handle.index()];
    if (!slot.value || slot.generation != handle.generation())
        return smartFailure<std::reference_wrapper<Claim>>(DiagnosticCode::StaleHandle, "smart object claim is stale");
    return Result<std::reference_wrapper<Claim>>::success(std::ref(*slot.value));
}

Result<std::reference_wrapper<const SmartObjectWorld::Claim>> SmartObjectWorld::resolveClaim(
    SmartObjectClaimHandle handle) const {
    if (!handle.isValid() || handle.index() >= claims_.size())
        return smartFailure<std::reference_wrapper<const Claim>>(DiagnosticCode::StaleHandle,
                                                                 "smart object claim is stale");
    const auto& slot = claims_[handle.index()];
    if (!slot.value || slot.generation != handle.generation())
        return smartFailure<std::reference_wrapper<const Claim>>(DiagnosticCode::StaleHandle,
                                                                 "smart object claim is stale");
    return Result<std::reference_wrapper<const Claim>>::success(std::cref(*slot.value));
}

Result<void> SmartObjectWorld::removeObject(SmartObjectHandle object, SmartObjectRemoval policy) {
    auto resolved = resolveObject(object);
    if (!resolved.ok()) return Result<void>::failure(resolved.status());
    auto& value = resolved.value().get();
    if (!value.claimsBySlot.empty() && policy == SmartObjectRemoval::RejectClaimed)
        return smartFailure<void>(DiagnosticCode::Conflict, "smart object still has live claims");
    std::vector<SmartObjectClaimHandle> claims;
    for (const auto& [slot, claim] : value.claimsBySlot) {
        (void)slot;
        claims.push_back(claim);
    }
    for (const auto claim : claims) releaseClaimUnchecked(claim);
    objectsByLogicalId_.erase(value.definition.logicalId);
    auto& slot = objects_[object.index()];
    slot.value.reset();
    const auto next = SmartObjectHandle::nextGeneration(slot.generation);
    if (next) {
        slot.generation = *next;
        freeObjects_.push_back(object.index());
    }
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<std::vector<SmartObjectCandidate>> SmartObjectWorld::query(const SmartObjectQuery& queryValue) const {
    if (queryValue.activity.empty() || !finitePosition(queryValue.origin) || !std::isfinite(queryValue.maxDistance) ||
        queryValue.maxDistance < 0.0)
        return smartFailure<std::vector<SmartObjectCandidate>>(DiagnosticCode::InvalidArgument,
                                                               "smart object query is invalid");
    std::vector<SmartObjectCandidate> result;
    for (std::uint32_t index = 0; index < objects_.size(); ++index) {
        const auto& objectSlot = objects_[index];
        if (!objectSlot.value) continue;
        for (const auto& slot : objectSlot.value->definition.slots) {
            if (slot.activity != queryValue.activity || objectSlot.value->claimsBySlot.contains(slot.id)) continue;
            const bool tagsMatch = std::all_of(
                queryValue.requiredTags.begin(), queryValue.requiredTags.end(),
                [&](const auto& tag) { return std::binary_search(slot.tags.begin(), slot.tags.end(), tag); });
            if (!tagsMatch) continue;
            const double dx       = slot.position[0] - queryValue.origin[0];
            const double dy       = slot.position[1] - queryValue.origin[1];
            const double dz       = slot.position[2] - queryValue.origin[2];
            const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (distance <= queryValue.maxDistance)
                result.push_back({SmartObjectHandle(index, objectSlot.generation),
                                  objectSlot.value->definition.logicalId, slot.id, slot.position, distance});
        }
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return std::tuple{a.distance, a.logicalId, a.slotId} < std::tuple{b.distance, b.logicalId, b.slotId};
    });
    return Result<std::vector<SmartObjectCandidate>>::success(std::move(result));
}

Result<SmartObjectClaimHandle> SmartObjectWorld::claim(AgentHandle agent, SmartObjectHandle object,
                                                       std::string_view slotId, std::uint64_t currentTick,
                                                       std::uint64_t leaseTicks) {
    if (!agent.isValid() || slotId.empty() || leaseTicks == 0 || currentTick > UINT64_MAX - leaseTicks)
        return smartFailure<SmartObjectClaimHandle>(DiagnosticCode::InvalidArgument, "smart object claim is invalid");
    auto resolved = resolveObject(object);
    if (!resolved.ok()) return Result<SmartObjectClaimHandle>::failure(resolved.status());
    auto&      value      = resolved.value().get();
    const auto definition = std::find_if(value.definition.slots.begin(), value.definition.slots.end(),
                                         [&](const auto& slot) { return slot.id == slotId; });
    if (definition == value.definition.slots.end())
        return smartFailure<SmartObjectClaimHandle>(DiagnosticCode::NotFound, "smart object slot was not found");
    if (value.claimsBySlot.contains(std::string(slotId)))
        return smartFailure<SmartObjectClaimHandle>(DiagnosticCode::Conflict, "smart object slot is already claimed");
    std::uint32_t index;
    if (freeClaims_.empty()) {
        index = static_cast<std::uint32_t>(claims_.size());
        claims_.push_back({});
    } else {
        index = freeClaims_.back();
        freeClaims_.pop_back();
    }
    const SmartObjectClaimHandle handle(index, claims_[index].generation);
    claims_[index].value = Claim{object, std::string(slotId), agent, currentTick + leaseTicks};
    value.claimsBySlot.emplace(slotId, handle);
    return Result<SmartObjectClaimHandle>::success(handle);
}

Result<void> SmartObjectWorld::renew(SmartObjectClaimHandle claimHandle, AgentHandle agent, std::uint64_t currentTick,
                                     std::uint64_t leaseTicks) {
    if (leaseTicks == 0 || currentTick > UINT64_MAX - leaseTicks)
        return smartFailure<void>(DiagnosticCode::InvalidArgument, "smart object lease duration is invalid");
    auto resolved = resolveClaim(claimHandle);
    if (!resolved.ok()) return Result<void>::failure(resolved.status());
    auto& claim = resolved.value().get();
    if (claim.agent != agent) return smartFailure<void>(DiagnosticCode::Conflict, "smart object claim owner mismatch");
    if (claim.expiresAtTick <= currentTick)
        return smartFailure<void>(DiagnosticCode::PreconditionViolation, "smart object claim already expired");
    claim.expiresAtTick = currentTick + leaseTicks;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> SmartObjectWorld::release(SmartObjectClaimHandle claimHandle, AgentHandle agent) {
    auto resolved = resolveClaim(claimHandle);
    if (!resolved.ok()) return Result<void>::failure(resolved.status());
    if (resolved.value().get().agent != agent)
        return smartFailure<void>(DiagnosticCode::Conflict, "smart object claim owner mismatch");
    releaseClaimUnchecked(claimHandle);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

void SmartObjectWorld::releaseClaimUnchecked(SmartObjectClaimHandle handle) noexcept {
    if (!handle.isValid() || handle.index() >= claims_.size()) return;
    auto& slot = claims_[handle.index()];
    if (!slot.value || slot.generation != handle.generation()) return;
    auto object = resolveObject(slot.value->object);
    if (object.ok()) object.value().get().claimsBySlot.erase(slot.value->slotId);
    slot.value.reset();
    const auto next = SmartObjectClaimHandle::nextGeneration(slot.generation);
    if (next) {
        slot.generation = *next;
        freeClaims_.push_back(handle.index());
    }
}

Result<std::uint32_t> SmartObjectWorld::releaseAgentClaims(AgentHandle agent) {
    if (!agent.isValid())
        return smartFailure<std::uint32_t>(DiagnosticCode::InvalidArgument, "agent handle is invalid");
    std::vector<SmartObjectClaimHandle> owned;
    for (std::uint32_t index = 0; index < claims_.size(); ++index)
        if (claims_[index].value && claims_[index].value->agent == agent)
            owned.emplace_back(index, claims_[index].generation);
    for (const auto claim : owned) releaseClaimUnchecked(claim);
    return Result<std::uint32_t>::success(static_cast<std::uint32_t>(owned.size()));
}

Result<SmartObjectExpirationReport> SmartObjectWorld::expire(std::uint64_t currentTick) {
    std::vector<SmartObjectClaimHandle> expired;
    for (std::uint32_t index = 0; index < claims_.size(); ++index)
        if (claims_[index].value && claims_[index].value->expiresAtTick <= currentTick)
            expired.emplace_back(index, claims_[index].generation);
    for (const auto claim : expired) releaseClaimUnchecked(claim);
    return Result<SmartObjectExpirationReport>::success({static_cast<std::uint32_t>(expired.size())});
}

Result<SmartObjectClaimSnapshot> SmartObjectWorld::snapshot(SmartObjectClaimHandle claimHandle) const {
    auto resolved = resolveClaim(claimHandle);
    if (!resolved.ok()) return Result<SmartObjectClaimSnapshot>::failure(resolved.status());
    const auto& claim = resolved.value().get();
    return Result<SmartObjectClaimSnapshot>::success(
        {claimHandle, claim.object, claim.slotId, claim.agent, claim.expiresAtTick});
}

}  // namespace eve::npc_ai
