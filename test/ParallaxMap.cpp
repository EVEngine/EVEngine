#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>

#include "graphics/ParallaxMap.h"

using eve::graphics::ParallaxParams;
using eve::graphics::clampParallaxParams;
using eve::graphics::parallaxOffsetUV;

TEST_CASE("ParallaxMap.scaleZeroIsIdentity") {
    float ou = -1.f, ov = -1.f;
    parallaxOffsetUV(0.4f, 0.6f, 0.2f, 0.5f, -0.3f, 0.8f, 0.f, ou, ov);
    CHECK(std::fabs(ou - 0.4f) < 1e-6f);
    CHECK(std::fabs(ov - 0.6f) < 1e-6f);
}

TEST_CASE("ParallaxMap.heightDisplacesTowardViewXY") {
    // Low height → deep → larger displacement along -viewXY.
    float ouLo = 0.f, ovLo = 0.f;
    float ouHi = 0.f, ovHi = 0.f;
    parallaxOffsetUV(0.5f, 0.5f, 0.0f, 0.4f, 0.0f, 0.9f, 0.1f, ouLo, ovLo);
    parallaxOffsetUV(0.5f, 0.5f, 1.0f, 0.4f, 0.0f, 0.9f, 0.1f, ouHi, ovHi);
    CHECK(ouLo < ouHi);  // deeper surface shifts UV more opposite +viewX
    CHECK(std::fabs(ovLo - 0.5f) < 1e-5f);
    CHECK(std::fabs(ovHi - 0.5f) < 1e-5f);
}

TEST_CASE("ParallaxMap.clampParams") {
    ParallaxParams p;
    p.scale = 1.f;
    p.minLayers = 0.f;
    p.maxLayers = 100.f;
    clampParallaxParams(p);
    CHECK(p.scale == 0.25f);
    CHECK(p.minLayers == 1.f);
    CHECK(p.maxLayers == 64.f);

    p.scale = -1.f;
    p.minLayers = 16.f;
    p.maxLayers = 8.f;
    clampParallaxParams(p);
    CHECK(p.scale == 0.f);
    CHECK(p.maxLayers == 16.f);
}
