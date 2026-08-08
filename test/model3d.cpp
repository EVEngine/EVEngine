#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "model3d/Model3D.h"
#include "model3d/ModelData.h"
#include "data/ByteData.h"
#include "filesystem/Filesystem.h"
#include "common/Exception.h"

#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "image/Image.h"
#include "image/ImageData.h"
#include "window/Window.h"

#include <assimp/mesh.h>
#include <assimp/scene.h>
#include <assimp/material.h>
#include <assimp/texture.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "graphics/shaders/mesh3d_toon_frag_spv.inc"
#include "graphics/shaders/mesh3d_toon_vert_spv.inc"

using namespace eve::graphics;

namespace {

std::string pathBesideThisSource(const char *filename) {
    std::string here = __FILE__;
    auto slash = here.find_last_of("/\\");
    std::string dir = (slash == std::string::npos) ? std::string(".") : here.substr(0, slash);
    return dir + "/" + filename;
}

std::vector<char> readBinaryFile(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return {};
    return std::vector<char>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

float luma(const Color &c) { return (c.r + c.g + c.b) / 3.f; }

void openGfxWindow(eve::window::Window *&win, Graphics *&gfx, int w = 640, int h = 480) {
    win = eve::window::Window::create();
    gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = w;
    s.height = h;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
}

void resetScene3D() {
    if (ecs::current()->getManager<Renderable3D>() != nullptr) {
        auto view = ecs::View<Renderable3D, Renderable3D::MeshRenderer>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [mr] = *it;
            mr->visible = false;
        }
    }
    if (ecs::current()->getManager<Camera3D>() != nullptr) {
        auto camView = ecs::View<Camera3D, Camera3D::Data>();
        for (auto it = camView.begin(); it != camView.end(); ++it) {
            auto [data] = *it;
            data->active = false;
        }
    }
    if (ecs::current()->getManager<Renderable2D>() != nullptr) {
        auto view = ecs::View<Renderable2D, Renderable2D::Sprite>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [sp] = *it;
            sp->visible = false;
        }
    }
}

Texture *makeSolidGray(Graphics *gfx, uint8_t g) {
    uint8_t px[4] = {g, g, g, 255};
    return gfx->newTexture(1, 1, px);
}

Texture *makeSolidRGB(Graphics *gfx, uint8_t r, uint8_t g, uint8_t b) {
    uint8_t px[4] = {r, g, b, 255};
    return gfx->newTexture(1, 1, px);
}

Texture *textureFromImageData(Graphics *gfx, eve::image::ImageData *img) {
    if (!img)
        return nullptr;
    return gfx->newTexture(img);
}

