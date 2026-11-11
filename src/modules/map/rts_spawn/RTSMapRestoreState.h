#pragma once
/** @file RTSMapRestoreState.h
 * @brief map_rts_spawn module-interface: authority-free native checkpoint admission before world construction.
 * @remarks No new persistence schema, ECS model, listeners, services or retained callbacks.
 */
#include <memory>
#include "common/BorrowedRef.h"
#include "common/Export.h"
#include "common/Snapshot.h"
namespace eve::spawn {
class SpawnRestoreState;
}
namespace eve::map_rts_spawn {
class RTSMapCheckpoint;
/** @brief Own immutable joined facts and admitted core for independent isolated materialization attempts.
 * @ownership Shares detached checkpoint storage; neither source nor destination world, registry or hash is retained.
 * @thread Content/simulation owner thread; prepare may invoke only the explicit synchronous digest callback.
 * No locks, ECS mutation, time advancement, listeners or providers are involved.
 */
class EVENGINE_API_ORCHESTRATION RTSMapRestoreState final {
public:
    /** @brief Discard authority-free CPU facts. */
    ~RTSMapRestoreState();
    /** @brief Transfer admitted data. */
    RTSMapRestoreState(RTSMapRestoreState&&) noexcept;
    /** @brief Replace admitted data by move. */
    RTSMapRestoreState& operator=(RTSMapRestoreState&&) noexcept;
    RTSMapRestoreState(const RTSMapRestoreState&)            = delete;
    RTSMapRestoreState& operator=(const RTSMapRestoreState&) = delete;
    /** @brief Admit the exact core, future content and joined native relations before any target world exists.
     * @param saved Immutable owning checkpoint, pinned before any callback.
     * @param hashProvider Explicit digest borrowed only during this call; never retained.
     * @return CPU state or incomplete/mismatched checkpoint, digest or content failure; no world is touched.
     * @remarks Callback code cannot mutate or move the shared immutable checkpoint. Source owners may already be gone.
     * @cost Core sealing/decoding, pinned template compilation and relation indexing once per restore admission.
     * @throws std::bad_alloc No live state or capability changed; the same checkpoint remains retryable.
     */
    [[nodiscard]] static Result<RTSMapRestoreState> prepare(std::shared_ptr<const RTSMapCheckpoint> saved,
                                                            const SnapshotHashProvider&             hashProvider);
    /** @brief Share immutable original joined facts; empty after move. No live authority is issued. */
    [[nodiscard]] std::shared_ptr<const RTSMapCheckpoint> checkpoint() const noexcept;
    /** @brief Borrow admitted core until move/destruction; empty after move. No callbacks or allocations. */
    [[nodiscard]] OptionalRef<const spawn::SpawnRestoreState> core() const noexcept;

private:
    struct Impl;
    explicit RTSMapRestoreState(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::map_rts_spawn
