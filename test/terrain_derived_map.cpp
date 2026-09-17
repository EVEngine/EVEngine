#include <cmath>
#include <simplesquirrel/simplesquirrel.hpp>
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainDerivedMap.h"
#include "procgen/heightmap/TerrainStampScript.h"
#include "zeroerr/unittest.h"

using namespace eve::procgen;

TEST_CASE("procgen.derivedMap.aspectSourceConventions") {
    Heightmap plane(5, 5), output(5, 5);
    for (int y = 0; y < 5; ++y)
        for (int x = 0; x < 5; ++x) plane.setHeight(x, y, float(x) / 4.F);
    REQUIRE(generateTerrainHeightmapAspect(output, plane, TerrainHeightmapAspect::Aspect).ok());
    CHECK(std::abs(output.height(2, 2) - 0.75F) < 0.000001F);
    REQUIRE(generateTerrainHeightmapAspect(output, plane, TerrainHeightmapAspect::Northerness).ok());
    CHECK(std::abs(output.height(2, 2) - 0.5F) < 0.000001F);
    REQUIRE(generateTerrainHeightmapAspect(output, plane, TerrainHeightmapAspect::Easterness).ok());
    CHECK(std::abs(output.height(2, 2)) < 0.000001F);

    Heightmap flat(3, 3);
    flat.data().assign(9, 0.2F);
    REQUIRE(generateTerrainHeightmapAspect(flat, flat, TerrainHeightmapAspect::Aspect).ok());
    CHECK(flat.height(1, 1) == 0.5F);
}

TEST_CASE("procgen.derivedMap.curvatureModesAndAtomicFailure") {
    Heightmap bowl(5, 5), average(5, 5), horizontal(5, 5), vertical(5, 5);
    for (int y = 0; y < 5; ++y)
        for (int x = 0; x < 5; ++x) {
            const float dx = float(x - 2), dy = float(y - 2);
            bowl.setHeight(x, y, 0.02F * (dx * dx + 2.F * dy * dy));
        }
    REQUIRE(generateTerrainHeightmapCurvature(horizontal, bowl, TerrainHeightmapCurvature::Horizontal).ok());
    REQUIRE(generateTerrainHeightmapCurvature(vertical, bowl, TerrainHeightmapCurvature::Vertical).ok());
    REQUIRE(generateTerrainHeightmapCurvature(average, bowl, TerrainHeightmapCurvature::Average).ok());
    CHECK(std::abs(average.height(3, 2) - (horizontal.height(3, 2) + vertical.height(3, 2)) * 0.5F) < 0.000001F);
    CHECK(horizontal.height(3, 2) != vertical.height(3, 2));
    const auto before = average.data();
    CHECK(!generateTerrainHeightmapCurvature(average, bowl, static_cast<TerrainHeightmapCurvature>(9)).ok());
    CHECK(average.data() == before);
    Heightmap wrong(2, 2);
    CHECK(!generateTerrainHeightmapAspect(wrong, bowl, TerrainHeightmapAspect::Aspect).ok());
}

