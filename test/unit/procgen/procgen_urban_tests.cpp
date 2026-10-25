// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "procgen_road.cpp",
    "urban_generator.cpp",
    "housegen.cpp"
  ]
}
*/
// clang-format on

#include "zeroerr/unittest.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_housegen)
#include <algorithm>
#include <limits>
#include <set>
#include "PrivateTestScope.h"
#include "procgen/GridGraph.h"
#include "procgen/PointGraph.h"
#include "procgen/Semantic.h"
#include "procgen/house/HouseComponentLibrary.h"
#include "procgen/house/HouseGenerator.h"
#include "procgen/house/HouseLayout.h"
namespace test_house_generation_contract {
using namespace eve::housegen;
namespace {
constexpr const char* kit     = R"({"components":[
  {"id":"foundation","model":"foundation.glb","category":"foundation"},
  {"id":"floor","model":"floor.glb","category":"floor"},
  {"id":"wall","model":"wall.glb","category":"wall"},
  {"id":"door","model":"door.glb","category":"door"},
  {"id":"roof","model":"roof.glb","category":"roof"}]})";
constexpr const char* roomKit = R"({"components":[
  {"id":"foundation","model":"foundation.glb","category":"foundation"},
  {"id":"floor","model":"floor.glb","category":"floor"},
  {"id":"wall","model":"wall.glb","category":"wall"},
  {"id":"door","model":"door.glb","category":"door"},
  {"id":"roof","model":"roof.glb","category":"roof"},
  {"id":"interior.wall","model":"wall.glb","category":"interior_wall"},
  {"id":"interior.door","model":"door.glb","category":"interior_door"}]})";
}  // namespace

TEST_CASE("House component library validates identity model and material bounds before publication") {
    HouseComponentLibrary library;
    REQUIRE(library.loadFromJson(kit).ok());
    CHECK_EQ(library.count(), 5);
    CHECK(!library.loadFromJson(R"([{"id":"broken","category":"wall"}])").ok());
    CHECK_EQ(library.count(), 5);
    CHECK(!library.loadFromJson(R"([{"id":"bad","model":"wall.glb","category":"wall","material":{"roughness":1.2}}])")
               .ok());
    CHECK_EQ(library.count(), 5);
    REQUIRE(
        library
            .loadFromJson(
                R"([{"id":"painted","model":"wall.glb","category":"wall","material":{"baseColorTexture":"wall.png","roughness":0.86}}])")
            .ok());
    const auto painted = library.find("painted");
    REQUIRE(painted.has_value());
    CHECK_EQ(painted->get().material.baseColorTexture, std::string("wall.png"));
    CHECK(painted->get().material.hasRoughness);
}

TEST_CASE("House generation is seeded reproducible and serializable with structural validation") {
    HouseComponentLibrary library;
    REQUIRE(library.loadFromJson(kit).ok());
    HouseRequest request;
    request.seed   = 42;
    request.width  = 5;
    request.depth  = 4;
    request.floors = 2;
    HouseLayout    first, second;
    HouseGenerator generator(library);
    REQUIRE(generator.generate(request, first).ok());
    REQUIRE(generator.generate(request, second).ok());
    CHECK_EQ(first.toJson(), second.toJson());
    CHECK(first.instances.size() > 20);
    HouseLayout restored;
    REQUIRE(restored.fromJson(first.toJson()).ok());
    CHECK_EQ(restored.toJson(), first.toJson());
    CHECK(restored.validate(library).ok());
    const auto roof = std::find_if(restored.instances.begin(), restored.instances.end(), [&](const auto& instance) {
        const auto component = library.find(instance.componentId);
        return component && component->get().category == "roof" && instance.z == request.floors;
    });
    REQUIRE(roof != restored.instances.end());
    restored.instances.erase(roof);
    CHECK(!restored.validate(library).ok());
}

