#include "Fixtures.h"
#include "ResourceTestSupport.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "animation/AnimClip.h"
#include "animation/AnimImporter.h"
#include "animation/AnimPlayer.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/AnimSkin.h"
#include "animation/Animation.h"

#include "common/Exception.h"
#include "data/ByteData.h"
#include "filesystem/FileData.h"
#include "filesystem/Filesystem.h"
#include "graphics/AmbientOcclusion.h"
#include "graphics/AntiAliasing.h"
#include "graphics/Canvas.h"
#include "graphics/DrawItem2D.h"
#include "graphics/Font.h"
#include "graphics/GBuffer.h"
#include "graphics/GlobalIllumination.h"
#include "graphics/Graphics.h"
#include "graphics/Grass.h"
#include "graphics/Material.h"
#include "graphics/Outline.h"
#include "graphics/Quad.h"
#include "graphics/RenderControl.h"
#include "graphics/ScreenSpaceReflection.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;
#include "graphics/Light.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "image/Image.h"
#include "image/ImageData.h"
#include "model3d/Model3D.h"
#include "model3d/ModelData.h"
#include "window/Window.h"

#include <assimp/mesh.h>
#include <assimp/scene.h>

#include <SDL2/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace eve::animation;
using namespace eve::graphics;

namespace {

#include "PathBesideSource.h"
EVE_DEFINE_PATH_BESIDE_SOURCE()

[[maybe_unused]] bool fileExists(const std::string &path) {
    return std::filesystem::is_regular_file(path);
}

[[maybe_unused]] std::string cesiumManDir() {
    return pathBesideThisSource("assets/skinned/cesium_man/glTF");
}

[[maybe_unused]] std::string cesiumManGltfPath() { return cesiumManDir() + "/CesiumMan.gltf"; }

[[maybe_unused]] bool ensureSkinnedAssets() {
    const std::string gltf = cesiumManGltfPath();
    if (fileExists(gltf)) return true;
    std::printf(
        "animation.skinned: missing %s — run scripts/download_skinned_character.sh "
        "(or cmake --build --target download_skinned_character / "
        "-DEVENGINE_DOWNLOAD_SKINNED_CHARACTER=ON)\n",
        gltf.c_str());
    return false;
}

/** Mount the CesiumMan glTF folder in physfs and load via relative path. */
[[maybe_unused]] eve::model3d::ModelData *loadCesiumMan(const char *fsIdentity) {
    const std::string dir = cesiumManDir();
    REQUIRE(fileExists(dir + "/CesiumMan.gltf"));
    auto *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs != nullptr);
    REQUIRE(fs->setIdentity(fsIdentity, true));
    REQUIRE(fs->setupWriteDirectory());
    fs->allowMountingForPath(dir);
    REQUIRE(fs->mount(dir, "", false));
    auto *mod = eve::model3d::Model3D::create();
    return mod->newModelDataFromFile("CesiumMan.gltf");
}

[[maybe_unused]] int findFirstSkinnedMesh(const eve::model3d::ModelData *model) {
    for (int i = 0; i < model->getMeshCount(); ++i) {
        if (model->hasBones(i)) return i;
    }
    return -1;
}

[[maybe_unused]] float vertexDeltaMax(const std::vector<float> &a, const std::vector<float> &b) {
    const size_t n = std::min(a.size(), b.size());
    float maxd     = 0.f;
    for (size_t i = 0; i < n; ++i) {
        maxd = std::max(maxd, std::fabs(a[i] - b[i]));
    }
    return maxd;
}

// ---- skinned render helpers ------------------------------------------------

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


