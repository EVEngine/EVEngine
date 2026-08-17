#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "sceneloader/SceneLoader.h"
#include "scene/NodeDesc.h"
#include "scene/SceneHost.h"
#include "scene/TransformSystem.h"
#include "graphics/Graphics.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "filesystem/Filesystem.h"
#include "model3d/Model3D.h"
#include "window/Window.h"

#include <assimp/mesh.h>
#include <assimp/scene.h>
#include <assimp/vector3.h>

#include <SDL2/SDL.h>

#include <cmath>
#include <string>
#include <vector>

using namespace eve::scene;
using namespace eve::sceneloader;

namespace {
#include "PathBesideSource.h"
EVE_DEFINE_PATH_BESIDE_SOURCE()

bool approx(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }

void openGfx(eve::window::Window *&win, eve::graphics::Graphics *&gfx, int w = 320, int h = 240) {
    win = eve::window::Window::create();
    gfx = eve::graphics::Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = w;
    s.height = h;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
}
}  // namespace

// ---- pure diff / apply logic (no graphics required) ----

TEST_CASE("SceneLoader.diff.addRemoveModifyMove") {
    SceneHost *h = SceneHost::createHost("sl");
    h->setTree(node("root", {node("a"), node("b").withPosition(1.f, 0.f, 0.f), node("c", {node("c1")})}));

    // b modified, a removed, d added, c stays.
    NodeDesc newRoot = node("root",
                            {
                                node("b").withPosition(9.f, 0.f, 0.f),
                                node("d"),
                                node("c", {node("c1")}),
                            });

    SceneDiff d = SceneLoader::diffTree(h, newRoot);
    CHECK_EQ(d.removed, 1);    // a
    CHECK_EQ(d.added, 1);      // d
    CHECK_EQ(d.modified, 1);   // b
    CHECK_EQ(d.moved, 0);

    REQUIRE(SceneLoader::applyTreeDiff(h, newRoot, d, nullptr, nullptr));
    CHECK(h->findById("a") == nullptr);
    CHECK(h->findById("d") != nullptr);
    CHECK(h->findById("b") != nullptr);
    CHECK(approx(h->findById("b")->x, 9.f));
    CHECK(h->findById("c") != nullptr);
    CHECK(h->findById("c1") != nullptr);
    // Root keeps its identity.
    CHECK(h->getRoot()->id == "root");
}

TEST_CASE("SceneLoader.diff.movesReparent") {
    SceneHost *h = SceneHost::createHost("slmove");
    h->setTree(node("root", {node("a"), node("b")}));

    // Move b under a.
    NodeDesc newRoot = node("root", {node("a", {node("b")})});
    SceneDiff d = SceneLoader::diffTree(h, newRoot);
    CHECK_EQ(d.moved, 1);
    CHECK_EQ(d.removed, 0);
    CHECK_EQ(d.added, 0);

    REQUIRE(SceneLoader::applyTreeDiff(h, newRoot, d, nullptr, nullptr));
    CHECK(h->getParentById("b")->id == "a");
    CHECK_EQ(h->getChildCountById("root"), 1);
}

TEST_CASE("SceneLoader.diff.unchangedIsNoop") {
    SceneHost *h = SceneHost::createHost("slnop");
    h->setTree(node("root", {node("m1").withPosition(1.f, 2.f, 3.f), node("m2")}));

    NodeDesc same = node("root", {node("m1").withPosition(1.f, 2.f, 3.f), node("m2")});
    SceneDiff d = SceneLoader::diffTree(h, same);
    CHECK(d.empty());
    CHECK_EQ(d.added, 0);
    CHECK_EQ(d.removed, 0);
    CHECK_EQ(d.modified, 0);
}

