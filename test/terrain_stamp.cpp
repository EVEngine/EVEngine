#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainStamp.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

using namespace eve::procgen;

namespace {
Heightmap filled(int w, int h, float value) {
    Heightmap map(w, h);
    std::fill(map.data().begin(), map.data().end(), value);
    return map;
}
TerrainStampSettings centered() {
    TerrainStampSettings s;
    s.centerX = s.centerZ = 1;
    s.width = s.depth = 2;
    return s;
}
}  // namespace

TEST_CASE("procgen.stamp.sixOperations") {
    const auto                 stamp = filled(1, 1, 3), one = filled(1, 1, 1);
    auto                       s = centered();
    const std::array<float, 6> expected{3, 2, 2.5F, 3, 5, -1};
    for (int op = 0; op < 6; ++op) {
        auto target = filled(3, 3, 2);
        s.operation = static_cast<TerrainStampOperation>(op);
        auto r      = applyTerrainStamp(target, stamp, s, one, one);
        REQUIRE(r.ok());
        CHECK(r.value() == (op == 1 ? 0 : 9));
        for (float value : target.data()) CHECK(value == expected[size_t(op)]);
    }
}

TEST_CASE("procgen.stamp.localAndGlobalHaveDistinctMeaning") {
    auto       target = filled(3, 3, 2);
    const auto stamp = filled(1, 1, 4), half = filled(1, 1, 0.5F);
    auto       s = centered();
    s.baseHeight = 1;
    s.operation  = TerrainStampOperation::Set;
    auto r       = applyTerrainStamp(target, stamp, s, half, half);
    REQUIRE(r.ok());
    CHECK(target.height(1, 1) == 2.5F);  // lerp(2, 1 + 4 * .5, .5)
    auto zero   = filled(1, 1, 0);
    s.operation = TerrainStampOperation::Add;
    r           = applyTerrainStamp(target, stamp, s, half, zero);
    REQUIRE(r.ok());
    CHECK(r.value() == 0);
    CHECK(target.height(1, 1) == 2.5F);
}

TEST_CASE("procgen.stamp.rotatedFootprintAndBilinearSampling") {
    auto target = filled(5, 5, -2);
    auto stamp  = filled(2, 2, 0);
    stamp.setHeight(1, 0, 2);
    stamp.setHeight(0, 1, 4);
    stamp.setHeight(1, 1, 6);
    const auto one = filled(1, 1, 1);
    auto       s   = centered();
    s.centerX = s.centerZ = 2;
    s.width               = 2;
    s.depth               = 4;
    s.rotation            = std::numbers::pi / 2;
    s.operation           = TerrainStampOperation::Set;
    auto r                = applyTerrainStamp(target, stamp, s, one, one);
    REQUIRE(r.ok());
    CHECK(r.value() == 15);
    CHECK(std::abs(target.height(2, 2) - 3) < 1e-6F);
    CHECK(std::abs(target.height(1, 2) - 4) < 1e-6F);
    CHECK(target.height(2, 0) == -2);
    CHECK(target.height(0, 0) == -2);
}

TEST_CASE("procgen.stamp.tilesMatchOneWorldRaster") {
    auto whole = filled(5, 3, 0), left = filled(3, 3, 0), right = filled(3, 3, 0);
    auto stamp     = filled(2, 2, 0);
    stamp.data()   = {0, 1, 2, 4};
    const auto one = filled(1, 1, 1);
    auto       s   = centered();
    s.centerX      = 2;
    s.width        = 4;
    s.operation    = TerrainStampOperation::Set;
    auto r         = applyTerrainStamp(whole, stamp, s, one, one);
    REQUIRE(r.ok());
    r = applyTerrainStamp(left, stamp, s, one, one);
    REQUIRE(r.ok());
    s.originX = 2;
    r         = applyTerrainStamp(right, stamp, s, one, one);
    REQUIRE(r.ok());
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x) {
            CHECK(left.height(x, y) == whole.height(x, y));
            CHECK(right.height(x, y) == whole.height(x + 2, y));
        }
}