Texture *loadAssimpDiffuseTexture(Graphics *gfx, const aiScene *scene, const aiMaterial *mat) {
    if (!scene || !mat)
        return nullptr;

    aiString path;
    if (mat->GetTexture(aiTextureType_DIFFUSE, 0, &path) != AI_SUCCESS)
        return nullptr;

    const char *p = path.C_Str();
    if (!p || !p[0])
        return nullptr;

    eve::image::Image::create();

    // Embedded: "*0", "*1", ...
    if (p[0] == '*') {
        int idx = std::atoi(p + 1);
        if (idx < 0 || static_cast<unsigned>(idx) >= scene->mNumTextures)
            return nullptr;
        const aiTexture *tex = scene->mTextures[idx];
        if (!tex || !tex->pcData)
            return nullptr;

        if (tex->mHeight == 0) {
            // Compressed blob (png/jpg/...).
            eve::data::ByteData bytes(tex->pcData, static_cast<size_t>(tex->mWidth));
            try {
                eve::image::ImageData *img = eve::image::Image::create()->newImageData(&bytes);
                Texture *t = textureFromImageData(gfx, img);
                delete img;
                return t;
            } catch (...) {
                return nullptr;
            }
        }

        // Uncompressed BGRA8888.
        const unsigned w = tex->mWidth;
        const unsigned h = tex->mHeight;
        std::vector<uint8_t> rgba(size_t(w) * size_t(h) * 4);
        const aiTexel *src = tex->pcData;
        for (unsigned i = 0; i < w * h; ++i) {
            rgba[i * 4 + 0] = src[i].r;
            rgba[i * 4 + 1] = src[i].g;
            rgba[i * 4 + 2] = src[i].b;
            rgba[i * 4 + 3] = src[i].a;
        }
        return gfx->newTexture(int(w), int(h), rgba.data());
    }

    // External file — try VFS then OS path beside test assets.
    try {
        auto *fs = eve::filesystem::Filesystem::create();
        std::unique_ptr<eve::filesystem::FileData> fd(fs->read(p));
        if (fd && fd->getSize() > 0) {
            eve::image::ImageData *img = eve::image::Image::create()->newImageData(fd.get());
            Texture *t = textureFromImageData(gfx, img);
            delete img;
            return t;
        }
    } catch (...) {
    }

    // Basename / common relative layouts under test/
    std::string base = p;
    auto slash = base.find_last_of("/\\");
    if (slash != std::string::npos)
        base = base.substr(slash + 1);

    const char *candidates[] = {
        base.c_str(),
        nullptr,  // filled below for Textures/
    };
    std::string underTextures = std::string("Textures/") + base;
    candidates[1] = underTextures.c_str();

    for (const char *rel : candidates) {
        if (!rel || !rel[0])
            continue;
        auto fileBytes = readBinaryFile(pathBesideThisSource(rel));
        if (fileBytes.empty())
            continue;
        try {
            eve::data::ByteData bytes(fileBytes.data(), fileBytes.size());
            eve::image::ImageData *img = eve::image::Image::create()->newImageData(&bytes);
            Texture *t = textureFromImageData(gfx, img);
            delete img;
            return t;
        } catch (...) {
        }
    }
    return nullptr;
}

Texture *loadTextureFile(Graphics *gfx, const char *relPathBesideTest) {
    if (!relPathBesideTest || !relPathBesideTest[0])
        return nullptr;
    auto fileBytes = readBinaryFile(pathBesideThisSource(relPathBesideTest));
    if (fileBytes.empty())
        return nullptr;
    eve::image::Image::create();
    try {
        eve::data::ByteData bytes(fileBytes.data(), fileBytes.size());
        eve::image::ImageData *img = eve::image::Image::create()->newImageData(&bytes);
        Texture *t = textureFromImageData(gfx, img);
        delete img;
        return t;
    } catch (...) {
        return nullptr;
    }
}

struct Bounds {
    float minX = 0, minY = 0, minZ = 0;
    float maxX = 0, maxY = 0, maxZ = 0;
    bool valid = false;

    void expand(float x, float y, float z) {
        if (!valid) {
            minX = maxX = x;
            minY = maxY = y;
            minZ = maxZ = z;
            valid = true;
            return;
        }
        minX = std::min(minX, x);
        minY = std::min(minY, y);
        minZ = std::min(minZ, z);
        maxX = std::max(maxX, x);
        maxY = std::max(maxY, y);
        maxZ = std::max(maxZ, z);
    }

    float centerX() const { return 0.5f * (minX + maxX); }
    float centerY() const { return 0.5f * (minY + maxY); }
    float centerZ() const { return 0.5f * (minZ + maxZ); }
    float radius() const {
        const float dx = maxX - minX;
        const float dy = maxY - minY;
        const float dz = maxZ - minZ;
        return 0.5f * std::sqrt(dx * dx + dy * dy + dz * dz);
    }
};

Bounds boundsOf(eve::model3d::ModelData *md) {
    Bounds b;
    for (int i = 0; i < md->getMeshCount(); ++i) {
        const aiMesh *mesh = md->getMesh(i);
        if (!mesh)
            continue;
        for (unsigned v = 0; v < mesh->mNumVertices; ++v)
            b.expand(mesh->mVertices[v].x, mesh->mVertices[v].y, mesh->mVertices[v].z);
    }
    return b;
}