TEST_CASE("SceneLoader.apply.preservesUnchangedRenderableIdentity") {
    SceneHost *h = SceneHost::createHost("slid");
    h->setTree(node("root", {node("keep"), node("gone")}));

    // Manually link a Renderable3D to "keep" (pure ECS entity; no mesh upload here).
    auto *r = eve::graphics::Renderable3D::create();
    REQUIRE(r != nullptr);
    REQUIRE(h->linkRenderable3D("keep", r));

    // New tree: "gone" removed, "keep" stays (unchanged), "keep2" added.
    NodeDesc newRoot = node("root", {node("keep"), node("keep2")});
    SceneDiff d = SceneLoader::diffTree(h, newRoot);
    CHECK_EQ(d.removed, 1);
    CHECK_EQ(d.added, 1);

    REQUIRE(SceneLoader::applyTreeDiff(h, newRoot, d, nullptr, nullptr));
    SceneNode *after = h->findById("keep");
    REQUIRE(after != nullptr);
    // The unchanged GameObject keeps its linked Renderable3D — no rebuild / re-upload.
    REQUIRE_EQ(after->links.size(), 1u);
    CHECK(after->links[0].target == r);
    CHECK(int(after->links[0].kind) == int(eve::scene::LinkKind::Renderable3D));
    CHECK(h->findById("keep2") != nullptr);
    CHECK(h->findById("gone") == nullptr);

    ecs::DestroyEntity(r);
}

// ---- integration: decode a real 3D scene into ECS GameObjects + hot reload ----

static const char kCubeObj[] =
    "v -0.5 -0.5 -0.5\n"
    "v  0.5 -0.5 -0.5\n"
    "v  0.5  0.5 -0.5\n"
    "v -0.5  0.5 -0.5\n"
    "v -0.5 -0.5  0.5\n"
    "v  0.5 -0.5  0.5\n"
    "v  0.5  0.5  0.5\n"
    "v -0.5  0.5  0.5\n"
    "f 1 2 3\n"
    "f 1 3 4\n"
    "f 5 6 7\n"
    "f 5 7 8\n"
    "f 1 2 6\n"
    "f 1 6 5\n"
    "f 2 3 7\n"
    "f 2 7 6\n"
    "f 3 4 8\n"
    "f 3 8 7\n"
    "f 4 1 5\n"
    "f 4 5 8\n";

TEST_CASE("SceneLoader.load.buildsGameObjectTreeWithRenderables") {
    auto *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity("ev_ut_sceneloader", true));
    REQUIRE(fs->setupWriteDirectory());
    const char *name = "sl_cube.obj";
    fs->write(name, kCubeObj, sizeof(kCubeObj) - 1);

    eve::window::Window *win = nullptr;
    eve::graphics::Graphics *gfx = nullptr;
    openGfx(win, gfx);

    auto *loader = SceneLoader::create();
    SceneHost *h = loader->load(name);
    REQUIRE(h != nullptr);
    // Root + one mesh child = 2 GameObjects.
    CHECK_GE(loader->nodeCount(name), 2);
    CHECK(loader->loaded(name));
    CHECK(loader->host(name) == h);

    // Exactly one mesh GameObject with a linked Renderable3D.
    std::vector<SceneNode *> linked = h->findAllLinked();
    REQUIRE_EQ(linked.size(), 1u);
    REQUIRE_EQ(linked[0]->links.size(), 1u);
    CHECK(int(linked[0]->links[0].kind) == int(eve::scene::LinkKind::Renderable3D));
    auto *r = static_cast<eve::graphics::Renderable3D *>(linked[0]->links[0].target);
    REQUIRE(r != nullptr);
    CHECK(r->meshRenderer()->mesh != nullptr);

    // Hot-reload with an identical file is a fast no-op.
    SceneDiff out;
    CHECK(!loader->reload(name, &out));
    CHECK(out.empty());

    // Overwrite with a two-cube scene (two `o` objects) -> diff detects an add.
    const std::string twoCubes = std::string("o cubeA\n") + kCubeObj + "o cubeB\n" + kCubeObj;
    fs->write(name, twoCubes.data(), twoCubes.size());
    REQUIRE(loader->reload(name, &out));
    CHECK_GT(out.added, 0);
    // A second (unchanged) mesh object still has a linked Renderable3D.
    std::vector<SceneNode *> linked2 = h->findAllLinked();
    REQUIRE_EQ(linked2.size(), 2u);

    loader->unload(name);
    CHECK(!loader->loaded(name));
    win->close();
}

