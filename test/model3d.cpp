#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "model3d/Model3D.h"
#include "model3d/ModelData.h"
#include "model3d/ModelRenderer.h"
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

#include <SDL2/SDL.h>
#include <assimp/mesh.h>
#include <assimp/scene.h>
#include <assimp/material.h>
#include <assimp/matrix4x4.h>
#include <assimp/texture.h>
#include <assimp/vector3.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

#include "PathBesideSource.h"
EVE_DEFINE_PATH_BESIDE_SOURCE()

std::vector<char> readBinaryFile(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return {};
    return std::vector<char>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::string base64Encode(const uint8_t *data, size_t size) {
    static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((size + 2) / 3) * 4);
    for (size_t i = 0; i < size; i += 3) {
        const uint32_t a = data[i];
        const uint32_t b = i + 1 < size ? data[i + 1] : 0;
        const uint32_t c = i + 2 < size ? data[i + 2] : 0;
        const uint32_t v = (a << 16) | (b << 8) | c;
        out += tbl[(v >> 18) & 63];
        out += tbl[(v >> 12) & 63];
        out += (i + 1 < size) ? tbl[(v >> 6) & 63] : '=';
        out += (i + 2 < size) ? tbl[v & 63] : '=';
    }
    return out;
}

void appendF32LE(std::vector<uint8_t> &bin, float v) {
    uint32_t bits = 0;
    std::memcpy(&bits, &v, 4);
    bin.push_back(uint8_t(bits));
    bin.push_back(uint8_t(bits >> 8));
    bin.push_back(uint8_t(bits >> 16));
    bin.push_back(uint8_t(bits >> 24));
}

void appendU16LE(std::vector<uint8_t> &bin, uint16_t v) {
    bin.push_back(uint8_t(v));
    bin.push_back(uint8_t(v >> 8));
}

/**
 * Minimal glTF 2.0 quad (XY plane facing +Z) with an embedded PNG base color
 * texture: top half red, bottom half blue. Y=+1 maps to v=0 so a correctly
 * oriented render shows red on top / blue on bottom.
 */
