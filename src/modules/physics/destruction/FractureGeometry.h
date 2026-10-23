#pragma once
#include <cstdint>
#include <vector>
#include "common/Export.h"
#include "common/Result.h"
namespace eve::physics {
/** @brief Owning closed convex fragment in body-local metres; outward triangle winding.
 * Each triangle has material 0 (source exterior) or 1 (new fracture interior).
 * Immutable after publication; no renderer, source mesh or physics pointers.
 */
struct FractureFragment {
    std::vector<float>         positions;
    std::vector<std::uint32_t> indices;
    std::vector<std::uint32_t> materials;
};
/** @brief Owning, typed mesh payload shared immutably by simulation, clip and renderer.
 * @thread Read-only sharing is safe; build/validation is single caller, no callbacks.
 */
struct EVENGINE_API_DOMAINS FractureGeometry {
    std::vector<FractureFragment> fragments;
    /** @brief Validate budgets, closed outward convex meshes, indices, finite coordinates and tags.
     * @cost Quadratic in vertices per fragment; validate once before publication, not per frame.
     * @return Checked failure; never changes this object.
     */
    [[nodiscard]] Result<void> validate() const;
};
}  // namespace eve::physics
