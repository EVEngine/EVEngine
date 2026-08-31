#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "scene/NodeDesc.h"
#include "scene/Scene.h"
#include "scene/SceneComponent.h"
#include "scene/SceneHost.h"
#include "scene/SceneNodeRef.h"
#include "scene/SceneObject.h"
#include "scene/TransformSystem.h"

#include "graphics/AmbientOcclusion.h"
#include "graphics/AntiAliasing.h"
#include "graphics/Canvas.h"
#include "graphics/DrawItem2D.h"
#include "graphics/Font.h"
#include "graphics/GBuffer.h"
#include "graphics/GlobalIllumination.h"
#include "graphics/Graphics.h"
#include "graphics/Grass.h"
#include "graphics/Light.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/Outline.h"
#include "graphics/Quad.h"
#include "graphics/RenderControl.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/ScreenSpaceReflection.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"
#include "physics/Body.h"
#include "physics/Body3D.h"
#include "physics/World.h"
#include "physics/World3D.h"
#include "spatial/Octree.h"
#include "window/Window.h"

#include "common/ECS.h"
#include "common/Capability.h"
#include "common/Module.h"
#include "common/ProcgenSceneSink.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <SDL2/SDL.h>

#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <string>
#include <utility>

using namespace eve::scene;
using namespace eve::graphics;

namespace {

bool approxEq(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }

bool matApproxEq(const glm::mat4 &a, const glm::mat4 &b, float eps = 1e-4f) {
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            if (!approxEq(a[c][r], b[c][r], eps)) return false;
        }
    }
    return true;
}

template <class T>
T *sceneValue(eve::Result<T *> result) {
    CHECK(result.ok());
    if (!result.ok()) return nullptr;
    return std::move(result).takeValue();
}

class BattleScene : public SceneComponent {
public:
    bool showAlly = true;
    NodeDesc slot;

    NodeDesc build() override {
        std::vector<NodeDesc> kids = {
            node("map").withPosition(0.f, 0.f, 0.f),
            node("player").withPosition(1.f, 0.f, 0.f),
            when(showAlly, node("ally").withPosition(2.f, 0.f, 0.f)),
        };
        if (slot.id.empty()) kids.push_back(group({}, "__slot_empty"));
        else kids.push_back(slot);
        return node("battle", std::move(kids), "battle");
    }
};

}  // namespace

TEST_CASE("Scene.NodeDesc.composeNestedTree") {
    SceneHost *h = sceneValue(SceneHost::createHost("nest"));
    h->setTree(node("root",
                    {
                        node("a").withPosition(1.f, 0.f, 0.f),
                        group({node("b").withPosition(0.f, 2.f, 0.f)}, "g"),
                    }));

    auto t = h->tree();
    REQUIRE_EQ(t->root, 0);
    REQUIRE_GE(t->nodes.size(), 4u);
    CHECK(sceneValue(h->findById("a")) != nullptr);
    CHECK(sceneValue(h->findById("b")) != nullptr);
    CHECK_EQ(sceneValue(h->findById("root"))->firstChild, 1);
}

TEST_CASE("Scene.reconcile.patchesPropsKeepsIdentity") {
    SceneHost *h = sceneValue(SceneHost::createHost("recon"));
    h->setTree(node("root", {node("player").withPosition(0.f, 0.f, 0.f)}));
    SceneNode *before = sceneValue(h->findById("player"));
    REQUIRE(before != nullptr);
    const std::string *idPtr = &before->id;

    bool rebuilt = h->setTreeReconcile(node("root", {node("player").withPosition(3.f, 4.f, 5.f)}));
    CHECK(!rebuilt);
    SceneNode *after = sceneValue(h->findById("player"));
    REQUIRE(after != nullptr);
    CHECK_EQ(&after->id, idPtr);
    CHECK(approxEq(after->x, 3.f));
    CHECK(approxEq(after->y, 4.f));
    CHECK(approxEq(after->z, 5.f));
}

TEST_CASE("Scene.reconcile.structureChangeRebuilds") {
    SceneHost *h = sceneValue(SceneHost::createHost("recon2"));
    h->setTree(node("root", {node("a")}));
    bool rebuilt = h->setTreeReconcile(node("root", {node("a"), node("b")}));
    CHECK(rebuilt);
    CHECK(sceneValue(h->findById("b")) != nullptr);
}

TEST_CASE("Scene.TransformSystem.parentChildWorld") {
    SceneHost *h = sceneValue(SceneHost::createHost("xf"));
    h->setTree(node("root", {node("child").withPosition(1.f, 0.f, 0.f)}).withPosition(10.f, 0.f, 0.f));
    TransformSystem::updateHost(h);

    SceneNode *root  = sceneValue(h->findById("root"));
    SceneNode *child = sceneValue(h->findById("child"));
    REQUIRE(root != nullptr);
    REQUIRE(child != nullptr);

    glm::mat4 expectRoot = glm::translate(glm::mat4(1.f), glm::vec3(10.f, 0.f, 0.f));
    glm::mat4 expectChild = expectRoot * glm::translate(glm::mat4(1.f), glm::vec3(1.f, 0.f, 0.f));
    CHECK(matApproxEq(root->world, expectRoot));
    CHECK(matApproxEq(child->world, expectChild));
}

TEST_CASE("Scene.hierarchy.cycleRejected") {
    SceneHost *h = sceneValue(SceneHost::createHost("cycle"));
    h->setTree(node("root", {node("a", {node("b")})}));
    int root = h->findIndexById("root");
    int a = h->findIndexById("a");
    int b = h->findIndexById("b");
    REQUIRE_GE(root, 0);
    REQUIRE_GE(a, 0);
    REQUIRE_GE(b, 0);

    CHECK(!h->setParent(root, b));
    CHECK(h->setParent(b, root));
    CHECK_EQ(h->tree()->nodes[size_t(b)].parent, root);
}

TEST_CASE("Scene.SceneComponent.buildWhenAndSlot") {
    BattleScene comp;
    comp.slot = node("hud").withPosition(0.f, 1.f, 0.f);
    comp.mountAs("battle_host");

    SceneHost *h = sceneValue(Scene::create()->findHost("battle_host"));
    REQUIRE(h != nullptr);
    CHECK(sceneValue(h->findById("ally")) != nullptr);
    CHECK(sceneValue(h->findById("hud")) != nullptr);

    comp.showAlly = false;
    comp.markDirty();
    CHECK(comp.updateIfDirty());
    CHECK(sceneValue(h->findById("ally")) == nullptr);
    CHECK(sceneValue(h->findById("hud")) != nullptr);
}

TEST_CASE("Scene.module.mountAndSetNode") {
    Scene *mod = Scene::create();
    mod->mountAs("main", node("root", {node("p").withPosition(0.f, 0.f, 0.f)})).ignore("test setup");
    CHECK(mod->select("main"));
    CHECK(mod->setNodePosition("p", 2.f, 3.f, 4.f));
    TransformSystem::updateHost(mod->current());
    SceneNode *p = sceneValue(mod->current()->findById("p"));
    REQUIRE(p != nullptr);
    CHECK(approxEq(p->x, 2.f));
    CHECK(approxEq(p->world[3][0], 2.f));
}

TEST_CASE("Scene.whenElse") {
    NodeDesc a = whenElse(true, node("yes"), node("no"));
    CHECK(a.id == "yes");
    NodeDesc b = whenElse(false, node("yes"), node("no"));
    CHECK(b.id == "no");
}

TEST_CASE("Scene.builder.mountBuildAs") {
    Scene *mod = Scene::create();
    mod->beginBuild();
    mod->beginNode("root");
    mod->setBuildPosition(1.f, 0.f, 0.f);
    mod->addNode("child");
    mod->end();
    CHECK(mod->mountBuildAs("built"));
    SceneHost *h = sceneValue(mod->findHost("built"));
    REQUIRE(h != nullptr);
    CHECK(sceneValue(h->findById("root")) != nullptr);
    CHECK(sceneValue(h->findById("child")) != nullptr);
    CHECK(approxEq(sceneValue(h->findById("root"))->x, 1.f));
}

