#include "procgen/Procgen.h"
#include "procgen/MeshBuild.h"
#include "procgen/SpatialData.h"

#include "common/Capability.h"
#include "common/ProcgenWorldQuery.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cmath>

using namespace eve::procgen;

namespace {

class FlatProcgenWorldQuery final : public eve::IProcgenWorldQuery {
public:
    eve::Result<eve::ProcgenSurfaceHit> projectDown(float x, float z, float maxY, float minY,
                                                    std::uint64_t) override {
        if (maxY < 4.f || minY > 4.f)
            return eve::Result<eve::ProcgenSurfaceHit>::success({});
        return eve::Result<eve::ProcgenSurfaceHit>::success(
            {true, 91, x, 4.f, z, 0.f, 1.f, 0.f});
    }
};

struct CapabilityReset {
    CapabilityReset() { eve::cap::detail::clearAllRaw(); }
    ~CapabilityReset() { eve::cap::detail::clearAllRaw(); }
};

}  // namespace

TEST_CASE("procgen.pointSet.exposesProductionPointSemantics") {
    PointSet points;
    const int index = points.add(1.f, 2.f, 3.f);
    points.setRotation(index, 10.f, 20.f, 30.f);
    points.setBounds(index, 2.f, 3.f, 4.f, -2.f, -3.f, -4.f);
    points.setColor(index, 1.2f, 0.5f, -0.5f, 0.75f);
    points.setSteepness(index, 2.f);

    CHECK_EQ(points.getPitch(index), 10.f);
    CHECK_EQ(points.getYaw(index), 20.f);
    CHECK_EQ(points.getRoll(index), 30.f);
    CHECK_EQ(points.getBoundsMinX(index), -2.f);
    CHECK_EQ(points.getBoundsMinY(index), -3.f);
    CHECK_EQ(points.getBoundsMinZ(index), -4.f);
    CHECK_EQ(points.getBoundsMaxX(index), 2.f);
    CHECK_EQ(points.getBoundsMaxY(index), 3.f);
    CHECK_EQ(points.getBoundsMaxZ(index), 4.f);
    CHECK_EQ(points.getColorR(index), 1.f);
    CHECK_EQ(points.getColorG(index), 0.5f);
    CHECK_EQ(points.getColorB(index), 0.f);
    CHECK_EQ(points.getColorA(index), 0.75f);
    CHECK_EQ(points.getSteepness(index), 1.f);
}

TEST_CASE("procgen.worldQuery.coversProviderPresenceAndAbsence") {
    CapabilityReset reset;
    Procgen          proc;
    auto             inputResult = proc.newPointSetHandle();
    REQUIRE(inputResult.ok());
    const auto input = std::move(inputResult).takeValue();
    auto       view  = proc.resolvePointSet(input);
    REQUIRE(view.isBound());
    view->add(2.f, 100.f, 3.f);

    auto absent = proc.projectToWorldHandle(input, 20.f, -20.f, ~std::uint64_t{0}, false);
    CHECK(!absent.ok());
    REQUIRE(absent.error() != nullptr);
    CHECK_EQ(absent.error()->code(), eve::DiagnosticCode::Unsupported);

    FlatProcgenWorldQuery provider;
    eve::cap::provide<eve::IProcgenWorldQuery>(&provider);
    auto projectedResult = proc.projectToWorldHandle(input, 20.f, -20.f, ~std::uint64_t{0}, false);
    REQUIRE(projectedResult.ok());
    const auto projected = std::move(projectedResult).takeValue();
    auto       projectedView = proc.resolvePointSet(projected);
    REQUIRE(projectedView.isBound());
    CHECK_EQ(projectedView->getCount(), 1);
    CHECK_EQ(projectedView->getY(0), 4.f);
    CHECK_EQ(projectedView->getNormalY(0), 1.f);
    CHECK_EQ(projectedView->getIntAttribute(0, "worldObjectId", 0), 91);
    REQUIRE(proc.releasePointSet(projected).ok());
    REQUIRE(proc.releasePointSet(input).ok());
}

