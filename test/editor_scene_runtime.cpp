#include "scene_editing/SceneTarget.h"

#include "scene/NodeDesc.h"
#include "scene/SceneHost.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <utility>

using namespace eve::scene_editing;

namespace {

template <class T>
T* take(eve::Result<T*> result) {
    REQUIRE(result.ok());
    return std::move(result).takeValue();
}

}  // namespace

TEST_CASE("editor.scene.live_host_applies_incremental_hierarchy_and_trs_without_losing_links") {
    using namespace eve::scene;
    SceneHost* host = take(SceneHost::createHost("editor-live"));
    host->setTree(node("root", {node("mesh").withPosition(1.f, 2.f, 3.f), node("socket")}));
    SceneNode* mesh = take(host->findById("mesh"));
    int external = 42;
    mesh->links.push_back({777, &external, 0});

    SceneHostEditorTarget target("live", host);
    auto* inspector = static_cast<ISceneComponentInspector*>(
        target.queryCapability(ISceneComponentInspector::editorCapabilityId()));
    REQUIRE(inspector);
    auto links = inspector->componentLinks(ObjectId("mesh"));
    REQUIRE(links.ok()); REQUIRE_EQ(links.value().size(), size_t{1});
    CHECK(links.value().at(0).targetAlive);
    CHECK_EQ(links.value().at(0).syncMode, 0);
    auto rename = target.makeRename(ObjectId("mesh"), "Hero Mesh");
    REQUIRE(rename.ok());
    REQUIRE(target.applyDomainOperation(rename.value()).ok());
    mesh = take(host->findById("mesh"));
    CHECK_EQ(mesh->name, "Hero Mesh");
    REQUIRE_EQ(mesh->links.size(), size_t{1});
    CHECK_EQ(mesh->links[0].target, &external);

    SceneTransformValue transform{4.0, 5.0, 6.0, 0.1, 0.2, 0.3, 2.0, 3.0, 4.0};
    auto move = target.makeSetTransform(ObjectId("mesh"), transform);
    REQUIRE(move.ok());
    REQUIRE(target.applyDomainOperation(move.value()).ok());
    mesh = take(host->findById("mesh"));
    CHECK_EQ(mesh->x, 4.f);
    CHECK_EQ(mesh->pitch, 0.1f);
    CHECK_EQ(mesh->yaw, 0.2f);
    CHECK_EQ(mesh->sx, 2.f);
    CHECK_EQ(mesh->links[0].target, &external);

    auto reparent = target.makeReparent(ObjectId("mesh"), ObjectId("socket"));
    REQUIRE(reparent.ok());
    REQUIRE(target.applyDomainOperation(reparent.value()).ok());
    CHECK_EQ(host->getParentById("mesh")->id, "socket");
}

TEST_CASE("editor.scene.live_host_staged_create_delete_and_conflict_are_atomic") {
    using namespace eve::scene;
    SceneHost* host = take(SceneHost::createHost("editor-live-create"));
    host->setTree(node("root", {node("existing")}));
    SceneHostEditorTarget target("live-create", host);

    CreateSceneObjectRequest request{ObjectId("created"), ObjectId("root"), "Created",
                                     SceneTransformValue{1.0, 0.0, 0.0}};
    auto create = target.makeCreate(request);
    REQUIRE(create.ok());
    REQUIRE(target.applyDomainOperation(create.value()).ok());
    CHECK(host->hasNode("created"));
    CHECK_EQ(host->getParentById("created")->id, "root");

    auto remove = target.makeDelete(ObjectId("created"));
    REQUIRE(remove.ok());
    REQUIRE(target.applyDomainOperation(remove.value()).ok());
    CHECK(!host->hasNode("created"));

    auto staged = target.cloneDomainState();
    auto* stagedScene = dynamic_cast<SceneHostEditorTarget*>(staged.get());
    REQUIRE(stagedScene);
    auto rename = stagedScene->makeRename(ObjectId("existing"), "Changed");
    REQUIRE(rename.ok());
    REQUIRE(stagedScene->SceneTargetBase::applyDomainOperation(rename.value()).ok());
    REQUIRE_EQ(static_cast<int>(host->renameNode("existing", "External Change")),
               static_cast<int>(eve::scene::SceneMutationStatus::Applied));
    const auto conflict = target.commitDomainState(std::move(staged));
    CHECK_EQ(static_cast<int>(conflict.code()), static_cast<int>(EditorStatus::Conflict));
    CHECK_EQ(take(host->findById("existing"))->name, "External Change");
    CHECK_EQ(target.sceneObject(ObjectId("existing")).value().name, "existing");
}