TEST_CASE("procgen.derivedMap.scriptBinding") {
    Heightmap source(3, 3), curvature(3, 3), aspect(3, 3), filtered(3, 3);
    source.data() = {0.F, 0.1F, 0.2F, 0.F, 0.1F, 0.2F, 0.F, 0.1F, 0.2F};
    ssq::VM vm(1024);
    auto table = vm.addTable("eve");
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

TEST_CASE("procgen.neighborhood.exactInPlaceTraversal") {
    Heightmap grow(2, 2), shrink(2, 2), output(2, 2);
    grow.data() = {0.F, 1.F, 0.F, 0.F};
    auto result = filterTerrainHeightmapNeighborhood(output, grow, 1, TerrainHeightmapNeighborhood::GrowEdges);
    REQUIRE(result.ok());
    const std::vector<float> expectedGrow{0.5F, 1.F, 0.5F, 0.5F};
    CHECK(output.data() == expectedGrow);

    shrink.data() = {0.F, 1.F, 1.F, 1.F};
    result = filterTerrainHeightmapNeighborhood(output, shrink, 1, TerrainHeightmapNeighborhood::ShrinkEdges);
    REQUIRE(result.ok());
    const std::vector<float> expectedShrink{0.F, 0.5F, 0.5F, 0.5F};
    CHECK(output.data() == expectedShrink);

    Heightmap spike(3, 3);
    spike.data() = {0.F, 0.F, 0.F, 0.F, 1.F, 0.F, 0.F, 0.F, 0.F};
    REQUIRE(filterTerrainHeightmapNeighborhood(spike, spike, 1, TerrainHeightmapNeighborhood::DeNoise).ok());
    CHECK(spike.height(1, 1) == 0.F);
}

TEST_CASE("procgen.neighborhood.invalidInputIsAtomic") {
    Heightmap source(3, 3), target(3, 3);
    source.data().assign(9, 0.25F);
    target.data().assign(9, 0.75F);
    const auto before = target.data();
    CHECK(!filterTerrainHeightmapNeighborhood(target, source, 0, TerrainHeightmapNeighborhood::DeNoise).ok());
    CHECK(target.data() == before);
    CHECK(!filterTerrainHeightmapNeighborhood(target, source, 1, static_cast<TerrainHeightmapNeighborhood>(7)).ok());
    CHECK(target.data() == before);
}

TEST_CASE("procgen.heightmapFilter.sourceOrderAndRadiusQuirk") {
    Heightmap source(2, 2), output(2, 2);
    source.data() = {0.F, 1.F, 0.F, 0.F};
    REQUIRE(smoothTerrainHeightmap(output, source, 1).ok());
    const std::vector<float> expected{0.25F, 0.5625F, 0.0625F, 0.15625F};
    CHECK(output.data() == expected);

    Heightmap wide(11, 12), radiusOutput(11, 12);
    wide.data().assign(132, 1.F);
    REQUIRE(smoothTerrainHeightmapRadius(radiusOutput, wide, 0).ok());
    CHECK(radiusOutput.height(5, 5) == 1.F);
    CHECK(std::abs(radiusOutput.height(5, 6) - 1.F) < 0.00001F);
}

TEST_CASE("procgen.heightmapFilter.convolveAndScriptBinding") {
    Heightmap source(3, 3), output(3, 3), kernel(3, 3);
    source.data() = {0.F, 0.F, 0.F, 0.F, 1.F, 0.F, 0.F, 0.F, 0.F};
    kernel.data().assign(9, 1.F);
    REQUIRE(convolveTerrainHeightmap(output, source, kernel).ok());
    CHECK(std::abs(output.height(1, 1) - 1.F / 9.F) < 0.000001F);
    CHECK(output.height(0, 0) == 0.F);

    ssq::VM vm(1024);
    auto table = vm.addTable("eve");
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

TEST_CASE("procgen.heightmapSlope.exactPlaneAndEdges") {
    Heightmap plane(3, 3), slope(3, 3);
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x) plane.setHeight(x, y, float(x) * 0.5F);
    REQUIRE(generateTerrainHeightmapSlope(slope, plane).ok());
    CHECK(std::abs(slope.height(1, 1) - float(0.5 / std::sqrt(1.25))) < 0.000001F);
    CHECK(slope.height(0, 1) < slope.height(1, 1));
    CHECK(slope.height(2, 1) < slope.height(1, 1));
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
    auto table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("slopeSource", [&]() { return &source; });
    vm.addFunc("slopeOutput", [&]() { return &output; });
    vm.run(vm.compileSource(R"(
        assert(eve.generateTerrainHeightmapSlope(slopeOutput(),slopeSource()).ok);
        assert(eve.quantizeTerrainHeightmap(slopeOutput(),slopeSource(),0.25).ok);
        assert(!eve.quantizeTerrainHeightmap(slopeOutput(),slopeSource(),0.0).ok);
    )"));
}

TEST_CASE("procgen.heightmapArithmetic.scalarAndClamp") {
    Heightmap source(2, 1), output(2, 1);
    source.data() = {0.25F, 0.75F};
    REQUIRE(applyTerrainHeightmapScalarArithmetic(output, source, 0.5F, TerrainHeightmapArithmetic::Add, false, 0.F,
                                                  0.F).ok());
    const std::vector<float> expectedAdd{0.75F, 1.25F};
    CHECK(output.data() == expectedAdd);
    REQUIRE(applyTerrainHeightmapScalarArithmetic(output, source, 2.F, TerrainHeightmapArithmetic::Multiply, true,
                                                  0.2F, 1.F).ok());
    const std::vector<float> expectedMultiply{0.5F, 1.F};
    CHECK(output.data() == expectedMultiply);
    const auto before = output.data();
    CHECK(!applyTerrainHeightmapScalarArithmetic(output, source, 0.F, TerrainHeightmapArithmetic::Divide, false,
                                                 0.F, 1.F).ok());
    CHECK(output.data() == before);
}

TEST_CASE("procgen.heightmapArithmetic.resampleLerpAndBindings") {
    Heightmap source(4, 1), operand(2, 1), mask(2, 1), output(4, 1);
    source.data().assign(4, 1.F);
    operand.data() = {0.F, 1.F};
    mask.data() = {-1.F, 2.F};
    REQUIRE(applyTerrainHeightmapRasterArithmetic(output, source, operand, TerrainHeightmapArithmetic::Subtract,
                                                  false, 0.F, 0.F).ok());
    const std::vector<float> expectedSubtract{1.F, 0.5F, 0.F, 0.F};
    CHECK(output.data() == expectedSubtract);
    REQUIRE(lerpTerrainHeightmap(output, source, operand, mask).ok());
    const std::vector<float> expectedLerp{1.F, 0.75F, 1.F, 1.F};
    CHECK(output.data() == expectedLerp);

    ssq::VM vm(1024);
    auto table = vm.addTable("eve");
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
    auto table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("transformSource", [&]() { return &source; });
    vm.addFunc("transformOutput", [&]() { return &output; });
    vm.run(vm.compileSource(R"(
        assert(eve.transformTerrainHeightmap(transformOutput(),transformSource(),1,0.0).ok);
        assert(eve.transformTerrainHeightmap(transformOutput(),transformSource(),3,2.0).ok);
    )"));
}

TEST_CASE("procgen.heightmapCopy.conditionalAndResampled") {
    Heightmap target(4, 1), source(2, 1);
    target.data() = {0.25F, 0.25F, 0.75F, 0.75F};
    source.data() = {0.F, 1.F};
    REQUIRE(copyTerrainHeightmap(target, source, TerrainHeightmapCopy::IfLess).ok());
    const std::vector<float> expectedLess{0.F, 0.25F, 0.75F, 0.75F};
    CHECK(target.data() == expectedLess);
    REQUIRE(copyTerrainHeightmap(target, source, TerrainHeightmapCopy::IfGreater).ok());
    const std::vector<float> expectedGreater{0.F, 0.5F, 1.F, 1.F};
    CHECK(target.data() == expectedGreater);
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
    auto table = vm.addTable("eve");
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
    auto table = vm.addTable("eve");
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
    auto minimum = measureTerrainHeightmap(source, TerrainHeightmapMeasure::Minimum);
    auto maximum = measureTerrainHeightmap(source, TerrainHeightmapMeasure::Maximum);
    auto sum = measureTerrainHeightmap(source, TerrainHeightmapMeasure::Sum);
    auto average = measureTerrainHeightmap(source, TerrainHeightmapMeasure::Average);
    auto base = measureTerrainHeightmap(source, TerrainHeightmapMeasure::BaseLevel);
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
    auto table = vm.addTable("eve");
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
    starts.data() = {0.1F, 0.5F};
    const auto before = output.data();
    CHECK(!quantizeTerrainHeightmapTerraces(output, source, starts, curves).ok());
    CHECK(output.data() == before);
    starts.data() = {0.F, 0.5F};

    ssq::VM vm(1024);
    auto table = vm.addTable("eve");
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
    auto average = measureTerrainHeightmapSlope(source, 0.5F, 0.5F, TerrainHeightmapSlopeQuery::NormalizedAverage);
    REQUIRE(forward.ok());
    REQUIRE(central.ok());
    REQUIRE(average.ok());
    CHECK(std::abs(forward.value() - std::sqrt(0.05)) < 0.000001);
    CHECK(central.value() == 90.0);
    CHECK(std::abs(average.value() - 60.0) < 0.00001);
    CHECK(!measureTerrainHeightmapSlope(source, 2.F, 2.F, TerrainHeightmapSlopeQuery::GridForward).ok());

    ssq::VM vm(1024);
    auto table = vm.addTable("eve");
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
    row.data() = {0.1F, 0.2F};
    column.data() = {0.3F, 0.4F, 0.5F};
    REQUIRE(setTerrainHeightmapRow(target, 1, row).ok());
    REQUIRE(setTerrainHeightmapColumn(target, 0, column).ok());
    const std::vector<float> expected{0.3F, 0.4F, 0.5F, 0.25F, 0.2F, 1.F};
    CHECK(target.data() == expected);
    const auto before = target.data();
    CHECK(!setTerrainHeightmapRow(target, 3, row).ok());
    CHECK(target.data() == before);

    ssq::VM vm(1024);
    auto table = vm.addTable("eve");
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