TEST_CASE("procgen.spatialData.squirrelFacadeExposesComposablePrimitives") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local procgen = eve.Procgen();
        local pointsResult = procgen.newPointSet();
        if (pointsResult.ok) {
            local points = pointsResult.value;
            points.add(1.0, 0.0, 1.0);
            points.add(5.0, 0.0, 5.0);
            points.add(9.0, 0.0, 9.0);
            points.setFloatAttribute(0, "priority", 2.0);
            points.setFloatAttribute(1, "priority", 0.5);
            points.setFloatAttribute(2, "priority", 3.0);
            points.setIntAttribute(0, "variant", 7);
            points.setBoolAttribute(0, "enabled", true);
            points.setVectorAttribute(0, "wind", 1.0, 2.0, 3.0);
            local worldResult = procgen.boxVolume(0.0, -1.0, 0.0, 10.0, 1.0, 10.0);
            local holeResult = procgen.sphereVolume(5.0, 0.0, 5.0, 1.5);
            if (worldResult.ok && holeResult.ok) {
                local domainResult = procgen.differenceSpatial(worldResult.value, holeResult.value);
                if (domainResult.ok) {
                    local filteredResult = procgen.filterSpatial(points, domainResult.value, false);
                    local priorityResult = procgen.filterFloatAttribute(points, "priority", 1.0, 4.0, false);
                    local transformedResult = procgen.transformPoints(
                        points, 2.0, 0.0, 3.0, 90.0, 1.0, 1.0, 1.0);
                    local transformed3DResult = procgen.transformPoints3D(
                        points, 0.0, 0.0, 0.0, 90.0, 0.0, 0.0, 1.0, 1.0, 1.0);
                    local unionResult = procgen.unionPoints(points, filteredResult.value);
                    local intersectResult = procgen.intersectPoints(points, filteredResult.value);
                    local pointDifferenceResult = procgen.differencePoints(points, filteredResult.value);
                    local copyResult = procgen.copyPoints(filteredResult.value, points, true);
                    local remapResult = procgen.remapDensity(points, 0.0, 1.0, 0.0, 0.5, true);
                    local mathResult = procgen.mathFloatAttribute(
                        points, "priority", "weightedPriority", "multiply", 2.0, 0.0);
                    local sampledResult = procgen.sampleSpatial(domainResult.value, 5.0, 7, 0.0);
                    local poissonResult = procgen.poissonDisk(10, 10, 2.0, 9, 32);
                    if (points.getAttributeType(0, "variant") == "int" &&
                        points.getAttributeType(0, "enabled") == "bool" &&
                        points.getAttributeType(0, "wind") == "vector" &&
                        points.getIntAttribute(0, "variant", 0) == 7 &&
                        points.getBoolAttribute(0, "enabled", false) &&
                        points.getVectorAttributeZ(0, "wind", 0.0) == 3.0 &&
                        filteredResult.ok && filteredResult.value.getCount() == 2 &&
                        priorityResult.ok && priorityResult.value.getCount() == 2 &&
                        transformedResult.ok && transformedResult.value.getCount() == 3 &&
                        transformed3DResult.ok && transformed3DResult.value.getY(0) == -1.0 &&
                        unionResult.ok && unionResult.value.getCount() == 3 &&
                        intersectResult.ok && intersectResult.value.getCount() == 2 &&
                        pointDifferenceResult.ok && pointDifferenceResult.value.getCount() == 1 &&
                        copyResult.ok && copyResult.value.getCount() == 6 &&
                        remapResult.ok && remapResult.value.getDensity(0) == 0.5 &&
                        mathResult.ok && mathResult.value.getFloatAttribute(
                            0, "weightedPriority", 0.0) == 4.0 &&
                        sampledResult.ok && sampledResult.value.getCount() > 0 &&
                        poissonResult.ok && poissonResult.value.getCount() > 0)
                        result = "ok";
                }
            }
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
}

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

TEST_CASE("procgen.spatialData.polygonTextureMaskAndMeshSurface") {
    PointSet polygon;
    polygon.add(0.f, 0.f, 0.f);
    polygon.add(4.f, 0.f, 0.f);
    polygon.add(4.f, 0.f, 4.f);
    polygon.add(0.f, 0.f, 4.f);
    const SpatialData polygonVolume = SpatialData::polygon(polygon, -1.f, 2.f);
    CHECK_EQ(polygonVolume.getKind(), std::string("volume.polygon"));
    CHECK(polygonVolume.contains(2.f, 0.f, 2.f));
    CHECK(!polygonVolume.contains(5.f, 0.f, 2.f));

    Heightmap mask(2, 2);
    mask.setHeight(0, 0, 0.f);
    mask.setHeight(1, 0, 1.f);
    mask.setHeight(0, 1, 1.f);
    mask.setHeight(1, 1, 1.f);
    const SpatialData textureMask =
        SpatialData::textureMask(mask, 10.f, 20.f, 2.f, 0.75f, 1.f, -2.f, 3.f);
    CHECK_EQ(textureMask.getKind(), std::string("volume.texture_mask"));
    CHECK(textureMask.contains(12.f, 0.f, 22.f));
    CHECK(!textureMask.contains(10.f, 0.f, 20.f));

    MeshBuild mesh;
    mesh.addVertex(0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f);
    mesh.addVertex(4.f, 1.f, 0.f, 0.f, 1.f, 0.f, 1.f, 0.f);
    mesh.addVertex(0.f, 3.f, 4.f, 0.f, 1.f, 0.f, 0.f, 1.f);
    mesh.addTriangle(0, 1, 2);
    const SpatialData surface = SpatialData::meshSurface(mesh, 0.1f);
    CHECK_EQ(surface.getKind(), std::string("surface.mesh"));
    CHECK(surface.contains(1.f, 1.5f, 1.f));
    PointSet source;
    source.add(1.f, 100.f, 1.f);
    const PointSet projected = surface.project(source);
    CHECK_EQ(projected.getY(0), 1.5f);
    CHECK(projected.getNormalY(0) > 0.f);
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
