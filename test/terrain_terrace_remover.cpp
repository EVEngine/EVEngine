#include <cmath>
#include <simplesquirrel/simplesquirrel.hpp>
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainStampScript.h"
#include "procgen/heightmap/TerrainTerraceRemover.h"
#include "zeroerr/unittest.h"

using namespace eve::procgen;

TEST_CASE("procgen.terraceRemoval.classificationPriorityAndEdges") {
    Heightmap source(5, 5), classes(5, 5);
    source.data().assign(25, 0.1F);
    source.setHeight(2, 1, 0.F);
    source.setHeight(2, 3, 0.3F);
    TerrainTerraceRemovalSettings settings;
    settings.minimumTerraceThreshold = 0.2F;
    settings.maximumTerraceThreshold = 0.4F;
    REQUIRE(analyzeTerrainTerraces(classes, source, settings).ok());
    CHECK(classes.height(2, 2) == float(TerrainTerraceClass::Green));
    CHECK(classes.height(0, 0) == float(TerrainTerraceClass::Black));

    const auto before = classes.data();
    settings.minimumTerraceThreshold = 0.5F;
    settings.maximumTerraceThreshold = 0.4F;
    CHECK(!analyzeTerrainTerraces(classes, source, settings).ok());
    CHECK(classes.data() == before);
    CHECK(!analyzeTerrainTerraces(source, source, TerrainTerraceRemovalSettings{}).ok());
}

TEST_CASE("procgen.terraceRemoval.pipelineAndAtomicFailure") {
    Heightmap source(7, 7), classes(7, 7), output(7, 7);
    for (int y = 0; y < 7; ++y)
        for (int x = 0; x < 7; ++x) source.setHeight(x, y, 0.005F * float(y) + (x == 3 ? 0.003F : 0.F));
    classes.data().assign(49, float(TerrainTerraceClass::Green));
    output.data().assign(49, -1.F);
    TerrainTerraceRemovalSettings settings;
    settings.perlinStrength = 0.F;
    REQUIRE(removeTerrainTerraces(output, source, classes, settings).ok());
    CHECK(output.height(0, 0) == source.height(0, 0));
    CHECK(output.height(6, 6) == source.height(6, 6));
    CHECK(output.height(3, 3) < source.height(3, 3));
    for (float value : output.data()) CHECK(std::isfinite(value));

    const auto before = output.data();
    settings.perlinScale = -1.F;
    CHECK(!removeTerrainTerraces(output, source, classes, settings).ok());
    CHECK(output.data() == before);
    CHECK(!removeTerrainTerraces(classes, source, classes, TerrainTerraceRemovalSettings{}).ok());
}

TEST_CASE("procgen.terraceRemoval.scriptBinding") {
    Heightmap source(7, 7), classes(7, 7), output(7, 7);
    for (int y = 0; y < 7; ++y)
        for (int x = 0; x < 7; ++x) source.setHeight(x, y, 0.004F * float(y));
    ssq::VM vm(1024);
    auto table = vm.addTable("eve");
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
