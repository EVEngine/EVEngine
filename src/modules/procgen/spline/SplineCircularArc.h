#pragma once
#include <array>
#include "common/Export.h"
#include "common/Result.h"

namespace eve::procgen::detail {
struct CircularArcSampleXZ;
/**
 * @brief Scale a circular neighbor tangent to its owning 3D marker chord.
 * @param tangent Analytic XZ arc derivative; borrowed only during this call.
 * @param heightDerivative Height derivative at the same endpoint.
 * @param chord Owning span endpoint differences in double precision.
 * @return Owned finite float tangent components, or an invalid/unsupported geometry diagnostic.
 * @thread Pure value computation; callable on any thread with immutable inputs, no callbacks or retained pointers.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<std::array<float, 3>> scaleCircularTangentToChord(
    const CircularArcSampleXZ& tangent, double heightDerivative, const std::array<double, 3>& chord);
/** @brief Which endpoint tangent determines a circular span's orientation. */
enum class CircularArcReference { Start, End };
/** @brief Owning derived XZ arc coefficients; not persistent author state. */
struct CircularArcXZ {
    double ax = 0, az = 0, bx = 0, bz = 0, cx = 0, cz = 0, radius = 0, angle = 0, sweep = 0;
    bool   line  = true;  ///< Observable collinear limit.
    double leadX = 0, leadZ = 0, leadLength = 0;
    bool   reversed = false;  ///< Derived sampling orientation; never persistent state.
};
/** @brief Position and parameter derivative of one arc sample. */
struct CircularArcSampleXZ {
    double x = 0, z = 0, dx = 0, dz = 0;
};
/** @brief Construct a finite circle from two endpoints and one endpoint direction.
 * @ownership Returns owning coefficients without retaining inputs.
 * @thread Synchronous caller; no callbacks or mutable services.
 * @cost Constant span-sized geometry setup with trigonometry; reuse coefficients for multiple samples.
 * @return Valid coefficients, including the collinear line limit, or an invalid-geometry diagnostic.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<CircularArcXZ> makeCircularArcXZ(double ax, double az, double bx, double bz,
                                                                           double dx, double dz,
                                                                           CircularArcReference reference);
/** @brief Fit a forward straight lead followed by a circle tangent to both neighboring directions.
 * A negative lead emits the endpoint chord; a singular fit retains the one-direction circle contract.
 * @ownership Returns owned derived coefficients; inputs are not retained.
 * @thread Pure synchronous value computation, no callbacks.
 * @cost Constant span-sized geometry setup, amortized over samples.
 * @return Validated coefficients or the existing one-direction invalid-geometry diagnostic.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<CircularArcXZ> makeCircularLeadArcXZ(double ax, double az, double bx,
                                                                               double bz, double dx, double dz,
                                                                               double endDx, double endDz);
/** @brief Sample validated coefficients with u in [0,1]; endpoints remain exact.
 * @ownership Returns a copied sample; retains nothing.
 * @thread Synchronous caller; no callbacks.
 * @cost Constant arithmetic/trigonometry per sample after span setup.
 */
[[nodiscard]] EVENGINE_API_DOMAINS CircularArcSampleXZ sampleCircularArcXZ(const CircularArcXZ& arc, double u);
}  // namespace eve::procgen::detail