/** Upload every mesh with Assimp material tint/texture; frame camera; save PNG.
 *  @param fallbackDiffuse optional path relative to test/ when Assimp has no map.
 *  @param makeStyle optional factory: given Graphics*, return Mesh3D Shader for all entities. */
void renderModelSmoke(eve::model3d::ModelData *md, const char *pngName,
                      const char *fallbackDiffuse = nullptr,
                      std::function<Shader *(Graphics *)> makeStyle = {}) {
    REQUIRE(md != nullptr);
    REQUIRE(md->getMeshCount() >= 1);
    REQUIRE(pngName != nullptr);
    const aiScene *scene = md->getScene();
    REQUIRE(scene != nullptr);

    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx);
    resetScene3D();

    Shader *styleShader = makeStyle ? makeStyle(gfx) : nullptr;

    Bounds b = boundsOf(md);
    REQUIRE(b.valid);
    const float cx = b.centerX();
    const float cy = b.centerY();
    const float cz = b.centerZ();
    const float rad = std::max(0.5f, b.radius());

    Texture *white = makeSolidGray(gfx, 255);
    Texture *fallbackTex = loadTextureFile(gfx, fallbackDiffuse);
    struct MatLook {
        Texture *tex = nullptr;
        float tr = 1.f, tg = 1.f, tb = 1.f;
    };
    std::unordered_map<unsigned, MatLook> matCache;

    int spawned = 0;
    for (int i = 0; i < md->getMeshCount(); ++i) {
        const aiMesh *ai = md->getMesh(i);
        if (!ai || ai->mNumFaces == 0)
            continue;
        Mesh *mesh = gfx->newMeshFromAssimp(*ai);
        REQUIRE(mesh != nullptr);
        REQUIRE_GT(mesh->indexCount, 0);

        MatLook look{white, 1.f, 1.f, 1.f};
        const unsigned matIndex = ai->mMaterialIndex;
        auto cached = matCache.find(matIndex);
        if (cached != matCache.end()) {
            look = cached->second;
        } else if (scene->mMaterials && matIndex < scene->mNumMaterials) {
            const aiMaterial *mat = scene->mMaterials[matIndex];
            aiColor3D kd(1.f, 1.f, 1.f);
            mat->Get(AI_MATKEY_COLOR_DIFFUSE, kd);

            Texture *fromMat = loadAssimpDiffuseTexture(gfx, scene, mat);
            if (fromMat) {
                look.tex = fromMat;
                look.tr = kd.r;
                look.tg = kd.g;
                look.tb = kd.b;
            } else if (fallbackTex) {
                look.tex = fallbackTex;
                look.tr = look.tg = look.tb = 1.f;
            } else {
                look.tex = makeSolidRGB(gfx, uint8_t(std::clamp(kd.r, 0.f, 1.f) * 255.f + 0.5f),
                                        uint8_t(std::clamp(kd.g, 0.f, 1.f) * 255.f + 0.5f),
                                        uint8_t(std::clamp(kd.b, 0.f, 1.f) * 255.f + 0.5f));
                look.tr = look.tg = look.tb = 1.f;
            }
            matCache[matIndex] = look;
        } else if (fallbackTex) {
            look.tex = fallbackTex;
            matCache[matIndex] = look;
        }

        auto *ent = Renderable3D::create();
        ent->meshRenderer()->mesh = mesh;
        ent->meshRenderer()->texture = look.tex;
        ent->meshRenderer()->shader = styleShader;
        ent->meshRenderer()->visible = true;
        ent->setTint(look.tr, look.tg, look.tb, 1.f);
        ++spawned;
    }
    REQUIRE(spawned >= 1);

    auto *cam = Camera3D::createCamera();
    // 3/4 view: above + along +Z. Fit clip planes to asset scale (Rock1 is ~1e3 units).
    cam->setTarget(cx, cy, cz);
    cam->setEye(cx + rad * 0.9f, cy + rad * 0.55f, cz + rad * 1.8f);
    cam->data()->nearZ = std::max(0.05f, rad * 0.01f);
    cam->data()->farZ = std::max(100.f, rad * 20.f);

    auto *hud = Renderable2D::create();
    hud->transform()->x = 0;
    hud->transform()->y = 0;
    hud->sprite()->width = 2;
    hud->sprite()->height = 2;
    hud->sprite()->r = 0.f;
    hud->sprite()->g = 0.f;
    hud->sprite()->b = 0.f;

    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.12f, 0.14f, 0.18f, 1.f));

    // Light rays travel from camera toward the subject (matches RenderSystem3D lighting test).
    const float lx = cx - (cx + rad * 0.9f);
    const float ly = cy - (cy + rad * 0.55f);
    const float lz = cz - (cz + rad * 1.8f);
    RenderSystem3D::setDirectionalLight(lx, ly + rad * 1.2f, lz, 1.8f, 1.8f, 1.8f);

    for (int i = 0; i < 10; ++i) {
        if (ecs::current()->getManager<Renderable3D>() != nullptr) {
            auto view = ecs::View<Renderable3D, Renderable3D::Transform3D, Renderable3D::MeshRenderer>();
            for (auto it = view.begin(); it != view.end(); ++it) {
                auto [xf, mr] = *it;
                if (!mr->visible)
                    continue;
                // Gentle yaw so materials stay readable in the last saved frame.
                xf->yaw = float(i) * 0.06f;
            }
        }
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }

    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.05f);

    eve::image::Image::create();
    eve::image::ImageData *frame = gfx->newImageData();
    REQUIRE(frame != nullptr);
    eve::filesystem::FileData *png =
        frame->encode(medialoader::FormatHandler::ENCODED_PNG, pngName, false);
    REQUIRE(png != nullptr);
    REQUIRE(png->getSize() > 0);

    // Under the CMake test binary dir (build/.../test/out), already gitignored via build*/.
    const std::string outDir = std::string(EVENGINE_TEST_BINARY_DIR) + "/out";
    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);
    const std::string outPath = outDir + "/" + pngName;
    {
        std::ofstream out(outPath, std::ios::binary);
        REQUIRE(out.good());
        out.write(static_cast<const char *>(png->getData()),
                  static_cast<std::streamsize>(png->getSize()));
        REQUIRE(out.good());
    }
    std::printf("model3d render saved: %s (materials=%u cached=%zu style=%s)\n", outPath.c_str(),
                scene->mNumMaterials, matCache.size(), styleShader ? "yes" : "no");
    delete png;
    delete frame;

    win->close();
}

}  // namespace

