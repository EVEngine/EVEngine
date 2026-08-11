#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <SDL2/SDL.h>
#include <assimp/material.h>
#include <assimp/mesh.h>
#include <assimp/scene.h>
#include <assimp/texture.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "data/ByteData.h"
#include "filesystem/Filesystem.h"
#include "graphics/Graphics.h"
#include "graphics/Light.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "image/Image.h"
#include "image/ImageData.h"
#include "model3d/Model3D.h"
#include "model3d/ModelData.h"
#include "window/Window.h"

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
    if (!in) return {};
    return std::vector<char>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool fileExists(const std::string &path) {
    return std::filesystem::is_regular_file(path);
}

float luma(const Color &c) {
    return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
}

void openGfxWindow(eve::window::Window *&win, Graphics *&gfx, int w = 960, int h = 540) {
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
            data->envMap = nullptr;
            data->envIntensity = 1.f;
        }
    }
    if (ecs::current()->getManager<Light3D>() != nullptr) {
        auto lightView = ecs::View<Light3D, Light3D::Data>();
        for (auto it = lightView.begin(); it != lightView.end(); ++it) {
            auto [d] = *it;
            d->enabled = false;
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

Texture *makeSolid(Graphics *gfx, uint8_t r, uint8_t g, uint8_t b) {
    const uint8_t px[4] = {r, g, b, 255};
    return gfx->newTexture(1, 1, px);
}

Texture *makeSolidCubemap(Graphics *gfx, uint8_t r, uint8_t g, uint8_t b, int face = 8) {
    std::vector<uint8_t> faces(size_t(face) * size_t(face) * 4u * 6u);
    for (size_t i = 0; i < faces.size(); i += 4) {
        faces[i + 0] = r;
        faces[i + 1] = g;
        faces[i + 2] = b;
        faces[i + 3] = 255;
    }
    return gfx->newCubemap(face, faces.data());
}

/** Studio-like face-colored env so metal reflections read clearly in flight. */
Texture *makeStudioCubemap(Graphics *gfx, int face = 16) {
    const uint8_t faceRgb[6][3] = {
        {220, 180, 140},  // +X warm
        {120, 150, 210},  // -X cool
        {245, 245, 250},  // +Y skylight
        {40, 40, 45},     // -Y ground
        {200, 210, 230},  // +Z
        {180, 160, 140},  // -Z
    };
    const size_t faceBytes = size_t(face) * size_t(face) * 4u;
    std::vector<uint8_t> faces(faceBytes * 6u);
    for (int f = 0; f < 6; ++f) {
        uint8_t *dst = faces.data() + size_t(f) * faceBytes;
        for (size_t i = 0; i < faceBytes; i += 4) {
            dst[i + 0] = faceRgb[f][0];
            dst[i + 1] = faceRgb[f][1];
            dst[i + 2] = faceRgb[f][2];
            dst[i + 3] = 255;
        }
    }
    return gfx->newCubemap(face, faces.data());
}

Texture *textureFromImageData(Graphics *gfx, eve::image::ImageData *img) {
    if (!img) return nullptr;
    return gfx->newTexture(img);
}

Texture *loadAssimpDiffuseTexture(Graphics *gfx, const aiScene *scene, const aiMaterial *mat,
                                  const std::string &assetDir) {
    if (!scene || !mat) return nullptr;

    aiString path;
    if (mat->GetTexture(aiTextureType_DIFFUSE, 0, &path) != AI_SUCCESS &&
        mat->GetTexture(aiTextureType_BASE_COLOR, 0, &path) != AI_SUCCESS) {
        return nullptr;
    }

    const char *p = path.C_Str();
    if (!p || !p[0]) return nullptr;

    eve::image::Image::create();

    if (p[0] == '*') {
        int idx = std::atoi(p + 1);
        if (idx < 0 || static_cast<unsigned>(idx) >= scene->mNumTextures) return nullptr;
        const aiTexture *tex = scene->mTextures[idx];
        if (!tex || !tex->pcData) return nullptr;
        if (tex->mHeight == 0) {
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
        return nullptr;
    }

    std::string full = assetDir;
    if (!full.empty() && full.back() != '/' && full.back() != '\\') full.push_back('/');
    full += p;
    // Assimp sometimes returns nested paths with backslashes.
    for (char &c : full) {
        if (c == '\\') c = '/';
    }
    auto raw = readBinaryFile(full);
    if (raw.empty()) {
        // Try basename only under assetDir (glTF often stores URI as filename).
        const char *base = std::strrchr(p, '/');
        if (!base) base = std::strrchr(p, '\\');
        base = base ? base + 1 : p;
        full = assetDir;
        if (!full.empty() && full.back() != '/' && full.back() != '\\') full.push_back('/');
        full += base;
        raw = readBinaryFile(full);
    }
    if (raw.empty()) return nullptr;
    eve::data::ByteData bytes(raw.data(), raw.size());
    try {
        eve::image::ImageData *img = eve::image::Image::create()->newImageData(&bytes);
        Texture *t = textureFromImageData(gfx, img);
        delete img;
        return t;
    } catch (...) {
        return nullptr;
    }
}

struct Bounds {
    bool valid = false;
    float minX = 0, minY = 0, minZ = 0;
    float maxX = 0, maxY = 0, maxZ = 0;
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
        if (!valid) return 1.f;
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
        if (!mesh) continue;
        for (unsigned v = 0; v < mesh->mNumVertices; ++v)
            b.expand(mesh->mVertices[v].x, mesh->mVertices[v].y, mesh->mVertices[v].z);
    }
    return b;
}

void addHud() {
    auto *hud = Renderable2D::create();
    hud->transform()->x = 0;
    hud->transform()->y = 0;
    hud->sprite()->width = 2;
    hud->sprite()->height = 2;
    hud->sprite()->r = 0.f;
    hud->sprite()->g = 0.f;
    hud->sprite()->b = 0.f;
}

struct CamKey {
    float eyeX, eyeY, eyeZ;
    float tgtX, tgtY, tgtZ;
};

CamKey lerpKey(const CamKey &a, const CamKey &b, float t) {
    const float u = std::clamp(t, 0.f, 1.f);
    return CamKey{a.eyeX + (b.eyeX - a.eyeX) * u, a.eyeY + (b.eyeY - a.eyeY) * u,
                  a.eyeZ + (b.eyeZ - a.eyeZ) * u, a.tgtX + (b.tgtX - a.tgtX) * u,
                  a.tgtY + (b.tgtY - a.tgtY) * u, a.tgtZ + (b.tgtZ - a.tgtZ) * u};
}

void applyCam(Camera3D *cam, const CamKey &k) {
    cam->setEye(k.eyeX, k.eyeY, k.eyeZ);
    cam->setTarget(k.tgtX, k.tgtY, k.tgtZ);
}

enum class RenderCfg {
    AmbientOnly,
    DirectionalLit,
    DirectionalShadows,
    MultiPointLit,
    IblReflections,
};

const char *cfgName(RenderCfg c) {
    switch (c) {
    case RenderCfg::AmbientOnly:
        return "ambient";
    case RenderCfg::DirectionalLit:
        return "dir_lit";
    case RenderCfg::DirectionalShadows:
        return "dir_shadows";
    case RenderCfg::MultiPointLit:
        return "point_lit";
    case RenderCfg::IblReflections:
        return "ibl_reflect";
    }
    return "unknown";
}

struct SceneActors {
    Camera3D *cam = nullptr;
    Light3D *sun = nullptr;
    Light3D *pointA = nullptr;
    Light3D *pointB = nullptr;
    Texture *env = nullptr;
    std::vector<Renderable3D *> ents;
    Bounds bounds;
};

void disableAllLights(SceneActors &a) {
    if (a.sun) a.sun->setEnabled(false);
    if (a.pointA) a.pointA->setEnabled(false);
    if (a.pointB) a.pointB->setEnabled(false);
    RenderSystem3D::setDirectionalLight(0.f, 1.f, 0.f, 0.f, 0.f, 0.f);
}

void applyConfig(SceneActors &a, RenderCfg cfg, bool polishMetals) {
    disableAllLights(a);
    a.cam->setEnvMap(nullptr);
    a.cam->setEnvIntensity(0.f);

    switch (cfg) {
    case RenderCfg::AmbientOnly:
        a.cam->setAmbient(0.18f, 0.18f, 0.2f);
        break;
    case RenderCfg::DirectionalLit:
        a.cam->setAmbient(0.06f, 0.06f, 0.07f);
        a.sun->setEnabled(true);
        a.sun->setCastShadow(false);
        a.sun->setColor(1.f, 0.97f, 0.92f, 2.2f);
        break;
    case RenderCfg::DirectionalShadows:
        a.cam->setAmbient(0.04f, 0.04f, 0.05f);
        a.sun->setEnabled(true);
        a.sun->setCastShadow(true);
        a.sun->setShadowStrength(1.f);
        a.sun->setColor(1.f, 0.97f, 0.92f, 2.6f);
        break;
    case RenderCfg::MultiPointLit:
        a.cam->setAmbient(0.03f, 0.03f, 0.04f);
        a.pointA->setEnabled(true);
        a.pointB->setEnabled(true);
        break;
    case RenderCfg::IblReflections:
        a.cam->setAmbient(0.02f, 0.02f, 0.03f);
        a.sun->setEnabled(true);
        a.sun->setCastShadow(false);
        a.sun->setColor(0.55f, 0.55f, 0.6f, 0.8f);
        a.cam->setEnvMap(a.env);
        a.cam->setEnvIntensity(1.35f);
        if (polishMetals) {
            for (auto *e : a.ents) {
                e->setMetallic(std::max(e->meshRenderer()->metallic, 0.85f));
                e->setRoughness(std::min(e->meshRenderer()->roughness, 0.2f));
            }
        }
        break;
    }
}

void warmPresent(Graphics *gfx) {
    for (int i = 0; i < 2; ++i) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }
}

void saveFramePng(Graphics *gfx, const std::string &name) {
    eve::image::Image::create();
    eve::image::ImageData *frame = gfx->newImageData();
    REQUIRE(frame != nullptr);
    eve::filesystem::FileData *png =
        frame->encode(medialoader::FormatHandler::ENCODED_PNG, name.c_str(), false);
    REQUIRE(png != nullptr);
    const std::string outDir = std::string(EVENGINE_TEST_BINARY_DIR) + "/out/classic_scenes";
    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);
    const std::string outPath = outDir + "/" + name;
    {
        std::ofstream out(outPath, std::ios::binary);
        REQUIRE(out.good());
        out.write(static_cast<const char *>(png->getData()),
                  static_cast<std::streamsize>(png->getSize()));
        REQUIRE(out.good());
    }
    std::printf("ClassicScenes frame: %s\n", outPath.c_str());
    delete png;
    delete frame;
}

