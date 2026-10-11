#include "procgen/heightmap/TerrainFile.h"
#include "zeroerr/unittest.h"

#include <array>
#include <cmath>

using namespace eve::procgen;

TEST_CASE("procgen.terrainFileRaw.byteDepthEndianAndTraversal") {
    const std::array<std::uint8_t, 4> raw8{0, 64, 128, 255};
    auto eight = decodeTerrainFile(raw8, TerrainFileFormat::Raw8);
    REQUIRE(eight.ok());
    CHECK(eight.value().format == "raw8");
    CHECK(eight.value().heightmap.height(0, 0) == 0.F);
    CHECK(std::abs(eight.value().heightmap.height(0, 1) - 64.F / 255.F) < 0.000001F);
    CHECK(std::abs(eight.value().heightmap.height(1, 0) - 128.F / 255.F) < 0.000001F);
    CHECK(eight.value().heightmap.height(1, 1) == 1.F);

    const std::array<std::uint8_t, 8> little{0, 0, 0, 64, 0, 128, 255, 255};
    const std::array<std::uint8_t, 8> big{0, 0, 64, 0, 128, 0, 255, 255};
    auto le = decodeTerrainFile(little, TerrainFileFormat::Raw16LE);
    auto be = decodeTerrainFile(big, TerrainFileFormat::Raw16BE);
    REQUIRE(le.ok());
    REQUIRE(be.ok());
    CHECK(le.value().heightmap.data() == be.value().heightmap.data());
    CHECK(std::abs(le.value().heightmap.height(1, 0) - 32768.F / 65535.F) < 0.000001F);
}

TEST_CASE("procgen.terrainFileRaw.requiresExplicitExactSquareFormat") {
    const std::array<std::uint8_t, 3> malformed{0, 1, 2};
    CHECK(!decodeTerrainFile(malformed, TerrainFileFormat::Raw8).ok());
    CHECK(!decodeTerrainFile(malformed, TerrainFileFormat::Raw16LE).ok());
    const std::array<std::uint8_t, 4> raw{0, 1, 2, 3};
    CHECK(!decodeTerrainFile(raw, TerrainFileFormat::Auto).ok());
    CHECK(int(parseTerrainFileFormat("RAW8")) == int(TerrainFileFormat::Raw8));
    CHECK(int(parseTerrainFileFormat("raw16le")) == int(TerrainFileFormat::Raw16LE));
    CHECK(int(parseTerrainFileFormat("raw16be")) == int(TerrainFileFormat::Raw16BE));
}
