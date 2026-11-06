#pragma once
#include <array>
#include "common/Export.h"
#include "common/Result.h"

namespace eve::spatial {
struct CellPosition;
}
namespace eve::graphics::multiscale {
/** @brief Explicit perspective view in a common dyadic lattice; forward points into the scene. */
struct AngularView {
    std::array<double, 3> right{1, 0, 0}, up{0, 1, 0}, forward{0, 0, 1};
    double                tanHalfVerticalFov = 1;
    double                aspect             = 1;
    int                   cellExponent       = 0;
};
/** @brief Geometric culling result, independent of occlusion, brightness and legacy camera clipping. */
enum class AngularVisibility { Outside, Projected, IntersectsCameraPlane };
/** @brief Owning conservative NDC rectangle; camera-plane intersection returns the full viewport.
 * @remarks Bounds may extend outside [-1,1]. No depth-buffer value or render-order claim is supplied.
 */
struct AngularBounds {
    AngularVisibility     visibility = AngularVisibility::Outside;
    std::array<double, 2> minimum{}, maximum{};
};
/**
 * @brief Conservatively project a sphere without an absolute-distance cutoff or absolute float positions.
 * @param center Borrowed canonical value in the same lattice as camera; not retained.
 * @param camera Borrowed camera position in that lattice; not retained.
 * @param radiusMantissa Positive finite radius mantissa; physical radius is mantissa * 2^radiusExponent.
 * @param radiusExponent Physical radius exponent in [-900,900].
 * @param view Valid orthonormal right/up/forward basis, positive finite FOV tangent and aspect.
 * @return Owning bounds or structured invalid/range failure; never silently discards uncertain geometry.
 * @ownership No pointers, camera state or graphics resources are retained or changed.
 * @thread Reentrant, no callbacks, clocks or RNG; requires IEEE arithmetic without fast-math/FTZ.
 * @cost Constant arithmetic/storage per sphere; traversal and active-object count are caller budgets.
 * @remarks Interval inflation covers represented center projection and arithmetic. Input shape and
 * transform approximation are caller-owned. Basis axes must be unit and mutually orthogonal within
 * 1e-12; radius is inflated for their measured norms. No near/far distance, occlusion or flux test exists.
 */
[[nodiscard]] EVENGINE_API_WORLD Result<AngularBounds> projectSphere(const spatial::CellPosition& center,
                                                                     const spatial::CellPosition& camera,
                                                                     double radiusMantissa, int radiusExponent,
                                                                     const AngularView& view);
}  // namespace eve::graphics::multiscale
