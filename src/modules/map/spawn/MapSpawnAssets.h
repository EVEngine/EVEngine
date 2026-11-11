#pragma once
/** @file MapSpawnAssets.h
 * @brief map_spawn module-interface: immutable structural asset read closure and versioned source participant.
 * @remarks Captures actual injected filesystem reads, binding and object contract. Runtime tile mutations,
 * renderer image bytes, template/unit content and scheduler progress remain separate participants.
 */
#include <memory>
#include <span>
#include <vector>
#include "common/Snapshot.h"
namespace eve::map {
class Map;
class PreparedMapLoad;
}  // namespace eve::map
namespace eve::map_spawn {
class PreparedMapSpawn;
/** @brief Owning bytes under the exact path requested from the project's read service. */
struct MapSpawnAssetFile {
    std::string               path;
    std::vector<std::uint8_t> bytes;
};
/** @brief Owning frozen structural inputs, not proof of live Map admission or a complete game save.
 * @ownership Captured by the actual loader or structurally parsed; immutable shared owners may outlive all services.
 * @thread Map/content owner thread; no file service, Map pointer, subscriptions or callbacks retained.
 */
class EVENGINE_API_WORLD MapSpawnAssets {
public:
    /** @brief Release source bytes only. */
    ~MapSpawnAssets();
    MapSpawnAssets(const MapSpawnAssets&)            = delete;
    MapSpawnAssets& operator=(const MapSpawnAssets&) = delete;
    /** @brief Borrow the exact frozen binding until owner destruction. */
    [[nodiscard]] const Value& binding() const noexcept;
    /** @brief Borrow the frozen object contract source until owner destruction. */
    [[nodiscard]] std::string_view objectContract() const noexcept;
    /** @brief Borrow files in path order until owner destruction; no file service is consulted. */
    [[nodiscard]] std::span<const MapSpawnAssetFile> files() const noexcept;
    /** @brief Seal eve.map-spawn-assets/v1 with exact binding/contract/file bytes, without live asset reads.
     * @return Owning participant or failure without source/world mutation. Hash callbacks cannot destroy this owner.
     * @remarks Caller supplies joint epoch/revision/tick and digest algorithm; no default/security claim.
     * @thread Owner thread, no locks; only the synchronous hash provider executes external code.
     * @cost Full byte encoding and canonical hashing per save artifact; hex doubles source byte storage in payload.
     * @throws std::bad_alloc Local encoding allocation failed without publication.
     */
    [[nodiscard]] Result<SnapshotEnvelope> seal(PersistentId saveEpoch, Revision revision, SimulationTick tick,
                                                const SnapshotHashProvider& hashProvider) const;
    /** @brief Verify and own strict v1 structural inputs; semantic Map validation belongs to prepareMap.
     * @return Immutable owner or schema/version/digest/duplicate/path/byte-encoding failure.
     * @ownership Copies the whole envelope before callbacks; output retains no input/provider references.
     * @remarks Unknown fields/versions reject; initial v1 has no legacy migration or live-file fallback.
     * @thread Owner thread; only injected digest code executes callbacks, without locks.
     * @cost Envelope copy/hash and full source-byte decoding per admitted asset participant.
     * @throws std::bad_alloc Local admission allocation failed without source/world mutation.
     */
    [[nodiscard]] static Result<std::shared_ptr<const MapSpawnAssets>> parse(const SnapshotEnvelope&     envelope,
                                                                             PersistentId                saveEpoch,
                                                                             const SnapshotHashProvider& hashProvider);
    /** @brief Stage the original Map using only captured bytes and the canonical Map/object-contract parser.
     * @return Isolated Map candidate or failure preserving target; missing dependency never reads current disk.
     * @ownership Borrows target for preparation only; result uses its canonical weak Map lease.
     * @remarks Does not restore runtime tiles/navigation, renderer resources, templates/units or Spawn progress.
     * @thread Map owner thread outside Views; read replay invokes no external filesystem callback.
     * @cost Whole structural map/dependency parsing and isolated layer allocation per map restore attempt.
     * @throws std::bad_alloc Local staging failed without target publication.
     */
    [[nodiscard]] Result<std::unique_ptr<map::PreparedMapLoad>> prepareMap(map::Map& target) const;

private:
    friend class PreparedMapSpawn;
    MapSpawnAssets(Value binding, std::string contract, std::vector<MapSpawnAssetFile> files);
    Value                          binding_;
    std::string                    contract_;
    std::vector<MapSpawnAssetFile> files_;
};
}  // namespace eve::map_spawn
