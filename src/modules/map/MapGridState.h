#pragma once
/** @file MapGridState.h
 * @brief map module-interface: owning runtime grid/terrain/navigation facts and strict persistence admission.
 * @remarks No Map/entity pointers, textures, renderer bindings, chunks or navigation caches are serialized.
 * Draw/atlas/animation changes, object metadata and image resources are separate save participants.
 */
#include <memory>
#include "common/Snapshot.h"
namespace eve::map {
class Map;
class PreparedMapLoad;
/** @brief Immutable runtime cell, geometry, terrain and per-GID navigation state for structural map layers.
 * @ownership Owns copied scalar/array data; independent of Map, filesystem and render resource lifetimes.
 * @thread Map/content owner thread; capture/admission invoke no gameplay callbacks.
 */
class EVENGINE_API_WORLD MapGridState {
public:
    /** @brief Release owned data only. */
    ~MapGridState();
    MapGridState(const MapGridState&)            = delete;
    MapGridState& operator=(const MapGridState&) = delete;
    /** @brief Capture actual visible map layers without changing them or reading original files.
     * @return Immutable owner or invalid/inconsistent/oversized grid failure; empty maps reject.
     * @ownership Map is borrowed only during capture; no entity/component pointer is retained.
     * @cost Copies all grid cells and navigation/terrain metadata per checkpoint; no derived grid capture.
     * @throws std::bad_alloc Local copy failed without source mutation.
     */
    [[nodiscard]] static Result<std::shared_ptr<const MapGridState>> capture(Map& source);
    /** @brief Number of owning layer records, in structural layer order. */
    [[nodiscard]] std::size_t layerCount() const noexcept;
    /** @brief Seal eve.map-grid-runtime/v1 using the coordinator's common boundary and injected digest.
     * @return Owning envelope or failure preserving these facts; callbacks must not destroy this owner.
     * @remarks No default digest/security claim; only the hash provider executes callbacks, without locks.
     * @cost Full cell/metadata encoding and canonical hashing per runtime map save artifact.
     * @throws std::bad_alloc Local encoding allocation failed without source publication.
     */
    [[nodiscard]] Result<SnapshotEnvelope> seal(PersistentId saveEpoch, Revision revision, SimulationTick tick,
                                                const SnapshotHashProvider& hashProvider) const;
    /** @brief Verify and own strict v1 runtime facts before any isolated-map application.
     * @return Immutable state or digest/schema/version/range/shape failure; unknown fields/versions reject.
     * @ownership Copies the entire envelope before hash callbacks. Initial v1 has no inferred legacy migration.
     * @remarks Actual layer count/path binding is checked by PreparedMapLoad::applyGridState, not by parsing.
     * @cost Whole envelope verification and bounded cell/metadata decoding per restoration attempt.
     * @throws std::bad_alloc Local admission failed without input or map mutation.
     */
    [[nodiscard]] static Result<std::shared_ptr<const MapGridState>> parse(const SnapshotEnvelope&     envelope,
                                                                           PersistentId                saveEpoch,
                                                                           const SnapshotHashProvider& hashProvider);

private:
    friend class PreparedMapLoad;
    struct Impl;
    explicit MapGridState(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::map
