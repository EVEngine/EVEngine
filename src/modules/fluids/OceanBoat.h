#pragma once

#include "fluids/OceanWaterView.h"

#include <memory>

namespace eve::physics {
class Body3D;
}
namespace eve::fluids {
/** @brief Body-local displacement cell; volumes sum to the hull's assigned displacement capacity. */
struct OceanHullCell {
    glm::dvec3 center{0.0};
    double     radius = 0.5;
    double     volume = 0.5;
};
/** @brief Hull water response; local +Z forward, +X starboard, +Y up. */
struct OceanHullSettings {
    std::vector<OceanHullCell> cells;
    double                     waterDensity = 1025.0;
    double                     gravity      = 9.81;
    glm::dvec3                 drag{1.2, 1.5, 0.15};
    glm::dvec3                 propeller{0.0, -0.3, -1.5};
    double                     thrust            = 3000.0;
    double                     rudderCoefficient = 200.0;
};
/** @brief Owning wet hull sample for wake/foam consumers; no rigid-body pointer escapes. */
struct OceanBoatContact {
    glm::dvec3 position{0.0};
    glm::dvec3 relativeVelocity{0.0};
    double     submergedVolume = 0.0;
};
/** @brief Owning fixed-step force receipt; consumers may retain it for wake injection. */
struct OceanBoatReceipt {
    double                        time = 0.0;
    glm::dvec3                    totalForce{0.0};
    double                        submergedVolume = 0.0;
    std::vector<OceanBoatContact> contacts;
};
/**
 * @brief Convert wet hull motion to bounded bow/stern dipoles and vertical impacts in a local wave field.
 * @param wake Exclusively borrowed field at the receipt time; mutated only after full validation.
 * @param receipt Contacts from a successful boat application, borrowed only during this call.
 * @param dt Fixed seconds in (0,1/30].
 * @return Success or InvalidArgument; no physics body is accessed and no input reference is retained.
 * @cost O(wake cells * wet contacts); invoke once per boat per fixed step.
 * @thread Wake owner thread only, no callbacks. Bounded game-water coupling, not conservative CFD.
 */
[[nodiscard]] Result<void> injectOceanBoatWake(OceanWake& wake, const OceanBoatReceipt& receipt, double dt);
/**
 * @brief Water-force adapter over a World3D-owned rigid body, storing generation-qualified links only.
 * All operations are physics-owner-thread-only and invoke no callbacks. Destroying this adapter never
 * destroys the body. Body/world destruction makes the next submission return StaleHandle. Recreate
 * the adapter after restore/hot reload; process-local links are never serialized.
 */
class OceanBoat {
public:
    /**
     * @brief Validate/copy a bounded hull and attach to a live dynamic body.
     * @param body Borrowed only during attachment; the World3D remains its sole owner.
     * @param settings At most 64 cells, with finite positive radii and displacement volumes.
     * @return Owned adapter or structured input/stale failure; does not mutate the body.
     */
    [[nodiscard]] static Result<std::unique_ptr<OceanBoat>> create(physics::Body3D&         body,
                                                                   const OceanHullSettings& settings);
    ~OceanBoat();
    OceanBoat(const OceanBoat&)            = delete;
    OceanBoat& operator=(const OceanBoat&) = delete;
    /**
     * @brief Submit buoyancy, body-local directional drag, submerged thrust and rudder forces before physics.
     * @param frame Immutable water at this fixed-step time; borrowed only for this call.
     * @param dt Fixed seconds in (0,1/30]; used to bound dissipative impulses.
     * @param throttle Engine command in [-1,1].
     * @param rudder Steering command in [-1,1].
     * @return Owning contacts/force receipt or structured failure. Validation and all queries finish
     * before any force is submitted. Each timestamp can be submitted only once to this adapter.
     * @thread Physics owner only; no callbacks. Rejected calls preserve adapter and body state.
     */
    [[nodiscard]] Result<OceanBoatReceipt> apply(const OceanWaterView& frame, double dt, double throttle,
                                                 double rudder);

private:
    struct Impl;
    explicit OceanBoat(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::fluids