TEST_CASE("procgen.stamp.aliasUsesOriginalInput") {
    auto target       = filled(3, 3, 0);
    target.data()     = {0, 1, 2, 3, 4, 5, 6, 7, 8};
    const auto source = target, one = filled(1, 1, 1);
    auto       expected = target;
    auto       s        = centered();
    s.rotation          = std::numbers::pi;
    s.operation         = TerrainStampOperation::Set;
    auto r              = applyTerrainStamp(expected, source, s, one, one);
    REQUIRE(r.ok());
    r = applyTerrainStamp(target, target, s, one, one);
    REQUIRE(r.ok());
    CHECK(target.data() == expected.data());
}

TEST_CASE("procgen.stamp.invalidAndOverflowAreAtomic") {
    auto       target = filled(3, 3, 7);
    auto       stamp  = filled(3, 3, 1);
    const auto one = filled(1, 1, 1), before = target;
    auto       s = centered();
    s.operation  = TerrainStampOperation::Set;
    s.width      = 0;
    auto r       = applyTerrainStamp(target, stamp, s, one, one);
    CHECK(!r.ok());
    CHECK(target.data() == before.data());
    s.width             = 2;
    stamp.data().back() = std::numeric_limits<float>::infinity();
    r                   = applyTerrainStamp(target, stamp, s, one, one);
    CHECK(!r.ok());
    CHECK(target.data() == before.data());
    stamp.data().back() = std::numeric_limits<float>::max();
    s.amplitude         = 2;
    r                   = applyTerrainStamp(target, stamp, s, one, one);
    CHECK(!r.ok());
    CHECK(target.data() == before.data());
    stamp.data().pop_back();  // Legacy Heightmap mutable data can violate its shape.
    r = applyTerrainStamp(target, stamp, s, one, one);
    CHECK(!r.ok());
    CHECK(target.data() == before.data());
}

TEST_CASE("procgen.stamp.maskBlendGoldenValues") {
    const auto                 source = filled(1, 1, 0.8F);
    const std::array<float, 5> expected{0.16F, 0.8F, 0.2F, 1.0F, -0.6F};
    for (int op = 0; op < 5; ++op) {
        auto target = filled(1, 1, 0.2F);
        auto r      = blendTerrainMask(target, source, static_cast<TerrainMaskBlend>(op));
        REQUIRE(r.ok());
        CHECK(std::abs(target.height(0, 0) - expected[size_t(op)]) < 1e-6F);
    }
}

TEST_CASE("procgen.stamp.maskOrderStrengthInvertAndAlias") {
    auto       target = filled(1, 1, 0.2F);
    const auto source = filled(1, 1, 0.8F), half = filled(1, 1, 0.5F);
    auto       r = blendTerrainMask(target, source, TerrainMaskBlend::Add);
    REQUIRE(r.ok());
    r = blendTerrainMask(target, half, TerrainMaskBlend::Multiply);
    REQUIRE(r.ok());
    CHECK(std::abs(target.height(0, 0) - 0.5F) < 1e-6F);
    r = blendTerrainMask(target, target, TerrainMaskBlend::Multiply, 0.5F, true);
    REQUIRE(r.ok());
    CHECK(std::abs(target.height(0, 0) - 0.375F) < 1e-6F);
}

TEST_CASE("procgen.stamp.maskFailuresPreserveTarget") {
    auto       target = filled(2, 1, 1), badSize = filled(1, 1, 1);
    const auto before = target;
    auto       r      = blendTerrainMask(target, badSize, TerrainMaskBlend::Multiply);
    CHECK(!r.ok());
    auto large = filled(2, 1, std::numeric_limits<float>::max());
    r          = blendTerrainMask(target, large, TerrainMaskBlend::Add, -1);
    CHECK(!r.ok());
    r = blendTerrainMask(target, large, static_cast<TerrainMaskBlend>(999));
    CHECK(!r.ok());
    CHECK(target.data() == before.data());
    target.data().back()   = std::numeric_limits<float>::max();
    const auto largeBefore = target;
    r                      = blendTerrainMask(target, large, TerrainMaskBlend::Add);
    CHECK(!r.ok());
    CHECK(target.data() == largeBefore.data());
}