TEST_CASE("House request rejects invalid dimensions and missing structural or interior categories") {
    HouseComponentLibrary library;
    REQUIRE(library.loadFromJson(kit).ok());
    HouseGenerator generator(library);
    HouseRequest request;
    HouseLayout    output;
    SUB_CASE("zero module size") {
        request.moduleSize = 0;
        CHECK(!generator.generate(request, output).ok());
    };
    SUB_CASE("nonfinite floor height") {
        request.floorHeight = std::numeric_limits<float>::infinity();
        CHECK(!generator.generate(request, output).ok());
    };
    SUB_CASE("requested rooms require interior components") {
        request.requiredRooms = {"living", "kitchen"};
        CHECK(!generator.generate(request, output).ok());
    };
}

TEST_CASE("Required rooms are placed inside the active house footprint") {
    HouseComponentLibrary library;
    REQUIRE(library.loadFromJson(roomKit).ok());
    HouseRequest request;
    request.width         = 6;
    request.depth         = 5;
    request.footprint     = "l_shape";
    request.requiredRooms = {"living", "kitchen", "bedroom"};
    HouseLayout layout;
    REQUIRE(HouseGenerator(library).generate(request, layout).ok());
    REQUIRE_EQ(layout.rooms.size(), 3u);
    std::set<std::string> types;
    for (const auto& room : layout.rooms) {
        types.insert(room.type);
        for (int y = room.y; y < room.y + room.depth; ++y)
            for (int x = room.x; x < room.x + room.width; ++x)
                CHECK(layout.footprintMask[std::size_t(y * layout.footprintWidth + x)] != 0);
    }
    CHECK(types.contains("living"));
    CHECK(types.contains("kitchen"));
    CHECK(types.contains("bedroom"));
}

TEST_CASE("House layout exports footprint and component positions through graph interfaces") {
    HouseComponentLibrary library;
    REQUIRE(library.loadFromJson(kit).ok());
    HouseLayout layout;
    REQUIRE(HouseGenerator(library).generate(HouseRequest{}, layout).ok());
    eve::procgen::Grid2D footprint;
    REQUIRE(layout.writeFootprintGrid(footprint).ok());
    eve::procgen::GridGraph grid;
    REQUIRE(grid.addNode("source", "grid.input").ok());
    REQUIRE(grid.addNode("occupied", "convert.grid_to_points").ok());
    REQUIRE(grid.connect("source", "occupied").ok());
    REQUIRE(grid.setNodeGrid("source", footprint).ok());
    const auto occupied = grid.execute("occupied");
    REQUIRE(occupied.ok());
    CHECK(std::get<eve::procgen::PointSet>(occupied.value()).getCount() > 0);
    eve::procgen::PointSet components;
    REQUIRE(layout.writeComponentPoints(components).ok());
    CHECK_EQ(components.getCount(), int(layout.instances.size()));
}
}  // namespace test_house_generation_contract
#endif


