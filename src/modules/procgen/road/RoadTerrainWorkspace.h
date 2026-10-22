#pragma once
#include "common/Value.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/road/RoadBake.h"

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace eve::procgen::road {
/** @brief Owned detail raster keyed by the external terrain's stable resource identity. */
struct RoadTerrainDetail {
    std::string      resourceId;
    int              width = 0, height = 0;
    std::vector<int> density;
};
/** @brief Owned terrain tree instance; its stable identity survives removal, undo and save. */
struct RoadTerrainTree {
    std::string id, resourceId;
    float       x = 0.f, y = 0.f, z = 0.f, yaw = 0.f, width = 1.f, height = 1.f;
};
/** @brief Renderer-neutral complete tile value, handed into one RoadTerrainWorkspace owner.
 * Splat layers are normalized weights. Hole value one denotes missing terrain. Each raster has its own resolution.
 * External terrain adapters must publish copies from this owner rather than maintain another writable road state.
 */
struct RoadTerrainTile {
    std::string                    id;
    float                          originX = 0.f, originZ = 0.f, sizeX = 1.f, sizeZ = 1.f, heightScale = 1.f;
    Heightmap                      heights, holes;
    std::vector<Heightmap>         splatLayers;
    std::vector<RoadTerrainDetail> details;
    std::vector<RoadTerrainTree>   trees;
    float                          originY = 0.f;  ///< World height baseline; absent schema-v1 fields migrate to zero.
};
/** @brief Explicit channel selection for one all-tile atomic write; no implicit optional providers. */
struct RoadTerrainWriteOptions {
    float blendDistance = 2.f, maxVerticalDelta = 2.f;
    int   splatLayer   = -1;  ///< Negative one preserves texture weights; otherwise paints the selected layer.
    bool  clearDetails = true, clearTrees = true, cutTunnelHoles = false;
    bool  tunnelHolesAtPortalsOnly = true;  ///< Cut authored portal surfaces; false selects the legacy full footprint.
    std::size_t maximumWork        = 16u * 1024u * 1024u;
    float       splatOpacity       = 1.f;  ///< Finite [0,1] target weight ceiling; multiplies coverage for splats only.
};
/** @brief One synchronous terrain operation; mesh is borrowed until the batch returns, options are copied values. */
struct RoadTerrainWrite {
    std::reference_wrapper<const MeshBuild> mesh;
    RoadTerrainWriteOptions                 options;
};
/** @brief Single mutable owner of imported terrain tiles and their complete-channel history.
 * Caller-thread affine, synchronous, callback-free. Imported tiles are owned; mesh inputs are borrowed until return.
 * Returned tile values own storage.
 * Same-build deterministic floating arithmetic, no clocks or RNG. History publishes once after every tile succeeds.
 * Snapshot schema procgen.road.terrain version 2 requires originY; version 1 migrates missing originY to zero.
 * Unknown object fields are ignored; unsupported versions and malformed known fields reject atomically.
 * Restore replaces current data and undo/redo history atomically; no external tile pointer or receipt survives.
 */
class EVENGINE_API_DOMAINS RoadTerrainWorkspace {
public:
    /** @brief Construct an empty terrain owner. */
    RoadTerrainWorkspace();
    /** @brief Destroy owned tiles and history. */
    ~RoadTerrainWorkspace();
    /** @brief Transfer ownership; moved-from objects can be initialized again. */
    RoadTerrainWorkspace(RoadTerrainWorkspace&&) noexcept;
    /** @brief Transfer ownership and release the previous history. */
    RoadTerrainWorkspace& operator=(RoadTerrainWorkspace&&) noexcept;
    RoadTerrainWorkspace(const RoadTerrainWorkspace&)            = delete;
    RoadTerrainWorkspace& operator=(const RoadTerrainWorkspace&) = delete;
    /** @brief Validate and import complete tile values, clearing history atomically.
     * @cost Linear in all channel samples and tree instances, amortized at import boundaries.
     * @return Imported tile count, or a structured diagnostic leaving this owner unchanged.
     */
    [[nodiscard]] Result<int> initialize(std::vector<RoadTerrainTile> tiles);
    /** @brief Evaluate road coverage and publish height, texture, details, trees and selected holes together.
     * Bridge surfaces never paint/clear terrain. Tunnel heights and vegetation roofs remain intact unless holes are
     * selected.
     * Splat coverage times opacity raises the selected weight only when greater than its current weight; other layers
     * retain their relative proportions. Zero preserves splats while independent channels execute. No quantization.
     * @cost Raster work is bounded by maximumWork; each transaction copies bounded retained history before publishing.
     * @return Changed channel element count; any failure preserves every tile and the redo branch.
     */
    [[nodiscard]] Result<int> apply(const MeshBuild& mesh, const RoadTerrainWriteOptions& options);
    /** @brief Apply ordered per-road policies as one complete-channel history frame.
     * @param writes Nonempty borrowed operations (at most 4096); referenced meshes must survive this call.
     * @param maximumWork Shared work budget across every road and tile; each operation also retains its own cap.
     * @return Summed per-operation changed element counts; failure preserves current state and redo history.
     * @cost One retained-history candidate plus bounded raster work summed over the operations, per explicit build.
     * @note Owner-thread only, synchronous, no callbacks. Overlap follows input order; no reference overlap parity
     * claim.
     */
    [[nodiscard]] Result<int> applyBatch(std::span<const RoadTerrainWrite> writes, std::size_t maximumWork);
    /** @brief Restore the previous complete-channel value; no mutation when history is exhausted.
     * Selects an owned immutable history frame by cursor, without copying channel storage.
     */
    [[nodiscard]] Result<int> undo();
    /** @brief Restore the next complete-channel value; no mutation when history is exhausted.
     * Selects an owned immutable history frame by cursor, without copying channel storage.
     */
    [[nodiscard]] Result<int> redo();
    /** @brief Copy one tile snapshot; returned data never aliases the owner.
     * @cost Linear in this tile's channel storage, amortized at renderer/save boundaries.
     */
    [[nodiscard]] Result<RoadTerrainTile> copyTile(const std::string& id) const;
    /** @brief Encode current data and the complete undo/redo history as an owning canonical Value.
     * @cost Linear in retained history storage, amortized at save boundaries.
     */
    [[nodiscard]] Result<Value> capture() const;
    /** @brief Validate schema, channel topology and every historical frame before one atomic owner replacement.
     * @cost Linear in encoded history storage, amortized at load boundaries.
     */
    [[nodiscard]] Result<int> restore(const Value& snapshot);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::procgen::road
