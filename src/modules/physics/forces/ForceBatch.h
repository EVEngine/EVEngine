#pragma once
#include <span>
#include <vector>
#include "physics/PhysicsLink.h"
#include "physics/forces/ForceModels.h"
#include "physics/forces/ForceReceipt.h"

namespace eve::physics {
/** @brief Owned SI load request: world point in meters, force in N, free torque in Nm.
 * @lifetime The link is observed, never owns a body; restore/destruction invalidates it.
 */
struct ForceLoad {
    PhysicsLink target;
    ForceVector force, point, torque;
};
/** @brief Move-only, single-use collection of validated force requests.
 * @ownership Owns values and handles only, never world/body pointers. Safe to discard.
 * @thread Prepare/commit on the world's simulation owner, outside solver callbacks.
 * @reentrancy No callbacks. A successful commit consumes the batch; failure changes no bodies.
 * @details Commit revalidates all targets and recomputes moment arms at current centers of mass.
 * Binds forces to an exact tick/duration. A failed step preserves pending forces for retry without resubmission.
 * All batches pending in a world must name the same step. Direct raw stepping/force clearing is forbidden.
 * Do not mix unbounded raw-backend force accumulation with this API.
 */
class ForceBatch {
public:
    ForceBatch(const ForceBatch&)            = delete;
    ForceBatch& operator=(const ForceBatch&) = delete;
    /** @brief Transfers the single-use submission right; the source becomes consumed. */
    ForceBatch(ForceBatch&& other) noexcept;
    /** @brief Replaces unsubmitted requests, consuming the source. */
    ForceBatch& operator=(ForceBatch&& other) noexcept;
    /** @brief Validate and copy planar requests without waking or applying forces.
     * @return Owning batch or invalid/stale/stepping-world failure.
     * @cost Linear storage and O(n log n) aggregation in request count; prepare once per fixed step.
     */
    [[nodiscard]] static Result<ForceBatch> prepare(World& world, std::span<const ForceLoad> loads);
    /** @brief Validate and copy spatial requests without waking or applying forces.
     * @return Owning batch or invalid/stale/stepping-world failure.
     * @cost Linear storage and O(n log n) aggregation in request count; prepare once per fixed step.
     */
    [[nodiscard]] static Result<ForceBatch> prepare(World3D& world, std::span<const ForceLoad> loads);
    /** @brief Revalidate all planar requests and submit once; rejects consumed or foreign batches.
     * @return Read-only step receipt or structured failure, with no partial application on failure.
     * @cost O(n log n) validation/aggregation per batch, then one force/torque pair per target.
     */
    [[nodiscard]] Result<ForceReceipt> commit(World& world, const SimulationStep& step);
    /** @brief Revalidate all spatial requests and submit once; rejects consumed or foreign batches.
     * @return Read-only step receipt or structured failure, with no partial application on failure.
     * @cost O(n log n) validation/aggregation per batch, then one force/torque pair per target.
     */
    [[nodiscard]] Result<ForceReceipt> commit(World3D& world, const SimulationStep& step);

private:
    ForceBatch(PhysicsWorldHandle world, bool spatial, std::span<const ForceLoad> loads);
    PhysicsWorldHandle     world_;
    bool                   spatial_ = false, consumed_ = false;
    std::vector<ForceLoad> loads_;
};
}  // namespace eve::physics
