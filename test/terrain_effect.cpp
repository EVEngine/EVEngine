#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainEffect.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <cmath>
#include <limits>

using namespace eve::procgen;
namespace {
Heightmap filled(int width, int height, float value) {
    Heightmap map(width, height);
    std::fill(map.data().begin(), map.data().end(), value);
    return map;
}
Heightmap row(std::initializer_list<float> values) {
    Heightmap map(int(values.size()), 1);
    map.data() = values;
    return map;
}
}  // namespace

TEST_CASE("procgen.effect.contrastAllowsPcgDefaultAndExtrapolation") {
    auto map = row({0, 1, 0}), mask = row({1, 1, 1});
    auto r = applyTerrainContrast(map, mask, 2, 1);
    REQUIRE(r.ok());
    CHECK(map.data() == row({-0.3125F, 1.625F, -0.3125F}).data());
    map = row({0, 1, 0});
    r   = applyTerrainContrast(map, mask, 10, 1);
    REQUIRE(r.ok());
    CHECK(map.height(1, 0) == 4.125F);
    map               = row({0, std::numeric_limits<float>::max(), 0});
    const auto before = map.data();
    r                 = applyTerrainContrast(map, mask, 2, 1);
    CHECK(!r.ok());
    CHECK(map.data() == before);
}

TEST_CASE("procgen.effect.smoothSeparableImpulseAndVerticality") {
    auto original = filled(15, 15, 0), mask = filled(15, 15, 1);
    original.setHeight(7, 7, 1);
    auto                  map = original;
    TerrainSmoothSettings s;
    s.radius = 1;
    auto r   = applyTerrainSmooth(map, mask, s);
    REQUIRE(r.ok());
    CHECK(std::abs(map.height(7, 7) - 1.0 / 57.76) < 1e-7);
    CHECK(std::abs(map.height(8, 9) - 0.95 * 0.85 / 57.76) < 1e-7);
    s.verticality = -1;
    map           = original;
    r             = applyTerrainSmooth(map, mask, s);
    REQUIRE(r.ok());
    for (size_t i = 0; i < map.data().size(); ++i) CHECK(map.data()[i] <= original.data()[i]);
    CHECK(std::abs(map.height(7, 7) - 1.0 / 57.76) < 1e-7);
    s.verticality = 1;
    map           = original;
    r             = applyTerrainSmooth(map, mask, s);
    REQUIRE(r.ok());
    for (size_t i = 0; i < map.data().size(); ++i) CHECK(map.data()[i] >= original.data()[i]);
}

TEST_CASE("procgen.effect.smoothRectangularRasterUsesXTexelForVertical") {
    auto map = filled(2, 4, 0), mask = filled(2, 4, 1);
    map.setHeight(0, 1, 1);
    map.setHeight(1, 1, 1);
    TerrainSmoothSettings s;
    s.radius = 0.5F;
    auto r   = applyTerrainSmooth(map, mask, s);
    REQUIRE(r.ok());
    CHECK(std::abs(map.height(0, 1) - 1.0 / 7.6) < 1e-7);
    CHECK(std::abs(map.height(0, 2) - 0.95 / 7.6) < 1e-7);
    CHECK(std::abs(map.height(0, 3) - 0.85 / 7.6) < 1e-7);
}

TEST_CASE("procgen.effect.smoothMaskRemainsSnapshotAcrossPasses") {
    auto original = filled(3, 3, 0.2F);
    original.setHeight(1, 1, 0.8F);
    auto                  map = original, alias = original;
    TerrainSmoothSettings s;
    s.radius = 0.5F;
    auto r   = applyTerrainSmooth(map, original, s);
    REQUIRE(r.ok());
    r = applyTerrainSmooth(alias, alias, s);
    REQUIRE(r.ok());
    CHECK(map.data() == alias.data());
    auto before = map.data();
    s.radius    = 0;
    r           = applyTerrainSmooth(map, original, s);
    REQUIRE(r.ok());
    CHECK(map.data() == before);
    s.radius = -1;
    r        = applyTerrainSmooth(map, original, s);
    CHECK(!r.ok());
    CHECK(map.data() == before);
}

