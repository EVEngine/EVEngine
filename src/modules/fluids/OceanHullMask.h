#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <span>
#include <vector>
#include "common/Result.h"

namespace eve::fluids {
/** @brief One convex dry volume in vessel-local metres, bounded by an oriented box.
 * Extra halfspaces use dot(normal, localPoint) <= offset; at most ten bevel planes.
 * Pose is copied from the authoritative vessel, never integrated by the mask.
 * The box guarantees finite support even when additional planes are redundant.
 */
struct OceanHullVolume {
    glm::dvec3              center{0.0};
    glm::dquat              rotation{1.0, 0.0, 0.0, 0.0};
    glm::dvec3              halfExtent{1.0};
    std::vector<glm::dvec4> bevels;
};
class OceanHullMaskBuilder;
/** @brief Immutable owning render-only union of convex dry volumes.
 * No body handles, callbacks, physics mutation or retained input pointers. Rebuild after
 * vessel pose or render-origin changes. Empty means no masking. Concurrent reads are safe.
 */
class OceanHullMaskFrame {
public:
    /** @brief Borrow packed render-space halfspaces until frame destruction/move.
     * Each volume has seventeen vec4 rows: plane count then up to sixteen normalized planes.
     * Plane equation is dot(xyz, renderPoint) <= w; unused rows are zero.
     */
    std::span<const glm::vec4> rows() const { return rows_; }
    /** @brief World origin subtracted before float projection. */
    glm::dvec3 origin() const { return origin_; }
    /** @brief Number of dry volumes; at most 32. */
    unsigned size() const { return static_cast<unsigned>(rows_.size() / 17); }
    /** @brief Reference fragment classification; nonfinite points are outside, boundary is inside.
     * @cost At most 512 halfspace tests, no allocation; diagnostics/tests rather than buoyancy queries.
     */
    bool contains(glm::dvec3 worldPoint) const;

private:
    friend class OceanHullMaskBuilder;
    glm::dvec3             origin_{0.0};
    std::vector<glm::vec4> rows_;
};
/** @brief Synchronous borrowed geometry preparation, with no provider dependency. */
class OceanHullMaskBuilder {
public:
    /** @brief Borrow volumes, which must remain alive and unchanged until build returns. */
    explicit OceanHullMaskBuilder(std::span<const OceanHullVolume> volumes) : volumes_(volumes) {}
    glm::dvec3 origin{0.0};
    /** @brief Validate the entire batch then return its owning render projection.
     * @return Complete mask or InvalidArgument; no external mutation or callbacks.
     * @cost O(volumes * planes) allocation and transform, amortized per changed vessel pose.
     * @thread Caller-owned builder; input reads must not race mutation.
     */
    [[nodiscard]] Result<OceanHullMaskFrame> build() const;

private:
    std::span<const OceanHullVolume> volumes_;
};
}  // namespace eve::fluids
