// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "scene.cpp",
    "scene_visibility.cpp"
  ]
}
*/
// clang-format on

#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_scene)
#include "PrivateTestScope.h"
#include "common/ECS.h"
#include "graphics/RenderSystem3D.h"
#include "scene/NodeDesc.h"
#include "scene/Scene.h"
#include "scene/SceneComponent.h"
#include "scene/SceneHost.h"
#include "scene/SceneNodeRef.h"
#include "scene/SceneObject.h"
#include "scene/TransformSystem.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <string>
#include <utility>
#include <vector>

// scene.cpp
namespace scene_contract {
using namespace eve::scene;

SceneHost* host(const char* name, NodeDesc desc) {
    auto created = SceneHost::createHost(name);
    REQUIRE(created.ok());
    auto* result = created.value();
    result->setTree(std::move(desc));
    return result;
}

SceneNode* find(SceneHost* host, const char* id) {
    auto found = host->findById(id);
    REQUIRE(found.ok());
    return found.value();
}

bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

TEST_CASE("scene.tree.declarativeNestedIdsBuildParentAndPathIndex") {
    auto* h =
        host("tree-contract",
             node("root", {node("player", {node("weapon").withName("Sword")}), group({node("marker")}, "markers")}));
    CHECK_EQ(h->getNodeCount(), 5);
    CHECK_EQ(h->getRoot()->id, std::string("root"));
    CHECK_EQ(h->getParentById("weapon")->id, std::string("player"));
    CHECK_EQ(h->getChildCountById("root"), 2);
    CHECK_EQ(h->getPathById("weapon"), std::string("root/player/weapon"));
    CHECK_EQ(h->findByPath("root/player/weapon").value()->id, std::string("weapon"));
    CHECK_EQ(h->findByName("Sword").value()->id, std::string("weapon"));
    CHECK(h->isAncestorOfById("root", "weapon"));
    CHECK(!h->isAncestorOfById("markers", "weapon"));
    CHECK(!h->findById("missing").ok());
}

TEST_CASE("scene.reconcile.propertyPatchKeepsIdentityStructureChangeRebuilds") {
    auto*       h        = host("reconcile-contract", node("root", {node("player").withPosition(0.f, 0.f, 0.f)}));
    auto*       before   = find(h, "player");
    const auto* stableId = &before->id;
    CHECK(!h->setTreeReconcile(node("root", {node("player").withPosition(3.f, 4.f, 5.f)})));
    auto* patched = find(h, "player");
    CHECK(&patched->id == stableId);
    CHECK_EQ(patched->x, 3.f);
    CHECK_EQ(patched->y, 4.f);
    CHECK_EQ(patched->z, 5.f);
    CHECK(h->setTreeReconcile(node("root", {node("player"), node("other")})));
    CHECK(h->findById("other").ok());
}

TEST_CASE("scene.transform.parentChildWorldAndSpaceConversion") {
    auto* scene   = Scene::create();
    auto  mounted = scene->mountAs(
        "transform-contract", node("root", {node("child").withPosition(1.f, 2.f, 3.f)}).withPosition(10.f, 20.f, 30.f));
    REQUIRE(mounted.ok());
    scene->updateTransformsAll();
    auto world = scene->getNodeWorldPositionAt("transform-contract", "child");
    REQUIRE_EQ(world.size(), std::size_t{3});
    CHECK(near(world[0], 11.f));
    CHECK(near(world[1], 22.f));
    CHECK(near(world[2], 33.f));
    auto localToWorld = scene->localToWorldAt("transform-contract", "child", 2.f, 3.f, 4.f);
    CHECK(near(localToWorld[0], 13.f));
    CHECK(near(localToWorld[1], 25.f));
    CHECK(near(localToWorld[2], 37.f));
    auto worldToLocal = scene->worldToLocalAt("transform-contract", "child", 13.f, 25.f, 37.f);
    CHECK(near(worldToLocal[0], 2.f));
    CHECK(near(worldToLocal[1], 3.f));
    CHECK(near(worldToLocal[2], 4.f));
}

TEST_CASE("scene.hierarchy.rejectsCyclesAndPreservesNodeOnDetach") {
    auto* h = host("hierarchy-contract", node("root", {node("a"), node("b")}));
    REQUIRE(h->setParentById("b", "a"));
    CHECK_EQ(h->getParentById("b")->id, std::string("a"));
    CHECK(!h->setParentById("a", "b"));
    CHECK_EQ(h->getParentById("b")->id, std::string("a"));
    REQUIRE(h->removeChildById("a", "b"));
    CHECK(h->getParentById("b") == nullptr);
    CHECK(h->hasNode("b"));
}

TEST_CASE("scene.module.mountSelectFindOwnerAndSelectedNodeMutation") {
    auto* scene = Scene::create();
    CHECK(!scene->select("absent"));
    CHECK(!scene->findHost("absent").ok());
    auto mounted = scene->mountAs("module-contract", node("root", {node("child")}));
    REQUIRE(mounted.ok());
    CHECK(scene->select("module-contract"));
    CHECK(scene->current() == mounted.value());
    scene->bindOwner(91);
    auto byOwner = scene->findHostByOwner(91);
    REQUIRE(byOwner.ok());
    CHECK(byOwner.value() == mounted.value());
    CHECK(!scene->findHostByOwner(0).ok());
    REQUIRE(scene->setNodePosition("child", 2.f, 3.f, 4.f));
    REQUIRE(scene->setNodeScale("child", 2.f, 2.f, 2.f));
    REQUIRE(scene->setNodeVisible("child", false));
    CHECK_EQ(scene->getNodePositionAt("module-contract", "child")[0], 2.f);
    CHECK_EQ(scene->getNodeScaleAt("module-contract", "child")[1], 2.f);
    CHECK(!scene->getNodeVisibleAt("module-contract", "child"));
    CHECK(!scene->setNodePosition("missing", 1.f, 2.f, 3.f));
    scene->setHostVisible(false);
    CHECK(!mounted.value()->meta()->visible);
    scene->setHostLayer(5);
    CHECK_EQ(mounted.value()->meta()->layer, 5);
}

TEST_CASE("scene.tags.layersAndQueriesReflectMutations") {
    auto* scene = Scene::create();
    REQUIRE(scene->mountAs("tags-contract", node("root", {node("enemy").withTag("hostile").withLayer(2), node("ally")}))
                .ok());
    CHECK(scene->hasNodeTagAt("tags-contract", "enemy", "hostile"));
    REQUIRE(scene->addNodeTagAt("tags-contract", "enemy", "boss"));
    CHECK_EQ(scene->collectIdsByTagAt("tags-contract", "boss").size(), std::size_t{1});
    REQUIRE(scene->removeNodeTagAt("tags-contract", "enemy", "boss"));
    CHECK(!scene->hasNodeTagAt("tags-contract", "enemy", "boss"));
    CHECK_EQ(scene->getNodeLayerAt("tags-contract", "enemy"), 2);
    REQUIRE(scene->setNodeLayerAt("tags-contract", "ally", 7));
    CHECK_EQ(scene->getNodeLayerAt("tags-contract", "ally"), 7);
    CHECK_EQ(scene->getParentId("enemy"), std::string("root"));
}

TEST_CASE("scene.serialize.roundTripAndMalformedInput") {
    auto* scene = Scene::create();
    REQUIRE(scene
                ->mountAs("save-contract",
                          node("root", {node("child").withPosition(1.f, 2.f, 3.f).withTag("enemy").withLayer(2)}))
                .ok());
    const auto saved = scene->serializeHostAt("save-contract");
    CHECK(saved.find("child") != std::string::npos);
    REQUIRE(scene->deserializeHostAt("restore-contract", saved));
    auto position = scene->getNodePositionAt("restore-contract", "child");
    REQUIRE_EQ(position.size(), std::size_t{3});
    CHECK(near(position[0], 1.f));
    CHECK(near(position[1], 2.f));
    CHECK(near(position[2], 3.f));
    CHECK(scene->hasNodeTagAt("restore-contract", "child", "enemy"));
    CHECK_EQ(scene->getNodeLayerAt("restore-contract", "child"), 2);
    CHECK(!scene->deserializeHostAt("invalid-contract", "{not json"));
    CHECK(!scene->findHost("invalid-contract").ok());
}

TEST_CASE("Scene.nodeRef.transformsAndStructure") {
    auto* scene = Scene::create();
    REQUIRE(scene->mountAs("ref-contract", node("root", {node("child").withPosition(1.f, 2.f, 3.f)})).ok());
    SceneNodeRef ref("ref-contract", "child");
    CHECK(ref.isValid());
    CHECK_EQ(ref.getPath(), std::string("root/child"));
    CHECK(ref.isVisible());
    REQUIRE(ref.setVisible(false));
    CHECK(!ref.isVisible());
    REQUIRE(ref.setVisible(true));
    CHECK(ref.isVisible());
    REQUIRE(ref.setPosition(4.f, 5.f, 6.f));
    scene->updateTransformsAll();
    CHECK_EQ(ref.getPositionX(), 4.f);
    CHECK_EQ(ref.getWorldPositionY(), 5.f);
    REQUIRE(scene->remountReconcile(node("root", {node("child").withPosition(7.f, 8.f, 9.f)})).ok());
    CHECK(ref.isValid());
    CHECK_EQ(ref.getPositionX(), 7.f);
}

TEST_CASE("Scene.nodeRef.invalidRefs") {
    auto* scene = Scene::create();
    REQUIRE(scene->mountAs("invalid-ref-contract", node("root", {node("present")})).ok());
    SceneNodeRef missing("invalid-ref-contract", "missing");
    CHECK(!missing.isValid());
    CHECK(!missing.setPosition(1.f, 2.f, 3.f));
    CHECK(!missing.setVisible(false));
    CHECK(missing.getPath().empty());
    SceneNodeRef missingHost("absent-ref-contract", "present");
    CHECK(!missingHost.isValid());
    CHECK(!missingHost.setVisible(true));
}

TEST_CASE("scene.links.purgeDestroyedRenderableWithoutRetainingBorrowedPointer") {
    auto* h          = host("link-contract", node("root", {node("mesh")}));
    auto* renderable = eve::graphics::Renderable3D::create();
    REQUIRE(renderable != nullptr);
    REQUIRE(h->linkRenderable3D("mesh", renderable));
    CHECK_EQ(h->linkCount("mesh"), 1);
    ecs::DestroyEntity(renderable);
    TransformSystem::updateHost(h);
    CHECK_EQ(h->linkCount("mesh"), 0);
}

TEST_CASE("scene.objectIdentity.copiesLocatorIntoEcsCompanion") {
    auto* scene = Scene::create();
    REQUIRE(scene->mountAs("identity-contract", node("root", {node("child")})).ok());
    auto found = SceneObject::createObject("identity-contract", "child");
    REQUIRE(found.ok());
    CHECK(found.value() != nullptr);
    CHECK_EQ(found.value()->meta()->hostName, std::string("identity-contract"));
    CHECK_EQ(found.value()->meta()->nodeId, std::string("child"));
    ecs::DestroyEntity(found.value());
}
}  // namespace scene_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_scene_visibility)
#include "PrivateTestScope.h"
#include "graphics/SceneVisibility.h"
#include "zeroerr/unittest.h"

