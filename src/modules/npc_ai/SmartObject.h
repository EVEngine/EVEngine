#pragma once

#include "common/Result.h"
#include "common/RuntimeHandle.h"
#include "npc_ai/NpcAi.h"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace eve::npc_ai {

struct SmartObjectHandleTag {};
struct SmartObjectClaimHandleTag {};
using SmartObjectHandle      = RuntimeHandle<SmartObjectHandleTag>;
using SmartObjectClaimHandle = RuntimeHandle<SmartObjectClaimHandleTag>;

enum class SmartObjectRemoval : std::uint8_t { RejectClaimed, CancelClaims };

struct SmartObjectSlotDefinition {
    std::string              id;
    std::string              activity;
    std::vector<std::string> tags;
    std::array<double, 3>    position{};
};

struct SmartObjectDefinition {
    std::string                            logicalId;
    std::vector<SmartObjectSlotDefinition> slots;
};

struct SmartObjectQuery {
    std::string              activity;
    std::vector<std::string> requiredTags;
    std::array<double, 3>    origin{};
    double                   maxDistance = 0.0;
};

struct SmartObjectCandidate {
    SmartObjectHandle     object;
    std::string           logicalId;
    std::string           slotId;
    std::array<double, 3> position{};
    double                distance = 0.0;
};

struct SmartObjectClaimSnapshot {
    SmartObjectClaimHandle claim;
    SmartObjectHandle      object;
    std::string            slotId;
    AgentHandle            agent;
    std::uint64_t          expiresAtTick = 0;
};

struct SmartObjectExpirationReport {
    std::uint32_t claimsExpired = 0;
};

/**
 * @brief Authoritative registry and lease manager for reservable world interactions.
 * @remarks All handles are process-local and generation checked. The registry owns
 * definitions and claims. Call releaseAgentClaims before destroying an owning NPC,
 * or rely on lease expiry as bounded stale cleanup. Removing an object either rejects
 * live claims or explicitly cancels them according to SmartObjectRemoval.
 * @thread Simulation-thread-affine; no synchronization is provided.
 * @reentrancy Methods do not invoke external callbacks.
 */
class SmartObjectWorld {
public:
    /** @brief Validates and atomically registers one uniquely named object. */
    [[nodiscard]] Result<SmartObjectHandle> registerObject(SmartObjectDefinition definition);
    /** @brief Removes an object using the explicit live-claim policy. */
    [[nodiscard]] Result<void> removeObject(SmartObjectHandle object, SmartObjectRemoval policy);
    /** @brief Returns unclaimed matching slots in deterministic distance/name order. */
    [[nodiscard]] Result<std::vector<SmartObjectCandidate>> query(const SmartObjectQuery& query) const;
    /** @brief Atomically leases one free slot to an agent for a bounded tick duration. */
    [[nodiscard]] Result<SmartObjectClaimHandle> claim(AgentHandle agent, SmartObjectHandle object,
                                                       std::string_view slotId, std::uint64_t currentTick,
                                                       std::uint64_t leaseTicks);
    /** @brief Extends a live claim after verifying its owner. */
    [[nodiscard]] Result<void> renew(SmartObjectClaimHandle claim, AgentHandle agent, std::uint64_t currentTick,
                                     std::uint64_t leaseTicks);
    /** @brief Releases a live claim after verifying its owner. */
    [[nodiscard]] Result<void> release(SmartObjectClaimHandle claim, AgentHandle agent);
    /** @brief Releases every live claim owned by an agent before agent destruction. */
    [[nodiscard]] Result<std::uint32_t> releaseAgentClaims(AgentHandle agent);
    /** @brief Expires all elapsed leases in stable claim-slot order. */
    [[nodiscard]] Result<SmartObjectExpirationReport> expire(std::uint64_t currentTick);
    /** @brief Returns an owning claim snapshot, or StaleHandle. */
    [[nodiscard]] Result<SmartObjectClaimSnapshot> snapshot(SmartObjectClaimHandle claim) const;

private:
    struct Object {
        SmartObjectDefinition                         definition;
        std::map<std::string, SmartObjectClaimHandle> claimsBySlot;
    };
    struct Claim {
        SmartObjectHandle object;
        std::string       slotId;
        AgentHandle       agent;
        std::uint64_t     expiresAtTick = 0;
    };
    struct ObjectSlot {
        std::uint32_t         generation = 1;
        std::optional<Object> value;
    };
    struct ClaimSlot {
        std::uint32_t        generation = 1;
        std::optional<Claim> value;
    };

    [[nodiscard]] Result<std::reference_wrapper<Object>>       resolveObject(SmartObjectHandle handle);
    [[nodiscard]] Result<std::reference_wrapper<const Object>> resolveObject(SmartObjectHandle handle) const;
    [[nodiscard]] Result<std::reference_wrapper<Claim>>        resolveClaim(SmartObjectClaimHandle handle);
    [[nodiscard]] Result<std::reference_wrapper<const Claim>>  resolveClaim(SmartObjectClaimHandle handle) const;
    void releaseClaimUnchecked(SmartObjectClaimHandle handle) noexcept;

    std::vector<ObjectSlot>                  objects_;
    std::vector<std::uint32_t>               freeObjects_;
    std::vector<ClaimSlot>                   claims_;
    std::vector<std::uint32_t>               freeClaims_;
    std::map<std::string, SmartObjectHandle> objectsByLogicalId_;
};

}  // namespace eve::npc_ai