#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_procgen_road)
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include "PrivateTestScope.h"
#include "image/ImageData.h"
#include "procgen/GeneratedArtifact.h"
#include "procgen/Params.h"
#include "procgen/algorithms/MarchingCubes.h"
#include "procgen/road/RoadBake.h"
#include "procgen/road/RoadNetwork.h"
#include "procgen/road/RoadRecipes.h"
#include "procgen/road/RoadTypes.h"
#include "procgen/texture/PbrMaterial.h"
#include "procgen/texture/TextureRecipe.h"
namespace test_road_generation_contract {
using namespace eve::procgen::road;

TEST_CASE("Road cross section derives physical half width from lane curb and sidewalk inputs") {
    RoadStyle style;
    style.laneWidth     = 3.5f;
    style.curbWidth     = .4f;
    style.sidewalkWidth = 1.5f;
    const auto profile  = makeRoadProfile(style, 2, 0);
    REQUIRE(profile.ok());
    CHECK(std::fabs(profile.value().halfWidth - 5.4f) < 1e-4f);
    CHECK(profile.value().points.size() >= 8u);
    CHECK_EQ(std::string(roadMaterialGroup(RoadMaterial::Asphalt)), "asphalt");
}

TEST_CASE("Road graph rejects invalid geometry and preserves edge count and revision on failure") {
    RoadNetwork network;
    const auto  a = network.addNode(0, 0, 0);
    const auto  b = network.addNode(10, 0, 0);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    const auto revision = network.revision();
    CHECK(!network.addEdge(a.value(), b.value(), {{0, 0, 0}}, 2, 0).ok());
    CHECK_EQ(network.edgeCount(), 0u);
    CHECK_EQ(network.revision(), revision);
    {
        CHECK(!network.addEdge(a.value(), a.value(), {{0, 0, 0}, {10, 0, 0}}, 1, 0).ok());
        CHECK_EQ(network.edgeCount(), 0u);
    };
    {
        const auto c = network.addNode(0, 0, 0);
        REQUIRE(c.ok());
        const auto stable = network.revision();
        CHECK(!network.addEdge(a.value(), c.value(), {{0, 0, 0}, {0, 0, 0}}, 1, 0).ok());
        CHECK_EQ(network.revision(), stable);
    };
}

TEST_CASE("Road node junction edits are revisioned only for actual valid changes") {
    RoadNetwork network;
    const auto  node = network.addNode(0, 0, 0, 4);
    REQUIRE(node.ok());
    const auto start = network.revision();
    REQUIRE(network.setNodeJunctionRadius(node.value(), 8).ok());
    CHECK_EQ(network.revision(), start + 1);
    REQUIRE(network.setNodeJunctionRadius(node.value(), 8).ok());
    CHECK_EQ(network.revision(), start + 1);
    CHECK(!network.setNodeJunctionRadius(node.value(), 0).ok());
    CHECK_EQ(network.revision(), start + 1);
    REQUIRE(network.setNodeJunctionControl(node.value(), RoadJunctionControl::Yield).ok());
    CHECK_EQ(network.revision(), start + 2);
    REQUIRE(network.setNodeJunctionControl(node.value(), RoadJunctionControl::Yield).ok());
    CHECK_EQ(network.revision(), start + 2);
}

TEST_CASE("Interchange bake produces finite grouped geometry and lane navigation overlay") {
    const auto interchange = RoadNetwork::makeInterchange(40, 7, 2, 3);
    REQUIRE(interchange.ok());
    CHECK(interchange.value().nodeCount() >= 8);
    CHECK(interchange.value().edgeCount() >= 6);
    RoadBakeOptions options;
    options.pathSegmentsPerEdge = 24;
    options.turnSamples         = 8;
    const auto baked            = bakeRoadNetwork(interchange.value(), options);
    REQUIRE(baked.ok());
    CHECK(baked.value().mesh.getVertexCount() > 100);
    CHECK(baked.value().mesh.getIndexCount() > 100);
    CHECK(!baked.value().overlay.lanes.empty());
    CHECK(!baked.value().overlay.turns.empty());
    CHECK_EQ(baked.value().mesh.getMeta("schema", ""), "eve.procgen.roadNetwork");
    for (float coordinate : baked.value().mesh.positions()) CHECK(std::isfinite(coordinate));
    bool asphalt = false, pier = false, curb = false;
    for (int group = 0; group < baked.value().mesh.getGroupCount(); ++group) {
        const auto name = baked.value().mesh.getGroupName(group);
        asphalt |= name == "asphalt";
        pier |= name == "pier";
        curb |= name == "curb";
    }
    CHECK(asphalt);
    CHECK(pier);
    CHECK(curb);
}

TEST_CASE("Road automatic turns balance lane count and preserve authored links") {
    RoadNetwork network;
    const auto  west = network.addNode(-20, 0, 0);
    const auto  hub  = network.addNode(0, 0, 0);
    const auto  east = network.addNode(20, 0, 0);
    REQUIRE(west.ok());
    REQUIRE(hub.ok());
    REQUIRE(east.ok());
    const auto incoming = network.addEdge(west.value(), hub.value(), {{-20, 0, 0}, {0, 0, 0}}, 4, 0);
    const auto outgoing = network.addEdge(hub.value(), east.value(), {{0, 0, 0}, {20, 0, 0}}, 2, 0);
    REQUIRE(incoming.ok());
    REQUIRE(outgoing.ok());
    const RoadLaneConnection authored{incoming.value(),          1, outgoing.value(), 0, RoadLaneDirection::Forward,
                                      RoadLaneDirection::Forward};
    REQUIRE(network.addLaneLink(authored).ok());
    const auto connected = network.connectAllTurns(hub.value());
    REQUIRE(connected.ok());
    CHECK_EQ(connected.value(), 3);
    CHECK_EQ(network.laneLinkCount(), 4u);
    int mapped[4] = {-1, -1, -1, -1};
    for (const auto& link : network.laneLinks()) mapped[link.inLane] = link.outLane;
    CHECK_EQ(mapped[0], 0);
    CHECK_EQ(mapped[1], 0);
    CHECK_EQ(mapped[2], 1);
    CHECK_EQ(mapped[3], 1);
    const auto stable = network.revision();
    REQUIRE(network.connectAllTurns(hub.value()).ok());
    CHECK_EQ(network.revision(), stable);
    CHECK(network.validate().ok());
}

TEST_CASE("Blocked turn remains blocked through automatic reconnection and direction reversal") {
    RoadNetwork network;
    const auto  west = network.addNode(-10, 0, 0);
    const auto  hub  = network.addNode(0, 0, 0);
    const auto  east = network.addNode(10, 0, 0);
    REQUIRE(west.ok());
    REQUIRE(hub.ok());
    REQUIRE(east.ok());
    const auto incoming = network.addEdge(west.value(), hub.value(), {{}, {}}, 1, 0);
    const auto outgoing = network.addEdge(hub.value(), east.value(), {{}, {}}, 1, 0);
    REQUIRE(incoming.ok());
    REQUIRE(outgoing.ok());
    REQUIRE(network.connectAllTurns(hub.value()).ok());
    const RoadLaneConnection turn{incoming.value(),          0, outgoing.value(), 0, RoadLaneDirection::Forward,
                                  RoadLaneDirection::Forward};
    REQUIRE(network.blockLaneLink(turn).ok());
    CHECK_EQ(network.laneLinkCount(), 0u);
    REQUIRE(network.connectAllTurns(hub.value()).ok());
    CHECK_EQ(network.laneLinkCount(), 0u);
    REQUIRE(network.reverseEdge(incoming.value()).ok());
    CHECK_EQ(network.blockedLaneLinks().front().inDirection, RoadLaneDirection::Backward);
    REQUIRE(network.reverseEdge(incoming.value()).ok());
    REQUIRE(network.unblockLaneLink(turn).ok());
    REQUIRE(network.connectAllTurns(hub.value()).ok());
    CHECK_EQ(network.laneLinkCount(), 1u);
}

TEST_CASE("Road endpoint reconnection prunes obsolete turn links and reanchors control point") {
    RoadNetwork network;
    const auto  start  = network.addNode(-12, 0, 0);
    const auto  oldHub = network.addNode(0, 0, 0);
    const auto  newHub = network.addNode(4, 1, 6);
    const auto  exit   = network.addNode(0, 0, 12);
    REQUIRE(start.ok());
    REQUIRE(oldHub.ok());
    REQUIRE(newHub.ok());
    REQUIRE(exit.ok());
    const auto road   = network.addEdge(start.value(), oldHub.value(), {{}, {}}, 1, 0);
    const auto branch = network.addEdge(oldHub.value(), exit.value(), {{}, {}}, 1, 0);
    REQUIRE(road.ok());
    REQUIRE(branch.ok());
    REQUIRE(
        network
            .addLaneLink({road.value(), 0, branch.value(), 0, RoadLaneDirection::Forward, RoadLaneDirection::Forward})
            .ok());
    const auto changed = network.reconnectEdgeEndpoint(road.value(), false, newHub.value());
    REQUIRE(changed.ok());
    const auto moved = network.edgeResult(road.value());
    REQUIRE(moved.ok());
    CHECK_EQ(moved.value().to, newHub.value());
    CHECK_EQ(moved.value().controlPoints.back().x, 4.f);
    CHECK_EQ(moved.value().controlPoints.back().y, 1.f);
    CHECK_EQ(network.laneLinkCount(), 0u);
    const auto stable = network.revision();
    CHECK(!network.reconnectEdgeEndpoint(road.value(), false, start.value()).ok());
    CHECK_EQ(network.revision(), stable);
}

TEST_CASE("Road split keeps both halves connected and migrates downstream lane links") {
    RoadNetwork network;
    const auto  a = network.addNode(-20, 0, 0);
    const auto  b = network.addNode(20, 0, 0);
    const auto  c = network.addNode(20, 0, -20);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    REQUIRE(c.ok());
    const auto road = network.addEdge(a.value(), b.value(), {{-20, 0, 0}, {0, 1, 3}, {20, 0, 0}}, 2, 1);
    const auto exit = network.addEdge(b.value(), c.value(), {{20, 0, 0}, {20, 0, -20}}, 1, 0);
    REQUIRE(road.ok());
    REQUIRE(exit.ok());
    REQUIRE(
        network.addLaneLink({road.value(), 0, exit.value(), 0, RoadLaneDirection::Forward, RoadLaneDirection::Forward})
            .ok());
    const auto split = network.splitEdge(road.value(), 1, 4);
    REQUIRE(split.ok());
    const auto first  = network.edgeResult(split.value().firstEdgeId);
    const auto second = network.edgeResult(split.value().secondEdgeId);
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    CHECK_EQ(first.value().to, split.value().nodeId);
    CHECK_EQ(second.value().from, split.value().nodeId);
    CHECK_EQ(second.value().to, b.value());
    bool migrated = false;
    for (const auto& link : network.laneLinks())
        migrated |= link.inEdge == split.value().secondEdgeId && link.outEdge == exit.value();
    CHECK(migrated);
    CHECK(network.validate().ok());
}

TEST_CASE("Road baking validates tessellation finite widths and element budget") {
    const auto road = RoadNetwork::makeStraight(12, 1);
    REQUIRE(road.ok());
    RoadBakeOptions options;
    {
        options.pathSegmentsPerEdge = 4097;
        CHECK(!bakeRoadNetwork(road.value(), options).ok());
    };
    {
        options.pathSegmentsPerEdge = 12;
        options.turnSamples         = 4097;
        CHECK(!bakeRoadNetwork(road.value(), options).ok());
    };
    {
        options.turnSamples        = 8;
        options.navRibbonHalfWidth = std::numeric_limits<float>::quiet_NaN();
        CHECK(!bakeRoadNetwork(road.value(), options).ok());
    };
    {
        options.navRibbonHalfWidth  = .22f;
        options.maximumMeshElements = 1;
        CHECK(!bakeRoadNetwork(road.value(), options).ok());
    };
}


TEST_CASE("Road recipes produce deterministic mesh dimensions and requested texture size") {
    using namespace eve::procgen;
    MeshRecipeRegistry::instance().registerBuiltins();
    TextureRecipeRegistry::instance().registerBuiltins();
    REQUIRE(MeshRecipeRegistry::instance().has("mesh.roadNetwork"));
    REQUIRE(TextureRecipeRegistry::instance().has("tex.roadMarkings"));
    Params params;
    params.setSeed(11);
    params.setFloat("span", 36);
    params.setFloat("bridgeHeight", 6);
    params.setInt("lanes", 2);
    params.setInt("pathSegments", 20);
    MeshBuild   first, second;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.roadNetwork", params, first, error));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.roadNetwork", params, second, error));
    CHECK_EQ(first.getVertexCount(), second.getVertexCount());
    CHECK_EQ(first.getIndexCount(), second.getIndexCount());
    params.setSize(128, 32);
    const auto texture = TextureRecipeRegistry::instance().generate("tex.roadMarkings", params, error);
    REQUIRE(texture != nullptr);
    CHECK_EQ(texture->getWidth(), 128);
    CHECK_EQ(texture->getHeight(), 32);
}