TEST_CASE("Scene.identity.mountFindAndOwnershipContracts") {
    Scene *mod     = Scene::create();
    auto   mounted = mod->mountAs("checked", node("root", {node("child")}));
    REQUIRE(mounted.ok());
    SceneHost *host = mounted.value();
    REQUIRE(host != nullptr);

    auto found = mod->findHost("checked");
    REQUIRE(found.ok());
    CHECK_EQ(found.value(), host);
    auto missingHost = mod->findHost("missing");
    CHECK(!missingHost.ok());

    auto foundNode = host->findById("child");
    REQUIRE(foundNode.ok());
    CHECK_EQ(foundNode.value()->id, std::string("child"));
    auto missingNode = host->findById("missing");
    CHECK(!missingNode.ok());

    auto object = SceneObject::createObject("checked", "child");
    REQUIRE(object.ok());
    CHECK(object.value() != nullptr);
}

TEST_CASE("Scene.procgenSink.reconcilesAndClearsBatchHosts") {
    Scene *mod = Scene::create();
    auto *sink = eve::cap::query<eve::IProcgenSceneSink>();
    REQUIRE(sink != nullptr);

    std::vector<eve::ProcgenInstanceDesc> instances(2);
    instances[0].sourcePointId = 101;
    instances[0].id     = "tree-1";
    instances[0].asset  = "oak";
    instances[0].x      = 3.f;
    instances[0].scaleY = 2.f;
    instances[1].sourcePointId = 102;
    instances[1].id     = "rock-2";
    instances[1].asset  = "granite";
    instances[1].z      = 8.f;
    CHECK(sink->applyBatch("biome/0/0", instances));
    CHECK_EQ(sink->instanceCount("biome/0/0"), 2);
    CHECK_EQ(sink->lastCreatedCount("biome/0/0"), 2);
    CHECK_EQ(sink->lastReusedCount("biome/0/0"), 0);
    CHECK_EQ(sink->batchRevision("biome/0/0"), uint64_t(1));

    auto hostResult = mod->findHost("__pcg/biome/0/0");
    REQUIRE(hostResult.ok());
    SceneHost *host = hostResult.value();
    REQUIRE(host != nullptr);
    CHECK_EQ(host->getNodeCount(), 3);
    auto treeResult = host->findById("tree-1");
    REQUIRE(treeResult.ok());
    SceneNode *tree = treeResult.value();
    REQUIRE(tree != nullptr);
    CHECK(approxEq(tree->x, 3.f));
    CHECK(approxEq(tree->sy, 2.f));
    CHECK(host->hasTag(tree, "pcg.asset:oak"));
    auto *pooledRenderable = eve::graphics::Renderable3D::create();
    REQUIRE(host->linkRenderable3D("tree-1", pooledRenderable));

    eve::ProcgenInstanceDelta delta;
    delta.baseRevision   = 1;
    delta.targetRevision = 2;
    instances[0].x       = 7.f;
    delta.updated.push_back(instances[0]);
    delta.removedPointIds.push_back(102);
    eve::ProcgenInstanceDesc flower;
    flower.sourcePointId = 103;
    flower.id            = "flower-3";
    flower.asset         = "lily";
    flower.y             = 4.f;
    delta.added.push_back(flower);
    delta.targetPointOrder = {103, 101};
    auto applied           = sink->applyDelta("biome/0/0", delta);
    REQUIRE(applied.ok());
    CHECK_EQ(applied.value(), uint64_t(2));
    CHECK_EQ(sink->batchRevision("biome/0/0"), uint64_t(2));
    CHECK_EQ(sink->instanceCount("biome/0/0"), 2);
    CHECK_EQ(host->getNodeCount(), 3);
    auto updatedTreeResult = host->findById("tree-1");
    REQUIRE(updatedTreeResult.ok());
    CHECK(approxEq(updatedTreeResult.value()->x, 7.f));
    auto removedRockResult = host->findById("rock-2");
    CHECK(!removedRockResult.ok());
    auto flowerResult = host->findById("flower-3");
    REQUIRE(flowerResult.ok());
    CHECK(approxEq(flowerResult.value()->y, 4.f));
    CHECK_EQ(host->linkCount("tree-1"), 1);
    CHECK_EQ(sink->lastCreatedCount("biome/0/0"), 1);
    CHECK_EQ(sink->lastReusedCount("biome/0/0"), 1);
    CHECK_EQ(sink->lastRemovedCount("biome/0/0"), 1);

    eve::ProcgenInstanceDelta unknownRemoval;
    unknownRemoval.baseRevision   = 2;
    unknownRemoval.targetRevision = 3;
    unknownRemoval.removedPointIds.push_back(999);
    unknownRemoval.targetPointOrder = {103, 101};
    auto unknown                    = sink->applyDelta("biome/0/0", unknownRemoval);
    CHECK(!unknown.ok());
    CHECK_EQ(sink->batchRevision("biome/0/0"), uint64_t(2));
    CHECK_EQ(host->getNodeCount(), 3);
    CHECK(approxEq(host->findById("tree-1").value()->x, 7.f));

    eve::ProcgenInstanceDelta renamed;
    renamed.baseRevision   = 2;
    renamed.targetRevision = 3;
    auto renamedTree       = instances[0];
    renamedTree.id         = "tree-renamed";
    renamedTree.x          = 8.f;
    renamed.updated.push_back(renamedTree);
    renamed.targetPointOrder = {101, 103};
    auto renamedResult       = sink->applyDelta("biome/0/0", renamed);
    REQUIRE(renamedResult.ok());
    CHECK_EQ(sink->batchRevision("biome/0/0"), uint64_t(3));
    CHECK(!host->findById("tree-1").ok());
    REQUIRE(host->findById("tree-renamed").ok());
    CHECK(approxEq(host->findById("tree-renamed").value()->x, 8.f));

    std::vector<eve::ProcgenInstanceDesc> recoveredInstances;
    flower.y = 6.f;
    recoveredInstances.push_back(flower);
    recoveredInstances.push_back(renamedTree);
    auto recovered = sink->replaceBatch("biome/0/0", 6, recoveredInstances);
    REQUIRE(recovered.ok());
    CHECK_EQ(recovered.value(), uint64_t(6));
    CHECK_EQ(sink->batchRevision("biome/0/0"), uint64_t(6));
    CHECK(approxEq(host->findById("flower-3").value()->y, 6.f));
    auto staleRecovery = sink->replaceBatch("biome/0/0", 5, instances);
    CHECK(!staleRecovery.ok());
    CHECK_EQ(sink->batchRevision("biome/0/0"), uint64_t(6));
    CHECK(approxEq(host->findById("flower-3").value()->y, 6.f));

    delta.baseRevision = 1;
    delta.updated[0].x = 99.f;
    auto stale         = sink->applyDelta("biome/0/0", delta);
    CHECK(!stale.ok());
    CHECK_EQ(sink->batchRevision("biome/0/0"), uint64_t(6));
    CHECK(approxEq(host->findById("tree-renamed").value()->x, 8.f));

    CHECK(sink->removeBatch("biome/0/0"));
    CHECK_EQ(sink->lastRemovedCount("biome/0/0"), 2);
    ecs::DestroyEntity(pooledRenderable);
    CHECK_EQ(sink->instanceCount("biome/0/0"), 0);
    CHECK_EQ(host->getNodeCount(), 1);
}

TEST_CASE("Scene.procgenSink.replacesMultipleBatchesAtomically") {
    Scene* mod  = Scene::create();
    auto*  sink = eve::cap::query<eve::IProcgenSceneSink>();
    REQUIRE(sink != nullptr);

    eve::ProcgenInstanceDesc a1;
    a1.id            = "a1";
    a1.sourcePointId = 101;
    a1.x             = 1.f;
    eve::ProcgenInstanceDesc b1;
    b1.id            = "b1";
    b1.sourcePointId = 201;
    b1.x             = 2.f;
    REQUIRE(sink->replaceBatch("tx/a", 1, {a1}).ok());
    REQUIRE(sink->replaceBatch("tx/b", 1, {b1}).ok());

    auto a2 = a1;
    a2.x    = 10.f;
    auto b2 = b1;
    b2.x    = 20.f;
    std::vector<eve::ProcgenBatchSnapshot> transaction{
        {"tx/a", 2, {a2}},
        {"tx/b", 2, {b2}},
    };
    auto committed = sink->replaceBatches(transaction);
    REQUIRE(committed.ok());
    CHECK_EQ(committed.value(), uint64_t(2));
    CHECK_EQ(sink->batchRevision("tx/a"), uint64_t(2));
    CHECK_EQ(sink->batchRevision("tx/b"), uint64_t(2));

    auto hostAResult = mod->findHost("__pcg/tx/a");
    auto hostBResult = mod->findHost("__pcg/tx/b");
    REQUIRE(hostAResult.ok());
    REQUIRE(hostBResult.ok());
    SceneHost* hostA = hostAResult.value();
    SceneHost* hostB = hostBResult.value();
    CHECK(approxEq(hostA->findById("a1").value()->x, 10.f));
    CHECK(approxEq(hostB->findById("b1").value()->x, 20.f));
    transaction[0].targetRevision = 3;
    transaction[0].instances[0].x = 30.f;
    transaction[1].targetRevision = 2;
    transaction[1].instances[0].x = 40.f;
    auto rejected                 = sink->replaceBatches(transaction);
    REQUIRE(!rejected.ok());
    CHECK_EQ(sink->batchRevision("tx/a"), uint64_t(2));
    CHECK_EQ(sink->batchRevision("tx/b"), uint64_t(2));
    CHECK(approxEq(hostA->findById("a1").value()->x, 10.f));
    CHECK(approxEq(hostB->findById("b1").value()->x, 20.f));

    transaction[1].targetRevision = 3;
    transaction[1].instances.push_back(transaction[1].instances.front());
    auto invalid = sink->replaceBatches(transaction);
    REQUIRE(!invalid.ok());
    CHECK_EQ(sink->batchRevision("tx/a"), uint64_t(2));
    CHECK_EQ(sink->batchRevision("tx/b"), uint64_t(2));
    CHECK(approxEq(hostA->findById("a1").value()->x, 10.f));
    CHECK(approxEq(hostB->findById("b1").value()->x, 20.f));
}

