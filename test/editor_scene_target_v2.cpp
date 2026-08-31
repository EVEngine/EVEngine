#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "editor/EditorAuthority.h"
#include "editor/EditorSceneTarget.h"
#include "editor/EditorTransactionService.h"

#include <string>

using namespace eve::editor;

namespace {

EditorResult<TransactionReceipt> commitOperation(SceneTargetBase& target, LocalTransactionBackend& transactions,
                                                 const DomainOperation& operation, const std::string& id) {
    TransactionSpec specification;
    specification.id           = TransactionId(id);
    specification.label        = "Scene edit";
    specification.target       = TargetId(target.targetId());
    specification.baseRevision = target.revision();
    auto begun                 = transactions.begin(std::move(specification));
    if (!begun.isAccepted())
        return EditorResult<TransactionReceipt>::error(begun.status, RuleId("test.scene.begin"),
                                                       "Could not begin scene transaction");
    auto appended = transactions.append(operation);
    if (!appended.isAccepted()) {
        auto rolledBack = transactions.rollback();
        if (!rolledBack.isAccepted())
            return EditorResult<TransactionReceipt>::error(rolledBack.status, RuleId("test.scene.rollback"),
                                                           "Could not roll back scene transaction");
        return EditorResult<TransactionReceipt>::error(appended.status, RuleId("test.scene.append"),
                                                       "Could not append scene operation");
    }
    return transactions.commit();
}

void exerciseSceneBackend(SceneTargetBase& target, const std::string& transactionPrefix) {
    LocalWorldAuthority     authority(&target);
    LocalTransactionBackend transactions(&authority);
    ScenePlacementToolLogic placement;
    SceneTransformToolLogic transform;

    CreateSceneObjectRequest root;
    root.id            = ObjectId("root");
    root.name          = "Root";
    root.transform     = {0.0, 0.0, 0.0};
    auto rootOperation = placement.plan(target, root);
    CHECK(rootOperation.isAccepted());
    CHECK(commitOperation(target, transactions, *rootOperation.value, transactionPrefix + ".root").isAccepted());

    CreateSceneObjectRequest child;
    child.id            = ObjectId("coaster");
    child.parent        = root.id;
    child.name          = "Coaster";
    child.transform     = {1.0, 0.0, 2.0};
    auto childOperation = placement.plan(target, child);
    CHECK(childOperation.isAccepted());
    CHECK(commitOperation(target, transactions, *childOperation.value, transactionPrefix + ".child").isAccepted());
    CHECK_EQ(target.sceneChildren(root.id).size(), static_cast<std::size_t>(1));

    SceneTransformValue moved{8.0, 3.0, 5.0, 0.1, 0.5, -0.2, 2.0, 1.5, 0.75};
    auto                transformOperation = transform.plan(target, child.id, moved);
    CHECK(transformOperation.isAccepted());
    CHECK(commitOperation(target, transactions, *transformOperation.value, transactionPrefix + ".move").isAccepted());
    CHECK(target.readTransform(child.id).value == moved);

    CHECK(transactions.undo().isAccepted());
    CHECK(target.readTransform(child.id).value == child.transform);
    CHECK(transactions.redo().isAccepted());
    CHECK(target.readTransform(child.id).value == moved);
}

TEST_CASE("editor.v2.scene_transform_supports_atomic_multi_object_trs_edits") {
    RuntimeWorldTarget       target("runtime");
    LocalWorldAuthority      authority(&target);
    LocalTransactionBackend transactions(&authority);
    ScenePlacementToolLogic  placement;
    SceneTransformToolLogic  transform;

    for (const std::string id : {"left", "right"}) {
        CreateSceneObjectRequest request;
        request.id   = ObjectId(id);
        request.name = id;
        auto operation = placement.plan(target, request);
        REQUIRE(operation.isAccepted());
        REQUIRE(commitOperation(target, transactions, *operation.value, "create." + id).isAccepted());
    }

    const SceneTransformValue leftValue{1.0, 2.0, 3.0, 0.0, 0.25, 0.0, 2.0, 2.0, 2.0};
    const SceneTransformValue rightValue{-1.0, 4.0, 6.0, 0.5, 0.0, 1.0, 0.5, 1.0, 1.5};
    auto left  = transform.plan(target, ObjectId("left"), leftValue);
    auto right = transform.plan(target, ObjectId("right"), rightValue);
    REQUIRE(left.isAccepted());
    REQUIRE(right.isAccepted());

    TransactionSpec specification;
    specification.id           = TransactionId("transform.multi");
    specification.label        = "Transform selection";
    specification.target       = TargetId(target.targetId());
    specification.baseRevision = target.revision();
    REQUIRE(transactions.begin(std::move(specification)).isAccepted());
    REQUIRE(transactions.append(*left.value).isAccepted());
    REQUIRE(transactions.append(*right.value).isAccepted());
    REQUIRE(transactions.commit().isAccepted());
    CHECK(target.readTransform(ObjectId("left")).value == leftValue);
    CHECK(target.readTransform(ObjectId("right")).value == rightValue);

    REQUIRE(transactions.undo().isAccepted());
    CHECK(target.readTransform(ObjectId("left")).value == SceneTransformValue{});
    CHECK(target.readTransform(ObjectId("right")).value == SceneTransformValue{});
    REQUIRE(transactions.redo().isAccepted());
    CHECK(target.readTransform(ObjectId("left")).value == leftValue);
    CHECK(target.readTransform(ObjectId("right")).value == rightValue);
}

}  // namespace

