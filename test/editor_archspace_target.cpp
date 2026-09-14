#include "archspace/ArchSpaceDocument.h"
#include "archspace/editing/ArchSpaceTarget.h"
#include "archspace/editor/ArchSpaceEditorModule.h"

#include "common/Capability.h"
#include "common/EditorAutomation.h"
#include "editor/Editor.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <fstream>

using namespace eve::archspace;
using namespace eve::archspace_editing;
using namespace eve::editing;

namespace {

void apply(ArchSpaceDocumentTarget& target, EditorResult<DomainOperation> operation) {
    REQUIRE(operation.ok());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
}

SelectionSnapshot select(const ArchSpaceDocumentTarget& target, const char* id, const char* type) {
    SelectionSnapshot selection;
    selection.channel = "archspace";
    selection.items.push_back({SelectionDomain::Asset, TargetId(target.targetId()), StableId(id), type});
    return selection;
}

}  // namespace

TEST_CASE("archspace.document.bootstrap_room_opening_and_mesh_bake") {
    Document document;
    REQUIRE(document.bootstrap("site", "building", "level0").ok());
    REQUIRE(
        document.createRoom("level0", "living", "Living Room", {{0, 0}, {6, 0}, {6, 4}, {0, 4}}, 3.0, 0.2, 0.2).ok());
    REQUIRE(document.find("living.wall.0") != nullptr);
    REQUIRE(document.find("living.zone") != nullptr);
    Node door;
    door.id            = "living.door";
    door.kind          = NodeKind::Opening;
    door.parentId      = "living.wall.0";
    door.name          = "Entry";
    door.openingKind   = OpeningKind::Door;
    door.t             = 0.5;
    door.width         = 1.0;
    door.openingHeight = 2.1;
    REQUIRE(document.insert(std::move(door)).ok());
    const MeshBake bake = document.bakeMesh();
    CHECK(bake.positions.size() >= 8);
    CHECK(!bake.indices.empty());
    CHECK(document.diagnostics().empty());
}

TEST_CASE("archspace.editing.room_property_undo_and_snapshot_are_atomic") {
    ArchSpaceDocumentTarget target("apartment");
    apply(target, target.makeBootstrap("site", "building", "level0"));
    apply(target, target.makeCreateRectRoom("level0", "office", "Office", 0, 0, 5, 4));
    CHECK(target.document().find("office.zone") != nullptr);

    const auto selection = select(target, "office.wall.0", "archspace.wall");
    auto       edit      = target.makeSet(selection, PropertyPath("thickness"), 0.35, PropertySetMode::Absolute);
    REQUIRE(edit.ok());
    REQUIRE(target.applyDomainOperation(edit.value()).ok());
    CHECK_EQ(target.document().find("office.wall.0")->thickness, 0.35);

    DomainOperation undo = edit.value();
    undo.payload         = edit.value().inverse;
    REQUIRE(target.applyDomainOperation(undo).ok());
    CHECK_EQ(target.document().find("office.wall.0")->thickness, 0.2);

    apply(target, target.makeCreateOpening("office.wall.1", "office.window", OpeningKind::Window, 0.4, 1.2, 1.4, 0.9));
    apply(target, target.makePlaceItem("level0", "desk", "furniture.desk", Vec3{1.5, 0, 1.5}, 90));
    CHECK(target.document().find("desk") != nullptr);

    const EditorValue       snapshot = target.snapshotValue();
    ArchSpaceDocumentTarget restored("copy");
    REQUIRE(restored.loadSnapshot(snapshot).ok());
    CHECK_EQ(restored.document().nodeCount(), target.document().nodeCount());

    EditorValue invalid      = snapshot;
    auto*       root         = invalid.getIf<EditorValue::Object>();
    (*root)["schemaVersion"] = int64_t{99};
    const auto before        = target.snapshotValue();
    CHECK(!target.loadSnapshot(invalid).ok());
    CHECK_EQ(target.snapshotValue(), before);

    auto gizmo = target.gizmo();
    REQUIRE(gizmo.ok());
    CHECK(!gizmo.value().primitives.empty());
    CHECK(target.bakeMesh().indices.size() >= 3);
}