TEST_CASE("Road artifact publishes geometry collision topology terrain and navigation as one composite") {
    using namespace eve::procgen;
    Params params;
    params.setString("scene", "curve");
    params.setFloat("span", 24);
    params.setInt("lanes", 2);
    params.setInt("pathSegments", 12);
    params.setBool("piers", false);
    params.setBool("markings", true);
    params.setBool("navigation", true);
    params.setBool("junctions", true);
    params.setFloat("maskCellSize", 1);
    params.setFloat("terrainBlendDistance", 3);
    params.setFloat("chunkSize", 16);
    params.setInt("lodCount", 3);
    const auto id = ArtifactId::parse("018f0b7e-6e50-7a10-8c22-2c8f8e3dd071");
    REQUIRE(id.has_value());
    const auto generated = generateMeshArtifact("mesh.roadNetwork", params, *id);
    REQUIRE(generated.ok());
    CHECK_EQ(static_cast<int>(generated.value().type), static_cast<int>(ArtifactType::Composite));
    const auto& composite = std::get<CompositeArtifact>(generated.value().payload);
    const auto* mesh      = composite.find("mesh");
    const auto* collider  = composite.find("collider");
    const auto* mask      = composite.find("topology");
    const auto* terrain   = composite.find("terrain/surface-weight");
    const auto* lane      = composite.find("navigation/lane/0");
    REQUIRE(mesh != nullptr);
    REQUIRE(collider != nullptr);
    REQUIRE(mask != nullptr);
    REQUIRE(terrain != nullptr);
    REQUIRE(lane != nullptr);
    CHECK(std::get<Collider>(collider->payload).isValid());
    CHECK(std::get<Grid2D>(mask->payload).getWidth() > 0);
    CHECK_EQ(std::get<Grid2D>(terrain->payload).getMeta("semantics", ""), "road_surface_weight");
    CHECK(std::get<PointSet>(lane->payload).getCount() > 1);
    params.setFloat("terrainBlendDistance", 65);
    CHECK(!generateMeshArtifact("mesh.roadNetwork", params, *id).ok());
}
}  // namespace test_road_generation_contract
#endif


