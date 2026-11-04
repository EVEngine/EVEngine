#pragma once

#include "common/Export.h"
#include "common/Result.h"

#include <array>
#include <cstddef>
#include <span>
#include <vector>

namespace eve::math {

/** @brief Owned XY coordinates; no script object or module lifetime required. */
using HullPoint2 = std::array<float, 2>;
/** @brief Owned XYZ coordinates; no script object or module lifetime required. */
using HullPoint3 = std::array<float, 3>;
/** @brief Triangle indices into the returned vertex array. */
using HullTriangle = std::array<std::size_t, 3>;

/** @brief Affine dimension of a successful hull, including degenerate inputs. */
enum class HullDimension { Empty, Point, Segment, Plane, Volume };

/** @brief Self-contained 2D hull; vertices are CCW, with no repeated closing point. */
struct EVENGINE_API_FOUNDATION_INLINE ConvexHull2D {
    HullDimension           dimension = HullDimension::Empty;
    std::vector<HullPoint2> vertices;
};

/** @brief Self-contained 3D hull; volume triangles face outward. */
struct EVENGINE_API_FOUNDATION_INLINE ConvexHull3D {
    HullDimension             dimension = HullDimension::Empty;
    std::vector<HullPoint3>   vertices;
    std::vector<HullTriangle> faces;
};

/**
 * @brief Andrew monotone-chain hull; removes exact duplicates and collinear edge interiors.
 * @param points Finite XY values, borrowed only during this call.
 * @return Owned hull, or InvalidArgument for non-finite coordinates. Collinear input
 * returns sorted endpoints; singleton and empty input are successful.
 * @ownership Result owns all output; input is neither changed nor retained.
 * @thread Stateless, callable on any thread with immutable input; invokes no callbacks.
 * @cost O(n log n) time and O(n) memory per point set; cache the hull for reuse.
 * @note Predicates use double precision, not exact arithmetic.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<ConvexHull2D> convexHull2D(std::span<const HullPoint2> points);

/**
 * @brief Incremental 3D hull with outward triangles and compact vertex indices.
 * @param points Finite XYZ values, borrowed only during this call.
 * @param tolerance Distance relative to the largest bounding-box extent; finite,
 * nonnegative and less than one. Coordinates are normalized internally.
 * @return Owned hull or InvalidArgument. Empty/point/segment have no faces. Plane
 * vertices form a boundary cycle with a triangle fan (orientation is unspecified).
 * Volume faces retain coplanar triangulation; edge-interior vertices may remain.
 * @ownership Result owns all output; input is neither changed nor retained.
 * @thread Stateless, callable on any thread with immutable input; invokes no callbacks.
 * @cost Worst-case O(n^2 log n) time and O(n) memory per point set; cache results for reuse.
 * @note Sorted processing is deterministic for identical input sets on one platform.
 * Floating-point predicates are approximate; near-degenerate features within tolerance
 * may be omitted. No exact topology or cross-platform bitwise parity is promised.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<ConvexHull3D> convexHull3D(std::span<const HullPoint3> points,
                                                                        double                      tolerance = 1e-10);

}  // namespace eve::math
