// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "default": "script-binding",
  "module": "procgen",
  "modules": [
    "procgen",
    "timer",
    "transaction",
    "ui",
    "vehicle",
    "weapon",
    "weather"
  ],
  "origins": [
    "terrain_curve_texture.cpp",
    "terrain_detail.cpp",
    "terrain_erosion.cpp",
    "terrain_generation_session.cpp",
    "terrain_legacy_hydraulic.cpp",
    "terrain_multitile.cpp",
    "terrain_probe.cpp",
    "terrain_sediment.cpp",
    "terrain_smooth_stamp_script.cpp",
    "terrain_stamp_script.cpp",
    "terrain_tree.cpp",
    "terrain_water.cpp",
    "thunder_strike.cpp",
    "timer.cpp",
    "transaction.cpp",
    "ui_tween_script.cpp",
    "vehicle.cpp",
    "weapon.cpp"
  ],
  "cases": {
    "performance": [
      "procgen.terrain.performance.real1kRoundTripAnd1k4k8kRandomAccess"
    ]
  }
}
*/
// clang-format on

// Related integration contracts share one translation unit to amortize header parsing.

// Contract section: terrain curve texture
#include "PrivateTestScope.h"
#include "zeroerr/assert.h"

#include <zeroerr/unittest.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <simplesquirrel/simplesquirrel.hpp>
#include "image/ImageData.h"
#include "procgen/PointSet.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainAsset.h"
#include "procgen/heightmap/TerrainCurveTexture.h"
#include "procgen/heightmap/TerrainDerivedMap.h"
#include "procgen/heightmap/TerrainFile.h"
#include "procgen/heightmap/TerrainObjectPlacement.h"
#include "procgen/heightmap/TerrainStamp.h"
#include "procgen/heightmap/TerrainStampScript.h"
#include "procgen/heightmap/TerrainStreaming.h"
#include "zeroerr/unittest.h"
// terrain_curve_texture.cpp
namespace test_terrain_curve_texture {
// terrain_curve_texture.cpp


TEST_CASE("procgen.terrainCurveTexture.bindsReceiptToSquirrel") {
    eve::procgen::Heightmap curve(2, 1);
    curve.data() = {0.2F, 0.8F};
    eve::image::ImageData output(2, 1, "RGBA32F");
    ssq::VM               vm(1024);
    auto                  table = vm.addTable("eve");
    table.addClass<eve::image::ImageData>(
        "ImageData", std::function<eve::image::ImageData*()>([]() -> eve::image::ImageData* { return nullptr; }), true);
    eve::procgen::exposeHeightmap(table);
    eve::procgen::exposeTerrainCurveTexture(table);
    vm.addFunc("output", [&]() { return &output; });
    vm.addFunc("curve", [&]() { return &curve; });
    vm.run(vm.compileSource(R"(
        local result=eve.bakeTerrainCurveTexture(output(),curve());
        assert(result.ok && result.value.writtenPixels==2);
        assert(result.value.minimum>0.19 && result.value.minimum<0.21);
        assert(result.value.maximum==0.5);
    )"));
    CHECK(output.getPixel(0, 0).r == 0);
    CHECK(output.getPixel(1, 0).r == 0.5F);
}

// terrain_derived_map.cpp
using namespace eve::procgen;


TEST_CASE("procgen.derivedMap.scriptBinding") {
    Heightmap source(3, 3), curvature(3, 3), aspect(3, 3), filtered(3, 3);
    source.data() = {0.F, 0.1F, 0.2F, 0.F, 0.1F, 0.2F, 0.F, 0.1F, 0.2F};
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("derivedSource", [&]() { return &source; });
    vm.addFunc("derivedCurvature", [&]() { return &curvature; });
    vm.addFunc("derivedAspect", [&]() { return &aspect; });
    vm.addFunc("derivedFiltered", [&]() { return &filtered; });
    vm.run(vm.compileSource(R"(
        assert(eve.generateTerrainHeightmapCurvature(derivedCurvature(),derivedSource(),0).ok);
        assert(eve.generateTerrainHeightmapAspect(derivedAspect(),derivedSource(),2).ok);
        assert(!eve.generateTerrainHeightmapAspect(derivedAspect(),derivedSource(),9).ok);
        assert(eve.filterTerrainHeightmapNeighborhood(derivedFiltered(),derivedSource(),1,0).ok);
        assert(!eve.filterTerrainHeightmapNeighborhood(derivedFiltered(),derivedSource(),1,9).ok);
    )"));
}


TEST_CASE("procgen.heightmapFilter.convolveAndScriptBinding") {
    Heightmap source(3, 3), output(3, 3), kernel(3, 3);
    source.data() = {0.F, 0.F, 0.F, 0.F, 1.F, 0.F, 0.F, 0.F, 0.F};
    kernel.data().assign(9, 1.F);
    REQUIRE(convolveTerrainHeightmap(output, source, kernel).ok());
    CHECK(std::abs(output.height(1, 1) - 1.F / 9.F) < 0.000001F);
    CHECK(output.height(0, 0) == 0.F);

    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("filterSource", [&]() { return &source; });
    vm.addFunc("filterOutput", [&]() { return &output; });
    vm.addFunc("filterKernel", [&]() { return &kernel; });
    vm.run(vm.compileSource(R"(
        assert(eve.smoothTerrainHeightmap(filterOutput(),filterSource(),1).ok);
        assert(eve.smoothTerrainHeightmapRadius(filterOutput(),filterSource(),5).ok);
        assert(eve.convolveTerrainHeightmap(filterOutput(),filterSource(),filterKernel()).ok);
    )"));
}


TEST_CASE("procgen.heightmapSlope.quantizeAndBindings") {
    Heightmap source(2, 2), output(2, 2);
    source.data() = {0.125F, 0.375F, 0.625F, 0.875F};
    REQUIRE(quantizeTerrainHeightmap(output, source, 0.25F).ok());
    const std::vector<float> expected{0.F, 0.5F, 0.5F, 1.F};
    CHECK(output.data() == expected);
    const auto before = output.data();
    CHECK(!quantizeTerrainHeightmap(output, source, 0.F).ok());
    CHECK(output.data() == before);

    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("slopeSource", [&]() { return &source; });
    vm.addFunc("slopeOutput", [&]() { return &output; });
    vm.run(vm.compileSource(R"(
        assert(eve.generateTerrainHeightmapSlope(slopeOutput(),slopeSource()).ok);
        assert(eve.quantizeTerrainHeightmap(slopeOutput(),slopeSource(),0.25).ok);
        assert(!eve.quantizeTerrainHeightmap(slopeOutput(),slopeSource(),0.0).ok);
    )"));
}


TEST_CASE("procgen.heightmapArithmetic.resampleLerpAndBindings") {
    Heightmap source(4, 1), operand(2, 1), mask(2, 1), output(4, 1);
    source.data().assign(4, 1.F);
    operand.data() = {0.F, 1.F};
    mask.data()    = {-1.F, 2.F};
    REQUIRE(applyTerrainHeightmapRasterArithmetic(output, source, operand, TerrainHeightmapArithmetic::Subtract, false,
                                                  0.F, 0.F)
                .ok());
    const std::vector<float> expectedSubtract{1.F, 0.5F, 0.F, 0.F};
    CHECK(output.data() == expectedSubtract);
    REQUIRE(lerpTerrainHeightmap(output, source, operand, mask).ok());
    const std::vector<float> expectedLerp{1.F, 0.75F, 1.F, 1.F};
    CHECK(output.data() == expectedLerp);

    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("arithmeticSource", [&]() { return &source; });
    vm.addFunc("arithmeticOperand", [&]() { return &operand; });
    vm.addFunc("arithmeticMask", [&]() { return &mask; });
    vm.addFunc("arithmeticOutput", [&]() { return &output; });
    vm.run(vm.compileSource(R"(
        assert(eve.applyTerrainHeightmapScalarArithmetic(arithmeticOutput(),arithmeticSource(),0.5,0,true,0.0,1.0).ok);
        assert(eve.applyTerrainHeightmapRasterArithmetic(arithmeticOutput(),arithmeticSource(),arithmeticOperand(),2,false,0.0,1.0).ok);
        assert(eve.lerpTerrainHeightmap(arithmeticOutput(),arithmeticSource(),arithmeticOperand(),arithmeticMask()).ok);
    )"));
}

TEST_CASE("procgen.heightmapTransform.sourceFormulasAndAtomicFailure") {
    Heightmap source(3, 1), output(3, 1);
    source.data() = {0.25F, 0.5F, 0.75F};
    REQUIRE(transformTerrainHeightmap(output, source, TerrainHeightmapTransform::Invert, 0.F).ok());
    const std::vector<float> inverted{0.75F, 0.5F, 0.25F};
    CHECK(output.data() == inverted);
    REQUIRE(transformTerrainHeightmap(output, source, TerrainHeightmapTransform::Normalise, 0.F).ok());
    const std::vector<float> normalized{0.F, 0.5F, 1.F};
    CHECK(output.data() == normalized);
    REQUIRE(transformTerrainHeightmap(output, source, TerrainHeightmapTransform::Power, 2.F).ok());
    CHECK(output.height(0, 0) == 0.0625F);
    REQUIRE(transformTerrainHeightmap(output, source, TerrainHeightmapTransform::Contrast, 2.F).ok());
    const std::vector<float> contrasted{0.F, 0.5F, 1.F};
    CHECK(output.data() == contrasted);
    const auto before = output.data();
    CHECK(!transformTerrainHeightmap(output, source, static_cast<TerrainHeightmapTransform>(8), 1.F).ok());
    CHECK(output.data() == before);

    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("transformSource", [&]() { return &source; });
    vm.addFunc("transformOutput", [&]() { return &output; });
    vm.run(vm.compileSource(R"(
        assert(eve.transformTerrainHeightmap(transformOutput(),transformSource(),1,0.0).ok);
        assert(eve.transformTerrainHeightmap(transformOutput(),transformSource(),3,2.0).ok);
    )"));
}


TEST_CASE("procgen.heightmapCopy.clampAtomicAndBindings") {
    Heightmap source(2, 1), target(3, 1);
    source.data() = {-1.F, 2.F};
    REQUIRE(copyTerrainHeightmapClamped(target, source, 0.2F, 0.8F).ok());
    const std::vector<float> expected{0.2F, 0.8F, 0.8F};
    CHECK(target.data() == expected);
    const auto before = target.data();
    CHECK(!copyTerrainHeightmap(target, source, static_cast<TerrainHeightmapCopy>(9)).ok());
    CHECK(target.data() == before);

    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("copySource", [&]() { return &source; });
    vm.addFunc("copyTarget", [&]() { return &target; });
    vm.run(vm.compileSource(R"(
        assert(eve.copyTerrainHeightmap(copyTarget(),copySource(),0).ok);
        assert(eve.copyTerrainHeightmapClamped(copyTarget(),copySource(),0.0,1.0).ok);
        assert(!eve.copyTerrainHeightmap(copyTarget(),copySource(),9).ok);
    )"));
}

TEST_CASE("procgen.heightmapFlip.rectangularAliasAndBinding") {
    Heightmap source(3, 2), output(1, 1);
    source.data() = {1.F, 2.F, 3.F, 4.F, 5.F, 6.F};
    REQUIRE(flipTerrainHeightmap(output, source).ok());
    CHECK(output.getWidth() == 2);
    CHECK(output.getHeight() == 3);
    const std::vector<float> expected{1.F, 4.F, 2.F, 5.F, 3.F, 6.F};
    CHECK(output.data() == expected);
    REQUIRE(flipTerrainHeightmap(source, source).ok());
    CHECK(source.getWidth() == 2);
    CHECK(source.getHeight() == 3);
    CHECK(source.data() == expected);

    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("flipSource", [&]() { return &source; });
    vm.addFunc("flipOutput", [&]() { return &output; });
    vm.run(vm.compileSource(R"(
        assert(eve.flipTerrainHeightmap(flipOutput(),flipSource()).ok);
        assert(flipOutput().getWidth()==3 && flipOutput().getHeight()==2);
    )"));
}

TEST_CASE("procgen.heightmapMeasure.sourceOrderAndBaseLevel") {
    Heightmap source(3, 3);
    source.data() = {-2.F, -1.F, -3.F, -4.F, 10.F, -5.F, -6.F, -7.F, -8.F};
    auto minimum  = measureTerrainHeightmap(source, TerrainHeightmapMeasure::Minimum);
    auto maximum  = measureTerrainHeightmap(source, TerrainHeightmapMeasure::Maximum);
    auto sum      = measureTerrainHeightmap(source, TerrainHeightmapMeasure::Sum);
    auto average  = measureTerrainHeightmap(source, TerrainHeightmapMeasure::Average);
    auto base     = measureTerrainHeightmap(source, TerrainHeightmapMeasure::BaseLevel);
    REQUIRE(minimum.ok());
    REQUIRE(maximum.ok());
    REQUIRE(sum.ok());
    REQUIRE(average.ok());
    REQUIRE(base.ok());
    CHECK(minimum.value() == -8.0);
    CHECK(maximum.value() == 10.0);
    CHECK(sum.value() == -26.0);
    CHECK(std::abs(average.value() + 26.0 / 9.0) < 0.000001);
    CHECK(base.value() == 0.0);

    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("measureSource", [&]() { return &source; });
    vm.run(vm.compileSource(R"(
        assert(eve.measureTerrainHeightmap(measureSource(),0).value==-8.0);
        assert(eve.measureTerrainHeightmap(measureSource(),1).value==10.0);
        assert(eve.measureTerrainHeightmap(measureSource(),4).value==0.0);
        assert(!eve.measureTerrainHeightmap(measureSource(),9).ok);
    )"));
}

TEST_CASE("procgen.heightmapTerraceQuantize.curvesBoundariesAndBinding") {
    Heightmap source(5, 1), output(5, 1), starts(2, 1), curves(3, 2);
    source.data() = {0.F, 0.25F, 0.5F, 0.75F, 1.F};
    starts.data() = {0.F, 0.5F};
    curves.data() = {0.F, 0.F, 1.F, 0.F, 1.F, 1.F};
    REQUIRE(quantizeTerrainHeightmapTerraces(output, source, starts, curves).ok());
    const std::vector<float> expected{0.F, 0.F, 0.5F, 0.75F, 1.F};
    CHECK(output.data() == expected);
    starts.data()     = {0.1F, 0.5F};
    const auto before = output.data();
    CHECK(!quantizeTerrainHeightmapTerraces(output, source, starts, curves).ok());
    CHECK(output.data() == before);
    starts.data() = {0.F, 0.5F};

    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("terraceSource", [&]() { return &source; });
    vm.addFunc("terraceOutput", [&]() { return &output; });
    vm.addFunc("terraceStarts", [&]() { return &starts; });
    vm.addFunc("terraceCurves", [&]() { return &curves; });
    vm.run(vm.compileSource(R"(
        assert(eve.quantizeTerrainHeightmapTerraces(terraceOutput(),terraceSource(),terraceStarts(),terraceCurves()).ok);
    )"));
}

TEST_CASE("procgen.heightmapSlopeQuery.threeSourceFormulasAndBinding") {
    Heightmap source(3, 3);
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x) source.setHeight(x, y, float(x + 2 * y) * 0.1F);
    auto forward = measureTerrainHeightmapSlope(source, 1.F, 1.F, TerrainHeightmapSlopeQuery::GridForward);
    auto central = measureTerrainHeightmapSlope(source, 0.5F, 0.5F, TerrainHeightmapSlopeQuery::NormalizedCentral);
    // Center the four-neighbor stencil on the interior texel, away from edge clamping.
    auto average =
        measureTerrainHeightmapSlope(source, 1.F / 3.F, 1.F / 3.F, TerrainHeightmapSlopeQuery::NormalizedAverage);
    REQUIRE(forward.ok());
    REQUIRE(central.ok());
    REQUIRE(average.ok());
    CHECK(std::abs(forward.value() - std::sqrt(0.05)) < 0.000001);
    CHECK(central.value() == 90.0);
    CHECK(std::abs(average.value() - 60.0) < 0.00001);
    CHECK(!measureTerrainHeightmapSlope(source, 2.F, 2.F, TerrainHeightmapSlopeQuery::GridForward).ok());

    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("slopeQuerySource", [&]() { return &source; });
    vm.run(vm.compileSource(R"(
        assert(eve.measureTerrainHeightmapSlope(slopeQuerySource(),1.0,1.0,0).ok);
        assert(eve.measureTerrainHeightmapSlope(slopeQuerySource(),0.5,0.5,2).ok);
        assert(!eve.measureTerrainHeightmapSlope(slopeQuerySource(),2.0,2.0,0).ok);
    )"));
}

TEST_CASE("procgen.heightmapWrite.safeFillRowsColumnsReset") {
    Heightmap target(3, 2), row(2, 1), column(3, 1);
    REQUIRE(fillTerrainHeightmap(target, 2.F).ok());
    CHECK(target.height(1, 1) == 1.F);
    REQUIRE(setTerrainHeightmapSafe(target, -4, 8, 0.25F).ok());
    CHECK(target.height(0, 1) == 0.25F);
    row.data()    = {0.1F, 0.2F};
    column.data() = {0.3F, 0.4F, 0.5F};
    REQUIRE(setTerrainHeightmapRow(target, 1, row).ok());
    REQUIRE(setTerrainHeightmapColumn(target, 0, column).ok());
    const std::vector<float> expected{0.3F, 0.4F, 0.5F, 0.25F, 0.2F, 1.F};
    CHECK(target.data() == expected);
    const auto before = target.data();
    CHECK(!setTerrainHeightmapRow(target, 3, row).ok());
    CHECK(target.data() == before);

    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("writeTarget", [&]() { return &target; });
    vm.addFunc("writeRow", [&]() { return &row; });
    vm.addFunc("writeColumn", [&]() { return &column; });
    vm.run(vm.compileSource(R"(
        assert(eve.fillTerrainHeightmap(writeTarget(),0.5).ok);
        assert(eve.setTerrainHeightmapSafe(writeTarget(),-1,9,0.2).ok);
        assert(eve.setTerrainHeightmapRow(writeTarget(),1,writeRow()).ok);
        assert(eve.setTerrainHeightmapColumn(writeTarget(),0,writeColumn()).ok);
        assert(eve.resetTerrainHeightmap(writeTarget()).ok);
        assert(writeTarget().getWidth()==0 && writeTarget().getHeight()==0);
    )"));
}

// terrain_file_raw.cpp
using namespace eve::procgen;


// terrain_heightmap_access.cpp
using namespace eve::procgen;


TEST_CASE("procgen.heightmapAccess.scriptContract") {
    Heightmap map(2, 2);
    map.data() = {0, 1, 2, 3};
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("map", [&]() { return &map; });
    vm.run(vm.compileSource(R"(
        assert(map().hasData().ok && map().hasData().value);
        assert(map().isPowerOfTwo().ok && map().isPowerOfTwo().value);
        local a=eve.sampleTerrainHeightmapSafe(map(),-1,9); assert(a.ok && a.value==2.0);
        local b=eve.sampleTerrainHeightmapNormalized(map(),0.25,0.25); assert(b.ok && b.value==1.5);
    )"));
}

// terrain_object.cpp
using namespace eve::procgen;

namespace {
[[maybe_unused]] Heightmap filled(int width, int height, float value) {
    Heightmap result(width, height);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) result.setHeight(x, y, value);
    return result;
}
[[maybe_unused]] TerrainObjectPlacementSettings settings() {
    TerrainObjectPlacementSettings value;
    value.width = value.depth = 2;
    value.spacing             = 1;
    value.spawnDensity        = 1;
    value.jitterPercent = value.failureRate = value.minimumFitness = value.minimumInstanceFitness = 0;
    value.boundsRadius                                                                            = 0.2F;
    value.prototype                                                                               = "rocks";
    value.namespaceId                                                                             = 71;
    value.seed                                                                                    = 5;
    TerrainObjectInstanceSettings instance;
    instance.asset            = "rock-a";
    instance.minimumInstances = instance.maximumInstances = 1;
    value.instances.push_back(instance);
    return value;
}
}  // namespace


TEST_CASE("procgen.terrainObject.scriptConfiguresAndExecutesRealVm") {
    auto     fitness = filled(3, 3, 1), heights = filled(2, 2, 0.25F);
    PointSet output;
    ssq::VM  vm(1024);
    auto     table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("fitness", [&]() { return &fitness; });
    vm.addFunc("heights", [&]() { return &heights; });
    vm.addFunc("output", [&]() { return &output; });
    vm.addFunc("pointCount", [](const PointSet* points) { return points->getCount(); });
    vm.run(vm.compileSource(R"(
        local child=eve.TerrainObjectInstanceSettings();
        child.asset="rock-a"; child.minimumInstances=1; child.maximumInstances=1;
        child.setScaleMode(0); child.setYOffsetMode(0);
        local s=eve.TerrainObjectPlacementSettings();
        s.width=2.0;s.depth=2.0;s.spacing=1.0;s.spawnDensity=1.0;s.jitterPercent=0.0;
        s.minimumFitness=0.0;s.minimumInstanceFitness=0.0;s.prototype="rocks";s.namespaceId=81;
        assert(s.addInstance(child).ok);
        assert(s.exportPoints(output(),fitness(),heights()).value==9);
        assert(pointCount(output())==9);
        assert(s.removePoints(output(),output(),fitness(),0.5).value==9);
        assert(pointCount(output())==0);
    )"));
}


TEST_CASE("procgen.terrainObject.multiTileExecutesThroughRealVm") {
    PointSet empty, output;
    auto     heights = filled(2, 2, 0.25F), fitness = filled(4, 2, 1);
    ssq::VM  vm(1024);
    auto     table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("emptyObjects", [&]() { return &empty; });
    vm.addFunc("heights", [&]() { return &heights; });
    vm.addFunc("fitness", [&]() { return &fitness; });
    vm.addFunc("output", [&]() { return &output; });
    vm.addFunc("pointCount", [](const PointSet* points) { return points->getCount(); });
    vm.run(vm.compileSource(R"(
        local child=eve.TerrainObjectInstanceSettings();child.asset="rock-a";
        local s=eve.TerrainObjectPlacementSettings();s.spacing=1.0;s.spawnDensity=1.0;
        s.jitterPercent=0.0;s.minimumFitness=0.0;s.minimumInstanceFitness=0.0;
        s.prototype="rocks";s.namespaceId=92;s.boundsRadius=0.2;s.maxPoints=100;
        assert(s.addInstance(child).ok);
        local op=eve.TerrainStampSettings();op.setCenter(2.0,1.0);op.setSize(4.0,2.0);
        local w=eve.TerrainMultiObjectWorkspace();
        assert(w.addTile("west",emptyObjects(),heights(),0.0,0.0,2.0,2.0,2,2,false).ok);
        assert(w.addTile("east",emptyObjects(),heights(),2.0,0.0,2.0,2.0,2,2,false).ok);
        assert(w.apply(fitness(),s,op,0,false).ok);
        assert(w.copyTile("east",output()).ok && pointCount(output())>0);
        assert(w.getTileCount()==2 && w.getLastAffectedTiles()==2);
    )"));
}

// terrain_performance.cpp
using namespace eve;
using namespace eve::procgen;

namespace {
constexpr std::size_t kHeaderSize = 44, kEntrySize = 36;
[[maybe_unused]] void put16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(std::uint8_t(value));
    out.push_back(std::uint8_t(value >> 8));
}
[[maybe_unused]] void put32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) out.push_back(std::uint8_t(value >> shift));
}
[[maybe_unused]] void put64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) out.push_back(std::uint8_t(value >> shift));
}
[[maybe_unused]] void putFloat(std::vector<std::uint8_t>& out, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    put32(out, bits);
}
[[maybe_unused]] std::uint32_t checksum(const std::vector<std::uint8_t>& bytes) {
    std::uint32_t value = 2166136261U;
    for (auto byte : bytes) {
        value ^= byte;
        value *= 16777619U;
    }
    return value;
}
[[maybe_unused]] double milliseconds(std::chrono::steady_clock::duration duration) {
    return std::chrono::duration<double, std::milli>(duration).count();
}

/** Full EVTR address space backed only for metadata and the chunks actually read. */
class SparseTerrainArchiveSource final : public ITerrainArchiveSource {
public:
    explicit SparseTerrainArchiveSource(int dimension) {
        constexpr int chunkSize = 64;
        for (int index = 0; index < chunkSize * chunkSize; ++index) {
            put16(chunk_, std::uint16_t(index));
            chunk_.insert(chunk_.end(), {255, 0, 128, 128, 0, 0, 128, 128, 0, 2});
        }
        const int  columns = (dimension + chunkSize - 1) / chunkSize;
        const auto count   = std::uint32_t(columns * columns);
        payloadStart_      = kHeaderSize + std::uint64_t(count) * kEntrySize;
        metadata_.insert(metadata_.end(), {'E', 'V', 'T', 'R'});
        put16(metadata_, 5);
        put16(metadata_, 0);
        put32(metadata_, dimension);
        put32(metadata_, dimension);
        put32(metadata_, chunkSize);
        put32(metadata_, count);
        putFloat(metadata_, 0);
        putFloat(metadata_, 1);
        putFloat(metadata_, 1);
        put64(metadata_, kHeaderSize);
        const auto    chunkChecksum = checksum(chunk_);
        const auto    chunkBytes    = static_cast<std::uint32_t>(chunk_.size());
        std::uint64_t offset        = payloadStart_;
        for (int y = 0; y < columns; ++y)
            for (int x = 0; x < columns; ++x) {
                put32(metadata_, x);
                put32(metadata_, y);
                put16(metadata_, chunkSize);
                put16(metadata_, chunkSize);
                metadata_.insert(metadata_.end(), {0, 0, 0, 0});
                put64(metadata_, offset);
                put32(metadata_, chunkBytes);
                put32(metadata_, chunkBytes);
                put32(metadata_, chunkChecksum);
                offset += chunk_.size();
            }
        size_ = offset;
    }
    [[nodiscard]] std::uint64_t                     size() const noexcept override { return size_; }
    [[nodiscard]] Result<std::vector<std::uint8_t>> read(std::uint64_t offset, std::size_t length) const override {
        bytesRead_ += length;
        ++reads_;
        if (offset <= metadata_.size() && length <= metadata_.size() - offset) {
            auto begin = metadata_.begin() + std::ptrdiff_t(offset);
            return Result<std::vector<std::uint8_t>>::success({begin, begin + std::ptrdiff_t(length)});
        }
        if (offset >= payloadStart_ && length == chunk_.size() && (offset - payloadStart_) % chunk_.size() == 0 &&
            offset + length <= size_)
            return Result<std::vector<std::uint8_t>>::success(chunk_);
        return Result<std::vector<std::uint8_t>>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                            "sparse terrain read is out of range", {},
                                                                            {}, "procgen.terrain-performance"));
    }
    [[nodiscard]] std::uint64_t bytesRead() const noexcept { return bytesRead_; }
    [[nodiscard]] std::uint64_t reads() const noexcept { return reads_; }

private:
    std::uint64_t             payloadStart_ = 0, size_ = 0;
    std::vector<std::uint8_t> metadata_, chunk_;
    mutable std::uint64_t     bytesRead_ = 0, reads_ = 0;
};
}  // namespace

TEST_CASE("procgen.terrain.performance.real1kRoundTripAnd1k4k8kRandomAccess") {
    constexpr int     dimension = 1024, chunkSize = 64;
    const std::size_t count = std::size_t(dimension) * dimension;
    Heightmap         heightmap(dimension, dimension);
    HydrologyMap      hydrology;
    hydrology.width = hydrology.height = dimension;
    hydrology.flowAccumulation.assign(count, 1);
    hydrology.rivers.assign(count, 0);
    ClimateMap climate;
    climate.width = climate.height = dimension;
    climate.temperature.assign(count, .5F);
    climate.moisture.assign(count, .5F);
    climate.biomes.assign(count, Biome::Grassland);
    for (int y = 0; y < dimension; ++y)
        for (int x = 0; x < dimension; ++x)
            heightmap.setHeight(x, y, .5F + .25F * std::sin(float(x) * .011F) * std::cos(float(y) * .013F));
    std::vector<std::uint8_t> archive;
    std::string               error;
    const auto                bakeBegin = std::chrono::steady_clock::now();
    REQUIRE(TerrainAsset::bake(heightmap, hydrology, climate, chunkSize, archive, &error));
    const double     bakeMs = milliseconds(std::chrono::steady_clock::now() - bakeBegin);
    TerrainAsset     roundTrip;
    TerrainChunkData decoded;
    REQUIRE(roundTrip.open(archive.data(), archive.size(), &error));
    REQUIRE(roundTrip.loadChunk(7, 9, decoded, &error));
    CHECK(std::abs(decoded.heights.height(3, 5) - heightmap.height(451, 581)) < .001F);
    CHECK(bakeMs < 20000);
    std::printf("terrain.performance dense_dimension=1024 archive_bytes=%zu bake_ms=%.3f\n", archive.size(), bakeMs);

    for (int worldDimension : std::array{1024, 4096, 8192}) {
        auto                  source = std::make_shared<SparseTerrainArchiveSource>(worldDimension);
        TerrainStreamingCache streaming;
        const auto            openBegin = std::chrono::steady_clock::now();
        REQUIRE(streaming.openSource(source).ok());
        const double        openMs = milliseconds(std::chrono::steady_clock::now() - openBegin);
        std::vector<double> moves;
        for (int index = 0; index < 20; ++index) {
            const int  coordinate = (index * 389) % (worldDimension - 1);
            const auto begin      = std::chrono::steady_clock::now();
            auto       stats      = streaming.streamAround(coordinate, worldDimension - 1 - coordinate, 2, 4, &error);
            moves.push_back(milliseconds(std::chrono::steady_clock::now() - begin));
            CHECK_EQ(stats.failed, 0);
            CHECK(stats.resident <= 13);
        }
        std::sort(moves.begin(), moves.end());
        const auto columns        = std::uint64_t((worldDimension + chunkSize - 1) / chunkSize);
        const auto directoryBytes = kHeaderSize + columns * columns * kEntrySize;
        CHECK(source->bytesRead() <= directoryBytes * 2 + 80 * chunkSize * chunkSize * 12);
        CHECK(openMs < 1000);
        CHECK(moves[18] < 100);
        std::printf(
            "terrain.performance sparse_dimension=%d virtual_bytes=%llu reads=%llu "
            "bytes_read=%llu open_ms=%.3f stream_p95_ms=%.3f\n",
            worldDimension, static_cast<unsigned long long>(source->size()),
            static_cast<unsigned long long>(source->reads()), static_cast<unsigned long long>(source->bytesRead()),
            openMs, moves[18]);
    }
}
}  // namespace test_terrain_curve_texture

// Contract section: terrain detail
#include "PrivateTestScope.h"

#include "procgen/heightmap/TerrainDetailLayer.h"
#include "procgen/heightmap/TerrainDetailPlacement.h"
// terrain_detail.cpp
namespace test_terrain_detail {
using namespace eve::procgen;
namespace {
[[maybe_unused]] int countAt(const TerrainDetailLayer& layer, int x, int z = 0) {
    auto count = layer.sample(x, z);
    REQUIRE(count.ok());
    return count.value();
}
}  // namespace


TEST_CASE("procgen.detail.scriptOwnedLayer") {
    Heightmap fitness(1, 1);
    fitness.data() = {1};
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("fitness", [&]() { return &fitness; });
    vm.run(vm.compileSource(R"(
        local layer=eve.TerrainDetailLayer();assert(layer.reset(1,1,0).ok);
        local settings=eve.TerrainDetailSettings();settings.density=9.0;
        assert(layer.apply(fitness(),settings,0,17).ok);
        assert(layer.sample(0,0).value==9);
        assert(layer.getWidth()==1 && layer.getHeight()==1);
        assert(layer.apply(fitness(),settings,2,17).ok);
        assert(layer.sample(0,0).value==0);
        assert(!layer.apply(fitness(),settings,99,17).ok);
    )"));
}

TEST_CASE("procgen.detail.resourceLayersClearIndependently") {
    TerrainDetailLayer layer;
    Heightmap          fitness(2, 1);
    fitness.data() = {1, 1};
    REQUIRE(layer.reset(2, 1).ok());
    TerrainDetailSettings first;
    first.minimumFitness = first.fadeStart = 0;
    first.density                          = 3;
    first.namespaceId                      = 101;
    REQUIRE(layer.apply(fitness, first, 7).ok());
    auto second        = first;
    second.density     = 5;
    second.namespaceId = 202;
    REQUIRE(layer.apply(fitness, second, 9).ok());
    CHECK(layer.sample(0, 0).value() == 8);
    CHECK(layer.sampleResource(101, 0, 0).value() == 3);
    CHECK(layer.sampleResource(202, 0, 0).value() == 5);
    Heightmap                      heights(2, 2);
    TerrainDetailPlacementSettings placement;
    placement.asset              = "flowers";
    placement.maxPoints          = 20;
    placement.densityNamespaceId = 202;
    placement.namespaceId        = 303;
    PointSet selected;
    REQUIRE(exportTerrainDetailPoints(selected, layer, heights, placement).ok());
    CHECK(selected.getCount() == 10);
    for (int i = 0; i < selected.getCount(); ++i) CHECK(selected.getStringAttribute(i, "spawnNamespace", "") == "303");
    REQUIRE(layer.clearResource(101).ok());
    CHECK(layer.sample(0, 0).value() == 5);
    CHECK(layer.sampleResource(101, 0, 0).value() == 0);

    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("layer", [&]() { return &layer; });
    vm.run(vm.compileSource(R"(
        assert(layer().sampleResource(202,0,0).value==5);
        assert(layer().clearResource(202).value==2);
        assert(layer().sample(0,0).value==0);
    )"));
}


TEST_CASE("procgen.detail.scriptPointExport") {
    TerrainDetailLayer layer;
    REQUIRE(layer.reset(1, 1, 2).ok());
    Heightmap heights(1, 1);
    heights.data() = {3};
    PointSet output;
    ssq::VM  vm(1024);
    auto     table = vm.addTable("eve");
    table.addClass("PointSet", ssq::Class::Ctor<PointSet()>());
    exposeHeightmap(table);
    vm.addFunc("layer", [&]() { return &layer; });
    vm.addFunc("heights", [&]() { return &heights; });
    vm.addFunc("output", [&]() { return &output; });
    vm.run(vm.compileSource(R"(
        local settings=eve.TerrainDetailPlacementSettings();settings.asset="grass/test";
        settings.namespaceId=17;settings.seed=4;
        settings.minimumWidth=0.5;settings.maximumWidth=0.5;settings.minimumHeight=2.0;settings.maximumHeight=2.0;
        settings.healthyR=0.8;settings.dryR=0.2;settings.noiseSpread=0.0;settings.noiseSeed=-4;
        assert(eve.exportTerrainDetailPoints(output(),layer(),heights(),settings).ok);
    )"));
    CHECK(output.getCount() == 2);
    CHECK(output.getY(0) == 3);
    CHECK(output.getScaleX(0) == 0.5F);
    CHECK(output.getScaleY(0) == 2);
    CHECK(std::abs(output.getColorR(0) - 0.5F) < 0.000001F);
}
}  // namespace test_terrain_detail

// Contract section: terrain erosion
#include "PrivateTestScope.h"

#include "procgen/heightmap/TerrainErosion.h"
// terrain_erosion.cpp
namespace test_terrain_erosion {
using namespace eve::procgen;
namespace {
[[maybe_unused]] Heightmap filled(int width, int height, float value) {
    Heightmap result(width, height);
    std::fill(result.data().begin(), result.data().end(), value);
    return result;
}
}  // namespace


TEST_CASE("procgen.erosion.thermalScriptOutputsAndRejectedAlias") {
    auto height = filled(3, 3, 1), sediment = filled(3, 3, 0);
    height.setHeight(1, 1, 2);
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("height", [&]() { return &height; });
    vm.addFunc("sediment", [&]() { return &sediment; });
    vm.run(vm.compileSource(R"(
        local s=eve.TerrainThermalSettings();
        s.spacingX=1.0; s.spacingZ=1.0; s.heightScale=1.0;
        s.reposeSlope=0.0; s.dt=0.1; s.iterations=1;
        local r=height().applyThermal(sediment(),s);
        assert(r.ok && r.value == 9);
        assert(height().height(1,1)<2.0 && sediment().height(1,1)>0.0);
        assert(!height().applyThermal(height(),s).ok);
        s.spacingX=0.0;
        assert(!height().applyThermal(sediment(),s).ok);
    )"));
    CHECK(std::abs(height.height(1, 1) - 1.957325F) < 1e-6F);
}
}  // namespace test_terrain_erosion

// Contract section: terrain generation session
#include "PrivateTestScope.h"

#include "procgen/heightmap/TerrainEffect.h"
#include "procgen/heightmap/TerrainGenerationSession.h"
#include "procgen/heightmap/TerrainWaterField.h"
// terrain_generation_session.cpp
namespace test_terrain_generation_session {
using namespace eve::procgen;


TEST_CASE("procgen.session.scriptEffectCommands") {
    Heightmap baseline(1, 1), mask(1, 1), curve(1, 1), out(1, 1);
    baseline.data() = {0.5F};
    mask.data()     = {0.5F};
    curve.data()    = {0.5F};
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("baseline", [&]() { return &baseline; });
    vm.addFunc("mask", [&]() { return &mask; });
    vm.addFunc("curve", [&]() { return &curve; });
    vm.addFunc("out", [&]() { return &out; });
    vm.run(vm.compileSource(R"(
        local s=eve.TerrainGenerationSession();assert(s.reset(baseline()).ok);
        assert(s.contrast(mask(),2.0,1.0).ok);
        assert(s.smooth(mask(),eve.TerrainSmoothSettings()).ok);
        local ridges=eve.TerrainRidgeSettings();ridges.passes=1;
        assert(s.ridges(mask(),ridges).ok);
        assert(s.terrace(mask(),eve.TerrainTerraceSettings()).ok);
        assert(s.power(mask(),3.0).ok);
        assert(s.heightCurve(mask(),curve(),0.0,1.0).ok);
        assert(s.heightMix(mask(),mask(),eve.TerrainHeightMixSettings()).ok);
        assert(s.getOperationCount()==7 && s.getAppliedCount()==7);
        assert(s.copyTerrain(out()).ok);local saved=out().height(0,0);
        assert(s.undo().ok && s.redo().ok && s.replay().ok);
        assert(s.copyTerrain(out()).ok && out().height(0,0)==saved);
    )"));
}


TEST_CASE("procgen.session.scriptLifecycleHistoryAndLocks") {
    Heightmap baseline(1, 1), stamp(1, 1), one(1, 1), out(1, 1);
    stamp.data() = {2};
    one.data()   = {1};
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("baseline", [&]() { return &baseline; });
    vm.addFunc("stamp", [&]() { return &stamp; });
    vm.addFunc("one", [&]() { return &one; });
    vm.addFunc("out", [&]() { return &out; });
    vm.run(vm.compileSource(R"(
        local session=eve.TerrainGenerationSession();local s=eve.TerrainStampSettings();
        assert(session.reset(baseline()).ok);
        assert(session.stamp(stamp(),s,4,one(),one()).ok);
        assert(session.getOperationCount()==1 && session.getAppliedCount()==1);
        assert(session.operationEnabled(0).value);
        assert(session.undo().ok && session.redo().ok && session.replay().ok);
        assert(session.setOperationEnabled(0,false).ok);
        assert(session.copyTerrain(out()).ok && out().height(0,0)==0.0);
        assert(session.setOperationEnabled(0,true).ok);
        assert(session.setAccess(1).ok && session.getAccess()==1);
        assert(!session.undo().ok);
        assert(session.copyTerrain(out()).ok && out().height(0,0)==2.0);
        assert(session.setAccess(0).ok);
    )"));
    CHECK(out.data()[0] == 2);
}


TEST_CASE("procgen.session.scriptErosionCommands") {
    Heightmap baseline(1, 1), out(1, 1);
    baseline.data() = {1};
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("baseline", [&]() { return &baseline; });
    vm.addFunc("out", [&]() { return &out; });
    vm.run(vm.compileSource(R"(
        local s=eve.TerrainGenerationSession(); assert(s.reset(baseline()).ok);
        assert(s.resetSimulation(1.0).ok);
        local thermal=eve.TerrainThermalSettings(); thermal.iterations=0;
        assert(s.thermal(thermal).ok);
        local water=eve.TerrainWaterSettings(); water.precipitation=1.0;water.evaporation=0.0;water.dt=0.5;
        assert(s.hydraulic(water,eve.TerrainSedimentSettings(),thermal,2).ok);
        assert(s.exportWater(out(),0).ok && out().height(0,0)==2.0);
        assert(s.copySediment(out()).ok);
        assert(s.undo().ok);
        assert(s.exportWater(out(),0).ok && out().height(0,0)==1.0);
        assert(s.redo().ok && s.replay().ok);
        assert(s.exportWater(out(),0).ok && out().height(0,0)==2.0);
        assert(!s.exportWater(out(),99).ok);
    )"));
}


TEST_CASE("procgen.session.snapshotRealVmRoundTrip") {
    Heightmap baseline(1, 1), out(1, 1);
    baseline.data()[0] = 0.25F;
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("baseline", [&]() { return &baseline; });
    vm.addFunc("out", [&]() { return &out; });
    vm.run(vm.compileSource(R"(
        local source=eve.TerrainGenerationSession();assert(source.reset(baseline()).ok);
        assert(source.resetSimulation(0.5).ok);local json=source.snapshotJson();assert(json.ok);
        local restored=eve.TerrainGenerationSession();assert(restored.restoreJson(json.value).ok);
        assert(restored.getOperationCount()==1 && restored.copyTerrain(out()).ok);
        assert(restored.snapshotJson().value==json.value);
    )"));
    CHECK(out.data()[0] == 0.25F);
}
}  // namespace test_terrain_generation_session

// Contract section: terrain legacy hydraulic
#include "PrivateTestScope.h"

// terrain_legacy_hydraulic.cpp
namespace test_terrain_legacy_hydraulic {
// terrain_legacy_hydraulic.cpp
using namespace eve::procgen;


TEST_CASE("procgen.legacyHydraulic.atomicValidationAndScript") {
    Heightmap heights(2, 2), sediment(2, 2), hardness(2, 2), rain(2, 2);
    heights.data()                          = {0, 1, 1, 0};
    const auto                     original = heights.data();
    TerrainLegacyHydraulicSettings invalid;
    invalid.rainFrequency = 0;
    CHECK(!applyTerrainLegacyHydraulic(heights, sediment, hardness, rain, invalid).ok());
    CHECK(heights.data() == original);
    CHECK(!applyTerrainLegacyHydraulic(heights, heights, hardness, rain, {}).ok());

    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("heights", [&]() { return &heights; });
    vm.addFunc("sediment", [&]() { return &sediment; });
    vm.addFunc("hardness", [&]() { return &hardness; });
    vm.addFunc("rain", [&]() { return &rain; });
    vm.run(vm.compileSource(R"(
        local s=eve.TerrainLegacyHydraulicSettings(); s.iterations=0;
        assert(heights().applyLegacyHydraulic(sediment(),hardness(),rain(),s).ok);
    )"));
}

// terrain_legacy_thermal.cpp
using namespace eve::procgen;


TEST_CASE("procgen.legacyThermal.steepestOrderHardnessAndScript") {
    Heightmap map(3, 3), hardness(1, 1);
    map.setHeight(1, 1, 1);
    TerrainLegacySteepestErosionSettings settings;
    REQUIRE(applyTerrainLegacySteepestErosion(map, hardness, settings).ok());
    CHECK(map.height(1, 1) == 0.5F);
    CHECK(map.height(1, 2) == 0.5F);

    Heightmap protectedMap(3, 3);
    protectedMap.setHeight(1, 1, 1);
    hardness.setHeight(0, 0, 1);
    REQUIRE(applyTerrainLegacySteepestErosion(protectedMap, hardness, settings).ok());
    CHECK(protectedMap.height(1, 1) == 1);

    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("map", [&]() { return &map; });
    vm.addFunc("hardness", [&]() { return &hardness; });
    vm.run(vm.compileSource(R"(
        local a=eve.TerrainLegacyDistributedErosionSettings(); a.iterations=0;
        assert(map().applyLegacyDistributedErosion(a).ok);
        local b=eve.TerrainLegacySteepestErosionSettings(); b.iterations=0;
        assert(map().applyLegacySteepestErosion(hardness(),b).ok);
    )"));
}
}  // namespace test_terrain_legacy_hydraulic

// Contract section: terrain multitile
#include "PrivateTestScope.h"

#include <vector>
#include "procgen/heightmap/TerrainMultiTile.h"
#include "procgen/heightmap/TerrainTreePlacement.h"
// terrain_multitile.cpp
namespace test_terrain_multitile {
using namespace eve::procgen;
namespace {
[[maybe_unused]] Heightmap filled(int width, int height, float value) {
    Heightmap result(width, height);
    std::fill(result.data().begin(), result.data().end(), value);
    return result;
}
[[maybe_unused]] TerrainStampSettings setStamp(double x, double z, double width, double depth) {
    TerrainStampSettings settings;
    settings.centerX   = x;
    settings.centerZ   = z;
    settings.width     = width;
    settings.depth     = depth;
    settings.operation = TerrainStampOperation::Set;
    return settings;
}
}  // namespace


TEST_CASE("procgen.multitile.detailWorkspaceOwnsHistoryAndScriptBinding") {
    TerrainDetailLayer source, output;
    REQUIRE(source.reset(3, 3, 1).ok());
    REQUIRE(output.reset(3, 3, 0).ok());
    Heightmap             fitness = filled(4, 3, 1);
    TerrainDetailSettings detail;
    detail.minimumFitness = detail.fadeStart = 0;
    detail.density                           = 5;
    TerrainMultiDetailWorkspace workspace;
    REQUIRE(workspace.addTile("west", source, 0, 0, 2, 2).ok());
    REQUIRE(workspace.addTile("east", source, 2, 0, 2, 2).ok());
    REQUIRE(workspace.apply(fitness, detail, setStamp(2, 1, 2, 2), 9).ok());
    CHECK(workspace.getTileCount() == 2);
    CHECK(workspace.getLastAffectedTiles() == 2);
    CHECK(workspace.getOperationCount() == 1);
    REQUIRE(workspace.copyTile("east", output).ok());
    CHECK(output.sample(0, 1).value() == 5);
    REQUIRE(workspace.undo().ok());
    REQUIRE(workspace.copyTile("east", output).ok());
    CHECK(output.sample(0, 1).value() == 1);
    REQUIRE(workspace.redo().ok());
    CHECK(workspace.getAppliedCount() == 1);

    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("source", [&]() { return &source; });
    vm.addFunc("fitness", [&]() { return &fitness; });
    vm.addFunc("output", [&]() { return &output; });
    vm.run(vm.compileSource(R"(
        local w=eve.TerrainMultiDetailWorkspace();
        assert(w.addTile("west",source(),0.0,0.0,2.0,2.0,false).ok);
        assert(w.addTile("east",source(),2.0,0.0,2.0,2.0,false).ok);
        local d=eve.TerrainDetailSettings();d.minimumFitness=0.0;d.fadeStart=0.0;d.density=6.0;
        local s=eve.TerrainStampSettings();s.setCenter(2.0,1.0);s.setSize(2.0,2.0);
        assert(w.apply(fitness(),d,s,0,11,false).ok);
        assert(w.getTileCount()==2 && w.getLastAffectedTiles()==2 && w.getLastChangedSamples()==18);
        assert(w.copyTile("east",output()).ok && output().sample(0,1).value==6);
        assert(w.undo().ok && w.copyTile("east",output()).ok && output().sample(0,1).value==1);
        assert(w.redo().ok && w.getOperationCount()==1 && w.getAppliedCount()==1);
    )"));
}


TEST_CASE("procgen.multitile.treeWorkspaceOwnsHistoryAndScriptBinding") {
    PointSet                     empty, output;
    Heightmap                    heights = filled(2, 2, 0.25F), fitness = filled(4, 2, 1);
    TerrainTreePlacementSettings tree;
    tree.spacing = tree.spawnDensity = 1;
    tree.jitterPercent = tree.failureRate = tree.minimumFitness = 0;
    tree.scaleMode                                              = TerrainTreeScaleMode::Fixed;
    tree.minimumWidth = tree.maximumWidth = tree.minimumHeight = tree.maximumHeight = 1;
    tree.asset                                                                      = "oak";
    tree.namespaceId                                                                = 77;
    tree.maxPoints                                                                  = 100;
    tree.seed                                                                       = 5;
    auto                      bounds                                                = setStamp(2, 1, 4, 2);
    TerrainMultiTreeWorkspace workspace;
    REQUIRE(workspace.addTile("west", empty, heights, 0, 0, 2, 2, 2, 2).ok());
    REQUIRE(workspace.addTile("east", empty, heights, 2, 0, 2, 2, 2, 2).ok());
    REQUIRE(workspace.apply(fitness, tree, bounds, TerrainTreeOperationMode::Add).ok());
    REQUIRE(workspace.copyTile("east", output).ok());
    CHECK(output.getCount() == 4);
    REQUIRE(workspace.undo().ok());
    REQUIRE(workspace.copyTile("east", output).ok());
    CHECK(output.empty());
    REQUIRE(workspace.redo().ok());
    CHECK(workspace.getTileCount() == 2);
    CHECK(workspace.getLastChangedSamples() == 8);
    CHECK(workspace.getOperationCount() == 1);
    CHECK(workspace.getAppliedCount() == 1);

    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("emptyTrees", [&]() { return &empty; });
    vm.addFunc("heights", [&]() { return &heights; });
    vm.addFunc("fitness", [&]() { return &fitness; });
    vm.addFunc("output", [&]() { return &output; });
    vm.addFunc("pointCount", [](const PointSet* points) { return points->getCount(); });
    vm.run(vm.compileSource(R"(
        local w=eve.TerrainMultiTreeWorkspace();
        assert(w.addTile("west",emptyTrees(),heights(),0.0,0.0,2.0,2.0,2,2,false).ok);
        assert(w.addTile("east",emptyTrees(),heights(),2.0,0.0,2.0,2.0,2,2,false).ok);
        local t=eve.TerrainTreePlacementSettings();t.spacing=1.0;t.spawnDensity=1.0;
        t.jitterPercent=0.0;t.failureRate=0.0;t.minimumFitness=0.0;t.asset="oak";
        t.namespaceId=91;t.maxPoints=100;t.seed=5;t.setScaleMode(0);
        local s=eve.TerrainStampSettings();s.setCenter(2.0,1.0);s.setSize(4.0,2.0);
        assert(w.apply(fitness(),t,s,0,false).ok);
        assert(w.copyTile("east",output()).ok && pointCount(output())==4);
        assert(w.getTileCount()==2 && w.getLastAffectedTiles()==2 && w.getLastChangedSamples()==8);
        assert(w.undo().ok && w.copyTile("east",output()).ok && pointCount(output())==0);
        assert(w.redo().ok && w.getOperationCount()==1 && w.getAppliedCount()==1);
    )"));
}


TEST_CASE("procgen.multitile.scriptOwnedTransaction") {
    Heightmap west = filled(3, 3, 0), east = filled(3, 3, 0), out = filled(3, 3, 0);
    Heightmap stamp = filled(1, 1, 3), one = filled(1, 1, 1);
    ssq::VM   vm(1024);
    auto      table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("west", [&]() { return &west; });
    vm.addFunc("east", [&]() { return &east; });
    vm.addFunc("stampRaster", [&]() { return &stamp; });
    vm.addFunc("one", [&]() { return &one; });
    vm.addFunc("out", [&]() { return &out; });
    vm.run(vm.compileSource(R"(
        local w=eve.TerrainMultiTileWorkspace();
        assert(w.addTile("west",west(),0.0,0.0,2.0,2.0,false).ok);
        assert(w.addTile("east",east(),2.0,0.0,2.0,2.0,false).ok);
        local s=eve.TerrainStampSettings();s.setCenter(2.0,1.0);s.setSize(2.0,2.0);
        assert(w.stamp(stampRaster(),s,3,one(),one(),false).ok);
        assert(w.getTileCount()==2 && w.getLastAffectedTiles()==2 && w.getLastMappingCount()==2);
        assert(w.copyTile("east",out()).ok && out().height(0,1)==3.0);
        assert(w.getOperationCount()==1 && w.getAppliedCount()==1);
        assert(w.undo().ok && w.getAppliedCount()==0);
        assert(w.copyTile("east",out()).ok && out().height(0,1)==0.0);
        assert(w.redo().ok && w.getAppliedCount()==1);
        assert(w.copyTile("east",out()).ok && out().height(0,1)==3.0);
        assert(!w.copyTile("missing",out()).ok);
    )"));
    CHECK(west.height(1, 1) == 0);
    CHECK(east.height(0, 1) == 0);
}


TEST_CASE("procgen.terrainStitcher.workspaceUndoAndScriptBinding") {
    Heightmap                 low = filled(3, 3, 0), high = filled(3, 3, 1), output = filled(3, 3, 0);
    TerrainMultiTileWorkspace workspace;
    REQUIRE(workspace.addTile("west", low, 0, 0, 2, 2).ok());
    REQUIRE(workspace.addTile("east", high, 2, 0, 2, 2).ok());
    TerrainHeightStitchSettings settings;
    REQUIRE(workspace.stitch("west", "east", settings).ok());
    REQUIRE(workspace.copyTile("west", output).ok());
    CHECK(output.height(2, 1) == 0.5F);
    REQUIRE(workspace.undo().ok());
    REQUIRE(workspace.copyTile("west", output).ok());
    CHECK(output.height(2, 1) == 0);
    REQUIRE(workspace.redo().ok());

    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("low", [&]() { return &low; });
    vm.addFunc("high", [&]() { return &high; });
    vm.addFunc("output", [&]() { return &output; });
    vm.run(vm.compileSource(R"(
        local w=eve.TerrainMultiTileWorkspace();
        assert(w.addTile("south",low(),0.0,0.0,2.0,2.0,false).ok);
        assert(w.addTile("north",high(),0.0,2.0,2.0,2.0,false).ok);
        local s=eve.TerrainHeightStitchSettings();s.extraSeamSize=1;s.maxDifference=1.0;
        assert(w.stitch("south","north",s).ok);
        assert(w.copyTile("south",output()).ok && output().height(1,2)==0.5);
        assert(w.undo().ok && w.copyTile("south",output()).ok && output().height(1,2)==0.0);
    )"));
}
}  // namespace test_terrain_multitile

// Contract section: terrain probe
#include "PrivateTestScope.h"

#include "common/Capability.h"
#include "common/ProcgenProbeSink.h"
#include "common/Value.h"
#include "procgen/heightmap/TerrainBiomePreset.h"
#include "procgen/heightmap/TerrainImageAdapter.h"
#include "procgen/heightmap/TerrainProbePlacement.h"
#include "procgen/heightmap/TerrainSpawnPlan.h"
#include "procgen/heightmap/TerrainSplatmap.h"
#include "procgen/heightmap/TerrainTerraceRemover.h"
#include "procgen/heightmap/TerrainWorldWorkspace.h"
// terrain_probe.cpp
namespace test_terrain_probe {
// terrain_probe.cpp
using namespace eve::procgen;


TEST_CASE("procgen.probe.realVmBinding") {
    Heightmap fitness(2, 2), heights(2, 2);
    std::fill(fitness.data().begin(), fitness.data().end(), 1.0F);
    std::fill(heights.data().begin(), heights.data().end(), 1.0F);
    PointSet output;
    ssq::VM  vm(1024);
    auto     table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("output", [&]() { return &output; });
    vm.addFunc("fitness", [&]() { return &fitness; });
    vm.addFunc("heights", [&]() { return &heights; });
    vm.run(vm.compileSource(R"(
        local s=eve.TerrainProbePlacementSettings();
        s.name="vm-probe";s.width=2.0;s.depth=2.0;s.spacing=1.0;s.jitterPercent=0.0;
        s.minimumFitness=0.0;s.namespaceId=902;
        local r=eve.generateTerrainProbes(output(),fitness(),heights(),s,0);
        assert(r.ok && r.value==9);
    )"));
    CHECK(output.getCount() == 9);
}

TEST_CASE("procgen.probe.graphicsProviderPublicationAndMissingProvider") {
    struct ProbeSink final : eve::IProcgenProbeSink {
        int              count = 0, reflections = 0, lights = 0;
        eve::Result<int> replaceProbeBatch(const std::string&,
                                           const std::vector<eve::ProcgenProbeDesc>& probes) override {
            count = reflections = lights = 0;
            for (const auto& probe : probes) {
                ++count;
                probe.type == 0 ? ++reflections : ++lights;
            }
            return eve::Result<int>::success(count);
        }
        eve::Result<int> removeProbeBatch(const std::string&) override {
            const int removed = count;
            count = reflections = lights = 0;
            return eve::Result<int>::success(removed);
        }
        eve::Result<int> tickProbeBatches(int, int, int) override { return eve::Result<int>::success(0); }
        int              probeCount(const std::string&) const override { return count; }
        int              reflectionProbeCount(const std::string&) const override { return reflections; }
        int              lightProbeCount(const std::string&) const override { return lights; }
    } sink;
    auto* previous = eve::cap::query<eve::IProcgenProbeSink>();
    struct RestoreProvider {
        eve::IProcgenProbeSink* previous;
        eve::IProcgenProbeSink* temporary;
        ~RestoreProvider() {
            eve::cap::revoke<eve::IProcgenProbeSink>(temporary);
            if (previous) eve::cap::provide<eve::IProcgenProbeSink>(previous);
        }
    } restore{previous, &sink};
    eve::cap::provide<eve::IProcgenProbeSink>(&sink);
    Heightmap fitness(2, 2), heights(2, 2);
    std::fill(fitness.data().begin(), fitness.data().end(), 1.0F);
    std::fill(heights.data().begin(), heights.data().end(), 1.0F);
    PointSet output;
    ssq::VM  vm(1024);
    auto     table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("output", [&]() { return &output; });
    vm.addFunc("fitness", [&]() { return &fitness; });
    vm.addFunc("heights", [&]() { return &heights; });
    vm.run(vm.compileSource(R"(
        local s=eve.TerrainProbePlacementSettings();
        s.name="provider-probe";s.width=1.0;s.depth=1.0;s.spacing=1.0;s.jitterPercent=0.0;
        s.minimumFitness=0.0;s.namespaceId=904;
        assert(eve.generateTerrainProbes(output(),fitness(),heights(),s,0).ok);
        local published=eve.publishTerrainProbes("probe-test",output());
        assert(published.ok && eve.getTerrainProbeBatchCount("probe-test")==published.value);
        assert(eve.getTerrainReflectionProbeCount("probe-test")==published.value);
        assert(eve.removeTerrainProbeBatch("probe-test").ok);
    )"));
    eve::cap::revoke<eve::IProcgenProbeSink>(&sink);
    vm.run(vm.compileSource(R"(
        local missing=eve.publishTerrainProbes("probe-test",output());
        assert(!missing.ok && eve.getTerrainProbeBatchCount("probe-test")==0);
    )"));
}


// terrain_spawn_plan.cpp


using namespace eve::procgen;

namespace {
[[maybe_unused]] TerrainWorldCreationSettings worldSettings() {
    TerrainWorldCreationSettings settings;
    settings.tilesX                   = 2;
    settings.tilesZ                   = 1;
    settings.tileSize                 = 2;
    settings.heightmapResolution      = 3;
    settings.controlTextureResolution = settings.detailResolution = 2;
    settings.treeResolution = settings.objectResolution = 2;
    settings.splatLayers                                = 2;
    return settings;
}
[[maybe_unused]] TerrainStampSettings bounds() {
    TerrainStampSettings value;
    value.centerX = value.centerZ = 0;
    value.width                   = 4;
    value.depth                   = 2;
    return value;
}
[[maybe_unused]] Heightmap fitness() {
    Heightmap value(4, 2);
    std::fill(value.data().begin(), value.data().end(), 1.0F);
    return value;
}
[[maybe_unused]] TerrainTreePlacementSettings trees() {
    TerrainTreePlacementSettings value;
    value.spacing = value.spawnDensity = 1;
    value.jitterPercent = value.failureRate = value.minimumFitness = 0;
    value.minimumWidth = value.maximumWidth = value.minimumHeight = value.maximumHeight = 1;
    value.asset                                                                         = "oak";
    value.namespaceId                                                                   = 1201;
    value.maxPoints                                                                     = 100;
    return value;
}
[[maybe_unused]] TerrainObjectPlacementSettings objects() {
    TerrainObjectPlacementSettings value;
    value.spacing = value.spawnDensity = 1;
    value.jitterPercent = value.failureRate = value.minimumFitness = value.minimumInstanceFitness = 0;
    value.boundsRadius                                                                            = 0.2F;
    value.prototype                                                                               = "rocks";
    value.namespaceId                                                                             = 1301;
    value.maxPoints                                                                               = 100;
    TerrainObjectInstanceSettings child;
    child.asset = "rock";
    REQUIRE(value.addInstance(child).ok());
    return value;
}
}  // namespace


TEST_CASE("procgen.spawnPlan.executesOwnedPlanThroughRealVm") {
    TerrainWorldWorkspace world;
    Heightmap             map      = fitness();
    auto                  settings = worldSettings();
    REQUIRE(world.create(settings).ok());
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("world", [&]() { return &world; });
    vm.addFunc("fitness", [&]() { return &map; });
    vm.run(vm.compileSource(R"(
        local bounds=eve.TerrainStampSettings();bounds.setCenter(0.0,0.0);bounds.setSize(4.0,2.0);
        local detail=eve.TerrainDetailSettings();detail.minimumFitness=0.0;detail.fadeStart=0.0;
        detail.density=2.0;detail.namespaceId=3101;
        local plan=eve.TerrainSpawnPlan();
        assert(plan.addModifierStamp("vm-modifier",fitness(),bounds,3,fitness(),fitness()).value==1);
        assert(plan.addSplat("vm-texture",fitness(),1,bounds).value==2);
        assert(plan.addDetail("vm-detail",fitness(),detail,bounds,0,17).value==3);
        local preset=eve.TerrainBiomePreset();
        assert(preset.addSpawner("forest",plan,true,false,true).value==1);
        assert(preset.getSpawnerCount()==1 && preset.getAutoAssignResources("forest").value);
        local presetJson=preset.snapshotJson().value;
        local restoredPreset=eve.TerrainBiomePreset();assert(restoredPreset.restoreJson(presetJson).ok);
        assert(world().beginSpawnBiome(restoredPreset).value==1);
        assert(world().stepSpawn(1).value==1 && world().getSpawnCompletedRules()==1);
        assert(world().cancelSpawn().value==3);
        assert(world().beginSpawnBiome(restoredPreset).value==1);
        assert(world().stepSpawn(10).value==2 && world().getSpawnStatus()==2);
        assert(!world().spawnStamper(restoredPreset).ok);
        assert(world().getOperationCount()==1);
    )"));
}


// terrain_splatmap.cpp
using namespace eve::procgen;


TEST_CASE("procgen.terrainTextureAligner.bindsRealVm") {
    TerrainSplatmap a, b;
    REQUIRE(a.initialize(2, 2, 2, 0).ok());
    REQUIRE(b.initialize(2, 2, 2, 1).ok());
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("a", [&]() { return &a; });
    vm.addFunc("b", [&]() { return &b; });
    vm.run(vm.compileSource(R"(
        local s=eve.TerrainTextureAlignSettings();
        s.terrainAOriginX=0.0;s.terrainAOriginZ=0.0;s.terrainAWidth=2.0;s.terrainADepth=2.0;
        s.terrainBOriginX=2.0;s.terrainBOriginZ=0.0;s.terrainBWidth=2.0;s.terrainBDepth=2.0;
        s.blendStrength=1.0;s.blendWidth=1;s.adjacencyTolerance=0.1;
        local r=eve.alignTerrainSplatTextures(a(),b(),s);
        assert(r.ok);
    )"));
    // Pcg's X-adjacent Top/Bottom edges use the splatmap's second coordinate.
    CHECK(a.sample(0, 1, 1).value() < 1.0F);
    CHECK(b.sample(0, 0, 0).value() > 0.0F);
}


TEST_CASE("procgen.terrainSplatmap.scriptPaintsAndReadsRealVm") {
    Heightmap paint(2, 1), extracted(2, 1);
    paint.setHeight(0, 0, 0.25F);
    paint.setHeight(1, 0, 0.75F);
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("paint", [&]() { return &paint; });
    vm.addFunc("extracted", [&]() { return &extracted; });
    vm.run(vm.compileSource(R"(
        local s=eve.TerrainSplatmap();
        assert(s.initialize(2,1,3,0).value==2);
        assert(s.paint(paint(),1).value==2);
        assert(s.getWidth()==2 && s.getHeight()==1 && s.getLayerCount()==3);
        assert(s.sample(1,0,0).value==0.25);
        assert(s.sample(1,1,0).value==0.75);
        assert(s.copyLayer(1,extracted()).ok && extracted().height(1,0)==0.75);
        assert(s.getLastChangedSamples()==2);
    )"));
}

TEST_CASE("procgen.terrainSplatmap.workspaceOwnsHistoryAndScriptBinding") {
    TerrainSplatmap source, output;
    REQUIRE(source.initialize(2, 2, 2, 0).ok());
    Heightmap paint(4, 2);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 4; ++x) paint.setHeight(x, y, x < 2 ? 0.25F : 0.75F);
    TerrainStampSettings operation;
    operation.centerX = 2;
    operation.centerZ = 1;
    operation.width   = 4;
    operation.depth   = 2;
    TerrainMultiSplatWorkspace workspace;
    REQUIRE(workspace.addTile("west", source, 0, 0, 2, 2).ok());
    REQUIRE(workspace.addTile("east", source, 2, 0, 2, 2).ok());
    REQUIRE(workspace.paint(paint, 1, operation).ok());
    REQUIRE(workspace.copyTile("east", output).ok());
    CHECK(output.sample(1, 0, 0).value() == 0.75F);
    REQUIRE(workspace.undo().ok());
    REQUIRE(workspace.copyTile("east", output).ok());
    CHECK(output.sample(1, 0, 0).value() == 0);
    REQUIRE(workspace.redo().ok());
    CHECK(workspace.getTileCount() == 2);
    CHECK(workspace.getLastAffectedTiles() == 2);
    CHECK(workspace.getLastChangedSamples() == 8);
    CHECK(workspace.getOperationCount() == 1);
    CHECK(workspace.getAppliedCount() == 1);

    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("source", [&]() { return &source; });
    vm.addFunc("paintMap", [&]() { return &paint; });
    vm.addFunc("operation", [&]() { return &operation; });
    vm.addFunc("output", [&]() { return &output; });
    vm.run(vm.compileSource(R"(
        local w=eve.TerrainMultiSplatWorkspace();
        assert(w.addTile("west",source(),0.0,0.0,2.0,2.0,false).ok);
        assert(w.addTile("east",source(),2.0,0.0,2.0,2.0,false).ok);
        assert(w.paint(paintMap(),1,operation(),false).value==8);
        assert(w.copyTile("east",output()).ok && output().sample(1,0,0).value==0.75);
        assert(w.undo().ok && w.copyTile("east",output()).ok && output().sample(1,0,0).value==0.0);
        assert(w.redo().ok && w.getTileCount()==2 && w.getLastAffectedTiles()==2);
        assert(w.getLastChangedSamples()==8 && w.getOperationCount()==1 && w.getAppliedCount()==1);
    )"));
}


TEST_CASE("procgen.gtsHeightBlend.reweightsAllLayersAtomicallyAndBinds") {
    TerrainSplatmap input, output;
    REQUIRE(input.initialize(1, 1, 2, 0).ok());
    Heightmap half(1, 1);
    half.setHeight(0, 0, 0.5F);
    REQUIRE(paintTerrainSplatLayer(input, half, 1).ok());
    Heightmap low(1, 1), high(1, 1);
    low.setHeight(0, 0, 0.2F);
    high.setHeight(0, 0, 0.8F);
    GtsHeightBlendSet layers;
    REQUIRE(layers.addLayer(low).ok());
    REQUIRE(layers.addLayer(high).ok());
    auto result = applyGtsHeightBlend(output, input, layers, 0.1F);
    REQUIRE(result.ok());
    CHECK(result.value() == 1);
    CHECK(output.sample(0, 0, 0).value() < 1e-5F);
    CHECK(output.sample(1, 0, 0).value() > 0.999F);
    const float preserved = output.sample(1, 0, 0).value();
    CHECK(!applyGtsHeightBlend(output, input, layers, -1).ok());
    CHECK(output.sample(1, 0, 0).value() == preserved);
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("input", [&]() { return &input; });
    vm.addFunc("output", [&]() { return &output; });
    vm.addFunc("low", [&]() { return &low; });
    vm.addFunc("high", [&]() { return &high; });
    vm.run(vm.compileSource(R"(
        local h=eve.GtsHeightBlendSet();assert(h.addLayer(low(),1.0,1.0,0.0).ok);assert(h.addLayer(high(),1.0,1.0,0.0).ok);
        assert(h.getLayerCount()==2 && eve.applyGtsHeightBlend(output(),input(),h,0.1).ok);
    )"));
}

TEST_CASE("procgen.gtsPackedLayers.blendsMaterialChannelsAndBinds") {
    TerrainSplatmap splat;
    REQUIRE(splat.initialize(1, 1, 2, 0).ok());
    Heightmap half(1, 1);
    half.setHeight(0, 0, 0.25F);
    REQUIRE(paintTerrainSplatLayer(splat, half, 1).ok());
    eve::image::ImageData a0(1, 1, "RGBA32F"), a1(1, 1, "RGBA32F"), n0(1, 1, "RGBA32F"), n1(1, 1, "RGBA32F");
    a0.setPixel(0, 0, {0.2F, 0.4F, 0.6F, 0.1F});
    a1.setPixel(0, 0, {0.8F, 0.2F, 0.1F, 0.9F});
    n0.setPixel(0, 0, {0.5F, 0.5F, 0.2F, 0.4F});
    n1.setPixel(0, 0, {0.7F, 0.3F, 0.8F, 0.6F});
    GtsPackedLayerSettings s0, s1;
    s0.geoAmount    = 0.2F;
    s0.detailAmount = 0.4F;
    s1.geoAmount    = 0.8F;
    s1.detailAmount = 1;
    GtsPackedLayerSet layers;
    REQUIRE(layers.addLayer(a0, n0, s0).ok());
    REQUIRE(layers.addLayer(a1, n1, s1).ok());
    eve::image::ImageData albedo(1, 1, "RGBA32F"), normal(1, 1, "RGBA32F");
    Heightmap             geo(1, 1), detail(1, 1), heights(1, 1);
    auto                  r = bakeGtsPackedLayers(albedo, normal, geo, detail, heights, splat, layers);
    REQUIRE(r.ok());
    CHECK(r.value() == 4);
    CHECK(std::abs(albedo.getPixel(0, 0).r - 0.35F) < 1e-6F);
    CHECK(std::abs(normal.getPixel(0, 0).b - 0.35F) < 1e-6F);
    CHECK(std::abs(normal.getPixel(0, 0).a - 0.45F) < 1e-6F);
    CHECK(std::abs(geo.height(0, 0) - 0.35F) < 1e-6F);
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    exposeTerrainImageAdapter(table);
    vm.addFunc("a0", [&]() { return &a0; });
    vm.addFunc("n0", [&]() { return &n0; });
    vm.run(vm.compileSource(
        R"(local s=eve.GtsPackedLayerSettings();local p=eve.GtsPackedLayerSet();assert(p.addLayer(a0(),n0(),s).ok&&p.getLayerCount()==1);)"));
}


TEST_CASE("procgen.gtsPackedLayers.bakesTopFourDisplacementWithCameraCutoff") {
    TerrainSplatmap splat;
    REQUIRE(splat.initialize(1, 1, 2, 0).ok());
    Heightmap blend(1, 1);
    blend.setHeight(0, 0, 0.25F);
    REQUIRE(paintTerrainSplatLayer(splat, blend, 1).ok());
    eve::image::ImageData a0(1, 1, "RGBA32F"), a1(1, 1, "RGBA32F"), n(1, 1, "RGBA32F");
    a0.setPixel(0, 0, {0, 0, 0, 0.25F});
    a1.setPixel(0, 0, {0, 0, 0, 1});
    n.setPixel(0, 0, {0.5F, 0.5F, 1, 0});
    GtsPackedLayerSettings s0, s1;
    s0.displacementContrast   = 1;
    s0.displacementBrightness = 2;
    s0.tessellationAmount     = 4;
    s1.displacementContrast   = 1;
    s1.displacementBrightness = 2;
    s1.tessellationAmount     = 12;
    GtsPackedLayerSet layers;
    REQUIRE(layers.addLayer(a0, n, s0).ok());
    REQUIRE(layers.addLayer(a1, n, s1).ok());
    Heightmap heights(1, 1), displacement(1, 1), tessellation(1, 1);
    auto      result = bakeGtsPackedLayerDisplacement(displacement, tessellation, heights, splat, layers, 0, 0, 0, 2);
    REQUIRE(result.ok());
    CHECK(result.value() == 2);
    CHECK(std::abs(displacement.height(0, 0) - 0.875F) < 1e-6F);
    CHECK(std::abs(tessellation.height(0, 0) - 12.0F) < 1e-6F);
    REQUIRE(bakeGtsPackedLayerDisplacement(displacement, tessellation, heights, splat, layers, 400, 0, 0, 2).ok());
    CHECK(displacement.height(0, 0) == 0);
    CHECK(tessellation.height(0, 0) == 0);
    const auto before = displacement.data();
    CHECK(!bakeGtsPackedLayerDisplacement(displacement, tessellation, heights, splat, layers, 0, 0, 0, 2, 0, 0, 0, 0, 1)
               .ok());
    CHECK(displacement.data() == before);
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    exposeTerrainImageAdapter(table);
    vm.addFunc("d", [&]() { return &displacement; });
    vm.addFunc("t", [&]() { return &tessellation; });
    vm.addFunc("h", [&]() { return &heights; });
    vm.addFunc("splat", [&]() { return &splat; });
    vm.addFunc("layers", [&]() { return &layers; });
    vm.run(vm.compileSource(
        R"(assert(eve.bakeGtsPackedLayerDisplacement(d(),t(),h(),splat(),layers(),0.0,0.0,0.0,1.0,0.0,0.0,0.0,1.0,1.0).ok);)"));
    CHECK(std::abs(displacement.height(0, 0) - 0.875F) < 1e-6F);
}


TEST_CASE("procgen.gtsGlobalBlend.matchesCameraSquaredDistanceAndBinding") {
    Heightmap heights(2, 1), output(2, 1);
    heights.setHeight(0, 0, 0);
    heights.setHeight(1, 0, 0);
    auto generated = generateGtsGlobalBlendDistance(output, heights, 0, 0, 0, 100, 2, 0, 0, 0, 5, 1);
    REQUIRE(generated.ok());
    CHECK(generated.value() == 1);
    CHECK(output.height(0, 0) == 0);
    CHECK(std::abs(output.height(1, 0) - 0.0625F) < 1e-6F);
    const auto before = output.data();
    CHECK(!generateGtsGlobalBlendDistance(output, heights, 0, 0, 0, -1, 1).ok());
    CHECK(output.data() == before);
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    exposeTerrainImageAdapter(table);
    vm.addFunc("heights", [&]() { return &heights; });
    vm.addFunc("output", [&]() { return &output; });
    vm.run(vm.compileSource(R"(
        local r=eve.generateGtsGlobalBlendDistance(output(),heights(),0.0,0.0,0.0,100.0,2.0,0.0,0.0,0.0,5.0,1.0);
        assert(r.ok && output().height(1,0)>0.0624 && output().height(1,0)<0.0626);
    )"));
}

TEST_CASE("procgen.gtsColorVariation.failureIsAtomicAndBindingsRun") {
    eve::image::ImageData albedo(1, 1, "RGBA32F"), colorMap(1, 1, "RGBA32F"), variation(1, 1, "RGBA32F");
    Heightmap             blend(1, 1);
    albedo.setPixel(0, 0, {0.4F, 0.4F, 0.4F, 1});
    colorMap.setPixel(0, 0, {1, 0, 0, 1});
    variation.setPixel(0, 0, {0.5F, 0, 0, 1});
    blend.setHeight(0, 0, 2);
    GtsColorMapSettings invalid;
    CHECK(!bakeGtsColorMapAlbedo(albedo, colorMap, blend, invalid).ok());
    CHECK(albedo.getPixel(0, 0).r == 0.4F);
    blend.setHeight(0, 0, 0.5F);
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeTerrainImageAdapter(table);
    vm.addFunc("albedo", [&]() { return &albedo; });
    vm.addFunc("map", [&]() { return &colorMap; });
    vm.addFunc("variation", [&]() { return &variation; });
    vm.addFunc("blend", [&]() { return &blend; });
    vm.run(vm.compileSource(R"(
        local c=eve.GtsColorMapSettings();c.nearIntensity=0.25;c.farIntensity=0.75;
        assert(eve.bakeGtsColorMapAlbedo(albedo(),map(),blend(),c).ok);
        local m=eve.GtsMacroVariationSettings();m.sizeA=10.0;m.sizeB=20.0;m.sizeC=30.0;m.intensity=0.5;m.objectSpace=true;
        assert(eve.bakeGtsMacroVariationAlbedo(albedo(),variation(),m,5.0,7.0,1.0,1.0).ok);
    )"));
}


TEST_CASE("procgen.gtsGeological.failureIsAtomicAndBindingRuns") {
    Heightmap heights(1, 1), strength(1, 1), distance(1, 1);
    strength.setHeight(0, 0, 2);
    distance.setHeight(0, 0, 0);
    eve::image::ImageData albedo(1, 1, "RGBA32F"), packed(1, 1, "RGBA32F");
    eve::image::ImageData geoAlbedo(1, 1, "RGBA32F"), geoNormal(1, 1, "RGBA32F");
    albedo.setPixel(0, 0, {0.2F, 0.3F, 0.4F, 1});
    packed.setPixel(0, 0, {0.5F, 0.5F, 0.7F, 0.8F});
    geoAlbedo.setPixel(0, 0, {0.5F, 0.5F, 0.5F, 1});
    geoNormal.setPixel(0, 0, {0, 0.5F, 0, 0.5F});
    GtsGeologicalSettings geo;
    geo.enabled = true;
    CHECK(!bakeGtsGeologicalSurface(albedo, packed, heights, strength, distance, geoAlbedo, geoNormal, geo).ok());
    CHECK(albedo.getPixel(0, 0).r == 0.2F);
    CHECK(packed.getPixel(0, 0).b == 0.7F);
    strength.setHeight(0, 0, 1);
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeTerrainImageAdapter(table);
    vm.addFunc("albedo", [&]() { return &albedo; });
    vm.addFunc("packed", [&]() { return &packed; });
    vm.addFunc("heights", [&]() { return &heights; });
    vm.addFunc("strength", [&]() { return &strength; });
    vm.addFunc("distance", [&]() { return &distance; });
    vm.addFunc("geoAlbedo", [&]() { return &geoAlbedo; });
    vm.addFunc("geoNormal", [&]() { return &geoNormal; });
    vm.run(vm.compileSource(R"(
        local g=eve.GtsGeologicalSettings();g.enabled=true;g.objectSpace=true;g.nearScale=50.0;g.farScale=200.0;
        assert(eve.bakeGtsGeologicalSurface(albedo(),packed(),heights(),strength(),distance(),geoAlbedo(),geoNormal(),g,0.0).ok);
    )"));
}


TEST_CASE("procgen.gtsDetail.bindingFeedsSnowDetailAndFailureIsAtomic") {
    Heightmap heights(1, 1), strength(1, 1), distance(1, 1), greyscale(1, 1);
    heights.setHeight(0, 0, 100);
    strength.setHeight(0, 0, 1);
    distance.setHeight(0, 0, 0);
    eve::image::ImageData albedo(1, 1, "RGBA32F"), packed(1, 1, "RGBA32F"), detail(1, 1, "RGBA32F");
    eve::image::ImageData snowAlbedo(1, 1, "RGBA32F"), snowMask(1, 1, "RGBA32F");
    albedo.setPixel(0, 0, {0.2F, 0.2F, 0.2F, 1});
    packed.setPixel(0, 0, {0.5F, 0.5F, 1, 1});
    detail.setPixel(0, 0, {1, 0.5F, 0, 1});
    snowAlbedo.setPixel(0, 0, {1, 1, 1, 1});
    snowMask.setPixel(0, 0, {1, 1, 1, 1});
    GtsDetailNormalSettings bad;
    bad.nearTiling = 0;
    CHECK(!bakeGtsDetailSurface(albedo, packed, greyscale, strength, distance, detail, bad).ok());
    CHECK(albedo.getPixel(0, 0).r == 0.2F);
    CHECK(greyscale.height(0, 0) == 0);
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeTerrainImageAdapter(table);
    vm.addFunc("albedo", [&]() { return &albedo; });
    vm.addFunc("packed", [&]() { return &packed; });
    vm.addFunc("greyscale", [&]() { return &greyscale; });
    vm.addFunc("strength", [&]() { return &strength; });
    vm.addFunc("distance", [&]() { return &distance; });
    vm.addFunc("detail", [&]() { return &detail; });
    vm.addFunc("heights", [&]() { return &heights; });
    vm.addFunc("snowAlbedo", [&]() { return &snowAlbedo; });
    vm.addFunc("snowMask", [&]() { return &snowMask; });
    vm.run(vm.compileSource(R"(
        local d=eve.GtsDetailNormalSettings();d.nearTiling=100.0;d.farTiling=1000.0;
        assert(eve.bakeGtsDetailSurface(albedo(),packed(),greyscale(),strength(),distance(),detail(),d,0.0,0.0,1.0,1.0).ok);
        local s=eve.GtsSnowSurfaceSettings();s.enabled=true;s.minimumHeight=100.0;s.blendRange=20.0;s.slopeBlend=0.0;
        local r=eve.GtsRainSurfaceSettings();
        assert(eve.bakeGtsWeatherAlbedoDetailed(albedo(),heights(),snowAlbedo(),snowMask(),s,r,greyscale(),0.0,0.0,1.0,1.0).ok);
    )"));
    CHECK(albedo.getPixel(0, 0).r < 0.5F);
}

TEST_CASE("procgen.gtsWeather.exposesProfileAndBakeToRealVm") {
    Heightmap heights(1, 1);
    heights.setHeight(0, 0, 100);
    eve::image::ImageData output(1, 1, "RGBA32F"), snowAlbedo(1, 1, "RGBA32F"), snowMask(1, 1, "RGBA32F");
    output.setPixel(0, 0, {0.2F, 0.4F, 0.1F, 1});
    snowAlbedo.setPixel(0, 0, {0.8F, 0.9F, 1, 1});
    snowMask.setPixel(0, 0, {1, 1, 1, 1});
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeTerrainImageAdapter(table);
    vm.addFunc("heights", [&]() { return &heights; });
    vm.addFunc("output", [&]() { return &output; });
    vm.addFunc("snowAlbedo", [&]() { return &snowAlbedo; });
    vm.addFunc("snowMask", [&]() { return &snowMask; });
    vm.run(vm.compileSource(R"(
        local snow=eve.GtsSnowSurfaceSettings();
        snow.enabled=true;snow.minimumHeight=100.0;snow.blendRange=20.0;
        local rain=eve.GtsRainSurfaceSettings();rain.enabled=true;rain.maximumHeight=3000.0;
        local result=eve.bakeGtsWeatherAlbedo(output(),heights(),snowAlbedo(),snowMask(),snow,rain,0.0,0.0,1.0,1.0);
        assert(result.ok && result.value==1);
    )"));
    CHECK(std::abs(output.getPixel(0, 0).r - 0.41F) < 1e-6F);
}


TEST_CASE("procgen.gtsWeather.exposesPbrBakeToRealVm") {
    Heightmap heights(1, 1), displacement(1, 1), tessellation(1, 1);
    heights.setHeight(0, 0, 100);
    eve::image::ImageData normal(1, 1, "RGBA32F"), mask(1, 1, "RGBA32F"), snowNormal(1, 1, "RGBA32F");
    eve::image::ImageData snowMask(1, 1, "RGBA32F"), rainData(1, 1, "RGBA32F");
    normal.setPixel(0, 0, {0.5F, 0.5F, 1, 1});
    mask.setPixel(0, 0, {0, 0, 0, 0});
    snowNormal.setPixel(0, 0, {0.5F, 0.5F, 1, 1});
    snowMask.setPixel(0, 0, {0.2F, 0.4F, 0.5F, 0.6F});
    rainData.setPixel(0, 0, {0, 0, 0, 0});
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeTerrainImageAdapter(table);
    vm.addFunc("normal", [&]() { return &normal; });
    vm.addFunc("mask", [&]() { return &mask; });
    vm.addFunc("displacement", [&]() { return &displacement; });
    vm.addFunc("tessellation", [&]() { return &tessellation; });
    vm.addFunc("heights", [&]() { return &heights; });
    vm.addFunc("snowNormal", [&]() { return &snowNormal; });
    vm.addFunc("snowMask", [&]() { return &snowMask; });
    vm.addFunc("rainData", [&]() { return &rainData; });
    vm.run(vm.compileSource(R"(
        local snow=eve.GtsSnowSurfaceSettings();snow.enabled=true;snow.minimumHeight=100.0;snow.blendRange=20.0;
        snow.setMaskRemapMin(0.0,0.0,0.0,0.0);snow.setMaskRemapMax(1.0,1.0,3.0,1.0);
        local rain=eve.GtsRainSurfaceSettings();rain.enabled=true;rain.maximumHeight=3000.0;
        local r=eve.bakeGtsWeatherPbr(normal(),mask(),displacement(),tessellation(),heights(),snowNormal(),snowMask(),rainData(),snow,rain,0.0,0.0,0.0,1.0,1.0);
        assert(r.ok);
    )"));
    CHECK(std::abs(mask.getPixel(0, 0).a - 0.75F) < 1e-6F);
}


TEST_CASE("procgen.pcgMaskMapExport.combinesRedChannelsAndRejectsAtomically") {
    eve::image::ImageData output(2, 1, "RGBA32F"), red(2, 1, "RGBA32F");
    eve::image::ImageData green(2, 1, "RGBA32F"), blue(2, 1, "RGBA32F"), alpha(2, 1, "RGBA32F");
    output.setPixel(0, 0, {0.9F, 0.8F, 0.7F, 0.6F});
    output.setPixel(1, 0, {0.6F, 0.7F, 0.8F, 0.9F});
    red.setPixel(0, 0, {0.1F, 0.9F, 0.9F, 0.9F});
    red.setPixel(1, 0, {1.4F, 0, 0, 0});
    green.setPixel(0, 0, {0.2F, 0.8F, 0.8F, 0.8F});
    green.setPixel(1, 0, {0.3F, 0, 0, 0});
    blue.setPixel(0, 0, {0.4F, 0.7F, 0.7F, 0.7F});
    blue.setPixel(1, 0, {0.5F, 0, 0, 0});
    alpha.setPixel(0, 0, {0.6F, 0.6F, 0.6F, 0.6F});
    alpha.setPixel(1, 0, {-0.2F, 0, 0, 0});
    auto combined = combinePcgMaskMapChannels(output, red, green, blue, alpha, 0b1011);
    REQUIRE(combined.ok());
    CHECK_EQ(combined.value(), 2);
    auto first = output.getPixel(0, 0), second = output.getPixel(1, 0);
    CHECK(std::abs(first.r - 0.1F) < 1e-6F);
    CHECK(std::abs(first.g - 0.2F) < 1e-6F);
    CHECK(first.b == 0.0F);
    CHECK(std::abs(first.a - 0.6F) < 1e-6F);
    CHECK(second.r == 1.0F);
    CHECK(second.a == 0.0F);
    const auto            before = output.getPixel(0, 0);
    eve::image::ImageData wrong(1, 1, "RGBA32F");
    CHECK(!combinePcgMaskMapChannels(output, wrong, green, blue, alpha, 15).ok());
    const auto after = output.getPixel(0, 0);
    CHECK(before.r == after.r);
    CHECK(before.g == after.g);
    CHECK(before.b == after.b);
    CHECK(before.a == after.a);

    eve::image::ImageData atlas(4, 2, "RGBA32F");
    auto                  placed = placePcgMaskMapTileInto(atlas, output, 1, 0, 2, 2);
    REQUIRE(placed.ok());
    CHECK_EQ(placed.value(), 4);
    CHECK(std::abs(atlas.getPixel(1, 0).r - output.getPixel(0, 0).r) < 1e-6F);
    CHECK(std::abs(atlas.getPixel(2, 1).g - output.getPixel(1, 0).g) < 1e-6F);
    const auto atlasBefore = atlas.getPixel(1, 0);
    CHECK(!placePcgMaskMapTileInto(atlas, output, 3, 0, 2, 2).ok());
    CHECK(atlas.getPixel(1, 0).r == atlasBefore.r);

    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeTerrainImageAdapter(table);
    vm.addFunc("maskAtlas", [&]() { return &atlas; });
    vm.addFunc("maskOut", [&]() { return &output; });
    vm.addFunc("maskR", [&]() { return &red; });
    vm.addFunc("maskG", [&]() { return &green; });
    vm.addFunc("maskB", [&]() { return &blue; });
    vm.addFunc("maskA", [&]() { return &alpha; });
    vm.run(vm.compileSource(R"(
        local result=eve.combinePcgMaskMapChannels(maskOut(),maskR(),maskG(),maskB(),maskA(),15);
        assert(result.ok && result.value==2);
        local placed=eve.placePcgMaskMapTileInto(maskAtlas(),maskOut(),0,0,2,1);
        assert(placed.ok && placed.value==2);
    )"));
}

// terrain_terrace_remover.cpp
using namespace eve::procgen;


TEST_CASE("procgen.terraceRemoval.scriptBinding") {
    Heightmap source(7, 7), classes(7, 7), output(7, 7);
    for (int y = 0; y < 7; ++y)
        for (int x = 0; x < 7; ++x) source.setHeight(x, y, 0.004F * float(y));
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("source", [&]() { return &source; });
    vm.addFunc("classes", [&]() { return &classes; });
    vm.addFunc("output", [&]() { return &output; });
    vm.run(vm.compileSource(R"(
        local s=eve.TerrainTerraceRemovalSettings();
        s.perlinStrength=0.0;s.noiseSeed=73;s.terrainWorkflow=true;
        assert(eve.analyzeTerrainTerraces(classes(),source(),s).ok);
        assert(eve.removeTerrainTerraces(output(),source(),classes(),s).ok);
        s.perlinScale=-1.0;
        assert(!eve.removeTerrainTerraces(output(),source(),classes(),s).ok);
    )"));
}

// terrain_world_workspace.cpp
using namespace eve::procgen;

namespace {
[[maybe_unused]] Heightmap filled(int width, int height, float value) {
    Heightmap result(width, height);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) result.setHeight(x, y, value);
    return result;
}
[[maybe_unused]] TerrainWorldCreationSettings smallWorld() {
    TerrainWorldCreationSettings s;
    s.tilesX                   = 2;
    s.tilesZ                   = 1;
    s.tileSize                 = 2;
    s.tileHeight               = 8;
    s.heightmapResolution      = 3;
    s.controlTextureResolution = 2;
    s.detailResolution         = 2;
    s.treeResolution           = 2;
    s.objectResolution         = 2;
    s.splatLayers              = 2;
    s.nameSuffix               = "stable";
    return s;
}
[[maybe_unused]] TerrainStampSettings wholeWorld() {
    TerrainStampSettings s;
    s.centerX = 0;
    s.centerZ = 0;
    s.width   = 4;
    s.depth   = 2;
    return s;
}
}  // namespace


TEST_CASE("procgen.terrainWorld.executesCreationAndHistoryThroughRealVm") {
    TerrainWorldWorkspace world;
    Heightmap             output;
    ssq::VM               vm(1024);
    auto                  table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("world", [&]() { return &world; });
    vm.addFunc("output", [&]() { return &output; });
    vm.run(vm.compileSource(R"(
        local s=eve.TerrainWorldCreationSettings();s.tilesX=2;s.tilesZ=1;s.tileSize=2.0;s.tileHeight=8.0;
        s.heightmapResolution=3;s.controlTextureResolution=2;s.detailResolution=2;
        s.treeResolution=2;s.objectResolution=2;s.splatLayers=2;s.nameSuffix="vm";
        assert(world().create(s).value==2);
        assert(world().getTileName(0).value=="Terrain_0_0-vm");
        assert(world().getTileOriginX(0).value==-2.0 && world().getTileOriginX(1).value==0.0);
        assert(world().copyHeightmap(0,output()).value==9);
        assert(world().getTileCount()==2 && world().getOperationCount()==0);
    )"));
}

TEST_CASE("procgen.terrainHeightAdjuster.setsWorldUnitsAtomicallyAndBinds") {
    TerrainWorldWorkspace world;
    REQUIRE(world.create(smallWorld()).ok());
    auto adjusted = world.setHeightWorldUnits(250, 1000);
    REQUIRE(adjusted.ok());
    CHECK(adjusted.value() == 18);
    Heightmap output;
    REQUIRE(world.copyHeightmap(0, output).ok());
    for (float value : output.data()) CHECK(value == 0.25F);
    REQUIRE(world.setHeightWorldUnits(2000, 1000).ok());
    REQUIRE(world.copyHeightmap(1, output).ok());
    for (float value : output.data()) CHECK(value == 1);
    REQUIRE(world.undo().ok());
    REQUIRE(world.copyHeightmap(1, output).ok());
    CHECK(output.height(1, 1) == 0.25F);
    CHECK(!world.setHeightWorldUnits(1, 0).ok());
    CHECK(!world.setHeightWorldUnits(std::numeric_limits<float>::quiet_NaN(), 1000).ok());
    REQUIRE(world.copyHeightmap(1, output).ok());
    CHECK(output.height(1, 1) == 0.25F);

    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("world", [&]() { return &world; });
    vm.addFunc("output", [&]() { return &output; });
    vm.run(vm.compileSource(R"(
        assert(world().setHeightWorldUnits(400.0,1000.0).value==18);
        assert(world().copyHeightmap(0,output()).ok && output().height(1,1)==0.4);
        assert(world().undo().ok && world().copyHeightmap(0,output()).ok);
        assert(output().height(1,1)==0.25);
    )"));
}
}  // namespace test_terrain_probe

// Contract section: terrain sediment
#include "PrivateTestScope.h"

// terrain_sediment.cpp
namespace test_terrain_sediment {
using namespace eve::procgen;

TEST_CASE("procgen.sediment.scriptCombinedAndStandalone") {
    Heightmap heights(1, 1), sediment(1, 1), velocity(1, 1);
    heights.data()[0]  = 1;
    sediment.data()[0] = 2;
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("heights", [&]() { return &heights; });
    vm.addFunc("sediment", [&]() { return &sediment; });
    vm.addFunc("velocity", [&]() { return &velocity; });
    vm.run(vm.compileSource(R"(
        local w=eve.TerrainWaterSettings(); w.dt=0.0;
        local r=eve.TerrainSedimentSettings(); r.depositRate=0.0;
        local t=eve.TerrainThermalSettings(); t.iterations=0;
        assert(heights().applySediment(sediment(),velocity(),velocity(),w,r).ok);
        local f=eve.TerrainWaterField(); assert(f.reset(1,1,1.0).ok);
        assert(f.advanceHydraulic(heights(),sediment(),w,r,t,2).ok);
        assert(!f.advanceHydraulic(heights(),heights(),w,r,t,1).ok);
    )"));
    CHECK(sediment.data()[0] == 16);
    CHECK(heights.data()[0] == 1);
}
}  // namespace test_terrain_sediment

// Contract section: terrain smooth stamp script
#include "PrivateTestScope.h"

#include "common/SquirrelBinding.h"
#include "procgen/MeshBuild.h"
// terrain_smooth_stamp_script.cpp
namespace test_terrain_smooth_stamp_script {
using namespace eve::procgen;

TEST_CASE("procgen.smoothStamp.scriptBakeAndApply") {
    MeshBuild source;
    source.positions() = {0, 10, 0, 4, 10, 0, 0, 10, 4, 4, 10, 4};
    source.indices()   = {0, 1, 2, 1, 3, 2};
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    // Production registers settings before the heightmap and mesh classes.
    exposeTerrainStampSettings(table);
    table.addClass("ProcgenMeshBuild", ssq::Class::Ctor<MeshBuild()>());
    auto heightmap = table.addClass("ProcgenHeightmap", ssq::Class::Ctor<Heightmap(int, int)>());
    heightmap.addFunc("height", &Heightmap::height);
    heightmap.addFunc("setHeight", &Heightmap::setHeight);
    // Test bridge observes the actual registered settings object in the native consumer.
    vm.addFunc("apply", [handle = vm.getHandle()](Heightmap* target, const Heightmap* stamp,
                                                  const TerrainStampSettings* settings, const Heightmap* one,
                                                  const Heightmap* coverage) {
        auto value      = *settings;
        value.operation = TerrainStampOperation::SmoothRaise;
        return eve::script::projectResult(handle, applyTerrainStamp(*target, *stamp, value, *one, *coverage),
                                          [](int changed) { return eve::Value(changed); });
    });
    vm.addFunc("source", [&]() { return &source; });
    vm.run(vm.compileSource(R"(
        local builder=eve.TerrainMeshStampBuilder();
        assert(builder.setSource(source()).ok);
        local stamp=eve.ProcgenHeightmap(5,5), coverage=eve.ProcgenHeightmap(5,5);
        local baked=builder.bake(stamp,coverage,0.0,0.0,4.0,4.0,0.0);
        assert(baked.ok && baked.value==25 && stamp.height(2,2)==10.0);
        assert(!builder.bake(stamp,stamp,0.0,0.0,4.0,4.0,0.0).ok);
        local terrain=eve.ProcgenHeightmap(5,5), one=eve.ProcgenHeightmap(1,1);
        one.setHeight(0,0,1.0);
        for(local z=0;z<5;++z) for(local x=0;x<5;++x) terrain.setHeight(x,z,10.0);
        local s=eve.TerrainStampSettings();
        s.setGrid(0.0,0.0,1.0,1.0); s.setCenter(2.0,2.0); s.setSize(4.0,4.0);
        s.smoothWidth=4.0; s.edgeFade=2.0;
        local result=apply(terrain,stamp,s,one,coverage);
        assert(result.ok && result.value==9);
        assert(terrain.height(2,2)==11.0 && terrain.height(0,2)==10.0);
        s.smoothWidth=-1.0;
        assert(!apply(terrain,stamp,s,one,coverage).ok);
        assert(terrain.height(2,2)==11.0);
    )"));
}
}  // namespace test_terrain_smooth_stamp_script

// Contract section: terrain stamp script
#include "PrivateTestScope.h"

// terrain_stamp_script.cpp
namespace test_terrain_stamp_script {
TEST_CASE("procgen.mask.scriptWorldContextConfiguration") {
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    eve::procgen::exposeHeightmap(table);
    vm.run(vm.compileSource(R"(
        local world=eve.TerrainStampSettings();world.setGrid(0.0,0.0,1.0,1.0);
        world.setCenter(1.0,1.0);world.setSize(2.0,2.0);
        local grid=eve.TerrainSampleGrid();grid.width=3;grid.height=3;
        grid.setOrigin(0.0,0.0);grid.setSpacing(1.0,1.0);
        local spatial=eve.TerrainBrushBlendSettings();spatial.strength=0.25;
        assert(spatial.configureWorld(world,3,3,grid).ok);
        assert(spatial.heightXX==1.0 && spatial.heightZZ==1.0 && spatial.heightOffsetX==0.0);
        assert(spatial.brushXX==1.5 && spatial.brushOffsetX==-0.25 && spatial.strength==0.25);
        grid.setSpacing(0.0,1.0);
        assert(!spatial.configureWorld(world,3,3,grid).ok);
        assert(spatial.brushXX==1.5);
    )"));
}

TEST_CASE("procgen.mask.scriptImageMaskChannelsAndSettings") {
    using eve::procgen::Heightmap;
    Heightmap target(1, 1), channel(1, 1), curve(2, 1);
    target.data()  = {1};
    channel.data() = {0.5F};
    curve.data()   = {0, 1};
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    eve::procgen::exposeHeightmap(table);
    vm.addFunc("target", [&]() { return &target; });
    vm.addFunc("channel", [&]() { return &channel; });
    vm.addFunc("curve", [&]() { return &curve; });
    vm.run(vm.compileSource(R"(
        local s=eve.TerrainImageMaskSettings();s.tiling=true;s.rotation=0.0;s.accuracy=0.5;
        assert(target().generateImageMask(target(),channel(),channel(),channel(),channel(),curve(),s,5,0).ok);
        assert(target().height(0,0)==0.5);
        assert(!target().generateImageMask(target(),channel(),channel(),channel(),channel(),curve(),s,99,0).ok);
    )"));
    CHECK(target.data()[0] == 0.5F);
}

TEST_CASE("procgen.mask.scriptGlobalSpawnerMask") {
    using eve::procgen::Heightmap;
    Heightmap target(1, 1), input(1, 1), source(1, 1), curve(1, 1);
    input.data()[0]  = 0.8F;
    source.data()[0] = curve.data()[0] = 0.5F;
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    eve::procgen::exposeHeightmap(table);
    vm.addFunc("target", [&]() { return &target; });
    vm.addFunc("input", [&]() { return &input; });
    vm.addFunc("source", [&]() { return &source; });
    vm.addFunc("curve", [&]() { return &curve; });
    vm.run(vm.compileSource(R"(
        local settings=eve.TerrainImageMaskSettings();
        assert(target().applyGlobalSpawnerMask(input(),source(),curve(),settings,0).ok);
        assert(target().height(0,0)==0.4);
    )"));
}

TEST_CASE("procgen.mask.scriptNoiseMask") {
    using eve::procgen::Heightmap;
    Heightmap target(3, 3), input(3, 3), curve(1, 1);
    std::fill(input.data().begin(), input.data().end(), 1.0F);
    curve.data()[0] = 0.75F;
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    eve::procgen::exposeHeightmap(table);
    vm.addFunc("target", [&]() { return &target; });
    vm.addFunc("input", [&]() { return &input; });
    vm.addFunc("curve", [&]() { return &curve; });
    vm.run(vm.compileSource(R"(
        local settings=eve.TerrainNoiseMaskSettings();
        settings.octaves=2.5;settings.warpIterations=1.5;settings.seed=99;
        assert(target().generateNoiseMask(input(),curve(),settings,0,0).ok);
        assert(target().height(1,1)==0.75);
        assert(!target().generateNoiseMask(input(),curve(),settings,99,0).ok);
    )"));
}

TEST_CASE("procgen.mask.scriptCollisionMaskStack") {
    using eve::procgen::Heightmap;
    Heightmap target(1, 1), input(1, 1), mask(1, 1), curve(2, 1);
    input.data()[0] = 0.75F;
    mask.data()[0]  = 1;
    curve.data()    = {0, 1};
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    eve::procgen::exposeHeightmap(table);
    vm.addFunc("target", [&]() { return &target; });
    vm.addFunc("input", [&]() { return &input; });
    vm.addFunc("mask", [&]() { return &mask; });
    vm.addFunc("curve", [&]() { return &curve; });
    vm.run(vm.compileSource(R"(
        local stack=eve.TerrainCollisionMaskStack();
        assert(stack.addLayer(mask(),2,true,false).value==1);
        assert(stack.getLayerCount()==1);
        assert(stack.apply(target(),input(),curve(),0).ok && target().height(0,0)==0.0);
        stack.clear();assert(stack.getLayerCount()==0);
    )"));
}

TEST_CASE("procgen.mask.scriptPolygonMask") {
    using eve::procgen::Heightmap;
    Heightmap output(5, 5), brush(1, 1);
    ssq::VM   vm(1024);
    auto      table = vm.addTable("eve");
    eve::procgen::exposeHeightmap(table);
    vm.addFunc("output", [&]() { return &output; });
    vm.addFunc("brush", [&]() { return &brush; });
    vm.run(vm.compileSource(R"(
        local poly = eve.TerrainPolygonMask();
        assert(poly.addNode(1.0, 1.0, 0.5, 1.0).ok);
        assert(poly.addNode(3.0, 1.0, 0.5, 1.0).ok);
        assert(poly.addNode(3.0, 3.0, 0.5, 1.0).ok);
        assert(poly.rasterize(output(), brush(), 0.0, 0.0, 1.0, 1.0, 1).ok);
        assert(output().height(2, 2) == 1.0);
        assert(poly.getNodeCount() == 3);
        poly.clear();
        assert(poly.getNodeCount() == 0);
    )"));
    CHECK(output.data()[12] == 1.0F);
}

TEST_CASE("procgen.mask.scriptWorldBiomeBakedMaskCache") {
    using eve::procgen::Heightmap;
    Heightmap source(1, 1), output(1, 1);
    source.data()[0] = 0.625F;
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    eve::procgen::exposeHeightmap(table);
    vm.addFunc("source", [&]() { return &source; });
    vm.addFunc("output", [&]() { return &output; });
    vm.run(vm.compileSource(R"(
        local cache=eve.TerrainBakedMaskCache();
        assert(cache.store("terrain-0","biome-a",source()).value==1);
        assert(cache.copyMask("terrain-0","biome-a",output()).ok && output().height(0,0)==0.625);
        assert(cache.markDirty("biome-a").value==1);
        assert(!cache.copyMask("terrain-0","biome-a",output()).ok);
        assert(cache.store("terrain-0","biome-a",source()).value==1);
        assert(cache.erase("terrain-0","biome-a").value==1 && cache.getEntryCount()==0);
        cache.clear();
    )"));
}

TEST_CASE("procgen.mask.scriptConcavityControls") {
    using eve::procgen::Heightmap;
    Heightmap target(1, 1), heights(1, 1), curve(1, 1);
    target.data() = {0.25F};
    curve.data()  = {0.5F};
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    eve::procgen::exposeHeightmap(table);
    vm.addFunc("target", [&]() { return &target; });
    vm.addFunc("heights", [&]() { return &heights; });
    vm.addFunc("curve", [&]() { return &curve; });
    vm.run(vm.compileSource(R"(
        local s=eve.TerrainConcavitySettings();s.featureSize=1.0;s.concavity=-1.0;
        assert(target().generateConcavityMask(target(),heights(),curve(),s,3).ok);
        assert(target().height(0,0)==0.75);
        s.featureSize=0.0;
        assert(!target().generateConcavityMask(target(),heights(),curve(),s,0).ok);
    )"));
    CHECK(target.data()[0] == 0.75F);
}

TEST_CASE("procgen.mask.scriptCurvatureSettingsAndModes") {
    using eve::procgen::Heightmap;
    Heightmap target(1, 1), heights(1, 1), curve(1, 1);
    target.data() = {0.25F};
    curve.data()  = {0.5F};
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    eve::procgen::exposeHeightmap(table);
    vm.addFunc("target", [&]() { return &target; });
    vm.addFunc("heights", [&]() { return &heights; });
    vm.addFunc("curve", [&]() { return &curve; });
    vm.run(vm.compileSource(R"(
        local s=eve.TerrainCurvatureSettings();
        s.steps=1; s.directions=4; s.radius=0.1; s.worldUnits=1.0; s.intensity=1.0;
        assert(target().generateCurvatureMask(target(),heights(),curve(),s,2).ok);
        assert(target().height(0,0)==0.5);
        s.steps=0;
        assert(!target().generateCurvatureMask(target(),heights(),curve(),s,0).ok);
    )"));
    CHECK(target.data()[0] == 0.5F);
}

TEST_CASE("procgen.mask.scriptGrowShrinkCurveEvenAtZero") {
    using eve::procgen::Heightmap;
    Heightmap target(1, 1), curve(1, 1);
    target.data() = {1};
    curve.data()  = {0.25F};
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    eve::procgen::exposeHeightmap(table);
    vm.addFunc("target", [&]() { return &target; });
    vm.addFunc("curve", [&]() { return &curve; });
    vm.run(vm.compileSource(R"(
        assert(target().growShrinkMask(target(),curve(),0.0).ok);
        assert(target().height(0,0)==0.25);
        assert(target().growShrinkMask(target(),curve(),-0.25).ok);
    )"));
    CHECK(target.data()[0] == 0.25F);
}

TEST_CASE("procgen.mask.scriptErosionStrengthWorkflow") {
    using eve::procgen::Heightmap;
    Heightmap target(1, 1), oldHeights(1, 1), erosion(1, 1), brush(1, 1), curve(1, 1);
    oldHeights.data() = {0.25F};
    erosion.data()    = {0.75F};
    brush.data()      = {1};
    curve.data()      = {0.25F};
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    eve::procgen::exposeHeightmap(table);
    vm.addFunc("target", [&]() { return &target; });
    vm.addFunc("oldHeights", [&]() { return &oldHeights; });
    vm.addFunc("erosion", [&]() { return &erosion; });
    vm.addFunc("brush", [&]() { return &brush; });
    vm.addFunc("curve", [&]() { return &curve; });
    vm.run(vm.compileSource(R"(
        local s=eve.TerrainBrushBlendSettings();
        assert(target().generateErosionMask(oldHeights(),erosion(),brush(),s,curve(),0,1.0,false).ok);
        assert(target().height(0,0)==0.75);
        assert(target().applyStrength(target(),curve(),3,1.0,false).ok);
        assert(target().height(0,0)==1.0);
        assert(!target().generateErosionMask(oldHeights(),erosion(),brush(),s,curve(),99,1.0,false).ok);
        assert(target().height(0,0)==1.0);
    )"));
    CHECK(target.data()[0] == 1);
}

TEST_CASE("procgen.mask.scriptBrushSpatialBlend") {
    using eve::procgen::Heightmap;
    Heightmap target(2, 1), source(1, 1), brush(1, 1);
    target.data() = {2, 4};
    source.data() = {10};
    brush.data()  = {0.5F};
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    eve::procgen::exposeHeightmap(table);
    vm.addFunc("target", [&]() { return &target; });
    vm.addFunc("source", [&]() { return &source; });
    vm.addFunc("brush", [&]() { return &brush; });
    vm.run(vm.compileSource(R"(
        local s=eve.TerrainBrushBlendSettings();
        s.brushXX=2.0;
        assert(target().blendBrush(target(),source(),brush(),s).ok);
        assert(target().height(0,0)==6.0 && target().height(1,0)==4.0);
    )"));
    CHECK(target.data() == std::vector<float>({6, 4}));
}

TEST_CASE("procgen.stamp.scriptBindingsAndFailures") {
    using eve::procgen::Heightmap;
    Heightmap terrain(3, 3), stamp(1, 1), local(1, 1), global(1, 1);
    stamp.setHeight(0, 0, 4);
    local.setHeight(0, 0, 0.5F);
    global.setHeight(0, 0, 1);
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    eve::procgen::exposeHeightmap(table);
    vm.addFunc("getTerrain", [&terrain]() { return &terrain; });
    vm.addFunc("getStamp", [&stamp]() { return &stamp; });
    vm.addFunc("getLocal", [&local]() { return &local; });
    vm.addFunc("getGlobal", [&global]() { return &global; });
    vm.run(vm.compileSource(R"(
        local target = getTerrain();
        local s = eve.TerrainStampSettings();
        s.setCenter(1.0, 1.0); s.setSize(2.0, 2.0);
        s.baseHeight = 1.0;
        local r = target.applyStamp(getStamp(), s, 3, getLocal(), getGlobal());
        assert(r.ok && r.value == 9);
        assert(target.height(1, 1) == 3);
        local blended = getGlobal().blendMask(getLocal(), 0, 1.0, false);
        assert(blended.ok && blended.value == 1);
        s.setSize(0.0, 2.0);
        local rejected = target.applyStamp(getStamp(), s, 3, getLocal(), getGlobal());
        assert(!rejected.ok && target.height(1, 1) == 3);
        local badMode = getGlobal().blendMask(getLocal(), 999, 1.0, false);
        assert(!badMode.ok);
    )"));
    CHECK(terrain.height(1, 1) == 3);
    CHECK(global.height(0, 0) == 0.5F);
}

TEST_CASE("procgen.mask.scriptTerrainMaskStampComposition") {
    using eve::procgen::Heightmap;
    Heightmap heights(3, 3), mask(3, 3), distance(3, 3), target(3, 3);
    Heightmap curve(2, 1), reverse(2, 1), stamp(1, 1), one(1, 1);
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x) heights.setHeight(x, y, float(x));
    curve.setHeight(1, 0, 1);
    reverse.setHeight(0, 0, 1);
    stamp.setHeight(0, 0, 2);
    one.setHeight(0, 0, 1);
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    eve::procgen::exposeHeightmap(table);
    vm.addFunc("heights", [&]() { return &heights; });
    vm.addFunc("mask", [&]() { return &mask; });
    vm.addFunc("distance", [&]() { return &distance; });
    vm.addFunc("target", [&]() { return &target; });
    vm.addFunc("curve", [&]() { return &curve; });
    vm.addFunc("reverse", [&]() { return &reverse; });
    vm.addFunc("stamp", [&]() { return &stamp; });
    vm.addFunc("one", [&]() { return &one; });
    vm.run(vm.compileSource(R"(
        local slopes = mask().deriveSlope(heights(), 1.0, 1.0, 1.0);
        assert(slopes.ok && mask().height(1, 1) > 0.29 && mask().height(1, 1) < 0.30);
        local ranged = mask().rangeMask(heights(), 0.0, 2.0, curve(), curve());
        assert(ranged.ok && mask().height(1, 1) == 0.5);
        assert(mask().transformMask(mask(), curve()).ok);
        local d = eve.TerrainDistanceMaskSettings();
        d.scaleX = 1.0; d.scaleZ = 1.0; d.tiling = false;
        assert(distance().distanceMask(d, 0, reverse(), curve()).ok);
        assert(mask().blendMask(distance(), 0, 1.0, false).ok);
        local s = eve.TerrainStampSettings();
        s.setCenter(1.0, 1.0); s.setSize(2.0, 2.0);
        assert(target().applyStamp(stamp(), s, 3, mask(), one()).ok);
        assert(target().height(1, 1) == 1.0 && target().height(0, 0) == 0.0);
        assert(!distance().distanceMask(d, 999, curve(), curve()).ok);
    )"));
    CHECK(target.height(1, 1) == 1);
}

TEST_CASE("procgen.mask.scriptSmoothMask") {
    using eve::procgen::Heightmap;
    Heightmap input(15, 15), output(15, 15);
    input.setHeight(7, 7, 1);
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    eve::procgen::exposeHeightmap(table);
    vm.addFunc("input", [&]() { return &input; });
    vm.addFunc("output", [&]() { return &output; });
    vm.run(vm.compileSource(R"(
        local r=output().smoothMask(input(),0.0,1.0);
        assert(r.ok && output().height(7,7)>0.017 && output().height(7,7)<0.018);
        local before=output().height(7,7);
        assert(!output().smoothMask(input(),2.0,1.0).ok);
        assert(output().height(7,7)==before);
    )"));
}

TEST_CASE("procgen.effect.scriptAllEffectsAndFailures") {
    using eve::procgen::Heightmap;
    Heightmap terrain(3, 1), mask(3, 1), curve(2, 1);
    terrain.data() = {0.125F, 0.25F, 0.375F};
    mask.data()    = {1, 1, 1};
    curve.data()   = {0, 1};
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    eve::procgen::exposeHeightmap(table);
    vm.addFunc("terrain", [&]() { return &terrain; });
    vm.addFunc("mask", [&]() { return &mask; });
    vm.addFunc("curve", [&]() { return &curve; });
    vm.run(vm.compileSource(R"(
        local t = terrain();
        assert(t.applyContrast(mask(),2.0,1.0).ok);
        local smooth = eve.TerrainSmoothSettings();
        smooth.radius = 0.5; smooth.verticality = 0.0; smooth.strength = 0.5;
        assert(t.applySmooth(mask(),smooth).ok);
        local ridge = eve.TerrainRidgeSettings();
        assert(ridge.passes == 18);
        ridge.passes = 2; ridge.minimum = 0.0; ridge.maximum = 1.0;
        ridge.mixStrength = 0.5; ridge.exponent = 2.0; ridge.strength = 1.0;
        assert(t.applyRidges(mask(),ridge).ok);
        local terrace = eve.TerrainTerraceSettings();
        terrace.count = 4.0; terrace.bevel = 0.0; terrace.strength = 1.0;
        assert(t.applyTerrace(mask(),terrace).ok);
        assert(t.applyPower(mask(),2.0).ok);
        assert(t.applyHeightCurve(mask(),curve(),0.0,1.0).ok);
        local mix = eve.TerrainHeightMixSettings();
        mix.minimum = 0.0; mix.maximum = 1.0; mix.midpoint = 0.5;
        mix.strength = 2.0; mix.clipMinimum = 0.0; mix.clipMaximum = 0.5;
        assert(t.applyHeightMix(mask(),mask(),mix).ok);
        assert(t.height(0,0) == 0.5 && t.height(2,0) == 0.5);
        terrace.count = 0.0;
        assert(!t.applyTerrace(mask(),terrace).ok);
        ridge.passes = -1;
        assert(!t.applyRidges(mask(),ridge).ok);
        smooth.verticality = 2.0;
        assert(!t.applySmooth(mask(),smooth).ok);
        assert(!t.applyHeightCurve(mask(),curve(),1.0,0.0).ok);
        mix.clipMaximum = -1.0;
        assert(!t.applyHeightMix(mask(),mask(),mix).ok);
        t.setHeight(2,0,-1.0);
        assert(!t.applyPower(mask(),2.0).ok);
        assert(t.height(0,0) == 0.5 && t.height(2,0) == -1.0);
    )"));
    CHECK(terrain.data() == std::vector<float>({0.5F, 0.5F, -1}));
}
}  // namespace test_terrain_stamp_script

// Contract section: terrain tree
#include "PrivateTestScope.h"

// terrain_tree.cpp
namespace test_terrain_tree {
using namespace eve::procgen;

namespace {
[[maybe_unused]] TerrainTreePlacementSettings baseSettings() {
    TerrainTreePlacementSettings settings;
    settings.asset          = "tree/oak";
    settings.width          = 20;
    settings.depth          = 20;
    settings.spacing        = 10;
    settings.spawnDensity   = 1;
    settings.jitterPercent  = 0;
    settings.minimumFitness = 0;
    settings.minimumWidth   = 2;
    settings.maximumWidth   = 4;
    settings.minimumHeight  = 3;
    settings.maximumHeight  = 7;
    settings.healthyR       = 0.8F;
    settings.dryR           = 0.2F;
    settings.bendFactor     = 0.35F;
    settings.boundsRadius   = 1.5F;
    settings.seed           = 17;
    settings.namespaceId    = 91;
    return settings;
}
}  // namespace


TEST_CASE("procgen.tree.scriptSettingsModesAndExport") {
    Heightmap fitness(1, 1), heights(1, 1);
    fitness.data()[0] = 1;
    heights.data()[0] = 0.25F;
    PointSet output;
    ssq::VM  vm(1024);
    auto     table = vm.addTable("eve");
    table.addClass("PointSet", ssq::Class::Ctor<PointSet()>());
    exposeHeightmap(table);
    vm.addFunc("fitness", [&]() { return &fitness; });
    vm.addFunc("heights", [&]() { return &heights; });
    vm.addFunc("output", [&]() { return &output; });
    vm.run(vm.compileSource(R"(
        local s=eve.TerrainTreePlacementSettings();s.asset="tree/oak";
        s.width=20.0;s.depth=20.0;s.spacing=10.0;s.jitterPercent=0.0;s.minimumFitness=0.0;
        s.setScaleMode(0);s.setYOffsetMode(2);s.snapToTerrain=false;s.customOffset=7.0;
        s.minimumYOffset=0.0;s.maximumYOffset=0.0;
        s.minimumWidth=2.0;s.maximumWidth=2.0;s.minimumHeight=3.0;s.maximumHeight=3.0;
        assert(s.getScaleMode()==0 && s.getYOffsetMode()==2);
        local result=eve.exportTerrainTreePoints(output(),fitness(),heights(),s);
        assert(result.ok && result.value==9);
    )"));
    CHECK(output.getCount() == 9);
    CHECK(output.getY(0) == 7);
    CHECK(output.getScaleX(0) == 2);
}


TEST_CASE("procgen.tree.scriptRemoveAndPrototypeRefresh") {
    Heightmap fitness(1, 1), heights(1, 1);
    fitness.data()[0] = 1;
    PointSet input, output;
    auto     settings  = baseSettings();
    settings.scaleMode = TerrainTreeScaleMode::Fixed;
    REQUIRE(exportTerrainTreePoints(input, fitness, heights, settings).ok());
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    table.addClass("PointSet", ssq::Class::Ctor<PointSet()>());
    exposeHeightmap(table);
    vm.addFunc("fitness", [&]() { return &fitness; });
    vm.addFunc("input", [&]() { return &input; });
    vm.addFunc("output", [&]() { return &output; });
    vm.run(vm.compileSource(R"(
        local refresh=eve.TerrainTreeRescaleSettings();refresh.asset="tree/oak";
        refresh.setScaleMode(0);refresh.minimumWidth=5.0;refresh.maximumWidth=5.0;
        refresh.minimumHeight=6.0;refresh.maximumHeight=6.0;
        assert(eve.rescaleTerrainTreePoints(output(),input(),refresh).value==9);
        local remove=eve.TerrainTreePlacementSettings();remove.asset="tree/oak";
        remove.width=20.0;remove.depth=20.0;remove.minimumFitness=0.5;
        assert(eve.removeTerrainTreePoints(output(),output(),fitness(),remove).value==9);
    )"));
    CHECK(output.getCount() == 0);
}
}  // namespace test_terrain_tree

// Contract section: terrain water
#include "PrivateTestScope.h"

#include "procgen/heightmap/TerrainMask.h"
// terrain_water.cpp
namespace test_terrain_water {
using namespace eve::procgen;


TEST_CASE("procgen.waterFlow.scriptBinding") {
    Heightmap source(3, 3), output(3, 3);
    source.data()[4] = 1.f;
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("source", [&]() { return &source; });
    vm.addFunc("output", [&]() { return &output; });
    vm.run(vm.compileSource(R"(
        local s=eve.TerrainWaterFlowMapSettings();
        s.dropletVolume=0.11;s.absorptionRate=0.05;s.smoothIterations=0;
        assert(eve.generateTerrainWaterFlowMap(output(),source(),s).ok);
        s.absorptionRate=0.0;
        assert(!eve.generateTerrainWaterFlowMap(output(),source(),s).ok);
    )"));
    CHECK(output.data()[4] == 0.05F);
}


TEST_CASE("procgen.velocityFlow.scriptBinding") {
    Heightmap source(3, 3), output(3, 3);
    source.data() = {0.F, 0.1F, 0.2F, 0.F, 0.2F, 0.4F, 0.F, 0.3F, 0.6F};
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("sourceVelocityFlow", [&]() { return &source; });
    vm.addFunc("outputVelocityFlow", [&]() { return &output; });
    vm.run(vm.compileSource(R"(
        assert(eve.generateTerrainVelocityFlowMap(outputVelocityFlow(),sourceVelocityFlow(),3).ok);
        assert(!eve.generateTerrainVelocityFlowMap(outputVelocityFlow(),sourceVelocityFlow(),-1).ok);
    )"));
}

TEST_CASE("procgen.water.exportsSignedMaskChannelsAtomically") {
    Heightmap terrain(2, 1), output(2, 1);
    terrain.data() = {1, 0};
    TerrainWaterField field;
    REQUIRE(field.reset(2, 1, 1).ok());
    TerrainWaterSettings settings;
    settings.dt               = 1;
    settings.flowAcceleration = 1;
    REQUIRE(field.advance(terrain, settings).ok());
    REQUIRE(field.exportChannel(output, TerrainWaterChannel::VelocityX).ok());
    CHECK(output.data() == std::vector<float>({-0.5F, 0}));
    const auto original = output.data();
    CHECK(!field.exportChannel(output, static_cast<TerrainWaterChannel>(99)).ok());
    CHECK(output.data() == original);
    Heightmap wrong(1, 1);
    CHECK(!field.exportChannel(wrong, TerrainWaterChannel::Depth).ok());
    REQUIRE(field.exportChannel(output, TerrainWaterChannel::FluxRight).ok());
    CHECK(output.data() == std::vector<float>({1, 0}));
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("field", [&]() { return &field; });
    vm.addFunc("output", [&]() { return &output; });
    vm.run(vm.compileSource(R"(
        assert(field().exportChannel(output(),1).ok);
        assert(!field().exportChannel(output(),-1).ok);
    )"));
    CHECK(output.data() == original);
}


TEST_CASE("procgen.water.scriptOwnsFieldAndSamplesValues") {
    Heightmap terrain(2, 1);
    terrain.data() = {1, 0};
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("terrain", [&]() { return &terrain; });
    vm.run(vm.compileSource(R"(
        local field=eve.TerrainWaterField();
        assert(!field.sample(0,0).ok);
        assert(field.reset(2,1,1.0).ok && field.getWidth()==2 && field.getHeight()==1);
        local s=eve.TerrainWaterSettings();
        s.dt=1.0; s.flowAcceleration=1.0; s.precipitation=0.0; s.evaporation=0.0;
        assert(field.advance(terrain(),s).ok);
        local copy=field.sample(0,0).value;
        assert(copy.depth==1.0 && copy.fluxRight==1.0 && copy.velocityX==-0.5);
        assert(copy.fluxTop==0.0 && copy.fluxBottom==0.0 && copy.velocityZ==0.0);
        assert(field.advance(terrain(),s).ok);
        assert(copy.fluxRight==1.0 && field.sample(0,0).value.fluxRight==2.0);
        assert(!field.sample(-1,0).ok);
        s.spacingX=0.0;
        assert(!field.advance(terrain(),s).ok);
    )"));
}
}  // namespace test_terrain_water

// Contract section: thunder strike
#include "PrivateTestScope.h"

#include "weather/ThunderStrike.h"
#include "weather/Weather.h"
// thunder_strike.cpp
namespace test_thunder_strike {
using namespace eve::weather;


TEST_CASE("weather.thunderStrike.squirrelContract") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    auto    table = vm.addTable("eve");
    eve::script::exposeResultBindings(table);
    Weather::expose(table);
    vm.run(vm.compileSource(R"(
        local state = eve.ThunderStrikeState();
        local settings = eve.ThunderStrikeSettings();
        settings.intensity = 1.5; settings.radius = 350.0;
        settings.volume = 0.8; settings.audioClipCount = 3;
        local strike = eve.triggerThunderStrike(state, settings, 10.0, 20.0, 30.0, 42);
        assert(strike.ok && strike.value.y >= 70.0 && strike.value.y < 270.0);
        assert(strike.value.audioClipIndex >= 1 && strike.value.audioClipIndex < 3);
        assert(state.playing && state.intensity == 1.5);
        assert(eve.advanceThunderStrike(state, 1.0).ok && !state.playing);
    )"));
}
}  // namespace test_thunder_strike

// Contract section: timer
#include "PrivateTestScope.h"

#include "ScriptTest.h"
extern const char* timer_content;
// timer.cpp
namespace test_timer {
using namespace eve;


class TimerTest : public ScriptTest {
public:
    TimerTest() : ScriptTest(timer_content) {}
};

TEST_CASE_FIXTURE(TimerTest, "TimerTest.basic") { REQUIRE(callScript("basic")); }
}  // namespace test_timer

// Contract section: transaction
#include "PrivateTestScope.h"

#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include "StatePatchTestSupport.h"
#include "common/Module.h"
#include "statepatch/StatePatch.h"
#include "transaction/Transaction.h"
// transaction.cpp
namespace test_transaction {
// transaction.cpp
using namespace eve::transaction;

namespace {

[[maybe_unused]] eve::TransactionId transactionId(const char* text) {
    const auto parsed = eve::TransactionId::parse(text);
    return parsed ? *parsed : eve::TransactionId::nil();
}

[[maybe_unused]] eve::OperationId operationId(const char* text) {
    const auto parsed = eve::OperationId::parse(text);
    return parsed ? *parsed : eve::OperationId::nil();
}

[[maybe_unused]] Plan* createPlan(Ledger& ledger, const char* transaction, const char* correlation = "",
                                  const char* causation = "") {
    auto result = ledger.create(correlation, causation, transactionId(transaction));
    REQUIRE(result.ok());
    return result.value();
}

[[maybe_unused]] eve::OperationId stageOperation(Plan& plan, const char* operation, const char* kind,
                                                 const char* target, const char* payload) {
    auto result = plan.stage(kind, target, payload, operationId(operation));
    REQUIRE(result.ok());
    return result.value();
}

}  // namespace


TEST_CASE("transaction.script.api") {
    ssq::VM    vm(1024, ssq::Libs::ALL);
    ssq::Table eve = vm.addTable("eve");
    Transaction::expose(eve);
    vm.run(vm.compileSource(R"(
        result <- "bad";
        local transaction = eve.Transaction();
        local ledger = transaction.newLedger();
        local created = ledger.create("war-1", "order-9", "00000000-0000-4000-8000-000000000001");
        if (created.ok) {
            local plan = ledger.find(created.value.id);
            local staged = plan.stage("change_control", "base-4", "{\"owner\":\"faction-b\"}", "00000000-0000-4000-8000-000000000002");
            if (staged.ok && plan.markValid(staged.value).ok && plan.validate().ok && plan.commit().ok) {
            local copy = transaction.newLedger();
            if (copy.restore(ledger.snapshotJson()).ok)
                result = copy.at(0).getState() + ":" + copy.at(0).operationAt(0).getKind();
            }
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("committed:change_control"));
}

// transaction_participant.cpp


namespace {

using eve::Diagnostic;
using eve::DiagnosticCode;
using eve::Result;
using eve::StatusCode;
using eve::transaction::ITransactionParticipant;
using eve::transaction::TransactionContext;

[[maybe_unused]] Result<void> participantFailure(std::string_view name, std::string_view phase) {
    return Result<void>::failure(Diagnostic::error(DiagnosticCode::Failed,
                                                   std::string(name) + " failed during " + std::string(phase),
                                                   "test." + std::string(phase)));
}

class RecordingParticipant final : public ITransactionParticipant {
public:
    RecordingParticipant(std::string name, std::vector<std::string>& events)
        : name_(std::move(name)), events_(events) {}

    std::string_view name() const noexcept override { return name_; }

    Result<void> prepare(const TransactionContext& context) override {
        record("prepare", context);
        if (failPrepare) return participantFailure(name_, "prepare");
        prepared = true;
        return Result<void>::success();
    }

    Result<void> commit(const TransactionContext& context) override {
        record("commit", context);
        if (!prepared) return participantFailure(name_, "commit_without_prepare");
        if (failCommit) return participantFailure(name_, "commit");
        prepared  = false;
        committed = true;
        return Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    Result<void> rollback(const TransactionContext& context) override {
        record("rollback", context);
        if (!prepared) return participantFailure(name_, "rollback_without_prepare");
        if (failRollback) return participantFailure(name_, "rollback");
        prepared = false;
        return Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    Result<void> compensate(const TransactionContext& context) override {
        record("compensate", context);
        if (!committed) return participantFailure(name_, "compensate_without_commit");
        if (failCompensate) return participantFailure(name_, "compensate");
        committed = false;
        return Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    bool        failPrepare    = false;
    bool        failCommit     = false;
    bool        failRollback   = false;
    bool        failCompensate = false;
    bool        prepared       = false;
    bool        committed      = false;
    std::string seenTransaction;
    std::string seenCorrelation;
    std::string seenCausation;

private:
    void record(std::string_view phase, const TransactionContext& context) {
        events_.push_back(name_ + "." + std::string(phase));
        seenTransaction = context.transactionId();
        seenCorrelation = context.correlationId();
        seenCausation   = context.causationId();
    }

    std::string               name_;
    std::vector<std::string>& events_;
};

[[maybe_unused]] void checkEvents(const std::vector<std::string>&         actual,
                                  std::initializer_list<std::string_view> expected) {
    CHECK_EQ(actual.size(), expected.size());
    size_t index = 0;
    for (const auto value : expected) {
        const std::string actualValue = actual[index];
        ++index;
        CHECK_EQ(actualValue, std::string(value));
    }
}

template <size_t N>
[[maybe_unused]] auto participantSpan(std::array<ITransactionParticipant*, N>& participants) {
    return std::span<ITransactionParticipant*>(participants.data(), participants.size());
}

}  // namespace
}  // namespace test_transaction

// Contract section: ui tween script
#include "PrivateTestScope.h"

#include "ui/UI.h"
// ui_tween_script.cpp
namespace test_ui_tween_script {
TEST_CASE("UI.tween.scriptOptionalEaseAndDelay") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    // Short arities must bind: historical 3-arg host calls and documented
    // ease-without-delay. A throw here means Squirrel rejected the arity.
    vm.run(vm.compileSource(R"(
        ui <- eve.UI();
        ui.beginBuild();
        ui.beginWindow("Panel", "root");
        ui.button("Go", "go");
        ui.end();
        ui.mountBuildAs("script-tween");
        ui.animateHostPos(10.0, 20.0, 0.0);
        ui.animateHostPos(30.0, 40.0, 0.0, "linear");
        ui.animateHostSize(100.0, 50.0, 0.0, "outCubic");
        ui.animateHostOverlayAlpha(0.5, 0.0);
        ui.animateItemOpacity("go", 0.25, 0.0, "linear");
        ui.animateItemPos("go", 5.0, 6.0, 0.0);
        ui.cancelItemTweens();
        hostCount <- ui.getHostTweenCount();
        itemCount <- ui.getItemTweenCount();
    )"));

    CHECK_EQ(vm.find("hostCount").toInt(), 0);
    CHECK_EQ(vm.find("itemCount").toInt(), 0);
}
}  // namespace test_ui_tween_script

// Contract section: vehicle
#include "PrivateTestScope.h"

#include "common/ECS.h"
#include "common/ScriptProvider.h"
#include "vehicle/Vehicle.h"
#include "vehicle/VehicleMobility.h"
#include "vehicle/VehicleOrderQueueAdapter.h"
#include "weapon/Weapon.h"
// vehicle.cpp
namespace test_vehicle {
using namespace eve;


// vehicle.cpp
using namespace eve::vehicle;

namespace {

const char* kWeaponDefs = R"JSON(
[{"id":"cannon.125","logic":"hitscan","damage":320,"cooldown":4.0,
  "ammo":{"mag":1,"reserve":40,"reload":6.0}}]
)JSON";

const char* kVehicleDefs = R"JSON(
[{"id":"tank.t90","category":"tank","mobility":"kinematic",
  "maxSpeed":120,"accel":80,"turnRate":90,"radius":24,"maxHealth":600,
  "armorZones":[{"name":"front","mult":1.0}],
  "mounts":[{"name":"turret","weapon":"cannon.125","type":"turret",
             "limits":[-180,180,-8,20],"rotSpeed":45,"aimMode":"auto"}]},
 {"id":"apc","category":"apc","mobility":"kinematic",
  "maxSpeed":200,"accel":120,"turnRate":120,"radius":20,"maxHealth":300}]
)JSON";

const char* kFpsDefs = R"JSON(
[{"id":"tank.fps","mobility":"kinematic","maxSpeed":120,"accel":80,"turnRate":90,
  "radius":24,"maxHealth":600,
  "armorZones":[{"name":"front","mult":1.0},{"name":"side","mult":0.5}],
  "mounts":[{"name":"turret","weapon":"cannon.125","type":"turret",
             "limits":[-180,180,-8,20],"rotSpeed":45,"aimMode":"manual"}],
  "seats":[{"name":"driver","cameraMode":"first"},
           {"name":"gunner","cameraMode":"third","mountIndex":0}]}]
)JSON";

[[maybe_unused]] eve::weapon::Weapon* weaponMod() {
    return eve::ModuleManager::requireInstance<eve::weapon::Weapon>("Weapon");
}

[[maybe_unused]] int countVehicleView() {
    int  n    = 0;
    auto view = ecs::View<VehicleEntity, VehicleEntity::Identity>();
    for (auto it = view.begin(); it != view.end(); ++it) ++n;
    return n;
}

/** @brief 自定义移动模型：每帧向东瞬移（验证扩展点）。 */
class TeleportMobility : public IVehicleMobility {
public:
    const char* name() const override { return "test.teleport"; }
    void        update(VehicleEntity& v, float) override { v.motion()->x += 10.f; }
};

}  // namespace


static const char* kViewScript = R"SQ(
function testVehicleCppView() {
    local vehicle = eve.Vehicle()
    vehicle.registerVehiclesFromJson("[{\"id\":\"jeep\"}]")
    local v = vehicle.newVehicle("jeep", 5.0, 6.0, 0.0, "")
    if (v == null) return false
    local all = eve.view(eve.VehicleEntity)
    foreach (e in all) {
        if (e.getId() == v.getId()) return true
    }
    return false
}
)SQ";

class VehicleViewScriptTest : public ScriptTest {
public:
    VehicleViewScriptTest() : ScriptTest(kViewScript) {}
};

TEST_CASE_FIXTURE(VehicleViewScriptTest, "vehicle.script.viewSeesNewVehicle") {
    REQUIRE(callScript("testVehicleCppView"));
}

/**
 * 脚本伤害规则。`note` 是自由函数，因此 `seen = text` 写的是根表槽位，测试据此断言
 * 参数编组；`modifyDamage` 只接收稳定标识与标量，收不到引擎内部借用指针。
 */
static const char* kScriptDamageRule = R"SQ(
seen <- ""
function note(text) { seen = text }
class HalfDamage {
    function modifyDamage(vehicleId, defId, amount, zone, sourceId) {
        note(vehicleId + "|" + defId + "|" + amount.tointeger() + "|" + zone + "|" + sourceId)
        return amount * 0.5
    }
}
class BrokenRule {
    function modifyDamage(vehicleId, defId, amount, zone, sourceId) { throw "rule is broken" }
}
modifier <- HalfDamage()
broken <- BrokenRule()
)SQ";

/**
 * Script rule shared by the two probes for judgement criterion J-4. The fixture
 * only defines the class; the test body runs the `eve.provider` calls, because the
 * binding seam is declared by `Vehicle`'s constructor and fixture members are
 * constructed after the script has already run.
 */
class VehicleScriptDamageTest : public ScriptTest {
public:
    VehicleScriptDamageTest() : ScriptTest(kScriptDamageRule) { eve::ScriptProviderRegistry::instance().clearAll(); }
    ~VehicleScriptDamageTest() { eve::ScriptProviderRegistry::instance().clearAll(); }
};

TEST_CASE_FIXTURE(VehicleScriptDamageTest, "vehicle.script.damageRule.present") {
    // Provider-present probe: script answers the native damage socket.
    Vehicle module;
    module.registerVehiclesFromJson(kFpsDefs);
    VehicleEntity* v = module.newVehicle("tank.fps", 0.f, 0.f);
    REQUIRE(v != nullptr);

    REQUIRE(eve::ScriptProviderRegistry::instance().hasCapabilitySeam("IVehicleDamageModifier"));
    REQUIRE_EQ(eve::cap::listenerCount<IVehicleDamageModifier>(), std::size_t{0});

    // The publish happens in script, through the same API a game would use.
    vm.run(
        vm.compileSource("published <- eve.provider.publishFor(\"IVehicleDamageModifier\","
                         " \"vehicle.half\", modifier)"));
    ssq::Object published = vm.find("published");
    REQUIRE_EQ(published.getType(), ssq::Type::TABLE);
    REQUIRE(published.find("ok").toBool());
    REQUIRE_EQ(published.find("value").find("name").toString(), std::string("vehicle.half"));
    REQUIRE_EQ(eve::cap::listenerCount<IVehicleDamageModifier>(), std::size_t{1});
    REQUIRE_EQ(vm.find("seen").toString(), std::string(""));

    // 100 * 0.5 * front(1.0) == 50, and the rule saw the marshalled arguments.
    module.applyDamage(v, 100.f, "front", 7);
    REQUIRE_EQ(module.getHealth(v), 550.f);
    const std::string firstSeen = v->identity()->id + "|tank.fps|100|front|7";
    REQUIRE_EQ(vm.find("seen").toString(), firstSeen);

    // The script rule is retired by name, and dispatch stops with it.
    vm.run(vm.compileSource("unloaded <- eve.provider.unload(\"vehicle.half\")"));
    ssq::Object unloaded = vm.find("unloaded");
    REQUIRE_EQ(unloaded.getType(), ssq::Type::TABLE);
    REQUIRE(unloaded.find("ok").toBool());
    REQUIRE_EQ(eve::cap::listenerCount<IVehicleDamageModifier>(), std::size_t{0});

    module.applyDamage(v, 40.f, "front", 7);
    REQUIRE_EQ(module.getHealth(v), 510.f);
    // `seen` still holds the first call's text, so the retired rule was not invoked.
    REQUIRE_EQ(vm.find("seen").toString(), firstSeen);
}

TEST_CASE_FIXTURE(VehicleScriptDamageTest, "vehicle.script.damageRule.absent") {
    // Provider-absent probe. No `Vehicle` instance exists, so the module never
    // declared the seam: the pipeline keeps its old behaviour and a publish attempt
    // is refused with `NotFound` instead of silently binding nothing.
    REQUIRE(!eve::ScriptProviderRegistry::instance().hasCapabilitySeam("IVehicleDamageModifier"));

    vm.run(
        vm.compileSource("refused <- eve.provider.publishFor(\"IVehicleDamageModifier\","
                         " \"vehicle.half\", modifier)"));
    ssq::Object refused = vm.find("refused");
    REQUIRE_EQ(refused.getType(), ssq::Type::TABLE);
    REQUIRE(!refused.find("ok").toBool());
    REQUIRE_EQ(refused.find("code").toString(), std::string("not_found"));
    REQUIRE_EQ(eve::cap::listenerCount<IVehicleDamageModifier>(), std::size_t{0});

    // The same absence through the plain publish path: published, but nothing is
    // dispatched, which is what "absent provider" has to mean.
    vm.run(vm.compileSource("plain <- eve.provider.publish(\"vehicle.orphan\", modifier)"));
    REQUIRE(vm.find("plain").find("ok").toBool());
    REQUIRE_EQ(eve::cap::listenerCount<IVehicleDamageModifier>(), std::size_t{0});
}

TEST_CASE_FIXTURE(VehicleScriptDamageTest, "vehicle.script.damageRule.failureKeepsAmount") {
    // A rule that raises must not drop the hit. The alternative -- swallowing the
    // damage because a game rule has a typo -- is the failure mode this asserts
    // against, and the fallback is reported through the engine log.
    Vehicle module;
    module.registerVehiclesFromJson(kFpsDefs);
    VehicleEntity* v = module.newVehicle("tank.fps", 0.f, 0.f);
    REQUIRE(v != nullptr);

    vm.run(
        vm.compileSource("brokenPublish <- eve.provider.publishFor(\"IVehicleDamageModifier\","
                         " \"vehicle.broken\", broken)"));
    REQUIRE(vm.find("brokenPublish").find("ok").toBool());
    REQUIRE_EQ(eve::cap::listenerCount<IVehicleDamageModifier>(), std::size_t{1});

    module.applyDamage(v, 100.f, "front", 7);
    REQUIRE_EQ(module.getHealth(v), 500.f);

    // The provider survives its own failure: the raising call restored the VM stack.
    module.applyDamage(v, 100.f, "front", 7);
    REQUIRE_EQ(module.getHealth(v), 400.f);
}

static const char* kRtsFlowScript = R"SQ(
function testVehicleRtsFlow() {
    local weapon = eve.Weapon()
    weapon.registerWeaponsFromJson("[{\"id\":\"cannon\",\"logic\":\"hitscan\",\"cooldown\":1.0,\"ammo\":{\"mag\":5,\"reserve\":20,\"reload\":2.0}}]")
    local vehicle = eve.Vehicle()
    vehicle.registerVehiclesFromJson("[{\"id\":\"tank\",\"mobility\":\"kinematic\",\"maxSpeed\":100,\"accel\":80,\"turnRate\":90,\"maxHealth\":300,\"mounts\":[{\"name\":\"turret\",\"weapon\":\"cannon\",\"type\":\"turret\",\"rotSpeed\":90,\"aimMode\":\"auto\"}]}]")
    local v = vehicle.newVehicle("tank", 0.0, 0.0, 0.0, "")
    if (v == null) return false

    vehicle.moveTo(v, 100.0, 0.0)
    for (local i = 0; i < 300 && !vehicle.isArrived(v); i += 1) vehicle.update(0.1)
    if (!vehicle.isArrived(v)) return false

    local m = vehicle.getMount(v, 0)
    if (m == null) return false
    local w = weapon.mountGetWeapon(m)
    if (w == null) return false
    vehicle.attack(v, 200.0, 0.0, 0)
    for (local i = 0; i < 20; i += 1) { vehicle.update(0.1); weapon.update(0.1) }
    if (!weapon.canFire(w)) return false   // 开火后进入冷却

    vehicle.applyDamage(v, 100.0, "", 0)
    return vehicle.getHealth(v) == 200.0
}
)SQ";

class VehicleRtsFlowTest : public ScriptTest {
public:
    VehicleRtsFlowTest() : ScriptTest(kRtsFlowScript) {}
};

TEST_CASE_FIXTURE(VehicleRtsFlowTest, "vehicle.script.rtsFlow") { REQUIRE(callScript("testVehicleRtsFlow")); }

// vehicle_orders_adapter.cpp
using eve::vehicle::VehicleOrder;
using eve::vehicle::VehicleOrderQueueAdapter;
using eve::vehicle::VehicleOrderType;

namespace {

[[maybe_unused]] VehicleOrder moveOrder() {
    VehicleOrder order;
    order.type = VehicleOrderType::Move;
    order.x    = 10.f;
    order.y    = 20.f;
    return order;
}

[[maybe_unused]] VehicleOrder attackOrder() {
    VehicleOrder order;
    order.type     = VehicleOrderType::Attack;
    order.x        = 30.f;
    order.y        = 40.f;
    order.targetId = 7;
    return order;
}

[[maybe_unused]] std::string takeId(eve::Result<std::string> result) {
    REQUIRE(result.hasValue());
    return std::move(result).takeValue();
}

}  // namespace
}  // namespace test_vehicle

// Contract section: weapon
#include "PrivateTestScope.h"

#include "weapon/WeaponLogic.h"
#include "weapon/WeaponSystem.h"
// weapon.cpp
namespace test_weapon {
using namespace eve;


using namespace eve::weapon;

namespace {

const char* kDefs = R"JSON(
[{"id":"rifle","logic":"hitscan","damage":25,"range":500,"spread":0.5,
  "fireMode":"single","cooldown":0.2,
  "ammo":{"mag":30,"reserve":90,"reload":2.0}},
 {"id":"cannon","logic":"projectile","damage":320,"penetration":260,"range":1200,
  "fireMode":"single","cooldown":4.0,
  "ammo":{"mag":1,"reserve":40,"reload":6.0},
  "projectile":{"type":"shell","speed":900,"gravity":1.0,"aoe":3.0}},
 {"id":"smg","logic":"hitscan","fireMode":"burst","cooldown":0.05,
  "burst":{"size":3,"interval":0.06},
  "ammo":{"mag":30,"reserve":-1,"reload":1.0}},
 {"id":"noreload","logic":"hitscan","ammo":{"mag":5,"reserve":0,"reload":1.0}}]
)JSON";

// 近战 / 阶段机测试定义。
const char* kMeleeDefs = R"JSON(
[{"id":"sword","kind":"melee","logic":"melee","damage":40,"range":1.6,
  "stages":{"windup":0.25,"active":0.10,"recover":0.35},"arc":90,
  "resource":{"kind":"none"}},
 {"id":"cleaver","kind":"melee","logic":"melee","damage":15,"range":1.2,
  "stages":{"windup":0.1,"active":0.05,"recover":0.1},"arc":60,
  "resource":{"kind":"stamina","max":10,"cost":3}}]
)JSON";

// P0 功能测试定义（多弹丸 / 散布 bloom / 后坐 / 射击模式切换 / 开镜 / 伤害类型 / 弹药池）。
const char* kP0Defs = R"JSON(
[{"id":"shotgun","logic":"projectile","damage":10,"cooldown":0.2,
  "ammo":{"mag":2,"reserve":0,"reload":1},
  "projectile":{"type":"pellet","speed":500,"pelletCount":4,"pelletSpread":3}},
 {"id":"bloom","logic":"hitscan","cooldown":0,
  "spreadMin":1,"spreadMax":10,"spreadPerShot":3,"spreadRecover":5,
  "ammo":{"mag":50,"reserve":-1}},
 {"id":"recoil","logic":"hitscan","cooldown":0,"recoilPitch":2,"recoilYaw":-1,"recoilRecover":1,
  "ammo":{"mag":50,"reserve":-1}},
 {"id":"selector","logic":"hitscan","cooldown":0,"fireMode":"auto","fireModes":["single","burst"],
  "ammo":{"mag":50,"reserve":-1}},
 {"id":"fixedsel","logic":"hitscan","cooldown":0,"fireMode":"single","ammo":{"mag":50,"reserve":-1}},
 {"id":"aim","logic":"hitscan","cooldown":0,"zoomFov":40,"ammo":{"mag":50,"reserve":-1}},
 {"id":"typed","logic":"hitscan","cooldown":0,"damageType":"pierce","element":"fire",
  "ammo":{"mag":50,"reserve":-1}},
 {"id":"poolgun","logic":"hitscan","cooldown":0,"ammo":{"mag":2,"reserve":0,"reload":0.5}}]
)JSON";

[[maybe_unused]] int countWeaponView() {
    int  n    = 0;
    auto view = ecs::View<WeaponEntity, WeaponEntity::Identity>();
    for (auto it = view.begin(); it != view.end(); ++it) ++n;
    return n;
}

class StubProjectileService : public IProjectileService {
public:
    int  spawns = 0;
    void spawnProjectile(const WeaponEntity&, const FireRequest&) override { ++spawns; }
};

class CountingLogic : public IWeaponLogic {
public:
    int         fires = 0;
    const char* name() const override { return "test.counting"; }
    bool        canFire(const WeaponEntity&) const override { return true; }
    void        fire(WeaponEntity&, const AttackRequest&) override { ++fires; }
    void        update(WeaponEntity&, float) override {}
};

class HookLogic : public IWeaponLogic {
public:
    int         begins = 0, channels = 0, fires = 0, ends = 0;
    const char* name() const override { return "test.hook"; }
    bool        canFire(const WeaponEntity&) const override { return true; }
    void        fire(WeaponEntity&, const AttackRequest&) override { ++fires; }
    void        update(WeaponEntity&, float) override {}
    void        begin(WeaponEntity&, const AttackRequest&) override { ++begins; }
    void        channel(WeaponEntity&, const AttackRequest&, float) override { ++channels; }
    void        end(WeaponEntity&, const AttackRequest&) override { ++ends; }
};

}  // namespace


// ---------------------------------------------------------------------------
// v2：近战 / 阶段机 / 资源 / 手持位
// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------
// P0：多弹丸 / 散布 bloom / 后坐 / 射击模式 / 开镜 / 伤害类型 / 弹药池
// ---------------------------------------------------------------------------


static const char* kViewScript = R"SQ(
function testWeaponCppView() {
    local weapon = eve.Weapon()
    weapon.registerWeaponsFromJson("[{\"id\":\"pistol\",\"ammo\":{\"mag\":12}}]")
    local w = weapon.newWeapon("pistol")
    if (w == null) return false
    local all = eve.view(eve.WeaponEntity)
    foreach (e in all) {
        if (e.getId() == w.getId()) return true
    }
    return false
}

// 统计某个原生实体类视图里出现指定 id 的次数。
// weapon 模块为四个实体类各登记了一个原生→脚本视图收集器：登记到错误的类上
// 只会得到空视图，四个类共用同一个收集器则会把别的类也数进来——两种错误都必须
// 能被这里发现，所以同时检查"本类恰好一次"和"他类一次都没有"。
function countVisible(cls, id) {
    local n = 0
    foreach (e in eve.view(cls)) {
        if (e.getId() == id) n++
    }
    return n
}

function testEveryWeaponEntityClassIsVisible() {
    local weapon = eve.Weapon()
    weapon.registerWeaponsFromJson("[{\"id\":\"probe\",\"kind\":\"melee\",\"logic\":\"melee\"}]")
    local w = weapon.newWeapon("probe")
    local mount = weapon.newMount("probe-mount", "turret")
    local rig = weapon.newRig("probe-rig", "right_hand")
    local pool = weapon.newAmmoPool("probe-pool", "probe-ammo", -1)
    if (w == null || mount == null || rig == null || pool == null) return false

    if (countVisible(eve.WeaponEntity, w.getId()) != 1) return false
    if (countVisible(eve.WeaponMountEntity, mount.getId()) != 1) return false
    if (countVisible(eve.WeaponRigEntity, rig.getId()) != 1) return false
    if (countVisible(eve.AmmoPoolEntity, pool.getId()) != 1) return false

    if (countVisible(eve.WeaponMountEntity, w.getId()) != 0) return false
    if (countVisible(eve.WeaponEntity, mount.getId()) != 0) return false
    if (countVisible(eve.AmmoPoolEntity, rig.getId()) != 0) return false
    return true
}
)SQ";

class WeaponViewScriptTest : public ScriptTest {
public:
    WeaponViewScriptTest() : ScriptTest(kViewScript) {}
};

TEST_CASE_FIXTURE(WeaponViewScriptTest, "weapon.script.viewSeesNewWeapon") { REQUIRE(callScript("testWeaponCppView")); }

TEST_CASE_FIXTURE(WeaponViewScriptTest, "weapon.script.viewSeesEveryNativeEntityClass") {
    REQUIRE(callScript("testEveryWeaponEntityClassIsVisible"));
}

// ---------------------------------------------------------------------------
// 阶段 4 的第二个插座：IProjectileService 的脚本接缝。
// 形状与标量 sink 不同——两个结构体进、无返回——用来检验接缝不是为标量定制的。
// ---------------------------------------------------------------------------

/**
 * 脚本投射物提供方。`seen` 是根表槽位，测试据此断言 16 个位置参数的编组（浮点都取整，
 * 所以文本比较是精确的）；`visits` 区分"真的被调用过"和"根本没被调用"。
 */
static const char* kScriptProjectileProvider = R"SQ(
seen <- ""
visits <- 0
function note(text) { seen = text }
class ScriptProjectiles {
    function spawnProjectile(weaponId, defId, ownerId, yaw, pitch, shooterId, hasTarget,
                             targetX, targetY, targetZ, muzzleX, muzzleY, muzzleZ,
                             spread, pelletIndex, pelletCount) {
        visits += 1
        note(weaponId + "|" + defId + "|" + ownerId
             + "|" + yaw.tointeger() + "|" + pitch.tointeger() + "|" + shooterId
             + "|" + (hasTarget ? 1 : 0)
             + "|" + targetX.tointeger() + "|" + targetY.tointeger() + "|" + targetZ.tointeger()
             + "|" + muzzleX.tointeger() + "|" + muzzleY.tointeger() + "|" + muzzleZ.tointeger()
             + "|" + spread.tointeger() + "|" + pelletIndex + "|" + pelletCount)
    }
}
class BrokenProjectiles {
    function spawnProjectile(a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p) {
        visits += 1
        throw "provider is broken"
    }
}
projectiles <- ScriptProjectiles()
broken <- BrokenProjectiles()
)SQ";

/**
 * J-4 双探针的夹具。接缝由 `Weapon` 的构造函数声明，而夹具成员是在脚本跑完之后才构造的，
 * 所以发布/卸载的 `eve.provider` 调用必须写在测试体里。
 */
class WeaponProjectileProviderTest : public ScriptTest {
public:
    WeaponProjectileProviderTest() : ScriptTest(kScriptProjectileProvider) {
        eve::ScriptProviderRegistry::instance().clearAll();
    }
    ~WeaponProjectileProviderTest() { eve::ScriptProviderRegistry::instance().clearAll(); }
};

TEST_CASE_FIXTURE(WeaponProjectileProviderTest, "weapon.script.projectileProviderPresent") {
    Weapon mod;
    mod.registerWeaponsFromJson(kDefs);
    WeaponEntity* cannon = mod.newWeapon("cannon");
    REQUIRE(cannon != nullptr);
    cannon->identity()->ownerId = 42;

    vm.run(vm.compileSource(
        "published <- eve.provider.publishFor(\"IProjectileService\", \"weapon.script\", projectiles)"));
    ssq::Object published = vm.find("published");
    REQUIRE_EQ(published.getType(), ssq::Type::TABLE);
    REQUIRE(published.find("ok").toBool());
    // 脚本提供方占的是单实现槽位，消费者用的正是 `cap::query`。
    REQUIRE(eve::cap::query<IProjectileService>() != nullptr);

    // 用 `WeaponSystem::tryFire` 带一个自己写的请求，而不是 `fireAt`：这样每个被编组的字段
    // 都由测试决定。注意 `tryFireRanged` 会**改写**其中几个（`WeaponSystem.cpp:182-194`）：
    // `yaw`/`pitch` 取自武器的 aim，`spread` 取自 bloom 后的 `currentSpread`，`pelletIndex`/
    // `pelletCount` 由 `spawnPellets` 按模板弹丸数逐粒给出。所以下面设的是**引擎真正交给
    // 提供方的那些值**，而不是我塞进请求里的值——这正是"断言现实而不是断言假设"。
    AttackRequest req;
    req.shooterId                  = 7;
    req.hasTarget                  = true;
    req.targetX                    = 1.f;
    req.targetY                    = 2.f;
    req.targetZ                    = 3.f;
    req.muzzleX                    = 4.f;
    req.muzzleY                    = 5.f;
    req.muzzleZ                    = 6.f;
    cannon->aim()->yaw             = 45.f;
    cannon->aim()->pitch           = 10.f;
    cannon->state()->currentSpread = 2.f;
    mod.clearEvents();
    REQUIRE(WeaponSystem::tryFire(*cannon, req));

    REQUIRE_EQ(vm.find("visits").toInt(), 1);
    const std::string expected = cannon->identity()->id + "|cannon|42|45|10|7|1|1|2|3|4|5|6|2|0|1";
    REQUIRE_EQ(vm.find("seen").toString(), expected);
    // 开火事件仍然由武器系统产生（提供方只负责"投射物怎么来"）。
    REQUIRE(mod.getEventCount() >= 1);
    REQUIRE_EQ(mod.getEventType(0), std::string("fire"));

    // 卸载后适配器从单实现槽位摘掉：再查询必须为空（开火路径本身由 absent 用例覆盖）。
    vm.run(vm.compileSource("unloaded <- eve.provider.unload(\"weapon.script\")"));
    REQUIRE(vm.find("unloaded").find("ok").toBool());
    REQUIRE(eve::cap::query<IProjectileService>() == nullptr);
}

TEST_CASE_FIXTURE(WeaponProjectileProviderTest, "weapon.script.projectileProviderAbsent") {
    // 提供方缺席：接缝已由 `Weapon` 声明，但没人发布。缺席不是错误——投射物由游戏侧
    // 决定，开火本身必须照常成功，而且不能有任何人被调用。
    Weapon mod;
    mod.registerWeaponsFromJson(kDefs);
    WeaponEntity* cannon = mod.newWeapon("cannon");
    REQUIRE(cannon != nullptr);

    REQUIRE(eve::ScriptProviderRegistry::instance().hasCapabilitySeam(IProjectileService::capabilityName));
    REQUIRE(eve::cap::query<IProjectileService>() == nullptr);

    mod.clearEvents();
    REQUIRE(mod.fireAt(cannon, 0.f, 0.f, 0.f));
    REQUIRE_EQ(vm.find("visits").toInt(), 0);
    REQUIRE(mod.getEventCount() >= 1);  // 事件仍然产生：无提供方的语义没被改变
    REQUIRE_EQ(mod.getEventType(0), std::string("fire"));
}

TEST_CASE_FIXTURE(WeaponProjectileProviderTest, "weapon.script.projectileProviderSeesEveryPellet") {
    // 多弹丸模板：`spawnPellets` 逐粒调用提供方，粒序号/总粒数是脚本真正需要的量
    // （一枚霰弹要生成 3 个可见投射物），所以这条断言覆盖的是"每粒一次的编组"。
    Weapon mod;
    mod.registerWeaponsFromJson(
        "[{\"id\":\"scatter\",\"logic\":\"projectile\",\"cooldown\":1.0,"
        "\"ammo\":{\"mag\":2,\"reserve\":4},"
        "\"projectile\":{\"type\":\"pellet\",\"speed\":900,\"pelletCount\":3}}]");
    WeaponEntity* scatter = mod.newWeapon("scatter");
    REQUIRE(scatter != nullptr);

    vm.run(vm.compileSource(
        "published <- eve.provider.publishFor(\"IProjectileService\", \"weapon.script\", projectiles)"));
    REQUIRE(vm.find("published").find("ok").toBool());

    REQUIRE(mod.fireAt(scatter, 0.f, 0.f, 0.f));
    // 三次调用，最后一次留在 `seen` 里（同一次开火的三粒）。
    REQUIRE_EQ(vm.find("visits").toInt(), 3);
    const std::string expected = scatter->identity()->id + "|scatter|0|0|0|0|1|0|0|0|0|0|0|0|2|3";
    REQUIRE_EQ(vm.find("seen").toString(), expected);
}

TEST_CASE_FIXTURE(WeaponProjectileProviderTest, "weapon.script.projectileProviderFailureIsVisible") {
    // 提供方抛错：一次开火不能因为游戏规则写错而被吞掉，所以 `tryFire` 仍为真、事件仍产生；
    // 而 `visits == 1` 证明"确实是跑过并抛出"，不是"回调根本没跑"。
    Weapon mod;
    mod.registerWeaponsFromJson(kDefs);
    WeaponEntity* cannon = mod.newWeapon("cannon");
    REQUIRE(cannon != nullptr);

    vm.run(vm.compileSource(
        "brokenPublish <- eve.provider.publishFor(\"IProjectileService\", \"weapon.broken\", broken)"));
    REQUIRE(vm.find("brokenPublish").find("ok").toBool());
    REQUIRE(eve::cap::query<IProjectileService>() != nullptr);

    mod.clearEvents();
    REQUIRE(mod.fireAt(cannon, 0.f, 0.f, 0.f));
    REQUIRE_EQ(vm.find("visits").toInt(), 1);
    REQUIRE(mod.getEventCount() >= 1);
    REQUIRE_EQ(mod.getEventType(0), std::string("fire"));
}
}  // namespace test_weapon
