#pragma once
/** @file RPGMapCheckpoint.h
 * @brief map_rpg_spawn module-interface: joined frozen core/domain/lifecycle facts for coordinated saving.
 * @remarks Sealing delegates the RPG Spawn bundle schema; no live handles or restore are published.
 */
#include <memory>
#include <span>
#include "common/Export.h"
#include "common/Snapshot.h"
namespace eve::spawn {
class SpawnCheckpoint;
struct LifecycleInput;
}  // namespace eve::spawn
namespace eve::rpg_spawn {
class RPGSpawnCapture;
}
namespace eve::map {
class MapGridState;
}
namespace eve::map_spawn {
class MapSpawnAssets;
}
namespace eve::map_rpg_spawn {
class RPGMapSession;
/** @brief Detached same-epoch core/domain freeze with all retained lifecycle inputs.
 * @ownership Owns both participants and copied lifecycle data; never retains a live session, map, Table or provider.
 * @thread Simulation owner thread; read-only access has no callbacks. Not a complete game save.
 */
class EVENGINE_API_WORLD RPGMapCheckpoint {
public:
    /** @brief Release frozen local data. */
    ~RPGMapCheckpoint();
    /** @brief Transfer frozen data. */
    RPGMapCheckpoint(RPGMapCheckpoint&&) noexcept;
    /** @brief Release old data and transfer ownership. */
    RPGMapCheckpoint& operator=(RPGMapCheckpoint&&) noexcept;
    RPGMapCheckpoint(const RPGMapCheckpoint&)            = delete;
    RPGMapCheckpoint& operator=(const RPGMapCheckpoint&) = delete;
    /** @brief Borrow the frozen core until this checkpoint is moved or destroyed; requires an unconsumed object. */
    [[nodiscard]] const spawn::SpawnCheckpoint& core() const noexcept;
    /** @brief Borrow frozen RPG facts until this checkpoint is moved or destroyed; requires an unconsumed object. */
    [[nodiscard]] const rpg_spawn::RPGSpawnCapture& domain() const noexcept;
    /** @brief Borrow copied unconsumed lifecycle inputs until this checkpoint is moved or destroyed. */
    [[nodiscard]] std::span<const spawn::LifecycleInput> pendingLifecycle() const noexcept;
    /** @brief Share the exact original structural input participant from the actual map load.
     * @ownership Detached immutable owner, valid after session/map destruction; empty after move.
     * @remarks Seal with the same epoch/revision/tick as the runtime bundle; runtime tile/image state is separate.
     */
    [[nodiscard]] std::shared_ptr<const map_spawn::MapSpawnAssets> assets() const noexcept;
    /** @brief Share immutable runtime grid facts; empty after move, independent of source lifetime. */
    [[nodiscard]] std::shared_ptr<const map::MapGridState> gridState() const noexcept;
    /** @brief Seal frozen core, RPG domain and durable pending events into one RPG Spawn bundle.
     * @return Owning integrity-covered envelope or failure retaining this checkpoint for retry.
     * @ownership Uses frozen facts only; source session/map/Table may already be destroyed.
     * @thread Simulation owner thread; same-checkpoint reentry rejects without locks. Hash callbacks must not
     * move/destroy this checkpoint; no gameplay callbacks are invoked.
     * @remarks Map asset/navigation persistence and full active RPG state are separate participants.
     * @cost Participant sealing, canonical joint admission and whole-bundle hashing per save artifact.
     * @throws std::bad_alloc Local allocation failed without changing frozen or live state.
     */
    [[nodiscard]] Result<SnapshotEnvelope> seal(const SnapshotHashProvider& hashProvider) const;
    /** @brief Seal structural sources, runtime grid and the RPG Spawn bundle as one eve.rpg-map-save/v1 artifact.
     * @return Owning same-boundary artifact or failure preserving this checkpoint for retry.
     * @ownership Uses frozen participants only; source Map/session may have been destroyed.
     * @thread Owner thread; all checkpoint sealing reentry rejects. Hash callbacks cannot move/destroy this owner.
     * @remarks Unknown versions have no inferred migration; image/object/render state and full active RPG
     * state remain outside this artifact. Does not publish a restored session.
     * @cost Full three-participant encoding and nested/outer hashing per map save artifact.
     * @throws std::bad_alloc Encoding failed without live or frozen state mutation.
     */
    [[nodiscard]] Result<SnapshotEnvelope> sealMap(const SnapshotHashProvider& hashProvider) const;

private:
    friend class RPGMapSession;
    struct Impl;
    explicit RPGMapCheckpoint(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::map_rpg_spawn
