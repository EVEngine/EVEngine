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
#include "image/Image.h"
#include "image/ImageData.h"

#include <assimp/mesh.h>
#include <assimp/scene.h>
#include <assimp/vector3.h>

#include <SDL2/SDL.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
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

std::string base64Encode(const uint8_t *data, size_t len) {
    static const char *kTable = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = uint32_t(data[i]) << 16;
        if (i + 1 < len) n |= uint32_t(data[i + 1]) << 8;
        if (i + 2 < len) n |= uint32_t(data[i + 2]);
        out.push_back(kTable[(n >> 18) & 63]);
        out.push_back(kTable[(n >> 12) & 63]);
        out.push_back((i + 1 < len) ? kTable[(n >> 6) & 63] : '=');
        out.push_back((i + 2 < len) ? kTable[n & 63] : '=');
    }
    return out;
}

std::vector<uint8_t> buildTriangleBuffer() {
    auto pushF = [](std::vector<uint8_t> &b, float v) {
        uint8_t bytes[4];
        std::memcpy(bytes, &v, sizeof(v));
        for (unsigned i = 0; i < 4; ++i) b.push_back(bytes[i]);
    };
    auto pushU16 = [](std::vector<uint8_t> &b, uint16_t v) {
        b.push_back(uint8_t(v & 0xff));
        b.push_back(uint8_t((v >> 8) & 0xff));
    };
    std::vector<uint8_t> buf;
    const float positions[9] = {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f};
    const float normals[9] = {0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f};
    const float uvs[6] = {0.f, 0.f, 1.f, 0.f, 0.f, 1.f};
    for (float v : positions) pushF(buf, v);
    for (float v : normals) pushF(buf, v);
    for (float v : uvs) pushF(buf, v);
    pushU16(buf, 0);
    pushU16(buf, 1);
    pushU16(buf, 2);
    return buf;
}

std::string buildPbrGltf() {
    std::vector<uint8_t> buf = buildTriangleBuffer();
    const std::string b64 = base64Encode(buf.data(), buf.size());
    return
        "{\"asset\":{\"version\":\"2.0\"},"
        "\"extensionsUsed\":[\"KHR_lights_punctual\"],"
        "\"extensions\":{\"KHR_lights_punctual\":{\"lights\":[{\"type\":\"directional\","
        "\"color\":[1.0,0.5,0.25],\"intensity\":2.0,\"name\":\"sun\"}]}},"
        "\"scene\":0,\"scenes\":[{\"nodes\":[0,1,2]}],"
        "\"nodes\":["
        "{\"name\":\"tri\",\"mesh\":0},"
        "{\"name\":\"light\",\"extensions\":{\"KHR_lights_punctual\":{\"light\":0}}},"
        "{\"name\":\"cam\",\"camera\":0}],"
        "\"cameras\":[{\"type\":\"perspective\",\"perspective\":{\"yfov\":0.7,\"znear\":0.1,"
        "\"zfar\":100.0}}],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"NORMAL\":1,"
        "\"TEXCOORD_0\":2},\"indices\":3,\"material\":0}]}],"
        "\"materials\":[{\"name\":\"pbr\",\"pbrMetallicRoughness\":{\"baseColorFactor\":"
        "[0.2,0.4,0.6,1.0],\"metallicFactor\":0.9,\"roughnessFactor\":0.25},\"doubleSided\":true}],"
        "\"buffers\":[{\"uri\":\"data:application/octet-stream;base64," + b64 +
        "\",\"byteLength\":" + std::to_string(buf.size()) + "}],"
        "\"bufferViews\":["
        "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36,\"target\":34962},"
        "{\"buffer\":0,\"byteOffset\":36,\"byteLength\":36,\"target\":34962},"
        "{\"buffer\":0,\"byteOffset\":72,\"byteLength\":24,\"target\":34962},"
        "{\"buffer\":0,\"byteOffset\":96,\"byteLength\":6,\"target\":34963}],"
        "\"accessors\":["
        "{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\","
        "\"min\":[0,0,0],\"max\":[1,1,0]},"
        "{\"bufferView\":1,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
        "{\"bufferView\":2,\"componentType\":5126,\"count\":3,\"type\":\"VEC2\"},"
        "{\"bufferView\":3,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}]}";
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

// ---- PBR material + texture cache + lights/cameras via a self-contained glTF ----

TEST_CASE("SceneLoader.load.pbrMaterialAndLightsAndCamera") {
    auto *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity("ev_ut_sceneloader_pbr", true));
    REQUIRE(fs->setupWriteDirectory());
    const std::string gltf = buildPbrGltf();
    fs->write("sl_pbr.gltf", gltf.data(), gltf.size());

    eve::window::Window *win = nullptr;
    eve::graphics::Graphics *gfx = nullptr;
    openGfx(win, gfx);

    auto *loader = SceneLoader::create();
    eve::sceneloader::LoadOptions opts;
    opts.importCameras = true;
    SceneHost *h = loader->load("sl_pbr.gltf", opts);
    REQUIRE(h != nullptr);

    // PBR factors landed on the linked Renderable3D.
    std::vector<SceneNode *> linked = h->findAllLinked();
    REQUIRE_EQ(linked.size(), 1u);
    auto *r = static_cast<eve::graphics::Renderable3D *>(linked[0]->linkTarget);
    REQUIRE(r != nullptr);
    auto mr = r->meshRenderer();
    CHECK(approx(mr->r, 0.2f, 1e-3f));
    CHECK(approx(mr->g, 0.4f, 1e-3f));
    CHECK(approx(mr->b, 0.6f, 1e-3f));
    CHECK(approx(mr->metallic, 0.9f, 1e-3f));
    CHECK(approx(mr->roughness, 0.25f, 1e-3f));

    // Lights + cameras imported.
    CHECK(loader->lightCount("sl_pbr.gltf") == 1);
    CHECK(loader->light("sl_pbr.gltf", 0) != nullptr);
    CHECK(loader->cameraCount("sl_pbr.gltf") == 1);
    eve::graphics::Camera3D *cam = loader->camera("sl_pbr.gltf", 0);
    REQUIRE(cam != nullptr);
    CHECK(!cam->data()->active);

    loader->unload("sl_pbr.gltf");
    CHECK(!loader->loaded("sl_pbr.gltf"));
    win->close();
}

