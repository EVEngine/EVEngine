#include "procgen/GridGraph.h"
#include "procgen/PointGraph.h"
#include "procgen/Semantic.h"
#include "procgen/house/HouseComponentLibrary.h"
#include "procgen/house/HouseGenerator.h"
#include "procgen/house/HouseLayout.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <limits>
#include <set>

using namespace eve::housegen;

static const char *kKit = R"({"components":[
 {"id":"foundation","model":"fixtures/foundation.glb","category":"foundation"},
 {"id":"floor","model":"fixtures/floor.glb","category":"floor"},
 {"id":"wall","model":"fixtures/wall.glb","category":"wall","weight":2},
 {"id":"wall.alt","model":"fixtures/wall.glb","category":"wall","weight":1},
 {"id":"door","model":"fixtures/door.glb","category":"door"},
 {"id":"roof","model":"fixtures/roof.glb","category":"roof"}
]})";

static const char* kInteriorKit = R"({"components":[
 {"id":"foundation","model":"foundation.glb","category":"foundation"},
 {"id":"floor","model":"floor.glb","category":"floor"},
 {"id":"wall","model":"wall.glb","category":"wall"},
 {"id":"door","model":"door.glb","category":"door"},
 {"id":"roof","model":"roof.glb","category":"roof"},
 {"id":"interior.wall","model":"wall.glb","category":"interior_wall"},
 {"id":"interior.door","model":"door.glb","category":"interior_door"}
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

