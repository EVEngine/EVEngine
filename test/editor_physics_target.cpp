#include "editor/EditorAuthority.h"
#include "editor/EditorPhysicsTarget.h"
#include "editor/EditorTransactionService.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::editor;

namespace {

SelectionSnapshot colliderSelection(const PhysicsColliderTarget& target) {
    SelectionSnapshot selection;
    selection.channel = "scene";
    SelectionItem item;
    item.domain = SelectionDomain::Scene;
    item.target = TargetId(target.targetId());
    item.item = StableId(target.targetId());
    item.type = target.describe().type;
    selection.items.push_back(item);
    selection.primary = item;
    return selection;
}

SelectionSnapshot jointSelection(const PhysicsJointTarget& target) {
    SelectionSnapshot selection;
    selection.channel = "scene";
    SelectionItem item;
    item.domain = SelectionDomain::Scene;
    item.target = TargetId(target.targetId());
    item.item = StableId(target.targetId());
    item.type = target.describe().type;
    selection.items.push_back(item);
    selection.primary = item;
    return selection;
}

EditorResult<TransactionReceipt> commitCollider(PhysicsColliderTarget& target,
                                                LocalTransactionBackend& transactions,
                                                const DomainOperation& operation,
                                                const char* id) {
    TransactionSpec specification;
    specification.id = TransactionId(id);
    specification.label = "Edit collider";
    specification.target = TargetId(target.targetId());
    specification.baseRevision = target.revision();
    auto begun = transactions.begin(std::move(specification));
    if (!begun.accepted())
        return EditorResult<TransactionReceipt>::error(begun.status, RuleId("test.physics.begin"),
                                                       "Could not begin collider transaction");
    auto appended = transactions.append(operation);
    if (!appended.accepted()) {
        [[maybe_unused]] const auto rolledBack = transactions.rollback();
        return EditorResult<TransactionReceipt>::error(appended.status, RuleId("test.physics.append"),
                                                       "Could not append collider operation");
    }
    return transactions.commit();
}

}  // namespace

TEST_CASE("editor.physics.collider_schema_tracks_backend_dimensionality") {
    PhysicsColliderTarget target2d("fixture", 2);
    PhysicsColliderTarget target3d("shape", 3);
    const auto schema2d = target2d.schema(colliderSelection(target2d));
    const auto schema3d = target3d.schema(colliderSelection(target3d));
    CHECK_EQ(schema2d.typeId, std::string("physics.collider2d"));
    CHECK_EQ(schema3d.typeId, std::string("physics.collider3d"));
    REQUIRE(schema2d.find(PropertyPath("shape.kind")));
    REQUIRE(schema3d.find(PropertyPath("shape.kind")));
    CHECK_EQ(schema2d.find(PropertyPath("shape.kind"))->enumItems.size(), static_cast<std::size_t>(4));
    CHECK_EQ(schema3d.find(PropertyPath("shape.kind"))->enumItems.size(), static_cast<std::size_t>(6));
    CHECK_EQ(target2d.describe().type, std::string("physics-collider-2d"));
    CHECK_EQ(target3d.describe().type, std::string("physics-collider-3d"));
}

TEST_CASE("editor.physics.collider_edits_validate_and_undo") {
    PhysicsColliderTarget target("shape", 3);
    LocalWorldAuthority authority(&target);
    LocalTransactionBackend transactions(&authority);
    const SelectionSnapshot selection = colliderSelection(target);

    auto invalid = target.makeSet(selection, PropertyPath("shape.size"),
                                  EditorValue::Array{1.0, -2.0, 3.0}, PropertySetMode::Absolute);
    CHECK_EQ(static_cast<int>(invalid.status), static_cast<int>(EditorStatus::Rejected));
    invalid = target.makeSet(selection, PropertyPath("material.friction"), 1.5,
                             PropertySetMode::Absolute);
    CHECK_EQ(static_cast<int>(invalid.status), static_cast<int>(EditorStatus::Rejected));

    auto shape = target.makeSet(selection, PropertyPath("shape.kind"), "capsule",
                                PropertySetMode::Absolute);
    REQUIRE(shape.accepted());
    REQUIRE(commitCollider(target, transactions, *shape.value, "collider.shape").accepted());
    auto radius = target.makeSet(selection, PropertyPath("shape.radius"), 0.75,
                                 PropertySetMode::Absolute);
    REQUIRE(radius.accepted());
    REQUIRE(commitCollider(target, transactions, *radius.value, "collider.radius").accepted());
    CHECK(target.read(selection, PropertyPath("shape.radius")).value == EditorValue(0.75));
    REQUIRE(transactions.undo().accepted());
    CHECK(target.read(selection, PropertyPath("shape.radius")).value == EditorValue(0.5));
    REQUIRE(transactions.redo().accepted());
    CHECK_EQ(target.validate().size(), static_cast<std::size_t>(1));
}

