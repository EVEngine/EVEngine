#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "scene/NodeDesc.h"
#include "scene/Scene.h"
#include "scene/SceneCameraProjection.h"
#include "scene/SceneComponent.h"
#include "scene/SceneHost.h"
#include "scene/SceneNodeRef.h"
#include "scene/SceneObject.h"
#include "scene/TransformSystem.h"

#include "common/Capability.h"

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
#include "common/EntitySpatialResolver.h"
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

[[maybe_unused]] bool approxEq(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }

[[maybe_unused]] bool matApproxEq(const glm::mat4 &a, const glm::mat4 &b, float eps = 1e-4f) {
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            if (!approxEq(a[c][r], b[c][r], eps)) return false;
        }
    }
    return true;
}

template <class T>
[[maybe_unused]] T *sceneValue(eve::Result<T *> result) {
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


// The camera projection the picking entry points need is an optional
// capability: without a provider they must fail explicitly instead of silently
// returning a result, and with it restored they must work again.


// ---------------------------------------------------------------------------
// Script side: eve.SceneEntity attach / detach / update / view integration
// ---------------------------------------------------------------------------

static const char *kSceneEcsScriptContent = R"SQ(
[[maybe_unused]] function testSceneEntityLifecycle() {
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

[[maybe_unused]] function testSceneEntityViewCacheAfterDetach() {
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

[[maybe_unused]] function testSceneEntityEnableAndDelayedDetach() {
    class ToggleAI extends eve.SceneEntity {
        ticks = 0
        enables = 0
        disables = 0
        detached = false
        function onEnable() { enables += 1 }
        function onDisable() { disables += 1 }
        function update(dt) {
            ticks += 1
            if (ticks == 1) scene().scheduleDetachEntity(nodeId, this)
        }
        function onDetach() { detached = true }
    }

    local scene = eve.Scene()
    scene.beginBuild()
    scene.beginNode("root")
    scene.addNode("npc")
    scene.addNode("prop")
    scene.end()
    scene.mountBuildAs("life")

    local e = scene.attachEntity("npc", ToggleAI)
    if (e == null) return false
    local other = scene.attachEntity("prop", ToggleAI)
    if (other == null) return false

    e.disable()
    if (e.disables != 1 || e.isEnabled()) return false
    scene.update(0.1)
    if (e.ticks != 0) return false  // disabled skips update
    if (other.ticks != 1) return false
    if (!other.detached) return false  // delayed detach flushed at end of update
    if (eve.view(ToggleAI).len() != 1) return false

    e.enable()
    if (e.enables != 1 || !e.isEnabled()) return false
    scene.update(0.1)
    if (e.ticks != 1) return false
    if (!e.detached) return false
    if (eve.view(ToggleAI).len() != 0) return false

    local ids = scene.collectIdsWith(ToggleAI)
    if (ids.len() != 0) return false
    return true
}

[[maybe_unused]] function testSceneLinkPhysics2D() {
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

[[maybe_unused]] function testSceneApiCompleteness() {
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

[[maybe_unused]] function testSceneNodeEvents() {
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

[[maybe_unused]] function testSceneComponentOnMount() {
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

[[maybe_unused]] function testSceneErrorSemantics() {
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

[[maybe_unused]] function testSceneSerializeAndPick() {
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

TEST_CASE_FIXTURE(SceneEcsBridgeFixture, "Scene.script.entityEnableAndDelayedDetach") {
    CHECK(vm.callFunc(vm.findFunc("testSceneEntityEnableAndDelayedDetach"), vm).toBool());
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