[[maybe_unused]] void resetScene3D() {
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

[[maybe_unused]] float luma(const Color &c) { return (c.r + c.g + c.b) / 3.f; }

[[maybe_unused]] std::vector<float> captureLumaGrid(Graphics *gfx, int step = 2) {
    const int w = gfx->getWidth();
    const int h = gfx->getHeight();
    std::vector<float> out(size_t(w) * size_t(h), 0.f);
    for (int y = 0; y < h; y += step) {
        for (int x = 0; x < w; x += step) {
            out[size_t(y) * size_t(w) + size_t(x)] = luma(gfx->getPixel(x, y));
        }
    }
    return out;
}

[[maybe_unused]] float maxLumaAbsDelta(Graphics *gfx, const std::vector<float> &a, const std::vector<float> &b,
                      int step = 2) {
    float best = -1.f;
    const int w = gfx->getWidth();
    const int h = gfx->getHeight();
    for (int y = 0; y < h; y += step) {
        for (int x = 0; x < w; x += step) {
            const size_t i = size_t(y * w + x);
            best = std::max(best, std::fabs(a[i] - b[i]));
        }
    }
    return best;
}

[[maybe_unused]] int foregroundPixelCount(const std::vector<float> &L, float threshold) {
    int count = 0;
    for (float v : L)
        if (v > threshold) ++count;
    return count;
}

[[maybe_unused]] std::vector<char> readBinaryFile(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    return std::vector<char>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

/** CesiumMan's glTF base-color map (jpg beside the .gltf). */
[[maybe_unused]] Texture *loadCesiumManDiffuseTexture(Graphics *gfx) {
    const std::string path = cesiumManDir() + "/CesiumMan_img0.jpg";
    const auto bytes = readBinaryFile(path);
    if (bytes.empty()) return nullptr;
    [[maybe_unused]] auto *const imageModule = eve::image::Image::create();
    try {
        eve::data::ByteData data(bytes.data(), bytes.size());
        eve::image::ImageData *img = eve::image::Image::create()->newImageData(&data);
        if (!img) return nullptr;
        Texture *tex = gfx->newTexture(img);
        delete img;
        return tex;
    } catch (...) {
        return nullptr;
    }
}

[[maybe_unused]] Texture *makeSolidGray(Graphics *gfx, uint8_t g) {
    const uint8_t px[4] = {g, g, g, 255};
    return gfx->newTexture(1, 1, px);
}

[[maybe_unused]] void savePng(Graphics *gfx, const char *pngName) {
    [[maybe_unused]] auto *const imageModule = eve::image::Image::create();
    eve::image::ImageData *frame = gfx->newImageData();
    REQUIRE(frame != nullptr);
    eve::filesystem::FileData *png =
        frame->encode(medialoader::FormatHandler::ENCODED_PNG, pngName, false);
    REQUIRE(png != nullptr);
    REQUIRE(png->getSize() > 0);

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
    std::printf("animation.skinned render saved: %s\n", outPath.c_str());
    delete png;
    delete frame;
}

enum class SkinnedRenderDriver {
    bindPose,
    clipSample,
    player,
};

/**
 * Open a window, CPU-skin the CesiumMan mesh every frame with the pose produced
 * by `driver`, upload it, render, and assert the character is visible. The last
 * frame is saved as a PNG next to the other test outputs. `frameCount` frames
 * are rendered at ~16ms each so the animation is actually on screen.
 */
[[maybe_unused]] void renderSkinnedAnimation(eve::model3d::ModelData *model, const char *pngName,
                            SkinnedRenderDriver driver, int frameCount) {
    REQUIRE(model != nullptr);
    REQUIRE(pngName != nullptr);
    REQUIRE(frameCount >= 1);

    const int meshIndex = findFirstSkinnedMesh(model);
    REQUIRE(meshIndex >= 0);
    const aiMesh *ai = model->getMesh(meshIndex);
    REQUIRE(ai != nullptr);
    REQUIRE(ai->mNumVertices > 0u);
    REQUIRE(ai->mNumFaces > 0u);
    const int vertexCount = static_cast<int>(ai->mNumVertices);

    std::unique_ptr<AnimSkeleton> skeleton(AnimImporter::loadSkeletonFromModel(model));
    REQUIRE(skeleton.get() != nullptr);
    std::unique_ptr<AnimSkin> skin(AnimSkin::fromModel(model, meshIndex, skeleton.get()));
    REQUIRE(skin.get() != nullptr);
    std::unique_ptr<AnimClip> clip(AnimImporter::loadClipFromModel(model, skeleton.get(), 0));
    REQUIRE(clip.get() != nullptr);
    CHECK(clip->getDuration() > 0.1f);

    std::vector<float> nrm;
    if (ai->mNormals) {
        nrm.reserve(size_t(vertexCount) * 3u);
        for (int v = 0; v < vertexCount; ++v) {
            nrm.push_back(ai->mNormals[v].x);
            nrm.push_back(ai->mNormals[v].y);
            nrm.push_back(ai->mNormals[v].z);
        }
    }
    std::vector<float> uv;
    if (ai->mTextureCoords[0]) {
        uv.reserve(size_t(vertexCount) * 2u);
        for (int v = 0; v < vertexCount; ++v) {
            uv.push_back(ai->mTextureCoords[0][v].x);
            uv.push_back(ai->mTextureCoords[0][v].y);
        }
    }
    std::vector<uint32_t> indices;
    indices.reserve(size_t(ai->mNumFaces) * 3u);
    for (unsigned f = 0; f < ai->mNumFaces; ++f) {
        const aiFace &face = ai->mFaces[f];
        REQUIRE(face.mNumIndices == 3u);
        for (unsigned k = 0; k < 3u; ++k) indices.push_back(face.mIndices[k]);
    }
    const int indexCount = static_cast<int>(indices.size());

    Bounds b;
    for (int v = 0; v < vertexCount; ++v)
        b.expand(ai->mVertices[v].x, ai->mVertices[v].y, ai->mVertices[v].z);
    REQUIRE(b.valid);
    const float cx = b.centerX(), cy = b.centerY(), cz = b.centerZ();
    const float rad = std::max(0.5f, b.radius());

    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx);
    resetScene3D();

    Texture *diffuse = loadCesiumManDiffuseTexture(gfx);
    Texture *tex = diffuse ? diffuse : makeSolidGray(gfx, 200);

    auto *ent = Renderable3D::create();
    ent->setTexture(tex);
    ent->setTint(1.f, 1.f, 1.f, 1.f);
    ent->meshRenderer()->visible = true;
    ent->meshRenderer()->castShadow = false;
    ent->meshRenderer()->receiveShadow = false;

    auto *cam = Camera3D::createCamera();
    cam->setTarget(cx, cy, cz);
    cam->setEye(cx + rad * 0.9f, cy + rad * 0.55f, cz + rad * 1.8f);
    cam->data()->nearZ = std::max(0.05f, rad * 0.01f);
    cam->data()->farZ = std::max(100.f, rad * 20.f);
    cam->setAmbient(0.15f, 0.15f, 0.15f);

    // Light rays travel from the camera toward the subject (RenderSystem3D legacy).
    RenderSystem3D::setDirectionalLight(cx - (cx + rad * 0.9f), cy - (cy + rad * 0.55f) + rad * 1.2f,
                                        cz - (cz + rad * 1.8f), 1.8f, 1.8f, 1.8f);

    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.02f, 0.02f, 0.03f, 1.f));

    std::unique_ptr<AnimPlayer> player;
    if (driver == SkinnedRenderDriver::player) {
        player.reset(new AnimPlayer(skeleton.get()));
        player->play(clip.get());
    }

    AnimPose pose;
    std::vector<float> firstLuma;
    float maxDelta = -1.f;
    bool first = true;

    std::vector<float> bindPos(static_cast<size_t>(vertexCount) * 3u);
    for (int v = 0; v < vertexCount; ++v) {
        bindPos[static_cast<size_t>(v) * 3u] = ai->mVertices[v].x;
        bindPos[static_cast<size_t>(v) * 3u + 1u] = ai->mVertices[v].y;
        bindPos[static_cast<size_t>(v) * 3u + 2u] = ai->mVertices[v].z;
    }
    Mesh *mesh = gfx->newMeshFromArrays(bindPos.data(), nrm.empty() ? nullptr : nrm.data(),
                                        uv.empty() ? nullptr : uv.data(), vertexCount, indices.data(), indexCount);
    REQUIRE(mesh != nullptr);
    REQUIRE(skin->bindGpuMesh(gfx, mesh));
    ent->meshRenderer()->mesh = mesh;

    for (int i = 0; i < frameCount; ++i) {
        AnimPose *usedPose = nullptr;
        switch (driver) {
        case SkinnedRenderDriver::bindPose:
            skeleton->applyBindPose(&pose);
            pose.computeWorld(skeleton.get());
            usedPose = &pose;
            break;
        case SkinnedRenderDriver::clipSample: {
            const float t = frameCount > 1
                                ? clip->getDuration() * float(i) / float(frameCount - 1)
                                : 0.f;
            clip->sample(t, &pose, skeleton.get());
            pose.computeWorld(skeleton.get());
            usedPose = &pose;
            break;
        }
        case SkinnedRenderDriver::player:
            player->update(1.f / 30.f);
            usedPose = player->getPose();
            REQUIRE(usedPose != nullptr);
            usedPose->computeWorld(skeleton.get());
            break;
        }

        REQUIRE(skin->updateGpuMesh(mesh, usedPose));

        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                win->close();
                return;
            }
        }
        SDL_Delay(16);

        const auto L = captureLumaGrid(gfx);
        if (first) {
            firstLuma = L;
            first = false;
        } else {
            maxDelta = std::max(maxDelta, maxLumaAbsDelta(gfx, firstLuma, L));
        }
    }

    const auto last = captureLumaGrid(gfx);
    CHECK(foregroundPixelCount(last, 0.15f) > 20);
    if (frameCount > 1 && driver != SkinnedRenderDriver::bindPose) CHECK(maxDelta > 0.01f);
    savePng(gfx, pngName);
    win->close();
}

}  // namespace