std::string makeUvOrientationGltf(const std::string &pngBase64) {
    std::vector<uint8_t> bin;
    const float pos[4][3] = {{-1.f, -1.f, 0.f}, {1.f, -1.f, 0.f}, {-1.f, 1.f, 0.f}, {1.f, 1.f, 0.f}};
    const float nrm[4][3] = {{0.f, 0.f, 1.f}, {0.f, 0.f, 1.f}, {0.f, 0.f, 1.f}, {0.f, 0.f, 1.f}};
    const float uv[4][2] = {{0.f, 1.f}, {1.f, 1.f}, {0.f, 0.f}, {1.f, 0.f}};
    for (const auto &p : pos)
        for (float c : p) appendF32LE(bin, c);
    for (const auto &n : nrm)
        for (float c : n) appendF32LE(bin, c);
    for (const auto &t : uv)
        for (float c : t) appendF32LE(bin, c);
    const uint16_t idx[6] = {0, 1, 2, 2, 1, 3};
    for (uint16_t i : idx) appendU16LE(bin, i);
    if (bin.size() != 140) return {};

    const std::string binBase64 = base64Encode(bin.data(), bin.size());
    return std::string(
               "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],"
               "\"nodes\":[{\"mesh\":0}],"
               "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"NORMAL\":1,"
               "\"TEXCOORD_0\":2},\"indices\":3,\"material\":0}]}],"
               "\"materials\":[{\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0},"
               "\"metallicFactor\":0.0,\"roughnessFactor\":1.0}}],"
               "\"textures\":[{\"source\":0}],"
               "\"images\":[{\"uri\":\"data:image/png;base64,") +
           pngBase64 + "\"}],\"buffers\":[{\"uri\":\"data:application/octet-stream;base64," +
           binBase64 + "\",\"byteLength\":140}],\"bufferViews\":[" +
           "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":48,\"target\":34962}," +
           "{\"buffer\":0,\"byteOffset\":48,\"byteLength\":48,\"target\":34962}," +
           "{\"buffer\":0,\"byteOffset\":96,\"byteLength\":32,\"target\":34962}," +
           "{\"buffer\":0,\"byteOffset\":128,\"byteLength\":12,\"target\":34963}],"
           "\"accessors\":[" +
           "{\"bufferView\":0,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\"}," +
           "{\"bufferView\":1,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\"}," +
           "{\"bufferView\":2,\"componentType\":5126,\"count\":4,\"type\":\"VEC2\"}," +
           "{\"bufferView\":3,\"componentType\":5123,\"count\":6,\"type\":\"SCALAR\"}]}";
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
    const aiScene *scene = md->getScene();
    if (!scene || !scene->mRootNode) return b;
    std::function<void(const aiNode *, const aiMatrix4x4 &)> walk =
        [&](const aiNode *node, const aiMatrix4x4 &parent) {
            const aiMatrix4x4 world = parent * node->mTransformation;
            for (unsigned i = 0; i < node->mNumMeshes; ++i) {
                const unsigned mi = node->mMeshes[i];
                if (mi >= scene->mNumMeshes) continue;
                const aiMesh *mesh = scene->mMeshes[mi];
                if (!mesh) continue;
                for (unsigned v = 0; v < mesh->mNumVertices; ++v) {
                    const aiVector3D p = world * mesh->mVertices[v];
                    b.expand(p.x, p.y, p.z);
                }
            }
            for (unsigned c = 0; c < node->mNumChildren; ++c)
                walk(node->mChildren[c], world);
        };
    walk(scene->mRootNode, aiMatrix4x4());
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
    std::function<void(const aiNode *, const aiMatrix4x4 &)> walk =
        [&](const aiNode *node, const aiMatrix4x4 &parent) {
            const aiMatrix4x4 world = parent * node->mTransformation;
            for (unsigned i = 0; i < node->mNumMeshes; ++i) {
                const unsigned mi = node->mMeshes[i];
                if (mi >= scene->mNumMeshes) continue;
                const aiMesh *ai = scene->mMeshes[mi];
                if (!ai || ai->mNumFaces == 0) continue;
                Mesh *mesh = gfx->newMeshFromAssimp(*ai, world);
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
            for (unsigned c = 0; c < node->mNumChildren; ++c)
                walk(node->mChildren[c], world);
        };
    REQUIRE(scene->mRootNode != nullptr);
    walk(scene->mRootNode, aiMatrix4x4());
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

    // ~1s live rotation so the asset is visibly on screen before PNG capture.
    for (int i = 0; i < 60; ++i) {
        if (ecs::current()->getManager<Renderable3D>() != nullptr) {
            auto view = ecs::View<Renderable3D, Renderable3D::Transform3D, Renderable3D::MeshRenderer>();
            for (auto it = view.begin(); it != view.end(); ++it) {
                auto [xf, mr] = *it;
                if (!mr->visible)
                    continue;
                xf->yaw = float(i) * 0.05f;
            }
        }
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) break;
        }
        SDL_Delay(16);
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

TEST_CASE("model3d.materialApi.objMtl") {
    const std::string objPath = pathBesideThisSource("sofa.obj");
    const std::string mtlPath = pathBesideThisSource("sofa.mtl");
    REQUIRE(!readBinaryFile(objPath).empty());
    REQUIRE(!readBinaryFile(mtlPath).empty());

    std::string testDir = pathBesideThisSource("");
    if (!testDir.empty() && (testDir.back() == '/' || testDir.back() == '\\'))
        testDir.pop_back();

    auto *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity("ev_ut_model3d_material_obj", true));
    REQUIRE(fs->setupWriteDirectory());
    fs->allowMountingForPath(testDir);
    REQUIRE(fs->mount(testDir, "", false));

    auto *mod = eve::model3d::Model3D::create();
    auto *md = mod->newModelDataFromFile("sofa.obj");
    REQUIRE(md != nullptr);
    REQUIRE(md->getMaterialCount() >= 5);

    const int mi = md->getMaterialIndex(0);
    REQUIRE(mi >= 0);
    REQUIRE(mi < md->getMaterialCount());

    const std::string name = md->getMaterialName(mi);
    REQUIRE(!name.empty());

    // Blender Kd lands in DIFFUSE (BASE_COLOR fallback).
    const float r = md->getMaterialBaseColorR(mi);
    const float g = md->getMaterialBaseColorG(mi);
    const float b = md->getMaterialBaseColorB(mi);
    CHECK(r > 0.05f);
    CHECK(g > 0.05f);
    CHECK(b > 0.05f);
    CHECK(md->getMaterialBaseColorA(mi) == 1.f);
    // No PBR factors in OBJ/MTL: defaults.
    CHECK(md->getMaterialMetallicFactor(mi) == 0.f);
    CHECK(md->getMaterialRoughnessFactor(mi) == 0.45f);
    // sofa.mtl has no map_Kd.
    CHECK(md->getMaterialTextureSlotCount(mi, "diffuse") == 0);
    CHECK(md->getMaterialTexturePath(mi, "diffuse").empty());
    CHECK(md->getMaterialTextureEmbeddedIndex(mi, "diffuse") == -1);
    CHECK(md->getMaterialTextureSlotCount(mi, "base_color") == 0);
    // Unknown texture type names are rejected.
    CHECK(md->getMaterialTextureSlotCount(mi, "bogus") == 0);
    CHECK(md->getMaterialTexturePath(mi, "bogus").empty());

    delete md;
    CHECK(fs->unmount(testDir));
}

