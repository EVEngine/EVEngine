#pragma once
/** @file RTSMapCheckpoint.h
 * @brief map_rts_spawn module-interface: detached joined map/core/provenance/native-root/lifecycle facts.
 * @remarks No persistent schema or restore authorization. Crowd/action/provider state remain separate participants.
 */
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include "common/Export.h"
#include "common/Snapshot.h"
namespace eve::spawn {
class SpawnCheckpoint;
struct LifecycleInput;
}  // namespace eve::spawn
namespace eve::rts_spawn {
class RTSSpawnCapture;
}
namespace eve::rts {
struct RTSStateSnapshot;
struct RTSFrameDamageEvent;
}  // namespace eve::rts
namespace eve::map {
class MapGridState;
}
namespace eve::map_spawn {
class MapSpawnAssets;
}
namespace eve::map_rts_spawn {
class RTSMapSession;
/** @brief Frozen participants from one quiescent capture boundary, owning all copied facts independently.
 * @ownership Never retains a source session, facade, Table, Map, provider lease or callback.
 * @thread Capture on the simulation owner thread; detached reads invoke no callbacks.
 * @remarks Native root/weapon facts and scheduler may straddle a retained lifecycle handoff; both event stages
 * are preserved explicitly. Caller still coordinates external domain/backend time. This is not a complete game
 * checkpoint: Crowd/action, external clock/frame-cache context, non-generated weapon/content closure and other
 * game participants are not sealed here.
 */
class EVENGINE_API_ORCHESTRATION RTSMapCheckpoint final {
public:
    /** @brief Release detached facts only. */
    ~RTSMapCheckpoint();
    /** @brief Transfer detached facts. */
    RTSMapCheckpoint(RTSMapCheckpoint&&) noexcept;
    /** @brief Replace detached facts by move. */
    RTSMapCheckpoint& operator=(RTSMapCheckpoint&&) noexcept;
    RTSMapCheckpoint(const RTSMapCheckpoint&)            = delete;
    RTSMapCheckpoint& operator=(const RTSMapCheckpoint&) = delete;
    /** @brief Joint identity, or nil after move. */
    [[nodiscard]] PersistentId saveEpoch() const noexcept;
    /** @brief Captured revision, or zero after move. */
    [[nodiscard]] Revision revision() const noexcept;
    /** @brief Actual core consumed tick at capture, or zero after move; not an external domain clock. */
    [[nodiscard]] SimulationTick coreTick() const noexcept;
    /** @brief Borrow detached core until move/destruction, or empty after move. */
    [[nodiscard]] std::optional<std::reference_wrapper<const spawn::SpawnCheckpoint>> core() const noexcept;
    /** @brief Borrow detached source/content/weapon facts until move/destruction, or empty after move. */
    [[nodiscard]] std::optional<std::reference_wrapper<const rts_spawn::RTSSpawnCapture>> provenance() const noexcept;
    /** @brief Borrow detached canonical RTS root snapshot until move/destruction, or empty after move. */
    [[nodiscard]] std::optional<std::reference_wrapper<const rts::RTSStateSnapshot>> roots() const noexcept;
    /** @brief Borrow exact retained untranslated damage inputs in original order, including duplicates.
     * @lifetime The checkpoint owns storage; borrow expires at move/destruction. No domain cache is polled or consumed.
     */
    [[nodiscard]] std::span<const rts::RTSFrameDamageEvent> pendingDamage() const noexcept;
    /** @brief Borrow exact retained translated lifecycle inputs in original order until move/destruction. */
    [[nodiscard]] std::span<const spawn::LifecycleInput> pendingLifecycle() const noexcept;
    /** @brief Share immutable original structural inputs, independent of source lifetime; empty after move. */
    [[nodiscard]] std::shared_ptr<const map_spawn::MapSpawnAssets> assets() const noexcept;
    /** @brief Share immutable runtime grid facts, independent of source lifetime; empty after move. */
    [[nodiscard]] std::shared_ptr<const map::MapGridState> gridState() const noexcept;

private:
    friend class RTSMapSession;
    friend class RTSRuntimeCheckpoint;
    struct Impl;
    explicit RTSMapCheckpoint(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::map_rts_spawn