TEST_CASE("Scene.link.syncRenderable3DWorld") {
    SceneHost *h = sceneValue(SceneHost::createHost("link3d"));
    h->setTree(node("root", {node("mesh").withPosition(1.f, 2.f, 3.f)}).withPosition(10.f, 0.f, 0.f));

    auto *r = eve::graphics::Renderable3D::create();
    CHECK(h->linkRenderable3D("mesh", r));
    TransformSystem::updateHost(h);

    CHECK(approxEq(r->transform()->x, 11.f));
    CHECK(approxEq(r->transform()->y, 2.f));
    CHECK(approxEq(r->transform()->z, 3.f));

    // Rebuild preserves link by id
    h->setTree(node("root", {node("mesh").withPosition(0.f, 0.f, 0.f)}).withPosition(5.f, 0.f, 0.f));
    TransformSystem::updateHost(h);
    SceneNode *mesh = sceneValue(h->findById("mesh"));
    REQUIRE(mesh != nullptr);
    REQUIRE_EQ(mesh->links.size(), 1u);
    CHECK(mesh->links[0].target == r);
    CHECK(approxEq(r->transform()->x, 5.f));
}

TEST_CASE("Scene.link.syncRenderable2DWorld") {
    SceneHost *h = sceneValue(SceneHost::createHost("link2d"));
    h->setTree(node("root", {node("spr").withSpace("2d").withPosition(2.f, 3.f, 0.f)})
                   .withSpace("2d")
                   .withPosition(4.f, 5.f, 0.f));

    auto *r = eve::graphics::Renderable2D::create();
    CHECK(h->linkRenderable2D("spr", r));
    TransformSystem::updateHost(h);
    CHECK(approxEq(r->transform()->x, 6.f));
    CHECK(approxEq(r->transform()->y, 8.f));
}

TEST_CASE("Scene.query.pathParentChildren") {
    SceneHost *h = sceneValue(SceneHost::createHost("query"));
    h->setTree(node("root",
                    {
                        node("player", {node("weapon").withName("gun")}),
                        node("enemy").withName("goblin"),
                    }));

    CHECK(h->hasNode("weapon"));
    CHECK_EQ(h->getNodeCount(), 4);
    CHECK(h->getRoot()->id == "root");
    CHECK(h->getParentById("weapon")->id == "player");
    CHECK_EQ(h->getChildCountById("root"), 2);
    CHECK(h->getChildAtById("root", 0)->id == "player");
    CHECK(h->getPathById("weapon") == "root/player/weapon");
    CHECK(sceneValue(h->findByPath("root/player/weapon"))->id == "weapon");
    CHECK(sceneValue(h->findByPath("player/weapon"))->id == "weapon");
    CHECK(sceneValue(h->findByName("gun"))->id == "weapon");
    CHECK(h->isAncestorOfById("root", "weapon"));
    CHECK(h->isDescendantOfById("weapon", "player"));
    CHECK(!h->isAncestorOfById("enemy", "weapon"));
}

TEST_CASE("Scene.query.walkFilterCollect") {
    SceneHost *h = sceneValue(SceneHost::createHost("walk"));
    h->setTree(node("root",
                    {
                        node("a", {node("a1")}),
                        node("b").withVisible(false),
                    }));

    std::vector<std::string> dfs;
    h->walkDepthFirst([&](SceneHost *, int, SceneNode &n) { dfs.push_back(n.id); });
    REQUIRE_EQ(dfs.size(), 4u);
    CHECK(dfs[0] == "root");
    CHECK(dfs[1] == "a");
    CHECK(dfs[2] == "a1");
    CHECK(dfs[3] == "b");

    std::vector<std::string> bfs;
    h->walkBreadthFirst([&](SceneHost *, int, SceneNode &n) { bfs.push_back(n.id); });
    REQUIRE_EQ(bfs.size(), 4u);
    CHECK(bfs[0] == "root");
    CHECK(bfs[1] == "a");
    CHECK(bfs[2] == "b");
    CHECK(bfs[3] == "a1");

    auto hidden = h->findAllVisible(false);
    REQUIRE_EQ(hidden.size(), 1u);
    CHECK(hidden[0]->id == "b");

    auto underA = h->collectIdsFrom(h->findIndexById("a"));
    REQUIRE_EQ(underA.size(), 2u);
    CHECK(underA[0] == "a");
    CHECK(underA[1] == "a1");

    SceneNode *found = h->findIf([&](SceneHost *, int, const SceneNode &n) { return n.id == "a1"; });
    REQUIRE(found != nullptr);
    CHECK(found->id == "a1");
}

TEST_CASE("Scene.module.queryWrappers") {
    Scene *mod = Scene::create();
    mod->mountAs("q", node("root", {node("p", {node("c").withName("child")})})).ignore("test setup");
    CHECK(mod->select("q"));
    CHECK(mod->hasNode("c"));
    CHECK(mod->getRootId() == "root");
    CHECK(mod->getParentId("c") == "p");
    CHECK_EQ(mod->getChildCount("root"), 1);
    CHECK(mod->getChildIdAt("p", 0) == "c");
    CHECK(mod->findIdByName("child") == "c");
    CHECK(mod->findIdByPath("root/p/c") == "c");
    CHECK(mod->getNodePath("c") == "root/p/c");
    CHECK(mod->isAncestor("root", "c"));
    CHECK(mod->isDescendant("c", "p"));
    auto ids = mod->collectIds();
    CHECK_EQ(ids.size(), 3u);
    auto bfs = mod->walkBreadthFirstIds();
    CHECK_EQ(bfs.size(), 3u);
    CHECK(bfs[0] == "root");
    CHECK(bfs[1] == "p");
    CHECK(bfs[2] == "c");
}

