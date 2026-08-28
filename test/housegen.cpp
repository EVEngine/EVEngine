#include "housegen/HouseComponentLibrary.h"
#include "housegen/HouseGenerator.h"
#include "housegen/HouseLayout.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <algorithm>

using namespace eve::housegen;

static const char *kKit = R"({"components":[
 {"id":"foundation","model":"fixtures/foundation.glb","category":"foundation"},
 {"id":"floor","model":"fixtures/floor.glb","category":"floor"},
 {"id":"wall","model":"fixtures/wall.glb","category":"wall","weight":2},
 {"id":"wall.alt","model":"fixtures/wall.glb","category":"wall","weight":1},
 {"id":"door","model":"fixtures/door.glb","category":"door"},
 {"id":"roof","model":"fixtures/roof.glb","category":"roof"},
 {"id":"stairs","model":"fixtures/stairs.glb","category":"stairs"}
]})";

TEST_CASE("housegen.library.validatesManifest") {
    HouseComponentLibrary lib;
    auto                  loaded = lib.loadFromJson(kKit);
    REQUIRE(loaded.ok());
    CHECK_EQ(lib.count(), 6);
    auto failed = lib.loadFromJson(R"([{"id":"broken","category":"wall"}])");
    REQUIRE(!failed.ok());
    CHECK(failed.status().hasDiagnostics());
}

TEST_CASE("housegen.library.validatesMaterialOverrides") {
    HouseComponentLibrary lib;
    auto                  loaded = lib.loadFromJson(R"([{
      "id":"wall.material","model":"wall.glb","category":"wall",
      "material":{"baseColor":[0.82,0.71,0.55,1.0],"baseColorTexture":"wall.png",
                  "normalTexture":"wall-normal.png","heightTexture":"wall-height.png",
                  "metallic":0.0,"roughness":0.86,"parallaxScale":0.035,
                  "parallaxMinLayers":8,"parallaxMaxLayers":24,
                  "cellBombScale":5,"cellBombStrength":0.2,"cellBombRotation":0.1}
    }])");
    REQUIRE(loaded.ok());
    const auto component = lib.find("wall.material");
    REQUIRE(component.has_value());
    CHECK(component->get().material.hasBaseColor);
    CHECK_EQ(component->get().material.baseColorTexture, std::string("wall.png"));
    CHECK_EQ(component->get().material.normalTexture, std::string("wall-normal.png"));
    CHECK_EQ(component->get().material.heightTexture, std::string("wall-height.png"));
    CHECK(component->get().material.hasMetallic);
    CHECK(component->get().material.hasRoughness);
    CHECK(component->get().material.parallaxScale > 0.f);
    auto failed = lib.loadFromJson(R"([{
      "id":"bad.material","model":"wall.glb","category":"wall",
      "material":{"roughness":1.2}
    }])");
    REQUIRE(!failed.ok());
    CHECK(failed.status().describe().find("between 0 and 1") != std::string::npos);
}

TEST_CASE("housegen.reproducibleAndSerializable") {
    HouseComponentLibrary lib;
    auto                  loaded = lib.loadFromJson(kKit);
    REQUIRE(loaded.ok());
    HouseGenerator generator(lib);
    HouseRequest   r;
    r.seed   = 42;
    r.width  = 5;
    r.depth  = 4;
    r.floors = 2;
    HouseLayout a, b;
    auto        first  = generator.generate(r, a);
    auto        second = generator.generate(r, b);
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    CHECK_EQ(a.toJson(), b.toJson());
    CHECK(a.instances.size() > 20);
    HouseLayout restored;
    auto        restoredResult = restored.fromJson(a.toJson());
    REQUIRE(restoredResult.ok());
    CHECK_EQ(restored.toJson(), a.toJson());
    auto validation = restored.validate(lib);
    CHECK(validation.ok());
}

TEST_CASE("housegen.requiresStructuralCategories") {
    HouseComponentLibrary lib;
    auto                  loaded = lib.loadFromJson(R"([{"id":"wall","model":"wall.glb","category":"wall"}])");
    REQUIRE(loaded.ok());
    HouseGenerator generator(lib);
    HouseLayout    out;
    auto           failed = generator.generate(HouseRequest{}, out);
    REQUIRE(!failed.ok());
    CHECK(failed.status().describe().find("foundation") != std::string::npos);
}