TEST_CASE("model3d.materialApi.gltf") {
    // CesiumMan.gltf references the external CesiumMan_data.bin buffer, so the
    // glTF must be loaded through the VFS (in-memory decode cannot resolve it).
    const std::string dir = pathBesideThisSource("assets/skinned/cesium_man/glTF/");
    REQUIRE(!readBinaryFile(dir + "CesiumMan.gltf").empty());
    REQUIRE(!readBinaryFile(dir + "CesiumMan_data.bin").empty());

    auto *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity("ev_ut_model3d_material_gltf", true));
    REQUIRE(fs->setupWriteDirectory());
    fs->allowMountingForPath(dir);
    REQUIRE(fs->mount(dir, "", false));

    auto *mod = eve::model3d::Model3D::create();
    auto *md = mod->newModelDataFromFile("CesiumMan.gltf");
    REQUIRE(md != nullptr);
    REQUIRE(md->getMaterialCount() >= 1);
    CHECK(md->getMaterialIndex(0) == 0);
    CHECK(!md->getMaterialName(0).empty());
    CHECK(md->hasNormals(0));
    CHECK(md->hasTexCoords(0));

    // CesiumMan material: metallic 0 / roughness 1, external base color JPEG.
    CHECK(md->getMaterialMetallicFactor(0) == 0.f);
    CHECK(md->getMaterialRoughnessFactor(0) == 1.f);
    CHECK(md->getMaterialTextureSlotCount(0, "base_color") == 1);
    CHECK(md->getMaterialTexturePath(0, "base_color") == "CesiumMan_img0.jpg");
    CHECK(md->getMaterialTextureEmbeddedIndex(0, "base_color") == -1);
    // Double-sided flag exists on glTF assets that set it; absence is fine here.
    CHECK(!md->getMaterialTwoSided(0));

    delete md;
    CHECK(fs->unmount(dir));
}

TEST_CASE("model3d.buildRenderable.objMtl") {
    std::string testDir = pathBesideThisSource("");
    if (!testDir.empty() && (testDir.back() == '/' || testDir.back() == '\\'))
        testDir.pop_back();

    auto *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity("ev_ut_model3d_build_obj", true));
    REQUIRE(fs->setupWriteDirectory());
    fs->allowMountingForPath(testDir);
    REQUIRE(fs->mount(testDir, "", false));

    auto *mod = eve::model3d::Model3D::create();
    auto *md = mod->newModelDataFromFile("sofa.obj");
    REQUIRE(md != nullptr);
    REQUIRE(md->getMeshCount() >= 1);

    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx);
    resetScene3D();

    auto *ent = mod->createRenderable(gfx, md, 0);
    REQUIRE(ent != nullptr);
    REQUIRE(ent->meshRenderer()->mesh != nullptr);
    REQUIRE(ent->meshRenderer()->mesh->indexCount > 0);
    const int mi = md->getMaterialIndex(0);
    if (mi >= 0) {
        CHECK(ent->meshRenderer()->r == md->getMaterialBaseColorR(mi));
        CHECK(ent->meshRenderer()->g == md->getMaterialBaseColorG(mi));
        CHECK(ent->meshRenderer()->b == md->getMaterialBaseColorB(mi));
        CHECK(ent->meshRenderer()->metallic == md->getMaterialMetallicFactor(mi));
        CHECK(ent->meshRenderer()->roughness == md->getMaterialRoughnessFactor(mi));
    }

    // Invalid indices return nullptr without throwing.
    CHECK(mod->createRenderable(gfx, md, 999) == nullptr);
    CHECK(eve::model3d::buildRenderable(*gfx, md, 999) == nullptr);
    CHECK(eve::model3d::buildRenderable(*gfx, nullptr, 0) == nullptr);

    win->close();
    delete md;
    CHECK(fs->unmount(testDir));
}

