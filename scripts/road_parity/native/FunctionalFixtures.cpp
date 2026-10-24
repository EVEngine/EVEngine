#include "common/Value.h"
#include "OptionalFixtures.h"
#include "editing/EditingProperty.h"
#include "procgen/editing/RoadNetworkEditTarget.h"
#include "procgen/editing/RoadPresetDocument.h"
#include "procgen/road/RoadBake.h"
#include "procgen/road/RoadNetworkCodec.h"
#include "procgen/road/RoadRecipes.h"
#include "procgen/road/RoadTerrainWorkspace.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace eve::procgen::road;
using eve::Value;

namespace {
template <class T>
T require(eve::Result<T> result) {
    if (!result.ok()) throw std::runtime_error(result.status().describe());
    return std::move(result).takeValue();
}
void require(eve::Result<void> result) {
    if (!result.ok()) throw std::runtime_error(result.status().describe());
}
void write(const std::filesystem::path& path, const std::string& value) {
    std::ofstream output(path, std::ios::binary);
    output.exceptions(std::ios::failbit | std::ios::badbit);
    output << value;
}
RoadContinuousFacility rail() {
    RoadContinuousFacility result;
    result.id            = 11;
    result.lateralOffset = 4.f;
    result.profile       = {{-.18f, 0.f, RoadMaterial::Curb},
                            {.18f, 0.f, RoadMaterial::Curb},
                            {.18f, 1.1f, RoadMaterial::Curb},
                            {-.18f, 1.1f, RoadMaterial::Curb}};
    return result;
}
RoadFacilityPrefab fencePrefab() {
    RoadFacilityPrefab result;
    result.assetId                = "fixture.fence.middle";
    result.middleCopies           = 12;
    result.weldCompatibleVertices = true;
    auto box                      = [&](float x0, float x1, float y0, float y1, float z0, float z1) {
        const std::array<std::array<float, 3>, 8> points{{{x0, y0, z0},
                                                          {x1, y0, z0},
                                                          {x1, y1, z0},
                                                          {x0, y1, z0},
                                                          {x0, y0, z1},
                                                          {x1, y0, z1},
                                                          {x1, y1, z1},
                                                          {x0, y1, z1}}};
        const int   faces[6][4]{{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 4, 7, 3}, {1, 2, 6, 5}, {0, 1, 5, 4}, {3, 7, 6, 2}};
        const float normals[6][3]{{0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}};
        for (int f = 0; f < 6; ++f) {
            const auto base = static_cast<std::uint32_t>(result.vertices.size());
            for (int i = 0; i < 4; ++i) {
                const auto& p = points[faces[f][i]];
                result.vertices.push_back({p[0], p[1], p[2], normals[f][0], normals[f][1], normals[f][2],
                                           i == 1 || i == 2 ? 1.f : 0.f, i >= 2 ? 1.f : 0.f});
            }
            result.triangles.push_back({base, base + 1u, base + 2u, RoadMaterial::Curb});
            result.triangles.push_back({base, base + 2u, base + 3u, RoadMaterial::Curb});
        }
    };
    box(-.12f, .12f, .8f, 1.05f, 0.f, 1.f);
    box(-.09f, .09f, 0.f, 1.05f, .44f, .56f);
    result.startMesh = {"fixture.fence.start", 1.f, result.vertices, result.triangles};
    result.endMesh   = {"fixture.fence.end", 1.f, result.vertices, result.triangles};
    return result;
}
RoadNetwork fixture(const std::string& name) {
    if (name.starts_with("option-") || name.starts_with("interchange-")) return optionalFixture(name);
    if (name == "junction-tee") return require(RoadNetwork::makeTee(48.f, 2));
    if (name == "junction-cross") return require(RoadNetwork::makeCross(48.f, 2));
    if (name == "junction-roundabout") return require(RoadNetwork::makeRoundabout(48.f, 1));
    RoadNetwork network;
    auto        a = require(network.addNode(-20.f, 2.f, 0.f)), b = require(network.addNode(20.f, 2.f, 0.f));
    RoadStyle   style;
    style.laneWidth           = 3.f;
    style.automaticStructures = false;
    style.crossSection        = {{-3.f, 0.f, RoadMaterial::Asphalt}, {3.f, 0.f, RoadMaterial::Asphalt}};
    if (name == "crowned-section")
        style.crossSection = {
            {-4.f, 0.f, RoadMaterial::Asphalt}, {0.f, .45f, RoadMaterial::Asphalt}, {4.f, 0.f, RoadMaterial::Sidewalk}};
    if (name == "concave-beam") {
        style.crossSectionClosed = true;
        style.crossSection       = {{-4.f, 0.f, RoadMaterial::Asphalt}, {4.f, 0.f, RoadMaterial::Deck},
                                    {4.f, -.5f, RoadMaterial::Deck},    {1.f, -.5f, RoadMaterial::Deck},
                                    {1.f, -1.2f, RoadMaterial::Deck},   {-1.f, -1.2f, RoadMaterial::Deck},
                                    {-1.f, -.5f, RoadMaterial::Deck},   {-4.f, -.5f, RoadMaterial::Deck}};
    }
    if (name == "bridge-tunnel" || name == "terrain-multichannel" || name == "tunnel-portals")
        style.structures = {{.15f, .4f, RoadStructureKind::Bridge}, {.6f, .85f, RoadStructureKind::Tunnel}};
    if (name == "continuous-rail" || name == "custom-connection") style.facilities.push_back(rail());
    if (name == "prefab-deformation" || name == "prefab-auto-spacing") {
        auto facility   = rail();
        facility.prefab = fencePrefab();
        if (name == "prefab-auto-spacing") {
            facility.prefab.automaticSpacing = true;
            for (std::size_t t = 0; t < facility.prefab.triangles.size(); ++t)
                facility.prefab.triangles[t].smoothingGroup = static_cast<std::uint32_t>(t / 2u + 1u);
            facility.prefab.startMesh.triangles = facility.prefab.endMesh.triangles = facility.prefab.triangles;
        }
        style.facilities = {facility};
        (void)require(
            network.addEdge(a, b, {{-20.f, 2.f, 0.f}, {0.f, 4.f, 6.f, 1.2f, 15.f}, {20.f, 2.f, 0.f}}, 1, 1, style));
        return network;
    }
    if (name == "multi-arm-facilities" || name == "tee-facilities") {
        style.facilities = {rail()};
        const auto hub = require(network.addNode(0.f, 2.f, 0.f)), north = require(network.addNode(0.f, 2.f, 20.f)),
                   south     = require(network.addNode(0.f, 2.f, -20.f));
        const auto     left  = require(network.addEdge(a, hub, {{-20.f, 2.f, 0.f}, {0.f, 2.f, 0.f}}, 1, 1, style));
        const auto     right = require(network.addEdge(b, hub, {{20.f, 2.f, 0.f}, {0.f, 2.f, 0.f}}, 1, 1, style));
        const auto     upper = require(network.addEdge(hub, north, {{0.f, 2.f, 0.f}, {0.f, 2.f, 20.f}}, 1, 1, style));
        RoadConnection connection;
        connection.facilityRoutes = {{right, 11, upper, 11}};
        if (name == "multi-arm-facilities") {
            const auto lower = require(network.addEdge(hub, south, {{0.f, 2.f, 0.f}, {0.f, 2.f, -20.f}}, 1, 1, style));
            connection.facilityRoutes.push_back({left, 11, lower, 11});
        }
        require(network.setNodeConnection(hub, std::move(connection)));
        (void)require(network.connectAllTurns(hub));
        return network;
    }
    if (name != "custom-connection") {
        (void)require(network.addEdge(a, b, {{-20.f, 2.f, 0.f}, {0.f, 2.f, 0.f}, {20.f, 2.f, 0.f}}, 1, 1, style));
        return network;
    }
    auto       hub = require(network.addNode(0.f, 2.f, 0.f));
    const auto first =
        require(network.addEdge(a, hub, {{-20.f, 2.f, 0.f}, {-10.f, 2.f, 0.f}, {0.f, 2.f, 0.f}}, 1, 1, style));
    const auto second =
        require(network.addEdge(hub, b, {{0.f, 2.f, 0.f}, {10.f, 2.f, 0.f}, {20.f, 2.f, 0.f}}, 1, 1, style));
    RoadConnection connection;
    connection.assetId   = "fixture.custom-square";
    connection.vertices  = {{-3.f, 0.f, -3.f, 0.f, 1.f, 0.f, 0.f, 0.f},
                            {-3.f, 0.f, 3.f, 0.f, 1.f, 0.f, 0.f, 1.f},
                            {3.f, 0.f, 3.f, 0.f, 1.f, 0.f, 1.f, 1.f},
                            {3.f, 0.f, -3.f, 0.f, 1.f, 0.f, 1.f, 0.f}};
    connection.triangles = {{0, 1, 2, RoadMaterial::Asphalt}, {0, 2, 3, RoadMaterial::Asphalt}};
    RoadConnectionPort left;
    left.id          = 1;
    left.edgeId      = first;
    left.x           = -3.f;
    left.forwardX    = -1.f;
    left.section     = style.crossSection;
    auto right       = left;
    right.id         = 2;
    right.edgeId     = second;
    right.x          = 3.f;
    right.forwardX   = 1.f;
    connection.ports = {left, right};
    require(network.setNodeConnection(hub, std::move(connection)));
    (void)require(network.connectAllTurns(hub));
    return network;
}
Value meshValue(const eve::procgen::MeshBuild& mesh) {
    Value::Array positions, normals, uvs, indices;
    for (int i = 0; i < mesh.getVertexCount(); ++i) {
        positions.insert(positions.end(), {mesh.getPositionX(i), mesh.getPositionY(i), mesh.getPositionZ(i)});
        normals.insert(normals.end(), {mesh.getNormalX(i), mesh.getNormalY(i), mesh.getNormalZ(i)});
        uvs.insert(uvs.end(), {mesh.getUvU(i), mesh.getUvV(i)});
    }
    for (int i = 0; i < mesh.getIndexCount(); ++i) indices.emplace_back(mesh.getIndex(i));
    return Value::Object{{"positions", std::move(positions)},
                         {"normals", std::move(normals)},
                         {"uvs", std::move(uvs)},
                         {"indices", std::move(indices)}};
}
std::string tint(const std::string& group) {
    if (group == "terrain") return "0.32,0.48,0.27";
    if (group == "terrain.road") return "0.58,0.49,0.31";
    if (group.starts_with("asphalt")) return "0.25,0.29,0.35";
    if (group == "tunnel") return "0.34,0.54,0.70";
    if (group == "deck" || group == "pier") return "0.54,0.58,0.64";
    if (group == "curb") return "0.95,0.73,0.30";
    return "0.97,0.97,0.94";
}
void exportFixture(const std::filesystem::path& root, const std::string& name) {
    auto       network     = fixture(name);
    const auto destination = root / name;
    std::filesystem::create_directories(destination);
    if (name == "shared-presets") {
        using namespace eve::procgen_editing;
        RoadPresetDraft draft;
        draft.source = network;
        RoadTypePreset project{"project.urban", "", {}};
        project.overrides.laneWidth           = 3.f;
        project.overrides.curbWidth           = 0.f;
        project.overrides.curbHeight          = 0.f;
        project.overrides.sidewalkWidth       = 1.f;
        project.overrides.automaticStructures = false;
        project.pointDefaults.widthScale      = .9f;
        project.pointDefaults.rollDegrees     = 3.f;
        auto inheritedRail                    = rail();
        inheritedRail.lateralOffset           = 5.3f;
        project.overrides.facilities          = std::vector<RoadContinuousFacility>{inheritedRail};
        RoadTypePreset scene{"scene.urban", "project.urban", {}};
        scene.overrides.speedLimitMps = 10.f;
        draft.types                   = {project, scene};
        draft.bindings                = {{network.edges()[0].id, "scene.urban", {}}};
        RoadMarkerOverride marker;
        marker.markerIndex           = 1;
        marker.overrides.widthScale  = 1.4f;
        marker.overrides.rollDegrees = 0.f;
        draft.bindings[0].markers.push_back(marker);
        RoadFacilityOverrides facility;
        facility.id             = inheritedRail.id;
        facility.verticalOffset = .2f;
        facility.capStart       = false;
        draft.bindings[0].facilities.push_back(facility);
        draft.metadata                       = {{network.edges()[0].id, "Main Street", false}};
        auto document                        = require(RoadPresetDocument::create(std::move(draft)));
        auto changed                         = document->beginEdit();
        changed.types[0].overrides.laneWidth = 4.f;
        require(document->commit(require(document->prepare(std::move(changed)))));
        require(document->undo());
        const auto saved = require(require(document->capture()).toJson());
        require(document->commit(require(document->prepareRestore(require(eve::Value::fromJson(saved))))));
        require(document->redo());
        write(destination / "preset_state.json", require(require(document->capture()).toJson()));
        network = document->network();
    }
    RoadBakeOptions options;
    options.pathSegmentsPerEdge = 96;
    options.includeNavigation   = false;
    eve::procgen::Params recipe;
    recipe.setInt("pathSegments", options.pathSegmentsPerEdge);
    recipe.setBool("navigation", options.includeNavigation);
    (void)require(setRoadRecipeNetwork(recipe, network));
    write(destination / "recipe_network.json", recipe.getString("networkSnapshot", ""));
    auto baked  = require(bakeRoadNetworkRecipe(recipe));
    auto direct = require(bakeRoadNetwork(network, options));
    if (baked.mesh.positions() != direct.mesh.positions() || baked.mesh.indices() != direct.mesh.indices())
        throw std::runtime_error("recipe author snapshot differs from direct road bake");
    if (name == "terrain-multichannel" || name == "tunnel-portals") {
        RoadTerrainTile tile;
        tile.id      = "fixture.terrain";
        tile.originX = tile.originZ = -24.f;
        tile.sizeX = tile.sizeZ = 48.f;
        tile.heights            = eve::procgen::Heightmap(49, 49);
        tile.holes              = eve::procgen::Heightmap(49, 49);
        for (auto& h : tile.heights.data()) h = 1.9f;
        if (name == "tunnel-portals")
            for (int z = 0; z < 49; ++z)
                for (int x = 28; x <= 38; ++x) tile.heights.data()[static_cast<std::size_t>(z) * 49u + x] = 5.f;
        tile.splatLayers = {eve::procgen::Heightmap(49, 49), eve::procgen::Heightmap(49, 49)};
        for (auto& h : tile.splatLayers[0].data()) h = 1.f;
        tile.details = {{"fixture.grass", 25, 25, std::vector<int>(625u, 6)}};
        tile.trees   = {{"fixture.tree.ground", "oak", -15.f, 1.9f, 0.f},
                        {"fixture.tree.bridge", "oak", -9.f, 1.9f, 0.f},
                        {"fixture.tree.roof", "oak", 9.f, 1.9f, 0.f}};
        RoadTerrainWorkspace workspace;
        (void)require(workspace.initialize({tile}));
        RoadTerrainWriteOptions writeOptions;
        writeOptions.blendDistance  = 1.f;
        writeOptions.splatLayer     = 1;
        writeOptions.cutTunnelHoles = name == "tunnel-portals";
        (void)require(workspace.apply(baked.mesh, writeOptions));
        (void)require(workspace.undo());
        const auto           state      = require(workspace.capture());
        const auto           serialized = require(state.toJson());
        RoadTerrainWorkspace restored;
        (void)require(restored.restore(require(Value::fromJson(serialized))));
        (void)require(restored.redo());
        write(destination / "terrain_state.json", require(require(restored.capture()).toJson()));
        tile            = require(restored.copyTile("fixture.terrain"));
        const auto base = static_cast<std::uint32_t>(baked.mesh.getVertexCount());
        for (int z = 0; z < 49; ++z)
            for (int x = 0; x < 49; ++x)
                baked.mesh.addVertex(tile.originX + static_cast<float>(x), tile.heights.height(x, z) - .04f,
                                     tile.originZ + static_cast<float>(z), 0.f, 1.f, 0.f, static_cast<float>(x) / 48.f,
                                     static_cast<float>(z) / 48.f);
        for (int z = 0; z < 48; ++z)
            for (int x = 0; x < 48; ++x) {
                if (tile.holes.height(x, z) != 0.f || tile.holes.height(x + 1, z) != 0.f ||
                    tile.holes.height(x, z + 1) != 0.f || tile.holes.height(x + 1, z + 1) != 0.f)
                    continue;
                baked.mesh.setActiveGroup(tile.splatLayers[1].height(x, z) > .1f ? "terrain.road" : "terrain");
                const auto v = base + static_cast<std::uint32_t>(z * 49 + x);
                baked.mesh.addTriangle(v, v + 49u, v + 50u);
                baked.mesh.addTriangle(v, v + 50u, v + 1u);
            }
    }
    eve::procgen_editing::RoadNetworkEditTarget target("road:fixture", network);
    write(destination / "authoring.json", require(eve::editing::toPresentationValue(target.snapshotValue()).toJson()));
    Value::Array groups, groupNames;
    std::string  renderer =
        "dofile(\"mesh_data.nut\");\npersist road_objects=[];\npersist road_camera=null;\nfunction eve_init() {\n";
    for (int group = 0; group < baked.mesh.getGroupCount(); ++group) {
        auto mesh = baked.mesh.copyGroup(group);
        if (!mesh || mesh->getIndexCount() == 0) continue;
        const auto groupName = baked.mesh.getGroupName(group);
        const auto index     = groups.size();
        groups.push_back(meshValue(*mesh));
        groupNames.emplace_back(groupName);
        const auto lookup = "road_data[" + std::to_string(index) + "]";
        renderer += "{ local data=" + lookup +
                    "; local "
                    "mesh=gfx.newMeshFromArrays(data.positions,data.normals,data.uvs,data.positions.len()/"
                    "3,data.indices,data.indices.len()); if(mesh==null) throw \"road authoring upload failed\"; local "
                    "material=gfx.newMaterial(); material.setTint(" +
                    tint(groupName) +
                    ",1.0); material.setShadingModel(\"unlit\"); material.setReceiveLight(false); local "
                    "object=eve.Renderable3D(); object.setMesh(mesh); object.setMaterial(material); "
                    "object.setCastShadow(false); object.setReceiveShadow(false); road_objects.append(object); }\n";
    }
    renderer +=
        "road_camera=eve.Camera3D(); road_camera.setEye(32.0,18.0,32.0); road_camera.setTarget(0.0,2.0,0.0); "
        "road_camera.setUp(0.0,1.0,0.0); road_camera.setFov(42.0); road_camera.setClipPlanes(0.1,250.0); "
        "road_camera.setActive(true); gfx.setBackgroundColor(0.78,0.82,0.86,1.0); print(\"ROAD_AUTHORING_READY\\n\"); "
        "}\nfunction eve_update(dt) {}\nfunction eve_render() { gfx.clear();gfx.render3D(); }\n";
    std::string data = "road_data <- [\n";
    for (const auto& group : groups) {
        data += "{\n";
        for (const auto* key : {"positions", "normals", "uvs", "indices"})
            data += std::string(key) + " = " + require(group.getIf<Value::Object>()->at(key).toJson()) + ",\n";
        data += "},\n";
    }
    data += "];\n";
    write(destination / "mesh_data.nut", data);
    write(destination / "groups.json", require(Value(Value::Object{{"schema", "eve.tests.road-component-groups"},
                                                                   {"schemaVersion", 1},
                                                                   {"groups", std::move(groupNames)}})
                                                   .toJson()));
    write(destination / "main.nut", renderer);
    write(destination / "config.nut",
          "config <- {width=1280 height=720 title=\"Road Authoring Validation\" debug=false hotReload=false};\n");
    write(destination / "evidence.json",
          require(Value(Value::Object{{"schema", "eve.tests.road-authoring"},
                                      {"schemaVersion", 1},
                                      {"caseId", name},
                                      {"scope", "EVEngine functional candidate; Unity comparison deferred"},
                                      {"vertices", baked.mesh.getVertexCount()},
                                      {"triangles", baked.mesh.getIndexCount() / 3}})
                      .toJson()));
    std::cout << name << ": vertices=" << baked.mesh.getVertexCount() << " triangles=" << baked.mesh.getIndexCount() / 3
              << '\n';
}
}  // namespace
int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("usage: road_authoring_fixture OUTPUT_DIRECTORY");
        for (const auto& name : {"crowned-section", "concave-beam", "bridge-tunnel", "continuous-rail",
                                 "custom-connection", "prefab-deformation", "multi-arm-facilities",
                                 "terrain-multichannel", "prefab-auto-spacing", "tunnel-portals", "shared-presets",
                                 "junction-tee", "junction-cross", "junction-roundabout", "tee-facilities",
                                 "option-median", "option-streetlights", "option-roadside-decor", "option-boulevard",
                                 "interchange-grade4-angle30", "interchange-grade8-angle60",
                                 "interchange-grade12-angle90", "interchange-banked-angle45"})
            exportFixture(argv[1], name);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
