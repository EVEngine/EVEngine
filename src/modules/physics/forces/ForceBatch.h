#pragma once
#include <span>
#include <vector>
#include "common/Export.h"
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
/** @brief Owning SI observation of one active dynamic body's center and mass, not mutable solver authority. */
struct ForceBodySample {
    PhysicsLink body;
    ForceVector center;
    double      mass = 0;
};
/** @brief Consistent owner-thread observation; rows are sorted by runtime body handle, never input order.
 * @lifetime Owns values only. Handles expire on destruction/restore; no raw pointers escape.
 * @remarks A tick alone does not detect setters between steps. Consumers must revalidate sampled state
 * before publishing derived loads; this value does not grant a right to apply stale forces.
 */
struct ForceBodySnapshot {
    PhysicsWorldHandle           world;
    SimulationTick               tick;
    std::vector<ForceBodySample> bodies;
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
class EVENGINE_API_WORLD ForceBatch {
public:
    /** @brief Capture active dynamic 3D body centers and masses without waking, applying or integrating.
     * @param world Borrowed on its simulation owner, outside stepping and callbacks, until return.
     * @param targets Borrowed identities; duplicate, foreign, stale or inactive recipients reject atomically.
     * @return Owning sorted snapshot, or a structured validation failure without world mutation.
     * @cost O(B+L) work/storage for B world bodies and L selected targets per explicit capture.
     * @reentrancy No callbacks or retained world/body pointers; callers serialize topology and property setters.
     */
    [[nodiscard]] static Result<ForceBodySnapshot> captureCenters(World3D& world, std::span<const PhysicsLink> targets);
    ForceBatch(const ForceBatch&)            = delete;
    ForceBatch& operator=(const ForceBatch&) = delete;
    /** @brief Transfers the single-use submission right; the source becomes consumed. */
    ForceBatch(ForceBatch&& other) noexcept;
    /** @brief Replaces unsubmitted requests, consuming the source. */
    ForceBatch& operator=(ForceBatch&& other) noexcept;
    /** @brief Validate and copy planar requests without waking or applying forces.
     * @return Owning batch or invalid/stale/stepping-world failure.
     * @cost O(B+L) time and storage for B world bodies and L requests; prepare once per fixed step.
     */
    [[nodiscard]] static Result<ForceBatch> prepare(World& world, std::span<const ForceLoad> loads);
    /** @brief Validate and copy spatial requests without waking or applying forces.
     * @return Owning batch or invalid/stale/stepping-world failure.
     * @cost O(B+L) time and storage for B world bodies and L requests; prepare once per fixed step.
     */
    [[nodiscard]] static Result<ForceBatch> prepare(World3D& world, std::span<const ForceLoad> loads);
    /** @brief Revalidate all planar requests and submit once; rejects consumed or foreign batches.
     * @return Read-only step receipt or structured failure, with no partial application on failure.
     * @cost O(B+L) validation/aggregation for B world bodies and L requests, then one load per target.
     */
    [[nodiscard]] Result<ForceReceipt> commit(World& world, const SimulationStep& step);
    /** @brief Revalidate all spatial requests and submit once; rejects consumed or foreign batches.
     * @return Read-only step receipt or structured failure, with no partial application on failure.
     * @cost O(B+L) validation/aggregation for B world bodies and L requests, then one load per target.
     */
    [[nodiscard]] Result<ForceReceipt> commit(World3D& world, const SimulationStep& step);

private:
    ForceBatch(PhysicsWorldHandle world, bool spatial, std::span<const ForceLoad> loads);
    PhysicsWorldHandle     world_;
    bool                   spatial_ = false, consumed_ = false;
    std::vector<ForceLoad> loads_;
};
}  // namespace eve::physics