TEST_CASE("model3d.render.gltfEmbeddedUvOrientation") {
    // 4x2 texture: top row red, bottom row blue.
    const int tw = 4;
    const int th = 2;
    std::vector<uint8_t> px(size_t(tw) * size_t(th) * 4);
    for (int y = 0; y < th; ++y) {
        for (int x = 0; x < tw; ++x) {
            uint8_t *p = px.data() + (size_t(y) * tw + size_t(x)) * 4;
            const bool top = (y == 0);
            p[0] = top ? 255 : 0;
            p[1] = 0;
            p[2] = top ? 0 : 255;
            p[3] = 255;
        }
    }
    eve::image::Image::create();
    eve::image::ImageData pngImg(tw, th, "RGBA8", px.data(), false);
    eve::filesystem::FileData *png =
        pngImg.encode(eve::image::ImageData::FormatHandler::ENCODED_PNG, "gltf_uv.png", false);
    REQUIRE(png != nullptr);
    REQUIRE(png->getSize() > 0);
    const std::string gltfText =
        makeUvOrientationGltf(base64Encode(static_cast<const uint8_t *>(png->getData()), png->getSize()));
    delete png;

    auto *mod = eve::model3d::Model3D::create();
    eve::data::ByteData data(gltfText.data(), gltfText.size());
    auto *md = mod->newModelData(&data, ".gltf");
    REQUIRE(md != nullptr);
    REQUIRE(md->getMeshCount() >= 1);

    // Material API on the synthetic file: PBR factors + embedded texture.
    CHECK(md->getMaterialMetallicFactor(0) == 0.f);
    CHECK(md->getMaterialRoughnessFactor(0) == 1.f);
    CHECK(md->getMaterialTextureSlotCount(0, "base_color") == 1);
    CHECK(md->getMaterialTexturePath(0, "base_color") == "*0");
    CHECK(md->getMaterialTextureEmbeddedIndex(0, "base_color") == 0);
    CHECK(md->getEmbeddedTextureCount() == 1);
    CHECK(md->getEmbeddedTextureWidth(0) > 0);  // compressed blob: width == byte length
    CHECK(md->getEmbeddedTextureHeight(0) == 0);  // compressed blob

    eve::image::ImageData *decoded = md->getEmbeddedTextureImageData(0);
    REQUIRE(decoded != nullptr);
    CHECK(decoded->getWidth() == tw);
    CHECK(decoded->getHeight() == th);
    CHECK(decoded->getFormat() == "RGBA8");
    {
        const auto top = decoded->getPixel(0, 0);
        const auto bot = decoded->getPixel(0, 1);
        CHECK(top.r > top.b);
        CHECK(bot.b > bot.r);
    }
    delete decoded;

    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx);
    resetScene3D();

    auto *ent = mod->createRenderable(gfx, md, 0);
    REQUIRE(ent != nullptr);
    REQUIRE(ent->meshRenderer()->mesh != nullptr);
    REQUIRE(ent->meshRenderer()->texture != nullptr);  // embedded texture decoded + uploaded

    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 0.f, 3.f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setUp(0.f, 1.f, 0.f);
    cam->data()->nearZ = 0.05f;
    cam->data()->farZ = 100.f;

    RenderSystem3D::setDirectionalLight(0.f, 0.f, -1.f, 1.5f, 1.5f, 1.5f);
    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.f, 0.f, 0.f, 1.f));

    for (int i = 0; i < 8; ++i) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) break;
        }
        SDL_Delay(16);
    }

    // Straight-on quad: world +Y (texture v=0, red) must land on the upper half.
    const Color top = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 4);
    const Color bot = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() * 3 / 4);
    CHECK(luma(top) > 0.05f);
    CHECK(luma(bot) > 0.05f);
    CHECK(top.r > top.b);  // red on top → not V-flipped
    CHECK(bot.b > bot.r);  // blue on bottom

    eve::image::Image::create();
    eve::image::ImageData *frame = gfx->newImageData();
    REQUIRE(frame != nullptr);
    eve::filesystem::FileData *framePng =
        frame->encode(medialoader::FormatHandler::ENCODED_PNG, "model3d_gltf_uv.png", false);
    REQUIRE(framePng != nullptr);
    const std::string outDir = std::string(EVENGINE_TEST_BINARY_DIR) + "/out";
    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);
    const std::string outPath = outDir + "/model3d_gltf_uv.png";
    {
        std::ofstream out(outPath, std::ios::binary);
        REQUIRE(out.good());
        out.write(static_cast<const char *>(framePng->getData()),
                  static_cast<std::streamsize>(framePng->getSize()));
        REQUIRE(out.good());
    }
    std::printf("model3d render saved: %s\n", outPath.c_str());
    delete framePng;
    delete frame;

    win->close();
    delete md;
}