TEST_CASE("SceneLoader.bounds.fillFromMeshAndUnion") {
    // Minimal Assimp scene: node "tank" with one cube mesh child.
    // Heap-allocate every node so the Assimp destructors (which delete[] mesh
    // vertex/face arrays and the scene/root node) stay well-defined.
    auto *scene = new aiScene{};
    auto *rootNode = new aiNode{};
    auto *mesh = new aiMesh{};
    auto *face = new aiFace[1];
    auto *cube = new aiVector3D[8]{
        {-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
        {-1, -1, 1},  {1, -1, 1},  {1, 1, 1},  {-1, 1, 1},
    };
    auto *meshIdx = new unsigned[1]{0};
    auto *meshPtrs = new aiMesh *[1];
    meshPtrs[0] = mesh;
    mesh->mNumVertices = 8;
    mesh->mVertices = cube;
    mesh->mNumFaces = 1;
    mesh->mFaces = face;
    rootNode->mName = "tank";
    rootNode->mNumMeshes = 1;
    rootNode->mMeshes = meshIdx;
    scene->mNumMeshes = 1;
    scene->mMeshes = meshPtrs;
    scene->mRootNode = rootNode;
    scene->mNumMaterials = 0;
    scene->mMaterials = nullptr;
    scene->mNumAnimations = 0;

    MeshSlotMap slots;
    NodeDesc root = SceneLoader::buildNodeDesc(scene, &slots);
    REQUIRE(slots.count("tank_mesh0") == 1u);

    SceneHost *h = SceneHost::createHost("boundstest");
    h->setTree(std::move(root));
    SceneLoader::fillSceneBounds(h, slots);

    SceneNode *meshN = h->findById("tank_mesh0");
    REQUIRE(meshN != nullptr);
    CHECK(meshN->hasBounds);
    CHECK(approx(meshN->bminX, -1.f));
    CHECK(approx(meshN->bminY, -1.f));
    CHECK(approx(meshN->bmaxZ, 1.f));

    // ancestor union: mesh child has identity TRS → same local AABB
    SceneNode *tank = h->findById("tank");
    REQUIRE(tank != nullptr);
    CHECK(tank->hasBounds);
    CHECK(approx(tank->bminX, -1.f));
    CHECK(approx(tank->bmaxZ, 1.f));

    // moving a child merges (bounds are conservative and never shrink)
    meshN->x = 5.f;
    SceneLoader::fillSceneBounds(h, slots);
    CHECK(approx(tank->bminX, -1.f));
    CHECK(approx(tank->bmaxX, 6.f));

    // fresh host: transformed child union lands in parent local space
    SceneHost *h2 = SceneHost::createHost("boundstest2");
    h2->setTree(node("tank", {node("tank_mesh0").withPosition(5.f, 0.f, 0.f)}));
    SceneLoader::fillSceneBounds(h2, slots);
    SceneNode *tank2 = h2->findById("tank");
    REQUIRE(tank2 != nullptr);
    CHECK(approx(tank2->bminX, 4.f));
    CHECK(approx(tank2->bmaxX, 6.f));

    // hot-reload path (null slots) keeps bounds on kept nodes
    NodeDesc newRoot = node("tank", {node("tank_mesh0")});
    SceneDiff d = SceneLoader::diffTree(h, newRoot);
    REQUIRE(SceneLoader::applyTreeDiff(h, newRoot, d, nullptr, nullptr));
    SceneNode *kept = h->findById("tank_mesh0");
    REQUIRE(kept != nullptr);
    CHECK(kept->hasBounds);
    CHECK(approx(kept->bminX, -1.f));

    delete scene;  // frees rootNode + mesh pointer arrays; mesh dtor frees verts/faces
}