TEST_CASE("procgen.housegen.exportsCanonicalGridAndPointGraphValues") {
    HouseComponentLibrary library;
    REQUIRE(library.loadFromJson(kKit).ok());
    HouseRequest request;
    request.seed = 42;
    request.width = 5;
    request.depth = 4;
    request.floors = 2;

    HouseLayout layout;
    REQUIRE(HouseGenerator(library).generate(request, layout).ok());

    eve::procgen::Grid2D footprint;
    REQUIRE(layout.writeFootprintGrid(footprint).ok());
    eve::procgen::GridGraph gridGraph;
    REQUIRE(gridGraph.addNode("footprint", "grid.input").ok());
    REQUIRE(gridGraph.addNode("placements", "convert.grid_to_points").ok());
    REQUIRE(gridGraph.connect("footprint", "placements").ok());
    REQUIRE(gridGraph.setNodeGrid("footprint", footprint).ok());
    auto occupied = gridGraph.execute("placements");
    REQUIRE(occupied.ok());
    CHECK(std::get<eve::procgen::PointSet>(occupied.value()).getCount() > 0);

    eve::procgen::PointSet components;
    REQUIRE(layout.writeComponentPoints(components).ok());
    REQUIRE_EQ(components.getCount(), int(layout.instances.size()));
    eve::procgen::PointGraph pointGraph;
    REQUIRE(pointGraph.addNode("components", "input"));
    REQUIRE(pointGraph.addNode("raised", "transform"));
    REQUIRE(pointGraph.connect("components", "raised"));
    REQUIRE(pointGraph.setNodePoints("components", &components));
    REQUIRE(pointGraph.setNodeFloat("raised", "y", 2.f));
    auto raised = pointGraph.executeResult("raised");
    REQUIRE(raised.ok());
    CHECK_EQ(raised.value().getCount(), components.getCount());
    CHECK_EQ(raised.value().getY(0), components.getY(0) + 2.f);
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

    auto roof = std::find_if(layout.instances.begin(), layout.instances.end(), [&](const auto &instance) {
        const auto component = lib.find(instance.componentId);
        return component && component->get().category == "roof" && instance.z == r.floors;
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
      {"id":"roof","model":"roof.glb","category":"roof"}
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

TEST_CASE("housegen.rejectsInvalidDimensionsAcrossGenerationLoadAndExport") {
    HouseComponentLibrary lib;
    REQUIRE(lib.loadFromJson(kKit).ok());
    HouseGenerator generator(lib);
    HouseRequest   request;
    HouseLayout    layout;

    request.moduleSize = 0.f;
    CHECK(!generator.generate(request, layout).ok());
    request.moduleSize  = 1.f;
    request.floorHeight = std::numeric_limits<float>::infinity();
    CHECK(!generator.generate(request, layout).ok());

    CHECK(!layout
               .fromJson(R"({"moduleSize":-1,"floorHeight":3,"footprintWidth":1,
                              "footprintDepth":1,"footprintMask":"1","instances":[]})")
               .ok());
    request.floorHeight = 3.f;
    REQUIRE(generator.generate(request, layout).ok());
    layout.moduleSize = std::numeric_limits<float>::quiet_NaN();
    eve::procgen::Grid2D   grid;
    eve::procgen::PointSet points;
    CHECK(!layout.writeFootprintGrid(grid).ok());
    CHECK(!layout.writeComponentPoints(points).ok());
}

TEST_CASE("housegen.emitsEveryRequiredRoomInsideTheActiveFootprint") {
    HouseComponentLibrary lib;
    REQUIRE(lib.loadFromJson(kInteriorKit).ok());
    HouseGenerator generator(lib);
    for (const std::string footprint : {std::string("rectangle"), std::string("l_shape")}) {
        HouseRequest request;
        request.width         = 6;
        request.depth         = 5;
        request.footprint     = footprint;
        request.requiredRooms = {"living", "kitchen", "bedroom"};
        HouseLayout layout;
        REQUIRE(generator.generate(request, layout).ok());
        REQUIRE_EQ(layout.rooms.size(), size_t(3));
        std::set<std::string> roomTypes;
        for (const HouseRoom& room : layout.rooms) {
            roomTypes.insert(room.type);
            for (int y = room.y; y < room.y + room.depth; ++y)
                for (int x = room.x; x < room.x + room.width; ++x)
                    CHECK(layout.footprintMask[size_t(y * layout.footprintWidth + x)] != 0);
        }
        CHECK(roomTypes.contains("living"));
        CHECK(roomTypes.contains("kitchen"));
        CHECK(roomTypes.contains("bedroom"));
    }
}

TEST_CASE("housegen.failsInsteadOfDroppingRequiredRooms") {
    HouseComponentLibrary lib;
    REQUIRE(lib.loadFromJson(kKit).ok());
    HouseRequest request;
    request.requiredRooms = {"living", "kitchen", "bedroom"};
    HouseLayout layout;
    auto        generated = HouseGenerator(lib).generate(request, layout);
    REQUIRE(!generated.ok());
    CHECK(generated.status().describe().find("interior_wall") != std::string::npos);

    REQUIRE(lib.loadFromJson(kInteriorKit).ok());
    request.width         = 3;
    request.depth         = 3;
    request.requiredRooms = {"a", "b", "c", "d"};
    generated             = HouseGenerator(lib).generate(request, layout);
    REQUIRE(!generated.ok());
    CHECK(generated.status().describe().find("room count") != std::string::npos);
}

TEST_CASE("housegen.respectsComponentRotationConstraints") {
    constexpr char        restrictedKit[] = R"({"components":[
      {"id":"foundation","model":"foundation.glb","category":"foundation"},
      {"id":"floor","model":"floor.glb","category":"floor"},
      {"id":"wall","model":"wall.glb","category":"wall"},
      {"id":"door","model":"door.glb","category":"door"},
      {"id":"roof","model":"roof.glb","category":"roof"},
      {"id":"interior.wall","model":"wall.glb","category":"interior_wall","rotations":[180]},
      {"id":"interior.door","model":"door.glb","category":"interior_door","rotations":[180]}
    ]})";
    HouseComponentLibrary lib;
    REQUIRE(lib.loadFromJson(restrictedKit).ok());
    HouseRequest request;
    request.requiredRooms = {"living", "kitchen", "bedroom"};
    HouseLayout layout;
    auto        generated = HouseGenerator(lib).generate(request, layout);
    REQUIRE(!generated.ok());
    CHECK(generated.status().describe().find("rotation") != std::string::npos);

    REQUIRE(lib.loadFromJson(kKit).ok());
    REQUIRE(HouseGenerator(lib).generate(HouseRequest{}, layout).ok());
    const auto roof = std::find_if(layout.instances.begin(), layout.instances.end(), [&](const HouseInstance& value) {
        const auto component = lib.find(value.componentId);
        return component && component->get().category == "roof";
    });
    REQUIRE(roof != layout.instances.end());
    const_cast<HouseInstance&>(*roof).rotationDeg = 45;
    CHECK(!layout.validate(lib).ok());
}

TEST_CASE("housegen.footprintIncludesRotatedMultiCellComponents") {
    constexpr char        wideDoorKit[] = R"({"components":[
      {"id":"foundation","model":"foundation.glb","category":"foundation"},
      {"id":"floor","model":"floor.glb","category":"floor"},
      {"id":"wall","model":"wall.glb","category":"wall"},
      {"id":"door.wide","model":"door.glb","category":"door","width":3,"depth":1},
      {"id":"roof","model":"roof.glb","category":"roof"}
    ]})";
    HouseComponentLibrary lib;
    REQUIRE(lib.loadFromJson(wideDoorKit).ok());
    HouseRequest request;
    request.width    = 3;
    request.depth    = 3;
    request.entrance = "east";
    HouseLayout layout;
    REQUIRE(HouseGenerator(lib).generate(request, layout).ok());
    eve::procgen::Grid2D grid;
    REQUIRE(layout.writeFootprintGrid(grid).ok());
    CHECK_EQ(grid.getWidth(), 3);
    CHECK_EQ(grid.getHeight(), 4);
    CHECK(grid.getCell(2, 3) != int(eve::procgen::Semantic::Empty));
}