TEST_CASE("animation.skinned.mat4FromTRSIdentity") {
    TransformTRS t = TransformTRS::identity();
    Mat4 m         = Mat4::fromTRS(t);
    CHECK(std::fabs(m.m[0] - 1.f) < 1e-5f);
    CHECK(std::fabs(m.m[5] - 1.f) < 1e-5f);
    CHECK(std::fabs(m.m[10] - 1.f) < 1e-5f);
    CHECK(std::fabs(m.m[15] - 1.f) < 1e-5f);
    float ox, oy, oz;
    m.transformPoint(1.f, 2.f, 3.f, ox, oy, oz);
    CHECK(std::fabs(ox - 1.f) < 1e-5f);
    CHECK(std::fabs(oy - 2.f) < 1e-5f);
    CHECK(std::fabs(oz - 3.f) < 1e-5f);
}

TEST_CASE("animation.skinned.gpuPaletteOwnsAndValidatesMatrices") {
    Mesh mesh;
    std::array<float, 32> matrices{};
    matrices[0] = matrices[5] = matrices[10] = matrices[15] = 1.f;
    matrices[16] = matrices[21] = matrices[26] = matrices[31] = 1.f;
    matrices[28] = 3.5f;
    CHECK(mesh.setSkinPalette(matrices.data(), 2).ok());
    CHECK(mesh.getSkinPaletteCount() == 2);
    CHECK(std::fabs(mesh.skinPalette()[28] - 3.5f) < 1e-6f);
    matrices[28] = 9.f;
    CHECK(std::fabs(mesh.skinPalette()[28] - 3.5f) < 1e-6f);
    CHECK(!mesh.setSkinPalette(nullptr, 1).ok());
    CHECK(!mesh.setSkinPalette(matrices.data(), -1).ok());
}

TEST_CASE("animation.skinned.worldMatrixMatchesTRS") {
    std::unique_ptr<AnimSkeleton> sk(new AnimSkeleton());
    const int root  = sk->addBone("root", -1);
    const int child = sk->addBone("child", root);
    sk->setBindPosition(child, 0.f, 1.f, 0.f);

    std::unique_ptr<AnimPose> pose(new AnimPose());
    sk->applyBindPose(pose.get());
    pose->setLocalPosition(root, 10.f, 0.f, 0.f);
    pose->computeWorld(sk.get());

    CHECK(std::fabs(pose->getWorldPositionX(child) - 10.f) < 1e-4f);
    CHECK(std::fabs(pose->getWorldPositionY(child) - 1.f) < 1e-4f);

    float mat[16];
    pose->getWorldMatrix(child, mat);
    CHECK(std::fabs(mat[12] - 10.f) < 1e-4f);
    CHECK(std::fabs(mat[13] - 1.f) < 1e-4f);
    CHECK(std::fabs(pose->getWorldMatrixElement(child, 12) - 10.f) < 1e-4f);
}
