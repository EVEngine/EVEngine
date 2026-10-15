#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>

#include "graphics/ParallaxMap.h"

using eve::graphics::ParallaxParams;
using eve::graphics::clampParallaxParams;
using eve::graphics::parallaxOffsetUV;
using eve::graphics::silPomCoverage;
using eve::graphics::silPomCoverageSoft;
using eve::graphics::silPomHorizonTrim;
using eve::graphics::ssdmCoverage;
using eve::graphics::ssdmScreenOffset;

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

TEST_CASE("ParallaxMap.silPomCoverageClipsOutsideChart") {
    CHECK(silPomCoverage(0.5f, 0.5f, 0.f) == 1.f);
    CHECK(silPomCoverage(0.0f, 0.0f, 0.f) == 1.f);
    CHECK(silPomCoverage(1.0f, 1.0f, 0.f) == 1.f);
    CHECK(silPomCoverage(-0.01f, 0.5f, 0.f) == 0.f);
    CHECK(silPomCoverage(0.5f, 1.01f, 0.f) == 0.f);
    // Padding expands the keep region.
    CHECK(silPomCoverage(-0.01f, 0.5f, 0.02f) == 1.f);
}

TEST_CASE("ParallaxMap.silPomSoftCoverageAndHorizon") {
    CHECK(silPomCoverageSoft(0.5f, 0.5f, 0.05f) == 1.f);
    CHECK(silPomCoverageSoft(-0.01f, 0.5f, 0.05f) == 0.f);
    const float edge = silPomCoverageSoft(0.02f, 0.5f, 0.05f);
    CHECK(edge > 0.f);
    CHECK(edge < 1.f);
    // Face-on: keep low height. Grazing: clip low height, keep high height.
    CHECK(silPomHorizonTrim(0.1f, 1.0f, 0.45f) == 1.f);
    CHECK(silPomHorizonTrim(0.1f, 0.05f, 0.45f) == 0.f);
    CHECK(silPomHorizonTrim(0.95f, 0.05f, 0.45f) == 1.f);
}

TEST_CASE("ParallaxMap.ssdmOffsetScalesWithHeight") {
    float ox0 = 0.f, oy0 = 0.f;
    float ox1 = 0.f, oy1 = 0.f;
    ssdmScreenOffset(0.0f, 1.0f, 0.0f, 0.05f, ox0, oy0);
    ssdmScreenOffset(1.0f, 1.0f, 0.0f, 0.05f, ox1, oy1);
    CHECK(std::fabs(ox0) < 1e-6f);
    CHECK(std::fabs(oy0) < 1e-6f);
    CHECK(std::fabs(ox1 - 0.05f) < 1e-5f);
    CHECK(std::fabs(oy1) < 1e-6f);
    CHECK(ssdmCoverage(0.9f, 0.5f, 0.15f, 0.f, 0.f) == 0.f);
    CHECK(ssdmCoverage(0.9f, 0.5f, 0.05f, 0.f, 0.f) == 1.f);
}