TEST_CASE("SceneLoader.load.pbrTextureCacheReusesTexture") {
    auto *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity("ev_ut_sceneloader_texcache", true));
    REQUIRE(fs->setupWriteDirectory());

    // Encode a small PNG through the image module (PNG is the only decoder).
    auto *img = eve::image::Image::create()->newImageData(4, 4, "RGBA8");
    REQUIRE(img != nullptr);
    REQUIRE(img->encode(medialoader::FormatHandler::ENCODED_PNG, "sl_cache.png", true) != nullptr);

    // Two objects share one material + one map_Kd -> the same GPU Texture must
    // be reused for both meshes (cache keyed by path + wrap mode).
    const char kObj[] =
        "mtllib sl_cache.mtl\n"
        "o a\n"
        "v -1 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "vt 0 0\n"
        "vt 1 0\n"
        "vt 0 1\n"
        "usemtl m\n"
        "f 1/1 2/2 3/3\n"
        "o b\n"
        "v -1 -1 0\n"
        "v 1 -1 0\n"
        "v 0 0 0\n"
        "vt 0 0\n"
        "vt 1 0\n"
        "vt 0 1\n"
        "usemtl m\n"
        "f 4/4 5/5 6/6\n";
    const char kMtl[] = "newmtl m\nKd 1 1 1\nmap_Kd sl_cache.png\n";
    fs->write("sl_cache.obj", kObj, sizeof(kObj) - 1);
    fs->write("sl_cache.mtl", kMtl, sizeof(kMtl) - 1);

    eve::window::Window *win = nullptr;
    eve::graphics::Graphics *gfx = nullptr;
    openGfx(win, gfx);

    auto *loader = SceneLoader::create();
    SceneHost *h = loader->load("sl_cache.obj");
    REQUIRE(h != nullptr);
    auto linked = h->findAllLinked();
    REQUIRE_EQ(linked.size(), 2u);
    auto *r0 = static_cast<eve::graphics::Renderable3D *>(linked[0]->linkTarget);
    auto *r1 = static_cast<eve::graphics::Renderable3D *>(linked[1]->linkTarget);
    REQUIRE(r0 != nullptr);
    REQUIRE(r1 != nullptr);
    REQUIRE(r0->meshRenderer()->texture != nullptr);
    // Shared texture instance across meshes that reference the same image.
    const bool sharedTexture = r0->meshRenderer()->texture == r1->meshRenderer()->texture;
    CHECK(sharedTexture);

    // Reload path stays healthy (diff-based) and reuses the same cached texture.
    SceneDiff out;
    CHECK(!loader->reload("sl_cache.obj", &out));
    CHECK(out.empty());

    loader->unload("sl_cache.obj");
    win->close();
}

TEST_CASE("SceneLoader.load.gltfCesiumManPbrAndAnimation") {
    const std::string dir = pathBesideThisSource("assets/skinned/cesium_man/glTF");
    auto *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity("ev_ut_sceneloader_cesium", true));
    REQUIRE(fs->setupWriteDirectory());
    fs->allowMountingForPath(dir);
    REQUIRE(fs->mount(dir, "", false));

    eve::window::Window *win = nullptr;
    eve::graphics::Graphics *gfx = nullptr;
    openGfx(win, gfx);

    auto *loader = SceneLoader::create();
    eve::sceneloader::LoadOptions opts;
    opts.importCameras = true;
    opts.mipmaps = true;
    SceneHost *h = loader->load("CesiumMan.gltf", opts);
    REQUIRE(h != nullptr);
    CHECK(loader->nodeCount("CesiumMan.gltf") > 0);

    std::vector<SceneNode *> linked = h->findAllLinked();
    REQUIRE(!linked.empty());
    auto *r = static_cast<eve::graphics::Renderable3D *>(linked[0]->linkTarget);
    REQUIRE(r != nullptr);
    // CesiumMan material: roughnessFactor = 1.0 (engine default is 0.45) and a
    // baseColorTexture — both must have been imported.
    CHECK(approx(r->meshRenderer()->roughness, 1.f, 1e-3f));
    CHECK(r->meshRenderer()->texture != nullptr);

    // Skinned character ships an animation; skeleton + clips must be imported.
    CHECK(loader->animationCount("CesiumMan.gltf") >= 1);
    REQUIRE(loader->skeleton("CesiumMan.gltf") != nullptr);
    CHECK(loader->clip("CesiumMan.gltf", 0) != nullptr);

    loader->unload("CesiumMan.gltf");
    win->close();
}