TEST_CASE("archspace.document.opening_cutout_changes_mesh_and_helpers_work") {
    Document document;
    REQUIRE(document.bootstrap("site", "building", "level0").ok());
    REQUIRE(document.createRoom("level0", "hall", "Hall", {{0, 0}, {8, 0}, {8, 5}, {0, 5}}, 3.0, 0.2, 0.2).ok());
    const MeshBake before = document.bakeMesh();
    REQUIRE(document.createOpening("hall.wall.0", "hall.door", "Door", OpeningKind::Door, 0.4, 1.1, 2.1, 0.0).ok());
    REQUIRE(document.createOpening("hall.wall.1", "hall.window", "Window", OpeningKind::Window, 0.5, 1.5, 1.3, 0.9)
                .ok());
    REQUIRE(document.createWall("level0", "partition", "Partition", Vec2{4, 0}, Vec2{4, 5}, 3.0, 0.15).ok());
    REQUIRE(document.placeItem("level0", "sofa", "Sofa", "furniture.sofa", Vec3{2, 0, 2.5}, 0).ok());
    const MeshBake after = document.bakeMesh();
    CHECK(after.indices.size() > before.indices.size());
    CHECK(document.find("hall.door") != nullptr);
    CHECK(document.find("partition") != nullptr);
    CHECK(document.find("sofa") != nullptr);
    const MeshArrays arrays = document.bakeMeshArrays();
    CHECK_EQ(arrays.vertexCount() * 3, static_cast<int>(arrays.positions.size()));
    CHECK_EQ(arrays.indexCount() % 3, 0);
    CHECK(arrays.triangleCount() >= 12);

    // Machine-readable evidence for walkthrough artifacts.
    std::ofstream evidence("/opt/cursor/artifacts/archspace_mesh_evidence.txt");
    evidence << "nodes=" << document.nodeCount() << "\n";
    evidence << "triangles=" << arrays.triangleCount() << "\n";
    evidence << "vertices=" << arrays.vertexCount() << "\n";
    evidence << "indices=" << arrays.indexCount() << "\n";
    evidence << "primitives=" << after.primitiveIds.size() << "\n";
    evidence << "has_door=1\n";
    evidence << "has_window=1\n";
    evidence << "has_partition=1\n";
    evidence << "has_item=1\n";
    evidence.close();
}

TEST_CASE("editor.archspace.adapter_registers_commands_and_owns_automation_targets") {
    using eve::editor::Editor;
    Editor editor;
    eve::archspace_editor::ArchSpaceEditorModule adapter;
    auto* automation = eve::cap::query<eve::IEditorAutomation>();
    REQUIRE(automation != nullptr);

    const std::string commands = automation->invoke("commands", "{}");
    CHECK(commands.find("archspace.bootstrap.v1") != std::string::npos);
    CHECK(commands.find("archspace.room.create.v1") != std::string::npos);
    CHECK(commands.find("archspace.wall.create.v1") != std::string::npos);
    CHECK(commands.find("archspace.opening.create.v1") != std::string::npos);
    CHECK(commands.find("archspace.item.place.v1") != std::string::npos);
    CHECK(commands.find("archspace.property.set.v1") != std::string::npos);
    CHECK(commands.find("archspace.node.delete.v1") != std::string::npos);

    const std::string created = automation->invoke(
        "target-create", R"({"target":"agent.archspace","type":"archspace","siteId":"site"})");
    REQUIRE(created.find("\"status\":\"applied\"") != std::string::npos);

    const std::string room = automation->invoke(
        "execute",
        R"({"target":"agent.archspace","command":"archspace.room.create.v1","payload":{"levelId":"site.level0","roomId":"studio","name":"Studio","originX":0,"originZ":0,"sizeX":6,"sizeZ":4}})");
    REQUIRE(room.find("\"status\":\"applied\"") != std::string::npos);

    const std::string opening = automation->invoke(
        "execute",
        R"({"target":"agent.archspace","command":"archspace.opening.create.v1","payload":{"wallId":"studio.wall.0","openingId":"studio.door","kind":"door","t":0.5,"width":1.0,"height":2.1,"sill":0.0}})");
    REQUIRE(opening.find("\"status\":\"applied\"") != std::string::npos);

    const std::string thickness = automation->invoke(
        "execute",
        R"({"target":"agent.archspace","command":"archspace.property.set.v1","payload":{"objectId":"studio.wall.0","path":"thickness","value":0.3}})");
    REQUIRE(thickness.find("\"status\":\"applied\"") != std::string::npos);

    const std::string inspected = automation->invoke("inspect", R"({"target":"agent.archspace"})");
    CHECK(inspected.find("studio") != std::string::npos);
    CHECK(inspected.find("studio.door") != std::string::npos);

    const std::string closed = automation->invoke("target-close", R"({"target":"agent.archspace"})");
    CHECK(closed.find("\"status\":\"applied\"") != std::string::npos);
}