TEST_CASE("Scene.render.parentChildOrbitPreview") {
    if (ecs::current()->getManager<Renderable2D>() != nullptr) {
        auto view = ecs::View<Renderable2D, Renderable2D::Sprite>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [sp] = *it;
            sp->visible = false;
        }
    }

    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width = 640;
    s.height = 420;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    SceneHost *h = sceneValue(SceneHost::createHost("orbit"));
    h->setTree(node("root",
                    {
                        node("hub")
                            .withSpace("2d")
                            .withPosition(320.f, 210.f, 0.f)
                            .withRotation(0.f, 0.f, 0.f),
                        node("arm",
                             {
                                 node("hand").withSpace("2d").withPosition(90.f, 0.f, 0.f),
                             })
                            .withSpace("2d")
                            .withPosition(320.f, 210.f, 0.f),
                        node("moon").withSpace("2d").withPosition(420.f, 210.f, 0.f),
                    })
                   .withSpace("2d"));

    auto *hub = Renderable2D::create();
    hub->sprite()->width = 48;
    hub->sprite()->height = 48;
    hub->sprite()->r = 0.95f;
    hub->sprite()->g = 0.75f;
    hub->sprite()->b = 0.25f;
    hub->sprite()->visible = true;

    auto *hand = Renderable2D::create();
    hand->sprite()->width = 28;
    hand->sprite()->height = 28;
    hand->sprite()->r = 0.35f;
    hand->sprite()->g = 0.85f;
    hand->sprite()->b = 1.f;
    hand->sprite()->visible = true;

    auto *moon = Renderable2D::create();
    moon->sprite()->width = 22;
    moon->sprite()->height = 22;
    moon->sprite()->r = 0.85f;
    moon->sprite()->g = 0.45f;
    moon->sprite()->b = 0.95f;
    moon->sprite()->visible = true;

    REQUIRE(h->linkRenderable2D("hub", hub));
    REQUIRE(h->linkRenderable2D("hand", hand));
    REQUIRE(h->linkRenderable2D("moon", moon));

    auto *cam = Camera2D::createCamera();
    cam->data()->r = 0.08f;
    cam->data()->g = 0.09f;
    cam->data()->b = 0.12f;

    float handTravel = 0.f;
    float prevHX = hand->transform()->x;
    for (int frame = 0; frame < 90; ++frame) {
        const float t = float(frame) * 0.06f;
        SceneNode  *arm   = sceneValue(h->findById("arm"));
        SceneNode  *moonN = sceneValue(h->findById("moon"));
        REQUIRE(arm != nullptr);
        REQUIRE(moonN != nullptr);
        arm->roll = t;
        arm->localDirty = true;
        moonN->x = 320.f + std::cos(t * 1.4f) * 120.f;
        moonN->y = 210.f + std::sin(t * 1.4f) * 70.f;
        moonN->localDirty = true;

        TransformSystem::updateHost(h);
        RenderSystem::render(*gfx);

        handTravel += std::fabs(hand->transform()->x - prevHX);
        prevHX = hand->transform()->x;

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) break;
        }
        SDL_Delay(16);
    }

    CHECK_GT(handTravel, 50.f);
    hub->sprite()->visible = false;
    hand->sprite()->visible = false;
    moon->sprite()->visible = false;
    win->close();
}

TEST_CASE("Scene.objectId.preservedAcrossFullRebuild") {
    SceneHost *h = sceneValue(SceneHost::createHost("objid"));
    h->setTree(node("root", {node("p")}));
    SceneNode *p = sceneValue(h->findById("p"));
    REQUIRE(p != nullptr);

    SceneObject *o = sceneValue(SceneObject::createObject("objid", "p"));
    p->objectId = uint32_t(o->id);

    // Full rebuild keeps the binding marker on same-id nodes, not on new ones.
    h->setTree(node("root", {node("p"), node("q")}));
    SceneNode *p2 = sceneValue(h->findById("p"));
    SceneNode *q  = sceneValue(h->findById("q"));
    REQUIRE(p2 != nullptr);
    REQUIRE(q != nullptr);
    CHECK_EQ(uint32_t(p2->objectId), uint32_t(o->id));
    CHECK_EQ(q->objectId, 0u);

    // Reconcile patch keeps the marker too.
    h->setTreeReconcile(node("root", {node("p").withPosition(1.f, 0.f, 0.f)}));
    SceneNode *p3 = sceneValue(h->findById("p"));
    REQUIRE(p3 != nullptr);
    CHECK_EQ(uint32_t(p3->objectId), uint32_t(o->id));

    o->release();
}

TEST_CASE("Scene.link.genericMultiLinkAndSync") {
    SceneHost *h = sceneValue(SceneHost::createHost("genlink"));
    h->setTree(node("root", {node("soldier")}));

    auto *r3 = eve::graphics::Renderable3D::create();
    eve::physics::World3D w3(0.f, -9.8f, 0.f, true);
    eve::physics::Body3D *b3 = w3.newBody("kinematic", 0.f, 0.f, 0.f);
    auto *cam = eve::graphics::Camera3D::createCamera();

    CHECK(h->linkRenderable3D("soldier", r3));
    CHECK(h->linkPhysics3D("soldier", b3, 0));
    CHECK(h->linkCamera3D("soldier", cam));
    CHECK_EQ(h->linkCount("soldier"), 3);

    // node → body + camera (syncMode 0)
    SceneNode *n = sceneValue(h->findById("soldier"));
    REQUIRE(n != nullptr);
    n->x = 3.f;
    n->y = 4.f;
    n->z = 5.f;
    n->localDirty = true;
    TransformSystem::updateHost(h);
    CHECK(approxEq(b3->getX(), 3.f));
    CHECK(approxEq(b3->getY(), 4.f));
    CHECK(approxEq(b3->getZ(), 5.f));
    CHECK(approxEq(cam->data()->eyeX, 3.f));
    CHECK(approxEq(cam->data()->eyeY, 4.f));
    CHECK(approxEq(cam->data()->eyeZ, 5.f));

    // body → node (syncMode 1); re-linking same kind replaces target/mode
    CHECK(h->linkPhysics3D("soldier", b3, 1));
    b3->setPosition(10.f, 20.f, 30.f);
    TransformSystem::updateHost(h);
    CHECK(approxEq(n->x, 10.f));
    CHECK(approxEq(n->y, 20.f));
    CHECK(approxEq(n->z, 30.f));

    // unlink by kind keeps the other links
    CHECK(h->unlink("soldier", findLinkKind("physics3d")));
    CHECK_EQ(h->linkCount("soldier"), 2);
    CHECK(!h->unlink("soldier", findLinkKind("physics3d")));

    // full rebuild preserves remaining links by id
    h->setTree(node("root", {node("soldier")}));
    SceneNode *n2 = sceneValue(h->findById("soldier"));
    REQUIRE(n2 != nullptr);
    REQUIRE_EQ(n2->links.size(), 2u);
    bool hasMesh = false, hasCam = false;
    for (const auto &l : n2->links) {
        if (l.target == r3) hasMesh = true;
        if (l.target == cam) hasCam = true;
    }
    CHECK(hasMesh);
    CHECK(hasCam);

    ecs::DestroyEntity(r3);
    ecs::DestroyEntity(cam);
    w3.destroy();
}

TEST_CASE("Scene.link.physics2D") {
    SceneHost *h = sceneValue(SceneHost::createHost("link2dphys"));
    h->setTree(node("root", {node("p").withSpace("2d").withPosition(10.f, 20.f, 0.f)}));

    eve::physics::World w(0.f, 0.f, true, 1.f);
    eve::physics::Body *b = w.newBody("kinematic", 0.f, 0.f);
    CHECK(h->linkPhysics2D("p", b, 0));
    TransformSystem::updateHost(h);
    CHECK(approxEq(b->getX(), 10.f));
    CHECK(approxEq(b->getY(), 20.f));

    // body → node: angle syncs into 2D roll
    CHECK(h->linkPhysics2D("p", b, 1));
    b->setPosition(30.f, 40.f);
    b->setAngle(0.5f);
    TransformSystem::updateHost(h);
    SceneNode *n = sceneValue(h->findById("p"));
    REQUIRE(n != nullptr);
    CHECK(approxEq(n->x, 30.f));
    CHECK(approxEq(n->y, 40.f));
    CHECK(approxEq(n->roll, 0.5f));

    CHECK(h->unlink("p", findLinkKind("physics2d")));
    CHECK_EQ(h->linkCount("p"), 0);
    w.destroy();
}

TEST_CASE("Scene.api.transformGettersAndSpaceConversion") {
    Scene *mod = Scene::create();
    mod->mountAs("api1", node("root", {node("child").withPosition(1.f, 0.f, 0.f)}).withPosition(10.f, 0.f, 0.f))
        .ignore("test setup");
    mod->updateTransformsAll();

    auto p = mod->getNodePositionAt("api1", "child");
    CHECK(approxEq(p[0], 1.f));
    CHECK(approxEq(p[1], 0.f));
    CHECK(approxEq(p[2], 0.f));
    auto wp = mod->getNodeWorldPositionAt("api1", "child");
    CHECK(approxEq(wp[0], 11.f));
    CHECK(approxEq(wp[1], 0.f));
    auto ws = mod->getNodeWorldScaleAt("api1", "child");
    CHECK(approxEq(ws[0], 1.f));
    CHECK(approxEq(ws[1], 1.f));
    CHECK(approxEq(ws[2], 1.f));

    auto lw = mod->localToWorldAt("api1", "child", 2.f, 3.f, 4.f);
    CHECK(approxEq(lw[0], 12.f));
    CHECK(approxEq(lw[1], 3.f));
    CHECK(approxEq(lw[2], 4.f));
    auto wl = mod->worldToLocalAt("api1", "child", 12.f, 3.f, 4.f);
    CHECK(approxEq(wl[0], 2.f));
    CHECK(approxEq(wl[1], 3.f));
    CHECK(approxEq(wl[2], 4.f));

    // selected-host setter/getter parity
    CHECK(mod->setNodeRotation("child", 0.5f, 0.25f, 0.1f));
    auto r = mod->getNodeRotationAt("api1", "child");
    CHECK(approxEq(r[0], 0.5f));
    CHECK(approxEq(r[1], 0.25f));
    CHECK(approxEq(r[2], 0.1f));
    CHECK(mod->setNodeScale("child", 2.f, 3.f, 4.f));
    auto s = mod->getNodeScaleAt("api1", "child");
    CHECK(approxEq(s[0], 2.f));
    CHECK(approxEq(s[2], 4.f));
    CHECK(mod->setNodeVisible("child", false));
    CHECK(!mod->getNodeVisibleAt("api1", "child"));

    auto wr = mod->getNodeWorldRotationAt("api1", "root");
    CHECK(approxEq(wr[0], 0.f));
    CHECK(approxEq(wr[1], 0.f));
    CHECK(approxEq(wr[2], 0.f));
}