#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_urban_generator)
#include <cmath>
#include <set>
#include "PrivateTestScope.h"
#include "procgen/GeneratorRegistry.h"
#include "procgen/Grid2D.h"
#include "procgen/MeshBuild.h"
#include "procgen/Params.h"
#include "procgen/Semantic.h"
#include "procgen/urban/UrbanGenerator.h"
#include "procgen/urban/UrbanGeometry.h"
#include "procgen/urban/UrbanOutput.h"
namespace test_urban_generation_contract {
using namespace eve::procgen;
using namespace eve::procgen::urban;
namespace {
UrbanOptions rectangle(uint32_t seed = 42) {
    UrbanOptions options;
    options.land          = {{0, 0}, {100, 0}, {100, 60}, {0, 60}};
    options.seed          = seed;
    options.targetParcels = 64;
    return options;
}
Params gridParams() {
    Params params;
    params.setSeed(42);
    params.setString("land", "rect");
    params.setFloat("landWidth", 100);
    params.setFloat("landHeight", 60);
    params.setFloat("minParcelArea", 4);
    params.setInt("targetParcels", 64);
    return params;
}
bool parcelsTouchStreet(const UrbanLayout& layout) {
    std::set<std::pair<int, int>> street;
    for (const auto& segment : layout.streetSegments) street.insert(std::minmax(segment.first, segment.second));
    for (const auto& parcel : layout.parcels) {
        bool reachable = false;
        for (std::size_t i = 0; i < parcel.ring.size(); ++i)
            reachable |= street.contains(std::minmax(parcel.ring[i], parcel.ring[(i + 1) % parcel.ring.size()]));
        if (!reachable) return false;
    }
    return true;
}
}

TEST_CASE("Urban polygon geometry preserves area when a rectangle is split") {
    const Polygon square = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};
    const Polygon wide   = {{0, 0}, {20, 0}, {20, 5}, {0, 5}};
    CHECK(shapeIrregularity(square) < 1e-6);
    CHECK(shapeIrregularity(wide) > shapeIrregularity(square));
    const Polygon    land = {{0, 0}, {20, 0}, {20, 10}, {0, 10}};
    const Polyline   cut  = {{10, 0}, {10, 10}};
    BoundaryPosition from, to;
    closestPointOnBoundary(land, cut.front(), &from);
    closestPointOnBoundary(land, cut.back(), &to);
    Polygon left, right;
    REQUIRE(splitPolygonByPolyline(land, cut, from, to, left, right));
    CHECK(polygonIsSimple(left));
    CHECK(polygonIsSimple(right));
    CHECK(std::fabs(area(left) + area(right) - area(land)) < 1e-6);
}

