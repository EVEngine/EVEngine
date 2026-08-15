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
 {"id":"roof","model":"fixtures/roof.glb","category":"roof"}
]})";

TEST_CASE("housegen.library.validatesManifest") {
    HouseComponentLibrary lib; std::string error;
    CHECK(lib.loadFromJson(kKit, &error));
    CHECK_EQ(lib.count(), 6);
    CHECK(!lib.loadFromJson(R"([{"id":"broken","category":"wall"}])", &error));
    CHECK(!error.empty());
}

TEST_CASE("housegen.library.validatesMaterialOverrides") {
    HouseComponentLibrary lib; std::string error;
    REQUIRE(lib.loadFromJson(R"([{
      "id":"wall.material","model":"wall.glb","category":"wall",
      "material":{"baseColor":[0.82,0.71,0.55,1.0],"baseColorTexture":"wall.png",
                  "normalTexture":"wall-normal.png","heightTexture":"wall-height.png",
                  "metallic":0.0,"roughness":0.86,"parallaxScale":0.035,
                  "parallaxMinLayers":8,"parallaxMaxLayers":24,
                  "cellBombScale":5,"cellBombStrength":0.2,"cellBombRotation":0.1}
    }])", &error));
    const HouseComponent *component = lib.find("wall.material");
    REQUIRE(component != nullptr);
    CHECK(component->material.hasBaseColor);
    CHECK_EQ(component->material.baseColorTexture, std::string("wall.png"));
    CHECK_EQ(component->material.normalTexture, std::string("wall-normal.png"));
    CHECK_EQ(component->material.heightTexture, std::string("wall-height.png"));
    CHECK(component->material.hasMetallic);
    CHECK(component->material.hasRoughness);
    CHECK(component->material.parallaxScale > 0.f);
    CHECK(!lib.loadFromJson(R"([{
      "id":"bad.material","model":"wall.glb","category":"wall",
      "material":{"roughness":1.2}
    }])", &error));
    CHECK(error.find("between 0 and 1") != std::string::npos);
}

TEST_CASE("housegen.reproducibleAndSerializable") {
    HouseComponentLibrary lib; REQUIRE(lib.loadFromJson(kKit));
    HouseGenerator generator(&lib); HouseRequest r; r.seed = 42; r.width = 5; r.depth = 4; r.floors = 2;
    HouseLayout a, b; std::string error;
    REQUIRE(generator.generate(r, a, &error)); REQUIRE(generator.generate(r, b, &error));
    CHECK_EQ(a.toJson(), b.toJson());
    CHECK(a.instances.size() > 20);
    HouseLayout restored; REQUIRE(restored.fromJson(a.toJson(), &error));
    CHECK_EQ(restored.toJson(), a.toJson());
    CHECK(restored.validate(lib, &error));
}

TEST_CASE("housegen.requiresStructuralCategories") {
    HouseComponentLibrary lib; REQUIRE(lib.loadFromJson(R"([{"id":"wall","model":"wall.glb","category":"wall"}])"));
    HouseGenerator generator(&lib); HouseLayout out; std::string error;
    CHECK(!generator.generate(HouseRequest{}, out, &error));
    CHECK(error.find("foundation") != std::string::npos);
}

TEST_CASE("housegen.upperFloorsNeverExpandBeyondSupport") {
    HouseComponentLibrary lib; REQUIRE(lib.loadFromJson(kKit));
    HouseGenerator generator(&lib); HouseRequest r;
    r.seed = 8675309; r.width = 6; r.depth = 5; r.floors = 3;
    HouseLayout layout; REQUIRE(generator.generate(r, layout));
    for (int z = 1; z < r.floors; ++z) {
        int lowerMinX = r.width, lowerMaxX = -1, lowerMinY = r.depth, lowerMaxY = -1;
        int upperMinX = r.width, upperMaxX = -1, upperMinY = r.depth, upperMaxY = -1;
        for (const auto &instance : layout.instances) {
            const auto *component = lib.find(instance.componentId);
            if (!component || component->category != "floor") continue;
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
    HouseComponentLibrary lib; REQUIRE(lib.loadFromJson(kKit));
    HouseGenerator generator(&lib);
    const char *shapes[] = {"rectangle", "l_shape", "t_shape"};
    const char *roofs[] = {"gable", "flat", "shed"};
    const char *entrances[] = {"north", "east", "west"};
    const int rotations[] = {0, 90, 270};
    int floorCounts[3] = {};
    for (int variant = 0; variant < 3; ++variant) {
        HouseRequest r; r.seed = 100 + variant; r.width = 7; r.depth = 6; r.floors = 2;
        r.footprint = shapes[variant]; r.roof = roofs[variant]; r.entrance = entrances[variant];
        HouseLayout layout; REQUIRE(generator.generate(r, layout));
        CHECK_EQ(layout.footprintStyle, std::string(shapes[variant]));
        CHECK_EQ(layout.roofStyle, std::string(roofs[variant]));
        CHECK_EQ(layout.entranceSide, std::string(entrances[variant]));
        bool foundDoor = false;
        for (const auto &instance : layout.instances) {
            const auto *component = lib.find(instance.componentId);
            if (component && component->category == "floor" && instance.z == 0) ++floorCounts[variant];
            if (component && component->category == "door") {
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
    HouseComponentLibrary lib; REQUIRE(lib.loadFromJson(kKit));
    HouseGenerator generator(&lib); HouseRequest r;
    r.seed = 8675309; r.width = 7; r.depth = 6; r.floors = 3;
    r.footprint = "l_shape";
    HouseLayout layout; std::string error;
    REQUIRE(generator.generate(r, layout, &error));
    CHECK(layout.validate(lib, &error));

    auto roof = std::find_if(layout.instances.begin(), layout.instances.end(), [&](const auto &instance) {
        const auto *component = lib.find(instance.componentId);
        return component && component->category == "roof" && instance.z == r.floors;
    });
    REQUIRE(roof != layout.instances.end());
    layout.instances.erase(roof);
    CHECK(!layout.validate(lib, &error));
    CHECK(error.find("roof coverage") != std::string::npos);
}

TEST_CASE("housegen.facadeKeepsCornersSolidAndWindowsAligned") {
    constexpr char facadeKit[] = R"({"components":[
      {"id":"foundation","model":"foundation.glb","category":"foundation"},
      {"id":"floor","model":"floor.glb","category":"floor"},
      {"id":"wall.solid","model":"wall.glb","category":"wall"},
      {"id":"wall.window","model":"window.glb","category":"wall","tags":["window"]},
      {"id":"door","model":"door.glb","category":"door"},
      {"id":"roof","model":"roof.glb","category":"roof"}
    ]})";
    HouseComponentLibrary lib; REQUIRE(lib.loadFromJson(facadeKit));
    HouseRequest r; r.seed = 44; r.width = 4; r.depth = 4; r.floors = 2;
    r.footprint = "rectangle"; r.roof = "flat"; r.entrance = "north";
    HouseLayout layout; HouseGenerator generator(&lib); REQUIRE(generator.generate(r, layout));
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
