#pragma once
/** @file RPGMapArchive.h
 * @brief map_rpg_spawn module-interface: same-boundary map sources, runtime grid and RPG Spawn save admission.
 * @remarks CPU admission and isolated map preparation only; no live session or actor publication.
 */
#include <memory>
#include "common/Snapshot.h"
namespace eve::map {
class Map;
class PreparedMapLoad;
class MapGridState;
}  // namespace eve::map
namespace eve::map_spawn {
class MapSpawnAssets;
}
namespace eve::rpg_spawn {
class RPGSpawnJointRestore;
}
namespace eve::rpg {
class SkillConditionBindings;
}
namespace eve::map_rpg_spawn {
class RPGMapCheckpoint;
/** @brief Immutable admitted map/RPG save artifact with owning participant facts.
 * @ownership Independent of source sessions/files/envelopes; admitted domain shares the actual immutable condition
 * executor owner. Does not certify later external execution availability or retain live Map/entity pointers.
 * @thread Simulation/content owner thread; only injected digest callbacks execute external code, without locks.
 */
class EVENGINE_API_WORLD RPGMapArchive {
public:
    /** @brief Release local participant owners only. */
    ~RPGMapArchive();
    RPGMapArchive(const RPGMapArchive&)            = delete;
    RPGMapArchive& operator=(const RPGMapArchive&) = delete;
    /** @brief Verify strict eve.rpg-map-save/v1 and all three same-boundary canonical participants.
     * @return Owning immutable facts or schema/digest/boundary/semantic failure without world mutation.
     * @ownership Copies the entire input before the first digest callback; provider and input are never retained.
     * @remarks Unknown fields/versions reject; initial v1 has no inferred legacy migration. RPG admission uses
     * the existing joint core/domain/pending validator. Map semantics are checked later by prepareMap.
     * @cost Whole artifact copy/hash plus canonical grid/asset/RPG admission per restore attempt.
     * @throws std::bad_alloc Local admission failed without publication.
     */
    [[nodiscard]] static Result<std::shared_ptr<const RPGMapArchive>> prepare(
        const SnapshotEnvelope& envelope, PersistentId saveEpoch, const SnapshotHashProvider& hashProvider,
        std::shared_ptr<const rpg::SkillConditionBindings> conditions = {});
    /** @brief Borrow the verified RPG Spawn bundle until archive destruction, for native session preparation.
     * @remarks Not a live session; runtime provider validation and materialization remain required. */
    [[nodiscard]] const SnapshotEnvelope& runtime() const noexcept;
    /** @brief Borrow exact admitted core/domain/pending facts until archive destruction.
     * @ownership Immutable joined owner; core.clone prepares independent attempts, domainOwner shares exact content.
     * @remarks No reparse, callback or live authority; later materialization still validates actual runtime bindings.
     */
    [[nodiscard]] const rpg_spawn::RPGSpawnJointRestore& runtimeState() const noexcept;
    /** @brief Share immutable actual structural sources, independent of this archive's lifetime. */
    [[nodiscard]] std::shared_ptr<const map_spawn::MapSpawnAssets> assets() const noexcept;
    /** @brief Share immutable saved runtime cells/navigation facts, independent of this archive's lifetime. */
    [[nodiscard]] std::shared_ptr<const map::MapGridState> gridState() const noexcept;
    /** @brief Prepare the saved structural map and overlay runtime facts in one unpublished candidate.
     * @return Owning isolated candidate or failure preserving target; source paths/count must agree.
     * @ownership Borrows target synchronously; returned candidate uses its canonical weak lease.
     * @thread Map owner thread outside Views; no external filesystem or gameplay callbacks.
     * @remarks Uses captured files only. Does not publish the map, actors or a running session.
     * @cost Canonical structural parsing, full cell overlay/chunk rebuild per restoration attempt.
     * @throws std::bad_alloc Local staging failed without target mutation.
     */
    [[nodiscard]] Result<std::unique_ptr<map::PreparedMapLoad>> prepareMap(map::Map& target) const;

private:
    friend class RPGMapCheckpoint;
    struct Impl;
    explicit RPGMapArchive(std::unique_ptr<Impl> impl);
    [[nodiscard]] static Result<SnapshotEnvelope> sealParticipants(SnapshotEnvelope runtime, SnapshotEnvelope assets,
                                                                   SnapshotEnvelope            grid,
                                                                   const SnapshotHashProvider& hashProvider);
    std::unique_ptr<Impl>                         impl_;
};
}  // namespace eve::map_rpg_spawn