TEST_CASE("procgen.effect.ridgesApplyHorizontalThenVerticalAndClipAfterMask") {
    auto map = filled(3, 3, 0.5F), mask = filled(3, 3, 1);
    map.setHeight(0, 1, 0);
    map.setHeight(2, 1, 1);
    map.setHeight(1, 0, 0.2F);
    map.setHeight(1, 2, 0.8F);
    TerrainRidgeSettings s;
    s.exponent    = 2;
    s.passes      = 1;
    s.maximum     = 1;
    s.mixStrength = 1;
    auto r        = applyTerrainRidges(map, mask, s);
    REQUIRE(r.ok());
    CHECK(std::abs(map.height(1, 1) - 0.2041666667) < 1e-7);
    map  = row({-2, 2});
    mask = row({0, 0});
    r    = applyTerrainRidges(map, mask, s);
    REQUIRE(r.ok());
    CHECK(map.data() == row({0, 1}).data());
}

TEST_CASE("procgen.effect.ridgePassCountAndAliasUseStableMask") {
    const auto           original = row({0.1F, 0.4F, 0.2F, 0.5F});
    auto                 batch = original, repeated = original, alias = original;
    TerrainRidgeSettings s;
    s.passes = 3;
    auto r   = applyTerrainRidges(batch, original, s);
    REQUIRE(r.ok());
    r = applyTerrainRidges(alias, alias, s);
    REQUIRE(r.ok());
    CHECK(batch.data() == alias.data());
    s.passes = 1;
    for (int i = 0; i < 3; ++i) {
        r = applyTerrainRidges(repeated, original, s);
        REQUIRE(r.ok());
    }
    CHECK(batch.data() == repeated.data());
    s.passes = 0;
    r        = applyTerrainRidges(batch, original, s);
    REQUIRE(r.ok());
    CHECK(batch.data() == repeated.data());
    s.passes = -1;
    r        = applyTerrainRidges(batch, original, s);
    CHECK(!r.ok());
    CHECK(batch.data() == repeated.data());
}

TEST_CASE("procgen.effect.terraceNearestEvenAndOneSidedBevel") {
    auto                   map  = row({-2.5F, -1.5F, -0.5F, 0.5F, 1.5F, 2.5F});
    auto                   mask = filled(6, 1, 1);
    TerrainTerraceSettings s;
    s.count = 1;
    auto r  = applyTerrainTerrace(map, mask, s);
    REQUIRE(r.ok());
    CHECK(map.data() == row({-2, -2, 0, 0, 2, 2}).data());
    map     = row({0.25F, 0.375F, 0.75F});
    mask    = filled(3, 1, 1);
    s.bevel = 0.75F;
    r       = applyTerrainTerrace(map, mask, s);
    REQUIRE(r.ok());
    CHECK(map.data() == row({0, 0.375F, 1}).data());
    auto before = map.data();
    s.count     = 0;
    r           = applyTerrainTerrace(map, mask, s);
    CHECK(!r.ok());
    CHECK(map.data() == before);
}

TEST_CASE("procgen.effect.powerUsesFourMinusControlAndAtomicDomainValidation") {
    auto map = row({0.25F, 0.5F, 1}), mask = row({1, 0.5F, 1});
    auto r = applyTerrainPower(map, mask, 2);
    REQUIRE(r.ok());
    CHECK(map.data() == row({0.0625F, 0.375F, 1}).data());
    map  = row({0.25F, 0.5F, 1});
    mask = row({1, 1, 1});
    r    = applyTerrainPower(map, mask, 5);
    REQUIRE(r.ok());
    CHECK(map.data() == row({4, 2, 1}).data());
    map         = row({0.25F, 0.5F, -1});
    auto before = map.data();
    r           = applyTerrainPower(map, mask, 2);
    CHECK(!r.ok());
    CHECK(map.data() == before);
    mask.setHeight(2, 0, 0);
    r = applyTerrainPower(map, mask, 2);
    REQUIRE(r.ok());
    CHECK(map.data() == row({0.0625F, 0.25F, -1}).data());
    map    = row({0, 1, 1});
    before = map.data();
    r      = applyTerrainPower(map, mask, 4);
    CHECK(!r.ok());
    CHECK(map.data() == before);
}