#include <limits>

// scene_visibility.cpp
namespace scene_visibility_contract {
eve::Value request() {
    using V = eve::Value;
    return V::Object{
        {"schemaId", "eve.scene.visibility-request"},
        {"schemaVersion", 1},
        {"sourceRevision", 3},
        {"candidateGeneration", 7},
        {"width", 64},
        {"height", 64},
        {"camera", V::Object{{"eye", V::Array{0, 0, 10}},
                             {"target", V::Array{0, 0, 0}},
                             {"up", V::Array{0, 1, 0}},
                             {"fov", 60},
                             {"near", 0.1},
                             {"far", 100}}},
        {"draws", V::Array{V::Object{{"object", "target"},
                                     {"entityId", 1},
                                     {"generation", 1},
                                     {"model", V::Array{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}}}}},
        {"targets", V::Array{"target"}}};
}

TEST_CASE("scene.visibility.validRequestPreparesAndRejectsPixelBudgetUnknownField") {
    auto valid = request();
    REQUIRE(eve::graphics::SceneVisibilityQuery::prepare(valid).ok());
    auto budget = request();
    budget.set("width", 2048);
    budget.set("height", 2048);
    CHECK(!eve::graphics::SceneVisibilityQuery::prepare(budget).ok());
    auto unknown = request();
    unknown.set("unknown", 1);
    CHECK(!eve::graphics::SceneVisibilityQuery::prepare(unknown).ok());
}

TEST_CASE("scene.visibility.rejectsDegenerateCameraAndDuplicateDrawHandle") {
    auto  degenerate = request();
    auto* camera     = degenerate.find("camera");
    REQUIRE(camera != nullptr);
    camera->set("target", *camera->find("eye"));
    CHECK(!eve::graphics::SceneVisibilityQuery::prepare(degenerate).ok());
    auto  duplicate = request();
    auto* draws     = duplicate.find("draws")->getIf<eve::Value::Array>();
    REQUIRE(draws != nullptr);
    draws->push_back(draws->front());
    CHECK(!eve::graphics::SceneVisibilityQuery::prepare(duplicate).ok());
    auto nonfinite = request();
    nonfinite.find("camera")->set("fov", std::numeric_limits<double>::quiet_NaN());
    CHECK(!eve::graphics::SceneVisibilityQuery::prepare(nonfinite).ok());
}
}  // namespace scene_visibility_contract
#endif

