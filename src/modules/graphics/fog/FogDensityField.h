#pragma once
#include "common/Export.h"

#include "common/Result.h"
#include "graphics/fog/FogTypes.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <glm/vec3.hpp>

namespace eve::graphics::fog {

/**
 * @brief World-space density bands, curl assist velocity, and lighting helpers.
 *
 * Sampling is always in world units so fog does not crawl with screen UVs.
 */
class EVENGINE_API_WORLD FogDensityField {
public:
    /** @brief Allocate a regular lattice over world bounds. */
    [[nodiscard]] Result<void> resize(int width, int height, int depth, const FogWorldBounds& bounds);

    [[nodiscard]] int width() const noexcept { return width_; }
    [[nodiscard]] int height() const noexcept { return height_; }
    [[nodiscard]] int depth() const noexcept { return depth_; }
    [[nodiscard]] const FogWorldBounds& bounds() const noexcept { return bounds_; }
    [[nodiscard]] glm::vec3 cellSize() const noexcept { return cellSize_; }
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

    /** @brief Clear density / curl / light-assist bands and bump revision. */
    void clear();

    /** @brief Write cell-centered density; indices are clamped. */
    void setDensity(int x, int y, int z, float density);
    [[nodiscard]] float densityAt(int x, int y, int z) const noexcept;

    /** @brief Write curl-assist velocity (m/s) at a cell center. */
    void setCurlVelocity(int x, int y, int z, const glm::vec3& velocity);
    [[nodiscard]] glm::vec3 curlVelocityAt(int x, int y, int z) const noexcept;

    /** @brief Accumulated lighting assist (pre-multiplied irradiance hint). */
    void setLightAssist(int x, int y, int z, const glm::vec3& assist);
    [[nodiscard]] glm::vec3 lightAssistAt(int x, int y, int z) const noexcept;

    /** @brief Trilinear world-space density sample. */
    [[nodiscard]] float sampleDensity(const glm::vec3& world) const noexcept;
    /** @brief Trilinear world-space curl velocity sample. */
    [[nodiscard]] glm::vec3 sampleCurlVelocity(const glm::vec3& world) const noexcept;
    /** @brief Trilinear world-space light-assist sample. */
    [[nodiscard]] glm::vec3 sampleLightAssist(const glm::vec3& world) const noexcept;

    /**
     * @brief Seed a height-banded density layer with optional curl noise.
     * @param baseDensity Peak density at baseHeight.
     * @param baseHeight World Y of the densest band.
     * @param falloff Exponential falloff per meter.
     * @param curlScale Curl velocity amplitude in m/s.
     * @param seed Deterministic noise seed.
     */
    [[nodiscard]] Result<void> seedHeightBand(float baseDensity, float baseHeight, float falloff,
                                              float curlScale, std::uint32_t seed);

    /** @brief Occupancy bit: true when density exceeds the threshold. */
    [[nodiscard]] bool isOccupied(int x, int y, int z, float threshold = 1e-4f) const noexcept;

    /** @brief Borrow density storage for MAC coupling (same resolution). */
    [[nodiscard]] std::span<float> densitySpan() noexcept;
    [[nodiscard]] std::span<const float> densitySpan() const noexcept;

private:
    [[nodiscard]] std::size_t index(int x, int y, int z) const noexcept;
    [[nodiscard]] glm::vec3 cellCenter(int x, int y, int z) const noexcept;
    void sampleLattice(const glm::vec3& world, int& x0, int& y0, int& z0, int& x1, int& y1, int& z1,
                       float& fx, float& fy, float& fz) const noexcept;

    int width_ = 0;
    int height_ = 0;
    int depth_ = 0;
    FogWorldBounds bounds_{};
    glm::vec3 cellSize_{1.f};
    std::uint64_t revision_ = 1;
    std::vector<float> density_;
    std::vector<glm::vec3> curl_;
    std::vector<glm::vec3> lightAssist_;
};

}  // namespace eve::graphics::fog