float meanLuma(Graphics *gfx, int step = 8) {
    const int w = gfx->getWidth();
    const int h = gfx->getHeight();
    double sum = 0.0;
    int n = 0;
    for (int y = 0; y < h; y += step) {
        for (int x = 0; x < w; x += step) {
            sum += luma(gfx->getPixel(x, y));
            ++n;
        }
    }
    return n ? float(sum / n) : 0.f;
}

/** Spawn every Assimp mesh with material tint / diffuse texture. */
int spawnModel(Graphics *gfx, eve::model3d::ModelData *md, const std::string &assetDir,
               SceneActors &out, float defaultMetallic = 0.05f, float defaultRoughness = 0.55f) {
    REQUIRE(md != nullptr);
    const aiScene *scene = md->getScene();
    REQUIRE(scene != nullptr);
    Texture *white = makeSolid(gfx, 230, 230, 230);

    struct MatLook {
        Texture *tex = nullptr;
        float tr = 1.f, tg = 1.f, tb = 1.f;
        float metallic = 0.05f;
        float roughness = 0.55f;
    };
    std::unordered_map<unsigned, MatLook> matCache;

    int spawned = 0;
    for (int i = 0; i < md->getMeshCount(); ++i) {
        const aiMesh *ai = md->getMesh(i);
        if (!ai || ai->mNumFaces == 0) continue;
        Mesh *mesh = gfx->newMeshFromAssimp(*ai);
        REQUIRE(mesh != nullptr);

        MatLook look{white, 1.f, 1.f, 1.f, defaultMetallic, defaultRoughness};
        const unsigned matIndex = ai->mMaterialIndex;
        auto cached = matCache.find(matIndex);
        if (cached != matCache.end()) {
            look = cached->second;
        } else if (scene->mMaterials && matIndex < scene->mNumMaterials) {
            const aiMaterial *mat = scene->mMaterials[matIndex];
            aiColor3D kd(1.f, 1.f, 1.f);
            mat->Get(AI_MATKEY_COLOR_DIFFUSE, kd);
            float metallic = defaultMetallic;
            float roughness = defaultRoughness;
            mat->Get(AI_MATKEY_METALLIC_FACTOR, metallic);
            mat->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness);

            Texture *fromMat = loadAssimpDiffuseTexture(gfx, scene, mat, assetDir);
            if (fromMat) {
                look.tex = fromMat;
                look.tr = kd.r;
                look.tg = kd.g;
                look.tb = kd.b;
            } else {
                look.tex = makeSolid(gfx, uint8_t(std::clamp(kd.r, 0.f, 1.f) * 255.f + 0.5f),
                                     uint8_t(std::clamp(kd.g, 0.f, 1.f) * 255.f + 0.5f),
                                     uint8_t(std::clamp(kd.b, 0.f, 1.f) * 255.f + 0.5f));
                look.tr = look.tg = look.tb = 1.f;
            }
            look.metallic = metallic;
            look.roughness = roughness;
            matCache[matIndex] = look;
        }

        auto *ent = Renderable3D::create();
        ent->meshRenderer()->mesh = mesh;
        ent->meshRenderer()->texture = look.tex;
        ent->meshRenderer()->visible = true;
        ent->setTint(look.tr, look.tg, look.tb, 1.f);
        ent->setMetallic(look.metallic);
        ent->setRoughness(look.roughness);
        ent->setCastShadow(true);
        ent->setReceiveShadow(true);
        out.ents.push_back(ent);
        ++spawned;
    }
    return spawned;
}