// ---- async loading ----

TEST_CASE("SceneLoader.loadAsync.mountsOnPoll") {
    auto *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity("ev_ut_sceneloader_async", true));
    REQUIRE(fs->setupWriteDirectory());
    fs->write("sl_async.obj", kCubeObj, sizeof(kCubeObj) - 1);

    eve::window::Window *win = nullptr;
    eve::graphics::Graphics *gfx = nullptr;
    openGfx(win, gfx);

    auto *loader = SceneLoader::create();
    bool called = false;
    SceneHost *cbHost = nullptr;
    REQUIRE(loader->loadAsync("sl_async.obj", eve::sceneloader::LoadOptions{},
                              [&](SceneHost *h) {
                                  called = true;
                                  cbHost = h;
                              }));

    int applied = 0;
    for (int i = 0; i < 100 && applied == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        applied = loader->pollAsync();
    }
    CHECK(applied == 1);
    CHECK(called);
    REQUIRE(cbHost != nullptr);
    CHECK(loader->host("sl_async.obj") == cbHost);
    CHECK(loader->nodeCount("sl_async.obj") >= 2);
    CHECK(loader->pendingAsyncCount() == 0);

    loader->unload("sl_async.obj");
    win->close();
}

TEST_CASE("SceneLoader.loadAsync.precodesTextures") {
    auto *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity("ev_ut_sceneloader_asynctex", true));
    REQUIRE(fs->setupWriteDirectory());
    auto *img = eve::image::Image::create()->newImageData(4, 4, "RGBA8");
    REQUIRE(img != nullptr);
    REQUIRE(img->encode(medialoader::FormatHandler::ENCODED_PNG, "sl_at.png", true) != nullptr);
    const char kObj[] =
        "mtllib sl_at.mtl\n"
        "o a\n"
        "v -1 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "vt 0 0\n"
        "vt 1 0\n"
        "vt 0 1\n"
        "usemtl m\n"
        "f 1/1 2/2 3/3\n";
    const char kMtl[] = "newmtl m\nKd 1 1 1\nmap_Kd sl_at.png\n";
    fs->write("sl_at.obj", kObj, sizeof(kObj) - 1);
    fs->write("sl_at.mtl", kMtl, sizeof(kMtl) - 1);

    eve::window::Window *win = nullptr;
    eve::graphics::Graphics *gfx = nullptr;
    openGfx(win, gfx);

    auto *loader = SceneLoader::create();
    bool called = false;
    SceneHost *cbHost = nullptr;
    REQUIRE(loader->loadAsync("sl_at.obj", eve::sceneloader::LoadOptions{},
                              [&](SceneHost *h) {
                                  called = true;
                                  cbHost = h;
                              }));
    int applied = 0;
    for (int i = 0; i < 100 && applied == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        applied = loader->pollAsync();
    }
    CHECK(applied == 1);
    CHECK(called);
    REQUIRE(cbHost != nullptr);
    auto linked = cbHost->findAllLinked();
    REQUIRE_EQ(linked.size(), 1u);
    auto *r = static_cast<eve::graphics::Renderable3D *>(linked[0]->linkTarget);
    REQUIRE(r != nullptr);
    // Texture pre-decoded off-thread and uploaded on the main thread.
    CHECK(r->meshRenderer()->texture != nullptr);

    loader->unload("sl_at.obj");
    win->close();
}

TEST_CASE("SceneLoader.prewarmAsync.reusesDecodedScene") {
    auto *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity("ev_ut_sceneloader_prewarm", true));
    REQUIRE(fs->setupWriteDirectory());
    fs->write("sl_prewarm.obj", kCubeObj, sizeof(kCubeObj) - 1);

    auto *loader = SceneLoader::create();
    REQUIRE(loader->prewarmAsync("sl_prewarm.obj"));
    int applied = 0;
    for (int i = 0; i < 100 && applied == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        applied = loader->pollAsync();
    }
    CHECK(applied == 1);
    CHECK(loader->prewarmed("sl_prewarm.obj"));

    SceneHost *host = loader->load("sl_prewarm.obj", false);
    REQUIRE(host != nullptr);
    CHECK(!loader->prewarmed("sl_prewarm.obj"));
    CHECK(loader->nodeCount("sl_prewarm.obj") >= 2);
    loader->unload("sl_prewarm.obj");
}
