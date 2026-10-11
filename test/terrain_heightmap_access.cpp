#include <simplesquirrel/simplesquirrel.hpp>
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainDerivedMap.h"
#include "procgen/heightmap/TerrainStampScript.h"
#include "zeroerr/unittest.h"

using namespace eve::procgen;

TEST_CASE("procgen.heightmapAccess.safeNormalizedAndShapeQueries") {
    Heightmap map(2, 2);
    map.data() = {0, 1, 2, 3};
    auto safe = sampleTerrainHeightmapSafe(map, -7, 9);
    REQUIRE(safe.ok());
    CHECK(safe.value() == 2);
    auto middle = sampleTerrainHeightmapNormalized(map, 0.25F, 0.25F);
    REQUIRE(middle.ok());
    CHECK(middle.value() == 1.5F);
    auto edge = sampleTerrainHeightmapNormalized(map, 1, 1);
    REQUIRE(edge.ok());
    CHECK(edge.value() == 3);
    REQUIRE(terrainHeightmapHasData(map).ok());
    CHECK(terrainHeightmapHasData(map).value());
    REQUIRE(terrainHeightmapIsPowerOfTwo(map).ok());
    CHECK(terrainHeightmapIsPowerOfTwo(map).value());
    Heightmap rectangular(2, 3), empty;
    REQUIRE(terrainHeightmapIsPowerOfTwo(rectangular).ok());
    CHECK(!terrainHeightmapIsPowerOfTwo(rectangular).value());
    REQUIRE(terrainHeightmapHasData(empty).ok());
    CHECK(!terrainHeightmapHasData(empty).value());
    CHECK(!sampleTerrainHeightmapNormalized(map, -0.1F, 0).ok());
}

TEST_CASE("procgen.heightmapAccess.scriptContract") {
    Heightmap map(2, 2);
    map.data() = {0, 1, 2, 3};
    ssq::VM vm(1024);
    auto table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("map", [&]() { return &map; });
    vm.run(vm.compileSource(R"(
        assert(map().hasData().ok && map().hasData().value);
        assert(map().isPowerOfTwo().ok && map().isPowerOfTwo().value);
        local a=eve.sampleTerrainHeightmapSafe(map(),-1,9); assert(a.ok && a.value==2.0);
        local b=eve.sampleTerrainHeightmapNormalized(map(),0.25,0.25); assert(b.ok && b.value==1.5);
    )"));
}