TEST_CASE("Urban generator produces seeded parcels with a street frontage") {
    UrbanGenerator first(rectangle(7));
    UrbanGenerator second(rectangle(7));
    std::string    error;
    REQUIRE(first.generate(&error));
    REQUIRE(second.generate(&error));
    const auto& layout = first.layout();
    CHECK(layout.parcels.size() >= 32);
    CHECK(!layout.streetSegments.empty());
    CHECK(parcelsTouchStreet(layout));
    CHECK(layout.totalStreetLength > 0);
    CHECK(layout.corners == second.layout().corners);
    CHECK_EQ(layout.parcels.size(), second.layout().parcels.size());
}

TEST_CASE("Urban generator accepts irregular land and keeps parcels on streets") {
    SUB_CASE("triangular land") {
        auto options = rectangle(3);
        options.land = {{0, 0}, {100, 0}, {50, 60}};
        UrbanGenerator generator(options);
        std::string    error;
        REQUIRE(generator.generate(&error));
        CHECK(generator.layout().parcels.size() >= 16);
        CHECK(parcelsTouchStreet(generator.layout()));
    };
    SUB_CASE("L shaped land") {
        auto options = rectangle(3);
        options.land = {{0, 0}, {100, 0}, {100, 33}, {45, 33}, {45, 60}, {0, 60}};
        UrbanGenerator generator(options);
        std::string    error;
        REQUIRE(generator.generate(&error));
        CHECK(generator.layout().parcels.size() >= 16);
        CHECK(parcelsTouchStreet(generator.layout()));
    };
}

