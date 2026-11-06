#pragma once
#include "graphics/multiscale/AngularView.h"

namespace eve::graphics::multiscale::detail {
// Module-private, value-owned preparation shared by the public one-shot query
// and complete frame packing. Never retained across caller mutations.
struct PreparedAngularView {
    AngularView                          view;
    std::array<std::array<double, 3>, 3> basis{};
    std::array<double, 3>                norms{};
};
struct ProjectedSphere {
    AngularBounds         bounds;
    std::array<double, 3> center{};
    double                radius = 0;
};
/** @brief Validate and copy one view for module-internal reuse within a frame.
 * @param view Borrowed only during this call; the result owns all values.
 * @return Validated basis/norms or a diagnostic. Reentrant; no callbacks or retained pointers.
 * @cost Constant arithmetic and storage; amortized once across the frame's sphere candidates.
 */
[[nodiscard]] Result<PreparedAngularView> prepareAngularView(const AngularView& view);
/** @brief Project one sphere using the validated view and retain its already computed local values.
 * @param center Borrowed canonical lattice position; not retained.
 * @param camera Borrowed canonical lattice origin; not retained.
 * @param radiusMantissa Positive finite physical radius mantissa.
 * @param radiusExponent Radius exponent in [-900,900].
 * @param prepared Value-owned result of prepareAngularView; no caller mutation during the query.
 * @return Owning conservative bounds and local values, or a diagnostic. Reentrant, no callbacks.
 * @cost Constant arithmetic/storage per sphere; no success-path allocation.
 */
[[nodiscard]] Result<ProjectedSphere>     projectPreparedSphere(const spatial::CellPosition& center,
                                                                const spatial::CellPosition& camera, double radiusMantissa,
                                                                int radiusExponent, const PreparedAngularView& prepared);
}  // namespace eve::graphics::multiscale::detail