/** Procedural metal/roughness chart — classic material-response test. */
void spawnMetalRoughnessChart(Graphics *gfx, SceneActors &out) {
    Mesh *sphere = gfx->newMeshSphere(24, 16);
    REQUIRE(sphere != nullptr);
    const int nM = 4;
    const int nR = 4;
    for (int im = 0; im < nM; ++im) {
        for (int ir = 0; ir < nR; ++ir) {
            auto *ent = Renderable3D::create();
            ent->setMesh(sphere);
            ent->setTexture(makeSolid(gfx, 210, 210, 215));
            const float x = (float(im) - 1.5f) * 1.15f;
            const float y = (float(ir) - 1.5f) * 1.15f;
            ent->setPosition(x, y, 0.f);
            ent->setScale(0.42f, 0.42f, 0.42f);
            ent->setMetallic(float(im) / float(nM - 1));
            ent->setRoughness(0.08f + 0.85f * (float(ir) / float(nR - 1)));
            ent->setCastShadow(true);
            ent->setReceiveShadow(true);
            out.ents.push_back(ent);
            out.bounds.expand(x - 0.5f, y - 0.5f, -0.5f);
            out.bounds.expand(x + 0.5f, y + 0.5f, 0.5f);
        }
    }
}

eve::model3d::ModelData *loadModelFromDir(const std::string &dir, const std::string &relPath) {
    REQUIRE(fileExists(dir + "/" + relPath));
    auto *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity("ev_ut_classic_scenes", true));
    REQUIRE(fs->setupWriteDirectory());
    fs->allowMountingForPath(dir);
    REQUIRE(fs->mount(dir, "", false));
    auto *mod = eve::model3d::Model3D::create();
    return mod->newModelDataFromFile(relPath);
}