TEST_CASE("Scene.api.hierarchyOps") {
    SceneHost *h = sceneValue(SceneHost::createHost("hier"));
    h->setTree(node("root", {node("a"), node("b")}));
    REQUIRE(h->setParentById("b", "a"));
    CHECK(h->getParentById("b")->id == "a");
    CHECK(!h->setParentById("a", "b"));  // cycle rejected
    CHECK(h->removeChildById("a", "b"));
    CHECK(h->getParentById("b") == nullptr);

    Scene *mod = Scene::create();
    mod->mountAs("hier2", node("root", {node("x"), node("y")})).ignore("test setup");
    mod->select("hier2");
    CHECK(mod->setNodeParentAt("hier2", "y", "x"));
    CHECK(mod->getParentId("y") == "x");
    CHECK(mod->removeNodeAt("hier2", "y"));   // detach; arena node stays
    CHECK(mod->getParentId("y").empty());
    CHECK(mod->hasNode("y"));
    CHECK(mod->addChildAt("hier2", "root", "y"));
    CHECK(mod->getParentId("y") == "root");
    CHECK(mod->removeChildAt("hier2", "root", "y"));
    CHECK(mod->getParentId("y").empty());
}

TEST_CASE("Scene.api.quaternionAndLookAt") {
    Scene *mod = Scene::create();
    mod->mountAs("quat2", node("root", {node("n")})).ignore("test setup");

    // quaternion round trip
    CHECK(mod->setNodeRotation("n", 0.5f, 0.25f, 0.1f));
    auto q = mod->getNodeQuaternionAt("quat2", "n");
    CHECK(mod->setNodeQuaternionAt("quat2", "n", q[0], q[1], q[2], q[3]));
    auto r = mod->getNodeRotationAt("quat2", "n");
    CHECK(approxEq(r[0], 0.5f));
    CHECK(approxEq(r[1], 0.25f));
    CHECK(approxEq(r[2], 0.1f));

    // lookAt: node +Z points at target
    CHECK(mod->setNodePosition("n", 0.f, 0.f, 0.f));
    CHECK(mod->setNodeLookAtAt("quat2", "n", 5.f, 0.f, 0.f));
    mod->updateTransformsAll();
    SceneNode *n2 = sceneValue(sceneValue(mod->findHost("quat2"))->findById("n"));
    REQUIRE(n2 != nullptr);
    glm::vec3 f(n2->world[2]);
    CHECK(approxEq(f.x, 1.f));
    CHECK(approxEq(f.y, 0.f));
    CHECK(approxEq(f.z, 0.f));

    CHECK(mod->setNodeLookAtAt("quat2", "n", 0.f, 0.f, 5.f));
    mod->updateTransformsAll();
    glm::vec3 f2(n2->world[2]);
    CHECK(approxEq(f2.x, 0.f));
    CHECK(approxEq(f2.y, 0.f));
    CHECK(approxEq(f2.z, 1.f));
}

TEST_CASE("Scene.api.tagsAndLayer") {
    SceneHost *h = sceneValue(SceneHost::createHost("tags"));
    h->setTree(node("root", {node("a").withTag("enemy").withLayer(2),
                             node("b").withTag("ally")}));
    SceneNode *a = sceneValue(h->findById("a"));
    REQUIRE(a != nullptr);
    CHECK(h->hasTag(a, "enemy"));
    CHECK_EQ(a->layer, 2);
    CHECK(h->addTag(a, "boss"));
    CHECK(!h->addTag(a, "boss"));
    CHECK(h->removeTag(a, "enemy"));
    CHECK(!h->hasTag(a, "enemy"));
    auto tagged = h->findAllByTag("boss");
    REQUIRE_EQ(tagged.size(), 1u);
    CHECK(tagged[0]->id == "a");

    Scene *mod = Scene::create();
    mod->mountAs("tags2", node("root", {node("a").withTag("enemy"), node("b")})).ignore("test setup");
    CHECK(mod->addNodeTagAt("tags2", "a", "boss"));
    CHECK(mod->hasNodeTagAt("tags2", "a", "enemy"));
    auto ids = mod->collectIdsByTagAt("tags2", "enemy");
    REQUIRE_EQ(ids.size(), 1u);
    CHECK(ids[0] == "a");
    CHECK(mod->removeNodeTagAt("tags2", "a", "boss"));
    CHECK_EQ(mod->getNodeTagsAt("tags2", "a").size(), 1u);
    CHECK(mod->setNodeLayerAt("tags2", "b", 7));
    CHECK_EQ(mod->getNodeLayerAt("tags2", "b"), 7);

    // reconcile patch propagates declarative tags/layer
    h->setTreeReconcile(node("root", {node("a").withTag("enemy"), node("b").withLayer(5)}));
    SceneNode *b = sceneValue(h->findById("b"));
    REQUIRE(b != nullptr);
    CHECK_EQ(b->layer, 5);
}

TEST_CASE("Scene.api.duplicateIdsRejected") {
    SceneHost *h = sceneValue(SceneHost::createHost("dupid"));
    REQUIRE_THROWS(([&]() -> bool {
        h->setTree(node("root", {node("dup"), node("dup")}));
        return true;
    }()));
}

TEST_CASE("Scene.link.purgeDeadTargets") {
    SceneHost *h = sceneValue(SceneHost::createHost("purge"));
    h->setTree(node("root", {node("m")}));
    auto *r = eve::graphics::Renderable3D::create();
    REQUIRE(h->linkRenderable3D("m", r));
    CHECK_EQ(h->linkCount("m"), 1);
    ecs::DestroyEntity(r);
    TransformSystem::updateHost(h);
    CHECK_EQ(h->linkCount("m"), 0);
}

TEST_CASE("Scene.transform.incrementalDirtySubtree") {
    Scene *mod = Scene::create();
    mod->mountAs("inc", node("root", {node("a", {node("a1")}), node("b")})).ignore("test setup");
    mod->updateTransformsAll();

    // API edit on "a" → incremental path; a1 follows, sibling b untouched.
    CHECK(mod->setNodePosition("a", 5.f, 0.f, 0.f));
    mod->updateTransformsAll();
    SceneHost *h  = sceneValue(mod->findHost("inc"));
    SceneNode *a1 = sceneValue(h->findById("a1"));
    SceneNode *b  = sceneValue(h->findById("b"));
    REQUIRE(a1 != nullptr);
    REQUIRE(b != nullptr);
    CHECK(approxEq(a1->world[3][0], 5.f));
    CHECK(approxEq(b->world[3][0], 0.f));

    // Direct legacy edit (localDirty only) still recomputes correctly.
    b->x = 9.f;
    b->localDirty = true;
    mod->updateTransformsAll();
    CHECK(approxEq(b->world[3][0], 9.f));
    CHECK(approxEq(a1->world[3][0], 5.f));
}

TEST_CASE("Scene.index.lazyRebuild") {
    SceneHost *h = sceneValue(SceneHost::createHost("idx"));
    h->setTree(node("root", {node("a"), node("b")}));
    CHECK(sceneValue(h->findById("b")) != nullptr);
    h->setTree(node("root", {node("b"), node("c")}));
    CHECK(sceneValue(h->findById("b")) != nullptr);
    CHECK(sceneValue(h->findById("a")) == nullptr);
    h->setTreeReconcile(node("root", {node("b"), node("c"), node("d")}));
    CHECK(sceneValue(h->findById("d")) != nullptr);
    CHECK(h->getParentById("d") != nullptr);
}

