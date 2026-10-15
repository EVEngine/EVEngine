#pragma once

#include "fluids/OceanSpectrum.h"

#include <functional>
#include <optional>

namespace eve::fluids {
class OceanWake;
/**
 * @brief Synchronous borrowed composition of the spectral sea and an optional local wake.
 * Both owners must outlive this view and remain unmodified throughout its use. Do not store
 * across simulation steps, owner moves or asynchronous work. The view owns no mutable state.
 */
class OceanWaterView {
public:
    /** @brief Borrow a spectral frame for the current synchronous operation. */
    OceanWaterView(const OceanSpectrumFrame& spectrum) : spectrum_(spectrum) {}
    /** @brief Borrow synchronized spectral and wake fields; queries reject mismatching timestamps. */
    OceanWaterView(const OceanSpectrumFrame& spectrum, const OceanWake& wake) : spectrum_(spectrum), wake_(wake) {}
    /** @brief Shared injected simulation timestamp. */
    double time() const { return spectrum_.get().time(); }
    /** @brief Query combined water in world coordinates; errors propagate without changing either owner. */
    [[nodiscard]] Result<OceanSurfaceSample> sample(glm::dvec2 worldXZ) const;
    /** @brief Displace a mesh parameter point and add wake height at the resulting world XZ. */
    [[nodiscard]] Result<glm::dvec3> position(glm::dvec2 parameterXZ) const;
    /** @brief Evaluate combined position and tangents directly; wake gradients follow displaced world XZ.
     * @return Owning geometry or query/time diagnostic; synchronous const operation without retained input.
     */
    [[nodiscard]] Result<OceanSurfaceGeometry> geometry(glm::dvec2 parameterXZ) const;

private:
    std::reference_wrapper<const OceanSpectrumFrame>       spectrum_;
    std::optional<std::reference_wrapper<const OceanWake>> wake_;
};
}  // namespace eve::fluids