void setupLights(SceneActors &a, const Bounds &b) {
    const float cx = b.centerX();
    const float cy = b.centerY();
    const float cz = b.centerZ();
    const float rad = std::max(0.5f, b.radius());

    a.sun = Light3D::createLight("dir");
    a.sun->setDirection(0.45f, 1.f, 0.35f);
    a.sun->setColor(1.f, 0.97f, 0.92f, 2.2f);
    a.sun->setCastShadow(false);
    a.sun->setEnabled(false);

    a.pointA = Light3D::createLight("point");
    a.pointA->setPosition(cx - rad * 0.55f, cy + rad * 0.35f, cz + rad * 0.2f);
    a.pointA->setColor(1.f, 0.55f, 0.35f, 4.5f);
    a.pointA->setRadius(rad * 2.2f);
    a.pointA->setEnabled(false);

    a.pointB = Light3D::createLight("point");
    a.pointB->setPosition(cx + rad * 0.55f, cy + rad * 0.25f, cz - rad * 0.15f);
    a.pointB->setColor(0.35f, 0.55f, 1.f, 4.0f);
    a.pointB->setRadius(rad * 2.2f);
    a.pointB->setEnabled(false);
}

std::vector<CamKey> makeOrbitPath(const Bounds &b, float elev = 0.35f, float distScale = 1.7f) {
    const float cx = b.centerX();
    const float cy = b.centerY();
    const float cz = b.centerZ();
    const float rad = std::max(0.5f, b.radius());
    const float dist = rad * distScale;
    const float height = cy + rad * elev;
    std::vector<CamKey> keys;
    const int n = 5;
    for (int i = 0; i < n; ++i) {
        const float a = float(i) / float(n) * 6.2831853f;
        keys.push_back(CamKey{cx + std::cos(a) * dist, height, cz + std::sin(a) * dist, cx, cy, cz});
    }
    keys.push_back(keys.front());
    return keys;
}

