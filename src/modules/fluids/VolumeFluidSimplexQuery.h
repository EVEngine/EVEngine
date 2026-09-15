#pragma once

#include "fluids/VolumeFluid.h"

namespace eve::fluids::detail {
[[nodiscard]] Result<std::vector<VolumeFluidSimplexHit>> querySimplexes(std::span<const VolumeFluidParticle> particles,
                                                                        std::span<const VolumeFluidSimplex>  simplexes,
                                                                        std::span<const VolumeFluidQueryShape> queries,
                                                                        unsigned maxHitsPerQuery);
}
