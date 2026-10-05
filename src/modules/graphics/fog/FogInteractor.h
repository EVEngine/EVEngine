#pragma once
#include "common/Export.h"

#include "common/Result.h"
#include "graphics/fog/FogTypes.h"

#include <cstddef>
#include <vector>

#include <glm/vec3.hpp>

namespace eve::graphics::fog {

class MacFluidGrid;

/**
 * @brief Analytic solid proxies that carve fog and inject wake into the MAC field.
 *
 * Sphere, capsule, and OBB proxies act as dynamic solid boundaries without a
 * second fog material. Surface velocity drives windward / leeward / tangential
 * drag; interiors clear concentration. Bounded grid traversal prevents thin
 * proxies from being skipped in one advection step.
 */
class EVENGINE_API_BACKENDS FogInteractor {
public:
    /** @brief Replace the proxy set atomically after validation. */
    [[nodiscard]] Result<void> setProxies(std::vector<FogSolidProxy> proxies);

    [[nodiscard]] std::size_t proxyCount() const noexcept { return proxies_.size(); }
    [[nodiscard]] const FogSolidProxy& proxyAt(std::size_t index) const;

    /** @brief True when a world point lies inside any enabled solid. */
    [[nodiscard]] bool isSolidWorld(const glm::vec3& world) const noexcept;

    /**
     * @brief Rasterize solids into the MAC solid mask, clear interior density,
     * and inject wake / drag into face velocities.
     */
    [[nodiscard]] Result<void> applyToGrid(MacFluidGrid& grid, float dt) const;

    /**
     * @brief Clip a local-cell advection displacement against thin solids.
     * @param startLocal Cell-space start (cell units).
     * @param deltaLocal Proposed displacement in cell units.
     * @param bounds World bounds of the MAC domain.
     * @param cellSize World size of one cell.
     * @return Clipped end position in cell space.
     */
    [[nodiscard]] glm::vec3 clipAdvection(const glm::vec3& startLocal, const glm::vec3& deltaLocal,
                                          const FogWorldBounds& bounds,
                                          const glm::vec3& cellSize) const noexcept;

private:
    [[nodiscard]] float signedDistance(const FogSolidProxy& proxy, const glm::vec3& world) const noexcept;
    [[nodiscard]] glm::vec3 closestPoint(const FogSolidProxy& proxy, const glm::vec3& world) const noexcept;
    [[nodiscard]] glm::vec3 surfaceNormal(const FogSolidProxy& proxy, const glm::vec3& world) const noexcept;

    std::vector<FogSolidProxy> proxies_;
};

}  // namespace eve::graphics::fog
