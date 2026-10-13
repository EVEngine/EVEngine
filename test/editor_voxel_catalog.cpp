#include "common/Capability.h"
#include "common/EditorAutomation.h"
#include "editor/Editor.h"
#include "voxel/CubeTypeRegistry.h"
#include "voxel/editing/VoxelCatalog.h"
#include "voxel/editing/VoxelPaletteTarget.h"
#include "voxel/editor/VoxelCatalogEditor.h"
#include "voxel/editor/VoxelEditorModule.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// editor_voxel_catalog.cpp
using eve::voxel_editing::DomainOperation;
using eve::voxel_editing::ObjectId;
using eve::voxel_editing::Result;
using eve::voxel_editing::VoxelCatalogTarget;
using eve::voxel_editing::VoxelCellFill;
using eve::voxel_editing::VoxelModelValue;
using eve::voxel_editing::VoxelSocketKind;

namespace {
void apply(VoxelCatalogTarget& target, Result<DomainOperation> operation) {
    REQUIRE(operation.ok());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
}

VoxelModelValue cube(const std::string& id, int n) {
    VoxelModelValue model;
    model.id    = ObjectId(id);
    model.name  = id;
    model.sizeX = n;
    model.sizeY = n;
    model.sizeZ = n;
    for (int z = 0; z < n; ++z)
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x) model.voxels.push_back({x, y, z});
    return model;
}
}  // namespace

TEST_CASE("editor.voxel.catalog_classifies_filled_and_partial_occupancy") {
    VoxelCatalogTarget target("project");
    apply(target, target.makeCreateModel(cube("block", 4)));
    CHECK_EQ(eve::voxel_editing::voxelClassifyModelFill(*target.findModel(ObjectId("block"))), VoxelCellFill::Filled);

    VoxelModelValue bed;
    bed.id    = ObjectId("bed");
    bed.name  = "bed";
    bed.sizeX = 8;
    bed.sizeY = 4;
    bed.sizeZ = 6;
    for (int x = 0; x < 8; ++x)
        for (int z = 0; z < 6; ++z) bed.voxels.push_back({x, 0, z});
    apply(target, target.makeCreateModel(bed));
    CHECK_EQ(eve::voxel_editing::voxelClassifyModelFill(*target.findModel(ObjectId("bed"))), VoxelCellFill::Partial);

    const auto pick =
        eve::voxel_editing::pickVoxelModel(*target.findModel(ObjectId("block")), -2.f, 1.5f, 1.5f, 1.f, 0.f, 0.f, 16.f);
    CHECK(pick.hit);
    CHECK(pick.canAttach);
    CHECK_EQ(pick.hitX, 0);
    CHECK_EQ(pick.prevX, -1);
}

TEST_CASE("editor.voxel.catalog_snapshot_roundtrip_preserves_occupancy") {
    VoxelCatalogTarget target("project");
    apply(target, target.makeCreateModel(cube("block", 2)));
    VoxelCatalogTarget restored("copy");
    REQUIRE(restored.loadSnapshot(target.snapshotValue()).ok());
    REQUIRE(restored.findModel(ObjectId("block")));
    CHECK_EQ(restored.findModel(ObjectId("block"))->voxels.size(), static_cast<std::size_t>(8));
}

TEST_CASE("editor.voxel.catalog_join_sockets_match_opposite_faces") {
    CHECK(eve::voxel_editing::canJoinVoxelSockets({"bed-rail", VoxelSocketKind::Male},
                                                  {"bed-rail", VoxelSocketKind::Female}));
    VoxelCatalogTarget target("project");
    VoxelModelValue    head = cube("head", 2);
    head.sockets[0]         = {"bed-rail", VoxelSocketKind::Male};
    VoxelModelValue foot    = cube("foot", 2);
    foot.sockets[1]         = {"bed-rail", VoxelSocketKind::Female};
    apply(target, target.makeCreateModel(head));
    apply(target, target.makeCreateModel(foot));
    const auto partners = target.hullJoinPartners(ObjectId("head"), 0);
    REQUIRE_EQ(partners.size(), static_cast<std::size_t>(1));
    CHECK_EQ(partners.front().value(), std::string("foot"));
}