TEST_CASE("housegen.upperFloorsNeverExpandBeyondSupport") {
    HouseComponentLibrary lib;
    auto                  loaded = lib.loadFromJson(kKit);
    REQUIRE(loaded.ok());
    HouseGenerator generator(lib);
    HouseRequest   r;
    r.seed = 8675309; r.width = 6; r.depth = 5; r.floors = 3;
    HouseLayout layout;
    auto        generated = generator.generate(r, layout);
    REQUIRE(generated.ok());
    for (int z = 1; z < r.floors; ++z) {
        int lowerMinX = r.width, lowerMaxX = -1, lowerMinY = r.depth, lowerMaxY = -1;
        int upperMinX = r.width, upperMaxX = -1, upperMinY = r.depth, upperMaxY = -1;
        for (const auto &instance : layout.instances) {
            const auto component = lib.find(instance.componentId);
            if (!component || component->get().category != "floor") continue;
            if (instance.z == z - 1) {
                lowerMinX = std::min(lowerMinX, instance.x); lowerMaxX = std::max(lowerMaxX, instance.x);
                lowerMinY = std::min(lowerMinY, instance.y); lowerMaxY = std::max(lowerMaxY, instance.y);
            } else if (instance.z == z) {
                upperMinX = std::min(upperMinX, instance.x); upperMaxX = std::max(upperMaxX, instance.x);
                upperMinY = std::min(upperMinY, instance.y); upperMaxY = std::max(upperMaxY, instance.y);
            }
        }
        CHECK(upperMinX >= lowerMinX); CHECK(upperMaxX <= lowerMaxX);
        CHECK(upperMinY >= lowerMinY); CHECK(upperMaxY <= lowerMaxY);
    }
}

TEST_CASE("housegen.shapeRoofAndEntranceVariants") {
    HouseComponentLibrary lib;
    auto                  loaded = lib.loadFromJson(kKit);
    REQUIRE(loaded.ok());
    HouseGenerator generator(lib);
    const char *shapes[] = {"rectangle", "l_shape", "t_shape"};
    const char *roofs[] = {"gable", "flat", "shed"};
    const char *entrances[] = {"north", "east", "west"};
    const int rotations[] = {0, 90, 270};
    int floorCounts[3] = {};
    for (int variant = 0; variant < 3; ++variant) {
        HouseRequest r;
        r.seed      = 100 + variant;
        r.width     = 7;
        r.depth     = 6;
        r.floors    = 2;
        r.footprint = shapes[variant]; r.roof = roofs[variant]; r.entrance = entrances[variant];
        HouseLayout layout;
        auto        generated = generator.generate(r, layout);
        REQUIRE(generated.ok());
        CHECK_EQ(layout.footprintStyle, std::string(shapes[variant]));
        CHECK_EQ(layout.roofStyle, std::string(roofs[variant]));
        CHECK_EQ(layout.entranceSide, std::string(entrances[variant]));
        bool foundDoor = false;
        for (const auto &instance : layout.instances) {
            const auto component = lib.find(instance.componentId);
            if (!component) continue;
            if (component->get().category == "floor" && instance.z == 0) ++floorCounts[variant];
            if (component->get().category == "door") {
                foundDoor = true; CHECK_EQ(instance.rotationDeg, rotations[variant]);
            }
        }
        CHECK(foundDoor);
    }
    CHECK(floorCounts[0] != floorCounts[1]);
    CHECK(floorCounts[1] != floorCounts[2]);
    CHECK(floorCounts[0] != floorCounts[2]);
}

TEST_CASE("housegen.everyFloorCellRequiresCoverage") {
    HouseComponentLibrary lib;
    auto                  loaded = lib.loadFromJson(kKit);
    REQUIRE(loaded.ok());
    HouseGenerator generator(lib);
    HouseRequest   r;
    r.seed = 8675309; r.width = 7; r.depth = 6; r.floors = 3;
    r.footprint = "l_shape";
    HouseLayout layout;
    auto        generated = generator.generate(r, layout);
    REQUIRE(generated.ok());
    auto valid = layout.validate(lib);
    REQUIRE(valid.ok());

    // Pick a top roof that actually covers a floor cell below it (the stairwell roof is
    // exempt — there is no floor beneath it), so removing it must break the coverage rule.
    auto roof = std::find_if(layout.instances.begin(), layout.instances.end(), [&](const auto &instance) {
        const auto component = lib.find(instance.componentId);
        if (!component || component->get().category != "roof" || instance.z != r.floors) return false;
        return std::find_if(layout.instances.begin(), layout.instances.end(), [&](const auto &other) {
                   const auto otherComp = lib.find(other.componentId);
                   return otherComp && otherComp->get().category == "floor" && other.x == instance.x &&
                          other.y == instance.y && other.z == r.floors - 1;
               }) != layout.instances.end();
    });
    REQUIRE(roof != layout.instances.end());
    layout.instances.erase(roof);
    auto invalid = layout.validate(lib);
    REQUIRE(!invalid.ok());
    CHECK(invalid.status().describe().find("roof coverage") != std::string::npos);
}