TEST_CASE("model3d.render.cesiumManGltf") {
    const std::string dir = pathBesideThisSource("assets/skinned/cesium_man/glTF/");
    REQUIRE(!readBinaryFile(dir + "CesiumMan.gltf").empty());
    REQUIRE(!readBinaryFile(dir + "CesiumMan_img0.jpg").empty());

    auto *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity("ev_ut_model3d_gltf", true));
    REQUIRE(fs->setupWriteDirectory());
    fs->allowMountingForPath(dir);
    REQUIRE(fs->mount(dir, "", false));

    auto *mod = eve::model3d::Model3D::create();
    auto *md = mod->newModelDataFromFile("CesiumMan.gltf");
    REQUIRE(md != nullptr);
    REQUIRE(md->getMeshCount() >= 1);

    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx);
    resetScene3D();

    auto *ent = mod->createRenderable(gfx, md, 0);
    REQUIRE(ent != nullptr);
    REQUIRE(ent->meshRenderer()->mesh != nullptr);
    REQUIRE(ent->meshRenderer()->texture != nullptr);  // CesiumMan_img0.jpg resolved via VFS

    Bounds b = boundsOf(md);
    REQUIRE(b.valid);
    const float cx = b.centerX();
    const float cy = b.centerY();
    const float cz = b.centerZ();
    const float rad = std::max(0.5f, b.radius());

    auto *cam = Camera3D::createCamera();
    cam->setTarget(cx, cy, cz);
    cam->setEye(cx + rad * 0.9f, cy + rad * 0.55f, cz + rad * 1.8f);
    cam->data()->nearZ = std::max(0.05f, rad * 0.01f);
    cam->data()->farZ = std::max(100.f, rad * 20.f);

    RenderSystem3D::setDirectionalLight(-(cx + rad * 0.9f), -(cy + rad * 0.55f) + rad * 1.2f,
                                        -(cz + rad * 1.8f), 1.8f, 1.8f, 1.8f);
    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.12f, 0.14f, 0.18f, 1.f));

    for (int i = 0; i < 60; ++i) {
        if (ecs::current()->getManager<Renderable3D>() != nullptr) {
            auto view = ecs::View<Renderable3D, Renderable3D::Transform3D, Renderable3D::MeshRenderer>();
            for (auto it = view.begin(); it != view.end(); ++it) {
                auto [xf, mr] = *it;
                if (!mr->visible) continue;
                xf->yaw = float(i) * 0.05f;
            }
        }
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) break;
        }
        SDL_Delay(16);
    }

    const Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.05f);

    eve::image::Image::create();
    eve::image::ImageData *frame = gfx->newImageData();
    REQUIRE(frame != nullptr);
    eve::filesystem::FileData *png =
        frame->encode(medialoader::FormatHandler::ENCODED_PNG, "model3d_cesium_man.png", false);
    REQUIRE(png != nullptr);
    const std::string outDir = std::string(EVENGINE_TEST_BINARY_DIR) + "/out";
    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);
    const std::string outPath = outDir + "/model3d_cesium_man.png";
    {
        std::ofstream out(outPath, std::ios::binary);
        REQUIRE(out.good());
        out.write(static_cast<const char *>(png->getData()), static_cast<std::streamsize>(png->getSize()));
        REQUIRE(out.good());
    }
    std::printf("model3d render saved: %s (materials=%d)\n", outPath.c_str(), md->getMaterialCount());
    delete png;
    delete frame;

    win->close();
    delete md;
    CHECK(fs->unmount(dir));
}