std::vector<CamKey> makeCornellPath(const Bounds &b) {
    const float cx = b.centerX();
    const float cy = b.centerY();
    const float cz = b.centerZ();
    return {
        CamKey{cx, cy + 0.15f, cz + 2.4f, cx, cy + 0.1f, cz},
        CamKey{cx - 0.55f, cy + 0.55f, cz + 1.6f, cx, cy + 0.35f, cz - 0.2f},
        CamKey{cx + 0.55f, cy + 0.35f, cz + 1.5f, cx, cy + 0.2f, cz - 0.1f},
        CamKey{cx, cy + 0.9f, cz + 1.2f, cx, cy + 0.2f, cz},
        CamKey{cx, cy + 0.15f, cz + 2.4f, cx, cy + 0.1f, cz},
    };
}

std::vector<CamKey> makeSponzaPath(const Bounds &b) {
    const float cx = b.centerX();
    const float cy = b.centerY();
    const float cz = b.centerZ();
    const float rad = std::max(1.f, b.radius());
    // Walk the atrium long axis then lift to a gallery-ish view.
    return {
        CamKey{cx, cy + rad * 0.08f, cz + rad * 0.55f, cx, cy + rad * 0.05f, cz},
        CamKey{cx + rad * 0.25f, cy + rad * 0.12f, cz, cx, cy + rad * 0.08f, cz - rad * 0.1f},
        CamKey{cx, cy + rad * 0.1f, cz - rad * 0.5f, cx, cy + rad * 0.08f, cz},
        CamKey{cx - rad * 0.2f, cy + rad * 0.28f, cz + rad * 0.15f, cx, cy + rad * 0.05f, cz},
        CamKey{cx, cy + rad * 0.08f, cz + rad * 0.55f, cx, cy + rad * 0.05f, cz},
    };
}

/**
 * Fly the camera through @p path while cycling render configs.
 * Presents live frames to the window and writes PNG snapshots per phase.
 * Returns mean luma sampled at the end of each config phase.
 */
