#pragma once
#include "common/Export.h"

#include "common/Result.h"
#include "graphics/fog/FogDensityField.h"
#include "graphics/fog/FogTypes.h"
#include "graphics/fog/SceneWind.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/vec3.hpp>

namespace eve::graphics::fog {

class FogInteractor;

/**
 * @brief MAC (Marker-And-Cell) staggered-grid Eulerian fog fluid.
 *
 * Velocity lives on face centers; concentration lives at cell centers. Each
 * fixed substep applies forces, wind, velocity advection, pressure projection,
 * and density transport. An accumulator keeps wall-clock progress under low FPS.
 */
class EVENGINE_API_WORLD MacFluidGrid {
public:
    static constexpr float kDefaultFixedDt = 1.f / 60.f;
    static constexpr float kMaxAccumulated = 0.25f;

    /** @brief Allocate MAC arrays matching a density field lattice. */
    [[nodiscard]] Result<void> configure(const FogDensityField& field, float fixedDt = kDefaultFixedDt);

    [[nodiscard]] int width() const noexcept { return width_; }
    [[nodiscard]] int height() const noexcept { return height_; }
    [[nodiscard]] int depth() const noexcept { return depth_; }
    [[nodiscard]] const FogWorldBounds& bounds() const noexcept { return bounds_; }
    [[nodiscard]] glm::vec3 cellSize() const noexcept { return cellSize_; }
    [[nodiscard]] float fixedDt() const noexcept { return fixedDt_; }
    [[nodiscard]] float accumulatedTime() const noexcept { return accumulator_; }
    [[nodiscard]] std::uint64_t stepCount() const noexcept { return stepCount_; }

    /** @brief Copy concentration from a density field (same resolution). */
    [[nodiscard]] Result<void> pullDensity(const FogDensityField& field);
    /** @brief Write concentration back into a density field. */
    [[nodiscard]] Result<void> pushDensity(FogDensityField& field) const;

    /** @brief Cell-centered concentration. */
    [[nodiscard]] float densityAt(int x, int y, int z) const noexcept;
    void setDensity(int x, int y, int z, float density);

    /** @brief Face-centered velocity samples (m/s). */
    [[nodiscard]] float uAt(int i, int y, int z) const noexcept;
    [[nodiscard]] float vAt(int x, int j, int z) const noexcept;
    [[nodiscard]] float wAt(int x, int y, int k) const noexcept;

    /** @brief Interpolated cell-center velocity. */
    [[nodiscard]] glm::vec3 velocityAtCell(int x, int y, int z) const noexcept;

    /**
     * @brief Advance simulation by wall-clock dt using fixed substeps.
     * @param wind World wind provider sampled each substep.
     * @param interactor Optional solid proxies; may be nullptr.
     * @param simTime Absolute simulation time in seconds.
     */
    [[nodiscard]] Result<FogCflReport> step(float dt, SceneWind& wind, FogInteractor* interactor,
                                           float simTime);

    /** @brief Diagnose CFL for the current velocity field and fixed dt. */
    [[nodiscard]] FogCflReport diagnoseCfl() const noexcept;

private:
    friend class FogInteractor;

    [[nodiscard]] std::size_t cellIndex(int x, int y, int z) const noexcept;
    [[nodiscard]] std::size_t uIndex(int i, int y, int z) const noexcept;
    [[nodiscard]] std::size_t vIndex(int x, int j, int z) const noexcept;
    [[nodiscard]] std::size_t wIndex(int x, int y, int k) const noexcept;

    void applyForcesAndWind(const SceneWind& wind, float dt, float simTime);
    void advectVelocity(float dt);
    void projectPressure();
    void advectDensity(float dt, const FogInteractor* interactor);
    [[nodiscard]] glm::vec3 sampleVelocity(const glm::vec3& localCell) const noexcept;
    [[nodiscard]] float sampleDensityLocal(const glm::vec3& localCell) const noexcept;
    [[nodiscard]] glm::vec3 traceBounded(const glm::vec3& start, const glm::vec3& delta,
                                        const FogInteractor* interactor) const noexcept;

    int width_ = 0;
    int height_ = 0;
    int depth_ = 0;
    FogWorldBounds bounds_{};
    glm::vec3 cellSize_{1.f};
    float fixedDt_ = kDefaultFixedDt;
    float accumulator_ = 0.f;
    std::uint64_t stepCount_ = 0;

    std::vector<float> u_;
    std::vector<float> v_;
    std::vector<float> w_;
    std::vector<float> density_;
    std::vector<float> pressure_;
    std::vector<float> divergence_;
    std::vector<float> tmpDensity_;
    std::vector<float> tmpU_;
    std::vector<float> tmpV_;
    std::vector<float> tmpW_;
    std::vector<std::uint8_t> solid_;
};

}  // namespace eve::graphics::fog
