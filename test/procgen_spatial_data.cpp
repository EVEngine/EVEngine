#include "procgen/Procgen.h"
#include "procgen/SpatialData.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>

using namespace eve::procgen;

TEST_CASE("procgen.spatialData.volumeComposition") {
    const SpatialData world = SpatialData::box(0.f, 0.f, 0.f, 10.f, 4.f, 10.f);
    const SpatialData hole  = SpatialData::sphere(5.f, 2.f, 5.f, 2.f);
    const SpatialData buildable = SpatialData::subtract(world, hole);

    CHECK_EQ(world.getKind(), std::string("volume.box"));
    CHECK(world.contains(1.f, 1.f, 1.f));
    CHECK(!world.contains(11.f, 1.f, 1.f));
    CHECK(!buildable.contains(5.f, 2.f, 5.f));
    CHECK(buildable.contains(1.f, 1.f, 1.f));
    CHECK(buildable.hasBounds());
    CHECK_EQ(buildable.getMaxX(), 10.f);

    const SpatialData overlap = SpatialData::intersect(
        world, SpatialData::box(8.f, -2.f, 8.f, 12.f, 8.f, 12.f));
    CHECK(overlap.contains(9.f, 2.f, 9.f));
    CHECK(!overlap.contains(7.f, 2.f, 9.f));
    CHECK_EQ(overlap.getMinX(), 8.f);
    CHECK_EQ(overlap.getMaxZ(), 10.f);
}

TEST_CASE("procgen.spatialData.splineAndDeterministicSampling") {
    PointSet control;
    control.add(0.f, 0.f, 0.f);
    control.add(10.f, 0.f, 0.f);
    const SpatialData road = SpatialData::spline(control, 1.f);
    CHECK(road.contains(5.f, 0.5f, 0.f));
    CHECK(!road.contains(5.f, 2.f, 0.f));

    const SpatialData volume = SpatialData::box(0.f, 0.f, 0.f, 4.f, 2.f, 4.f);
    const PointSet    first  = volume.sample(2.f, 42, 0.f);
    const PointSet    second = volume.sample(2.f, 42, 0.f);
    CHECK_EQ(first.getCount(), 18);
    CHECK_EQ(first.getCount(), second.getCount());
    for (int i = 0; i < first.getCount(); ++i) {
        CHECK_EQ(first.getX(i), second.getX(i));
        CHECK_EQ(first.getY(i), second.getY(i));
        CHECK_EQ(first.getZ(i), second.getZ(i));
        CHECK_EQ(first.getPointSeed(i), second.getPointSeed(i));
    }
}

TEST_CASE("procgen.spatialData.scriptFacingApiFiltersPoints") {
    Procgen proc;
    auto    includeResult = proc.boxVolumeHandle(0.f, -1.f, 0.f, 5.f, 1.f, 5.f);
    auto    excludeResult = proc.sphereVolumeHandle(2.f, 0.f, 2.f, 1.f);
    REQUIRE(includeResult.ok());
    REQUIRE(excludeResult.ok());
    auto include      = std::move(includeResult).takeValue();
    auto exclude      = std::move(excludeResult).takeValue();
    auto domainResult = proc.differenceSpatialHandle(include, exclude);
    REQUIRE(domainResult.ok());
    auto domain = std::move(domainResult).takeValue();

    auto inputResult = proc.newPointSetHandle();
    REQUIRE(inputResult.ok());
    auto input     = std::move(inputResult).takeValue();
    auto inputView = proc.resolvePointSet(input);
    REQUIRE(inputView.isBound());
    inputView->add(1.f, 0.f, 1.f);
    inputView->add(2.f, 0.f, 2.f);
    inputView->add(9.f, 0.f, 9.f);
    auto acceptedResult = proc.filterSpatialHandle(input, domain, false);
    auto rejectedResult = proc.filterSpatialHandle(input, domain, true);
    REQUIRE(acceptedResult.ok());
    REQUIRE(rejectedResult.ok());
    auto accepted     = std::move(acceptedResult).takeValue();
    auto rejected     = std::move(rejectedResult).takeValue();
    auto acceptedView = proc.resolvePointSet(accepted);
    auto rejectedView = proc.resolvePointSet(rejected);
    REQUIRE(acceptedView.isBound());
    REQUIRE(rejectedView.isBound());
    CHECK_EQ(acceptedView->getCount(), 1);
    CHECK_EQ(rejectedView->getCount(), 2);

    REQUIRE(proc.releasePointSet(rejected).ok());
    REQUIRE(proc.releasePointSet(accepted).ok());
    REQUIRE(proc.releasePointSet(input).ok());
    REQUIRE(proc.release(domain).ok());
    REQUIRE(proc.release(exclude).ok());
    REQUIRE(proc.release(include).ok());
}