std::vector<float> flyThroughConfigs(Graphics *gfx, SceneActors &actors,
                                     const std::vector<CamKey> &path, const char *sceneTag,
                                     bool polishMetalsForIbl, int framesPerLeg = 36) {
    REQUIRE(actors.cam != nullptr);
    REQUIRE(path.size() >= 2);
    REQUIRE(actors.env != nullptr);

    const RenderCfg phases[] = {
        RenderCfg::AmbientOnly,     RenderCfg::DirectionalLit, RenderCfg::DirectionalShadows,
        RenderCfg::MultiPointLit,   RenderCfg::IblReflections,
    };
    const int nPhases = int(sizeof(phases) / sizeof(phases[0]));
    std::vector<float> phaseLuma;
    phaseLuma.reserve(size_t(nPhases));

    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.08f, 0.09f, 0.11f, 1.f));
    addHud();

    // Distribute path progress across phases so each config is seen in flight.
    const int legs = int(path.size()) - 1;
    const int totalFrames = std::max(legs * framesPerLeg, nPhases * 12);
    int frame = 0;
    for (int pi = 0; pi < nPhases; ++pi) {
        applyConfig(actors, phases[pi], polishMetalsForIbl && phases[pi] == RenderCfg::IblReflections);
        std::printf("ClassicScenes[%s] phase=%s\n", sceneTag, cfgName(phases[pi]));

        const int phaseStart = (totalFrames * pi) / nPhases;
        const int phaseEnd = (totalFrames * (pi + 1)) / nPhases;
        for (int f = phaseStart; f < phaseEnd; ++f) {
            const float tPath = (totalFrames <= 1) ? 0.f : float(f) / float(totalFrames - 1);
            const float legF = tPath * float(legs);
            const int leg = std::min(legs - 1, int(legF));
            const float u = legF - float(leg);
            applyCam(actors.cam, lerpKey(path[size_t(leg)], path[size_t(leg + 1)], u));

            // Mild yaw on chart spheres so IBL / lighting reads in motion.
            if (std::string(sceneTag) == "pbr_chart") {
                for (auto *e : actors.ents) e->transform()->yaw = float(frame) * 0.03f;
            }

            RenderSystem3D::render(*gfx);
            RenderSystem::render(*gfx);
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT) break;
            }
            SDL_Delay(16);
            ++frame;
        }

        warmPresent(gfx);
        const float L = meanLuma(gfx);
        phaseLuma.push_back(L);
        saveFramePng(gfx, std::string(sceneTag) + "_" + cfgName(phases[pi]) + ".png");
        std::printf("ClassicScenes[%s] phase=%s meanLuma=%.4f\n", sceneTag, cfgName(phases[pi]), L);
    }
    return phaseLuma;
}

void expectLightingResponse(const std::vector<float> &L) {
    REQUIRE(L.size() >= 5);
    // Directional lit should brighten vs ambient-only.
    CHECK(L[1] > L[0] + 0.01f);
    // Multi-point and IBL should produce non-black frames.
    CHECK(L[3] > 0.02f);
    CHECK(L[4] > 0.02f);
}

}  // namespace

TEST_CASE("ClassicScenes.cornell.flythroughConfigs") {
    const std::string dir = pathBesideThisSource("assets/classic/cornell");
    const std::string obj = "CornellBox-Original.obj";
    REQUIRE(fileExists(dir + "/" + obj));

    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx);
    resetScene3D();

    auto *md = loadModelFromDir(dir, obj);
    REQUIRE(md != nullptr);

    SceneActors actors;
    actors.bounds = boundsOf(md);
    REQUIRE(actors.bounds.valid);
    REQUIRE(spawnModel(gfx, md, dir, actors, 0.02f, 0.75f) >= 1);

    actors.cam = Camera3D::createCamera();
    actors.cam->data()->nearZ = 0.05f;
    actors.cam->data()->farZ = 50.f;
    actors.env = makeStudioCubemap(gfx);
    setupLights(actors, actors.bounds);
    // Ceiling-ish sun for the box.
    actors.sun->setDirection(0.15f, 1.f, 0.05f);
    actors.pointA->setPosition(actors.bounds.centerX(), actors.bounds.maxY - 0.15f,
                               actors.bounds.centerZ());
    actors.pointA->setColor(1.f, 1.f, 0.95f, 6.f);
    actors.pointA->setRadius(4.f);

    auto path = makeCornellPath(actors.bounds);
    auto L = flyThroughConfigs(gfx, actors, path, "cornell", /*polishMetalsForIbl=*/true);
    expectLightingResponse(L);

    // Red vs green walls should remain distinguishable under directional light.
    applyConfig(actors, RenderCfg::DirectionalLit, false);
    applyCam(actors.cam, path[0]);
    warmPresent(gfx);
    const int w = gfx->getWidth();
    const int h = gfx->getHeight();
    const Color left = gfx->getPixel(w / 5, h / 2);
    const Color right = gfx->getPixel((w * 4) / 5, h / 2);
    CHECK(left.r + 0.02f > left.g);   // left wall tends red
    CHECK(right.g + 0.01f > right.r); // right wall tends green

    delete md;
    win->close();
}

