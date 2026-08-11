#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>

#include "graphics/TextureCellBomb.h"

using eve::graphics::TexCellBombParams;
using eve::graphics::texBombHash22;
using eve::graphics::texCellBombSampleUV;

TEST_CASE("TextureCellBomb.hash22InUnitInterval") {
    for (int y = -3; y <= 5; ++y) {
        for (int x = -3; x <= 5; ++x) {
            float hx = 0.f, hy = 0.f;
            texBombHash22(float(x), float(y), hx, hy);
            CHECK(hx >= 0.f);
            CHECK(hx < 1.f);
            CHECK(hy >= 0.f);
            CHECK(hy < 1.f);
        }
    }
}

TEST_CASE("TextureCellBomb.strengthZeroIsIdentity") {
    TexCellBombParams p;
    p.cellScale = 8.f;
    p.strength = 0.f;
    p.rotAmount = 1.f;
    float ou = -1.f, ov = -1.f;
    texCellBombSampleUV(0.37f, 0.81f, p, ou, ov);
    CHECK(std::fabs(ou - 0.37f) < 1e-6f);
    CHECK(std::fabs(ov - 0.81f) < 1e-6f);
}

TEST_CASE("TextureCellBomb.strengthPerturbsUV") {
    TexCellBombParams p;
    p.cellScale = 4.f;
    p.strength = 1.f;
    p.rotAmount = 1.f;
    float ou = 0.f, ov = 0.f;
    texCellBombSampleUV(0.25f, 0.25f, p, ou, ov);
    const float du = ou - 0.25f;
    const float dv = ov - 0.25f;
    CHECK(du * du + dv * dv > 1e-6f);
}

TEST_CASE("TextureCellBomb.neighborCellsDiffer") {
    TexCellBombParams p;
    p.cellScale = 2.f;
    p.strength = 1.f;
    p.rotAmount = 0.5f;
    float a0 = 0.f, a1 = 0.f, b0 = 0.f, b1 = 0.f;
    // Same fractional position in adjacent cells → different offsets.
    texCellBombSampleUV(0.1f, 0.1f, p, a0, a1);
    texCellBombSampleUV(0.6f, 0.1f, p, b0, b1);
    CHECK(std::fabs((a0 - 0.1f) - (b0 - 0.6f)) + std::fabs((a1 - 0.1f) - (b1 - 0.1f)) > 1e-4f);
}
