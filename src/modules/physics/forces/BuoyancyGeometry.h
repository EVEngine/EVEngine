#pragma once
#include "common/Export.h"
#include "physics/forces/ForceModels.h"

namespace eve::physics {
/** @brief Owned submerged volume (m^3), fraction and world-space center of buoyancy (m).
 * @details A dry shape returns zero volume/fraction and its original center as a harmless centroid placeholder.
 */
struct ImmersionSample {
    double      volume = 0, fraction = 0;
    ForceVector centroid;
};
/** @brief Exact primitive immersion in the half-space y <= surfaceY, with +Y upward.
 * @ownership All inputs/results are values; no body/world references or callbacks are retained.
 * @thread Reentrant on any thread with immutable inputs. No allocation on success.
 * @details Rectangle/box are world-axis aligned. Circle/sphere are rotation invariant.
 * Coordinates use SI meters. 2D centers must have z=0 and require explicit positive thickness.
 * @return InvalidArgument for non-finite values, nonpositive sizes, or unrepresentable volume/centroid.
 */
class EVENGINE_API_WORLD BuoyancyGeometry {
public:
    /** @brief Integrate a circular slice below a horizontal surface, extruded by thickness. */
    [[nodiscard]] static Result<ImmersionSample> circle2D(ForceVector center, double radius, double thickness,
                                                          double surfaceY);
    /** @brief Clip a world-aligned rectangle and extrude its submerged area by thickness. */
    [[nodiscard]] static Result<ImmersionSample> rectangle2D(ForceVector center, double width, double height,
                                                             double thickness, double surfaceY);
    /** @brief Integrate the submerged spherical cap and its volume centroid. */
    [[nodiscard]] static Result<ImmersionSample> sphere3D(ForceVector center, double radius, double surfaceY);
    /** @brief Clip a world-aligned box; size contains full X/Y/Z dimensions. */
    [[nodiscard]] static Result<ImmersionSample> box3D(ForceVector center, ForceVector size, double surfaceY);
};
}  // namespace eve::physics
