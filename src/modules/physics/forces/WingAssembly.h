#pragma once
#include "common/Export.h"
#include "physics/forces/ForceBatch.h"

namespace eve::physics {
/** @brief Owned wing/tail/control surface in body-local coordinates.
 * @details center is the aerodynamic center relative to body origin, in meters.
 * model.chord/normal are local unit orthogonal axes; other polar parameters retain WingModel units.
 */
struct WingSurface {
    WingModel   model;
    ForceVector center;
};
/** @brief Immutable multi-surface aerodynamics, producing a single external-force batch.
 * @ownership Copies geometry/polar values; stores no world/body or control-input pointers.
 * @thread Build with immutable inputs on any thread; prepare on simulation owner outside callbacks.
 * @reentrancy No callbacks, scripts or locks; failures never change bodies or partially apply surfaces.
 * @details Each surface samples actual body point velocity including omega cross r, minus uniform world wind.
 * Controls rotate chord toward normal by the corresponding angle (radians), around the local span axis.
 * Values are held for this prepared step; commit before moving bodies or changing controls/wind.
 * This section model does not simulate wake interaction, downwash between surfaces, or CFD.
 */
class EVENGINE_API_WORLD WingAssembly {
public:
    /** @brief Validate and copy local surface descriptions in deterministic input order.
     * @return Owning assembly or InvalidArgument for invalid geometry/polar values.
     * @cost Linear validation/storage per surface; build once and reuse for fixed steps.
     */
    [[nodiscard]] static Result<WingAssembly> build(std::span<const WingSurface> surfaces);
    /** @brief Prepare planar surface forces; centers, local axes and wind must have z=0.
     * @param deflections One finite angle per surface, including zero for fixed wings.
     * @return Atomic force batch or invalid/stale/dimension/overflow error.
     * @cost Linear surface sampling plus batch aggregation per fixed step.
     */
    [[nodiscard]] Result<ForceBatch> prepare(World& world, PhysicsLink body, std::span<const double> deflections,
                                             ForceVector wind) const;
    /** @brief Prepare spatial wing forces, transforming local axes and points from the actual body pose.
     * @param deflections One finite angle per surface, including zero for fixed wings.
     * @return Atomic force batch or invalid/stale/dimension/overflow error.
     * @cost Linear surface sampling plus batch aggregation per fixed step.
     */
    [[nodiscard]] Result<ForceBatch> prepare(World3D& world, PhysicsLink body, std::span<const double> deflections,
                                             ForceVector wind) const;

private:
    explicit WingAssembly(std::span<const WingSurface> surfaces) : surfaces_(surfaces.begin(), surfaces.end()) {}
    std::vector<WingSurface> surfaces_;
};
}  // namespace eve::physics