TEST_CASE("editor.v2.scene_document_and_runtime_targets_are_tool_isomorphic") {
    SceneDocumentTarget document("document-scene");
    RuntimeWorldTarget  runtime("runtime-scene");
    exerciseSceneBackend(document, "document");
    exerciseSceneBackend(runtime, "runtime");

    CHECK_EQ(document.describe().type, std::string("scene-document"));
    CHECK_EQ(runtime.describe().type, std::string("runtime-world"));
    CHECK_EQ(document.describe().capabilities, runtime.describe().capabilities);
}

TEST_CASE("editor.v2.scene_targets_expose_stable_capability_ids") {
    SceneDocumentTarget target("scene");
    CHECK(target.queryCapability(ISceneHierarchyEditTarget::editorCapabilityId()) != nullptr);
    CHECK(target.queryCapability(ITransformEditTarget::editorCapabilityId()) != nullptr);
    CHECK(target.queryCapability(CapabilityId("unknown")) == nullptr);

    CreateSceneObjectRequest invalid;
    invalid.id     = ObjectId("child");
    invalid.parent = ObjectId("missing");
    auto operation = ScenePlacementToolLogic().plan(target, invalid);
    CHECK_EQ(static_cast<int>(operation.status), static_cast<int>(EditorStatus::NotFound));
}

TEST_CASE("editor.v2.scene_create_is_reversible_through_authority") {
    RuntimeWorldTarget       target("runtime");
    LocalWorldAuthority      authority(&target);
    LocalTransactionBackend  transactions(&authority);
    CreateSceneObjectRequest request;
    request.id     = ObjectId("placed-object");
    request.name   = "Placed Object";
    auto operation = ScenePlacementToolLogic().plan(target, request);
    CHECK(operation.isAccepted());
    CHECK(commitOperation(target, transactions, *operation.value, "place").isAccepted());
    CHECK(target.sceneObject(request.id).isAccepted());
    CHECK(transactions.undo().isAccepted());
    CHECK_EQ(static_cast<int>(target.sceneObject(request.id).status), static_cast<int>(EditorStatus::NotFound));
}