TEST_CASE("Scene.reconcile.movePreservesIdentity") {
    SceneHost *h = sceneValue(SceneHost::createHost("mv"));
    h->setTree(node("root", {node("a"), node("b"), node("c")}));
    SceneNode *b = sceneValue(h->findById("b"));
    REQUIRE(b != nullptr);
    auto *r = eve::graphics::Renderable3D::create();
    CHECK(h->linkRenderable3D("b", r));

    // Same key set, different order → reorder without full rebuild.
    bool rebuilt = h->setTreeReconcile(node("root", {node("c"), node("a"), node("b")}));
    CHECK(!rebuilt);
    SceneNode *b2 = sceneValue(h->findById("b"));
    REQUIRE(b2 != nullptr);
    CHECK(b2 == b);  // arena identity preserved
    CHECK(h->getChildAtById("root", 0)->id == "c");
    CHECK(h->getChildAtById("root", 1)->id == "a");
    CHECK(h->getChildAtById("root", 2)->id == "b");
    CHECK_EQ(h->linkCount("b"), 1);  // link survived the reorder

    // Nested reorder also keeps identity.
    h->setTree(node("root", {node("p", {node("p1"), node("p2")})}));
    SceneNode *p1 = sceneValue(h->findById("p1"));
    REQUIRE(p1 != nullptr);
    rebuilt = h->setTreeReconcile(node("root", {node("p", {node("p2"), node("p1")})}));
    CHECK(!rebuilt);
    CHECK(sceneValue(h->findById("p1")) == p1);
    CHECK(h->getChildAtById("p", 0)->id == "p2");

    ecs::DestroyEntity(r);
}

TEST_CASE("Scene.serialize.roundTrip") {
    Scene *mod = Scene::create();
    mod->mountAs("s1", node("root", {node("a")
                                         .withPosition(1.f, 2.f, 3.f)
                                         .withRotation(0.5f, 0.25f, 0.1f)
                                         .withTag("enemy")
                                         .withLayer(2)
                                         .withBounds(-1.f, -1.f, -1.f, 1.f, 1.f, 1.f),
                                     node("b", {node("b1")})})
                           .withPosition(10.f, 0.f, 0.f))
        .ignore("test setup");

    const std::string json = mod->serializeHostAt("s1");
    CHECK(json.find("\"root\"") != std::string::npos);
    CHECK(json.find("b1") != std::string::npos);

    REQUIRE(mod->deserializeHostAt("s2", json));
    auto p = mod->getNodePositionAt("s2", "a");
    CHECK(approxEq(p[0], 1.f));
    CHECK(approxEq(p[1], 2.f));
    CHECK(approxEq(p[2], 3.f));
    auto r = mod->getNodeRotationAt("s2", "b1");
    CHECK(approxEq(r[0], 0.5f));
    CHECK(mod->hasNodeTagAt("s2", "a", "enemy"));
    CHECK_EQ(mod->getNodeLayerAt("s2", "a"), 2);
    auto b = mod->getNodeBoundsAt("s2", "a");
    CHECK(approxEq(b[0], -1.f));
    CHECK(approxEq(b[5], 1.f));
    CHECK(mod->hasNode("b1"));

    CHECK(!mod->deserializeHostAt("s3", "{not json"));
}

