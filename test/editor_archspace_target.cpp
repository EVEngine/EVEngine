#include "archspace/ArchSpaceDocument.h"
#include "archspace/editing/ArchSpaceTarget.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

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

TEST_CASE("archspace.editing.delete_is_cascading_and_reversible") {
    ArchSpaceDocumentTarget target("loft");
    apply(target, target.makeBootstrap("site", "building", "level0"));
    apply(target, target.makeCreateRectRoom("level0", "room", "Room", -2, -2, 4, 4));
    const std::size_t before = target.document().nodeCount();
    auto              del    = target.makeDeleteNode(ObjectId("room.zone"));
    REQUIRE(del.ok());
    REQUIRE(target.applyDomainOperation(del.value()).ok());
    CHECK(target.document().find("room.zone") == nullptr);
    CHECK(target.document().nodeCount() + 1 == before);

    DomainOperation undo = del.value();
    undo.payload         = del.value().inverse;
    REQUIRE(target.applyDomainOperation(undo).ok());
    CHECK(target.document().find("room.zone") != nullptr);
}