TEST_CASE("editor.v2.scene_hierarchy_edits_are_reversible_and_cycle_safe") {
    SceneDocumentTarget     target("scene");
    LocalWorldAuthority     authority(&target);
    LocalTransactionBackend transactions(&authority);
    ScenePlacementToolLogic placement;
    SceneHierarchyToolLogic hierarchy;

    for (const auto& request : {CreateSceneObjectRequest{ObjectId("root"), {}, "Root", {}},
                                CreateSceneObjectRequest{ObjectId("group"), ObjectId("root"), "Group", {}},
                                CreateSceneObjectRequest{ObjectId("leaf"), ObjectId("group"), "Leaf", {}}}) {
        auto operation = placement.plan(target, request);
        REQUIRE(operation.isAccepted());
        REQUIRE(commitOperation(target, transactions, *operation.value, "create." + request.id.value()).isAccepted());
    }

    auto cycle = hierarchy.planReparent(target, ObjectId("root"), ObjectId("leaf"));
    CHECK_EQ(static_cast<int>(cycle.status), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(static_cast<int>(hierarchy.planDelete(target, ObjectId("group")).status),
             static_cast<int>(EditorStatus::Rejected));

    auto rename = hierarchy.planRename(target, ObjectId("leaf"), "Camera");
    REQUIRE(rename.isAccepted());
    REQUIRE(commitOperation(target, transactions, *rename.value, "rename").isAccepted());
    CHECK_EQ(target.sceneObject(ObjectId("leaf")).value->name, std::string("Camera"));
    REQUIRE(transactions.undo().isAccepted());
    CHECK_EQ(target.sceneObject(ObjectId("leaf")).value->name, std::string("Leaf"));
    REQUIRE(transactions.redo().isAccepted());

    auto reparent = hierarchy.planReparent(target, ObjectId("leaf"), ObjectId("root"));
    REQUIRE(reparent.isAccepted());
    REQUIRE(commitOperation(target, transactions, *reparent.value, "reparent").isAccepted());
    CHECK(target.sceneObject(ObjectId("leaf")).value->parent == ObjectId("root"));

    auto remove = hierarchy.planDelete(target, ObjectId("leaf"));
    REQUIRE(remove.isAccepted());
    REQUIRE(commitOperation(target, transactions, *remove.value, "delete").isAccepted());
    CHECK_EQ(static_cast<int>(target.sceneObject(ObjectId("leaf")).status),
             static_cast<int>(EditorStatus::NotFound));
    REQUIRE(transactions.undo().isAccepted());
    CHECK_EQ(target.sceneObject(ObjectId("leaf")).value->name, std::string("Camera"));
    CHECK(target.sceneObject(ObjectId("leaf")).value->parent == ObjectId("root"));
}

TEST_CASE("editor.v2.scene_property_provider_multi_edits_generic_trs_inspector") {
    SceneDocumentTarget     target("scene");
    LocalWorldAuthority     authority(&target);
    LocalTransactionBackend transactions(&authority);
    ScenePlacementToolLogic placement;
    for (const std::string id : {"one", "two"}) {
        CreateSceneObjectRequest request;
        request.id   = ObjectId(id);
        request.name = id;
        auto operation = placement.plan(target, request);
        REQUIRE(operation.isAccepted());
        REQUIRE(commitOperation(target, transactions, *operation.value, "property.create." + id).isAccepted());
    }

    SelectionSnapshot selection;
    selection.channel = "scene";
    for (const std::string id : {"one", "two"}) {
        SelectionItem item;
        item.domain = SelectionDomain::Scene;
        item.target = TargetId(target.targetId());
        item.item   = StableId(id);
        item.type   = "scene.object";
        selection.items.push_back(item);
    }
    selection.primary = selection.items.front();

    ScenePropertyProvider provider(&target);
    CHECK_EQ(provider.schema(selection).properties.size(), static_cast<std::size_t>(3));
    CHECK_EQ(static_cast<int>(provider.read(selection, PropertyPath("transform.scale")).state),
             static_cast<int>(PropertyReadState::Value));
    auto setScale = provider.makeSet(selection, PropertyPath("transform.scale"),
                                     EditorValue::Array{2.0, 3.0, 4.0}, PropertySetMode::Absolute);
    REQUIRE(setScale.isAccepted());
    REQUIRE(commitOperation(target, transactions, *setScale.value, "property.scale").isAccepted());
    CHECK_EQ(target.readTransform(ObjectId("one")).value->scaleY, 3.0);
    CHECK_EQ(target.readTransform(ObjectId("two")).value->scaleZ, 4.0);
    REQUIRE(transactions.undo().isAccepted());
    CHECK_EQ(target.readTransform(ObjectId("one")).value->scaleY, 1.0);
    CHECK_EQ(target.readTransform(ObjectId("two")).value->scaleZ, 1.0);

    SceneTransformToolLogic transform;
    SceneTransformValue distinct;
    distinct.x = 9.0;
    auto moveOne = transform.plan(target, ObjectId("one"), distinct);
    REQUIRE(moveOne.isAccepted());
    REQUIRE(commitOperation(target, transactions, *moveOne.value, "property.mixed").isAccepted());
    CHECK_EQ(static_cast<int>(provider.read(selection, PropertyPath("transform.position")).state),
             static_cast<int>(PropertyReadState::Mixed));
}
