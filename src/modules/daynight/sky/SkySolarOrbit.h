#pragma once
#include <array>
#include "common/Result.h"

namespace eve::daynight {
/** @brief Non-astronomical daily solar orbit, matching the reference's manual pitch/yaw mode.
 * @details Value-owned immutable configuration. Directions point towards the sun in
 * EVEngine's Y-up coordinates, with Unreal (X,Y,Z) mapped to (X,Z,Y).
 * No clock, weather, renderer, intensity curve or persistence state is owned here.
 * @thread Concurrent evaluation is safe; there are no callbacks or borrowed objects. */
class SkySolarOrbit {
public:
    /** @brief Prepare a daily orbit with pitch [-90,90] and yaw [-360,360] degrees.
     * @param pitchDegrees Tilt from an overhead noon sun; reference default is 30.
     * @param yawDegrees Rotation about the vertical axis; reference default is zero.
     * @return Owned orbit, or InvalidArgument for nonfinite/out-of-range input. */
    [[nodiscard]] static Result<SkySolarOrbit> create(double pitchDegrees, double yawDegrees);
    /** @brief Evaluate a copied unit direction for an hour in [0,24).
     * @return Direction, or InvalidArgument; the orbit remains unchanged. */
    [[nodiscard]] Result<std::array<double, 3>> direction(double hour) const;

private:
    SkySolarOrbit(double pitch, double yaw);
    double sinPitch_, cosPitch_, sinYaw_, cosYaw_;
};
}  // namespace eve::daynight
