#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "scene/NodeDesc.h"
#include "scene/Scene.h"
#include "scene/SceneComponent.h"
#include "scene/SceneHost.h"
#include "scene/TransformSystem.h"

#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"

#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

using namespace eve::scene;

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
    SceneHost *h = SceneHost::createHost("nest");
    h->setTree(node("root",
                    {
                        node("a").withPosition(1.f, 0.f, 0.f),
                        group({node("b").withPosition(0.f, 2.f, 0.f)}, "g"),
                    }));

    auto t = h->tree();
    REQUIRE_EQ(t->root, 0);
    REQUIRE_GE(t->nodes.size(), 4u);
    CHECK(h->findById("a") != nullptr);
    CHECK(h->findById("b") != nullptr);
    CHECK_EQ(h->findById("root")->firstChild, 1);
}

TEST_CASE("Scene.reconcile.patchesPropsKeepsIdentity") {
    SceneHost *h = SceneHost::createHost("recon");
    h->setTree(node("root", {node("player").withPosition(0.f, 0.f, 0.f)}));
    SceneNode *before = h->findById("player");
    REQUIRE(before != nullptr);
    const std::string *idPtr = &before->id;

    bool rebuilt = h->setTreeReconcile(node("root", {node("player").withPosition(3.f, 4.f, 5.f)}));
    CHECK(!rebuilt);
    SceneNode *after = h->findById("player");
    REQUIRE(after != nullptr);
    CHECK_EQ(&after->id, idPtr);
    CHECK(approxEq(after->x, 3.f));
    CHECK(approxEq(after->y, 4.f));
    CHECK(approxEq(after->z, 5.f));
}

TEST_CASE("Scene.reconcile.structureChangeRebuilds") {
    SceneHost *h = SceneHost::createHost("recon2");
    h->setTree(node("root", {node("a")}));
    bool rebuilt = h->setTreeReconcile(node("root", {node("a"), node("b")}));
    CHECK(rebuilt);
    CHECK(h->findById("b") != nullptr);
}

TEST_CASE("Scene.TransformSystem.parentChildWorld") {
    SceneHost *h = SceneHost::createHost("xf");
    h->setTree(node("root", {node("child").withPosition(1.f, 0.f, 0.f)}).withPosition(10.f, 0.f, 0.f));
    TransformSystem::updateHost(h);

    SceneNode *root = h->findById("root");
    SceneNode *child = h->findById("child");
    REQUIRE(root != nullptr);
    REQUIRE(child != nullptr);

    glm::mat4 expectRoot = glm::translate(glm::mat4(1.f), glm::vec3(10.f, 0.f, 0.f));
    glm::mat4 expectChild = expectRoot * glm::translate(glm::mat4(1.f), glm::vec3(1.f, 0.f, 0.f));
    CHECK(matApproxEq(root->world, expectRoot));
    CHECK(matApproxEq(child->world, expectChild));
}

TEST_CASE("Scene.hierarchy.cycleRejected") {
    SceneHost *h = SceneHost::createHost("cycle");
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

    SceneHost *h = Scene::create()->findHost("battle_host");
    REQUIRE(h != nullptr);
    CHECK(h->findById("ally") != nullptr);
    CHECK(h->findById("hud") != nullptr);

    comp.showAlly = false;
    comp.markDirty();
    CHECK(comp.updateIfDirty());
    CHECK(h->findById("ally") == nullptr);
    CHECK(h->findById("hud") != nullptr);
}

TEST_CASE("Scene.module.mountAndSetNode") {
    Scene *mod = Scene::create();
    mod->mountAs("main", node("root", {node("p").withPosition(0.f, 0.f, 0.f)}));
    CHECK(mod->select("main"));
    CHECK(mod->setNodePosition("p", 2.f, 3.f, 4.f));
    TransformSystem::updateHost(mod->current());
    SceneNode *p = mod->current()->findById("p");
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
    SceneHost *h = mod->findHost("built");
    REQUIRE(h != nullptr);
    CHECK(h->findById("root") != nullptr);
    CHECK(h->findById("child") != nullptr);
    CHECK(approxEq(h->findById("root")->x, 1.f));
}

TEST_CASE("Scene.link.syncRenderable3DWorld") {
    SceneHost *h = SceneHost::createHost("link3d");
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
    SceneNode *mesh = h->findById("mesh");
    REQUIRE(mesh != nullptr);
    CHECK(mesh->linkTarget == r);
    CHECK(approxEq(r->transform()->x, 5.f));
}

TEST_CASE("Scene.link.syncRenderable2DWorld") {
    SceneHost *h = SceneHost::createHost("link2d");
    h->setTree(node("root", {node("spr").withSpace("2d").withPosition(2.f, 3.f, 0.f)})
                   .withSpace("2d")
                   .withPosition(4.f, 5.f, 0.f));

    auto *r = eve::graphics::Renderable2D::create();
    CHECK(h->linkRenderable2D("spr", r));
    TransformSystem::updateHost(h);
    CHECK(approxEq(r->transform()->x, 6.f));
    CHECK(approxEq(r->transform()->y, 8.f));
}
