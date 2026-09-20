#pragma once

#include "common/Result.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <glm/vec3.hpp>

namespace eve::graphics::hair {

/**
 * @brief One control point on a hair curve (UE `FHairStrandsDatas` point analogue).
 * @ownership Value type owned by `StrandsDatas`.
 */
struct StrandPoint {
    glm::vec3 position{0.f};
    float radius = 0.001f;
    /** @brief Root-to-tip parameter in [0, 1]. */
    float u = 0.f;
};

/**
 * @brief One strand as a contiguous point range.
 * @ownership Value type owned by `StrandsDatas`.
 */
struct StrandCurve {
    uint32_t pointOffset = 0;
    uint32_t pointCount  = 0;
    float length         = 0.f;
};

/**
 * @brief CPU-authoritative strand buffer (UE `FHairStrandsDatas` analogue).
 *
 * Points are dense; each curve references a contiguous subrange. Optional
 * per-point attributes can be added later without changing curve indices.
 *
 * @thread Affine to the owning asset/instance; no internal synchronization.
 * @reentrancy Does not invoke callbacks.
 */
class EVENGINE_API_BACKENDS StrandsDatas {
public:
    void setPoints(std::vector<StrandPoint> points);
    void setCurves(std::vector<StrandCurve> curves);

    [[nodiscard]] uint32_t addPoint(const StrandPoint &point);
    [[nodiscard]] uint32_t addCurve(const StrandCurve &curve);

    void clear();

    /**
     * @brief Verify offsets, counts, and finite values.
     * @return Success, or InvalidArgument / InvariantViolation with path.
     */
    [[nodiscard]] Result<void> validate() const;

    [[nodiscard]] size_t pointCount() const { return points_.size(); }
    [[nodiscard]] size_t curveCount() const { return curves_.size(); }

    /** @brief Borrowed points; invalidated by mutation. */
    [[nodiscard]] std::span<const StrandPoint> points() const { return points_; }
    /** @brief Borrowed curves; invalidated by mutation. */
    [[nodiscard]] std::span<const StrandCurve> curves() const { return curves_; }

    /**
     * @brief Borrowed points of one curve.
     * @return Empty span when `curveIndex` is out of range or the range is invalid.
     */
    [[nodiscard]] std::span<const StrandPoint> curvePoints(size_t curveIndex) const;

    /** @brief Axis-aligned bounds of all points; empty → zero box at origin. */
    void computeBounds(glm::vec3 &outMin, glm::vec3 &outMax) const;

private:
    std::vector<StrandPoint> points_;
    std::vector<StrandCurve> curves_;
};

}  // namespace eve::graphics::hair