TEST_CASE("Scene.pick.rayAndScreen") {
    Scene *mod = Scene::create();
    mod->mountAs("pk",
                 node("root", {node("target").withBounds(-1.f, -1.f, -1.f, 1.f, 1.f, 1.f),
                               node("back").withBounds(-1.f, -1.f, -1.f, 1.f, 1.f, 1.f).withPosition(0.f, 0.f, -10.f)}))
        .ignore("test setup");
    mod->updateTransformsAll();
    CHECK(mod->pickRayAt("pk", 0.f, 0.f, 5.f, 0.f, 0.f, -1.f) == "target");
    CHECK(mod->pickRayAt("pk", 10.f, 0.f, 5.f, 0.f, 0.f, -1.f).empty());

    auto *cam = eve::graphics::Camera3D::createCamera();
    cam->setEye(0.f, 0.f, 5.f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setFov(60.f);
    CHECK(mod->pickScreenAt("pk", cam, 320.f, 240.f, 640.f, 480.f) == "target");
    ecs::DestroyEntity(cam);
}

TEST_CASE("Scene.cull.frustum") {
    Scene *mod = Scene::create();
    mod->mountAs("cu",
                 node("root", {node("front").withBounds(-1.f, -1.f, -1.f, 1.f, 1.f, 1.f),
                               node("side").withBounds(-1.f, -1.f, -1.f, 1.f, 1.f, 1.f).withPosition(10.f, 0.f, 0.f)}))
        .ignore("test setup");
    auto *cam = eve::graphics::Camera3D::createCamera();
    cam->setEye(0.f, 0.f, 5.f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setFov(60.f);
    auto ids = mod->collectFrustumIdsAt("cu", cam, 640.f, 480.f);
    bool hasFront = false, hasSide = false;
    for (const auto &id : ids) {
        if (id == "front") hasFront = true;
        if (id == "side") hasSide = true;
    }
    CHECK(hasFront);
    CHECK(!hasSide);
    ecs::DestroyEntity(cam);
}

TEST_CASE("Scene.spatial.syncOctree") {
    Scene *mod = Scene::create();
    mod->mountAs("sp",
                 node("root", {node("a").withBounds(-2.f, -2.f, -2.f, 2.f, 2.f, 2.f).withPosition(5.f, 0.f, 0.f)}))
        .ignore("test setup");
    mod->updateTransformsAll();
    eve::spatial::Octree ot(-100.f, -100.f, -100.f, 100.f, 100.f, 100.f);
    REQUIRE(mod->syncSpatialIndexAt("sp", &ot));
    CHECK_GE(ot.getCount(), 1);
    ot.queryPoint(5.f, 0.f, 0.f);
    CHECK_GE(ot.getResultCount(), 1);
    const int idx = ot.getResultId(0);
    CHECK(mod->nodeIdFromSpatialIdAt("sp", idx) == "a");
}

// ---------------------------------------------------------------------------
// Script side: eve.SceneEntity attach / detach / update / view integration
// ---------------------------------------------------------------------------

static const char *kSceneEcsScriptContent = R"SQ(
function testSceneEntityLifecycle() {
    class MoveComp extends eve.Component { speed = 2.0 }
    class EnemyAI extends eve.SceneEntity {
        move = MoveComp
        ticks = 0
        detached = false
        function onAttach() { node().setPosition(1.0, 2.0, 3.0) }
        function update(dt) {
            ticks += 1
            local p = node().getPosition()
            node().setPosition(p[0] + move.speed * dt, p[1], p[2])
        }
        function onDetach() { detached = true }
    }

    local scene = eve.Scene()
    scene.beginBuild()
    scene.beginNode("root")
    scene.addNode("enemy")
    scene.end()
    scene.mountBuildAs("battle")

    local e = scene.attachEntity("enemy", EnemyAI)
    if (e == null) return false
    if (!e.isAlive()) return false
    if (eve.view(EnemyAI).len() != 1) return false
    if (!scene.hasEntity("enemy", EnemyAI)) return false
    if (scene.getEntity("enemy", EnemyAI) != e) return false
    if (scene.entitiesOf("enemy").len() != 1) return false

    local ref = scene.getNodeRef("enemy")
    if (ref == null || !ref.isValid()) return false
    if (ref.getParentId() != "root") return false
    if (ref.getPath() != "root/enemy") return false
    if (!ref.hasEntity(EnemyAI)) return false

    local p = ref.getPosition()
    if (p.len() != 3 || p[0] != 1.0 || p[1] != 2.0 || p[2] != 3.0) return false

    // scene.update(dt) drives updateTransforms + script update(dt)
    scene.update(0.5)
    if (e.ticks != 1) return false
    local p2 = ref.getPosition()
    if (p2[0] < 1.9 || p2[0] > 2.1) return false
    local wp = ref.getWorldPosition()
    if (wp[0] < 1.9 || wp[0] > 2.1) return false

    // reconcile keeps the binding alive (same node id)
    scene.beginBuild()
    scene.beginNode("root")
    scene.addNode("enemy")
    scene.end()
    scene.remountBuildAs("battle")
    if (!scene.hasEntity("enemy", EnemyAI)) return false
    if (eve.view(EnemyAI).len() != 1) return false
    if (!e.isAlive()) return false

    // full rebuild removing the node tears the binding down
    scene.beginBuild()
    scene.beginNode("root")
    scene.end()
    scene.mountBuildAs("battle")
    if (scene.hasEntity("enemy", EnemyAI)) return false
    if (eve.view(EnemyAI).len() != 0) return false
    if (!e.detached) return false
    if (e.isAlive()) return false
    return true
}

function testSceneEntityViewCacheAfterDetach() {
    class Marker extends eve.Component { v = 1 }
    class Unit extends eve.SceneEntity { mark = Marker }

    local scene = eve.Scene()
    scene.beginBuild()
    scene.beginNode("root")
    scene.addNode("a")
    scene.addNode("b")
    scene.end()
    scene.mountBuildAs("units")

    local a = scene.attachEntity("a", Unit)
    local b = scene.attachEntity("b", Unit)
    if (eve.view(Unit).len() != 2) return false

    scene.detachEntity("a", a)
    if (eve.view(Unit).len() != 1) return false
    local remaining = eve.view(Unit)
    if (remaining.len() != 1 || remaining[0] != b) return false

    scene.detachEntity("b", b)
    if (eve.view(Unit).len() != 0) return false
    return true
}

function testSceneLinkPhysics2D() {
    local scene = eve.Scene()
    scene.beginBuild()
    scene.beginNode("root")
    scene.addNode("ball")
    scene.setBuildPosition(50.0, 60.0, 0.0)
    scene.end()
    scene.mountBuildAs("links")

    local physics = eve.Physics()
    local world = physics.newWorld(0.0, 0.0, false)
    local body = world.newBody("kinematic", 0.0, 0.0)

    if (!scene.linkPhysics2D("ball", body)) return false
    if (scene.linkCount("ball") != 1) return false
    scene.updateTransforms()
    if (body.getX() != 50.0 || body.getY() != 60.0) return false

    // NodeRef forwarding + body-authoritative mode
    local ref = scene.getNodeRef("ball")
    if (ref == null) return false
    body.setPosition(99.0, 88.0)
    if (!ref.linkPhysics2D(body, "body")) return false
    scene.updateTransforms()
    local p = ref.getPosition()
    if (p[0] != 99.0 || p[1] != 88.0) return false

    // unlink by kind via NodeRef
    if (!ref.unlinkNodeKind("physics2d")) return false
    if (ref.linkCount() != 0) return false
    world.destroy()
    return true
}

function testSceneApiCompleteness() {
    local scene = eve.Scene()
    scene.beginBuild()
    scene.beginNode("root")
    scene.addNode("pivot")
    scene.setBuildPosition(10.0, 0.0, 0.0)
    scene.addNode("ball")
    scene.setBuildPosition(1.0, 2.0, 3.0)
    scene.end()
    scene.mountBuildAs("api")

    // getters
    local p = scene.getNodePosition("ball")
    if (p.len() != 3 || p[0] != 1.0 || p[1] != 2.0 || p[2] != 3.0) return false
    scene.setNodeScale("ball", 2.0, 3.0, 4.0)
    local s = scene.getNodeScale("ball")
    if (s[0] != 2.0 || s[2] != 4.0) return false
    scene.setNodeVisible("ball", false)
    if (scene.getNodeVisible("ball")) return false

    // world + space conversion
    scene.updateTransforms()
    local wp = scene.getNodeWorldPosition("ball")
    if (wp[0] != 11.0 || wp[1] != 2.0 || wp[2] != 3.0) return false
    local lw = scene.localToWorld("ball", 0.0, 0.0, 0.0)
    if (lw[0] != 11.0) return false
    local wl = scene.worldToLocal("ball", 11.0, 2.0, 3.0)
    if (wl[0] != 0.0 || wl[1] != 0.0) return false

    // hierarchy ops
    if (!scene.setNodeParent("ball", "pivot")) return false
    if (scene.getParentId("ball") != "pivot") return false
    if (!scene.removeNode("ball")) return false
    if (scene.getParentId("ball") != "") return false
    if (!scene.addNodeChild("root", "ball")) return false

    // quaternion round trip
    scene.setNodeRotation("ball", 0.5, 0.25, 0.1)
    local q = scene.getNodeQuaternion("ball")
    if (!scene.setNodeQuaternion("ball", q[0], q[1], q[2], q[3])) return false
    local r = scene.getNodeRotation("ball")
    local d0 = r[0] - 0.5
    local d1 = r[1] - 0.25
    local d2 = r[2] - 0.1
    if (d0 * d0 > 1e-8 || d1 * d1 > 1e-8 || d2 * d2 > 1e-8) return false

    // lookAt: +Z toward target
    scene.setNodePosition("ball", 0.0, 0.0, 0.0)
    if (!scene.setNodeLookAt("ball", 0.0, 0.0, 5.0)) return false
    scene.updateTransforms()
    local f = scene.getNodeRef("ball").getForward()
    if (f[0] * f[0] > 1e-8 || f[1] * f[1] > 1e-8 || (f[2] - 1.0) * (f[2] - 1.0) > 1e-8) return false

    // tags / layer
    if (!scene.addNodeTag("ball", "unit")) return false
    if (!scene.hasNodeTag("ball", "unit")) return false
    if (scene.collectIdsByTag("unit").len() != 1) return false
    if (!scene.removeNodeTag("ball", "unit")) return false
    scene.setNodeLayer("ball", 3)
    if (scene.getNodeLayer("ball") != 3) return false
    return true
}

function testSceneNodeEvents() {
    local scene = eve.Scene()
    scene.beginBuild()
    scene.beginNode("root")
    scene.addNode("a")
    scene.end()
    scene.mountBuildAs("evt")

    // handler registers against the selected host
    local events = []
    scene.setNodeEventHandler(function(action, nodeId, parentId) {
        events.push(action + ":" + nodeId)
    })

    // reconcile patch fires node_changed per affected node
    events.clear()
    scene.beginBuild()
    scene.beginNode("root")
    scene.addNode("a")
    scene.setBuildPosition(3.0, 0.0, 0.0)
    scene.end()
    scene.remountBuildAs("evt")
    if (events.len() != 2) return false
    if (events[0] != "node_changed:root") return false

    // full rebuild removing a node fires node_removed
    events.clear()
    scene.beginBuild()
    scene.beginNode("root")
    scene.end()
    scene.mountBuildAs("evt")
    if (events.len() < 1 || events[0] != "node_removed:a") return false

    // reparent fires node_moved
    scene.beginBuild()
    scene.beginNode("root")
    scene.addNode("x")
    scene.addNode("y")
    scene.end()
    scene.mountBuildAs("evt")
    events.clear()
    scene.setNodeParent("y", "x")
    if (events.len() < 1 || events[0] != "node_moved:y") return false
    return true
}

function testSceneComponentOnMount() {
    local mounted = 0
    scene <- eve.Scene()
    class MyComp extends eve.SceneComponent {
        function onMount() { mounted += 1 }
        function build() {
            scene.beginBuild()
            scene.beginNode("root")
            scene.end()
        }
    }
    local c = MyComp()
    c.mountAs("mc")
    if (mounted != 1) return false
    c.markDirty()
    c.updateIfDirty()
    if (mounted != 1) return false  // only the first mount
    return true
}

function testSceneErrorSemantics() {
    local scene = eve.Scene()
    scene.beginBuild()
    scene.beginNode("root")
    scene.addNode("a")
    scene.end()
    scene.mountBuildAs("err")
    local threw = false
    try {
        scene.getNodePosition("nope")
    } catch (e) {
        threw = true
    }
    if (!threw) return false
    return true
}

function testSceneSerializeAndPick() {
    local scene = eve.Scene()
    scene.beginBuild()
    scene.beginNode("root")
    scene.addNode("target")
    scene.end()
    scene.mountBuildAs("spk")
    scene.setNodeBounds("target", -1.0, -1.0, -1.0, 1.0, 1.0, 1.0)
    scene.updateTransforms()

    local json = scene.serializeHost()
    if (json.len() == 0) return false
    if (!scene.deserializeHostAt("spk2", json)) return false
    local p = scene.getNodePositionAt("spk2", "target")
    if (p[0] != 0.0 || p[1] != 0.0) return false
    if (scene.pickRay(0.0, 0.0, 5.0, 0.0, 0.0, -1.0) != "target") return false
    return true
}
)SQ";

class SceneEcsBridgeFixture {
public:
    SceneEcsBridgeFixture() : vm(2048, ssq::Libs::ALL) {
        eve::ModuleManager::expose(vm);
        ssq::Script s = vm.compileSource(kSceneEcsScriptContent);
        vm.run(s);
    }

    ssq::VM vm;
};

TEST_CASE_FIXTURE(SceneEcsBridgeFixture, "Scene.script.entityLifecycle") {
    CHECK(vm.callFunc(vm.findFunc("testSceneEntityLifecycle"), vm).toBool());
}

TEST_CASE_FIXTURE(SceneEcsBridgeFixture, "Scene.script.entityViewCacheAfterDetach") {
    CHECK(vm.callFunc(vm.findFunc("testSceneEntityViewCacheAfterDetach"), vm).toBool());
}

TEST_CASE_FIXTURE(SceneEcsBridgeFixture, "Scene.script.linkPhysics2D") {
    CHECK(vm.callFunc(vm.findFunc("testSceneLinkPhysics2D"), vm).toBool());
}

TEST_CASE_FIXTURE(SceneEcsBridgeFixture, "Scene.script.apiCompleteness") {
    CHECK(vm.callFunc(vm.findFunc("testSceneApiCompleteness"), vm).toBool());
}

TEST_CASE_FIXTURE(SceneEcsBridgeFixture, "Scene.script.nodeEvents") {
    CHECK(vm.callFunc(vm.findFunc("testSceneNodeEvents"), vm).toBool());
}

TEST_CASE_FIXTURE(SceneEcsBridgeFixture, "Scene.script.componentOnMount") {
    CHECK(vm.callFunc(vm.findFunc("testSceneComponentOnMount"), vm).toBool());
}

TEST_CASE_FIXTURE(SceneEcsBridgeFixture, "Scene.script.errorSemantics") {
    CHECK(vm.callFunc(vm.findFunc("testSceneErrorSemantics"), vm).toBool());
}

TEST_CASE_FIXTURE(SceneEcsBridgeFixture, "Scene.script.serializeAndPick") {
    CHECK(vm.callFunc(vm.findFunc("testSceneSerializeAndPick"), vm).toBool());
}

TEST_CASE("Scene.nodeRef.transformsAndStructure") {
    Scene *mod = Scene::create();
    mod->mountAs("refs", node("root", {node("child").withPosition(1.f, 2.f, 3.f)})).ignore("test setup");

    SceneNodeRef ref("refs", "child");
    CHECK(ref.isValid());
    CHECK_EQ(ref.getHostName(), std::string("refs"));
    CHECK_EQ(ref.getNodeId(), std::string("child"));
    CHECK_EQ(ref.getParentId(), std::string("root"));
    CHECK_EQ(ref.getChildCount(), 0);
    CHECK(ref.getChildIdAt(0).empty());
    CHECK_EQ(ref.getPath(), std::string("root/child"));

    CHECK(ref.setPosition(4.f, 5.f, 6.f));
    CHECK(approxEq(ref.getPositionX(), 4.f));
    CHECK(approxEq(ref.getPositionY(), 5.f));
    CHECK(approxEq(ref.getPositionZ(), 6.f));
    const auto pos = ref.getPosition();
    REQUIRE_EQ(pos.size(), 3u);
    CHECK(approxEq(pos[0], 4.f));
    CHECK(approxEq(pos[2], 6.f));

    CHECK(ref.setRotation(10.f, 20.f, 30.f));
    CHECK(approxEq(ref.getRotationYaw(), 10.f));
    CHECK(approxEq(ref.getRotationPitch(), 20.f));
    CHECK(approxEq(ref.getRotationRoll(), 30.f));
    const auto rot = ref.getRotation();
    REQUIRE_EQ(rot.size(), 3u);
    CHECK(approxEq(rot[0], 10.f));
    CHECK(ref.setRotation(0.f, 0.f, 0.f));  // reset so world axes are identity

    CHECK(ref.setScale(2.f, 3.f, 4.f));
    CHECK(approxEq(ref.getScaleX(), 2.f));
    CHECK(approxEq(ref.getScaleY(), 3.f));
    CHECK(approxEq(ref.getScaleZ(), 4.f));
    const auto sc = ref.getScale();
    REQUIRE_EQ(sc.size(), 3u);
    CHECK(approxEq(sc[1], 3.f));

    CHECK(ref.setVisible(false));
    CHECK(!ref.isVisible());
    CHECK(ref.setVisible(true));
    CHECK(ref.isVisible());

    mod->updateTransformsAll();
    const auto wp = ref.getWorldPosition();
    REQUIRE_EQ(wp.size(), 3u);
    CHECK(approxEq(wp[0], 4.f));  // parent root at origin -> world == local
    CHECK(approxEq(wp[1], 5.f));
    CHECK(approxEq(wp[2], 6.f));
    CHECK(approxEq(ref.getWorldPositionX(), 4.f));
    CHECK(approxEq(ref.getWorldPositionY(), 5.f));
    CHECK(approxEq(ref.getWorldPositionZ(), 6.f));
    const auto wm = ref.getWorldMatrix();
    CHECK_EQ(wm.size(), 16u);
    const auto fwd = ref.getForward();
    CHECK(approxEq(fwd[2], 1.f, 1e-3f));  // identity orientation
    const auto right = ref.getRight();
    CHECK(approxEq(right[0], 1.f, 1e-3f));
    const auto up = ref.getUp();
    CHECK(approxEq(up[1], 1.f, 1e-3f));
}

TEST_CASE("Scene.nodeRef.invalidRefs") {
    Scene *mod = Scene::create();
    mod->mountAs("refs2", node("root", {node("child")})).ignore("test setup");

    SceneNodeRef missing("refs2", "nope");
    CHECK(!missing.isValid());
    CHECK(!missing.setPosition(1.f, 2.f, 3.f));
    CHECK(!missing.setRotation(1.f, 2.f, 3.f));
    CHECK(!missing.setScale(1.f, 2.f, 3.f));
    CHECK(!missing.setVisible(false));
    CHECK_EQ(missing.getPositionX(), 0.f);
    CHECK_EQ(missing.getPositionY(), 0.f);
    CHECK_EQ(missing.getPositionZ(), 0.f);
    CHECK_EQ(missing.getPosition().size(), 3u);
    CHECK_EQ(missing.getWorldMatrix().size(), 16u);
    CHECK_EQ(missing.getForward()[2], 1.f);
    CHECK_EQ(missing.getRight()[0], 1.f);
    CHECK_EQ(missing.getUp()[1], 1.f);
    CHECK(missing.getParentId().empty());
    CHECK_EQ(missing.getChildCount(), 0);
    CHECK(missing.getChildIdAt(0).empty());
    CHECK(missing.getPath().empty());

    SceneNodeRef badHost("missing-host", "child");
    CHECK(!badHost.isValid());
    CHECK(!badHost.setPosition(1.f, 2.f, 3.f));

    SceneNodeRef emptyRef;
    CHECK(emptyRef.getHostName().empty());
    CHECK(emptyRef.getNodeId().empty());
    CHECK(!emptyRef.isValid());
}

TEST_CASE("Scene.api.hostManagement") {
    Scene *mod = Scene::create();
    CHECK(!mod->select("nope"));
    auto missing = mod->findHost("nope");
    CHECK(!missing.ok());
    auto invalidOwner = mod->findHostByOwner(0);
    CHECK(!invalidOwner.ok());  // owner id 0 never resolves

    SceneHost *h = sceneValue(mod->mountAs("mgmt", node("root")));
    REQUIRE(h != nullptr);
    CHECK(mod->select("mgmt"));
    CHECK_EQ(mod->current(), h);

    mod->bindOwner(99);
    auto owned = mod->findHostByOwner(99);
    REQUIRE(owned.ok());
    CHECK_EQ(owned.value(), h);
    CHECK_EQ(h->getOwnerId(), 99u);
    mod->bindOwner(0);
    auto invalidOwnerAfterBind = mod->findHostByOwner(0);
    CHECK(!invalidOwnerAfterBind.ok());

    mod->setHostVisible(false);
    CHECK(!h->meta()->visible);
    mod->setHostVisible(true);
    CHECK(h->meta()->visible);
    mod->setHostLayer(5);
    CHECK_EQ(h->meta()->layer, 5);

    // Transform propagation over the whole tree must not crash with/without a
    // selected host.
    mod->updateTransforms();
    mod->updateTransformsAll();
    mod->select("nope");
    mod->updateTransforms();
    mod->updateTransformsAll();
}
