#pragma once
#include <cstddef>
#include <memory>
#include "common/Export.h"
#include "common/Result.h"
namespace eve::procgen {
class TerrainWorldWorkspace;
}
namespace eve::procgen::road {
class RoadNetwork;
class RoadTerrainWorkspace;
struct RoadBakeOptions;
struct RoadTerrainPolicy;
/** @brief Immutable owning per-source terrain write plan prepared from one authoritative network snapshot.
 * Owner-thread only, synchronous. No network, mesh or provider pointers survive preparation.
 * Applying borrows the destination until return and publishes one undo frame; failures leave it unchanged.
 * Shared junction/derived geometry uses the explicit shared policy, never an inferred incident-edge winner.
 */
class EVENGINE_API_DOMAINS RoadTerrainPlan {
public:
    /** @brief Destroy the owned meshes and copied policies. */
    ~RoadTerrainPlan();
    /** @brief Transfer the plan. */
    RoadTerrainPlan(RoadTerrainPlan&&) noexcept;
    /** @brief Replace the plan. */
    RoadTerrainPlan& operator=(RoadTerrainPlan&&) noexcept;
    RoadTerrainPlan(const RoadTerrainPlan&)            = delete;
    RoadTerrainPlan& operator=(const RoadTerrainPlan&) = delete;
    /** @brief Bake the borrowed network and compact source intervals into independently owned write meshes.
     * @param network Borrowed and immutable until return; all author policies validated before baking.
     * @param options Borrowed until return; its provider lifetime/reentrancy contract matches bakeRoadNetwork.
     * @param sharedPolicy Explicit policy for junctions and network-derived geometry; disabled skips those sources.
     * @param maximumPreparedElements Bound copied index and vertex-attribute elements; positive.
     * @return Owned plan, or a diagnostic without changing authors or terrain. No hidden terrain writes.
     * @cost Full network bake plus expected linear source-index remapping and copied attributes, amortized per rebuild.
     */
    [[nodiscard]] static Result<RoadTerrainPlan> prepare(const RoadNetwork& network, const RoadBakeOptions& options,
                                                         const RoadTerrainPolicy& sharedPolicy,
                                                         std::size_t              maximumPreparedElements);
    /** @brief Apply the immutable plan in one workspace transaction; disabled-only plans are NoOp.
     * @param workspace Borrowed synchronously; retains only resulting channel/history values.
     * @param maximumWork Positive shared execution budget; never persisted as author state.
     * @return Changed cells or failure; failed writes preserve current tiles and redo.
     * @cost Linear triangle coverage plus bounded channel filtering, paid once per explicit terrain commit.
     */
    [[nodiscard]] Result<int> apply(RoadTerrainWorkspace& workspace, std::size_t maximumWork) const;
    /** @brief Apply through the native World complete-channel transaction.
     * @param world Borrowed synchronously; no pointer retained.
     * @param maximumWork Positive shared execution budget, not author state.
     * @return Changed cells or atomic failure; disabled-only plans return NoOp.
     * @cost Complete World tile projection and bounded terrain write, amortized per explicit commit.
     */
    [[nodiscard]] Result<int> applyWorld(TerrainWorldWorkspace& world, std::size_t maximumWork) const;

private:
    struct Impl;
    explicit RoadTerrainPlan(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::procgen::road