TEST_CASE("procgen.effect.heightCurveUsesRangeWithoutAddingMinimum") {
    auto map = row({2, 3, 4}), mask = row({1, 0.5F, 1}), curve = row({0, 1});
    auto r = applyTerrainHeightCurve(map, mask, curve, 2, 4);
    REQUIRE(r.ok());
    CHECK(map.data() == row({0, 2, 2}).data());
    map           = row({0, 0.5F, 1});
    mask          = row({1, 1, 1});
    auto original = map;
    auto expected = map;
    r             = applyTerrainHeightCurve(expected, mask, original, 0, 1);
    REQUIRE(r.ok());
    r = applyTerrainHeightCurve(map, mask, map, 0, 1);
    REQUIRE(r.ok());
    CHECK(map.data() == expected.data());
    curve       = row({0, 0, std::numeric_limits<float>::max()});
    map         = row({0, 1, 2});
    auto before = map.data();
    r           = applyTerrainHeightCurve(map, mask, curve, 0, 2);
    CHECK(!r.ok());
    CHECK(map.data() == before);
}

TEST_CASE("procgen.effect.heightMixLocalGlobalStrengthAndClipping") {
    auto                     map = row({0.25F, 0.25F, 0.25F}), local = row({0, 0.5F, 1}), global = row({1, 1, 0.25F});
    TerrainHeightMixSettings s;
    s.strength = 2;
    auto r     = applyTerrainHeightMix(map, local, global, s);
    REQUIRE(r.ok());
    CHECK(map.data() == row({0, 0.25F, 0.5F}).data());
    map    = row({-1, 2, 0.2F});
    global = row({0, 0, 0});
    r      = applyTerrainHeightMix(map, local, global, s);
    REQUIRE(r.ok());
    CHECK(map.data() == row({0, 0.5F, 0.2F}).data());
    auto original = row({0.1F, 0.2F, 0.3F}), alias = original, expected = original;
    r = applyTerrainHeightMix(expected, original, original, s);
    REQUIRE(r.ok());
    r = applyTerrainHeightMix(alias, alias, alias, s);
    REQUIRE(r.ok());
    CHECK(alias.data() == expected.data());
    s.clipMaximum = s.clipMinimum;
    auto before   = alias.data();
    r             = applyTerrainHeightMix(alias, local, global, s);
    CHECK(!r.ok());
    CHECK(alias.data() == before);
}

TEST_CASE("procgen.effect.smoothPreservesFiniteConstantExtrema") {
    const auto mask = filled(3, 2, 1);
    for (float value : {0.0F, -1000.0F, std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()}) {
        auto                  map    = filled(3, 2, value);
        const auto            before = map.data();
        TerrainSmoothSettings s;
        auto                  r = applyTerrainSmooth(map, mask, s);
        REQUIRE(r.ok());
        CHECK(map.data() == before);
    }
}

TEST_CASE("procgen.effect.ridgesRespectNativeWorldHeightBounds") {
    auto scalar = row({0.1F, 0.25F, 0.4F}), world = scalar, mask = row({1, 1, 1});
    for (float& value : world.data()) value = value * 200 + 100;
    TerrainRidgeSettings s;
    s.passes   = 2;
    s.exponent = 2;
    auto r     = applyTerrainRidges(scalar, mask, s);
    REQUIRE(r.ok());
    s.minimum = 100;
    s.maximum = 200;
    r         = applyTerrainRidges(world, mask, s);
    REQUIRE(r.ok());
    for (int x = 0; x < 3; ++x) CHECK(std::abs(world.height(x, 0) - (scalar.height(x, 0) * 200 + 100)) < 1e-4F);
}
