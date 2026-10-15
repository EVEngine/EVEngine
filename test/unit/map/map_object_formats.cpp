#include <cstdint>
#include <limits>
#include <string>
#include <vector>
#include "map/Map.h"
#include "map/MapObjectContract.h"
#include "map/TileLayer.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// map_object_contract.cpp
namespace {
namespace map_object_contract {
TEST_CASE("map.objectContract.validatesRequiredKindsBoundsAndEnums") {
    const std::string   contract = R"({
        "schema":"eve.map.object-contract",
        "version":1,
        "types":[{"type":"portal","properties":[
            {"name":"target","kind":"string","required":true,"enum":["forest"]},
            {"name":"x","kind":"int","required":true,"min":0,"max":10},
            {"name":"enabled","kind":"bool","required":true}
        ]}]
    })";
    eve::map::MapObject object;
    object.name       = "gate";
    object.type       = "portal";
    object.properties = {{"target", "forest"}, {"x", "4"}, {"enabled", "true"}};
    std::vector objects{object};
    auto        valid = eve::map::validateMapObjects(objects, contract);
    CHECK(valid.ok());

    objects.front().properties["x"] = "4.5";
    auto wrongKind                  = eve::map::validateMapObjects(objects, contract);
    CHECK(!wrongKind.ok());
    objects.front().properties["x"] = "11";
    auto outsideBounds              = eve::map::validateMapObjects(objects, contract);
    CHECK(!outsideBounds.ok());
    objects.front().properties["x"]      = "4";
    objects.front().properties["target"] = "village";
    auto outsideEnum                     = eve::map::validateMapObjects(objects, contract);
    CHECK(!outsideEnum.ok());
}

TEST_CASE("map.objectContract.rejectsDuplicateNamesAndInvalidGeometry") {
    const std::string contract =
        R"({"schema":"eve.map.object-contract","version":1,"types":[{"type":"spawn","properties":[]}]})";
    eve::map::MapObject first;
    first.name = "spawn";
    first.type = "spawn";
    std::vector objects{first, first};
    auto        duplicate = eve::map::validateMapObjects(objects, contract);
    CHECK(!duplicate.ok());

    objects.resize(1);
    objects.front().width = -1.f;
    auto negativeGeometry = eve::map::validateMapObjects(objects, contract);
    CHECK(!negativeGeometry.ok());
    objects.front().width  = 0.f;
    objects.front().x      = std::numeric_limits<float>::infinity();
    auto nonFiniteGeometry = eve::map::validateMapObjects(objects, contract);
    CHECK(!nonFiniteGeometry.ok());
}

TEST_CASE("map.objectContract.rejectsMalformedContractDefinitions") {
    std::vector<eve::map::MapObject> objects;
    auto                             badVersion =
        eve::map::validateMapObjects(objects, R"({"schema":"eve.map.object-contract","version":2,"types":[]})");
    CHECK(!badVersion.ok());
    auto duplicateTypes = eve::map::validateMapObjects(
        objects, R"({"schema":"eve.map.object-contract","version":1,"types":[{"type":"spawn"},{"type":"spawn"}]})");
    CHECK(!duplicateTypes.ok());
}
}  // namespace map_object_contract
}  // namespace

// resource_format_map.cpp
namespace {
namespace resource_format_map {
TEST_CASE("resourceFormats.map.tiledFlipBitsSurviveAllWritePaths") {
    auto          *layer   = eve::map::Map::create()->newLayer(2, 2, 16.f, 16.f);
    const uint32_t flipped = 0x80000001u;
    layer->setTile(0, 0, static_cast<int>(flipped));
    REQUIRE_EQ(static_cast<uint32_t>(layer->getTile(0, 0)), flipped);
    REQUIRE_EQ(layer->getNonEmptyChunkCount(), 1);
    layer->clear();
    layer->fillRect(0, 0, 2, 1, static_cast<int>(flipped));
    REQUIRE_EQ(static_cast<uint32_t>(layer->getTile(1, 0)), flipped);
    REQUIRE_EQ(layer->getTile(1, 1), 0);
    layer->fill(static_cast<int>(flipped));
    REQUIRE_EQ(static_cast<uint32_t>(layer->getTile(1, 1)), flipped);
    REQUIRE_EQ(layer->getNonEmptyChunkCount(), 1);
    layer->fill(static_cast<int>(0x80000000u));
    REQUIRE_EQ(layer->getNonEmptyChunkCount(), 0);
    layer->clear();
    layer->setVisible(false);
}
}  // namespace resource_format_map
}  // namespace