TEST_CASE("editor.voxel.catalog_empty_canvas_attaches_first_in_bounds_cell") {
    VoxelModelValue empty;
    empty.id        = ObjectId("empty");
    empty.name      = "empty";
    empty.sizeX     = 4;
    empty.sizeY     = 4;
    empty.sizeZ     = 4;
    const auto pick = eve::voxel_editing::pickVoxelModel(empty, -2.f, 1.5f, 1.5f, 1.f, 0.f, 0.f, 16.f);
    CHECK(!pick.hit);
    CHECK(pick.canAttach);
    CHECK_EQ(pick.prevX, 0);
    CHECK_EQ(pick.prevY, 1);
    CHECK_EQ(pick.prevZ, 1);
}

// editor_voxel_editor.cpp
using eve::editor::Editor;

TEST_CASE("editor.voxel.catalog_automation_owns_headless_targets") {
    Editor                               editor;
    eve::voxel_editor::VoxelEditorModule adapter;
    auto*                                automation = eve::cap::query<eve::IEditorAutomation>();
    REQUIRE(automation != nullptr);

    const std::string commands = automation->invoke("commands", "{}");
    CHECK(commands.find("voxel.model.create.v1") != std::string::npos);
    CHECK(commands.find("voxel.model.voxel.set.v1") != std::string::npos);

    const std::string created =
        automation->invoke("target-create", R"({"target":"agent.voxel","type":"voxel-catalog"})");
    REQUIRE(created.find("\"status\":\"applied\"") != std::string::npos);

    const std::string model = automation->invoke(
        "execute",
        R"({"target":"agent.voxel","command":"voxel.model.create.v1","payload":{"id":"brick","name":"brick","sizeX":4,"sizeY":4,"sizeZ":4}})");
    REQUIRE(model.find("\"status\":\"applied\"") != std::string::npos);

    const std::string voxel = automation->invoke(
        "execute",
        R"({"target":"agent.voxel","command":"voxel.model.voxel.set.v1","payload":{"item":"brick","x":1,"y":2,"z":3,"occupied":true}})");
    REQUIRE(voxel.find("\"status\":\"applied\"") != std::string::npos);

    const std::string inspected = automation->invoke("inspect", R"({"target":"agent.voxel"})");
    CHECK(inspected.find("voxel-catalog") != std::string::npos);

    const std::string undone = automation->invoke("undo", R"({"target":"agent.voxel"})");
    CHECK(undone.find("\"status\":\"applied\"") != std::string::npos);

    REQUIRE(automation->invoke("target-close", R"({"target":"agent.voxel"})").find("\"status\":\"applied\"") !=
            std::string::npos);
}

TEST_CASE("editor.voxel.sculpt_editor_attaches_inside_canvas_and_undoes") {
    eve::voxel_editor::VoxelCatalogEditor editor("asset.preview.sculpt");
    CHECK_EQ(editor.modelSizeX(), 8);
    CHECK_EQ(editor.voxelCount(), 64);
    REQUIRE(editor.setVoxel(1, 2, 2, true).ok());
    CHECK_EQ(editor.voxelCount(), 65);
    REQUIRE(editor.undo().ok());
    CHECK_EQ(editor.voxelCount(), 64);
    REQUIRE(editor.setViewport(640.f, 400.f).ok());
    REQUIRE(editor.setTool("erase").ok());
    REQUIRE(editor.pointerDown(320.f, 200.f).ok());
    CHECK_EQ(editor.voxelCount(), 63);
    REQUIRE(editor.undo().ok());
    CHECK_EQ(editor.voxelCount(), 64);
    REQUIRE(editor.setTool("erase").ok());
    REQUIRE(editor.pointerWorldRay(-2.f, 4.f, 4.f, 1.f, 0.f, 0.f).ok());
    CHECK_EQ(editor.voxelCount(), 63);
}

