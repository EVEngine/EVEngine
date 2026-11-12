#pragma once
#include <memory>
#include <span>
#include "common/Export.h"
#include "common/Result.h"
#include "common/Time.h"

namespace eve::physics {
class World3D;
class ForceReceipt;
struct PhysicsLink;
namespace field {
struct GravityPolicy;
}

/** @brief Explicit additive Newtonian center-point gravity submitted to the existing rigid solver.
 * @ownership Owns observations, loads and handles only. Destruction cancels unsubmitted work.
 * @thread Prepare and commit on the world's simulation owner, outside callbacks/steps; no callbacks or locks.
 * @remarks Selected active dynamic bodies are both sources and targets. No distance cutoff or second integrator.
 * Existing world gravity remains additive. Finite-volume tides/torques are not represented by this point model.
 */
class EVENGINE_API_DOMAINS PointGravityBatch {
public:
    /** @brief Release owned observations and unsubmitted loads. */
    ~PointGravityBatch();
    /** @brief Transfer the one-time submission right. */
    PointGravityBatch(PointGravityBatch&&) noexcept;
    /** @brief Cancel old work and transfer submission rights. */
    PointGravityBatch& operator=(PointGravityBatch&&) noexcept;
    PointGravityBatch(const PointGravityBatch&)            = delete;
    PointGravityBatch& operator=(const PointGravityBatch&) = delete;
    /** @brief Observe centers/masses, build/evaluate the point field and stage forces without changing bodies.
     * @param world Borrowed until return, never retained; owns all selected dynamic recipients.
     * @param bodies Borrowed nonempty source/target identities; duplicates reject.
     * @param policy Explicit SI gravity kernel and numerical/resource budgets, copied for this preparation.
     * @param step Exact next tick and positive duration to which loads will be bound.
     * @return Owning candidate or failure without waking, integrating or partially applying forces.
     * @cost One field build/evaluation plus O(B+N) observation/staging per simulation step; field budgets apply.
     */
    [[nodiscard]] static Result<PointGravityBatch> prepare(World3D& world, std::span<const PhysicsLink> bodies,
                                                           const field::GravityPolicy& policy, SimulationStep step);
    /** @brief Reobserve all selected centers/masses and atomically submit only if the observation still matches.
     * @param world Borrowed on its owner until return; foreign/destroyed/restored identities reject.
     * @return Existing physics receipt, or stale/changed/consumed failure with no partial application.
     * @cost O(B+N) revalidation and force submission per commit; no field recomputation or integration.
     * @remarks Include physics/forces/ForceReceipt.h when consuming the returned receipt. Call World3D::step
     * with the prepared step to integrate. A rejected backend step preserves submitted forces for retry.
     */
    [[nodiscard]] Result<ForceReceipt> commit(World3D& world);

private:
    struct Impl;
    explicit PointGravityBatch(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::physics
