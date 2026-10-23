#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_editor_behavior_graph)
#include "PrivateTestScope.h"
#include "crowd/Crowd.h"
#include "crowd/editing/CrowdDocument.h"
#include "npc_ai/editing/BehaviorGraph.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// editor_behavior_graph.cpp
namespace test_editor_behavior_graph {
// editor_behavior_graph.cpp
using namespace eve::npc_ai_editing;
using namespace eve::editing;

namespace {
GraphNodeRecord node(const char* id, const char* type, bool input, bool output) {
    GraphNodeRecord result{GraphNodeId(id), type, EditorValue::Object{}, {}};
    if (input)
        result.pins.push_back({GraphPinId(std::string(id) + ".in"), result.id, "flow", GraphPinDirection::Input});
    if (output)
        result.pins.push_back({GraphPinId(std::string(id) + ".out"), result.id, "flow", GraphPinDirection::Output});
    return result;
}
}  // namespace

TEST_CASE("editor.behavior.graph_compiles_deterministic_tree") {
    GraphDocument       graph;
    BehaviorGraphDomain domain;
    CHECK(graph.createNode(node("root", "root", false, true)).ok());
    CHECK(graph.createNode(node("sequence", "sequence", true, true)).ok());
    CHECK(graph.createNode(node("condition", "condition", true, false)).ok());
    CHECK(graph.createNode(node("action", "action", true, false)).ok());
    CHECK(graph
              .connect(
                  {StableId("e1"), GraphPinId("root.out"), GraphPinId("sequence.in")},
                  domain.canConnect(*graph.findPin(GraphPinId("root.out")), *graph.findPin(GraphPinId("sequence.in"))))
              .ok());
    CHECK(graph
              .connect({StableId("e2"), GraphPinId("sequence.out"), GraphPinId("condition.in")},
                       domain.canConnect(*graph.findPin(GraphPinId("sequence.out")),
                                         *graph.findPin(GraphPinId("condition.in"))))
              .ok());
    CHECK(graph
              .connect({StableId("e3"), GraphPinId("sequence.out"), GraphPinId("action.in")},
                       domain.canConnect(*graph.findPin(GraphPinId("sequence.out")),
                                         *graph.findPin(GraphPinId("action.in"))))
              .ok());
    auto compiled = domain.compile(graph.snapshot(domain.domain()));
    CHECK_EQ(static_cast<int>(compiled.status), static_cast<int>(EditorStatus::Applied));
    CHECK_EQ(compiled.root.value(), std::string("root"));
    CHECK_EQ(compiled.instructions.size(), 4U);
    CHECK_EQ(compiled.instructions[1].children.size(), 2U);
}

TEST_CASE("editor.behavior.graph_rejects_cycles_or_unreachable_nodes") {
    BehaviorGraphDomain domain;
    GraphDocumentData   graph;
    graph.domain   = "behavior";
    graph.revision = 7;
    graph.nodes    = {node("root", "root", false, true), node("orphan", "action", true, false)};
    auto compiled  = domain.compile(graph);
    CHECK_EQ(static_cast<int>(compiled.status), static_cast<int>(EditorStatus::Failed));
    CHECK(!compiled.diagnostics.empty());
    auto wrongDirection = domain.canConnect({GraphPinId("a"), GraphNodeId("a"), "flow", GraphPinDirection::Input},
                                            {GraphPinId("b"), GraphNodeId("b"), "flow", GraphPinDirection::Output});
    CHECK(!wrongDirection.allowed);
}

// editor_crowd_document.cpp
using namespace eve::crowd_editing;
using namespace eve::editing;

namespace {
CrowdPathRecord patrolPath() {
    return {
        StableId("patrol"), "Patrol", true, {{StableId("a"), 0.0, 0.0, 0.5, 0.0}, {StableId("b"), 5.0, 0.0, 0.5, 1.0}}};
}

CrowdAgentRecord guard() {
    CrowdAgentRecord result;
    result.id        = StableId("guard");
    result.archetype = "guard";
    result.x         = 1.0;
    result.y         = 2.0;
    result.behavior  = "path";
    result.path      = StableId("patrol");
    return result;
}
}  // namespace

TEST_CASE("editor.crowd.agent_zone_path_operations_are_reversible") {
    CrowdDocumentTarget document("level-ai");
    auto                path = document.makeSetPath(patrolPath());
    REQUIRE(path.ok());
    CHECK(document.applyDomainOperation(path.value()).ok());

    auto agent = document.makeSetAgent(guard());
    REQUIRE(agent.ok());
    CHECK(document.applyDomainOperation(agent.value()).ok());
    CHECK_EQ(document.agents().size(), 1U);
    CHECK_EQ(document.paths().size(), 1U);
    CHECK_EQ(static_cast<int>(document.makeDeletePath(StableId("patrol")).code()),
             static_cast<int>(EditorStatus::Conflict));

    DomainOperation undoAgent = agent.value();
    undoAgent.type            = agent.value().inverseType;
    undoAgent.payload         = agent.value().inverse;
    CHECK(document.applyDomainOperation(undoAgent).ok());
    CHECK(document.agents().empty());
    CHECK(document.makeDeletePath(StableId("patrol")).ok());

    CrowdZoneRecord zone{
        StableId("gate"), "Gate", "sense", {{{0.0, 0.0}}, {{3.0, 0.0}}, {{3.0, 2.0}}, {{0.0, 2.0}}}, 2.0, true};
    auto setZone = document.makeSetZone(zone);
    REQUIRE(setZone.ok());
    CHECK(document.applyDomainOperation(setZone.value()).ok());
    CHECK_EQ(document.zones().front().points.size(), 4U);
}

TEST_CASE("editor.crowd.snapshot_and_overlay_are_revision_and_budget_safe") {
    CrowdDocumentTarget source("source");
    auto                path = source.makeSetPath(patrolPath());
    REQUIRE(path.ok());
    CHECK(source.applyDomainOperation(path.value()).ok());
    auto agent = source.makeSetAgent(guard());
    REQUIRE(agent.ok());
    CHECK(source.applyDomainOperation(agent.value()).ok());

    CrowdDocumentTarget loaded("loaded");
    CHECK(loaded.loadSnapshot(source.snapshotValue()).ok());
    CHECK(loaded.validate().empty());
    auto overlay = loaded.overlay();
    CHECK_EQ(static_cast<int>(overlay.status), static_cast<int>(EditorStatus::Applied));
    CHECK_EQ(overlay.revision, loaded.revision());
    CHECK_EQ(overlay.primitives.size(), 2U);
    CHECK_EQ(static_cast<int>(loaded.overlay(1).status), static_cast<int>(EditorStatus::Rejected));

    EditorValue invalid = EditorValue::Object{{"schemaVersion", int64_t{1}},
                                              {"paths", EditorValue::Array{}},
                                              {"zones", EditorValue::Array{}},
                                              {"agents", EditorValue::Array{EditorValue::Object{{"id", "lost"},
                                                                                                {"archetype", "guard"},
                                                                                                {"x", 0.0},
                                                                                                {"y", 0.0},
                                                                                                {"heading", 0.0},
                                                                                                {"radius", 0.5},
                                                                                                {"maximumSpeed", 1.0},
                                                                                                {"behavior", "path"},
                                                                                                {"path", "missing"}}}}};
    CHECK_EQ(static_cast<int>(loaded.loadSnapshot(invalid).code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(loaded.agents().size(), 1U);
}

TEST_CASE("editor.crowd.document_applies_to_named_runtime_agents") {
    CrowdDocumentTarget document("runtime-source");
    auto                path = document.makeSetPath(patrolPath());
    REQUIRE(path.ok());
    CHECK(document.applyDomainOperation(path.value()).ok());
    auto agent = document.makeSetAgent(guard());
    REQUIRE(agent.ok());
    CHECK(document.applyDomainOperation(agent.value()).ok());
    eve::crowd::Crowd   runtime;
    CrowdRuntimeApplier applier;
    CHECK(applier.apply(document, &runtime).ok());
    CHECK_EQ(runtime.getAgentCount(), 1);
    CHECK(runtime.hasNamedAgent("guard"));
    const int index = runtime.getNamedAgentIndex("guard");
    CHECK_EQ(runtime.getAgentAction(index), std::string("seek"));
}
}  // namespace test_editor_behavior_graph
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_editor_biome_rules_editor)
#include "PrivateTestScope.h"
#include <cmath>
#include <map>
#include "common/Capability.h"
#include "common/EditorAutomation.h"
#include "editor/Editor.h"
#include "editor/EditorWorkspace.h"
#include "procgen/PointSet.h"
#include "procgen/SpatialData.h"
#include "procgen/biome/editing/BiomeTarget.h"
#include "procgen/biome/editor/BiomeEditorModule.h"
#include "procgen/biome/editor/BiomeRulesEditor.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// editor_biome_rules_editor.cpp
namespace test_editor_biome_rules_editor {
// editor_biome_rules_editor.cpp
using eve::biome_editor::BiomeRulesEditor;
using eve::editor::EditorWorkspace;

TEST_CASE("editor.biome.rules_editor_installs_workspace_and_previews_deterministically") {
    BiomeRulesEditor editor("preview.forest");
    EditorWorkspace  workspace("biome.preview", "Biome Editor");
    REQUIRE(editor.configureWorkspace(workspace).ok());
    CHECK_EQ(workspace.getPanelCount(), 4);
    CHECK_EQ(workspace.getPanelId(0), std::string("biome.layers"));
    CHECK_EQ(workspace.getPanelCapability(0), std::string("biome.rules"));
    CHECK_EQ(workspace.getPanelContext(0), std::string("list"));
    CHECK_EQ(workspace.getPanelContext(1), std::string("preview"));
    CHECK_EQ(workspace.getPanelContext(2), std::string("inspector"));
    CHECK_EQ(workspace.getPanelContext(3), std::string("assets"));
    CHECK(editor.pointCount() > 0);
    CHECK_EQ(editor.previewRevision(), editor.revision());
    const int first = editor.pointCount();
    REQUIRE(editor.setSeed(42).ok());
    CHECK_EQ(editor.pointCount(), first);

    REQUIRE(editor.selectLayer("forest").ok());
    const float density = editor.layerDensity(0);
    REQUIRE(editor.setLayerDensity(0.5).ok());
    CHECK(std::abs(editor.layerDensity(0) - 0.5f) < 0.0001f);
    REQUIRE(editor.undo().ok());
    CHECK_EQ(editor.layerDensity(0), density);
}

TEST_CASE("editor.biome.rules_editor_exclusions_reduce_points_and_reject_invalid_density") {
    BiomeRulesEditor editor("preview.forest");
    REQUIRE(editor.selectLayer("forest").ok());
    const int before = editor.pointCount();
    REQUIRE(editor.addExclusion("asset://preview/clearing.spatial").ok());
    CHECK_EQ(editor.exclusionCount(), 1);
    CHECK(editor.pointCount() < before);
    REQUIRE(editor.undo().ok());
    CHECK_EQ(editor.exclusionCount(), 0);
    CHECK_EQ(editor.pointCount(), before);

    const auto revision = editor.previewRevision();
    auto       rejected = editor.setLayerDensity(2.0);
    CHECK(!rejected.ok());
    CHECK_EQ(editor.previewRevision(), revision);
    CHECK_EQ(editor.pointCount(), before);
}

// editor_biome_target.cpp
using namespace eve::biome_editing;
using namespace eve::editing;
namespace {
SelectionSnapshot select(const BiomeDocumentTarget& target, const ObjectId& id, const std::string& type) {
    SelectionSnapshot selection;
    selection.channel = "biome";
    selection.items.push_back({SelectionDomain::Asset, TargetId(target.targetId()), StableId(id.value()), type});
    return selection;
}
void apply(BiomeDocumentTarget& target, Result<DomainOperation> operation) {
    REQUIRE(operation.ok());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
}
class SpatialResolver final : public IBiomeSpatialResolver {
public:
    Result<eve::procgen::SpatialData*> resolve(const std::string& asset) const override {
        const auto found = assets.find(asset);
        if (found == assets.end())
            return eve::editing::failed<eve::procgen::SpatialData*>(
                EditorStatus::NotFound, RuleId("test.biome.spatial"), "missing spatial asset");
        return eve::editing::applied<eve::procgen::SpatialData*>(found->second);
    }
    std::map<std::string, eve::procgen::SpatialData*> assets;
};
}  // namespace

TEST_CASE("editor.biome.properties_are_reversible_and_snapshot_load_is_atomic") {
    BiomeDocumentTarget target("world.biome");
    BiomeLayerValue     layer{ObjectId("forest"), "forest", "areas/forest.spatial", 1, 1.f, {}};
    apply(target, target.makeCreateLayer(layer));
    apply(target, target.makeCreateAsset(layer.id, {ObjectId("oak"), "models/oak.prefab", 2.f, .8f, 1.2f, true}));
    const auto selection = select(target, ObjectId("oak"), "biome.asset");
    const auto scale =
        target.makeSet(selection, PropertyPath("asset.scale"), EditorValue::Array{.5, 1.5}, PropertySetMode::Absolute);
    REQUIRE(scale.ok());
    REQUIRE(target.applyDomainOperation(scale.value()).ok());
    CHECK_EQ(target.layers()[0].assets[0].minScale, .5f);
    DomainOperation undo = scale.value();
    undo.payload         = scale.value().inverse;
    REQUIRE(target.applyDomainOperation(undo).ok());
    CHECK_EQ(target.layers()[0].assets[0].minScale, .8f);

    const EditorValue before  = target.snapshotValue();
    EditorValue       invalid = before;
    auto*             root    = invalid.getIf<EditorValue::Object>();
    auto*             content = (*root)["content"].getIf<EditorValue::Object>();
    auto*             layers  = (*content)["layers"].getIf<EditorValue::Array>();
    auto*             first   = (*layers)[0].getIf<EditorValue::Object>();
    (*first)["density"]       = 2.0;
    CHECK_EQ(static_cast<int>(target.loadSnapshot(invalid).code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(target.snapshotValue(), before);
}

TEST_CASE("editor.biome.runtime_publishes_candidates_and_previews_deterministically") {
    BiomeDocumentTarget target("world.biome");
    apply(target, target.makeCreateLayer({ObjectId("forest"), "forest", "areas/forest.spatial", 1, 1.f, {}}));
    apply(target,
          target.makeCreateAsset(ObjectId("forest"), {ObjectId("oak"), "models/oak.prefab", 1.f, 1.f, 1.f, false}));
    eve::procgen::SpatialData domain = eve::procgen::SpatialData::box(0.f, 0.f, 0.f, 4.f, 0.f, 4.f);
    SpatialResolver           resolver;
    resolver.assets["areas/forest.spatial"] = &domain;
    BiomeDocumentRuntime runtime;
    REQUIRE(runtime.publish(target, resolver).ok());
    const Revision published = runtime.revision();
    auto           first     = runtime.preview(&domain, 2.f, 42, 0.f, published);
    auto           second    = runtime.preview(&domain, 2.f, 42, 0.f, published);
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    REQUIRE(bool(first.value()));
    REQUIRE(bool(second.value()));
    CHECK_EQ(first.value()->getCount(), second.value()->getCount());
    for (int i = 0; i < first.value()->getCount(); ++i) {
        CHECK_EQ(first.value()->getStringAttribute(i, "asset", ""), second.value()->getStringAttribute(i, "asset", ""));
        CHECK_EQ(first.value()->getPointSeed(i), second.value()->getPointSeed(i));
    }
    CHECK_EQ(static_cast<int>(runtime.preview(&domain, 2.f, 42, 0.f, published + 1).code()),
             static_cast<int>(EditorStatus::Conflict));
    SpatialResolver missing;
    CHECK_EQ(static_cast<int>(runtime.publish(target, missing).code()), static_cast<int>(EditorStatus::NotFound));
    CHECK_EQ(runtime.revision(), published);
}

TEST_CASE("editor.biome.adapter_registers_commands_and_owns_automation_targets") {
    using eve::editor::Editor;
    Editor                               editor;
    eve::biome_editor::BiomeEditorModule adapter;
    auto*                                automation = eve::cap::query<eve::IEditorAutomation>();
    REQUIRE(automation != nullptr);

    const std::string commands = automation->invoke("commands", "{}");
    CHECK(commands.find("biome.layer.create.v1") != std::string::npos);
    CHECK(commands.find("biome.asset.create.v1") != std::string::npos);
    CHECK(commands.find("biome.property.set.v1") != std::string::npos);

    const std::string created = automation->invoke(
        "target-create",
        R"({"target":"agent.biome","type":"biome","layer":"forest","spatial":"areas/forest.spatial"})");
    REQUIRE(created.find("\"status\":\"applied\"") != std::string::npos);

    const std::string asset = automation->invoke(
        "execute",
        R"({"target":"agent.biome","command":"biome.asset.create.v1","payload":{"layer":"forest","id":"oak","asset":"models/oak.prefab","weight":2,"minScale":0.8,"maxScale":1.2}})");
    REQUIRE(asset.find("\"status\":\"applied\"") != std::string::npos);

    const std::string density = automation->invoke(
        "execute",
        R"({"target":"agent.biome","command":"biome.property.set.v1","payload":{"item":"forest","type":"biome.layer","path":"layer.density","value":0.5}})");
    REQUIRE(density.find("\"status\":\"applied\"") != std::string::npos);

    const std::string inspected = automation->invoke("inspect", R"({"target":"agent.biome"})");
    CHECK(inspected.find("forest") != std::string::npos);
    CHECK(inspected.find("oak") != std::string::npos);
    CHECK(inspected.find("0.5") != std::string::npos);

    const std::string undone = automation->invoke("undo", R"({"target":"agent.biome"})");
    CHECK(undone.find("\"status\":\"applied\"") != std::string::npos);
    const std::string afterUndo = automation->invoke("inspect", R"({"target":"agent.biome"})");
    CHECK(afterUndo.find("0.5") == std::string::npos);

    const std::string closed = automation->invoke("target-close", R"({"target":"agent.biome"})");
    CHECK(closed.find("\"status\":\"applied\"") != std::string::npos);
}
}  // namespace test_editor_biome_rules_editor
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_editor_building_target)
#include "PrivateTestScope.h"
#include "building/BuildingDef.h"
#include "building/BuildingSnapshot.h"
#include "building/PlacementSystem.h"
#include "building/PlacementWorld.h"
#include "building/editing/BuildingTarget.h"
#include "editing/EditingAuthority.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// editor_building_target.cpp
namespace test_editor_building_target {
using namespace eve::building_editing;

namespace {

void registerHouse() {
    eve::building::BuildingDefinition definition;
    definition.id           = "house";
    definition.displayName  = "House";
    definition.footprintW   = 2;
    definition.footprintH   = 2;
    definition.rotationMode = "cardinal";
    definition.tags         = {"building", "home"};
    eve::building::BuildingRegistry::registerBuilding(definition);
}

void registerEdges() {
    eve::building::BuildingDefinition fence;
    fence.id            = "editor-fence";
    fence.displayName   = "Fence";
    fence.placementKind = "edge";
    fence.channel       = "boundary";
    fence.tags          = {"building", "fence"};
    eve::building::BuildingRegistry::registerBuilding(fence);
    eve::building::BuildingDefinition gate = fence;
    gate.id                                = "editor-gate";
    gate.displayName                       = "Gate";
    gate.tags                              = {"building", "gate"};
    eve::building::BuildingRegistry::registerBuilding(gate);
}

void registerAreaTile() {
    eve::building::BuildingDefinition tile;
    tile.id          = "editor-area-tile";
    tile.displayName = "Area Tile";
    tile.footprintW  = 1;
    tile.footprintH  = 1;
    tile.channel     = "floor";
    tile.tags        = {"building", "floor"};
    eve::building::BuildingRegistry::registerBuilding(tile);
}

void registerCorners() {
    eve::building::BuildingDefinition post;
    post.id            = "editor-post";
    post.placementKind = "corner";
    post.channel       = "posts";
    post.tags          = {"building", "post"};
    eve::building::BuildingRegistry::registerBuilding(post);
    eve::building::BuildingDefinition reinforced = post;
    reinforced.id                                = "editor-reinforced-post";
    reinforced.tags                              = {"building", "post", "reinforced"};
    eve::building::BuildingRegistry::registerBuilding(reinforced);
}

void registerFreeProps() {
    eve::building::BuildingDefinition prop;
    prop.id                       = "editor-free-prop";
    prop.placementKind            = "free";
    prop.snapMode                 = "free";
    prop.rotationMode             = "free";
    prop.channel                  = "decor";
    prop.freeRadiusCells          = 0.2f;
    prop.freeFootprintWidthCells  = 0.8f;
    prop.freeFootprintHeightCells = 0.4f;
    prop.freeFootprintVertices    = {-0.4f, -0.2f, 0.4f, -0.2f, 0.4f, 0.2f, -0.4f, 0.2f};
    eve::building::BuildingRegistry::registerBuilding(prop);
}

DomainOperation inverse(const DomainOperation& source) {
    DomainOperation result = source;
    result.type            = source.inverseType;
    result.payload         = source.inverse;
    return result;
}

BuildingInstanceSnapshot house(int id, int x, int y) {
    BuildingInstanceSnapshot result;
    result.instanceId = id;
    result.buildingId = "house";
    result.cellX      = x;
    result.cellY      = y;
    result.worldX     = x * 10.0;
    result.worldY     = y * 10.0;
    result.tags       = {"building", "home"};
    return result;
}

}  // namespace

TEST_CASE("editor.building.preview_reports_snap_footprint_and_occupancy") {
    registerHouse();
    eve::building::PlacementWorld world(8, 8, 10.f);
    BuildingPlacementTarget       target("build-world", &world);
    const auto                    preview = target.preview("house", 21.0, 31.0, 0.0, 91.0);
    CHECK_EQ(static_cast<int>(preview.status), static_cast<int>(EditorStatus::Applied));
    CHECK_EQ(preview.snappedCellX, 2);
    CHECK_EQ(preview.snappedCellY, 3);
    CHECK_EQ(preview.normalizedRotation, 90.0);
    CHECK_EQ(preview.cells.size(), size_t{4});
    for (const BuildingFootprintCell& cell : preview.cells) {
        CHECK(cell.inBounds);
        CHECK_EQ(cell.occupant, 0);
    }

    auto place = target.makePlace(house(target.nextAvailableInstanceId(), 2, 3));
    REQUIRE(place.ok());
    REQUIRE(target.applyDomainOperation(place.value()).ok());
    const auto occupied = target.preview("house", 21.0, 31.0, 0.0, 0.0);
    CHECK_EQ(static_cast<int>(occupied.status), static_cast<int>(EditorStatus::Rejected));
    REQUIRE(!occupied.diagnostics.empty());
    CHECK(occupied.cells[0].occupant > 0);
}

TEST_CASE("editor.building.place_move_remove_are_exactly_reversible") {
    registerHouse();
    eve::building::PlacementWorld world(12, 12, 10.f);
    BuildingPlacementTarget       target("reversible-world", &world);
    BuildingInstanceSnapshot      placed = house(77, 1, 1);
    placed.level                         = 2;
    placed.properties["skin"]            = "blue";
    placed.garrison                      = {{"unit-1", "worker", {"builder"}}};
    placed.garrisonRevision              = 4;
    auto place                           = target.makePlace(placed);
    REQUIRE(place.ok());
    REQUIRE(target.applyDomainOperation(place.value()).ok());
    REQUIRE(world.hasBuilding(77));
    CHECK_EQ(world.getBuildingProp(77, "skin"), "blue");

    auto move = target.makeMove(77, 6, 5, 179.0);
    REQUIRE(move.ok());
    REQUIRE(target.applyDomainOperation(move.value()).ok());
    CHECK_EQ(world.getBuildingCellX(77), 6);
    CHECK_EQ(world.getBuildingCellY(77), 5);
    CHECK_EQ(world.getBuildingRotation(77), 180.f);
    REQUIRE(target.applyDomainOperation(inverse(move.value())).ok());
    CHECK_EQ(world.getBuildingCellX(77), 1);
    CHECK_EQ(world.getBuildingCellY(77), 1);

    auto remove = target.makeRemove(77);
    REQUIRE(remove.ok());
    REQUIRE(target.applyDomainOperation(remove.value()).ok());
    CHECK(!world.hasBuilding(77));
    REQUIRE(target.applyDomainOperation(inverse(remove.value())).ok());
    REQUIRE(world.hasBuilding(77));
    const auto restored = target.instance(77);
    REQUIRE(restored.ok());
    CHECK_EQ(restored.value().properties.at("skin"), "blue");
    CHECK_EQ(restored.value().garrison.size(), size_t{1});
    CHECK_EQ(restored.value().garrison[0].id, "unit-1");
    CHECK_EQ(restored.value().garrisonRevision, uint64_t{4});
    CHECK_EQ(restored.value().level, 2);
    CHECK_EQ(world.getOccupantAtLevel("", 1, 1, 2), 77);
}

TEST_CASE("editor.building.undo_restore_conflicts_instead_of_overwriting_occupancy") {
    registerHouse();
    eve::building::PlacementWorld world(8, 8, 10.f);
    BuildingPlacementTarget       target("conflict-world", &world);
    auto                          first = target.makePlace(house(10, 1, 1));
    REQUIRE(first.ok());
    REQUIRE(target.applyDomainOperation(first.value()).ok());
    auto remove = target.makeRemove(10);
    REQUIRE(remove.ok());
    REQUIRE(target.applyDomainOperation(remove.value()).ok());
    auto replacement = target.makePlace(house(11, 1, 1));
    REQUIRE(replacement.ok());
    REQUIRE(target.applyDomainOperation(replacement.value()).ok());
    const auto conflict = target.applyDomainOperation(inverse(remove.value()));
    CHECK_EQ(static_cast<int>(conflict.code()), static_cast<int>(EditorStatus::Rejected));
    CHECK(!world.hasBuilding(10));
    CHECK(world.hasBuilding(11));
}

TEST_CASE("editor.building.snapshot_is_deterministic_and_complete") {
    registerHouse();
    eve::building::PlacementWorld world(8, 8, 10.f);
    world.setId("settlement");
    BuildingPlacementTarget target("snapshot-world", &world);
    auto                    first  = target.makePlace(house(3, 0, 0));
    auto                    second = target.makePlace(house(9, 4, 4));
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    REQUIRE(target.applyDomainOperation(first.value()).ok());
    REQUIRE(target.applyDomainOperation(second.value()).ok());
    const EditorValue snapshot = target.snapshotValue();
    const auto*       root     = snapshot.getIf<EditorValue::Object>();
    REQUIRE(root);
    CHECK_EQ(*root->at("schemaVersion").getIf<int64_t>(), int64_t{8});
    CHECK_EQ(*root->at("worldId").getIf<std::string>(), "settlement");
    const auto* instances = root->at("instances").getIf<EditorValue::Array>();
    REQUIRE(instances);
    CHECK_EQ(instances->size(), size_t{2});
    const auto* curveGroups = root->at("edgeCurveGroups").getIf<EditorValue::Array>();
    REQUIRE(curveGroups);
    CHECK(curveGroups->empty());
    CHECK_EQ(*instances->at(0).getIf<EditorValue::Object>()->at("instanceId").getIf<int64_t>(), int64_t{3});
    CHECK_EQ(*instances->at(1).getIf<EditorValue::Object>()->at("instanceId").getIf<int64_t>(), int64_t{9});
}

TEST_CASE("editor.building.corner_place_move_replace_and_undo_preserve_vertex_domain") {
    registerCorners();
    eve::building::PlacementWorld world(8, 8, 10.f);
    BuildingPlacementTarget       target("corner-world", &world);
    BuildingInstanceSnapshot      post;
    post.instanceId    = 81;
    post.buildingId    = "editor-post";
    post.placementKind = "corner";
    post.cornerX       = 2;
    post.cornerY       = 3;
    post.cellX         = 2;
    post.cellY         = 3;
    post.worldX        = 20.0;
    post.worldY        = 30.0;
    post.channel       = "posts";
    post.tags          = {"building", "post"};

    auto place = target.makePlace(post);
    REQUIRE(place.ok());
    REQUIRE(target.applyDomainOperation(place.value()).ok());
    CHECK_EQ(world.getCornerOccupant("posts", 2, 3), 81);

    auto move = target.makeMove(81, 5, 6, 0.0);
    REQUIRE(move.ok());
    REQUIRE(target.applyDomainOperation(move.value()).ok());
    CHECK_EQ(world.getCornerOccupant("posts", 5, 6), 81);
    CHECK_EQ(world.getCornerOccupant("posts", 2, 3), 0);
    REQUIRE(target.applyDomainOperation(inverse(move.value())).ok());
    CHECK_EQ(world.getCornerOccupant("posts", 2, 3), 81);

    auto replace = target.makeReplace(81, "editor-reinforced-post");
    REQUIRE(replace.ok());
    REQUIRE(target.applyDomainOperation(replace.value()).ok());
    CHECK_EQ(world.getBuildingId(81), "editor-reinforced-post");
    REQUIRE(target.applyDomainOperation(inverse(replace.value())).ok());
    CHECK_EQ(world.getBuildingId(81), "editor-post");
    const auto restored = target.instance(81);
    REQUIRE(restored.ok());
    CHECK_EQ(restored.value().placementKind, "corner");
    CHECK_EQ(restored.value().cornerX, 2);
    CHECK_EQ(restored.value().cornerY, 3);
}

TEST_CASE("editor.building.free_snapshot_move_and_undo_preserve_exact_anchor") {
    registerFreeProps();
    eve::building::PlacementWorld world(8, 8, 10.f);
    BuildingPlacementTarget       target("free-world", &world);
    BuildingInstanceSnapshot      prop;
    prop.instanceId            = 91;
    prop.buildingId            = "editor-free-prop";
    prop.placementKind         = "free";
    prop.worldX                = 12.25;
    prop.worldY                = 17.75;
    prop.elevation             = 0.5;
    prop.rotationDegrees       = 23.0;
    prop.channel               = "decor";
    prop.freeRadius            = 2.0;
    prop.freeHalfWidth         = 4.0;
    prop.freeHalfHeight        = 2.0;
    prop.freeFootprintVertices = {-4.0, -2.0, 4.0, -2.0, 4.0, 2.0, -4.0, 2.0};
    auto place                 = target.makePlace(prop);
    REQUIRE(place.ok());
    REQUIRE(target.applyDomainOperation(place.value()).ok());
    CHECK_EQ(world.getBuildingWorldX(91), 12.25f);

    auto move = target.makeMoveFree(91, 43.125, 51.875, 2.5, 71.0);
    REQUIRE(move.ok());
    REQUIRE(target.applyDomainOperation(move.value()).ok());
    CHECK_EQ(world.getBuildingWorldX(91), 43.125f);
    CHECK_EQ(world.getBuildingElevation(91), 2.5f);
    CHECK_EQ(world.getBuildingRotation(91), 71.f);
    REQUIRE(target.applyDomainOperation(inverse(move.value())).ok());
    const auto restored = target.instance(91);
    REQUIRE(restored.ok());
    CHECK_EQ(restored.value().placementKind, "free");
    CHECK_EQ(restored.value().worldX, 12.25);
    CHECK_EQ(restored.value().worldY, 17.75);
    CHECK_EQ(restored.value().freeRadius, 2.0);
    CHECK_EQ(restored.value().freeHalfWidth, 4.0);
    CHECK_EQ(restored.value().freeHalfHeight, 2.0);
    CHECK_EQ(restored.value().freeFootprintVertices.size(), size_t{8});
    CHECK_EQ(restored.value().freeFootprintVertices[0], -4.0);
}

TEST_CASE("editor.building.replace_commits_undoes_and_redoes_through_authority") {
    registerHouse();
    eve::building::BuildingDefinition upgraded;
    upgraded.id           = "upgraded-house";
    upgraded.displayName  = "Upgraded House";
    upgraded.footprintW   = 2;
    upgraded.footprintH   = 2;
    upgraded.rotationMode = "cardinal";
    upgraded.tags         = {"building", "upgraded"};
    eve::building::BuildingRegistry::registerBuilding(upgraded);

    eve::building::PlacementWorld world(10, 10, 10.f);
    BuildingPlacementTarget       target("replace-world", &world);
    auto                          place = target.makePlace(house(42, 2, 2));
    REQUIRE(place.ok());
    REQUIRE(target.applyDomainOperation(place.value()).ok());
    world.setBuildingProp(42, "owner", "player");

    auto replacement = target.makeReplace(42, upgraded.id);
    REQUIRE(replacement.ok());
    eve::editing::LocalWorldAuthority authority(&target);
    eve::editing::TransactionSpec     transaction;
    transaction.id           = eve::editing::TransactionId("building.replace.1");
    transaction.label        = "Replace building";
    transaction.target       = target.targetId();
    transaction.baseRevision = target.revision();
    const std::vector<DomainOperation> operations{replacement.value()};
    auto                               plan = authority.preflight(transaction, operations);
    REQUIRE(plan.ok());
    eve::building::PlacementSystem::clearEvents();
    auto committed = authority.commit(plan.value());
    REQUIRE(committed.ok());
    CHECK_EQ(world.getBuildingId(42), upgraded.id);
    CHECK_EQ(world.getBuildingProp(42, "owner"), "player");
    const size_t committedEventCount = eve::building::PlacementSystem::events().size();

    auto undone = authority.compensate(committed.value());
    REQUIRE(undone.ok());
    CHECK_EQ(world.getBuildingId(42), "house");
    CHECK_EQ(world.getBuildingProp(42, "owner"), "player");
    CHECK_EQ(eve::building::PlacementSystem::events().size(), committedEventCount);

    transaction.id           = eve::editing::TransactionId("building.replace.2");
    transaction.baseRevision = target.revision();
    auto redoPlan            = authority.preflight(transaction, operations);
    REQUIRE(redoPlan.ok());
    auto redone = authority.commit(redoPlan.value());
    REQUIRE(redone.ok());
    CHECK_EQ(world.getBuildingId(42), upgraded.id);
}

TEST_CASE("editor.building.edge_replace_and_undo_preserve_canonical_address") {
    registerEdges();
    eve::building::PlacementWorld world(8, 8, 10.f);
    BuildingPlacementTarget       target("edge-replace-world", &world);
    BuildingInstanceSnapshot      edge;
    edge.instanceId          = 51;
    edge.buildingId          = "editor-fence";
    edge.placementKind       = "edge";
    edge.edgeX               = 3;
    edge.edgeY               = 4;
    edge.edgeAxis            = "vertical";
    edge.cellX               = 3;
    edge.cellY               = 4;
    edge.worldX              = 30.0;
    edge.worldY              = 45.0;
    edge.rotationDegrees     = 90.0;
    edge.channel             = "boundary";
    edge.properties["owner"] = "player";
    auto place               = target.makePlace(edge);
    REQUIRE(place.ok());
    REQUIRE(target.applyDomainOperation(place.value()).ok());

    auto replacement = target.makeReplace(51, "editor-gate");
    REQUIRE(replacement.ok());
    eve::editing::LocalWorldAuthority authority(&target);
    eve::editing::TransactionSpec     transaction;
    transaction.id           = eve::editing::TransactionId("building.edge.replace.1");
    transaction.label        = "Replace edge building";
    transaction.target       = target.targetId();
    transaction.baseRevision = target.revision();
    const std::vector<DomainOperation> operations{replacement.value()};
    auto                               plan = authority.preflight(transaction, operations);
    REQUIRE(plan.ok());
    auto committed = authority.commit(plan.value());
    REQUIRE(committed.ok());
    CHECK_EQ(world.getBuildingId(51), "editor-gate");
    auto replaced = target.instance(51);
    REQUIRE(replaced.ok());
    CHECK_EQ(replaced.value().edgeX, 3);
    CHECK_EQ(replaced.value().edgeY, 4);
    CHECK_EQ(replaced.value().edgeAxis, "vertical");
    CHECK_EQ(replaced.value().properties.at("owner"), "player");

    REQUIRE(authority.compensate(committed.value()).ok());
    CHECK_EQ(world.getBuildingId(51), "editor-fence");
    auto restored = target.instance(51);
    REQUIRE(restored.ok());
    CHECK_EQ(restored.value().edgeAxis, "vertical");
    CHECK_EQ(restored.value().properties.at("owner"), "player");
}

TEST_CASE("editor.building.surface_attachment_survives_replace_and_undo") {
    registerHouse();
    eve::building::BuildingDefinition upgraded = *eve::building::BuildingRegistry::find("house");
    upgraded.id                                = "surface-upgraded-house";
    eve::building::BuildingRegistry::registerBuilding(upgraded);
    eve::building::PlacementWorld world(8, 8, 10.f);
    BuildingPlacementTarget       target("surface-replace-world", &world);
    BuildingInstanceSnapshot      placed = house(61, 2, 2);
    placed.surfaceId                     = "terrain/main";
    placed.surfaceRevision               = 27;
    placed.surfaceNormalX                = 0.25;
    placed.surfaceNormalY                = 0.95;
    placed.surfaceNormalZ                = 0.1;
    placed.surfaceTangentX               = 0.9;
    placed.surfaceTangentY               = -0.2;
    placed.surfaceTangentZ               = 0.3;
    placed.surfaceSampleCount            = 4;
    placed.surfaceMaxSlopeDegrees        = 14.0;
    placed.surfaceHeightDelta            = 0.6;
    auto place                           = target.makePlace(placed);
    REQUIRE(place.ok());
    REQUIRE(target.applyDomainOperation(place.value()).ok());
    auto replacement = target.makeReplace(61, upgraded.id);
    REQUIRE(replacement.ok());
    REQUIRE(target.applyDomainOperation(replacement.value()).ok());
    REQUIRE(target.applyDomainOperation(inverse(replacement.value())).ok());
    const auto restored = target.instance(61);
    REQUIRE(restored.ok());
    CHECK_EQ(restored.value().surfaceId, "terrain/main");
    CHECK_EQ(restored.value().surfaceRevision, uint64_t{27});
    CHECK_EQ(restored.value().surfaceSampleCount, 4);
    CHECK_EQ(restored.value().surfaceMaxSlopeDegrees, 14.0);
    CHECK_EQ(restored.value().surfaceHeightDelta, 0.6);
}

TEST_CASE("editor.building.set_preflights_final_definition_and_pose_atomically") {
    registerHouse();
    eve::building::BuildingDefinition upgraded = *eve::building::BuildingRegistry::find("house");
    upgraded.id                                = "atomic-upgraded-house";
    eve::building::BuildingRegistry::registerBuilding(upgraded);
    eve::building::PlacementWorld world(8, 8, 10.f);
    BuildingPlacementTarget       target("atomic-set-world", &world);
    auto                          source  = target.makePlace(house(71, 0, 0));
    auto                          blocker = target.makePlace(house(72, 4, 4));
    REQUIRE(source.ok());
    REQUIRE(blocker.ok());
    REQUIRE(target.applyDomainOperation(source.value()).ok());
    REQUIRE(target.applyDomainOperation(blocker.value()).ok());
    auto replacement = target.makeReplace(71, upgraded.id);
    REQUIRE(replacement.ok());
    DomainOperation conflicting = replacement.value();
    auto*           payload     = conflicting.payload.getIf<EditorValue::Object>();
    REQUIRE(payload);
    (*payload)["cellX"] = int64_t{4};
    (*payload)["cellY"] = int64_t{4};
    const auto rejected = target.applyDomainOperation(conflicting);
    CHECK_EQ(static_cast<int>(rejected.code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(world.getBuildingId(71), "house");
    CHECK_EQ(world.getBuildingCellX(71), 0);
    CHECK_EQ(world.getBuildingCellY(71), 0);
}

TEST_CASE("editor.building.rectangle_is_one_atomic_undoable_gesture") {
    registerAreaTile();
    eve::building::PlacementWorld world(8, 8, 10.f);
    BuildingPlacementTarget       target("area-rectangle-world", &world);
    auto                          rectangle = target.makeRectangle("editor-area-tile", 1, 2, 3, 3, 0.0);
    REQUIRE(rectangle.ok());
    CHECK_EQ(rectangle.value().affectedObjects.size(), size_t{6});
    eve::editing::LocalWorldAuthority authority(&target);
    eve::editing::TransactionSpec     transaction;
    transaction.id           = eve::editing::TransactionId("building.area.rectangle.1");
    transaction.label        = "Paint rectangle";
    transaction.target       = target.targetId();
    transaction.baseRevision = target.revision();
    const std::vector<DomainOperation> operations{rectangle.value()};
    auto                               plan = authority.preflight(transaction, operations);
    REQUIRE(plan.ok());
    const auto revisionBefore = target.revision();
    auto       committed      = authority.commit(plan.value());
    REQUIRE(committed.ok());
    CHECK_EQ(world.getBuildingCount(), 6);
    CHECK_EQ(target.revision(), revisionBefore + 1);
    REQUIRE(authority.compensate(committed.value()).ok());
    CHECK_EQ(world.getBuildingCount(), 0);
    CHECK_EQ(target.revision(), revisionBefore + 2);
}

TEST_CASE("editor.building.edge_path_is_one_atomic_undoable_gesture") {
    registerEdges();
    eve::building::PlacementWorld world(8, 8, 10.f);
    BuildingPlacementTarget       target("edge-path-world", &world);
    auto                          path = target.makeEdgePath("editor-fence", {{1, 1}, {4, 1}, {4, 3}});
    REQUIRE(path.ok());
    CHECK_EQ(path.value().affectedObjects.size(), size_t{5});
    eve::editing::LocalWorldAuthority authority(&target);
    eve::editing::TransactionSpec     transaction;
    transaction.id           = eve::editing::TransactionId("building.edge-path.1");
    transaction.label        = "Place edge path";
    transaction.target       = target.targetId();
    transaction.baseRevision = target.revision();
    const std::vector<DomainOperation> operations{path.value()};
    auto                               plan = authority.preflight(transaction, operations);
    REQUIRE(plan.ok());
    const auto revisionBefore = target.revision();
    auto       committed      = authority.commit(plan.value());
    REQUIRE(committed.ok());
    CHECK_EQ(world.getBuildingCount(), 5);
    CHECK_EQ(target.revision(), revisionBefore + 1);
    CHECK(world.getEdgeOccupant("boundary", 3, 1, "north") > 0);
    CHECK(world.getEdgeOccupant("boundary", 4, 2, "west") > 0);
    REQUIRE(authority.compensate(committed.value()).ok());
    CHECK_EQ(world.getBuildingCount(), 0);
    CHECK_EQ(target.revision(), revisionBefore + 2);
}

TEST_CASE("editor.building.edge_curve_is_one_atomic_undoable_gesture") {
    registerEdges();
    eve::building::PlacementWorld world(10, 10, 10.f);
    BuildingPlacementTarget       target("edge-curve-world", &world);
    auto curve = target.makeEdgeCubicBezier("editor-fence", {{1.0, 1.0}, {1.0, 6.0}, {7.0, 6.0}, {7.0, 1.0}}, 24);
    REQUIRE(curve.ok());
    CHECK(curve.value().affectedObjects.size() > size_t{7});
    eve::editing::LocalWorldAuthority authority(&target);
    eve::editing::TransactionSpec     transaction;
    transaction.id           = eve::editing::TransactionId("building.edge-curve.1");
    transaction.label        = "Place cubic edge curve";
    transaction.target       = target.targetId();
    transaction.baseRevision = target.revision();
    const std::vector<DomainOperation> operations{curve.value()};
    auto                               plan = authority.preflight(transaction, operations);
    REQUIRE(plan.ok());
    const auto revisionBefore = target.revision();
    auto       committed      = authority.commit(plan.value());
    REQUIRE(committed.ok());
    CHECK_EQ(world.getBuildingCount(), static_cast<int>(curve.value().affectedObjects.size()));
    CHECK_EQ(world.getEdgeCurveGroupCount(), 1);
    const int firstMember = world.getBuildingInstanceAt(0);
    auto      group       = world.edgeCurveGroupForInstance(firstMember);
    REQUIRE(group.ok());
    CHECK_EQ(group.value().subdivisions, 24);
    CHECK_EQ(group.value().controlPoints.size(), size_t{4});
    const auto memberBeforeUndo = world.buildings().at(firstMember);
    CHECK_EQ(target.revision(), revisionBefore + 1);
    REQUIRE(authority.compensate(committed.value()).ok());
    CHECK_EQ(world.getBuildingCount(), 0);
    CHECK_EQ(world.getEdgeCurveGroupCount(), 0);
    CHECK_EQ(target.revision(), revisionBefore + 2);
    transaction.baseRevision = target.revision();
    const int blocker =
        world.placeEdge("editor-fence", memberBeforeUndo.edge.x, memberBeforeUndo.edge.y,
                        memberBeforeUndo.edge.axis == eve::building::EdgeAxis::Horizontal ? "north" : "west");
    REQUIRE(blocker > 0);
    auto rejectedRedo = authority.preflight(transaction, operations);
    CHECK(!rejectedRedo.ok());
    CHECK_EQ(world.getBuildingCount(), 1);
    CHECK_EQ(world.getEdgeCurveGroupCount(), 0);
    REQUIRE(world.removeBuilding(blocker));
    auto redoPlan = authority.preflight(transaction, operations);
    REQUIRE(redoPlan.ok());
    REQUIRE(authority.commit(redoPlan.value()).ok());
    CHECK_EQ(world.getEdgeCurveGroupCount(), 1);
    auto restoredGroup = world.edgeCurveGroupForInstance(firstMember);
    REQUIRE(restoredGroup.ok());
    CHECK_EQ(restoredGroup.value(), group.value());
    const EditorValue snapshot = target.snapshotValue();
    const auto*       root     = snapshot.getIf<EditorValue::Object>();
    REQUIRE(root);
    const auto* curveGroups = root->at("edgeCurveGroups").getIf<EditorValue::Array>();
    REQUIRE(curveGroups);
    CHECK_EQ(curveGroups->size(), size_t{1});
}

TEST_CASE("editor.building.edge_curve_handles_preview_and_update_group_atomically") {
    registerEdges();
    eve::building::PlacementWorld             world(12, 12, 10.f);
    BuildingPlacementTarget                   target("edge-curve-handle-world", &world);
    const std::vector<BuildingEdgeCurvePoint> original{{1.0, 1.0}, {1.0, 6.0}, {7.0, 6.0}, {7.0, 1.0}};
    auto                                      draft = target.previewEdgeCubicBezier("editor-fence", original, 24);
    CHECK_EQ(static_cast<int>(draft.status), static_cast<int>(EditorStatus::Applied));
    CHECK_EQ(draft.controlPoints.size(), size_t{4});
    CHECK(draft.edges.size() > size_t{7});
    auto gizmo = target.edgeCubicBezierGizmo("editor-fence", original, 24);
    CHECK_EQ(static_cast<int>(gizmo.status), static_cast<int>(EditorStatus::Applied));
    CHECK(gizmo.primitives.size() > draft.sampledVertices.size());
    CHECK_EQ(gizmo.primitives.front().id, "curve.control.0");
    CHECK_EQ(gizmo.primitives.front().kind, "sphere");

    auto initial = target.makeEdgeCubicBezier("editor-fence", original, 24);
    REQUIRE(initial.ok());
    REQUIRE(target.applyDomainOperation(initial.value()).ok());
    const int member = world.getBuildingInstanceAt(0);
    auto      before = world.edgeCurveGroupForInstance(member);
    REQUIRE(before.ok());
    const std::vector<BuildingEdgeCurvePoint> dragged{{1.0, 1.0}, {2.0, 7.0}, {8.0, 5.0}, {8.0, 1.0}};
    auto liveDraft = target.previewEdgeCubicBezier("editor-fence", dragged, 32, member);
    CHECK_EQ(static_cast<int>(liveDraft.status), static_cast<int>(EditorStatus::Applied));
    auto update = target.makeUpdateEdgeCubicBezier(member, dragged, 32);
    REQUIRE(update.ok());
    eve::editing::LocalWorldAuthority authority(&target);
    eve::editing::TransactionSpec     transaction;
    transaction.id           = eve::editing::TransactionId("building.edge-curve.drag.1");
    transaction.label        = "Drag cubic edge curve handle";
    transaction.target       = target.targetId();
    transaction.baseRevision = target.revision();
    const std::vector<DomainOperation> operations{update.value()};
    auto                               plan = authority.preflight(transaction, operations);
    REQUIRE(plan.ok());
    auto committed = authority.commit(plan.value());
    REQUIRE(committed.ok());
    CHECK_EQ(world.getEdgeCurveGroupCount(), 1);
    auto after = world.edgeCurveGroup(before.value().id);
    REQUIRE(after.ok());
    CHECK_EQ(after.value().subdivisions, 32);
    CHECK(after.value().controlPoints != before.value().controlPoints);
    REQUIRE(authority.compensate(committed.value()).ok());
    auto undone = world.edgeCurveGroup(before.value().id);
    REQUIRE(undone.ok());
    CHECK_EQ(undone.value(), before.value());
}

TEST_CASE("editor.building.surface_curve_create_preview_and_gizmo_share_frames") {
    registerEdges();
    eve::building::PlacementWorld world(12, 12, 10.f);
    world.setGridPlane("xz");
    eve::building::PlacementSystem::registerSurfaceProvider(
        "editor.curve-terrain", [](const eve::building::PlacementWorld&, float x, float y) {
            eve::building::PlacementSystem::PlacementHit hit;
            hit.worldX          = x;
            hit.worldY          = x * 0.1f;
            hit.worldZ          = y;
            hit.normalX         = -0.1f;
            hit.normalY         = 1.f;
            hit.surfaceId       = "terrain.editor";
            hit.surfaceRevision = 21;
            return eve::Result<eve::building::PlacementSystem::PlacementHit>::success(std::move(hit));
        });
    BuildingPlacementTarget                   target("surface-curve-editor-world", &world);
    const std::vector<BuildingEdgeCurvePoint> controls{{1.0, 1.0}, {1.0, 6.0}, {7.0, 6.0}, {7.0, 1.0}};
    auto preview = target.previewEdgeCubicBezier("editor-fence", controls, 16, 0, "editor.curve-terrain");
    REQUIRE(static_cast<int>(preview.status) == static_cast<int>(EditorStatus::Applied));
    CHECK_EQ(preview.surfaceProviderName, "editor.curve-terrain");
    CHECK_EQ(preview.surfaceId, "terrain.editor");
    CHECK_EQ(preview.surfaceRevision, uint64_t{21});
    CHECK_EQ(preview.surfaceSamples.size(), size_t{17});
    auto gizmo = target.edgeCubicBezierGizmo("editor-fence", controls, 16, 0, 0, "editor.curve-terrain");
    REQUIRE(static_cast<int>(gizmo.status) == static_cast<int>(EditorStatus::Applied));
    bool foundElevatedPath = false;
    for (const auto& primitive : gizmo.primitives)
        if (primitive.id.rfind("curve.path.", 0) == 0 && primitive.position[1] > 1.0) foundElevatedPath = true;
    CHECK(foundElevatedPath);

    auto operation = target.makeEdgeCubicBezierOnSurface("editor-fence", controls, 16, "editor.curve-terrain");
    REQUIRE(operation.ok());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
    REQUIRE(world.getEdgeCurveGroupCount() == 1);
    auto committed = world.edgeCurveGroupForInstance(world.getBuildingInstanceAt(0));
    REQUIRE(committed.ok());
    CHECK_EQ(committed.value().surfaceProviderName, "editor.curve-terrain");
    CHECK_EQ(committed.value().surfaceId, "terrain.editor");
    CHECK_EQ(committed.value().surfaceRevision, uint64_t{21});
    CHECK_EQ(committed.value().surfaceSamples.size(), size_t{17});

    BuildingEdgeCurveDragSession drag(&target, "editor-fence", committed.value().instanceIds.front(), controls, 16,
                                      "editor.curve-terrain");
    auto                         missed = drag.beginDrag(100.0, 100.0, 100.0, 0.0, 0.0, -1.0);
    CHECK(!missed.ok());
    CHECK(!drag.isDragging());
    auto begun = drag.beginDrag(10.0, 1.0, 30.0, 0.0, 0.0, -1.0);
    REQUIRE(begun.ok());
    CHECK(drag.isDragging());
    CHECK_EQ(drag.activeControlPointIndex(), 0);
    auto moved = drag.updateDrag(20.0, 2.0, 30.0, 0.0, 0.0, -1.0);
    REQUIRE(moved.ok());
    CHECK_EQ(static_cast<int>(moved.value().status), static_cast<int>(EditorStatus::Applied));
    CHECK(std::abs(moved.value().curve.controlPoints[0].x - 2.0) < 0.0001);
    auto draggedOperation = drag.finishDrag();
    REQUIRE(draggedOperation.ok());
    CHECK(!drag.isDragging());
    REQUIRE(target.applyDomainOperation(draggedOperation.value()).ok());
    auto afterDrag = world.edgeCurveGroup(committed.value().id);
    REQUIRE(afterDrag.ok());
    CHECK(std::abs(afterDrag.value().controlPoints[0].x - 2.f) < 0.0001f);
    CHECK_EQ(afterDrag.value().surfaceId, "terrain.editor");
    std::vector<BuildingEdgeCurvePoint> afterDragControls;
    for (const auto& point : afterDrag.value().controlPoints) afterDragControls.push_back({point.x, point.y});
    BuildingEdgeCurveDragSession staleDrag(&target, "editor-fence", afterDrag.value().instanceIds.front(),
                                           afterDragControls, 16, "editor.curve-terrain");
    REQUIRE(staleDrag.beginDrag(20.0, 2.0, 30.0, 0.0, 0.0, -1.0).ok());
    REQUIRE(target.commitDomainState(target.cloneDomainState()).ok());
    auto staleFinish = staleDrag.finishDrag();
    CHECK(!staleFinish.ok());
    CHECK_EQ(static_cast<int>(staleFinish.code()), static_cast<int>(EditorStatus::Conflict));
    CHECK(!staleDrag.isDragging());

    auto rejected = target.previewEdgeCubicBezier("editor-fence", controls, 16, 0, "editor.missing-terrain");
    CHECK_EQ(static_cast<int>(rejected.status), static_cast<int>(EditorStatus::Rejected));
    CHECK(!rejected.diagnostics.empty());
    auto rejectedGizmo = target.edgeCubicBezierGizmo("editor-fence", controls, 16, 0, 0, "editor.missing-terrain");
    CHECK_EQ(static_cast<int>(rejectedGizmo.status), static_cast<int>(EditorStatus::Rejected));
    bool foundRejectedPath = false;
    for (const auto& primitive : rejectedGizmo.primitives)
        if (primitive.id.rfind("curve.path.", 0) == 0 && primitive.color[0] == 0.95) foundRejectedPath = true;
    CHECK(foundRejectedPath);
    eve::building::PlacementSystem::unregisterSurface("editor.curve-terrain");
    eve::building::BuildingRegistry::clear();
}

TEST_CASE("editor.building.brush_conflict_discards_complete_staging_candidate") {
    registerAreaTile();
    eve::building::PlacementWorld world(9, 9, 10.f);
    BuildingPlacementTarget       target("area-conflict-world", &world);
    auto                          brush = target.makeBrush("editor-area-tile", 4, 4, 1, 0.0);
    REQUIRE(brush.ok());
    CHECK_EQ(brush.value().affectedObjects.size(), size_t{5});
    BuildingInstanceSnapshot blockerSnapshot;
    blockerSnapshot.instanceId = 99;
    blockerSnapshot.buildingId = "editor-area-tile";
    blockerSnapshot.cellX      = 4;
    blockerSnapshot.cellY      = 5;
    blockerSnapshot.worldX     = 40.0;
    blockerSnapshot.worldY     = 50.0;
    blockerSnapshot.channel    = "floor";
    auto blockerPlacement      = target.makePlace(blockerSnapshot);
    REQUIRE(blockerPlacement.ok());
    REQUIRE(target.applyDomainOperation(blockerPlacement.value()).ok());
    const int  countBefore    = world.getBuildingCount();
    const auto revisionBefore = target.revision();
    eve::building::PlacementSystem::clearEvents();
    const auto rejected = target.applyDomainOperation(brush.value());
    CHECK_EQ(static_cast<int>(rejected.code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(world.getBuildingCount(), countBefore);
    CHECK(world.hasBuilding(99));
    CHECK_EQ(target.revision(), revisionBefore);
    CHECK(eve::building::PlacementSystem::events().empty());
}
}  // namespace test_editor_building_target
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_editor_building_tool)
#include "PrivateTestScope.h"
#include <cmath>
#include <span>
#include "building/BuildingDef.h"
#include "building/PlacementWorld.h"
#include "building/editor/EditorBuildingTarget.h"
#include "editing/EditingAuthority.h"
#include "editor/EditorSession.h"
#include "zeroerr/unittest.h"
// editor_building_tool.cpp
namespace test_editor_building_tool {
namespace {

using eve::editor::BuildingEdgeCurveTool;
using eve::editor::BuildingEdgeCurveToolSelection;
using eve::editor::BuildingPlacementTarget;
using eve::editor::BuildingViewportRay;
using eve::editor::EditorPointerEvent;
using eve::editor::IBuildingViewportAdapter;
using eve::editor::IEditorOverlay;
using eve::editor::OverlayPoint;
using eve::editor::OverlayStyle;

void registerToolFence() {
    eve::building::BuildingDefinition fence;
    fence.id            = "editor-tool-fence";
    fence.displayName   = "Tool Fence";
    fence.placementKind = "edge";
    fence.channel       = "boundary";
    eve::building::BuildingRegistry::registerBuilding(fence);
}

class TestBuildingViewport final : public IBuildingViewportAdapter {
public:
    eve::editing::Result<BuildingViewportRay> pointerRay(const EditorPointerEvent& event) const override {
        BuildingViewportRay ray;
        ray.origin    = {event.x, 30.0, event.y};
        ray.direction = {0.0, -1.0, 0.0};
        return eve::editing::applied<BuildingViewportRay>(ray);
    }

    eve::editing::Result<OverlayPoint> projectWorld(const std::array<double, 3>& world) const override {
        return eve::editing::applied<OverlayPoint>(
            {static_cast<float>(world[0]), static_cast<float>(world[2]), static_cast<float>(world[1])});
    }
};

class CountingAuthority final : public eve::editing::IEditAuthority {
public:
    explicit CountingAuthority(eve::editing::IDomainOperationTarget* target) : local_(target) {}

    eve::editing::Result<eve::editing::AuthorityPlan> preflight(
        const eve::editing::TransactionSpec&           transaction,
        std::span<const eve::editing::DomainOperation> operations) override {
        ++preflights;
        return local_.preflight(transaction, operations);
    }

    eve::editing::Result<eve::editing::TransactionReceipt> commit(const eve::editing::AuthorityPlan& plan) override {
        ++commits;
        return local_.commit(plan);
    }

    eve::editing::Result<eve::editing::TransactionReceipt> compensate(
        const eve::editing::TransactionReceipt& receipt) override {
        return local_.compensate(receipt);
    }

    int preflights = 0;
    int commits    = 0;

private:
    eve::editing::LocalWorldAuthority local_;
};

class CountingOverlay final : public IEditorOverlay {
public:
    void line(const OverlayPoint&, const OverlayPoint&, const OverlayStyle&) override { ++lines; }
    void circle(const OverlayPoint&, float, const OverlayStyle&) override { ++circles; }
    void rectangle(const OverlayPoint&, const OverlayPoint&, const OverlayStyle&) override {}
    void text(const OverlayPoint&, const std::string&, const OverlayStyle&) override {}
    int  lines   = 0;
    int  circles = 0;
};

BuildingEdgeCurveToolSelection placedCurve(eve::building::PlacementWorld& world, BuildingPlacementTarget& target) {
    BuildingEdgeCurveToolSelection selection;
    selection.buildingId    = "editor-tool-fence";
    selection.controlPoints = {{1.0, 1.0}, {1.0, 5.0}, {6.0, 5.0}, {6.0, 1.0}};
    selection.subdivisions  = 20;
    auto operation = target.makeEdgeCubicBezier(selection.buildingId, selection.controlPoints, selection.subdivisions);
    REQUIRE(operation.ok());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
    selection.memberInstanceId = world.getBuildingInstanceAt(0);
    return selection;
}

EditorPointerEvent pointer(EditorPointerEvent::Phase phase, float x, float y, int pointerId = 4) {
    EditorPointerEvent event;
    event.phase     = phase;
    event.pointerId = pointerId;
    event.button    = 0;
    event.x         = x;
    event.y         = y;
    return event;
}

}  // namespace

TEST_CASE("editor.building.curve_tool_routes_drag_as_one_authority_transaction") {
    registerToolFence();
    eve::building::PlacementWorld world(12, 12, 10.f);
    world.setGridPlane("xz");
    BuildingPlacementTarget        target("building-curve-tool-world", &world);
    BuildingEdgeCurveToolSelection selection   = placedCurve(world, target);
    const auto                     groupBefore = world.edgeCurveGroupForInstance(selection.memberInstanceId);
    REQUIRE(groupBefore.ok());
    const auto revisionBefore = target.revision();

    TestBuildingViewport  viewport;
    CountingAuthority     authority(&target);
    BuildingEdgeCurveTool tool(&viewport, &authority);
    REQUIRE(tool.setSelection(selection).ok());
    eve::editor::EditorSession session;
    session.bindTarget(target);
    REQUIRE(session.addTool(&tool));
    REQUIRE(session.activateTool(tool.descriptor().id));

    CHECK(session.dispatchPointer(pointer(EditorPointerEvent::Phase::Down, 10.f, 10.f)).capturePointer);
    CHECK(session.hasPointerCapture());
    CHECK(tool.isDragging());
    CHECK(session.dispatchPointer(pointer(EditorPointerEvent::Phase::Move, 20.f, 10.f)).handled);
    CHECK_EQ(authority.preflights, 0);
    CHECK_EQ(authority.commits, 0);
    CHECK_EQ(target.revision(), revisionBefore);

    CountingOverlay overlay;
    session.drawOverlay(overlay);
    CHECK_EQ(overlay.circles, 4);
    CHECK(overlay.lines > 4);

    CHECK(session.dispatchPointer(pointer(EditorPointerEvent::Phase::Up, 20.f, 10.f)).releasePointer);
    CHECK(!session.hasPointerCapture());
    CHECK(!tool.isDragging());
    CHECK_EQ(authority.preflights, 1);
    CHECK_EQ(authority.commits, 1);
    CHECK_EQ(target.revision(), revisionBefore + 1);
    REQUIRE(tool.lastReceipt().has_value());
    const eve::editing::TransactionReceipt firstReceipt = *tool.lastReceipt();
    auto                                   groupAfter   = world.edgeCurveGroup(groupBefore.value().id);
    REQUIRE(groupAfter.ok());
    CHECK(std::abs(groupAfter.value().controlPoints[0].x - 2.f) < 0.0001f);
    CHECK_EQ(groupAfter.value().controlPoints[0].y, groupBefore.value().controlPoints[0].y);

    CHECK(session.dispatchPointer(pointer(EditorPointerEvent::Phase::Down, 20.f, 10.f)).capturePointer);
    CHECK(session.dispatchPointer(pointer(EditorPointerEvent::Phase::Move, 30.f, 10.f)).handled);
    CHECK(session.dispatchPointer(pointer(EditorPointerEvent::Phase::Up, 30.f, 10.f)).releasePointer);
    CHECK_EQ(authority.preflights, 2);
    CHECK_EQ(authority.commits, 2);
    REQUIRE(tool.lastReceipt().has_value());
    const eve::editing::TransactionReceipt secondReceipt        = *tool.lastReceipt();
    auto                                   groupAfterSecondDrag = world.edgeCurveGroup(groupBefore.value().id);
    REQUIRE(groupAfterSecondDrag.ok());
    CHECK(std::abs(groupAfterSecondDrag.value().controlPoints[0].x - 3.f) < 0.0001f);

    REQUIRE(authority.compensate(secondReceipt).ok());
    REQUIRE(authority.compensate(firstReceipt).ok());
    auto undone = world.edgeCurveGroup(groupBefore.value().id);
    REQUIRE(undone.ok());
    CHECK_EQ(undone.value(), groupBefore.value());
    eve::building::BuildingRegistry::clear();
}

TEST_CASE("editor.building.curve_tool_cancel_and_miss_do_not_mutate") {
    registerToolFence();
    eve::building::PlacementWorld world(12, 12, 10.f);
    world.setGridPlane("xz");
    BuildingPlacementTarget        target("building-curve-tool-cancel", &world);
    BuildingEdgeCurveToolSelection selection = placedCurve(world, target);
    const auto                     before    = world.edgeCurveGroupForInstance(selection.memberInstanceId);
    REQUIRE(before.ok());
    const auto revisionBefore = target.revision();

    TestBuildingViewport  viewport;
    CountingAuthority     authority(&target);
    BuildingEdgeCurveTool tool(&viewport, &authority);
    REQUIRE(tool.setSelection(selection).ok());
    eve::editor::EditorSession session;
    session.bindTarget(target);
    REQUIRE(session.addTool(&tool));
    REQUIRE(session.activateTool(tool.descriptor().id));

    CHECK(!session.dispatchPointer(pointer(EditorPointerEvent::Phase::Down, 200.f, 200.f)).handled);
    CHECK(!session.hasPointerCapture());
    CHECK(session.dispatchPointer(pointer(EditorPointerEvent::Phase::Down, 10.f, 10.f)).capturePointer);
    CHECK(session.dispatchPointer(pointer(EditorPointerEvent::Phase::Move, 30.f, 10.f)).handled);
    CHECK(session.dispatchPointer(pointer(EditorPointerEvent::Phase::Cancel, 30.f, 10.f)).releasePointer);
    CHECK_EQ(authority.preflights, 0);
    CHECK_EQ(authority.commits, 0);
    CHECK_EQ(target.revision(), revisionBefore);
    auto after = world.edgeCurveGroup(before.value().id);
    REQUIRE(after.ok());
    CHECK_EQ(after.value(), before.value());
    eve::building::BuildingRegistry::clear();
}
}  // namespace test_editor_building_tool
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_editor_camera_target)
#include <map>
#include <memory>
#include <string>
#include "PrivateTestScope.h"
#include "camera/editing/CameraTarget.h"
#include "daynight/DayNight.h"
#include "decal/DecalManager.h"
#include "decal/editing/DecalTarget.h"
#include "fluids/editor/EditorSurfaceFluidPreview.h"
#include "graphics/Canvas.h"
#include "graphics/ICanvasFactory.h"
#include "graphics/ICanvasTarget.h"
#include "graphics/ISolidRectRenderer.h"
#include "graphics/Light.h"
#include "graphics/lighting/editing/LightingTarget.h"
#include "graphics/material/editing/MaterialGraph.h"
#include "graphics/material/editor/EditorMaterialOffscreen.h"
#include "hd2d/editing/Hd2dTarget.h"
#include "image/ImageData.h"
#include "particles/graphics_editing/ParticleOffscreen.h"
#include "ui/editor/EditorUiOffscreen.h"
#include "weather/Weather.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// editor_camera_target.cpp
namespace test_editor_camera_target {
// editor_camera_target.cpp
using namespace eve::camera_editing;
using namespace eve::editing;

namespace {
void apply(CameraDocumentTarget& target, Result<DomainOperation> operation) {
    REQUIRE(operation.ok());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
}
SelectionSnapshot select(const CameraDocumentTarget& target, const char* id) {
    SelectionSnapshot selection;
    selection.channel = "camera";
    selection.items.push_back({SelectionDomain::Asset, TargetId(target.targetId()), StableId(id), "camera.rig"});
    return selection;
}
CameraRigValue rig(const char* id, const char* mode = "follow") {
    CameraRigValue value;
    value.id   = ObjectId(id);
    value.name = id;
    value.mode = mode;
    return value;
}
}  // namespace

TEST_CASE("editor.camera.rigs_are_property_editable_reversible_and_persistent") {
    CameraDocumentTarget target("shot");
    apply(target, target.makeCreateRig(rig("hero", "orbit")));
    const auto selection = select(target, "hero");
    auto       edit      = target.makeSet(selection, PropertyPath("rig.fov"), 72.0, PropertySetMode::Absolute);
    REQUIRE(edit.ok());
    REQUIRE(target.applyDomainOperation(edit.value()).ok());
    CHECK_EQ(*target.read(selection, PropertyPath("rig.fov")).value.getIf<double>(), 72.0);
    DomainOperation undo = edit.value();
    undo.payload         = edit.value().inverse;
    REQUIRE(target.applyDomainOperation(undo).ok());
    CHECK_EQ(*target.read(selection, PropertyPath("rig.fov")).value.getIf<double>(), 60.0);
    CameraDocumentTarget restored("copy");
    REQUIRE(restored.loadSnapshot(target.snapshotValue()).ok());
    CHECK_EQ(restored.rigs().front().id.value(), std::string("hero"));
}

TEST_CASE("editor.camera.timeline_has_stable_keys_reference_safety_and_scrub") {
    CameraDocumentTarget target("shot");
    auto                 hero = rig("hero", "follow");
    hero.offset               = {0, 3, 8};
    apply(target, target.makeCreateRig(hero));
    CameraTimelineKeyValue cut;
    cut.id    = ObjectId("cut-1");
    cut.kind  = "cut";
    cut.time  = 1;
    cut.rig   = ObjectId("hero");
    cut.blend = .5f;
    apply(target, target.makeCreateKey(cut));
    CameraTimelineKeyValue fov0;
    fov0.id       = ObjectId("fov-0");
    fov0.kind     = "float";
    fov0.time     = 1;
    fov0.property = "fov";
    fov0.value    = 60;
    apply(target, target.makeCreateKey(fov0));
    CameraTimelineKeyValue fov;
    fov.id       = ObjectId("fov-1");
    fov.kind     = "float";
    fov.time     = 3;
    fov.property = "fov";
    fov.value    = 80;
    apply(target, target.makeCreateKey(fov));
    CHECK_EQ(static_cast<int>(target.makeDeleteRig(ObjectId("hero")).code()), static_cast<int>(EditorStatus::Conflict));
    CameraPreview preview;
    auto          pose = preview.evaluate(target, 2);
    REQUIRE(pose.ok());
    CHECK_EQ(pose.value().rig.value(), std::string("hero"));
    CHECK_EQ(pose.value().fov, 70.f);
    auto overlay = preview.gizmo(target, 2);
    REQUIRE(overlay.ok());
    CHECK_EQ(overlay.value().primitives.size(), static_cast<std::size_t>(3));
}

TEST_CASE("editor.camera.invalid_snapshot_and_runtime_generation_are_atomic") {
    CameraDocumentTarget target("shot");
    apply(target, target.makeCreateRig(rig("hero")));
    const auto  before  = target.snapshotValue();
    EditorValue invalid = before;
    auto*       root    = invalid.getIf<EditorValue::Object>();
    auto*       content = (*root)["content"].getIf<EditorValue::Object>();
    auto*       rigs    = (*content)["rigs"].getIf<EditorValue::Array>();
    auto*       first   = (*rigs)[0].getIf<EditorValue::Object>();
    (*first)["fov"]     = 200.0;
    CHECK_EQ(static_cast<int>(target.loadSnapshot(invalid).code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(target.snapshotValue(), before);
    CameraDocumentRuntime runtime;
    REQUIRE(runtime.publish(target, nullptr).ok());
    auto*                generation = runtime.controller();
    const auto           revision   = runtime.revision();
    CameraDocumentTarget empty("empty");
    REQUIRE(runtime.publish(empty, nullptr).ok());
    CHECK(runtime.controller() != generation);
    CHECK(runtime.revision() != revision);
}

// editor_decal_target.cpp
using namespace eve::decal_editing;
using namespace eve::editing;

namespace {
SelectionSnapshot selection(const char* target) {
    SelectionSnapshot value;
    value.items.push_back({SelectionDomain::Asset, TargetId(target), StableId("decal"), "decal.instance"});
    return value;
}
class Assets final : public IDecalRuntimeAssetResolver {
public:
    std::map<std::string, eve::graphics::Texture*> values;
    Result<eve::graphics::Texture*>                texture(const std::string& asset) const override {
        auto found = values.find(asset);
        if (found == values.end())
            return eve::editing::failed<eve::graphics::Texture*>(EditorStatus::NotFound, RuleId("test.decal.texture"),
                                                                                "missing texture");
        return eve::editing::applied<eve::graphics::Texture*>(found->second);
    }
};
Result<void> set(DecalDocumentTarget& target, const char* path, EditorValue value) {
    auto op = target.makeSet(selection(target.targetId().value().c_str()), PropertyPath(path), value,
                             PropertySetMode::Absolute);
    if (!op.ok()) return Result<void>::failure(op.status());
    return target.applyDomainOperation(op.value());
}
}  // namespace

TEST_CASE("editor.decal.properties_are_reversible_persistent_and_gizmo_ready") {
    DecalDocumentTarget target("scorch");
    auto                operation =
        target.makeSet(selection("scorch"), PropertyPath("projection.size"), 2.5, PropertySetMode::Absolute);
    REQUIRE(operation.ok());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
    CHECK_EQ(*target.value("projection.size")->getIf<double>(), 2.5);
    DomainOperation undo = operation.value();
    undo.payload         = operation.value().inverse;
    REQUIRE(target.applyDomainOperation(undo).ok());
    CHECK_EQ(*target.value("projection.size")->getIf<double>(), 0.5);
    REQUIRE(set(target, "texture.albedo", "textures/scorch.png").ok());
    auto gizmo = DecalGizmoPreviewService().build(target);
    CHECK_EQ(static_cast<int>(gizmo.status), static_cast<int>(EditorStatus::Applied));
    REQUIRE_EQ(gizmo.primitives.size(), static_cast<std::size_t>(2));
    CHECK_EQ(gizmo.primitives[0].kind, std::string("oriented-wire-box"));
    DecalDocumentTarget restored("copy");
    REQUIRE(restored.loadSnapshot(target.snapshotValue()).ok());
    CHECK_EQ(*restored.value("texture.albedo")->getIf<std::string>(), std::string("textures/scorch.png"));
}

TEST_CASE("editor.decal.snapshot_is_atomic_and_rejects_projection_rules") {
    DecalDocumentTarget target("scorch");
    const auto          before    = target.snapshotValue();
    EditorValue         invalid   = before;
    auto*               root      = invalid.getIf<EditorValue::Object>();
    auto*               values    = (*root)["values"].getIf<EditorValue::Object>();
    (*values)["transform.normal"] = EditorValue::Array{0.0, 0.0, 0.0};
    CHECK_EQ(static_cast<int>(target.loadSnapshot(invalid).code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(target.snapshotValue(), before);
    CHECK_EQ(static_cast<int>(target
                                  .makeSet(selection("scorch"), PropertyPath("texture.uvRect"),
                                           EditorValue::Array{0.8, 0.0, 0.4, 1.0}, PropertySetMode::Absolute)
                                  .code()),
             static_cast<int>(EditorStatus::Rejected));
}

TEST_CASE("editor.decal.v1_snapshot_migrates_with_pom_disabled") {
    DecalDocumentTarget original("legacy");
    EditorValue         legacy = original.snapshotValue();
    auto*               root   = legacy.getIf<EditorValue::Object>();
    (*root)["schemaVersion"]   = int64_t{1};
    auto* values               = (*root)["values"].getIf<EditorValue::Object>();
    values->erase("projection.parallaxScale");
    values->erase("projection.parallaxMinLayers");
    values->erase("projection.parallaxMaxLayers");
    DecalDocumentTarget restored("restored");
    REQUIRE(restored.loadSnapshot(legacy).ok());
    CHECK_EQ(*restored.value("projection.parallaxScale")->getIf<double>(), 0.0);
    CHECK_EQ(*restored.value("projection.parallaxMinLayers")->getIf<double>(), 8.0);
    CHECK_EQ(*restored.value("projection.parallaxMaxLayers")->getIf<double>(), 24.0);
    CHECK_EQ(*restored.value("projection.edgeFadeWidth")->getIf<double>(), 0.06);
}

TEST_CASE("editor.decal.v2_snapshot_accepts_world_projection_upgrade") {
    DecalDocumentTarget source("v2");
    EditorValue         snapshot = source.snapshotValue();
    auto*               root     = snapshot.getIf<EditorValue::Object>();
    (*root)["schemaVersion"]     = int64_t{2};
    DecalDocumentTarget restored("restored-v2");
    REQUIRE(restored.loadSnapshot(snapshot).ok());
    CHECK_EQ(*restored.value("projection.edgeFadeWidth")->getIf<double>(), 0.06);
    REQUIRE(set(restored, "projection.mode", "world").ok());
}

TEST_CASE("editor.decal.v2_and_v3_snapshots_fill_missing_pom_fields") {
    for (const int64_t version : {int64_t{2}, int64_t{3}}) {
        DecalDocumentTarget source("legacy-pom");
        EditorValue         snapshot = source.snapshotValue();
        auto*               root     = snapshot.getIf<EditorValue::Object>();
        (*root)["schemaVersion"]     = version;
        auto* values                 = (*root)["values"].getIf<EditorValue::Object>();
        values->erase("projection.parallaxScale");
        values->erase("projection.parallaxMinLayers");
        values->erase("projection.parallaxMaxLayers");
        values->erase("projection.edgeFadeWidth");
        DecalDocumentTarget restored("restored-legacy-pom");
        REQUIRE(restored.loadSnapshot(snapshot).ok());
        CHECK_EQ(*restored.value("projection.parallaxScale")->getIf<double>(), 0.0);
        CHECK_EQ(*restored.value("projection.parallaxMinLayers")->getIf<double>(), 8.0);
        CHECK_EQ(*restored.value("projection.parallaxMaxLayers")->getIf<double>(), 24.0);
        CHECK_EQ(*restored.value("projection.edgeFadeWidth")->getIf<double>(), 0.06);
    }
}

TEST_CASE("editor.decal.runtime_replacement_resolves_assets_before_atomic_generation_swap") {
    auto& manager = eve::decal::DecalManager::inst();
    manager.clearAll();
    manager.setLimit("scorch", 1);
    DecalDocumentTarget target("scorch");
    REQUIRE(set(target, "decal.kind", "scorch").ok());
    REQUIRE(set(target, "texture.albedo", "textures/a.png").ok());
    Assets assets;
    auto*  texture                  = reinterpret_cast<eve::graphics::Texture*>(0x1);
    assets.values["textures/a.png"] = texture;
    DecalRuntimeBinding binding(&manager, &assets);
    REQUIRE(binding.publish(target).ok());
    const int first = binding.runtimeId();
    REQUIRE(first > 0);
    CHECK_EQ(manager.count(), 1);
    REQUIRE(set(target, "texture.albedo", "textures/missing.png").ok());
    CHECK_EQ(static_cast<int>(binding.publish(target).code()), static_cast<int>(EditorStatus::NotFound));
    CHECK_EQ(binding.runtimeId(), first);
    CHECK_EQ(manager.count(), 1);
    CHECK_EQ(manager.instances()[0].id, first);
    REQUIRE(set(target, "texture.albedo", "textures/a.png").ok());
    REQUIRE(set(target, "projection.depth", 0.7).ok());
    REQUIRE(set(target, "projection.mode", "world").ok());
    REQUIRE(set(target, "projection.parallaxScale", 0.05).ok());
    REQUIRE(set(target, "projection.edgeFadeWidth", 0.12).ok());
    REQUIRE(binding.publish(target).ok());
    CHECK(binding.runtimeId() != first);
    CHECK_EQ(manager.count(), 1);
    CHECK_EQ(manager.instances()[0].depth, 0.7f);
    CHECK_EQ(manager.instances()[0].projectionMode, 3);
    CHECK_EQ(manager.instances()[0].parallaxScale, 0.05f);
    CHECK_EQ(manager.instances()[0].edgeFadeWidth, 0.12f);
    REQUIRE(binding.clear().ok());
    CHECK_EQ(manager.count(), 0);
}

TEST_CASE("editor.decal.publishing_target_preserves_author_and_runtime_on_rejection") {
    auto& manager = eve::decal::DecalManager::inst();
    manager.clearAll();
    Assets assets;
    assets.values["textures/a.png"] = reinterpret_cast<eve::graphics::Texture*>(0x1);
    DecalRuntimeBinding   runtime(&manager, &assets);
    DecalPublishingTarget target("live", &runtime);
    auto first = target.authoringTarget().makeSet(selection("live"), PropertyPath("texture.albedo"), "textures/a.png",
                                                  PropertySetMode::Absolute);
    REQUIRE(first.ok());
    REQUIRE(target.applyDomainOperation(first.value()).ok());
    const int generation = runtime.runtimeId();
    REQUIRE(generation > 0);
    auto missing = target.authoringTarget().makeSet(selection("live"), PropertyPath("texture.albedo"),
                                                    "textures/missing.png", PropertySetMode::Absolute);
    REQUIRE(missing.ok());
    CHECK_EQ(static_cast<int>(target.applyDomainOperation(missing.value()).code()),
             static_cast<int>(EditorStatus::NotFound));
    CHECK_EQ(*target.authoringTarget().value("texture.albedo")->getIf<std::string>(), std::string("textures/a.png"));
    CHECK_EQ(runtime.runtimeId(), generation);
    CHECK_EQ(manager.instances()[0].id, generation);
    REQUIRE(runtime.clear().ok());
}

// editor_domain_offscreen.cpp
using namespace eve::editor;
using namespace eve::editing;
using namespace eve::graphics_editing;
using namespace eve::particles_graphics_editing;

namespace {
class Canvas final : public eve::graphics::Canvas {
public:
    Canvas(int width, int height) : width_(width), height_(height) {}
    int                     getWidth() const override { return width_; }
    int                     getHeight() const override { return height_; }
    eve::graphics::Texture* getTexture() override { return nullptr; }
    void clear(std::optional<eve::graphics::Color>, std::optional<int>, std::optional<double>) override {}
    eve::graphics::Color   getPixel(int, int) override { return {}; }
    eve::image::ImageData* newImageData() override { return new eve::image::ImageData(width_, height_); }
    void                   draw(eve::graphics::Graphics*, const glm::mat4&) const override {}
    void                   draw(eve::graphics::Canvas*, const glm::mat4&) const override {}

private:
    int width_, height_;
};
class Backend final : public eve::graphics::ICanvasFactory,
                      public eve::graphics::ICanvasTarget,
                      public eve::graphics::ISolidRectRenderer {
public:
    eve::graphics::Canvas* newCanvas(int width, int height) override {
        canvas = std::make_unique<Canvas>(width, height);
        return canvas.get();
    }
    void                   setCanvas(eve::graphics::Canvas* value) override { current = value; }
    eve::graphics::Canvas* getCanvas() const override { return current; }
    void drawSolidRectRGBA(float, float, float, float, float, float, float, float) override { ++rectangles; }
    std::unique_ptr<Canvas> canvas;
    eve::graphics::Canvas*  current    = nullptr;
    int                     rectangles = 0;
};

GraphDocumentData particleGraph() {
    eve::particles_editing::ParticleGraphDomain domain;
    GraphDocument                               document;
    for (const char* type : {"emission", "motion", "collision", "renderer", "output"}) {
        auto node = domain.makeNode(GraphNodeId(type), type);
        REQUIRE(node.ok());
        CHECK(document.createNode(node.value()).ok());
    }
    int sequence = 0;
    for (const auto& [from, to] : std::vector<std::pair<const char*, const char*>>{{"emission.out", "motion.in"},
                                                                                   {"motion.out", "collision.in"},
                                                                                   {"collision.out", "renderer.in"},
                                                                                   {"renderer.out", "output.in"}}) {
        const auto* output = document.findPin(GraphPinId(from));
        const auto* input  = document.findPin(GraphPinId(to));
        REQUIRE(output);
        REQUIRE(input);
        CHECK(document
                  .connect({StableId("edge-" + std::to_string(sequence++)), output->id, input->id},
                           domain.canConnect(*output, *input))
                  .ok());
    }
    return document.snapshot(domain.domain());
}
}  // namespace

TEST_CASE("editor.material.offscreen_adapter_produces_publishable_artifact") {
    Backend                          backend;
    auto*                            token = reinterpret_cast<eve::graphics::Graphics*>(&backend);
    GraphicsOffscreenPreviewService  offscreen(token, &backend, &backend);
    int                              draws = 0;
    OffscreenMaterialPreviewRenderer renderer(&offscreen,
                                              [&](const MaterialPreviewRenderRequest& request, auto*, auto*) {
                                                  CHECK(request.material.getIf<EditorValue::Object>() != nullptr);
                                                  ++draws;
                                                  return eve::editing::applied<void>();
                                              });
    MaterialDocumentTarget           material("material");
    MaterialPreviewService           service;
    auto                             task = service.render(DocumentId("doc"), material, {}, renderer);
    REQUIRE(task.ok());
    CHECK_EQ(draws, 1);
    CHECK(service.publish(DocumentId("doc"), material.revision(), task.value()).ok());
    CHECK(service.publishedArtifact(DocumentId("doc")).starts_with("preview://"));
}

TEST_CASE("editor.ui.offscreen_renderer_rasterizes_visible_widget_boxes") {
    Backend                         backend;
    auto*                           token = reinterpret_cast<eve::graphics::Graphics*>(&backend);
    GraphicsOffscreenPreviewService offscreen(token, &backend, &backend);
    UiDocumentTarget                document("hud");
    UiLayoutValue                   layout;
    layout.width  = 80;
    layout.height = 40;
    auto create   = document.makeCreate({ObjectId("panel"), {}, "panel", "Panel", layout});
    REQUIRE(create.ok());
    CHECK(document.applyDomainOperation(create.value()).ok());
    UiOffscreenPreviewRenderer renderer(&offscreen, &backend);
    auto                       artifact = renderer.render(document, 320, 200);
    REQUIRE(artifact.ok());
    CHECK_EQ(backend.rectangles, 1);
    CHECK_EQ(artifact.value().sourceRevision, document.revision());
    CHECK(backend.current == nullptr);
}

TEST_CASE("editor.particles.offscreen_preview_compiles_and_budget_checks_before_draw") {
    Backend                         backend;
    auto*                           token = reinterpret_cast<eve::graphics::Graphics*>(&backend);
    GraphicsOffscreenPreviewService offscreen(token, &backend, &backend);
    int                             draws = 0;
    ParticleOffscreenPreviewService renderer(
        &offscreen, [&](const eve::particles_editing::ParticleGraphCompileResult& compiled,
                        const eve::particles_editing::ParticleGraphPreviewResult& estimate, auto*, auto*) {
            CHECK_EQ(compiled.documentRevision, estimate.documentRevision);
            ++draws;
            return eve::editing::applied<void>();
        });
    ParticleOffscreenPreviewRequest request;
    request.previewId = StableId("particles");
    request.graph     = particleGraph();
    auto artifact     = renderer.render(request);
    REQUIRE(artifact.ok());
    CHECK_EQ(draws, 1);
    request.seconds = -1.0;
    CHECK_EQ(static_cast<int>(renderer.render(request).code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(draws, 1);
}

namespace {
class ParticlePresenter final : public IParticleOffscreenPresenter {
public:
    int          draws = 0;
    Result<void> draw(const ParticleOffscreenPreviewRequest&                    request,
                      const eve::particles_editing::ParticleGraphCompileResult& compiled,
                      const eve::particles_editing::ParticleGraphPreviewResult& estimate, eve::graphics::Graphics*,
                      eve::graphics::Canvas*) override {
        CHECK_EQ(request.graph.revision, compiled.documentRevision);
        CHECK_EQ(compiled.documentRevision, estimate.documentRevision);
        ++draws;
        return eve::editing::applied<void>();
    }
};
}  // namespace

TEST_CASE("editor.particles.offscreen_preview_dispatches_isolated_presenter_contract") {
    Backend                         backend;
    auto*                           token = reinterpret_cast<eve::graphics::Graphics*>(&backend);
    GraphicsOffscreenPreviewService offscreen(token, &backend, &backend);
    ParticlePresenter               presenter;
    ParticleOffscreenPreviewService renderer(&offscreen, &presenter);
    ParticleOffscreenPreviewRequest request;
    request.previewId = StableId("isolated-particles");
    request.graph     = particleGraph();
    auto artifact     = renderer.render(request);
    REQUIRE(artifact.ok());
    CHECK_EQ(presenter.draws, 1);
    ParticleEmitterOffscreenPresenter           runtime;
    eve::particles_editing::ParticleGraphDomain domain;
    const auto                                  compiled = domain.compile(request.graph);
    const auto estimate = domain.preview(request.graph, request.seconds, request.fixedStep, request.particleBudget);
    request.graph.revision++;
    CHECK_EQ(static_cast<int>(runtime.draw(request, compiled, estimate, nullptr, nullptr).code()),
             static_cast<int>(EditorStatus::Conflict));
}

namespace {
class SurfaceFluidRenderer final : public ISurfaceFluidPreviewRenderer {
public:
    int          draws = 0;
    Result<void> draw(const SurfaceFluidPreviewSnapshot& snapshot) override {
        CHECK_EQ(static_cast<int>(snapshot.status), static_cast<int>(EditorStatus::Applied));
        ++draws;
        return eve::editing::applied<void>();
    }
};
}  // namespace

TEST_CASE("editor.surface_fluid.offscreen_preview_replays_before_renderer_dispatch") {
    Backend                             backend;
    auto*                               token = reinterpret_cast<eve::graphics::Graphics*>(&backend);
    GraphicsOffscreenPreviewService     offscreen(token, &backend, &backend);
    SurfaceFluidTarget                  target("waterfall");
    SurfaceFluidRenderer                presenter;
    SurfaceFluidOffscreenPreviewService renderer(&offscreen, &presenter);
    SurfaceFluidPreviewRequest          request;
    request.previewId        = StableId("surface-fluid");
    request.documentRevision = target.revision();
    request.seconds          = 0.1;
    request.positions        = {{{0.0, 0.0, 0.0}}, {{1.0, 0.0, 0.0}}, {{0.0, 0.0, 1.0}}};
    request.indices          = {0, 1, 2};
    request.seeds.push_back({});
    auto artifact = renderer.render(target, request);
    REQUIRE(artifact.ok());
    CHECK_EQ(presenter.draws, 1);
    CHECK_EQ(artifact.value().sourceRevision, target.revision());
    request.documentRevision++;
    CHECK_EQ(static_cast<int>(renderer.render(target, request).code()), static_cast<int>(EditorStatus::Conflict));
    CHECK_EQ(presenter.draws, 1);
}

// editor_graph_material_v2.cpp
using namespace eve::material_editing;
using namespace eve::editing;

namespace {

GraphNodeRecord constantNode(const char* id) {
    GraphNodeRecord node;
    node.id         = GraphNodeId(id);
    node.type       = "material.constant";
    node.properties = EditorValue::Object{{"value", EditorValue(1.0)}};
    node.pins.push_back({GraphPinId(std::string(id) + ".out"), node.id, "float", GraphPinDirection::Output});
    return node;
}

GraphNodeRecord outputNode(const char* id) {
    GraphNodeRecord node;
    node.id         = GraphNodeId(id);
    node.type       = "material.output";
    node.properties = EditorValue::Object{};
    node.pins.push_back({GraphPinId(std::string(id) + ".roughness"), node.id, "float", GraphPinDirection::Input});
    return node;
}

}  // namespace

TEST_CASE("editor.v2.graph_core_is_domain_neutral_and_revisioned") {
    GraphDocument       graph;
    MaterialGraphDomain material;
    CHECK(graph.createNode(constantNode("constant")).ok());
    CHECK(graph.createNode(outputNode("output")).ok());
    CHECK_EQ(graph.revision(), static_cast<Revision>(2));

    const GraphPinRecord* from = graph.findPin(GraphPinId("constant.out"));
    const GraphPinRecord* to   = graph.findPin(GraphPinId("output.roughness"));
    CHECK(from != nullptr);
    CHECK(to != nullptr);
    GraphConnectionDecision allowed = material.canConnect(*from, *to);
    CHECK(allowed.allowed);
    CHECK(graph.connect({StableId("edge-1"), from->id, to->id}, allowed).ok());
    CHECK_EQ(graph.revision(), static_cast<Revision>(3));
    CHECK_EQ(graph.snapshot(material.domain()).edges.size(), static_cast<std::size_t>(1));

    GraphConnectionDecision rejected = material.canConnect(*to, *from);
    CHECK(!rejected.allowed);
    CHECK_EQ(static_cast<int>(graph.connect({StableId("edge-2"), to->id, from->id}, rejected).code()),
             static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(graph.revision(), static_cast<Revision>(3));
}

TEST_CASE("editor.v2.material_compile_failure_keeps_last_successful_preview") {
    GraphDocument         graph;
    MaterialGraphDomain   domain;
    MaterialEditorService materials;
    DocumentId            document("document:material");
    CHECK(graph.createNode(outputNode("output")).ok());

    auto firstTask = materials.compile(document, graph.snapshot(domain.domain()), domain);
    CHECK(firstTask.ok());
    auto firstResult = materials.result(firstTask.value());
    CHECK(firstResult.ok());
    CHECK_EQ(static_cast<int>(firstResult.value().status), static_cast<int>(EditorStatus::Applied));
    CHECK(materials.publishPreview(document, graph.revision(), firstTask.value()).ok());
    const std::string firstPreview = materials.previewArtifact(document);
    CHECK(!firstPreview.empty());

    GraphNodeRecord invalid;
    invalid.id         = GraphNodeId("error");
    invalid.type       = "material.error";
    invalid.properties = EditorValue::Object{};
    CHECK(graph.createNode(std::move(invalid)).ok());
    auto failedTask = materials.compile(document, graph.snapshot(domain.domain()), domain);
    CHECK(failedTask.ok());
    CHECK_EQ(static_cast<int>(materials.result(failedTask.value()).value().status),
             static_cast<int>(EditorStatus::Failed));
    CHECK_EQ(static_cast<int>(materials.publishPreview(document, graph.revision(), failedTask.value()).code()),
             static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(materials.previewArtifact(document), firstPreview);
}

TEST_CASE("editor.v2.material_stale_compile_cannot_replace_preview") {
    GraphDocument         graph;
    MaterialGraphDomain   domain;
    MaterialEditorService materials;
    DocumentId            document("document:material-stale");
    CHECK(graph.createNode(outputNode("output")).ok());
    auto task = materials.compile(document, graph.snapshot(domain.domain()), domain);
    CHECK(task.ok());

    CHECK(graph.setNodeProperties(GraphNodeId("output"), EditorValue::Object{{"opacity", EditorValue(0.5)}}).ok());
    auto stale = materials.publishPreview(document, graph.revision(), task.value());
    CHECK_EQ(static_cast<int>(stale.code()), static_cast<int>(EditorStatus::Conflict));
    CHECK(materials.previewArtifact(document).empty());

    auto currentTask = materials.compile(document, graph.snapshot(domain.domain()), domain);
    CHECK(materials.publishPreview(document, graph.revision(), currentTask.value()).ok());
    CHECK(materials.previewArtifact(document).find("r2") != std::string::npos);
}

// editor_hd2d_target.cpp
using namespace eve::hd2d_editing;
using namespace eve::editing;
namespace {
SelectionSnapshot select(const Hd2dDocumentTarget& t) {
    SelectionSnapshot s;
    s.channel = "hd2d";
    s.items.push_back({SelectionDomain::Asset, TargetId(t.targetId()), StableId(t.targetId().value()), "hd2d.asset"});
    return s;
}
void apply(Hd2dDocumentTarget& t, Result<DomainOperation> op) {
    REQUIRE(op.ok());
    REQUIRE(t.applyDomainOperation(op.value()).ok());
}
class MissingTexture final : public IHd2dTextureResolver {
public:
    Result<eve::graphics::Texture*> texture(const std::string&) const override {
        return eve::editing::failed<eve::graphics::Texture*>(EditorStatus::NotFound, RuleId("test.texture"),
                                                             "missing texture");
    }
};
}  // namespace
TEST_CASE("editor.hd2d.sprite_preset_is_reversible_and_scrubs_frames") {
    Hd2dDocumentTarget target("hero");
    const auto         selection = select(target);
    apply(target, target.makeSet(selection, PropertyPath("asset.source"), "hero.png", PropertySetMode::Absolute));
    apply(target, target.makeSet(selection, PropertyPath("sprite.grid"), EditorValue::Array{4.0, 2.0},
                                 PropertySetMode::Absolute));
    apply(target, target.makeSet(selection, PropertyPath("sprite.animation"), EditorValue::Array{2.0, 5.0},
                                 PropertySetMode::Absolute));
    apply(target, target.makeSet(selection, PropertyPath("sprite.fps"), 2.0, PropertySetMode::Absolute));
    Hd2dFramePreviewService preview;
    auto                    p = preview.evaluate(target, 1);
    REQUIRE(p.ok());
    CHECK_EQ(p.value().frame, 4);
    CHECK_EQ(p.value().uv[0], 0.f);
    CHECK_EQ(p.value().uv[1], .5f);
    auto flip = target.makeSet(selection, PropertyPath("sprite.flipX"), true, PropertySetMode::Absolute);
    REQUIRE(flip.ok());
    REQUIRE(target.applyDomainOperation(flip.value()).ok());
    DomainOperation undo = flip.value();
    undo.payload         = flip.value().inverse;
    REQUIRE(target.applyDomainOperation(undo).ok());
    CHECK(!target.value().flipX);
}
TEST_CASE("editor.hd2d.rejects_invalid_frame_ranges_and_loads_atomically") {
    Hd2dDocumentTarget target("hero");
    const auto         selection = select(target);
    CHECK_EQ(static_cast<int>(target
                                  .makeSet(selection, PropertyPath("sprite.animation"), EditorValue::Array{0.0, 2.0},
                                           PropertySetMode::Absolute)
                                  .code()),
             static_cast<int>(EditorStatus::Rejected));
    const auto  before   = target.snapshotValue();
    EditorValue invalid  = before;
    auto*       root     = invalid.getIf<EditorValue::Object>();
    auto*       content  = (*root)["content"].getIf<EditorValue::Object>();
    (*content)["wallUv"] = EditorValue::Array{.8, .8, .2, .2};
    CHECK_EQ(static_cast<int>(target.loadSnapshot(invalid).code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(target.snapshotValue(), before);
}
TEST_CASE("editor.hd2d.runtime_resolves_assets_before_live_creation") {
    Hd2dDocumentTarget target("hero");
    const auto         selection = select(target);
    apply(target, target.makeSet(selection, PropertyPath("asset.source"), "missing.png", PropertySetMode::Absolute));
    Hd2dDocumentRuntime runtime;
    MissingTexture      textures;
    auto*               module   = reinterpret_cast<eve::hd2d::Hd2D*>(std::uintptr_t{1});
    auto*               graphics = reinterpret_cast<eve::graphics::Graphics*>(std::uintptr_t{1});
    CHECK_EQ(static_cast<int>(runtime.publishSprite(target, module, graphics, nullptr, textures).code()),
             static_cast<int>(EditorStatus::NotFound));
    CHECK_EQ(runtime.revision(), Revision(0));
}

// editor_lighting_target.cpp
using namespace eve::lighting_editing;
using namespace eve::editing;

namespace {

SelectionSnapshot selection(const LightingPropertyTargetBase& target) {
    SelectionSnapshot result;
    result.items.push_back(
        {SelectionDomain::Scene, TargetId(target.targetId()), StableId("lighting"), target.describe().type});
    return result;
}

void set(LightingPropertyTargetBase& target, const char* path, EditorValue value) {
    auto operation = target.makeSet(selection(target), PropertyPath(path), value, PropertySetMode::Absolute);
    REQUIRE(operation.ok());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
}

}  // namespace

TEST_CASE("editor.lighting.light3d_properties_validate_snapshot_and_apply_runtime") {
    Light3DDocumentTarget document("key-light");
    set(document, "light.type", "dir");
    set(document, "transform.direction", EditorValue::Array{0.0, -1.0, 0.0});
    set(document, "light.color", EditorValue::Array{1.0, 0.8, 0.6, 1.0});
    set(document, "light.intensity", 4.0);
    set(document, "shadow.cast", true);
    set(document, "shadow.method", "csm");
    set(document, "shadow.strength", 0.75);
    CHECK(document.validate().empty());

    eve::graphics::Light3D* light = eve::graphics::Light3D::createLight();
    REQUIRE(light);
    Light3DRuntimeApplier applier;
    REQUIRE(applier.apply(document, light).ok());
    CHECK_EQ(light->getType(), "dir");
    CHECK_EQ(light->getDirY(), -1.f);
    CHECK_EQ(light->data()->intensity, 4.f);
    CHECK(light->getCastShadow());
    CHECK_EQ(light->getShadowMethod(), "csm");
    CHECK_EQ(light->getShadowStrength(), 0.75f);

    const EditorValue     snapshot = document.snapshotValue();
    Light3DDocumentTarget restored("restored-light");
    REQUIRE(restored.loadSnapshot(snapshot).ok());
    CHECK_EQ(restored.snapshotValue(), snapshot);
}

TEST_CASE("editor.lighting.light3d_spot_applies_cone_and_perspective_shadow") {
    Light3DDocumentTarget document("spot-light");
    set(document, "light.type", "spot");
    set(document, "transform.position", EditorValue::Array{1.0, 3.0, -2.0});
    set(document, "transform.direction", EditorValue::Array{0.0, -1.0, 0.2});
    set(document, "light.intensity", 5.0);
    set(document, "light.spot-angle", 28.0);
    set(document, "light.spot-softness", 0.4);
    set(document, "shadow.cast", true);
    set(document, "shadow.method", "perspective");
    CHECK(document.validate().empty());

    eve::graphics::Light3D* light = eve::graphics::Light3D::createLight();
    REQUIRE(light);
    REQUIRE(Light3DRuntimeApplier{}.apply(document, light).ok());
    CHECK_EQ(light->getType(), "spot");
    CHECK_EQ(light->getSpotAngle(), 28.f);
    CHECK_EQ(light->getSpotSoftness(), 0.4f);
    CHECK_EQ(light->getShadowMethod(), "perspective");
    CHECK(light->getCastShadow());
}

TEST_CASE("editor.lighting.light3d_point_shadow_warns_when_cube_reserved") {
    Light3DDocumentTarget document("point-light");
    set(document, "light.type", "point");
    set(document, "shadow.cast", true);
    set(document, "shadow.method", "auto");
    const auto diagnostics = document.validate();
    REQUIRE(!diagnostics.empty());
    CHECK_EQ(diagnosticRule(diagnostics.front()).value(), "editor.light.point-shadow-reserved");
}

TEST_CASE("editor.lighting.light3d_rejects_zero_direction_before_runtime_mutation") {
    Light3DDocumentTarget document("invalid-light");
    set(document, "transform.direction", EditorValue::Array{0.0, 0.0, 0.0});
    REQUIRE(!document.validate().empty());
    eve::graphics::Light3D* light = eve::graphics::Light3D::createLight();
    REQUIRE(light);
    light->setPosition(9.f, 8.f, 7.f);
    const auto rejected = Light3DRuntimeApplier{}.apply(document, light);
    CHECK_EQ(static_cast<int>(rejected.code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(light->getX(), 9.f);
}

TEST_CASE("editor.lighting.environment_applies_daynight_and_weather_modes") {
    EnvironmentDocumentTarget daynightDocument("sky");
    set(daynightDocument, "environment.mode", "daynight");
    set(daynightDocument, "daynight.time", 18.5);
    set(daynightDocument, "daynight.speed", 2.0);
    set(daynightDocument, "daynight.turbidity", 6.0);
    set(daynightDocument, "environment.exposure", 1.8);
    eve::daynight::DayNight   daynight;
    EnvironmentRuntimeApplier applier;
    REQUIRE(applier.applyDayNight(daynightDocument, &daynight).ok());
    CHECK_EQ(daynight.getTimeOfDay(), 18.5f);
    CHECK_EQ(daynight.getSpeed(), 2.f);
    CHECK_EQ(daynight.getTurbidity(), 6.f);
    CHECK_EQ(daynight.getSkyExposure(), 1.8f);

    EnvironmentDocumentTarget weatherDocument("weather");
    set(weatherDocument, "environment.mode", "weather");
    set(weatherDocument, "weather.preset", "storm");
    set(weatherDocument, "weather.intensity", 0.9);
    set(weatherDocument, "weather.wind-speed", 20.0);
    set(weatherDocument, "weather.lightning", true);
    eve::weather::Weather weather;
    REQUIRE(applier.applyWeather(weatherDocument, &weather).ok());
    CHECK_EQ(weather.getPreset(), "storm");
    CHECK_EQ(weather.getIntensity(), 0.9f);
    CHECK_EQ(weather.getWindSpeed(), 20.f);
    CHECK(weather.isLightningEnabled());
    CHECK_EQ(static_cast<int>(applier.applyDayNight(weatherDocument, &daynight).code()),
             static_cast<int>(EditorStatus::Rejected));
}

TEST_CASE("editor.lighting.environment_detects_multiple_environment_owners") {
    EnvironmentDocumentTarget document("owners");
    set(document, "environment.mode", "daynight");
    set(document, "weather.environment-enabled", true);
    const auto diagnostics = document.validate();
    REQUIRE_EQ(diagnostics.size(), size_t{1});
    CHECK_EQ(diagnosticRule(diagnostics.front()).value(), "editor.environment.multiple-owners");
}
}  // namespace test_editor_camera_target
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_editor_core_v2)
#include "PrivateTestScope.h"
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include "editor/EditCommand.h"
#include "editor/EditorAssetDatabase.h"
#include "editor/EditorAuthority.h"
#include "editor/EditorCommandService.h"
#include "editor/EditorCurveDocument.h"
#include "editor/EditorDocumentService.h"
#include "editor/EditorEnvironmentBrowser.h"
#include "editor/EditorEventTimeline.h"
#include "editor/EditorHostProfile.h"
#include "editor/EditorIds.h"
#include "editor/EditorSession.h"
#include "editor/EditorTransactionService.h"
#include "editor/EditorValue.h"
#include "editor/EvaAssetDatabaseProjection.h"
#include "map/level/editing/FieldTargets.h"
#include "map/level/editing/TileBuffer.h"
#include "scene/editor/EditorSceneTarget.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// editor_core_v2.cpp
namespace test_editor_core_v2 {
// editor_core_v2.cpp
using namespace eve::editor;
using namespace eve::level_editing;

static_assert(!std::is_convertible_v<CommandId, ToolId>);
static_assert(!std::is_convertible_v<TargetId, CommandId>);

TEST_CASE("editor.v2.strong_ids_and_values") {
    const CommandId command("scene.object.create");
    const CommandId same("scene.object.create");
    const CommandId other("scene.object.delete");
    CHECK(command == same);
    CHECK(command != other);
    CHECK_EQ(command.value(), std::string("scene.object.create"));

    EditorValue::Object root;
    root["name"]     = "Station";
    root["position"] = EditorValue::Array{1.0, 2.0, 3.0};
    root["visible"]  = true;
    const EditorValue value(std::move(root));
    CHECK_EQ(static_cast<int>(value.type()), static_cast<int>(EditorValue::Type::Object));
    CHECK(value.isWithinLimits(4, 16, 128));
    CHECK(!value.isWithinLimits(1, 16, 128));
    CHECK(!value.isWithinLimits(4, 2, 128));
    CHECK(!value.isWithinLimits(4, 16, 4));
}

TEST_CASE("editor.v2.idsUseCommonUuidAdapterWithLegacyParse") {
    const auto legacy = CommandId::parse("scene.object.create");
    REQUIRE(legacy.has_value());
    CHECK(!legacy->isCanonicalUuid());
    CHECK_EQ(legacy->value(), std::string("scene.object.create"));

    const auto projected = canonicalUuid(*legacy);
    const auto canonical = CommandId::fromUuid(projected);
    CHECK(canonical.isCanonicalUuid());
    CHECK_EQ(canonical.uuid(), projected);
    CHECK_EQ(canonical.canonicalFormat(), projected.format());

    const auto parsed = CommandId::parse("01020304-0506-0708-090a-0b0c0d0e0f10");
    REQUIRE(parsed.has_value());
    CHECK(parsed->isCanonicalUuid());
    CHECK_EQ(parsed->value(), std::string("01020304-0506-0708-090a-0b0c0d0e0f10"));
}

TEST_CASE("editor.v2.host_profile_is_an_execution_boundary") {
    const CommandId place("scene.asset.place");
    const CommandId importAsset("asset.import");
    HostProfile     runtime = HostProfile::runtimeBuilder();
    CHECK(!runtime.allowsCommand(place));
    runtime.allowCommand(place);
    CHECK(runtime.allowsCommand(place));
    CHECK(!runtime.allowsCommand(importAsset));
    CHECK(runtime.hasFeatures(HostFeature::RuntimeWorld));
    CHECK(!runtime.hasFeatures(HostFeature::SourceAssets));

    HostProfile developer = HostProfile::developer();
    CHECK(developer.allowsCommand(importAsset));
    CHECK(developer.hasFeatures(HostFeature::SourceAssets | HostFeature::BuildCook));
    developer.denyCommand(importAsset);
    CHECK(!developer.allowsCommand(importAsset));
}

TEST_CASE("editor.v2.command_registry_filters_and_executes") {
    EditorCommandService service;
    CommandDescriptor    descriptor;
    descriptor.id               = CommandId("scene.rename");
    descriptor.ownerModule      = "scene";
    descriptor.displayName      = "Rename";
    descriptor.requiredFeatures = HostFeature::RuntimeWorld;

    int  calls      = 0;
    auto registered = service.registerCommand(descriptor, [&](const CommandContext&, const EditorValue& payload) {
        ++calls;
        return eve::editing::applied<EditorValue>(payload);
    });
    CHECK(registered.ok());
    CHECK_EQ(service.revision(), static_cast<uint64_t>(1));

    auto duplicate = service.registerCommand(descriptor, [](const CommandContext&, const EditorValue&) {
        return eve::editing::applied<EditorValue>(EditorValue{});
    });
    CHECK_EQ(static_cast<int>(duplicate.code()), static_cast<int>(EditorStatus::Rejected));

    HostProfile    runtime = HostProfile::runtimeBuilder();
    CommandContext context;
    context.profile = &runtime;
    auto denied     = service.execute(descriptor.id, context, EditorValue("New Name"));
    CHECK_EQ(static_cast<int>(denied.code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(calls, 0);

    runtime.allowCommand(descriptor.id);
    auto executed = service.execute(descriptor.id, context, EditorValue("New Name"));
    CHECK(executed.ok());
    CHECK_EQ(calls, 1);
    CHECK(executed.ok());
    CHECK_EQ(*executed.value().getIf<std::string>(), std::string("New Name"));
    CHECK_EQ(service.commands(runtime).size(), static_cast<size_t>(1));

    CHECK_EQ(service.unregisterOwner("scene"), static_cast<size_t>(1));
    CHECK(service.find(descriptor.id) == nullptr);
}

TEST_CASE("editor.v2.command_registry_gates_automation_and_exceptions") {
    EditorCommandService service;
    CommandDescriptor    descriptor;
    descriptor.id                = CommandId("scene.dangerous");
    descriptor.ownerModule       = "scene";
    descriptor.automationAllowed = false;
    CHECK(service
              .registerCommand(descriptor,
                               [](const CommandContext&, const EditorValue&) {
                                   throw std::runtime_error("boom");
                                   return eve::editing::applied<EditorValue>(EditorValue{});
                               })
              .ok());

    HostProfile    developer = HostProfile::developer();
    CommandContext context;
    context.profile = &developer;
    context.source  = CommandSource::Automation;
    auto automation = service.execute(descriptor.id, context, {});
    CHECK_EQ(static_cast<int>(automation.code()), static_cast<int>(EditorStatus::Rejected));

    context.source = CommandSource::Api;
    auto failure   = service.execute(descriptor.id, context, {});
    CHECK_EQ(static_cast<int>(failure.code()), static_cast<int>(EditorStatus::Failed));
    CHECK_EQ(failure.diagnostics().size(), static_cast<size_t>(1));
    CHECK(failure.diagnostics().front().message().find("boom") != std::string::npos);
}

TEST_CASE("editor.v2.session_wraps_command_in_legacy_transaction") {
    TileBuffer           buffer(2, 2);
    TileBufferTarget     target("tiles", &buffer);
    EditorCommandService service;

    CommandDescriptor descriptor;
    descriptor.id               = CommandId("map.paint-one");
    descriptor.ownerModule      = "map";
    descriptor.displayName      = "Paint One Tile";
    descriptor.requiredFeatures = HostFeature::RuntimeWorld;
    CHECK(service
              .registerCommand(
                  descriptor,
                  [](const CommandContext& context, const EditorValue& payload) {
                      auto* after = payload.getIf<int64_t>();
                      if (!context.session || !after)
                          return eve::editing::failed<EditorValue>(EditorStatus::Rejected, RuleId("map.paint.invalid"),
                                                                   "Paint command requires an integer value");
                      auto command = std::make_unique<IntFieldEditCommand>("Paint One Tile", context.session->target());
                      if (!command->record(0, 0, static_cast<int>(*after)) ||
                          !context.session->execute(std::move(command)))
                          return eve::editing::failed<EditorValue>(EditorStatus::Failed, RuleId("map.paint.failed"),
                                                                   "Tile edit could not be applied");
                      return eve::editing::applied<EditorValue>(payload);
                  })
              .ok());

    EditorSession session;
    HostProfile   runtime = HostProfile::runtimeBuilder();
    runtime.allowCommand(descriptor.id);
    session.setHostProfile(std::move(runtime));
    session.setCommandService(&service);
    session.bindTarget(&target);

    auto result = session.executeCommand(descriptor.id, EditorValue(7));
    CHECK(result.ok());
    CHECK_EQ(buffer.getGid(0, 0), 7);
    CHECK(session.transactions().canUndo());
    CHECK(session.transactions().undo());
    CHECK_EQ(buffer.getGid(0, 0), 0);

    session.setHostProfile(HostProfile::runtimeBuilder());
    auto denied = session.executeCommand(descriptor.id, EditorValue(9));
    CHECK_EQ(static_cast<int>(denied.code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(buffer.getGid(0, 0), 0);
}

// editor_curve_document.cpp
using namespace eve::editor;

TEST_CASE("editor.curve.timeline_is_reversible_sampled_and_serializable") {
    EditorCurveDocument curve("particle-size-color");
    auto                start = curve.makeSetKey({StableId("start"), 0.0, 0.0, 0.0, 0.0, "linear"});
    auto                end   = curve.makeSetKey({StableId("end"), 1.0, 2.0, 0.0, 0.0, "linear"});
    REQUIRE(start.ok());
    REQUIRE(end.ok());
    CHECK(curve.applyDomainOperation(start.value()).ok());
    CHECK(curve.applyDomainOperation(end.value()).ok());
    auto black = curve.makeSetStop({StableId("black"), 0.0, {0.0, 0.0, 0.0, 1.0}});
    auto white = curve.makeSetStop({StableId("white"), 1.0, {1.0, 1.0, 1.0, 1.0}});
    REQUIRE(black.ok());
    REQUIRE(white.ok());
    CHECK(curve.applyDomainOperation(black.value()).ok());
    CHECK(curve.applyDomainOperation(white.value()).ok());
    CHECK_EQ(curve.sampleCurve(0.5), 1.0);
    CHECK_EQ(curve.sampleGradient(0.5)[0], 0.5);
    const auto preview = curve.preview(16);
    CHECK_EQ(static_cast<int>(preview.status), static_cast<int>(EditorStatus::Applied));
    CHECK_EQ(preview.curveSamples.size(), 16U);
    auto remove = curve.makeDeleteKey(StableId("end"));
    REQUIRE(remove.ok());
    CHECK(curve.applyDomainOperation(remove.value()).ok());
    DomainOperation undo = remove.value();
    undo.type            = remove.value().inverseType;
    undo.payload         = remove.value().inverse;
    CHECK(curve.applyDomainOperation(undo).ok());
    CHECK_EQ(curve.keys().size(), 2U);
    EditorCurveDocument restored("particle-size-color");
    CHECK(restored.loadSnapshot(curve.snapshotValue()).ok());
    CHECK_EQ(restored.snapshotValue(), curve.snapshotValue());
    CHECK_EQ(static_cast<int>(curve.preview(5000, 4096).status), static_cast<int>(EditorStatus::Rejected));
}

// editor_document_asset_v2.cpp
using namespace eve::editor;

TEST_CASE("editor.v2.asset_database_import_query_dependencies_and_page_generation") {
    MemoryAssetDatabase database;
    ImportCoordinator   imports(&database);
    ImportProduct       texture;
    texture.record.guid       = AssetGuid("asset-texture");
    texture.record.logicalUri = "content://Textures/Coaster.png";
    texture.record.typeId     = "texture";
    texture.record.sourceUri  = "source://Coaster.png";
    texture.record.importerId = "image.importer";
    texture.record.tags       = {"park", "metal"};
    CHECK(imports.publish(std::move(texture)).ok());

    ImportProduct material;
    material.record.guid       = AssetGuid("asset-material");
    material.record.logicalUri = "content://Materials/Coaster.evmaterial";
    material.record.typeId     = "material";
    material.record.sourceUri  = "source://Coaster.material";
    material.record.importerId = "material.importer";
    material.dependencies.push_back(
        {material.record.guid, AssetGuid("asset-texture"), DependencyKind::Hard, PropertyPath("baseColor")});
    CHECK(imports.publish(std::move(material)).ok());

    AssetQuery query;
    query.text     = "coaster";
    auto firstPage = database.query(query, 0, 1);
    CHECK(firstPage.ok());
    CHECK_EQ(firstPage.value().values.size(), static_cast<std::size_t>(1));
    CHECK(firstPage.value().hasMore);
    const std::uint64_t generation = firstPage.value().generation;
    auto                secondPage = database.query(query, firstPage.value().nextOffset, 1, generation);
    CHECK(secondPage.ok());
    CHECK_EQ(secondPage.value().values.size(), static_cast<std::size_t>(1));
    CHECK_EQ(database.dependencies(AssetGuid("asset-material")).size(), static_cast<std::size_t>(1));
    CHECK_EQ(database.dependencies(AssetGuid("asset-texture"), true).size(), static_cast<std::size_t>(1));

    AssetRecord extra;
    extra.guid       = AssetGuid("asset-extra");
    extra.logicalUri = "content://Other/Extra.asset";
    extra.typeId     = "generic";
    CHECK(database.publish(std::move(extra)).ok());
    auto expired = database.query(query, firstPage.value().nextOffset, 1, generation);
    CHECK_EQ(static_cast<int>(expired.code()), static_cast<int>(EditorStatus::Conflict));
}

TEST_CASE("editor.v2.document_save_ticket_preserves_newer_dirty_edits") {
    MemoryAtomicDocumentStore store;
    DocumentService           documents(&store);
    DocumentKey               key{DocumentKind::Scene, AssetGuid("scene-asset")};
    auto                      opened = documents.open(key, "Park", "content://World/Park.evscene");
    CHECK(opened.ok());
    const DocumentId id = opened.value().id;

    EditorValue first(EditorValue::Object{{"version", EditorValue(1)}});
    auto        editOne = documents.edit(id, first);
    CHECK(editOne.ok());
    auto ticket = documents.requestSave(id);
    CHECK(ticket.ok());

    EditorValue second(EditorValue::Object{{"version", EditorValue(2)}});
    auto        editTwo = documents.edit(id, second, editOne.value().revision.edit);
    CHECK(editTwo.ok());
    auto savedOldRevision = documents.executeSave(ticket.value());
    CHECK(savedOldRevision.ok());
    CHECK(savedOldRevision.value().dirty());
    CHECK_EQ(savedOldRevision.value().revision.saved, editOne.value().revision.edit);
    CHECK_EQ(documents.content(id).value(), second);

    auto currentTicket = documents.requestSave(id);
    CHECK(documents.executeSave(currentTicket.value()).ok());
    CHECK(!documents.snapshot(id).value().dirty());
    CHECK(documents.close(id).ok());

    DocumentService reopenedService(&store);
    auto            reopened = reopenedService.open(key, "Park", "content://World/Park.evscene");
    CHECK(reopened.ok());
    CHECK(reopenedService.content(reopened.value().id).value() == second);
}

TEST_CASE("editor.v2.document_save_detects_external_revision_conflict") {
    MemoryAtomicDocumentStore store;
    DocumentService           documents(&store);
    DocumentKey               key{DocumentKind::Material, AssetGuid("material-asset")};
    auto                      opened = documents.open(key, "Material", "content://Materials/Test.evmaterial");
    const DocumentId          id     = opened.value().id;
    CHECK(documents.edit(id, EditorValue("local-v1")).ok());
    auto firstTicket = documents.requestSave(id);
    CHECK(documents.executeSave(firstTicket.value()).ok());

    CHECK(documents.edit(id, EditorValue("local-v2")).ok());
    auto       staleTicket = documents.requestSave(id);
    const auto persisted   = store.read("content://Materials/Test.evmaterial");
    CHECK(store
              .compareAndSwap("content://Materials/Test.evmaterial", 1, persisted.value().contentHash,
                              EditorValue("external-v2"))
              .ok());
    auto conflict = documents.executeSave(staleTicket.value());
    CHECK_EQ(static_cast<int>(conflict.code()), static_cast<int>(EditorStatus::Conflict));
    CHECK_EQ(static_cast<int>(conflict.value().state), static_cast<int>(DocumentState::Conflict));
}

TEST_CASE("editor.v2.import_place_save_and_reopen_headless_loop") {
    MemoryAssetDatabase database;
    ImportCoordinator   imports(&database);
    ImportProduct       product;
    product.record.guid       = AssetGuid("asset-coaster-blueprint");
    product.record.logicalUri = "content://Park/Coaster.blueprint";
    product.record.typeId     = "park.placeable";
    product.record.sourceUri  = "source://Coaster.blueprint";
    product.record.importerId = "park.blueprint.importer";
    CHECK(imports.publish(std::move(product)).ok());
    AssetRecord placeable = database.find(AssetGuid("asset-coaster-blueprint")).value();

    SceneDocumentTarget      scene("park-scene");
    LocalWorldAuthority      authority(&scene);
    LocalTransactionBackend  transactions(&authority);
    CreateSceneObjectRequest request;
    request.id        = ObjectId("placed-coaster");
    request.name      = placeable.guid.value();
    request.transform = {10.0, 0.0, 20.0};
    auto operation    = ScenePlacementToolLogic().plan(scene, request);
    CHECK(operation.ok());
    TransactionSpec specification;
    specification.id           = TransactionId("place-imported-asset");
    specification.label        = "Place imported asset";
    specification.target       = TargetId(scene.targetId());
    specification.baseRevision = scene.revision();
    CHECK(transactions.begin(std::move(specification)).ok());
    CHECK(transactions.append(operation.value()).ok());
    CHECK(transactions.commit().ok());

    MemoryAtomicDocumentStore store;
    DocumentService           documents(&store);
    DocumentKey               key{DocumentKind::Scene, AssetGuid("park-scene-asset")};
    auto                      opened = documents.open(key, "Park Scene", "content://World/Park.evscene");
    CHECK(documents.edit(opened.value().id, scene.snapshotValue()).ok());
    auto ticket = documents.requestSave(opened.value().id);
    CHECK(documents.executeSave(ticket.value()).ok());
    const EditorValue savedScene = documents.content(opened.value().id).value();
    CHECK(documents.close(opened.value().id).ok());

    DocumentService reopened(&store);
    auto            reopenedDocument = reopened.open(key, "Park Scene", "content://World/Park.evscene");
    CHECK(reopenedDocument.ok());
    CHECK(reopened.content(reopenedDocument.value().id).value() == savedScene);
}

TEST_CASE("editor.v2.asset_database_indexes_and_pages_100k_records") {
    MemoryAssetDatabase database;
    constexpr int       count = 100000;
    for (int index = 0; index < count; ++index) {
        AssetRecord record;
        record.guid       = AssetGuid("asset-" + std::to_string(index));
        record.logicalUri = "content://Bulk/asset-" + std::to_string(index);
        record.typeId     = index % 2 == 0 ? "bulk.even" : "bulk.odd";
        REQUIRE(database.publish(std::move(record)).ok());
    }
    CHECK_EQ(database.generation(), static_cast<std::uint64_t>(count));
    AssetQuery query;
    query.typeIds = {"bulk.odd"};
    auto page     = database.query(query, 1000, 128, database.generation());
    REQUIRE(page.ok());
    CHECK_EQ(page.value().values.size(), static_cast<std::size_t>(128));
    CHECK(page.value().hasMore);
    CHECK(database.findByUri("content://Bulk/asset-99999").ok());
}

// editor_environment_browser.cpp
using namespace eve::editor;

namespace {
AssetRecord environment(const char* id, const char* uri, const char* layout, int width, int height, int faces = 1) {
    AssetRecord record;
    record.guid       = AssetGuid(id);
    record.logicalUri = uri;
    record.typeId     = "environment-map";
    record.sourceUri  = std::string("source/") + id;
    record.sourceHash = "hash";
    record.importerId = "environment-test";
    record.artifacts  = {std::string("artifact/") + id};
    record.metadata   = EditorValue::Object{
          {"layout", layout}, {"width", int64_t{width}}, {"height", int64_t{height}}, {"faceCount", int64_t{faces}}};
    return record;
}
}  // namespace

TEST_CASE("editor.environment.browser_pages_and_rejects_stale_generation") {
    MemoryAssetDatabase database;
    CHECK(database.publish(environment("sky-a", "env/sky-a", "equirectangular", 2048, 1024)).ok());
    const auto              generation = database.generation();
    EnvironmentAssetBrowser browser(&database);
    auto                    page = browser.query("sky", 0, 20, generation);
    REQUIRE(page.ok());
    CHECK_EQ(page.value().values.size(), 1U);
    CHECK_EQ(page.value().values.front().layout, std::string("equirectangular"));
    CHECK(database.publish(environment("sky-b", "env/sky-b", "cubemap", 512, 512, 6)).ok());
    CHECK_EQ(static_cast<int>(browser.query({}, 0, 20, generation).code()), static_cast<int>(EditorStatus::Conflict));
}

TEST_CASE("editor.environment.browser_surfaces_invalid_import_metadata") {
    MemoryAssetDatabase database;
    CHECK(database.publish(environment("broken", "env/broken", "cubemap", 512, 512, 5)).ok());
    EnvironmentAssetBrowser browser(&database);
    auto                    selected = browser.select(AssetGuid("broken"));
    CHECK_EQ(static_cast<int>(selected.code()), static_cast<int>(EditorStatus::Rejected));
    CHECK(!selected.diagnostics().empty());
}

// editor_eva_asset_projection.cpp
using namespace eve::editor;

TEST_CASE("editor.asset.evaProjectionPublishesOneGenerationAndRejectsAtomically") {
    MemoryAssetDatabase     database;
    eve::asset::EvaManifest manifest;
    manifest.packageId      = *eve::PersistentId::parse("018f6f22-2490-7ad2-bf58-4f1dbca31040");
    manifest.packageName    = "terrain-kit";
    manifest.packageVersion = "1.0.0";
    auto terrain            = eve::AssetRef::parse("asset://018f6f22-2490-7ad2-bf58-4f1dbca31041").takeValue();
    auto material           = eve::AssetRef::parse("asset://018f6f22-2490-7ad2-bf58-4f1dbca31042").takeValue();
    manifest.assets         = {
        {terrain, "eve.terrain", eve::SchemaVersion(1), "assets/terrain.json", "sha256:" + std::string(64, '1'), {}},
        {material,
                 "eve.terrain-material",
                 eve::SchemaVersion(1),
                 "assets/material.json",
                 "sha256:" + std::string(64, '2'),
                 {"ground"}},
    };
    manifest.dependencies.push_back({terrain,
                                     material,
                                     eve::asset::EvaDependencyKind::RuntimeRequired,
                                     "material",
                                     {},
                                     "eve.terrain-material",
                                     {}});

    auto published = publishEvaAssetProjection(database, manifest, "project://Assets/terrain.eva", "unity/1");
    REQUIRE(published.ok());
    CHECK_EQ(published.value().size(), std::size_t(2));
    CHECK_EQ(database.generation(), std::uint64_t(1));
    CHECK_EQ(database.dependencies(AssetGuid(terrain.id().format())).size(), std::size_t(1));

    auto conflicting            = manifest;
    conflicting.assets[1].asset = terrain;
    auto rejected = publishEvaAssetProjection(database, conflicting, "project://Assets/bad.eva", "unity/1");
    CHECK(!rejected.ok());
    CHECK_EQ(database.generation(), std::uint64_t(1));
    CHECK_EQ(database.find(AssetGuid(material.id().format())).value().sourceUri,
             std::string("project://Assets/terrain.eva#assets/material.json"));
}

// editor_event_timeline.cpp
using namespace eve::editor;

TEST_CASE("editor.timeline_filters_correlations_and_rejects_stale_pages") {
    EditorEventTimeline timeline;
    CHECK(timeline
              .append({0, 10, "network", "connected", "peer-1", "session-a", DiagnosticSeverity::Info,
                       EditorValue::Object{}})
              .ok());
    CHECK(timeline
              .append({0, 12, "network", "timeout", "peer-2", "session-b", DiagnosticSeverity::Warning,
                       EditorValue::Object{{"retry", int64_t{2}}}})
              .ok());
    EditorTimelineQuery filter;
    filter.domains         = {"network"};
    filter.correlation     = "session-b";
    filter.minimumSeverity = DiagnosticSeverity::Warning;
    auto page              = timeline.query(filter, 0, 10);
    REQUIRE(page.ok());
    CHECK_EQ(page.value().values.size(), 1U);
    const auto generation = page.value().generation;
    CHECK(timeline.append({0, 13, "filesystem", "modified", "asset", {}, DiagnosticSeverity::Info, {}}).ok());
    CHECK_EQ(static_cast<int>(timeline.query(filter, 0, 10, generation).code()),
             static_cast<int>(EditorStatus::Conflict));
}

TEST_CASE("editor.timeline_capacity_drops_oldest_and_payload_is_bounded") {
    EditorEventTimeline timeline;
    CHECK(timeline.setCapacity(2).ok());
    CHECK(timeline.append({0, 1, "a", "one", {}, {}, DiagnosticSeverity::Info, {}}).ok());
    CHECK(timeline.append({0, 2, "a", "two", {}, {}, DiagnosticSeverity::Info, {}}).ok());
    CHECK(timeline.append({0, 3, "a", "three", {}, {}, DiagnosticSeverity::Info, {}}).ok());
    auto page = timeline.query({}, 0, 10);
    REQUIRE(page.ok());
    CHECK_EQ(page.value().values.size(), 2U);
    CHECK_EQ(page.value().droppedEvents, 1U);
    CHECK_EQ(page.value().values.front().type, std::string("two"));
    CHECK_EQ(
        static_cast<int>(
            timeline.append({0, 4, "a", "large", {}, {}, DiagnosticSeverity::Info, EditorValue::Array{1.0, 2.0}}, 2)
                .code()),
        static_cast<int>(EditorStatus::Rejected));
}
}  // namespace test_editor_core_v2
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_editor_definition_target)
#include "PrivateTestScope.h"
#include "definitions/Definitions.h"
#include "definitions/editing/DefinitionForm.h"
#include "definitions/editing/DefinitionTarget.h"
#include "editor/queue/editing/QueueInspector.h"
#include "orders/CommandQueue.h"
#include "production/Production.h"
#include "schema/SchemaTypes.h"
#include "social/SocialGraph.h"
#include "social/editing/SocialDocument.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// editor_definition_target.cpp
namespace test_editor_definition_target {
// editor_definition_target.cpp
using namespace eve::definitions_editing;
using namespace eve::editing;

TEST_CASE("editor.definition.schema_form_exposes_field_metadata_and_reversible_edits") {
    DefinitionDocument document("weapon", "sword", 2);
    REQUIRE(document.setJson("{\"damage\":10,\"rarity\":\"common\"}").ok());
    eve::schema::SchemaDefinition schema;
    schema.id      = "weapon";
    schema.version = 2;
    eve::schema::FieldDefinition damage;
    damage.name        = "damage";
    damage.title       = "Damage";
    damage.type        = eve::schema::ValueType::Number;
    damage.required    = true;
    damage.minimum     = 0.0;
    damage.maximum     = 1000.0;
    damage.defaultJson = "1";
    eve::schema::FieldDefinition rarity;
    rarity.name        = "rarity";
    rarity.type        = eve::schema::ValueType::String;
    rarity.enumValues  = {"common", "rare", "legendary"};
    rarity.defaultJson = "\"common\"";
    schema.fields      = {damage, rarity};
    DefinitionSchemaFormTarget form(&document, &schema);
    SelectionSnapshot          selected;
    selected.items.push_back({SelectionDomain::Asset, TargetId(document.targetId()), StableId("sword"), "weapon"});
    CHECK_EQ(form.schema(selected).properties.size(), 2U);
    CHECK(form.validate().empty());
    auto operation = form.makeSet(selected, PropertyPath("damage"), 25.0, PropertySetMode::Absolute);
    REQUIRE(operation.ok());
    CHECK(document.applyDomainOperation(operation.value()).ok());
    const auto editedDamage = form.read(selected, PropertyPath("damage"));
    CHECK_EQ(*editedDamage.value.getIf<double>(), 25.0);
    DomainOperation undo = operation.value();
    undo.payload         = operation.value().inverse;
    CHECK(document.applyDomainOperation(undo).ok());
    const auto restoredDamage = form.read(selected, PropertyPath("damage"));
    CHECK_EQ(*restoredDamage.value.getIf<double>(), 10.0);
    CHECK_EQ(
        static_cast<int>(form.makeSet(selected, PropertyPath("rarity"), "invalid", PropertySetMode::Absolute).code()),
        static_cast<int>(EditorStatus::Rejected));
}

TEST_CASE("editor.definition.validates_schema_and_cross_references") {
    DefinitionDocument document("weapon", "iron_sword", 2);
    REQUIRE(document.setJson(R"({"damage":12,"effect":"burn"})").ok());
    REQUIRE(document.setReferences({{"/effect", "effect", "burn", true}}).ok());
    const auto valid = document.validate(
        [&](const std::string& type, int version, const std::string&) {
            CHECK_EQ(type, "weapon");
            CHECK_EQ(version, 2);
            return std::vector<EditorDiagnostic>{};
        },
        [](const std::string& type, const std::string& id) { return type == "effect" && id == "burn"; });
    CHECK(valid.empty());

    const auto unresolved = document.validate({}, [](const std::string&, const std::string&) { return false; });
    CHECK_EQ(unresolved.size(), size_t{1});
    CHECK_EQ(diagnosticRule(unresolved.front()).value(), "editor.definition.reference-not-found");
}

TEST_CASE("editor.definition.snapshot_load_is_atomic") {
    DefinitionDocument source("item", "potion");
    REQUIRE(source.setJson(R"({"heal":10})").ok());
    REQUIRE(source.setReferences({{"/upgrade", "item", "greater_potion", false}}).ok());
    const EditorValue snapshot = source.snapshotValue();

    DefinitionDocument restored("placeholder", "placeholder");
    REQUIRE(restored.loadSnapshot(snapshot).ok());
    CHECK_EQ(restored.snapshotValue(), snapshot);
    const std::uint64_t before = restored.revision();
    EditorValue         broken = snapshot;
    auto*               object = broken.getIf<EditorValue::Object>();
    REQUIRE(object);
    (*object)["definitionVersion"] = int64_t{0};
    CHECK_EQ(static_cast<int>(restored.loadSnapshot(broken).code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(restored.revision(), before);
    CHECK_EQ(restored.snapshotValue(), snapshot);
}

TEST_CASE("editor.definition.publishes_to_real_versioned_registry") {
    DefinitionDocument document("item", "potion");
    REQUIRE(document.setJson(R"({"heal":10})").ok());
    eve::definitions::DefinitionRegistry registry;
    DefinitionRuntimePublisher           publisher;
    REQUIRE(publisher.publish(document, &registry).ok());
    auto stored = registry.resolve("item", "potion");
    REQUIRE(stored.ok());
    CHECK_EQ(stored.value().get().json, R"({"heal":10})");

    REQUIRE(document.setJson(R"({"heal":20})").ok());
    CHECK_EQ(static_cast<int>(publisher.publish(document, &registry).code()), static_cast<int>(EditorStatus::Rejected));
    REQUIRE(publisher.publish(document, &registry, true).ok());
    stored = registry.resolve("item", "potion");
    REQUIRE(stored.ok());
    CHECK_EQ(stored.value().get().json, R"({"heal":20})");
}

// editor_queue_inspector.cpp
using namespace eve::queue_editing;

TEST_CASE("editor.queue.inspector_copies_orders_without_borrowing_runtime_storage") {
    eve::orders::CommandQueue queue;
    auto                      move = queue.append("move", 5, 10.0);
    auto                      wait = queue.append("wait", 1, 0.0);
    REQUIRE(move.value());
    REQUIRE(wait.value());
    RuntimeQueueInspector inspector;
    auto                  snapshot = inspector.capture(queue);
    CHECK_EQ(snapshot.domain, std::string("orders"));
    CHECK_EQ(snapshot.items.size(), 2U);
    CHECK(!snapshot.events.empty());
    auto filtered = inspector.capture(queue, "active", "move");
    CHECK_EQ(filtered.items.size(), 1U);
    queue.clear();
    CHECK_EQ(snapshot.items.size(), 2U);
}

TEST_CASE("editor.queue.inspector_filters_production_owner_and_state") {
    eve::production::WorkQueue queue;
    auto task = queue.enqueue("factory", "craft", "sword", eve::Value(eve::Value::Object{}), 3.0, 4);
    REQUIRE(task.value());
    RuntimeQueueInspector inspector;
    auto                  snapshot = inspector.capture(queue, "factory", "running", "craft");
    CHECK_EQ(snapshot.domain, std::string("production"));
    CHECK_EQ(snapshot.items.size(), 1U);
    CHECK_EQ(snapshot.items.front().product, std::string("sword"));
    CHECK_EQ(snapshot.items.front().duration, 3.0);
    CHECK(snapshot.diagnostics.empty());
}

// editor_social_document.cpp
using namespace eve::social_editing;
using namespace eve::editing;

TEST_CASE("editor.social.graph_edges_are_reversible_validated_and_publishable") {
    SocialDocumentTarget document("diplomacy");
    for (const SocialEntityRecord& entity : std::vector<SocialEntityRecord>{
             {StableId("unit"), "Unit", "actor"}, {StableId("player"), "Player", "faction"}}) {
        auto operation = document.makeSetEntity(entity);
        REQUIRE(operation.ok());
        CHECK(document.applyDomainOperation(operation.value()).ok());
    }
    SocialEdgeRecord owner{StableId("owns"), StableId("unit"), StableId("player"), "owner", {}, 1.0};
    auto             set = document.makeSetEdge(owner);
    REQUIRE(set.ok());
    CHECK(document.applyDomainOperation(set.value()).ok());
    CHECK_EQ(static_cast<int>(document.makeDeleteEntity(StableId("unit")).code()),
             static_cast<int>(EditorStatus::Conflict));
    eve::social::SocialGraph runtime;
    CHECK(SocialRuntimeApplier().apply(document, &runtime).ok());
    CHECK_EQ(runtime.ownerOf("unit"), std::string("player"));

    DomainOperation undo = set.value();
    undo.type            = set.value().inverseType;
    undo.payload         = set.value().inverse;
    CHECK(document.applyDomainOperation(undo).ok());
    CHECK(document.edges().empty());
    CHECK(document.makeDeleteEntity(StableId("unit")).ok());
}

TEST_CASE("editor.social.snapshot_rejects_dangling_or_duplicate_semantics_atomically") {
    SocialDocumentTarget source("source");
    CHECK(source.applyDomainOperation(source.makeSetEntity({StableId("a"), "A", "actor"}).value()).ok());
    CHECK(source.applyDomainOperation(source.makeSetEntity({StableId("b"), "B", "actor"}).value()).ok());
    CHECK(
        source
            .applyDomainOperation(
                source.makeSetEdge({StableId("likes"), StableId("a"), StableId("b"), "relation", "likes", 0.8}).value())
            .ok());
    SocialDocumentTarget loaded("loaded");
    CHECK(loaded.loadSnapshot(source.snapshotValue()).ok());
    CHECK_EQ(loaded.edges().size(), 1U);
    auto  invalid = source.snapshotValue();
    auto* root    = invalid.getIf<EditorValue::Object>();
    REQUIRE(root);
    auto* entities = root->at("entities").getIf<EditorValue::Array>();
    REQUIRE(entities);
    entities->clear();
    CHECK_EQ(static_cast<int>(loaded.loadSnapshot(invalid).code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(loaded.entities().size(), 2U);
}
}  // namespace test_editor_definition_target
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_editor_dialogue_graph)
#include "PrivateTestScope.h"
#include "dialogue/ConversationAuthoring.h"
#include "dialogue/editing/DialogueGraph.h"
#include "dialogue/editor/EditorDialoguePreview.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// editor_dialogue_graph.cpp
namespace test_editor_dialogue_graph {
// editor_dialogue_graph.cpp
using namespace eve::dialogue_editing;
using namespace eve::editing;

namespace {

GraphDocumentData conversationGraph(DialogueGraphDomain& domain) {
    auto line   = domain.makeNode(GraphNodeId("greeting"), "line");
    auto choice = domain.makeNode(GraphNodeId("answer"), "choice");
    auto route  = domain.makeRouteNode(GraphNodeId("answer.yes"), "Yes");
    auto end    = domain.makeNode(GraphNodeId("done"), "end");
    REQUIRE(line.ok());
    REQUIRE(choice.ok());
    REQUIRE(route.ok());
    REQUIRE(end.ok());
    auto* lineProperties = line.value().properties.getIf<EditorValue::Object>();
    REQUIRE(lineProperties);
    (*lineProperties)["speaker"] = "guide";
    (*lineProperties)["text"]    = "Continue?";
    (*lineProperties)["i18nKey"] = "dialogue.guide.continue";
    (*lineProperties)["voice"]   = "asset://voice/continue.ogg";

    GraphDocument graph;
    REQUIRE(graph.createNode(line.value()).ok());
    REQUIRE(graph.createNode(choice.value()).ok());
    REQUIRE(graph.createNode(route.value()).ok());
    REQUIRE(graph.createNode(end.value()).ok());
    const auto connect = [&](const char* edge, const char* from, const char* to) {
        const auto* output = graph.findPin(GraphPinId(from));
        const auto* input  = graph.findPin(GraphPinId(to));
        REQUIRE(output);
        REQUIRE(input);
        REQUIRE(graph.connect({StableId(edge), output->id, input->id}, domain.canConnect(*output, *input)).ok());
    };
    connect("line-choice", "greeting.out", "answer.in");
    connect("choice-route", "answer.out", "answer.yes.in");
    connect("route-end", "answer.yes.out", "done.in");
    EditorValue::Array parameters{"has_met_guide"};
    REQUIRE(
        graph
            .setParameters(EditorValue::Object{
                {"id", "intro"}, {"version", int64_t{2}}, {"entry", "greeting"}, {"parameters", std::move(parameters)}})
            .ok());
    return graph.snapshot(domain.domain());
}

}  // namespace

TEST_CASE("editor.dialogue.graph_compiles_nodes_routes_and_localization_metadata") {
    DialogueGraphDomain              domain;
    const GraphDocumentData          graph    = conversationGraph(domain);
    const DialogueGraphCompileResult compiled = domain.compile(graph);
    CHECK_EQ(static_cast<int>(compiled.status), static_cast<int>(EditorStatus::Applied));
    CHECK_EQ(compiled.documentRevision, graph.revision);
    const auto* definition = compiled.definition.getIf<EditorValue::Object>();
    REQUIRE(definition);
    CHECK_EQ(*definition->at("id").getIf<std::string>(), "intro");
    CHECK_EQ(*definition->at("version").getIf<int64_t>(), int64_t{2});
    CHECK_EQ(definition->at("nodes").getIf<EditorValue::Array>()->size(), size_t{3});
}

TEST_CASE("editor.dialogue.graph_rejects_bypassed_or_incomplete_routes") {
    DialogueGraphDomain domain;
    GraphDocumentData   graph = conversationGraph(domain);
    graph.edges.erase(graph.edges.begin() + 1);
    const auto incomplete = domain.compile(graph);
    CHECK_EQ(static_cast<int>(incomplete.status), static_cast<int>(EditorStatus::Failed));
    REQUIRE(!incomplete.diagnostics.empty());

    graph               = conversationGraph(domain);
    graph.schemaVersion = 3;
    CHECK_EQ(static_cast<int>(domain.compile(graph).status), static_cast<int>(EditorStatus::Unsupported));

    graph            = conversationGraph(domain);
    auto* parameters = graph.parameters.getIf<EditorValue::Object>();
    REQUIRE(parameters);
    (*parameters)["entry"] = "answer.yes";
    CHECK_EQ(static_cast<int>(domain.compile(graph).status), static_cast<int>(EditorStatus::Failed));
}

TEST_CASE("editor.dialogue.graph_builds_real_conversation_document") {
    DialogueGraphDomain         domain;
    const GraphDocumentData     graph = conversationGraph(domain);
    DialogueGraphRuntimeBuilder builder;
    auto                        built = builder.build(graph);
    REQUIRE(built.ok());
    auto* document = built.value();
    REQUIRE(document);
    CHECK_EQ(document->getId(), "intro");
    CHECK_EQ(document->getEntry(), "greeting");
    CHECK_EQ(document->getNodeCount(), 3);
    CHECK_EQ(document->getField("greeting", "next"), "answer");
    CHECK_EQ(document->getRouteLabel("answer", 0), "Yes");
    CHECK_EQ(document->getRouteTarget("answer", 0), "done");
    CHECK(document->validate());
    delete document;
}

// editor_dialogue_preview.cpp
using namespace eve::editor;

TEST_CASE("editor.dialogue.subtitle_preview_checks_safe_area_readability_and_lip_envelope") {
    EditorAudioPcm          pcm{4, 1, {0.0F, 0.5F, 1.0F, 0.5F}};
    AudioWaveformService    waveformService;
    const auto              waveform = waveformService.generate({"voice", 9, 4, 0.0, 0.0}, pcm);
    DialogueSubtitleRequest request;
    request.lineId                   = "line-1";
    request.locale                   = "en";
    request.speaker                  = "Ada";
    request.text                     = "This sentence wraps into a deterministic subtitle preview.";
    request.sourceRevision           = 9;
    request.viewportWidth            = 1920.0;
    request.viewportHeight           = 1080.0;
    request.maximumCharactersPerLine = 24;
    request.maximumLines             = 3;
    request.voiceDuration            = 1.0;
    DialogueSubtitlePreviewService service;
    const auto                     preview = service.build(request, &waveform);
    CHECK_EQ(static_cast<int>(preview.status), static_cast<int>(EditorStatus::Applied));
    CHECK_EQ(preview.safeX, 96.0);
    CHECK_EQ(preview.safeWidth, 1728.0);
    CHECK(preview.lines.size() > 1U);
    CHECK_EQ(preview.lipEnvelope.size(), 4U);
    REQUIRE(!preview.diagnostics.empty());

    request.maximumLines = 1;
    CHECK_EQ(static_cast<int>(service.build(request).status), static_cast<int>(EditorStatus::Failed));
    request.maximumLines   = 3;
    request.sourceRevision = 10;
    const auto stale       = service.build(request, &waveform);
    CHECK_EQ(static_cast<int>(stale.status), static_cast<int>(EditorStatus::Applied));
    CHECK(stale.lipEnvelope.empty());
}
}  // namespace test_editor_dialogue_graph
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_editor_extension_v2)
#include <simplesquirrel/simplesquirrel.hpp>
#include <stdexcept>
#include <string>
#include "PrivateTestScope.h"
#include "audio/editing/AudioTarget.h"
#include "common/Module.h"
#include "editor/EditorAssetDatabase.h"
#include "editor/EditorAuthority.h"
#include "editor/EditorExtension.h"
#include "editor/gizmo/editing/DomainGizmoPreview.h"
#include "editor/input/editing/InputMapTarget.h"
#include "graphics/lighting/editing/LightingTarget.h"
#include "graphics/material/editor/EditorMaterialStudio.h"
#include "physics/editing/PhysicsTarget.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// editor_extension_v2.cpp
namespace test_editor_extension_v2 {
// editor_extension_v2.cpp
using namespace eve::editor;

namespace {

class ParkBuilderExtension final : public IGameEditorExtension {
public:
    std::string ownerModule() const override { return "park"; }

    void registerEditor(IEditorExtensionRegistry& registry) override {
        CommandDescriptor append;
        append.id               = CommandId("park.track.append-segment");
        append.ownerModule      = "spoofed-owner";
        append.displayName      = "Append Track Segment";
        append.category         = "park.track";
        append.requiredFeatures = HostFeature::RuntimeWorld;
        CHECK(registry
                  .registerCommand(
                      append,
                      [&](const CommandContext&, const EditorValue& payload) {
                          ++calls;
                          return eve::editing::applied<EditorValue>(payload);
                      },
                      ExtensionAudience::Developer | ExtensionAudience::Player | ExtensionAudience::Automation)
                  .ok());

        ExtensionToolDescriptor tool;
        tool.id                 = ToolId("park.track.build");
        tool.labelKey           = "editor.park.track.build";
        tool.category           = "park.track";
        tool.targetRequirements = {CapabilityId("park.target.track-network")};
        tool.audiences          = ExtensionAudience::Developer | ExtensionAudience::Player;
        CHECK(registry.registerTool(std::move(tool)).ok());

        ExtensionPaletteDescriptor developer;
        developer.id        = StableId("park.level-design-palette");
        developer.titleKey  = "editor.palette.level.rides";
        developer.tools     = {ToolId("park.track.build")};
        developer.commands  = {CommandId("park.track.append-segment")};
        developer.audiences = ExtensionAudience::Developer;
        CHECK(registry.registerPalette(std::move(developer)).ok());

        ExtensionPaletteDescriptor player;
        player.id        = StableId("park.build-rides-palette");
        player.titleKey  = "game.build.rides";
        player.tools     = {ToolId("park.track.build")};
        player.commands  = {CommandId("park.track.append-segment")};
        player.audiences = ExtensionAudience::Player;
        CHECK(registry.registerPalette(std::move(player)).ok());

        ExtensionRuleDescriptor budget;
        budget.id        = RuleId("park.policy.build-cost");
        budget.audiences = ExtensionAudience::Player | ExtensionAudience::Automation;
        CHECK(registry.registerRule(std::move(budget)).ok());
    }

    int calls = 0;
};

class ThrowingExtension final : public IGameEditorExtension {
public:
    std::string ownerModule() const override { return "throwing"; }
    void        registerEditor(IEditorExtensionRegistry& registry) override {
        CommandDescriptor command;
        command.id = CommandId("throwing.partial");
        CHECK(registry
                         .registerCommand(
                      command,
                      [](const CommandContext&, const EditorValue&) {
                          return eve::editing::applied<EditorValue>(EditorValue{});
                      },
                      ExtensionAudience::Developer)
                         .ok());
        throw std::runtime_error("extension load failed");
    }
};

}  // namespace

TEST_CASE("editor.v2.game_extension_injects_same_tool_into_editor_and_game") {
    EditorCommandService    commands;
    EditorExtensionRegistry extensions(&commands);
    ParkBuilderExtension    park;
    CHECK(extensions.load(park).ok());

    HostProfile developer = HostProfile::developer();
    HostProfile runtime   = HostProfile::runtimeBuilder();
    extensions.configureProfile(runtime);
    CHECK(runtime.allowsCommand(CommandId("park.track.append-segment")));

    auto developerTools = extensions.tools(developer);
    auto runtimeTools   = extensions.tools(runtime);
    CHECK_EQ(developerTools.size(), static_cast<std::size_t>(1));
    CHECK_EQ(runtimeTools.size(), static_cast<std::size_t>(1));
    CHECK(developerTools.front().id == runtimeTools.front().id);
    CHECK_EQ(extensions.palettes(developer).front().id.value(), std::string("park.level-design-palette"));
    CHECK_EQ(extensions.palettes(runtime).front().id.value(), std::string("park.build-rides-palette"));
    CHECK_EQ(extensions.rules(developer).size(), static_cast<std::size_t>(0));
    CHECK_EQ(extensions.rules(runtime).size(), static_cast<std::size_t>(1));

    const CommandDescriptor* registered = commands.find(CommandId("park.track.append-segment"));
    CHECK(registered != nullptr);
    CHECK_EQ(registered->ownerModule, std::string("park"));

    CommandContext context;
    context.profile = &runtime;
    auto result     = commands.execute(CommandId("park.track.append-segment"), context, EditorValue("segment"));
    CHECK(result.ok());
    CHECK_EQ(park.calls, 1);

    CHECK(extensions.unload("park") > 0);
    CHECK(commands.find(CommandId("park.track.append-segment")) == nullptr);
    CHECK(extensions.tools(developer).empty());
}

TEST_CASE("editor.v2.extension_load_exception_rolls_back_partial_registration") {
    EditorCommandService    commands;
    EditorExtensionRegistry extensions(&commands);
    ThrowingExtension       throwing;
    auto                    result = extensions.load(throwing);
    CHECK_EQ(static_cast<int>(result.code()), static_cast<int>(EditorStatus::Rejected));
    CHECK(commands.find(CommandId("throwing.partial")) == nullptr);
}

// editor_field_tool_script.cpp
TEST_CASE("editor.script.composes_heightmap_target_field_tool_and_transaction") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        editor <- eve.Editor();
        heightmapTargets <- eve.HeightmapTargetModule();
        procgen <- eve.Procgen();
        heightmapResult <- procgen.newHeightmap(8, 8);
        heightmap <- heightmapResult.ok ? heightmapResult.value : null;
        target <- heightmapTargets.create("terrain", heightmap);
        falloff <- editor.newSmoothBrushFalloff();
        kernel <- editor.newCircleBrushKernel();
        kernel.setSmoothFalloff(falloff);
        operation <- editor.newAddScalarFieldOperation();
        tool <- editor.newFieldBrushTool("terrain.raise", "Raise Terrain");
        tool.setCircleKernel(kernel);
        tool.setAddScalarOperation(operation);
        tool.setRadius(1.5);
        tool.setStrength(0.25);
        session <- editor.newSession();
        added <- session.addFieldTool(tool);
        heightmapTargets.bind(session, target);
        activated <- session.activateTool("terrain.raise");
        down <- session.dispatchPointer(0, 1, 0, 3.0, 4.0, 0.0, 0.0, 1.0);
        up <- session.dispatchPointer(2, 1, 0, 3.0, 4.0, 0.0, 0.0, 1.0);
        raised <- target.readScalar(3, 4);
        revisionAfterStroke <- target.getRevision();
        undone <- session.undo();
        restored <- target.readScalar(3, 4);
        redone <- session.redo();
        replayed <- target.readScalar(3, 4);
        encoded <- heightmapTargets.encodeDocument(heightmap, 0.5, 0.5);
        target.writeScalar(3, 4, -2.0);
        decoded <- heightmapTargets.decodeDocument(encoded.value, heightmap);
        documentRestored <- target.readScalar(3, 4);
    )"));

    CHECK(vm.find("added").toBool());
    CHECK(vm.find("activated").toBool());
    CHECK((vm.find("down").toInt() & 2) != 0);
    CHECK((vm.find("up").toInt() & 4) != 0);
    CHECK_GT(vm.find("raised").toFloat(), 0.2f);
    CHECK_GT(vm.find("revisionAfterStroke").toInt(), 0);
    CHECK(vm.find("undone").toBool());
    CHECK_EQ(vm.find("restored").toFloat(), 0.f);
    CHECK(vm.find("redone").toBool());
    CHECK_GT(vm.find("replayed").toFloat(), 0.2f);
    CHECK(vm.find("encoded").toTable().find("ok").toBool());
    CHECK(vm.find("decoded").toTable().find("ok").toBool());
    CHECK_GT(vm.find("documentRestored").toFloat(), 0.2f);
}

// editor_gizmo_preview.cpp
using namespace eve::domain_gizmo_editing;
using namespace eve::editing;

namespace {
SelectionSnapshot selection(const char* target) {
    SelectionSnapshot result;
    result.items.push_back({SelectionDomain::Custom, TargetId(target), StableId("self"), "object"});
    return result;
}

void set(IPropertyProvider& provider, IDomainOperationTarget& target, const SelectionSnapshot& selected,
         const char* path, EditorValue value) {
    auto operation = provider.makeSet(selected, PropertyPath(path), value, PropertySetMode::Absolute);
    REQUIRE(operation.ok());
    CHECK(target.applyDomainOperation(operation.value()).ok());
}
}  // namespace

TEST_CASE("editor.gizmo.builds_physics_audio_and_light_overlays") {
    eve::domain_gizmo_editing::EditorGizmoPreviewBuilder builder;
    PhysicsColliderTarget                                collider("shape", 3);
    set(collider, collider, selection("shape"), "shape.kind", "capsule");
    set(collider, collider, selection("shape"), "shape.radius", 0.75);
    set(collider, collider, selection("shape"), "shape.capsule-height", 2.0);
    auto colliderGizmo = builder.collider(collider);
    CHECK_EQ(static_cast<int>(colliderGizmo.status), static_cast<int>(EditorStatus::Applied));
    REQUIRE_EQ(colliderGizmo.primitives.size(), 1U);
    CHECK_EQ(colliderGizmo.primitives[0].kind, "wire-capsule");

    PhysicsJointTarget joint("hinge");
    set(joint, joint, selection("hinge"), "body.a", "body-a");
    set(joint, joint, selection("hinge"), "body.b", "body-b");
    auto jointGizmo = builder.joint(joint, [](const std::string& body) {
        return eve::editing::applied<std::array<double, 3>>(body == "body-a" ? std::array<double, 3>{0.0, 0.0, 0.0}
                                                                             : std::array<double, 3>{2.0, 0.0, 0.0});
    });
    CHECK_EQ(static_cast<int>(jointGizmo.status), static_cast<int>(EditorStatus::Applied));
    CHECK_EQ(jointGizmo.primitives.size(), 4U);
    CHECK_EQ(jointGizmo.primitives[2].length, 2.0);

    AudioSourceTarget audio("speaker");
    set(audio, audio, selection("speaker"), "spatial.reference-distance", 2.0);
    set(audio, audio, selection("speaker"), "spatial.maximum-distance", 12.0);
    auto audioGizmo = builder.audioSource(audio);
    CHECK_EQ(static_cast<int>(audioGizmo.status), static_cast<int>(EditorStatus::Applied));
    CHECK_EQ(audioGizmo.primitives.size(), 3U);
    CHECK_EQ(audioGizmo.primitives[1].radius, 12.0);

    Light3DDocumentTarget light("sun");
    set(light, light, selection("sun"), "light.type", "dir");
    auto lightGizmo = builder.light(light);
    CHECK_EQ(static_cast<int>(lightGizmo.status), static_cast<int>(EditorStatus::Applied));
    REQUIRE_EQ(lightGizmo.primitives.size(), 1U);
    CHECK_EQ(lightGizmo.primitives[0].kind, "arrow");
}

// editor_import_coordinator.cpp
using namespace eve::editor;

namespace {
ImportProduct product(const ImportTicket& ticket) {
    ImportProduct result;
    result.record.guid            = ticket.asset;
    result.record.logicalUri      = "asset://animations/walk";
    result.record.typeId          = "animation-clip";
    result.record.sourceUri       = "source://walk.fbx";
    result.record.sourceHash      = ticket.sourceHash;
    result.record.importerId      = ticket.importerId;
    result.record.importerVersion = ticket.importerVersion;
    result.record.artifacts       = {"cache://walk.eva"};
    return result;
}
}  // namespace

TEST_CASE("editor.import.generation_tickets_reject_stale_or_incomplete_worker_products") {
    MemoryAssetDatabase database;
    ImportCoordinator   coordinator(&database);
    auto                first  = coordinator.begin(AssetGuid("walk"), "hash-a", "fbx-animation", 2);
    auto                latest = coordinator.begin(AssetGuid("walk"), "hash-b", "fbx-animation", 2);
    REQUIRE(first.ok());
    REQUIRE(latest.ok());
    CHECK_EQ(static_cast<int>(coordinator.publish(first.value(), product(first.value())).code()),
             static_cast<int>(EditorStatus::Conflict));
    ImportProduct incomplete = product(latest.value());
    incomplete.record.artifacts.clear();
    CHECK_EQ(static_cast<int>(coordinator.publish(latest.value(), std::move(incomplete)).code()),
             static_cast<int>(EditorStatus::Rejected));
    auto published = coordinator.publish(latest.value(), product(latest.value()));
    REQUIRE(published.ok());
    CHECK_EQ(published.value().sourceHash, "hash-b");
    REQUIRE(database.find(AssetGuid("walk")).ok());
}

// editor_input_map_target.cpp
using namespace eve::input_editing;
namespace {
void apply(InputMapTarget& t, Result<DomainOperation> op) {
    REQUIRE(op.ok());
    REQUIRE(t.applyDomainOperation(op.value()).ok());
}
}  // namespace
TEST_CASE("editor.input_map.evaluates_composite_axis_deadzone_and_buttons") {
    InputMapTarget target("game.input");
    apply(target, target.makeCreateAction({ObjectId("move"), "move-x", "axis1d"}));
    apply(target, target.makeCreateAction({ObjectId("jump"), "jump", "button"}));
    apply(target, target.makeCreateBinding({ObjectId("left"), ObjectId("move"), "keyboard", "A", -1, 0, false}));
    apply(target, target.makeCreateBinding({ObjectId("right"), ObjectId("move"), "keyboard", "D", 1, 0, false}));
    apply(target, target.makeCreateBinding({ObjectId("stick"), ObjectId("move"), "gamepad", "leftx", 1, .2, false}));
    apply(target, target.makeCreateBinding({ObjectId("space"), ObjectId("jump"), "keyboard", "Space", 1, 0, false}));
    InputMapEvaluator evaluator;
    auto              result =
        evaluator.evaluate(target, {{"keyboard", "D", 1}, {"gamepad", "leftx", .6}, {"keyboard", "Space", 1}});
    REQUIRE(result.ok());
    CHECK_EQ(result.value().at("move-x"), 1.0);
    CHECK_EQ(result.value().at("jump"), 1.0);
}
TEST_CASE("editor.input_map.rejects_dangling_delete_and_captures_intentional_input") {
    InputMapTarget target("game.input");
    apply(target, target.makeCreateAction({ObjectId("jump"), "jump", "button"}));
    apply(target, target.makeCreateBinding({ObjectId("space"), ObjectId("jump"), "keyboard", "Space", 1, 0, false}));
    CHECK_EQ(static_cast<int>(target.makeDeleteAction(ObjectId("jump")).code()),
             static_cast<int>(EditorStatus::Rejected));
    InputBindingCapture capture;
    capture.begin("gamepad");
    CHECK(!capture.feed({"keyboard", "A", 1}));
    CHECK(!capture.feed({"gamepad", "leftx", .2}));
    auto captured = capture.feed({"gamepad", "leftx", .8});
    REQUIRE(captured);
    CHECK_EQ(captured->control, std::string("leftx"));
    CHECK(!capture.active());
}
TEST_CASE("editor.input_map.snapshot_load_is_atomic") {
    InputMapTarget target("game.input");
    apply(target, target.makeCreateAction({ObjectId("jump"), "jump", "button"}));
    const auto  before  = target.snapshotValue();
    EditorValue invalid = before;
    auto*       root    = invalid.getIf<EditorValue::Object>();
    auto*       content = (*root)["content"].getIf<EditorValue::Object>();
    auto*       actions = (*content)["actions"].getIf<EditorValue::Array>();
    auto*       first   = (*actions)[0].getIf<EditorValue::Object>();
    (*first)["kind"]    = "vector3";
    CHECK_EQ(static_cast<int>(target.loadSnapshot(invalid).code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(target.snapshotValue(), before);
}

// editor_material_studio.cpp
using namespace eve::editor;

namespace {

class RecordingMaterialSink final : public IMaterialRuntimeSink {
public:
    Result<void> publish(const MaterialDocumentTarget& candidate) override {
        if (reject)
            return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("test.material.reload-rejected"),
                                              "Runtime rejected reloaded material");
        ++publications;
        last = candidate.snapshotValue();
        return eve::editing::applied<void>();
    }

    int         publications = 0;
    bool        reject       = false;
    EditorValue last;
};

class RecordingStudioRenderer final : public IMaterialPreviewRenderer {
public:
    MaterialPreviewRenderResult render(const MaterialPreviewRenderRequest& request) override {
        ++renders;
        revisions.push_back(request.documentRevision);
        snapshots.push_back(request.material);
        return {EditorStatus::Applied, "artifact://material-studio/" + std::to_string(renders), {}};
    }

    int                      renders = 0;
    std::vector<Revision>    revisions;
    std::vector<EditorValue> snapshots;
};

SelectionSnapshot materialSelection(const MaterialDocumentTarget& material) {
    SelectionSnapshot selection;
    selection.channel = "asset";
    SelectionItem item{SelectionDomain::Asset, TargetId(material.targetId()), StableId(material.targetId().value()),
                       "graphics.material"};
    selection.items.push_back(item);
    selection.primary = item;
    return selection;
}

}  // namespace

TEST_CASE("editor.material.studio_coalesces_live_gesture_into_one_transaction") {
    RecordingMaterialSink    sink;
    MaterialPublishingTarget target("studio-metal", &sink);
    LocalWorldAuthority      authority(&target);
    LocalTransactionBackend  transactions(&authority);
    MaterialPreviewService   previews;
    RecordingStudioRenderer  renderer;
    MaterialStudioController studio(DocumentId("material.studio-metal"), target, transactions, previews, renderer);

    REQUIRE(studio.beginInteraction(PropertyPath("shading.roughness")).ok());
    REQUIRE(studio.updateInteraction(0.2).ok());
    REQUIRE(studio.tick(100).ok());
    REQUIRE(studio.updateInteraction(0.35).ok());
    REQUIRE(studio.tick(133).ok());
    REQUIRE(studio.updateInteraction(0.7).ok());
    REQUIRE(studio.tick(166).ok());

    CHECK_EQ(renderer.renders, 3);
    CHECK_EQ(sink.publications, 0);
    CHECK(studio.state().interactionActive);
    const Revision before    = target.revision();
    auto           committed = studio.commitInteraction();
    REQUIRE(committed.ok());
    CHECK_EQ(target.revision(), before + 1);
    CHECK_EQ(sink.publications, 1);
    CHECK(!studio.state().interactionActive);
    CHECK(transactions.canUndo());
    CHECK(target.authoringTarget()
              .read(materialSelection(target.authoringTarget()), PropertyPath("shading.roughness"))
              .value == EditorValue(0.7));

    REQUIRE(transactions.undo().ok());
    CHECK(target.authoringTarget()
              .read(materialSelection(target.authoringTarget()), PropertyPath("shading.roughness"))
              .value == EditorValue(0.45));
}

TEST_CASE("editor.material.studio_cancel_preserves_live_material_and_refreshes_preview") {
    RecordingMaterialSink    sink;
    MaterialPublishingTarget target("studio-cancel", &sink);
    LocalWorldAuthority      authority(&target);
    LocalTransactionBackend  transactions(&authority);
    MaterialPreviewService   previews;
    RecordingStudioRenderer  renderer;
    MaterialStudioController studio(DocumentId("material.studio-cancel"), target, transactions, previews, renderer);

    const Revision original = target.revision();
    REQUIRE(studio.beginInteraction(PropertyPath("shading.metallic")).ok());
    REQUIRE(studio.updateInteraction(0.9).ok());
    REQUIRE(studio.refreshPreview().ok());
    REQUIRE(studio.cancelInteraction().ok());
    REQUIRE(studio.refreshPreview().ok());
    CHECK_EQ(target.revision(), original);
    CHECK_EQ(sink.publications, 0);
    CHECK_EQ(renderer.renders, 2);
    CHECK_EQ(studio.state().previewRevision, original);
}

TEST_CASE("editor.material.hot_reload_publishes_candidate_atomically") {
    RecordingMaterialSink    sink;
    MaterialPublishingTarget target("hot-leaf", &sink);
    MaterialDocumentTarget   source("hot-leaf");
    const SelectionSnapshot  selection = materialSelection(source);
    auto                     operation =
        source.makeSet(selection, PropertyPath("vegetation.alpha.glancing"), 0.65, PropertySetMode::Absolute);
    REQUIRE(operation.ok());
    REQUIRE(source.applyDomainOperation(operation.value()).ok());

    const Revision before = target.revision();
    sink.reject           = true;
    REQUIRE(!target.reloadSnapshot(source.snapshotValue()).ok());
    CHECK_EQ(target.revision(), before);
    CHECK(target.authoringTarget()
              .read(materialSelection(target.authoringTarget()), PropertyPath("vegetation.alpha.glancing"))
              .value == EditorValue(0.0));

    sink.reject = false;
    REQUIRE(target.reloadSnapshot(source.snapshotValue()).ok());
    CHECK_EQ(target.revision(), before + 1);
    CHECK_EQ(sink.publications, 1);
    CHECK(target.authoringTarget()
              .read(materialSelection(target.authoringTarget()), PropertyPath("vegetation.alpha.glancing"))
              .value == EditorValue(0.65));
}

TEST_CASE("editor.material.studio_rate_limits_with_injected_monotonic_time") {
    RecordingMaterialSink    sink;
    MaterialPublishingTarget target("studio-rate", &sink);
    LocalWorldAuthority      authority(&target);
    LocalTransactionBackend  transactions(&authority);
    MaterialPreviewService   previews;
    RecordingStudioRenderer  renderer;
    MaterialStudioController studio(DocumentId("material.studio-rate"), target, transactions, previews, renderer);

    REQUIRE(studio.setPreviewRate(20.0).ok());
    REQUIRE(studio.beginInteraction(PropertyPath("shading.roughness")).ok());
    REQUIRE(studio.updateInteraction(0.6).ok());
    REQUIRE(studio.tick(1000).ok());
    REQUIRE(studio.updateInteraction(0.61).ok());
    CHECK_EQ(static_cast<int>(studio.tick(1020).code()), static_cast<int>(EditorStatus::NoOp));
    REQUIRE(studio.tick(1050).ok());
    CHECK_EQ(renderer.renders, 2);
    REQUIRE(studio.updateInteraction(0.62).ok());
    CHECK_EQ(static_cast<int>(studio.tick(1049).code()), static_cast<int>(EditorStatus::Rejected));
}
}  // namespace test_editor_extension_v2
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_editor_fluid_target)
#include "PrivateTestScope.h"
#include "fluids/VolumeFluid.h"
#include "fluids/editing/FluidTarget.h"
#include "fluids/editing/VolumeFluidTarget.h"
#include "fluids/editor/FluidsEditorModule.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// editor_fluid_target.cpp
namespace test_editor_fluid_target {
// editor_fluid_target.cpp
using namespace eve::fluids_editing;
using namespace eve::editing;

TEST_CASE("editor.fluids.settings_are_reversible_persistent_and_budgeted") {
    FluidSimulationTarget fluid("waterfall");
    SelectionSnapshot     selection;
    selection.items.push_back({SelectionDomain::Asset, TargetId("waterfall"), StableId("settings"), "fluid"});
    auto radius = fluid.makeSet(selection, PropertyPath("particleRadius"), 0.08, PropertySetMode::Absolute);
    REQUIRE(radius.ok());
    CHECK(fluid.applyDomainOperation(radius.value()).ok());
    auto support = fluid.makeSet(selection, PropertyPath("supportRadius"), 0.32, PropertySetMode::Absolute);
    REQUIRE(support.ok());
    CHECK(fluid.applyDomainOperation(support.value()).ok());
    CHECK(fluid.validate().empty());
    const auto preview = fluid.previewBudget();
    CHECK_EQ(static_cast<int>(preview.status), static_cast<int>(EditorStatus::Applied));
    CHECK(preview.estimatedBytes > 0U);
    CHECK(preview.estimatedNeighborChecks > 0U);
    DomainOperation undo = support.value();
    undo.payload         = support.value().inverse;
    CHECK(fluid.applyDomainOperation(undo).ok());
    CHECK_EQ(fluid.settings().supportRadius, 0.20);
    FluidSimulationTarget restored("waterfall");
    CHECK(restored.loadSnapshot(fluid.snapshotValue()).ok());
    CHECK_EQ(restored.snapshotValue(), fluid.snapshotValue());
    CHECK_EQ(static_cast<int>(fluid.previewBudget(1, 1).status), static_cast<int>(EditorStatus::Rejected));
}

// editor_fluids_module.cpp
using namespace eve::editor;
using namespace eve::fluids_editor;

TEST_CASE("editor.fluids_factory_creates_all_schema_targets") {
    FluidsAutomationTargetFactory factory;
    for (const auto* type : {"fluid-simulation", "surface-fluid", "volume-fluid"}) {
        REQUIRE(factory.supports(type));
        auto created = factory.create(TargetId(std::string("target-") + type), type, {});
        REQUIRE(created.ok());
        REQUIRE(created.value().target != nullptr);
        CHECK_EQ(created.value().target->describe().type, std::string(type));
    }
    CHECK(!factory.supports("unknown"));
    CHECK_EQ(static_cast<int>(factory.create(TargetId("unknown"), "unknown", {}).code()),
             static_cast<int>(EditorStatus::Unsupported));
}

TEST_CASE("editor.fluids_factory_loads_volume_snapshot_atomically") {
    eve::fluids_editing::VolumeFluidTarget source("source");
    EditorValue::Object                    request;
    request.emplace("snapshot", source.snapshotValue());

    FluidsAutomationTargetFactory factory;
    auto                          created = factory.create(TargetId("volume"), "volume-fluid", request);
    REQUIRE(created.ok());
    auto* volume = dynamic_cast<eve::fluids_editing::VolumeFluidTarget*>(created.value().target.get());
    REQUIRE(volume != nullptr);
    CHECK_EQ(volume->snapshotValue(), source.snapshotValue());

    auto* root = request.at("snapshot").getIf<EditorValue::Object>();
    REQUIRE(root != nullptr);
    (*root)["unknown"] = true;
    CHECK_EQ(static_cast<int>(factory.create(TargetId("bad"), "volume-fluid", request).code()),
             static_cast<int>(EditorStatus::Unsupported));
}

// editor_volume_fluid_target.cpp
using namespace eve::editing;
using namespace eve::fluids_editing;

namespace {
SelectionSnapshot selected(const char* target) {
    SelectionSnapshot value;
    value.items.push_back({SelectionDomain::Asset, TargetId(target), StableId("volume"), "fluids.volume"});
    return value;
}
}  // namespace

TEST_CASE("editor.volume_fluid_schema_edits_are_reversible_and_budgeted") {
    VolumeFluidTarget target("tank");
    const auto        selection = selected("tank");
    CHECK_EQ(target.schema(selection).properties.size(), static_cast<std::size_t>(7));
    auto iterations = target.makeSet(selection, PropertyPath("iterations"), std::int64_t{2}, PropertySetMode::Absolute);
    REQUIRE(iterations.ok());
    REQUIRE(target.applyDomainOperation(iterations.value()).ok());
    CHECK_EQ(target.settings().iterations, 2U);
    CHECK_EQ(static_cast<int>(target.previewBudget().status), static_cast<int>(EditorStatus::Applied));
    DomainOperation undo = iterations.value();
    undo.payload         = iterations.value().inverse;
    REQUIRE(target.applyDomainOperation(undo).ok());
    CHECK_EQ(target.settings().iterations, 5U);
    CHECK_EQ(static_cast<int>(target.previewBudget(1, 1).status), static_cast<int>(EditorStatus::Rejected));
}

TEST_CASE("editor.volume_fluid_snapshot_is_strict_and_atomic") {
    VolumeFluidTarget target("tank");
    EditorValue       snapshot = target.snapshotValue();
    auto*             root     = snapshot.getIf<EditorValue::Object>();
    REQUIRE(root != nullptr);
    (*root)["unknown"] = true;
    const auto before  = target.settings();
    CHECK_EQ(static_cast<int>(target.loadSnapshot(snapshot).code()), static_cast<int>(EditorStatus::Unsupported));
    CHECK_EQ(target.settings().capacity, before.capacity);
    CHECK_EQ(target.settings().spacing, before.spacing);
    VolumeFluidTarget restored("restored");
    REQUIRE(restored.loadSnapshot(target.snapshotValue()).ok());
    CHECK_EQ(restored.snapshotValue(), target.snapshotValue());
}

TEST_CASE("editor.volume_fluid_runtime_publish_uses_atomic_restore") {
    eve::fluids::VolumeFluidSettings settings;
    settings.capacity = 16;
    auto created      = eve::fluids::VolumeFluid::create(settings);
    REQUIRE(created.ok());
    VolumeFluidTarget target("tank");
    const auto        selection = selected("tank");
    auto iterations = target.makeSet(selection, PropertyPath("iterations"), std::int64_t{2}, PropertySetMode::Absolute);
    REQUIRE(iterations.ok());
    REQUIRE(target.applyDomainOperation(iterations.value()).ok());
    REQUIRE(VolumeFluidRuntimeApplier().apply(target, created.value().get()).ok());
    const auto applied = created.value()->snapshot();
    CHECK_EQ(applied.settings.capacity, 8192U);
    CHECK_EQ(applied.settings.iterations, 2U);
    CHECK_EQ(applied.settings.spacing, 0.1f);
    CHECK_EQ(static_cast<int>(VolumeFluidRuntimeApplier().apply(target, nullptr).code()),
             static_cast<int>(EditorStatus::Rejected));
}
}  // namespace test_editor_fluid_target
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_editor_gpuagents)
#include "PrivateTestScope.h"
#include <cmath>
#include <vector>
#include "gpuagents/EffectBackend.h"
#include "gpuagents/GpuAgentWorld.h"
#include "gpuagents/LifeFieldMaterialBinding.h"
#include "gpuagents/SurfaceCapture.h"
#include "gpuagents/editing/GpuAgentsDocument.h"
#include "gpuagents/editor/GpuAgentsPreview.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// editor_gpuagents.cpp
namespace test_editor_gpuagents {
using namespace eve::gpuagents;
using namespace eve::gpuagents_editing;
using namespace eve::editing;

namespace {

SelectionSnapshot selection(const char* target) {
    SelectionSnapshot value;
    value.items.push_back({SelectionDomain::Asset, TargetId(target), StableId("effect"), "gpuagents.effect"});
    return value;
}

}  // namespace

TEST_CASE("gpuagents.surface_capture_builds_height_and_normals") {
    // Ramp triangle covering the domain center.
    std::vector<glm::vec3> positions = {
        {-4.f, 0.f, -4.f},
        {4.f, 2.f, -4.f},
        {0.f, 1.f, 4.f},
    };
    std::vector<std::uint32_t> indices = {0, 1, 2};
    SurfaceField               field;
    REQUIRE(SurfaceCapture::captureFromTriangles(field, positions, indices, glm::vec3(-8.f, 0.f, -8.f), 16.f, 33).ok());
    CHECK_EQ(field.resolution, 33);
    CHECK(field.sampleHeight(0.f, 0.f) > 0.2f);
    const glm::vec3 n = field.sampleNormal(0.f, 0.f);
    CHECK(n.y > 0.5f);
}

TEST_CASE("gpuagents.life_field_material_binding_packs_rgba") {
    SurfaceField field;
    field.initFlat(glm::vec3(-4.f, 0.f, -4.f), 8.f, 16, 0.f);
    field.lifeField[static_cast<size_t>(8 + 16 * 8)] = glm::vec4(1.f, 0.5f, 0.25f, 1.f);
    LifeFieldMaterialBinding binding;
    REQUIRE(binding.syncFrom(field).ok());
    CHECK_EQ(binding.uniforms().resolution, 16);
    CHECK_EQ(binding.lifeFieldPixels().size(), static_cast<size_t>(16 * 16 * 4));
    CHECK_EQ(binding.surfaceDataPixels().size(), static_cast<size_t>(16 * 16 * 4));
    const size_t p = static_cast<size_t>(8 + 16 * 8) * 4u;
    CHECK(binding.lifeFieldPixels()[p] >= 250);  // trail ~1
}

TEST_CASE("editor.gpuagents_document_schema_set_undo_and_runtime_apply") {
    GpuAgentsDocumentTarget target("school");
    const auto              selected = selection("school");
    CHECK(target.schema(selected).properties.size() >= 20u);
    auto setKind =
        target.makeSet(selected, PropertyPath("kind"), std::string("LifeNetwork"), PropertySetMode::Absolute);
    REQUIRE(setKind.ok());
    REQUIRE(target.applyDomainOperation(setKind.value()).ok());
    CHECK_EQ(target.settings().kind, std::string("LifeNetwork"));
    auto setDeposit = target.makeSet(selected, PropertyPath("depositStrength"), 0.9, PropertySetMode::Absolute);
    REQUIRE(setDeposit.ok());
    REQUIRE(target.applyDomainOperation(setDeposit.value()).ok());
    CHECK_EQ(target.settings().depositStrength, 0.9);
    DomainOperation undo = setDeposit.value();
    undo.payload         = setDeposit.value().inverse;
    REQUIRE(target.applyDomainOperation(undo).ok());
    CHECK_EQ(target.settings().depositStrength, 0.35);

    GpuAgentWorld           world;
    EffectBackend           backend;
    GpuAgentsRuntimeApplier applier;
    REQUIRE(applier.apply(target, &backend, &world).ok());
    CHECK_EQ(static_cast<int>(backend.kind()), static_cast<int>(EffectKind::LifeNetwork));
    CHECK_EQ(world.surface().resolution, static_cast<int>(target.settings().fieldResolution));
}

TEST_CASE("editor.gpuagents_preview_steps_life_network") {
    GpuAgentsDocumentTarget target("life");
    const auto              selected = selection("life");
    auto                    setKind =
        target.makeSet(selected, PropertyPath("kind"), std::string("LifeNetwork"), PropertySetMode::Absolute);
    REQUIRE(setKind.ok());
    REQUIRE(target.applyDomainOperation(setKind.value()).ok());
    auto setRes = target.makeSet(selected, PropertyPath("fieldResolution"), static_cast<std::int64_t>(32),
                                 PropertySetMode::Absolute);
    REQUIRE(setRes.ok());
    REQUIRE(target.applyDomainOperation(setRes.value()).ok());

    eve::gpuagents_editor::GpuAgentsPreviewService preview;
    eve::gpuagents_editor::GpuAgentsPreviewRequest request;
    request.agentCount = 16;
    request.seconds    = 0.5;
    request.fixedStep  = 1.0 / 60.0;
    auto snap          = preview.build(target, request);
    CHECK_EQ(static_cast<int>(snap.status), static_cast<int>(EditorStatus::Applied));
    CHECK(snap.agents.size() == 16u);
    CHECK(snap.simulatedSeconds > 0.4);
    CHECK(snap.lifeTrailEnergy > 0.f);
}
}  // namespace test_editor_gpuagents
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_editor_housegen_target)
#include "PrivateTestScope.h"
#include "procgen/editing/HouseGenTarget.h"
#include "procgen/house/HouseLayout.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// editor_housegen_target.cpp
namespace test_editor_housegen_target {
using namespace eve::housegen_editing;
using namespace eve::editing;

namespace {
void apply(HouseGenDocumentTarget& target, Result<DomainOperation> operation) {
    REQUIRE(operation.ok());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
}
HouseKitComponentValue component(const char* id, const char* category) {
    HouseKitComponentValue value;
    value.id                  = ObjectId(std::string("editor-") + id);
    value.component.id        = id;
    value.component.modelPath = std::string("fixtures/") + id + ".glb";
    value.component.category  = category;
    return value;
}
void completeKit(HouseGenDocumentTarget& target) {
    apply(target, target.makeCreateComponent(component("foundation", "foundation")));
    apply(target, target.makeCreateComponent(component("floor", "floor")));
    apply(target, target.makeCreateComponent(component("wall", "wall")));
    apply(target, target.makeCreateComponent(component("door", "door")));
    apply(target, target.makeCreateComponent(component("roof", "roof")));
}
SelectionSnapshot select(const HouseGenDocumentTarget& target, const char* id) {
    SelectionSnapshot s;
    s.channel = "housegen";
    s.items.push_back({SelectionDomain::Asset, TargetId(target.targetId()), StableId(id), "housegen.component"});
    return s;
}
}  // namespace

TEST_CASE("editor.housegen.kit_supports_incremental_authoring_properties_and_snapshot") {
    HouseGenDocumentTarget target("kit");
    apply(target, target.makeCreateComponent(component("wall", "wall")));
    CHECK(!target.validate().empty());
    auto selection = select(target, "editor-wall");
    auto edit      = target.makeSet(selection, PropertyPath("component.weight"), int64_t{5}, PropertySetMode::Absolute);
    REQUIRE(edit.ok());
    REQUIRE(target.applyDomainOperation(edit.value()).ok());
    CHECK_EQ(*target.read(selection, PropertyPath("component.weight")).value.getIf<int64_t>(), int64_t{5});
    DomainOperation undo = edit.value();
    undo.payload         = edit.value().inverse;
    REQUIRE(target.applyDomainOperation(undo).ok());
    CHECK_EQ(*target.read(selection, PropertyPath("component.weight")).value.getIf<int64_t>(), int64_t{1});
    HouseGenDocumentTarget restored("copy");
    REQUIRE(restored.loadSnapshot(target.snapshotValue()).ok());
    CHECK_EQ(restored.components().size(), static_cast<std::size_t>(1));
}

TEST_CASE("editor.housegen.preview_is_deterministic_revision_safe_and_failure_atomic") {
    HouseGenDocumentTarget target("kit");
    completeKit(target);
    eve::housegen::HouseRequest request;
    request.seed   = 42;
    request.width  = 5;
    request.depth  = 4;
    request.floors = 2;
    apply(target, target.makeSetRequest(request));
    HouseGenPreviewRuntime runtime;
    REQUIRE(runtime.publish(target).ok());
    const std::string first    = runtime.layout()->toJson();
    const auto        revision = runtime.revision();
    REQUIRE(runtime.publish(target).ok());
    CHECK_EQ(runtime.layout()->toJson(), first);
    auto overlay = runtime.gizmo(revision);
    REQUIRE(overlay.ok());
    CHECK(!overlay.value().primitives.empty());
    apply(target, target.makeDeleteComponent(ObjectId("editor-roof")));
    CHECK_EQ(static_cast<int>(runtime.publish(target).code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(runtime.layout()->toJson(), first);
    CHECK_EQ(runtime.revision(), revision);
    CHECK_EQ(static_cast<int>(runtime.gizmo(target.revision()).code()), static_cast<int>(EditorStatus::Conflict));
}

TEST_CASE("editor.housegen.invalid_snapshot_does_not_partially_replace_document") {
    HouseGenDocumentTarget target("kit");
    completeKit(target);
    const auto  before   = target.snapshotValue();
    EditorValue invalid  = before;
    auto*       root     = invalid.getIf<EditorValue::Object>();
    auto*       content  = (*root)["content"].getIf<EditorValue::Object>();
    auto*       request  = (*content)["request"].getIf<EditorValue::Object>();
    (*request)["floors"] = int64_t{1000};
    CHECK_EQ(static_cast<int>(target.loadSnapshot(invalid).code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(target.snapshotValue(), before);
}
}  // namespace test_editor_housegen_target
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_editor_localization)
#include "PrivateTestScope.h"
#include <zeroerr/assert.h>
#include <zeroerr/unittest.h>
#include "i18n/editing/LocalizationDocument.h"
#include "profiler/editing/ProfilerModel.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// editor_localization.cpp
namespace test_editor_localization {
// editor_localization.cpp
using namespace eve::localization_editing;

TEST_CASE("editor.localization_reports_completeness_placeholders_and_voice_metadata") {
    LocalizationDocument document;
    REQUIRE(document.addRow("dialogue.greeting", "Hello {name}", "Opening line").ok());
    REQUIRE(
        document
            .setVariant("dialogue.greeting", "zh-CN", {"你好 {name}", "asset://voice/zh/greeting.ogg", "approved", 1.2})
            .ok());
    REQUIRE(document.setVariant("dialogue.greeting", "fr-FR", {"Bonjour", "", "missing", 0.0}).ok());
    const LocalizationAnalysis analysis = document.analyze({"zh-CN", "fr-FR"}, true);
    CHECK_EQ(static_cast<int>(analysis.status), static_cast<int>(EditorStatus::Failed));
    CHECK_EQ(analysis.locales.size(), size_t{2});
    CHECK_EQ(analysis.locales[0].translated, 1);
    CHECK_EQ(analysis.locales[0].voiced, 1);
    CHECK_EQ(analysis.locales[0].approved, 1);
    CHECK(analysis.diagnostics.size() >= size_t{2});
}

TEST_CASE("editor.localization_snapshot_round_trip_is_atomic_and_deterministic") {
    LocalizationDocument source;
    REQUIRE(source.addRow("ui.quit", "Quit").ok());
    REQUIRE(source.setVariant("ui.quit", "zh-CN", {"退出", "", "", 0.0}).ok());
    const EditorValue snapshot = source.snapshotValue();

    LocalizationDocument restored;
    REQUIRE(restored.loadSnapshot(snapshot).ok());
    CHECK_EQ(restored.snapshotValue(), snapshot);
    const Revision before = restored.revision();
    EditorValue    broken = snapshot;
    auto*          object = broken.getIf<EditorValue::Object>();
    REQUIRE(object);
    (*object)["schemaVersion"] = int64_t{99};
    CHECK_EQ(static_cast<int>(restored.loadSnapshot(broken).code()), static_cast<int>(EditorStatus::Unsupported));
    CHECK_EQ(restored.revision(), before);
    CHECK_EQ(restored.snapshotValue(), snapshot);
}

TEST_CASE("editor.localization_rejects_duplicate_keys_and_invalid_voice_status") {
    LocalizationDocument document;
    REQUIRE(document.addRow("line.one", "One").ok());
    CHECK_EQ(static_cast<int>(document.addRow("line.one", "Duplicate").code()),
             static_cast<int>(EditorStatus::Conflict));
    CHECK_EQ(static_cast<int>(document.setVariant("line.one", "en-US", {"One", "", "done", 0.0}).code()),
             static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(static_cast<int>(document.setVariant("missing", "en-US", {"Missing", "", "", 0.0}).code()),
             static_cast<int>(EditorStatus::NotFound));
}

// editor_profiler.cpp
using namespace eve::profiler_editing;

namespace {
EditorProfilerFrame frame(std::uint64_t sequence, double cpuMs, double gpuMs) {
    EditorProfilerFrame value;
    value.sequence           = sequence;
    value.cpuFrameMs         = cpuMs;
    value.gpuFrameMs         = gpuMs;
    value.gpuTimingAvailable = true;
    value.zones              = {{"physics", "step", "main", 5.0, 7.0, 2, 1},
                                {"graphics", "draw", "render", 3.0, 4.0, 1, 1},
                                {"physics", "broadphase", "main", 2.0, 2.0, 1, 2}};
    return value;
}
}  // namespace

TEST_CASE("editor.profiler retains bounded owning history") {
    EditorProfilerModel model;
    auto                configured = model.configure({10.0, 10.0, 4.0, 2});
    REQUIRE(static_cast<int>(configured.code()) == static_cast<int>(EditorStatus::Applied));
    REQUIRE(static_cast<int>(model.ingest(frame(1, 8.0, 7.0)).code()) == static_cast<int>(EditorStatus::Applied));
    REQUIRE(static_cast<int>(model.ingest(frame(2, 12.0, 11.0)).code()) == static_cast<int>(EditorStatus::Applied));
    REQUIRE(static_cast<int>(model.ingest(frame(3, 9.0, 8.0)).code()) == static_cast<int>(EditorStatus::Applied));
    CHECK(model.history().size() == 2);
    CHECK(model.history().front().sequence == 2);

    const auto before = model.history().size();
    CHECK(static_cast<int>(model.ingest(frame(3, 9.0, 8.0)).code()) == static_cast<int>(EditorStatus::Conflict));
    CHECK(model.history().size() == before);
}

TEST_CASE("editor.profiler filters sorts and summarizes latest frame") {
    EditorProfilerModel model;
    REQUIRE(static_cast<int>(model.ingest(frame(1, 12.0, 11.0)).code()) == static_cast<int>(EditorStatus::Applied));

    EditorProfilerQuery query;
    query.module    = "physics";
    query.limit     = 1;
    const auto rows = model.query(query);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].name == "step");

    const auto modules = model.moduleSummary();
    REQUIRE(modules.size() == 2);
    CHECK(modules[0].module == "physics");
    CHECK(modules[0].selfMs == 7.0);
    CHECK(modules[0].count == 3);
}

TEST_CASE("editor.profiler reports configured budget violations") {
    EditorProfilerModel model;
    REQUIRE(static_cast<int>(model.configure({10.0, 10.0, 4.0, 30}).code()) == static_cast<int>(EditorStatus::Applied));
    REQUIRE(static_cast<int>(model.ingest(frame(1, 12.0, 11.0)).code()) == static_cast<int>(EditorStatus::Applied));
    CHECK(model.diagnostics().size() == 3);
}

TEST_CASE("editor.profiler rejects invalid frames atomically") {
    EditorProfilerModel model;
    auto                invalid = frame(1, 1.0, 1.0);
    invalid.zones[0].selfMs     = 8.0;
    CHECK(static_cast<int>(model.ingest(std::move(invalid)).code()) == static_cast<int>(EditorStatus::Rejected));
    CHECK(model.history().empty());
}
}  // namespace test_editor_localization
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_editor_map)
#include "PrivateTestScope.h"
#include <memory>
#include "editor/EditCommand.h"
#include "editor/Editor.h"
#include "map/Map.h"
#include "map/TileLayer.h"
#include "map/editing/TileLayerTarget.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// editor_map.cpp
namespace test_editor_map {
using namespace eve::editor;

TEST_CASE("editor.map.tileLayerTargetUsesLiveRevision") {
    auto*                                              map   = eve::map::Map::create();
    auto*                                              layer = map->newLayer(4, 3, 8.f, 8.f);
    std::unique_ptr<eve::map_editing::TileLayerTarget> target =
        eve::map_editing::createTileLayerTarget("ground", layer);
    const auto before = target->revision();
    CHECK_EQ(static_cast<int>(target->writeInt(2, 1, 9)), static_cast<int>(FieldWriteStatus::Applied));
    CHECK_EQ(layer->getTile(2, 1), 9);
    CHECK_GT(target->revision(), before);
    CHECK_EQ(target->dirtyRegion().minX, 2);
    CHECK_EQ(target->dirtyRegion().maxY, 1);
    IntFieldEditCommand command("paint", target.get());
    CHECK(command.record(2, 1, 4));
    CHECK(command.apply());
    CHECK_EQ(layer->getTile(2, 1), 4);
    command.revert();
    CHECK_EQ(layer->getTile(2, 1), 9);
}
}  // namespace test_editor_map
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_editor_map_document)
#include "PrivateTestScope.h"
#include "map/editing/MapDocument.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// editor_map_document.cpp
namespace test_editor_map_document {
using namespace eve::map_editing;
using namespace eve::editing;

namespace {
class RoadSink final : public IMapRoadMeshSink {
public:
    Result<void> publishRoad(const std::string&, const StableId& road, Revision revision,
                             const EditorValue& mesh) override {
        published         = road;
        publishedRevision = revision;
        value             = mesh;
        return eve::editing::applied<void>();
    }
    Result<void> removeRoad(const std::string&, const StableId&) override { return eve::editing::applied<void>(); }
    StableId     published;
    Revision     publishedRevision = 0;
    EditorValue  value;
};
}  // namespace

TEST_CASE("editor.map.road_mesh_publication_and_object_import_are_revision_safe") {
    MapDocumentTarget map("city");
    auto              roadLayer = map.makeCreateLayer({StableId("roads"), "Roads", "road", true, false, 1.0, 0});
    REQUIRE(roadLayer.ok());
    CHECK(map.applyDomainOperation(roadLayer.value()).ok());
    auto objectLayer = map.makeCreateLayer({StableId("objects"), "Objects", "object", true, false, 1.0, 1});
    REQUIRE(objectLayer.ok());
    CHECK(map.applyDomainOperation(objectLayer.value()).ok());
    MapRoadRecord road{StableId("main"),
                       StableId("roads"),
                       "Main",
                       "asphalt",
                       false,
                       {{StableId("a"), 0.0, 0.0, 0.0, 4.0}, {StableId("b"), 10.0, 0.0, 0.0, 4.0}}};
    auto          setRoad = map.makeSetRoad(road);
    REQUIRE(setRoad.ok());
    CHECK(map.applyDomainOperation(setRoad.value()).ok());
    RoadSink             sink;
    MapRoadMeshPublisher publisher;
    CHECK(publisher.publish(map, StableId("main"), map.revision(), sink).ok());
    CHECK_EQ(sink.published.value(), "main");
    CHECK_EQ(sink.publishedRevision, map.revision());
    CHECK_EQ(static_cast<int>(publisher.publish(map, StableId("main"), map.revision() - 1, sink).code()),
             static_cast<int>(EditorStatus::Conflict));

    MapObjectImporter importer;
    const auto        imported =
        importer.plan(map, StableId("objects"),
                      {{"tree-01", "asset://tree", 2.0, 0.0, 3.0, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0}}, "import/forest");
    CHECK_EQ(static_cast<int>(imported.status), static_cast<int>(EditorStatus::Applied));
    REQUIRE_EQ(imported.operations.size(), 1U);
    CHECK(map.applyDomainOperation(imported.operations[0]).ok());
    CHECK_EQ(map.mapPlacements()[0].id.value(), "import/forest/tree-01");
}

namespace {

DomainOperation inverse(const DomainOperation& source) {
    DomainOperation result = source;
    result.type            = source.inverseType;
    result.payload         = source.inverse;
    return result;
}

void createLayer(MapDocumentTarget& map, MapLayerRecord layer) {
    auto operation = map.makeCreateLayer(layer);
    REQUIRE(operation.ok());
    REQUIRE(map.applyDomainOperation(operation.value()).ok());
}

MapRoadRecord road() {
    return {StableId("main-road"),
            StableId("roads"),
            "Main Road",
            "asset://materials/asphalt",
            false,
            {{StableId("p0"), 0.0, 0.0, 0.0, 4.0},
             {StableId("p1"), 10.0, 0.0, 2.0, 5.0},
             {StableId("p2"), 20.0, 0.0, 0.0, 4.0}}};
}

}  // namespace

TEST_CASE("editor.map.layers_are_ordered_reversible_and_protect_nonempty_content") {
    MapDocumentTarget map("world-map");
    createLayer(map, {StableId("ground"), "Ground", "tile", true, false, 1.0, 0});
    createLayer(map, {StableId("roads"), "Roads", "road", true, false, 0.9, 10});
    createLayer(map, {StableId("objects"), "Objects", "object", true, false, 1.0, 20});
    REQUIRE_EQ(map.mapLayers().size(), size_t{3});
    CHECK_EQ(map.mapLayers()[0].id, StableId("ground"));
    CHECK_EQ(map.mapLayers()[2].id, StableId("objects"));

    MapLayerRecord changed = map.mapLayers()[2];
    changed.name           = "Props";
    changed.visible        = false;
    auto set               = map.makeSetLayer(changed);
    REQUIRE(set.ok());
    REQUIRE(map.applyDomainOperation(set.value()).ok());
    CHECK_EQ(map.mapLayers()[2].name, "Props");
    REQUIRE(map.applyDomainOperation(inverse(set.value())).ok());
    CHECK_EQ(map.mapLayers()[2].name, "Objects");

    auto addRoad = map.makeSetRoad(road());
    REQUIRE(addRoad.ok());
    REQUIRE(map.applyDomainOperation(addRoad.value()).ok());
    CHECK_EQ(static_cast<int>(map.makeDeleteLayer(StableId("roads")).code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(static_cast<int>(
                 map.makeCreateLayer({StableId("duplicate-order"), "Duplicate", "tile", true, false, 1.0, 10}).code()),
             static_cast<int>(EditorStatus::Conflict));
}

TEST_CASE("editor.map.road_spline_and_placements_support_create_update_undo") {
    MapDocumentTarget map("content-map");
    createLayer(map, {StableId("roads"), "Roads", "road", true, false, 1.0, 0});
    createLayer(map, {StableId("objects"), "Objects", "object", true, false, 1.0, 1});
    auto addRoad = map.makeSetRoad(road());
    REQUIRE(addRoad.ok());
    REQUIRE(map.applyDomainOperation(addRoad.value()).ok());
    CHECK_EQ(map.mapRoads()[0].points.size(), size_t{3});
    const auto preview = map.previewRoad(StableId("main-road"));
    CHECK_EQ(static_cast<int>(preview.status), static_cast<int>(EditorStatus::Applied));
    CHECK(preview.centerlineLength > 20.0);
    const auto* mesh = preview.mesh.getIf<EditorValue::Object>();
    REQUIRE(mesh);
    CHECK_EQ(mesh->at("vertices").getIf<EditorValue::Array>()->size(), size_t{6});
    CHECK_EQ(mesh->at("indices").getIf<EditorValue::Array>()->size(), size_t{12});
    CHECK_EQ(static_cast<int>(map.previewRoad(StableId("main-road"), 1).status),
             static_cast<int>(EditorStatus::Rejected));

    MapRoadRecord changed   = map.mapRoads()[0];
    changed.points[1].width = 8.0;
    changed.closed          = true;
    auto updateRoad         = map.makeSetRoad(changed);
    REQUIRE(updateRoad.ok());
    REQUIRE(map.applyDomainOperation(updateRoad.value()).ok());
    CHECK_EQ(map.mapRoads()[0].points[1].width, 8.0);
    REQUIRE(map.applyDomainOperation(inverse(updateRoad.value())).ok());
    CHECK_EQ(map.mapRoads()[0].points[1].width, 5.0);

    MapPlacementRecord tree{StableId("tree-1"),
                            StableId("objects"),
                            "asset://models/tree.evm",
                            4.0,
                            0.0,
                            8.0,
                            0.0,
                            30.0,
                            0.0,
                            1.0,
                            1.0,
                            1.0};
    auto               place = map.makeSetPlacement(tree);
    REQUIRE(place.ok());
    REQUIRE(map.applyDomainOperation(place.value()).ok());
    CHECK_EQ(map.mapPlacements()[0].asset, "asset://models/tree.evm");
    REQUIRE(map.applyDomainOperation(inverse(place.value())).ok());
    CHECK(map.mapPlacements().empty());
}

TEST_CASE("editor.map.locked_layers_reject_content_edits_and_batch_staging_is_atomic") {
    MapDocumentTarget map("locked-map");
    createLayer(map, {StableId("roads"), "Roads", "road", true, true, 1.0, 0});
    CHECK_EQ(static_cast<int>(map.makeSetRoad(road()).code()), static_cast<int>(EditorStatus::Rejected));

    auto  staged    = map.cloneDomainState();
    auto* candidate = dynamic_cast<MapDocumentTarget*>(staged.get());
    REQUIRE(candidate);
    auto unlock = candidate->makeSetLayer({StableId("roads"), "Roads", "road", true, false, 1.0, 0});
    REQUIRE(unlock.ok());
    REQUIRE(candidate->applyDomainOperation(unlock.value()).ok());
    auto addRoad = candidate->makeSetRoad(road());
    REQUIRE(addRoad.ok());
    REQUIRE(candidate->applyDomainOperation(addRoad.value()).ok());
    REQUIRE(map.commitDomainState(std::move(staged)).ok());
    CHECK_EQ(map.mapRoads().size(), size_t{1});
}

TEST_CASE("editor.map.snapshot_round_trip_and_geometry_diagnostics") {
    MapDocumentTarget map("snapshot-map");
    createLayer(map, {StableId("roads"), "Roads", "road", true, false, 1.0, 0});
    createLayer(map, {StableId("objects"), "Objects", "object", true, false, 1.0, 1});
    MapRoadRecord zero = road();
    zero.materialAsset.clear();
    zero.points[1].x = zero.points[0].x;
    zero.points[1].z = zero.points[0].z;
    zero.points[2].x = zero.points[0].x;
    zero.points[2].z = zero.points[0].z;
    auto add         = map.makeSetRoad(zero);
    REQUIRE(add.ok());
    REQUIRE(map.applyDomainOperation(add.value()).ok());
    CHECK_EQ(map.validate().size(), size_t{2});

    const EditorValue snapshot = map.snapshotValue();
    MapDocumentTarget restored("snapshot-map");
    REQUIRE(restored.loadSnapshot(snapshot).ok());
    CHECK_EQ(restored.snapshotValue(), snapshot);
    const Revision before = restored.revision();
    EditorValue    broken = snapshot;
    auto*          root   = broken.getIf<EditorValue::Object>();
    REQUIRE(root);
    (*root)["schemaVersion"] = int64_t{9};
    CHECK_EQ(static_cast<int>(restored.loadSnapshot(broken).code()), static_cast<int>(EditorStatus::Unsupported));
    CHECK_EQ(restored.revision(), before);
    CHECK_EQ(restored.snapshotValue(), snapshot);
}
}  // namespace test_editor_map_document
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_editor_material_batch_target)
#include "PrivateTestScope.h"
#include "graphics/material/editing/MaterialBatchTarget.h"
#include "graphics/material/editor/EditorMaterialPreview.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// editor_material_batch_target.cpp
namespace test_editor_material_batch_target {
// editor_material_batch_target.cpp
using namespace eve::editing;
using namespace eve::material_editing;

namespace {
SelectionSnapshot select(const MaterialBatchTarget& target, std::initializer_list<const char*> ids) {
    SelectionSnapshot result;
    for (const char* id : ids)
        result.items.push_back({SelectionDomain::Asset, target.targetId(), StableId(id), "graphics.material"});
    return result;
}

class BatchSink final : public IMaterialBatchRuntimeSink {
public:
    Result<void> publish(std::span<const MaterialDocumentTarget> candidates) override {
        ++calls;
        observed = candidates.size();
        if (reject)
            return eve::editing::failed<void>(Status::Failed, RuleId("test.material-batch.reject"),
                                              "injected atomic batch rejection");
        return eve::editing::applied<void>();
    }
    bool        reject   = false;
    int         calls    = 0;
    std::size_t observed = 0;
};
}  // namespace

TEST_CASE("editor.material.batch_reports_mixed_and_commits_one_atomic_publication") {
    MaterialDocumentTarget oak("oak");
    MaterialDocumentTarget grass("grass");
    SelectionSnapshot      grassSelection;
    grassSelection.items.push_back({SelectionDomain::Asset, grass.targetId(), StableId("grass"), "graphics.material"});
    auto grassTint = grass.makeSet(grassSelection, PropertyPath("shading.tint"), Value::Array{0.2, 0.4, 0.6, 1.0},
                                   PropertySetMode::Absolute);
    REQUIRE(grassTint.ok());
    REQUIRE(grass.applyDomainOperation(grassTint.value()).ok());

    BatchSink           sink;
    MaterialBatchTarget batch("vegetation-materials", {oak, grass}, &sink);
    const auto          both = select(batch, {"oak", "grass"});
    CHECK_EQ(static_cast<int>(batch.read(both, PropertyPath("shading.tint")).state),
             static_cast<int>(PropertyReadState::Mixed));
    const Value sharedTint = Value::Array{0.3, 0.5, 0.7, 1.0};
    auto        operation  = batch.makeSet(both, PropertyPath("shading.tint"), sharedTint, PropertySetMode::Absolute);
    REQUIRE(operation.ok());
    auto candidate = batch.cloneDomainState();
    REQUIRE(candidate->applyDomainOperation(operation.value()).ok());
    CHECK_EQ(sink.calls, 0);
    REQUIRE(batch.commitDomainState(std::move(candidate)).ok());
    CHECK_EQ(sink.calls, 1);
    CHECK_EQ(sink.observed, 2U);
    const auto read = batch.read(both, PropertyPath("shading.tint"));
    REQUIRE_EQ(static_cast<int>(read.state), static_cast<int>(PropertyReadState::Value));
    CHECK(read.value == sharedTint);
}

TEST_CASE("editor.material.batch_sink_failure_preserves_every_document") {
    BatchSink           sink;
    MaterialBatchTarget batch("vegetation-materials", {MaterialDocumentTarget("oak"), MaterialDocumentTarget("grass")},
                              &sink);
    const auto          both     = select(batch, {"oak", "grass"});
    const auto          before   = batch.snapshotValue();
    const auto          revision = batch.revision();
    auto                operation =
        batch.makeSet(both, PropertyPath("shading.tint"), Value::Array{0.6, 0.5, 0.4, 1.0}, PropertySetMode::Absolute);
    REQUIRE(operation.ok());
    sink.reject = true;
    CHECK(!batch.applyDomainOperation(operation.value()).ok());
    CHECK_EQ(batch.revision(), revision);
    CHECK(batch.snapshotValue() == before);
}

// editor_material_preview.cpp
using namespace eve::editor;

namespace {

class RecordingPreviewRenderer final : public IMaterialPreviewRenderer {
public:
    MaterialPreviewRenderResult render(const MaterialPreviewRenderRequest& request) override {
        ++calls;
        last = request;
        return {status, artifact, {}};
    }

    int                          calls    = 0;
    EditorStatus                 status   = EditorStatus::Applied;
    std::string                  artifact = "artifact://material-preview/one";
    MaterialPreviewRenderRequest last;
};

SelectionSnapshot selection(const MaterialDocumentTarget& material) {
    SelectionSnapshot result;
    result.items.push_back(
        {SelectionDomain::Asset, TargetId(material.targetId()), StableId("material"), "graphics.material"});
    return result;
}

}  // namespace

TEST_CASE("editor.material.preview_uses_immutable_isolated_revisioned_request") {
    MaterialDocumentTarget   material("preview-material");
    RecordingPreviewRenderer renderer;
    MaterialPreviewService   previews;
    MaterialPreviewSettings  settings;
    settings.geometry               = "custom";
    settings.customMeshAsset        = "asset://meshes/helmet.evm";
    const Revision renderedRevision = material.revision();
    auto           task             = previews.render(DocumentId("material-doc"), material, settings, renderer);
    REQUIRE(task.ok());
    CHECK_EQ(renderer.calls, 1);
    CHECK(!renderer.last.sceneId.empty());
    CHECK_EQ(renderer.last.documentRevision, renderedRevision);
    CHECK_EQ(renderer.last.settings.customMeshAsset, "asset://meshes/helmet.evm");
    REQUIRE(renderer.last.material.getIf<EditorValue::Object>());
    REQUIRE(previews.publish(DocumentId("material-doc"), material.revision(), task.value()).ok());
    CHECK_EQ(previews.publishedArtifact(DocumentId("material-doc")), renderer.artifact);
}

TEST_CASE("editor.material.preview_rejects_stale_or_failed_publication") {
    MaterialDocumentTarget   material("preview-stale");
    RecordingPreviewRenderer renderer;
    MaterialPreviewService   previews;
    auto                     task = previews.render(DocumentId("doc"), material, {}, renderer);
    REQUIRE(task.ok());
    auto roughness =
        material.makeSet(selection(material), PropertyPath("shading.roughness"), 0.8, PropertySetMode::Absolute);
    REQUIRE(roughness.ok());
    REQUIRE(material.applyDomainOperation(roughness.value()).ok());
    CHECK_EQ(static_cast<int>(previews.publish(DocumentId("doc"), material.revision(), task.value()).code()),
             static_cast<int>(EditorStatus::Conflict));
    CHECK(previews.publishedArtifact(DocumentId("doc")).empty());

    renderer.status = EditorStatus::Failed;
    renderer.artifact.clear();
    auto failed = previews.render(DocumentId("doc"), material, {}, renderer);
    REQUIRE(failed.ok());
    CHECK_EQ(static_cast<int>(previews.publish(DocumentId("doc"), material.revision(), failed.value()).code()),
             static_cast<int>(EditorStatus::Failed));
}

TEST_CASE("editor.material.preview_validates_scene_settings_before_renderer_call") {
    MaterialDocumentTarget   material("preview-invalid");
    RecordingPreviewRenderer renderer;
    MaterialPreviewService   previews;
    MaterialPreviewSettings  settings;
    settings.geometry  = "custom";
    settings.width     = 4;
    const auto invalid = previews.render(DocumentId("doc"), material, settings, renderer);
    CHECK_EQ(static_cast<int>(invalid.code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(renderer.calls, 0);
    CHECK(invalid.diagnostics().size() >= size_t{2});
}
}  // namespace test_editor_material_batch_target
#endif
