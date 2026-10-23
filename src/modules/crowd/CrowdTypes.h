#pragma once

#include <memory>
#include <vector>
#include "common/RuntimeHandle.h"
#include "common/StrongUint64.h"

#include <cstdint>

namespace eve::crowd {
/** @brief Logical grid destination; canonical request sets sort lexicographically and remove duplicates. */
struct FlowGoal {
    int x = 0, y = 0;
    /** @brief Compare exact cell coordinates, independently of source order. */
    friend auto operator<=>(const FlowGoal&, const FlowGoal&) = default;
};
/** @brief Domain tags for world-owned navigation identities. */
struct NavigationSurfaceTag {};
/** @brief Movement policy identity; the provider owns its stable definition. */
struct MovementClassTag {};
using NavigationSurfaceId = detail::StrongUint64<NavigationSurfaceTag>;
using MovementClassId     = detail::StrongUint64<MovementClassTag>;
/** @brief Agent-owned selection of a provider-defined movement policy.
 * Zero identity and revision mean compatibility mode; otherwise both must be positive.
 * A binding does not own a provider or a field. Stale/missing policies suspend Flow steering.
 */
struct MovementBinding {
    MovementClassId movement;
    std::uint64_t   revision                                                   = 0;
    friend bool     operator==(const MovementBinding&, const MovementBinding&) = default;
};
/** @brief Provenance of one immutable navigation projection. */
struct NavigationSurfaceStamp {
    NavigationSurfaceId surface;
    MovementClassId     movement;
    std::uint64_t       surfaceRevision = 0, movementRevision = 0, costModelRevision = 0;
    float               clearanceRadius                                                          = 0.f;
    friend bool         operator==(const NavigationSurfaceStamp&, const NavigationSurfaceStamp&) = default;
};
/** @brief Increment when the navigation construction algorithm changes its observable output. */
inline constexpr std::uint32_t flowBuildAlgorithmVersion = 1;
class Crowd;
class CrowdField;
/** @brief Owning immutable field lease; copies pin storage, never the Crowd provider.
 * @thread Copy/release/query on the simulation owner thread for deterministic eviction.
 * Owner/input/generation identify in-process use. Destruction releases the pin; never persist this value.
 */
class FlowLease {
    friend class Crowd;
    std::shared_ptr<const CrowdField> field_;
    std::shared_ptr<const void>       pin_;
    std::uint64_t                     owner_ = 0, input_ = 0, generation_ = 0;
    std::uint32_t                     algorithm_ = flowBuildAlgorithmVersion;

public:
    /** @brief Construct an unbound lease, selecting compatibility default-field steering. */
    FlowLease() = default;
    /** @brief Observe the non-reused owner-local generation; zero means unbound. */
    [[nodiscard]] std::uint64_t generation() const noexcept { return generation_; }
};
/** @brief Retained navigation-storage accounting; excludes allocator bookkeeping and pending builds. */
struct FlowCacheObservation {
    std::size_t   entries       = 0;
    std::uint64_t residentBytes = 0, budgetBytes = 0;
};
/** @brief Owning adapter input: raw physical terrain and separately inflated navigation costs.
 * @ownership Copies are values; prepare never retains caller arrays.
 * @thread Prepare/publish on the simulation owner thread; source ownership remains with the map provider.
 */
struct NavigationSurfaceSnapshot {
    NavigationSurfaceStamp stamp;
    int                    width = 0, height = 0;
    float                  cellSize = 0.f, originX = 0.f, originY = 0.f;
    std::vector<float>     terrainCosts, navigationCosts;
    bool                   diagonal = true;  ///< Eight-neighbor navigation when true, four-neighbor otherwise.
    bool                   destinationEntryCost = false;  ///< Forward movement charges destination costs when true.
};

/** @brief Owner-local registry identity for a Crowd agent, independent of dense storage. */
struct CrowdAgentTag {};
/** @brief Generation-qualified runtime agent identity; never persist its packed representation. */
using AgentHandle = RuntimeHandle<CrowdAgentTag>;

/**
 * @brief Agent identity qualified by the Crowd instance that owns its registry.
 * @ownership Value only; it neither owns nor keeps the Crowd alive.
 * @lifetime Survives swap-pop of other agents; invalid after removal, clear, restore or owner destruction.
 * @thread Resolve on the simulation owner thread. @reentrancy No callbacks.
 */
struct AgentRef {
    AgentHandle   handle;
    std::uint64_t owner = 0;

    /** @brief Whether the value contains an owner and usable coordinates; does not prove liveness. */
    [[nodiscard]] bool isValid() const noexcept { return owner != 0 && handle.isValid(); }
    /** @brief Compare exact owner, slot and generation identity. */
    friend bool operator==(const AgentRef&, const AgentRef&) noexcept = default;
};

}  // namespace eve::crowd