TEST_CASE("ClassicScenes.pbrChart.flythroughConfigs") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx);
    resetScene3D();

    SceneActors actors;
    spawnMetalRoughnessChart(gfx, actors);
    REQUIRE(actors.ents.size() == 16);
    REQUIRE(actors.bounds.valid);

    actors.cam = Camera3D::createCamera();
    actors.cam->data()->nearZ = 0.05f;
    actors.cam->data()->farZ = 40.f;
    actors.env = makeStudioCubemap(gfx);
    setupLights(actors, actors.bounds);

    auto path = makeOrbitPath(actors.bounds, 0.15f, 2.4f);
    auto L = flyThroughConfigs(gfx, actors, path, "pbr_chart", /*polishMetalsForIbl=*/false);
    expectLightingResponse(L);

    // IBL phase should brighten metals relative to ambient-only.
    CHECK(L[4] > L[0] + 0.01f);

    win->close();
}

TEST_CASE("ClassicScenes.sponza.flythroughConfigs") {
    const std::string dir = pathBesideThisSource("assets/classic/sponza/glTF");
    const std::string gltf = "Sponza.gltf";
    if (!fileExists(dir + "/" + gltf)) {
        std::printf(
            "ClassicScenes.sponza: missing %s — run scripts/download_classic_scenes.sh "
            "(or cmake --build --target download_classic_scenes)\n",
            (dir + "/" + gltf).c_str());
        return;
    }

    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 960, 540);
    resetScene3D();

    auto *md = loadModelFromDir(dir, gltf);
    REQUIRE(md != nullptr);
    REQUIRE(md->getMeshCount() >= 10);

    SceneActors actors;
    actors.bounds = boundsOf(md);
    REQUIRE(actors.bounds.valid);
    REQUIRE(spawnModel(gfx, md, dir, actors, 0.05f, 0.65f) >= 10);

    actors.cam = Camera3D::createCamera();
    const float rad = std::max(1.f, actors.bounds.radius());
    actors.cam->data()->nearZ = std::max(0.05f, rad * 0.002f);
    actors.cam->data()->farZ = std::max(200.f, rad * 25.f);
    actors.env = makeStudioCubemap(gfx);
    setupLights(actors, actors.bounds);
    actors.sun->setDirection(0.35f, 1.f, 0.15f);

    auto path = makeSponzaPath(actors.bounds);
    auto L = flyThroughConfigs(gfx, actors, path, "sponza", /*polishMetalsForIbl=*/false,
                               /*framesPerLeg=*/28);
    expectLightingResponse(L);

    // Shadows should not black out the whole atrium vs plain directional.
    CHECK(L[2] > 0.02f);
    CHECK(std::abs(L[2] - L[1]) < 0.45f);

    delete md;
    win->close();
}

TEST_CASE("ClassicScenes.damagedHelmet.flythroughConfigs") {
    const std::string dir = pathBesideThisSource("assets/classic/damaged_helmet/glTF");
    const std::string gltf = "DamagedHelmet.gltf";
    if (!fileExists(dir + "/" + gltf)) {
        std::printf(
            "ClassicScenes.damagedHelmet: missing %s — run scripts/download_classic_scenes.sh\n",
            (dir + "/" + gltf).c_str());
        return;
    }

    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 800, 600);
    resetScene3D();

    auto *md = loadModelFromDir(dir, gltf);
    REQUIRE(md != nullptr);

    SceneActors actors;
    actors.bounds = boundsOf(md);
    REQUIRE(actors.bounds.valid);
    REQUIRE(spawnModel(gfx, md, dir, actors, 0.9f, 0.35f) >= 1);

    actors.cam = Camera3D::createCamera();
    actors.cam->data()->nearZ = 0.05f;
    actors.cam->data()->farZ = 40.f;
    actors.env = makeStudioCubemap(gfx);
    setupLights(actors, actors.bounds);

    auto path = makeOrbitPath(actors.bounds, 0.25f, 2.1f);
    auto L = flyThroughConfigs(gfx, actors, path, "helmet", /*polishMetalsForIbl=*/false);
    expectLightingResponse(L);
    CHECK(L[4] > L[0] + 0.01f);

    delete md;
    win->close();
}