TEST_CASE("procgen.spatialData.heightfieldSamplesAndProjectsSurface") {
    Heightmap map(3, 3);
    for (int z = 0; z < 3; ++z)
        for (int x = 0; x < 3; ++x) map.setHeight(x, z, float(x + z));

    const SpatialData surface = SpatialData::heightfield(map, 10.f, 20.f, 2.f, 3.f);
    CHECK_EQ(surface.getKind(), std::string("surface.heightfield"));
    CHECK_EQ(surface.getMinX(), 10.f);
    CHECK_EQ(surface.getMaxZ(), 24.f);
    CHECK(surface.contains(12.f, 6.f, 22.f));

    const PointSet samples = surface.sample(2.f, 91, 0.f);
    CHECK_EQ(samples.getCount(), 9);
    CHECK_EQ(samples.getX(0), 10.f);
    CHECK_EQ(samples.getZ(0), 20.f);
    CHECK_EQ(samples.getY(4), 6.f);
    CHECK(samples.getNormalY(4) > 0.f);
    CHECK(samples.getNormalX(4) < 0.f);

    PointSet source;
    source.add(14.f, -100.f, 24.f);
    const PointSet projected = surface.project(source);
    CHECK_EQ(projected.getY(0), 12.f);
}

TEST_CASE("procgen.pointOperations.matchCorePcgNodes") {
    PointSet first;
    const int a = first.add(1.f, 0.f, 0.f);
    first.setDensity(a, 1.f);
    first.setFloatAttribute(a, "age", 12.f);
    first.setStringAttribute(a, "biome", "forest");

    PointSet second;
    const int b = second.add(0.f, 0.f, 2.f);
    second.setDensity(b, 0.f);
    second.setFloatAttribute(b, "age", 2.f);
    second.setStringAttribute(b, "biome", "desert");

    const PointSet merged = mergePointSets(first, second);
    CHECK_EQ(merged.getCount(), 2);
    const PointSet transformed =
        transformPointSet(merged, 10.f, 3.f, 20.f, 90.f, 2.f, 1.f, 2.f);
    CHECK(std::abs(transformed.getX(0) - 10.f) < 0.0001f);
    CHECK(std::abs(transformed.getZ(0) - 22.f) < 0.0001f);
    CHECK_EQ(transformed.getY(0), 3.f);
    CHECK_EQ(transformed.getYaw(0), 90.f);
    CHECK_EQ(transformed.getScaleX(0), 2.f);

    const PointSet mature = filterPointFloatAttribute(merged, "age", 10.f, 20.f, false);
    const PointSet forest = filterPointStringAttribute(merged, "biome", "forest", false);
    CHECK_EQ(mature.getCount(), 1);
    CHECK_EQ(forest.getCount(), 1);
    CHECK_EQ(forest.getStringAttribute(0, "biome", ""), std::string("forest"));

    const PointSet culledA = densityCullPoints(merged, 123, 1.f);
    const PointSet culledB = densityCullPoints(merged, 123, 1.f);
    CHECK_EQ(culledA.getCount(), 1);
    CHECK_EQ(culledA.getCount(), culledB.getCount());
    CHECK_EQ(culledA.getPointSeed(0), culledB.getPointSeed(0));
}
