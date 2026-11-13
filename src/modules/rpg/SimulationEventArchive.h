#pragma once
/** @file SimulationEventArchive.h
 * @brief rpg module-interface: canonical frozen five-queue file participant and detached reference rebinding.
 * @remarks No gameplay execution, registry publication, scripts, tools or acknowledgement state is introduced.
 */
#include <array>
#include <memory>
#include <span>
#include <string>
#include "common/Export.h"
#include "common/Snapshot.h"
#include "rpg/SimulationEvents.h"

namespace ecs {
class Table;
}
namespace eve::rpg {
/**
 * @brief Immutable eve.rpg.simulation-events v1 participant; FIFO order is preserved separately in each queue.
 * @ownership Holds values/stable subjects only, never source actors, Tables, registries or callbacks.
 * @thread Simulation/content owner thread; native capture/restore require the actor owner thread.
 * @remarks Boundary tick is a capture boundary, not an inferred timestamp for each event. This participant does
 * not certify content execution availability, consumer acknowledgements, frame phases or a complete game save.
 */
class EVENGINE_API_PLATFORM SimulationEventArchive {
public:
    /** @brief Release local frozen facts. */
    ~SimulationEventArchive();
    /** @brief Transfer frozen facts. */
    SimulationEventArchive(SimulationEventArchive&&) noexcept;
    /** @brief Release previous facts and transfer frozen facts. */
    SimulationEventArchive& operator=(SimulationEventArchive&&) noexcept;
    SimulationEventArchive(const SimulationEventArchive&)            = delete;
    SimulationEventArchive& operator=(const SimulationEventArchive&) = delete;
    /** @brief Freeze all five queues at the caller-supplied joint boundary without consuming them.
     * @return Detached facts or a checked anonymous, mixed-world, identity or event-shape failure.
     * @remarks Every specified reference requires its identity frozen at reference construction; no backfill.
     * All specified references must share one simulation Table affiliation, even after that Table has retired.
     * @reentrancy No callbacks, registry reads, locks or gameplay mutation.
     * @cost Copy every queued fact and deduplicate subjects/content names once per explicit capture.
     * @throws std::bad_alloc Source queues/world remain unchanged. */
    [[nodiscard]] static Result<SimulationEventArchive> capture(const detail::SimulationEvents& events,
                                                                PersistentId saveEpoch, Revision revision,
                                                                SimulationTick tick);
    /** @brief Copy input, verify the canonical envelope and strictly admit all queue/reference fields.
     * @return Detached facts or digest/schema/version/epoch/shape failure; unknown fields/versions reject.
     * @remarks Initial v1 has no inferred legacy migration. No live registry or world is consulted.
     * @reentrancy Only the synchronous digest callback runs external code; input is copied first, without locks.
     * @cost Whole envelope copy/hash plus one bounded-depth traversal per imported participant.
     * @throws std::bad_alloc No live state is modified. */
    [[nodiscard]] static Result<SimulationEventArchive> parse(const SnapshotEnvelope& input, PersistentId saveEpoch,
                                                              const SnapshotHashProvider& hashProvider);
    /** @brief Seal frozen facts with their original epoch/revision/tick; the source may already be destroyed.
     * @return Integrity-covered envelope or failure retaining these facts for retry.
     * @reentrancy Same-state seal/restore reentry rejects. Local shared pinning tolerates owner moves in callbacks.
     * @cost Full payload copy and canonical hashing once per emitted file participant.
     * @throws std::bad_alloc Frozen facts and source queues remain unchanged. */
    [[nodiscard]] Result<SnapshotEnvelope> seal(const SnapshotHashProvider& hashProvider) const;
    /** @brief Prepare all queue values against registered destination actors; never install or execute them.
     * @return Owning candidate queues, or a checked missing/duplicate/stale identity failure with no publication.
     * @ownership Linked references borrow destination incarnations. Retired references keep history and an empty
     * native handle even if a live destination actor has the same subject; they never resurrect by identity lookup.
     * @thread Destination owner thread outside Views; no callbacks. The caller owns final whole-world publication.
     * @cost Scan/deduplicate the RPG roster when live subjects exist, then rebind each queue reference per attempt.
     * @throws std::bad_alloc Destination actors and existing event owners remain unchanged. */
    [[nodiscard]] Result<detail::SimulationEvents> prepareRestore(ecs::Table& destination) const;
    /** @brief Read queue lengths in casts/ticks/changes/vitals/levels order; zero after move.
     * @ownership Derived from immutable admitted payload; no actor lookup or allocation. */
    [[nodiscard]] std::array<std::size_t, 5> queueSizes() const noexcept;
    /** @brief Saved joint boundary identity, or nil after move. */
    [[nodiscard]] PersistentId saveEpoch() const noexcept;
    /** @brief Saved participant revision, or zero after move. */
    [[nodiscard]] Revision revision() const noexcept;
    /** @brief Saved boundary tick, or zero after move. */
    [[nodiscard]] SimulationTick tick() const noexcept;
    /** @brief Borrow sorted unique linked subjects until archive move/destruction; retired subjects are excluded. */
    [[nodiscard]] std::span<const SubjectRef> linkedSubjects() const noexcept;
    /** @brief Borrow sorted historical skill names; the coordinator owns content-closure admission. */
    [[nodiscard]] std::span<const std::string> referencedSkills() const noexcept;
    /** @brief Borrow sorted historical effect names; no latest-registry substitution is performed. */
    [[nodiscard]] std::span<const std::string> referencedEffects() const noexcept;

private:
    struct Impl;
    explicit SimulationEventArchive(std::shared_ptr<Impl> impl);
    std::shared_ptr<Impl> impl_;
};
}  // namespace eve::rpg