// editor_voxel_palette.cpp
using namespace eve::voxel_editing;
using namespace eve::editing;
namespace {
void apply(VoxelPaletteTarget& t, Result<DomainOperation> op) {
    REQUIRE(op.ok());
    REQUIRE(t.applyDomainOperation(op.value()).ok());
}
VoxelPaletteEntryValue cube(const std::string& id, bool directional = false) {
    VoxelPaletteEntryValue v;
    v.id               = ObjectId("editor-" + id);
    v.type.name        = id;
    v.type.directional = directional;
    for (int i = 0; i < 6; ++i) v.type.faceTex[i] = static_cast<std::uint8_t>(i + 1);
    return v;
}
SelectionSnapshot select(const VoxelPaletteTarget& t, const char* id) {
    SelectionSnapshot s;
    s.channel = "voxel-palette";
    s.items.push_back({SelectionDomain::Asset, TargetId(t.targetId()), StableId(id), "voxel.cube-type"});
    return s;
}
}  // namespace
TEST_CASE("editor.voxel_palette.face_inspector_is_reversible_and_persistent") {
    VoxelPaletteTarget target("blocks");
    apply(target, target.makeCreate(cube("furnace", true)));
    const auto         selection = select(target, "editor-furnace");
    EditorValue::Array faces{int64_t{9}, int64_t{8}, int64_t{7}, int64_t{6}, int64_t{5}, int64_t{4}};
    auto               edit = target.makeSet(selection, PropertyPath("cube.faces"), faces, PropertySetMode::Absolute);
    REQUIRE(edit.ok());
    REQUIRE(target.applyDomainOperation(edit.value()).ok());
    CHECK_EQ(int(target.entries().front().type.faceTex[0]), 9);
    DomainOperation undo = edit.value();
    undo.payload         = edit.value().inverse;
    REQUIRE(target.applyDomainOperation(undo).ok());
    CHECK_EQ(int(target.entries().front().type.faceTex[0]), 1);
    VoxelPaletteTarget restored("copy");
    REQUIRE(restored.loadSnapshot(target.snapshotValue()).ok());
    CHECK(restored.entries().front().type.directional);
}
TEST_CASE("editor.voxel_palette.enforces_variant_capacity_before_runtime") {
    VoxelPaletteTarget target("blocks");
    for (int i = 0; i < 63; ++i) apply(target, target.makeCreate(cube("directional-" + std::to_string(i), true)));
    CHECK_EQ(static_cast<int>(target.makeCreate(cube("overflow", true)).code()),
             static_cast<int>(EditorStatus::Rejected));
    apply(target, target.makeCreate(cube("single", false)));
    CHECK_EQ(target.entries().size(), static_cast<std::size_t>(64));
}
TEST_CASE("editor.voxel_palette.publication_is_atomic_and_reports_variant_ids") {
    VoxelPaletteTarget target("blocks");
    apply(target, target.makeCreate(cube("stone")));
    apply(target, target.makeCreate(cube("furnace", true)));
    VoxelPaletteRuntime runtime;
    auto                result = runtime.publish(target);
    REQUIRE(result.ok());
    CHECK_EQ(result.value().size(), static_cast<std::size_t>(2));
    CHECK_EQ(result.value()[0].baseId, 1);
    CHECK_EQ(result.value()[1].baseId, 2);
    CHECK_EQ(result.value()[1].variants, 4);
    REQUIRE(runtime.registry());
    CHECK_EQ(int(runtime.registry()->variantId("furnace", 3)), 5);
    CHECK(runtime.registry()->find("furnace") != nullptr);
}
TEST_CASE("editor.voxel_palette.invalid_snapshot_does_not_partially_replace") {
    VoxelPaletteTarget target("blocks");
    apply(target, target.makeCreate(cube("stone")));
    const auto  before  = target.snapshotValue();
    EditorValue invalid = before;
    auto*       root    = invalid.getIf<EditorValue::Object>();
    auto*       content = (*root)["content"].getIf<EditorValue::Object>();
    auto*       entries = (*content)["entries"].getIf<EditorValue::Array>();
    auto*       entry   = (*entries)[0].getIf<EditorValue::Object>();
    auto*       faces   = (*entry)["faces"].getIf<EditorValue::Array>();
    (*faces)[0]         = int64_t{999};
    CHECK_EQ(static_cast<int>(target.loadSnapshot(invalid).code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(target.snapshotValue(), before);
}