TEST_CASE("Urban grid publishes semantic cells parcel identities and deterministic output") {
    const auto  params = gridParams();
    Grid2D      first, second;
    std::string error;
    REQUIRE(generateUrbanGrid(params, first, error));
    REQUIRE(generateUrbanGrid(params, second, error));
    CHECK(first.cells() == second.cells());
    CHECK(first.getWidth() > 10);
    CHECK(first.getHeight() > 10);
    int           floor = 0, road = 0, wall = 0;
    std::set<int> parcels;
    for (int y = 0; y < first.getHeight(); ++y)
        for (int x = 0; x < first.getWidth(); ++x) {
            const auto cell = first.getCell(x, y);
            floor += cell == int(Semantic::Floor);
            road += cell == int(Semantic::Road);
            wall += cell == int(Semantic::Wall);
            if (first.getDetail(x, y) > 0) parcels.insert(first.getDetail(x, y));
        }
    CHECK(floor > 200);
    CHECK(road > 10);
    CHECK(wall > 0);
    CHECK(parcels.size() >= 32);
    CHECK_EQ(first.getMeta("algorithm", ""), "urban.parcels");
    GeneratorRegistry::instance().registerBuiltins();
    CHECK(GeneratorRegistry::instance().has("urban.parcels"));
}

TEST_CASE("Urban mesh supports flat and extruded forms and rejects bad grid parameters") {
    auto        params = gridParams();
    MeshBuild   flat;
    std::string error;
    REQUIRE(generateUrbanMesh(params, flat, error));
    CHECK(flat.getVertexCount() > 100);
    CHECK_EQ(flat.getIndexCount() % 3, 0);
    params.setFloat("extrude", 5);
    MeshBuild extruded;
    REQUIRE(generateUrbanMesh(params, extruded, error));
    CHECK(extruded.getVertexCount() > flat.getVertexCount());
    params.setFloat("minParcelArea", 0);
    Grid2D invalid;
    CHECK(!generateUrbanGrid(params, invalid, error));
    CHECK(!error.empty());
}
}  // namespace test_urban_generation_contract
#endif
