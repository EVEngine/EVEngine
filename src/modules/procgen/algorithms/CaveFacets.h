#pragma once

#include <cstdint>

namespace eve::procgen {

struct CaveFacetInput {
    float    along    = 0.f;
    float    angle    = 0.f;
    float    distance = 0.f;
    float    radius   = 0.16f;
    float    strength = 0.f;
    uint32_t seed     = 0;
};

struct CaveFacetSample {
    float retreat      = 0.f;
    float planarWeight = 0.f;
    int   facetCount   = 0;
};

/**
 * @brief Sample planar condensation-corrosion facets in passage-local coordinates.
 * @param input Passage position, wall geometry, normalized intensity, and deterministic seed.
 * @return Bounded outward wall retreat, planar blend weight, and selected facet count.
 */
CaveFacetSample sampleCaveCondensationFacets(const CaveFacetInput& input);

}  // namespace eve::procgen
