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
    if (!begun.accepted())
        return EditorResult<TransactionReceipt>::error(begun.status, RuleId("test.scene.begin"),
                                                       "Could not begin scene transaction");
    auto appended = transactions.append(operation);
    if (!appended.accepted()) {
        auto rolledBack = transactions.rollback();
        if (!rolledBack.accepted())
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
    CHECK(rootOperation.accepted());
    CHECK(commitOperation(target, transactions, *rootOperation.value, transactionPrefix + ".root").accepted());

    CreateSceneObjectRequest child;
    child.id            = ObjectId("coaster");
    child.parent        = root.id;
    child.name          = "Coaster";
    child.transform     = {1.0, 0.0, 2.0};
    auto childOperation = placement.plan(target, child);
    CHECK(childOperation.accepted());
    CHECK(commitOperation(target, transactions, *childOperation.value, transactionPrefix + ".child").accepted());
    CHECK_EQ(target.sceneChildren(root.id).size(), static_cast<std::size_t>(1));

    SceneTransformValue moved{8.0, 3.0, 5.0};
    auto                transformOperation = transform.plan(target, child.id, moved);
    CHECK(transformOperation.accepted());
    CHECK(commitOperation(target, transactions, *transformOperation.value, transactionPrefix + ".move").accepted());
    CHECK(target.readTransform(child.id).value == moved);

    CHECK(transactions.undo().accepted());
    CHECK(target.readTransform(child.id).value == child.transform);
    CHECK(transactions.redo().accepted());
    CHECK(target.readTransform(child.id).value == moved);
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
    CHECK(operation.accepted());
    CHECK(commitOperation(target, transactions, *operation.value, "place").accepted());
    CHECK(target.sceneObject(request.id).accepted());
    CHECK(transactions.undo().accepted());
    CHECK_EQ(static_cast<int>(target.sceneObject(request.id).status), static_cast<int>(EditorStatus::NotFound));
}
