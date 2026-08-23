#pragma once

#include "fluids/FluidSurfaceBinding.h"

#include <vector>

namespace eve::fluids {

/** @brief Parameters controlling persistent wet-film traces on a triangle surface. */
struct SurfaceWetnessParams {
    float diffusion = 0.18f;
    float evaporation = 0.025f;
    float maxWetness = 1.f;
};

/**
 * @brief Lightweight material-space wet-film field stored at mesh vertices.
 *
 * The values follow the topology while the bound mesh moves or deforms. Deposits
 * are barycentrically distributed, then diffused over vertex edges and evaporated.
 */
class SurfaceWetnessField {
public:
    /** @brief Initialize the field from a surface topology. */
    bool build(const FluidSurfaceBinding& binding);

    /** @brief Deposit a wet trace at a material-space location. */
    void deposit(const SurfaceLocation& location, float amount);

    /** @brief Diffuse and evaporate the field by dt seconds. */
    void step(float dt, const SurfaceWetnessParams& params = {});

    /** @return interpolated wetness at a surface location. */
    float sample(const SurfaceLocation& location) const;

    /** @return per-vertex wetness values for rendering or debugging. */
    const std::vector<float>& values() const { return values_; }

    /** @brief Reset all wetness to zero. */
    void clear();

private:
    std::vector<float>              values_;
    std::vector<std::vector<int>>   neighbors_;
    std::vector<glm::uvec3>         triangles_;
};

}  // namespace eve::fluids