TEST_CASE("editor.physics.collider_snapshot_is_versioned_atomic_and_diagnostic") {
    PhysicsColliderTarget source("source", 3);
    const SelectionSnapshot selection = colliderSelection(source);
    auto mesh = source.makeSet(selection, PropertyPath("shape.kind"), "triangle-mesh",
                               PropertySetMode::Absolute);
    REQUIRE(mesh.value);
    REQUIRE(source.applyDomainOperation(*mesh.value).accepted());
    CHECK_EQ(source.validate().size(), static_cast<std::size_t>(1));

    PhysicsColliderTarget restored("restored", 3);
    REQUIRE(restored.loadSnapshot(source.snapshotValue()).accepted());
    CHECK(restored.read(colliderSelection(restored), PropertyPath("shape.kind")).value ==
          EditorValue("triangle-mesh"));

    PhysicsColliderTarget wrongDimension("wrong", 2);
    CHECK_EQ(static_cast<int>(wrongDimension.loadSnapshot(source.snapshotValue()).status),
             static_cast<int>(EditorStatus::Rejected));
    CHECK(wrongDimension.read(colliderSelection(wrongDimension), PropertyPath("shape.kind")).value ==
          EditorValue("box"));
}

TEST_CASE("editor.physics.joint_exposes_body_anchor_limit_motor_and_break_authoring") {
    PhysicsJointTarget target("suspension");
    const SelectionSnapshot selection = jointSelection(target);
    CHECK_EQ(target.schema(selection).typeId, std::string("physics.joint3d"));
    CHECK(target.schema(selection).find(PropertyPath("body.a")) != nullptr);
    CHECK(target.schema(selection).find(PropertyPath("joint.axis")) != nullptr);
    CHECK(target.schema(selection).find(PropertyPath("motor.force")) != nullptr);
    CHECK(target.schema(selection).find(PropertyPath("break.torque")) != nullptr);
    CHECK_EQ(target.validate().size(), static_cast<std::size_t>(1));

    for (const auto& [path, value] : {
             std::pair{PropertyPath("body.a"), EditorValue("vehicle")},
             {PropertyPath("body.b"), EditorValue("wheel")},
             {PropertyPath("joint.kind"), EditorValue("wheel")},
             {PropertyPath("limit.enabled"), EditorValue(true)},
             {PropertyPath("limit.minimum"), EditorValue(2.0)},
             {PropertyPath("limit.maximum"), EditorValue(1.0)}}) {
        auto operation = target.makeSet(selection, path, value, PropertySetMode::Absolute);
        REQUIRE(operation.value);
        REQUIRE(target.applyDomainOperation(*operation.value).accepted());
    }
    CHECK_EQ(target.validate().size(), static_cast<std::size_t>(1));

    auto fixLimit = target.makeSet(selection, PropertyPath("limit.maximum"), 3.0,
                                   PropertySetMode::Absolute);
    REQUIRE(fixLimit.value);
    REQUIRE(target.applyDomainOperation(*fixLimit.value).accepted());
    CHECK(target.validate().empty());
    CHECK_EQ(static_cast<int>(target.snapshotValue().type()),
             static_cast<int>(EditorValue::Type::Object));
}
