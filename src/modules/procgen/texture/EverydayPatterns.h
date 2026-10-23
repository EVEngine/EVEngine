#pragma once
#include "procgen/texture/EverydayMaterials.h"
namespace eve::procgen::detail {
/** @brief Internal pigment, physical relief and microfacet variation in a common periodic UV domain. */
struct EverydaySample {
    float pigment = .5f, relief = .5f, roughness = .5f;
};
/** @brief Pure seeded surface sampling; no state, IO, callbacks or retained borrows. */
EverydaySample sampleEveryday(EverydayMaterial family, float u, float v, uint32_t seed, int repeats, int bandwidth);
}  // namespace eve::procgen::detail
