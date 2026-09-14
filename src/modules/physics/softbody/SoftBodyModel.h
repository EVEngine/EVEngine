#pragma once

#include "common/Result.h"

#include <cstdint>
#include <string>
#include <vector>

namespace eve::physics {

/** @brief A particle stored in an immutable cooked soft-body model. */
struct SoftBodyModelParticle {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
};

/** @brief A surface vertex bound directly to one cooked simulation particle. */
struct SoftBodyModelSurfaceBinding {
    std::uint32_t particleIndex = 0;
};

/** @brief An overlapping shape-matching cluster stored by particle index. */
struct SoftBodyModelCluster {
    std::vector<std::uint32_t> particleIndices;
};

/**
 * @brief Backend-neutral, owning result of deterministic soft-body cooking.
 *
 * The model owns all arrays and retains no pointers into an asset archive. It
 * may be moved between worker and simulation threads before publication; after
 * publication callers should treat it as immutable.
 */
struct SoftBodyModel {
    static constexpr std::uint32_t FormatVersion = 1;

    std::string                              sourceMesh;
    std::vector<SoftBodyModelParticle>       particles;
    std::vector<std::uint32_t>               surfaceIndices;
    std::vector<SoftBodyModelSurfaceBinding> surfaceBindings;
    std::vector<SoftBodyModelCluster>        clusters;

    /** @brief Validate all finite values, topology indices, bindings and clusters. */
    [[nodiscard("check cooked soft-body model")]] eve::Result<void> validate() const;
};

}  // namespace eve::physics