#include <limits>
#include "scene/PcgBuildConfig.h"
#include "scene/PcgGrowth.h"

namespace test_pcg_local_growth_unit {
using namespace eve::scene;

TEST_CASE("scene.pcgBuildConfig.preservesOrderedHistory") {
    PcgBuildConfig c;
    CHECK_EQ(c.getPublicationType(), 1);
    REQUIRE(c.setPublicationType(0).ok());
    REQUIRE(c.addHistory(4, "World", 123456789).ok());
    REQUIRE(c.addHistory(0, "World", 123456790).ok());
    CHECK_EQ(c.getHistoryCount(), uint64_t(2));
    auto a = c.getHistoryCategory(0);
    auto n = c.getHistorySceneName(1);
    auto t = c.getHistoryTimestamp(1);
    REQUIRE(a.ok());
    REQUIRE(n.ok());
    REQUIRE(t.ok());
    CHECK_EQ(a.value(), 4);
    CHECK_EQ(n.value(), "World");
    CHECK_EQ(t.value(), int64_t(123456790));
    c.clearHistory();
    CHECK_EQ(c.getHistoryCount(), uint64_t(0));
}

TEST_CASE("scene.pcgBuildConfig.rejectsInvalidEnumsAndIndices") {
    PcgBuildConfig c;
    CHECK(!c.setPublicationType(2).ok());
    CHECK_EQ(c.getPublicationType(), 1);
    CHECK(!c.addHistory(7, "", 0).ok());
    CHECK_EQ(c.getHistoryCount(), uint64_t(0));
    CHECK(!c.getHistoryCategory(0).ok());
}

TEST_CASE("scene.pcgGrowth.linearVarianceAndDelayedDeath") {
    PcgGrowth g;
    REQUIRE(g.configure(.15f, 1, .25f, 5).ok());
    REQUIRE(g.start(10, 77).ok());
    float end = g.getActualEndScale();
    CHECK_GE(end, 1.f);
    CHECK_LT(end, 1.25f);
    auto start = g.advance(10);
    REQUIRE(start.ok());
    CHECK_EQ(start.value(), .15f);
    auto mid = g.advance(12.5f);
    REQUIRE(mid.ok());
    CHECK_GT(mid.value(), .15f);
    CHECK_LT(mid.value(), end);
    auto done = g.advance(15);
    REQUIRE(done.ok());
    CHECK_EQ(done.value(), end);
    CHECK(g.getFinished());
    REQUIRE(g.die(20).ok());
    auto early = g.shouldDestroy(24.999f);
    REQUIRE(early.ok());
    CHECK(!early.value());
    auto due = g.shouldDestroy(25);
    REQUIRE(due.ok());
    CHECK(due.value());
}

TEST_CASE("scene.pcgGrowth.rejectsInvalidInputAtomically") {
    PcgGrowth g;
    REQUIRE(g.configure(.2f, 1, .2f, 2).ok());
    REQUIRE(g.start(4, 9).ok());
    float before = g.getActualEndScale();
    CHECK(!g.configure(0, 1, .2f, 2).ok());
    CHECK_EQ(g.getActualEndScale(), before);
    CHECK(!g.advance(3).ok());
    CHECK_EQ(g.getScale(), .2f);
    CHECK(!g.die(std::numeric_limits<float>::quiet_NaN()).ok());
}
}  // namespace test_pcg_local_growth_unit
