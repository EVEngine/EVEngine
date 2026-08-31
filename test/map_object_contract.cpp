#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "map/MapObjectContract.h"

#include <limits>
#include <string>
#include <vector>

TEST_CASE("map.objectContract.validatesRequiredKindsBoundsAndEnums") {
    const std::string contract = R"({
        "schema":"eve.map.object-contract",
        "version":1,
        "types":[{"type":"portal","properties":[
            {"name":"target","kind":"string","required":true,"enum":["forest"]},
            {"name":"x","kind":"int","required":true,"min":0,"max":10},
            {"name":"enabled","kind":"bool","required":true}
        ]}]
    })";
    eve::map::MapObject object;
    object.name = "gate";
    object.type = "portal";
    object.properties = {{"target", "forest"}, {"x", "4"}, {"enabled", "true"}};
    std::vector objects{object};
    auto valid = eve::map::validateMapObjects(objects, contract);
    CHECK(valid.ok());

    objects.front().properties["x"] = "4.5";
    auto wrongKind = eve::map::validateMapObjects(objects, contract);
    CHECK(!wrongKind.ok());
    objects.front().properties["x"] = "11";
    auto outsideBounds = eve::map::validateMapObjects(objects, contract);
    CHECK(!outsideBounds.ok());
    objects.front().properties["x"] = "4";
    objects.front().properties["target"] = "village";
    auto outsideEnum = eve::map::validateMapObjects(objects, contract);
    CHECK(!outsideEnum.ok());
}

TEST_CASE("map.objectContract.rejectsDuplicateNamesAndInvalidGeometry") {
    const std::string contract =
        R"({"schema":"eve.map.object-contract","version":1,"types":[{"type":"spawn","properties":[]}]})";
    eve::map::MapObject first;
    first.name = "spawn";
    first.type = "spawn";
    std::vector objects{first, first};
    auto duplicate = eve::map::validateMapObjects(objects, contract);
    CHECK(!duplicate.ok());

    objects.resize(1);
    objects.front().width = -1.f;
    auto negativeGeometry = eve::map::validateMapObjects(objects, contract);
    CHECK(!negativeGeometry.ok());
    objects.front().width = 0.f;
    objects.front().x = std::numeric_limits<float>::infinity();
    auto nonFiniteGeometry = eve::map::validateMapObjects(objects, contract);
    CHECK(!nonFiniteGeometry.ok());
}

TEST_CASE("map.objectContract.rejectsMalformedContractDefinitions") {
    std::vector<eve::map::MapObject> objects;
    auto badVersion = eve::map::validateMapObjects(
        objects, R"({"schema":"eve.map.object-contract","version":2,"types":[]})");
    CHECK(!badVersion.ok());
    auto duplicateTypes = eve::map::validateMapObjects(
        objects,
        R"({"schema":"eve.map.object-contract","version":1,"types":[{"type":"spawn"},{"type":"spawn"}]})");
    CHECK(!duplicateTypes.ok());
}
