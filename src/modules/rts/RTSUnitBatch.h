#pragma once
/** @file RTSUnitBatch.h
 * @brief rts module-interface: owning unit recipes and explicit atomic Crowd admission.
 * @remarks No capability registration, events, scripts, tools or persistent format. ECS roots remain RTS-owned.
 */
#include <vector>
#include "common/SubjectRef.h"
#include "rts/RTSArchetype.h"
namespace eve::rts {
class RTS;
struct CommandSpec;
/** @brief One-use prepared removal of generation-qualified owned units.
 * @ownership Retains the real Table, weakly observes RTS, and owns identity copies.
 * @thread Simulation owner thread outside Views. @reentrancy No callbacks.
 * @lifetime Cancellation releases only local memory; token is not serializable.
 */
class EVENGINE_API_DOMAINS PreparedUnitRemoval {
public:
    /** @brief Cancel unpublished local work. */
    ~PreparedUnitRemoval();
    /** @brief Transfer the one-use candidate. */
    PreparedUnitRemoval(PreparedUnitRemoval&&) noexcept;
    /** @brief Cancel the old candidate and transfer the new one. */
    PreparedUnitRemoval& operator=(PreparedUnitRemoval&&) noexcept;
    PreparedUnitRemoval(const PreparedUnitRemoval&)            = delete;
    PreparedUnitRemoval& operator=(const PreparedUnitRemoval&) = delete;
    /** @brief Remove captured incarnations atomically; already absent units count as completed.
     * @return Completed identity count, or failure leaving domain/projections intact.
     * @details Consumes the token on every outcome. Replacements are never removed.
     * Units carrying occupants are rejected before publication.
     * @cost One reference sweep and bounded Crowd work, amortized over the captured batch.
     */
    [[nodiscard]] Result<std::size_t> commit();

private:
    friend class RTS;
    struct Impl;
    explicit PreparedUnitRemoval(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
/** @brief Owning request; position is canonical World2D and recipe pins exact asset revisions. */
struct UnitBatchRequest {
    SubjectRef            subject;
    PreparedUnitArchetype recipe;
    float                 x = 0, y = 0;
    /** @brief Optional canonical initial command; prepare copies its value, retaining no caller alias. */
    std::shared_ptr<const CommandSpec> initialCommand{};
};
/** @brief A blocked placement publishes nothing; committed subjects belong to the RTS facade. */
enum class UnitBatchState { Committed, Blocked };
/** @brief Owning batch result, independent of ECS compact storage. */
struct UnitBatchReceipt {
    UnitBatchState          state = UnitBatchState::Blocked;
    std::vector<SubjectRef> subjects;
    Status placementStatus;  ///< Original backend diagnostic for blocked geometry, capacity or work quota.
};
/** @brief One-use native unit/Crowd transaction; preparation creates no ECS roots.
 * @ownership Retains the real domain Table and recipes, weakly observes RTS and Crowd.
 * @thread Simulation owner thread outside Views. @reentrancy No callbacks or nested simulation.
 * @lifetime Cancellation only releases local prepared memory. Not serializable; restore must prepare again.
 */
class EVENGINE_API_DOMAINS PreparedUnitBatch {
public:
    /** @brief Release unpublished local resources. */
    ~PreparedUnitBatch();
    /** @brief Transfer the one-use transaction. */
    PreparedUnitBatch(PreparedUnitBatch&&) noexcept;
    /** @brief Cancel the old candidate and transfer the transaction. */
    PreparedUnitBatch& operator=(PreparedUnitBatch&&) noexcept;
    PreparedUnitBatch(const PreparedUnitBatch&)            = delete;
    PreparedUnitBatch& operator=(const PreparedUnitBatch&) = delete;
    /** @brief Commit all units, faction memberships, weapons and Crowd agents, or publish none.
     * @return Committed or retryable Blocked receipt; stale/invalid input is a structured failure.
     * @details Consumes the token even on failure. Uses RejectOverlap and never moves existing neighbors.
     * Exceptions propagate after local rollback. No Views or callbacks may observe intermediate roots.
     * @cost Linear component initialization plus one bounded Crowd batch snapshot; amortized over the batch.
     */
    [[nodiscard]] Result<UnitBatchReceipt> commit();

private:
    friend class RTS;
    struct Impl;
    explicit PreparedUnitBatch(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::rts
