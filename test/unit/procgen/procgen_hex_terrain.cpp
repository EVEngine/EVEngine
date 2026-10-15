#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "hexmap/HexMap.h"
#include "hexmap/HexMapGenerator.h"
#include "hexmap/HexSphereMap.h"
#include "hexmap/HexTerrainBake.h"
#include "procgen/GeneratorRegistry.h"
#include "procgen/Grid2D.h"
#include "procgen/Params.h"
#include "procgen/algorithms/HexTerrainBindings.h"

#include <cstdint>
#include <string>

using eve::procgen::generateHexSphere;
using eve::procgen::generateHexTerrain;
using eve::procgen::GeneratorRegistry;
using eve::procgen::Grid2D;
using eve::procgen::Params;

namespace {

[[nodiscard]] Params planarParams(std::uint32_t seed, int width, int height) {
    Params params;
    params.setSeed(seed);
    params.setSize(width, height);
    params.setInt("landPercentage", 50);
    params.setInt("waterLevel", 3);
    params.setInt("mapBorderX", 0);
    params.setInt("mapBorderZ", 0);
    params.setInt("regionBorder", 0);
    return params;
}

}  // namespace

TEST_CASE("procgen.hexTerrain.bakesAPlanarMapMatchingGenerateHexMap") {
    GeneratorRegistry::instance().registerBuiltins();
    Params params = planarParams(4242u, 20, 15);
    auto   baked  = generateHexTerrain(params);
    REQUIRE(baked.ok());

    auto bake = eve::hexmap::hexTerrainBakeFromValue(baked.value());
    REQUIRE(bake.ok());
    REQUIRE_EQ(bake.value().kind, eve::hexmap::HexTerrainBake::Kind::Planar);
    REQUIRE_EQ(bake.value().cellCountX, 20);
    REQUIRE_EQ(bake.value().cellCountZ, 15);
    REQUIRE_EQ(bake.value().seed, 4242u);

    eve::hexmap::HexMap expected;
    REQUIRE(expected.reset(20, 15, 4242u).ok());
    eve::hexmap::HexMapGeneratorSettings settings;
    settings.seed           = 4242u;
    settings.landPercentage = 50;
    settings.waterLevel     = 3;
    settings.mapBorderX     = 0;
    settings.mapBorderZ     = 0;
    settings.regionBorder   = 0;
    REQUIRE(eve::hexmap::generateHexMap(expected, settings).ok());

    eve::hexmap::HexMap applied;
    REQUIRE(eve::hexmap::applyHexTerrain(applied, bake.value()).ok());
    REQUIRE_EQ(applied.cellCount(), expected.cellCount());
    for (std::int32_t index = 0; index < expected.cellCount(); ++index) {
        const auto coordinates = expected.coordinatesAt(index);
        REQUIRE_EQ(applied.values(coordinates).raw(), expected.values(coordinates).raw());
        REQUIRE_EQ(applied.flags(coordinates).raw(), expected.flags(coordinates).raw());
    }
}

TEST_CASE("procgen.hexTerrain.bakesADeterministicSphere") {
    GeneratorRegistry::instance().registerBuiltins();
    Params params;
    params.setSeed(20260918u);
    params.setInt("subdivision", 1);
    params.setFloat("radius", 100.f);
    params.setInt("landPercentage", 45);
    params.setInt("waterLevel", 0);

    auto first  = generateHexSphere(params);
    auto second = generateHexSphere(params);
    REQUIRE(first.ok());
    REQUIRE(second.ok());

    auto a = eve::hexmap::hexTerrainBakeFromValue(first.value());
    auto b = eve::hexmap::hexTerrainBakeFromValue(second.value());
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    REQUIRE_EQ(a.value().kind, eve::hexmap::HexTerrainBake::Kind::Sphere);
    REQUIRE_EQ(a.value().cellCount, b.value().cellCount);
    REQUIRE_EQ(a.value().cells.size(), b.value().cells.size());
    for (std::size_t i = 0; i < a.value().cells.size(); ++i) {
        REQUIRE_EQ(a.value().cells[i].values.raw(), b.value().cells[i].values.raw());
        REQUIRE_EQ(a.value().cells[i].flags.raw(), b.value().cells[i].flags.raw());
    }
}

TEST_CASE("procgen.hexTerrain.schemaDefaultsAndGenerateToRefuseTheBake") {
    GeneratorRegistry::instance().registerBuiltins();
    REQUIRE(GeneratorRegistry::instance().has("hex.terrain"));
    REQUIRE(GeneratorRegistry::instance().has("hex.sphere"));

    const auto* terrain = GeneratorRegistry::instance().descriptor("hex.terrain");
    REQUIRE(terrain != nullptr);
    const auto* width  = terrain->find("width");
    const auto* height = terrain->find("height");
    REQUIRE(width != nullptr);
    REQUIRE(height != nullptr);
    REQUIRE_EQ(width->defaultValue, "20");
    REQUIRE_EQ(height->defaultValue, "15");

    // Params always owns seed/width/height, so applyDefaults fills only the
    // generator tunables. Callers must setSize() to a 5x5 chunk multiple.
    Params defaults;
    REQUIRE(GeneratorRegistry::instance().applyDefaults("hex.terrain", defaults));
    REQUIRE_EQ(defaults.getWidth(), 32);
    REQUIRE_EQ(defaults.getHeight(), 32);
    REQUIRE_EQ(defaults.getInt("landPercentage", 0), 50);

    Grid2D      grid;
    std::string error;
    CHECK(!GeneratorRegistry::instance().generate("hex.terrain", defaults, grid, error));
    CHECK(error.find("generateHexTerrain") != std::string::npos);

    const auto* sphere = GeneratorRegistry::instance().descriptor("hex.sphere");
    REQUIRE(sphere != nullptr);
    const auto* subdivision = sphere->find("subdivision");
    REQUIRE(subdivision != nullptr);
    REQUIRE_EQ(subdivision->defaultValue, "4");

    Params sphereDefaults;
    REQUIRE(GeneratorRegistry::instance().applyDefaults("hex.sphere", sphereDefaults));
    REQUIRE_EQ(sphereDefaults.getInt("subdivision", -1), 4);
    CHECK(!GeneratorRegistry::instance().generate("hex.sphere", sphereDefaults, grid, error));
    CHECK(error.find("generateHexSphere") != std::string::npos);
}

TEST_CASE("procgen.hexTerrain.rejectsASizeThatIsNotAChunkMultiple") {
    Params params = planarParams(1u, 7, 7);
    auto   baked  = generateHexTerrain(params);
    CHECK(!baked.ok());
}
