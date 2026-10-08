#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "hexmap/HexMap.h"
#include "hexmap/HexMapGenerator.h"
#include "hexmap/HexSphereGenerator.h"
#include "hexmap/HexSphereMap.h"
#include "hexmap/HexTerrainBake.h"

#include <cstdint>

using namespace eve::hexmap;

namespace {

[[nodiscard]] HexMap makePlanar(std::int32_t cellCountX = 20, std::int32_t cellCountZ = 15,
                                std::uint32_t seed = 1234u) {
    HexMap map;
    auto   created = map.reset(cellCountX, cellCountZ, seed);
    REQUIRE(created.ok());
    return map;
}

[[nodiscard]] HexMapGeneratorSettings tightSettings(std::uint32_t seed) {
    HexMapGeneratorSettings settings;
    settings.seed           = seed;
    settings.landPercentage = 50;
    settings.waterLevel     = 3;
    settings.mapBorderX     = 0;
    settings.mapBorderZ     = 0;
    settings.regionBorder   = 0;
    return settings;
}

}  // namespace

TEST_CASE("hexmap.terrainBake.roundtripPreservesPlanarCells") {
    HexMap source = makePlanar();
    REQUIRE(generateHexMap(source, tightSettings(4242u)).ok());

    const HexTerrainBake bake = snapshotHexTerrain(source);
    REQUIRE_EQ(bake.kind, HexTerrainBake::Kind::Planar);
    REQUIRE_EQ(bake.cellCountX, source.cellCountX());
    REQUIRE_EQ(bake.cellCountZ, source.cellCountZ());
    REQUIRE_EQ(bake.seed, source.seed());

    const eve::Value encoded = hexTerrainBakeToValue(bake);
    auto             decoded = hexTerrainBakeFromValue(encoded);
    REQUIRE(decoded.ok());
    REQUIRE_EQ(decoded.value().kind, HexTerrainBake::Kind::Planar);
    REQUIRE_EQ(decoded.value().cells.size(), static_cast<std::size_t>(source.cellCount()));

    HexMap target;
    REQUIRE(applyHexTerrain(target, decoded.value()).ok());
    REQUIRE_EQ(target.cellCount(), source.cellCount());
    REQUIRE_EQ(target.seed(), source.seed());
    for (std::int32_t index = 0; index < source.cellCount(); ++index) {
        const HexCoordinates coordinates = source.coordinatesAt(index);
        REQUIRE_EQ(target.values(coordinates).raw(), source.values(coordinates).raw());
        REQUIRE_EQ(target.flags(coordinates).raw(), source.flags(coordinates).raw());
    }
    CHECK(target.dirtyChunkCount() > 0);
}

TEST_CASE("hexmap.terrainBake.roundtripPreservesSphereCells") {
    HexSphereMap source;
    REQUIRE(source.reset(1, 100.f, 7u).ok());
    HexSphereGeneratorSettings settings;
    settings.seed           = 7u;
    settings.landPercentage = 45;
    REQUIRE(generateSphereMap(source, settings).ok());

    const HexTerrainBake bake = snapshotHexSphereTerrain(source);
    REQUIRE_EQ(bake.kind, HexTerrainBake::Kind::Sphere);
    REQUIRE_EQ(bake.subdivision, source.subdivision());
    REQUIRE_EQ(bake.cellCount, source.cellCount());

    auto decoded = hexTerrainBakeFromValue(hexTerrainBakeToValue(bake));
    REQUIRE(decoded.ok());

    HexSphereMap target;
    REQUIRE(target.reset(1, 100.f, 99u).ok());
    REQUIRE(applyHexSphereTerrain(target, decoded.value()).ok());
    REQUIRE_EQ(target.cellCount(), source.cellCount());
    for (std::int32_t cell = 0; cell < source.cellCount(); ++cell) {
        REQUIRE_EQ(target.values(cell).raw(), source.values(cell).raw());
        REQUIRE_EQ(target.flags(cell).raw(), source.flags(cell).raw());
    }
}

TEST_CASE("hexmap.terrainBake.rejectsMalformedAndMismatchedPayloads") {
    CHECK(!hexTerrainBakeFromValue(eve::Value(1)).ok());
    eve::Value missingKind = eve::Value::object({});
    missingKind.set("seed", eve::Value(static_cast<std::int64_t>(1)));
    CHECK(!hexTerrainBakeFromValue(missingKind).ok());

    HexMap planar = makePlanar(10, 10, 3u);
    REQUIRE(generateHexMap(planar, tightSettings(3u)).ok());
    HexTerrainBake sphereBake = snapshotHexTerrain(planar);
    sphereBake.kind           = HexTerrainBake::Kind::Sphere;
    CHECK(!applyHexTerrain(planar, sphereBake).ok());

    HexSphereMap sphere;
    REQUIRE(sphere.reset(1, 50.f, 1u).ok());
    CHECK(!applyHexSphereTerrain(sphere, snapshotHexTerrain(planar)).ok());

    HexSphereMap other;
    REQUIRE(other.reset(2, 50.f, 1u).ok());
    CHECK(!applyHexSphereTerrain(other, snapshotHexSphereTerrain(sphere)).ok());

    HexMap         empty;
    HexTerrainBake badSize = snapshotHexTerrain(planar);
    badSize.cellCountX     = 7;
    badSize.cellCountZ     = 7;
    CHECK(!applyHexTerrain(empty, badSize).ok());
}