TEST_CASE("housegen.facadeKeepsCornersSolidAndWindowsAligned") {
    constexpr char facadeKit[] = R"({"components":[
      {"id":"foundation","model":"foundation.glb","category":"foundation"},
      {"id":"floor","model":"floor.glb","category":"floor"},
      {"id":"wall.solid","model":"wall.glb","category":"wall"},
      {"id":"wall.window","model":"window.glb","category":"wall","tags":["window"]},
      {"id":"door","model":"door.glb","category":"door"},
      {"id":"roof","model":"roof.glb","category":"roof"},
      {"id":"stairs","model":"stairs.glb","category":"stairs"}
    ]})";
    HouseComponentLibrary lib;
    auto                  loaded = lib.loadFromJson(facadeKit);
    REQUIRE(loaded.ok());
    HouseRequest r;
    r.seed      = 44;
    r.width     = 4;
    r.depth     = 4;
    r.floors    = 2;
    r.footprint = "rectangle"; r.roof = "flat"; r.entrance = "north";
    HouseLayout    layout;
    HouseGenerator generator(lib);
    auto           generated = generator.generate(r, layout);
    REQUIRE(generated.ok());
    std::vector<std::tuple<int, int, int>> groundWindows, upperWindows;
    for (const auto &instance : layout.instances) {
        if (instance.componentId != "wall.window") continue;
        const bool corner = (instance.x == 0 || instance.x == r.width - 1) &&
                            (instance.y == 0 || instance.y == r.depth - 1);
        CHECK(!corner);
        auto key = std::make_tuple(instance.x, instance.y, instance.rotationDeg);
        (instance.z == 0 ? groundWindows : upperWindows).push_back(key);
    }
    REQUIRE(!groundWindows.empty());
    for (const auto &window : groundWindows)
        CHECK(std::find(upperWindows.begin(), upperWindows.end(), window) != upperWindows.end());
}

TEST_CASE("housegen.stairwellConnectsFloorsAndCarvesColumn") {
    HouseComponentLibrary lib;
    auto                  loaded = lib.loadFromJson(kKit);
    REQUIRE(loaded.ok());
    HouseRequest r;
    r.seed   = 2718;
    r.width  = 6;
    r.depth  = 5;
    r.floors = 3;
    r.footprint = "rectangle"; r.roof = "flat"; r.entrance = "north";
    HouseLayout    layout;
    HouseGenerator generator(lib);
    auto           generated = generator.generate(r, layout);
    REQUIRE(generated.ok());
    REQUIRE(layout.validate(lib).ok());

    // Every level that the house occupies carries a stair component at one column.
    std::unordered_map<int, std::pair<int, int>> stairColumns;  // floor -> (x,y)
    std::vector<std::tuple<int, int, int>>       stairCells;
    for (const auto &instance : layout.instances) {
        const auto component = lib.find(instance.componentId);
        REQUIRE(component.has_value());
        if (component->get().category == "stairs") {
            stairColumns[instance.z] = {instance.x, instance.y};
            stairCells.emplace_back(instance.x, instance.y, instance.z);
        }
    }
    REQUIRE_EQ(stairColumns.size(), 3u);  // one stair per floor
    const auto column = stairColumns[0];
    for (int z = 1; z < r.floors; ++z) CHECK(stairColumns[z] == column);
    // The stair column is carved: no floor or foundation instance occupies those cells.
    for (const auto &instance : layout.instances) {
        const auto component = lib.find(instance.componentId);
        if (!component) continue;
        const std::string category = component->get().category;
        if (category == "floor" || category == "foundation") {
            CHECK(!(instance.x == column.first && instance.y == column.second));
        }
    }
    CHECK_EQ(stairCells.size(), 3u);
}