// Minimal unit cube as Wavefront OBJ (same shape as medialoader model tests).
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

TEST_CASE("model3d.newModelData.objFromMemory") {
    auto *mod = eve::model3d::Model3D::create();
    eve::data::ByteData data(kCubeObj, sizeof(kCubeObj) - 1);
    auto *md = mod->newModelData(&data, ".obj");
    REQUIRE(md != nullptr);
    CHECK(!md->empty());
    CHECK(md->getMeshCount() >= 1);
    CHECK(md->getVertexCount(0) > 0);
    CHECK(md->getFaceCount(0) > 0);
    CHECK(md->getMesh(0) != nullptr);
    CHECK(md->getScene() != nullptr);
    CHECK(md->getMaterialCount() >= 0);
    // Assimp's OBJ importer always computes per-face normals even without
    // `vn` lines, but never synthesizes UVs without `vt` lines.
    CHECK(md->hasNormals(0));
    CHECK(!md->hasTexCoords(0));
    delete md;
}

TEST_CASE("model3d.newModelData.emptyThrows") {
    auto *mod = eve::model3d::Model3D::create();
    bool threw = false;
    try {
        mod->newModelData(nullptr, ".obj");
    } catch (const eve::Exception &) {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE("model3d.newModelData.invalidMeshIndexThrows") {
    auto *mod = eve::model3d::Model3D::create();
    eve::data::ByteData data(kCubeObj, sizeof(kCubeObj) - 1);
    auto *md = mod->newModelData(&data, ".obj");
    REQUIRE(md != nullptr);

    auto expectThrows = [](const std::function<void()> &fn) {
        try {
            fn();
        } catch (const eve::Exception &) {
            return true;
        }
        return false;
    };

    CHECK(expectThrows([&] { md->getVertexCount(999); }));
    CHECK(expectThrows([&] { md->getFaceCount(999); }));
    CHECK(expectThrows([&] { md->hasNormals(999); }));
    CHECK(expectThrows([&] { md->hasTexCoords(999); }));
    CHECK(md->getMesh(999) == nullptr);
    delete md;
}

TEST_CASE("model3d.newModelDataFromFile.missingThrows") {
    auto *mod = eve::model3d::Model3D::create();
    bool threw = false;
    try {
        mod->newModelDataFromFile("definitely_missing_eve_model3d.obj");
    } catch (const eve::Exception &) {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE("model3d.Model3D.getName") {
    auto *mod = eve::model3d::Model3D::create();
    CHECK(mod->getName() == "Model3D");
}

TEST_CASE("model3d.newModelData.rock1Fbx") {
    const std::string path = pathBesideThisSource("Rock1.fbx");
    auto bytes = readBinaryFile(path);
    REQUIRE(!bytes.empty());

    auto *mod = eve::model3d::Model3D::create();
    eve::data::ByteData data(bytes.data(), bytes.size());
    auto *md = mod->newModelData(&data, ".fbx");
    REQUIRE(md != nullptr);
    CHECK(!md->empty());
    CHECK(md->getMeshCount() >= 1);
    CHECK(md->getScene() != nullptr);
    CHECK(md->hasTexCoords(0));

    int totalVerts = 0;
    int totalFaces = 0;
    for (int i = 0; i < md->getMeshCount(); ++i) {
        CHECK(md->getMesh(i) != nullptr);
        int verts = md->getVertexCount(i);
        int faces = md->getFaceCount(i);
        CHECK(verts > 0);
        CHECK(faces > 0);
        totalVerts += verts;
        totalFaces += faces;
    }
    CHECK(totalVerts > 0);
    CHECK(totalFaces > 0);
    delete md;
}

TEST_CASE("model3d.newModelDataFromFile.sofaObjMtl") {
    const std::string objPath = pathBesideThisSource("sofa.obj");
    const std::string mtlPath = pathBesideThisSource("sofa.mtl");
    REQUIRE(!readBinaryFile(objPath).empty());
    REQUIRE(!readBinaryFile(mtlPath).empty());

    std::string testDir = pathBesideThisSource("");
    if (!testDir.empty() && (testDir.back() == '/' || testDir.back() == '\\'))
        testDir.pop_back();

    auto *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity("ev_ut_model3d_sofa", true));
    REQUIRE(fs->setupWriteDirectory());
    fs->allowMountingForPath(testDir);
    REQUIRE(fs->mount(testDir, "", false));

    auto *mod = eve::model3d::Model3D::create();
    auto *md = mod->newModelDataFromFile("sofa.obj");
    REQUIRE(md != nullptr);
    CHECK(!md->empty());
    CHECK(md->getMeshCount() >= 1);
    // Blender MTL lists 5 materials; Assimp may add a default.
    CHECK(md->getMaterialCount() >= 5);
    CHECK(md->getScene() != nullptr);

    int totalVerts = 0;
    int totalFaces = 0;
    for (int i = 0; i < md->getMeshCount(); ++i) {
        CHECK(md->getMesh(i) != nullptr);
        int verts = md->getVertexCount(i);
        int faces = md->getFaceCount(i);
        CHECK(verts > 0);
        CHECK(faces > 0);
        totalVerts += verts;
        totalFaces += faces;
    }
    CHECK(totalVerts > 0);
    CHECK(totalFaces > 0);
    delete md;

    CHECK(fs->unmount(testDir));
}

TEST_CASE("model3d.render.rock1Fbx") {
    const std::string path = pathBesideThisSource("Rock1.fbx");
    auto bytes = readBinaryFile(path);
    REQUIRE(!bytes.empty());
    REQUIRE(!readBinaryFile(pathBesideThisSource("Textures/Rock1_Diffuse.png")).empty());

    auto *mod = eve::model3d::Model3D::create();
    eve::data::ByteData data(bytes.data(), bytes.size());
    auto *md = mod->newModelData(&data, ".fbx");
    REQUIRE(md != nullptr);
    // FBX material has no embedded map path; bind companion diffuse next to the asset.
    renderModelSmoke(md, "model3d_rock1.png", "Textures/Rock1_Diffuse.png");
    delete md;
}

TEST_CASE("model3d.render.rock1ToonShader") {
    const std::string path = pathBesideThisSource("Rock1.fbx");
    auto bytes = readBinaryFile(path);
    REQUIRE(!bytes.empty());
    REQUIRE(!readBinaryFile(pathBesideThisSource("Textures/Rock1_Diffuse.png")).empty());

    auto *mod = eve::model3d::Model3D::create();
    eve::data::ByteData data(bytes.data(), bytes.size());
    auto *md = mod->newModelData(&data, ".fbx");
    REQUIRE(md != nullptr);

    renderModelSmoke(md, "model3d_rock1_toon.png", "Textures/Rock1_Diffuse.png",
                     [](Graphics *gfx) -> Shader * {
                         std::vector<uint32_t> vert(mesh3d_toon_vert_spv,
                                                    mesh3d_toon_vert_spv + mesh3d_toon_vert_spv_count);
                         std::vector<uint32_t> frag(mesh3d_toon_frag_spv,
                                                    mesh3d_toon_frag_spv + mesh3d_toon_frag_spv_count);
                         Shader *toon = gfx->newMeshShaderFromSpv(vert, frag);
                         if (!toon || !toon->gpuHandle)
                             throw eve::Exception("failed to create toon mesh shader");
                         toon->declareFloat("bands");
                         toon->declareFloat("rimPower");
                         toon->declareFloat("rimStrength");
                         toon->declareFloat("posterize");
                         toon->sendFloat("bands", 4.f);
                         toon->sendFloat("rimPower", 2.8f);
                         toon->sendFloat("rimStrength", 0.55f);
                         toon->sendFloat("posterize", 6.f);
                         return toon;
                     });
    delete md;
}

TEST_CASE("model3d.render.sofaObjMtl") {
    const std::string objPath = pathBesideThisSource("sofa.obj");
    const std::string mtlPath = pathBesideThisSource("sofa.mtl");
    REQUIRE(!readBinaryFile(objPath).empty());
    REQUIRE(!readBinaryFile(mtlPath).empty());

    std::string testDir = pathBesideThisSource("");
    if (!testDir.empty() && (testDir.back() == '/' || testDir.back() == '\\'))
        testDir.pop_back();

    auto *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity("ev_ut_model3d_sofa_render", true));
    REQUIRE(fs->setupWriteDirectory());
    fs->allowMountingForPath(testDir);
    REQUIRE(fs->mount(testDir, "", false));

    auto *mod = eve::model3d::Model3D::create();
    auto *md = mod->newModelDataFromFile("sofa.obj");
    REQUIRE(md != nullptr);
    renderModelSmoke(md, "model3d_sofa.png");
    delete md;

    CHECK(fs->unmount(testDir));
}