TEST_CASE("housegen.interiorPartitionProducesRequestedRooms") {
    constexpr char partitionedKit[] = R"({"components":[
      {"id":"foundation","model":"foundation.glb","category":"foundation"},
      {"id":"floor","model":"floor.glb","category":"floor"},
      {"id":"wall","model":"wall.glb","category":"wall"},
      {"id":"door","model":"door.glb","category":"door"},
      {"id":"roof","model":"roof.glb","category":"roof"},
      {"id":"stairs","model":"stairs.glb","category":"stairs"},
      {"id":"iwall","model":"iwall.glb","category":"interior_wall"},
      {"id":"idoor","model":"idoor.glb","category":"interior_door"}
    ]})";
    HouseComponentLibrary lib;
    auto                  loaded = lib.loadFromJson(partitionedKit);
    REQUIRE(loaded.ok());
    HouseRequest r;
    r.seed   = 5150;
    r.width  = 7;
    r.depth  = 6;
    r.floors = 2;
    r.footprint = "rectangle"; r.roof = "flat"; r.entrance = "south";
    r.requiredRooms = {"living", "kitchen", "bedroom"};
    HouseLayout    layout;
    HouseGenerator generator(lib);
    auto           generated = generator.generate(r, layout);
    REQUIRE(generated.ok());
    REQUIRE(layout.validate(lib).ok());

    bool hasInnerWall = false, hasInnerDoor = false;
    for (const auto &instance : layout.instances) {
        const auto component = lib.find(instance.componentId);
        if (!component) continue;
        const std::string category = component->get().category;
        hasInnerWall = hasInnerWall || category == "interior_wall";
        hasInnerDoor = hasInnerDoor || category == "interior_door";
    }
    CHECK(hasInnerWall);
    CHECK(hasInnerDoor);

    // Each requested room name appears as a labelled room on the ground floor.
    for (const std::string &name : r.requiredRooms)
        CHECK(std::find_if(layout.rooms.begin(), layout.rooms.end(), [&](const HouseRoom &room) {
                  return room.type == name && room.z == 0;
              }) != layout.rooms.end());
}

TEST_CASE("housegen.multiRoomWithoutInteriorKitFallsBack") {
    HouseComponentLibrary lib;
    auto                  loaded = lib.loadFromJson(kKit);
    REQUIRE(loaded.ok());
    HouseRequest r;
    r.seed = 7; r.width = 6; r.depth = 5; r.floors = 2;
    r.requiredRooms = {"living", "kitchen"};
    HouseLayout    layout;
    HouseGenerator generator(lib);
    auto           generated = generator.generate(r, layout);
    REQUIRE(generated.ok());
    CHECK(!layout.diagnostics.empty());
    CHECK(layout.validate(lib).ok());
}

TEST_CASE("housegen.polygonFootprintProducesArbitraryOutline") {
    HouseComponentLibrary lib;
    auto                  loaded = lib.loadFromJson(kKit);
    REQUIRE(loaded.ok());
    HouseRequest r;
    r.seed = 31337; r.width = 7; r.depth = 6; r.floors = 1;
    r.footprint = "polygon";
    // L-shaped outline in corner coordinates [0,7]x[0,6]: the top-right quadrant is cut out.
    r.perimeter = {{0.f, 0.f}, {4.f, 0.f}, {4.f, 3.f}, {7.f, 3.f}, {7.f, 6.f}, {0.f, 6.f}};
    HouseLayout    layout;
    HouseGenerator generator(lib);
    auto           generated = generator.generate(r, layout);
    REQUIRE(generated.ok());
    REQUIRE(layout.validate(lib).ok());
    CHECK_EQ(layout.footprintStyle, std::string("polygon"));

    bool hasBottomRight = false, hasCutTopRight = false;
    for (const auto &instance : layout.instances) {
        const auto component = lib.find(instance.componentId);
        if (!component || component->get().category != "floor") continue;
        if (instance.x == 6 && instance.y == 1) hasBottomRight = true;   // inside (y<3)
        if (instance.x == 6 && instance.y == 5) hasCutTopRight = true;   // outside (y>3, x>4)
    }
    CHECK(hasBottomRight);
    CHECK(!hasCutTopRight);
    CHECK_GT(layout.instances.size(), 20u);
}

TEST_CASE("housegen.polygonRequiresAtLeastThreePoints") {
    HouseComponentLibrary lib;
    auto                  loaded = lib.loadFromJson(kKit);
    REQUIRE(loaded.ok());
    HouseRequest r;
    r.seed = 1; r.width = 6; r.depth = 6; r.floors = 1;
    r.footprint = "polygon";
    r.perimeter = {{0.f, 0.f}, {6.f, 0.f}};
    HouseLayout    layout;
    HouseGenerator generator(lib);
    auto           generated = generator.generate(r, layout);
    REQUIRE(!generated.ok());
}
