#pragma once

#include "Fixtures.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "procgen/heightmap/TerrainSampler.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <vector>

#include "RenderImageAudit.h"
#include "graphics/AmbientOcclusion.h"
#include "graphics/AntiAliasing.h"
#include "graphics/Canvas.h"
#include "graphics/ClipSpace.h"
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
#include "image/ImageData.h"
#include "voxel/Chunk.h"
#include "voxel/CubeTypeRegistry.h"
#include "voxel/FaceDir.h"
#include "voxel/Voxel.h"
#include "voxel/VoxelPack.h"
#include "voxel/VoxelWorld.h"
#include "window/Window.h"

#include <cstdio>
#include <filesystem>
#include <string>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

using namespace eve::graphics;
using namespace eve::voxel;
using eve::image::ImageData;

[[maybe_unused]] static float luma(const Color &c) { return (c.r + c.g + c.b) / 3.f; }

/** How many samples on a coarse grid differ from the 3D clear color. */
[[maybe_unused]] static int countNonBgSamples(Graphics *gfx, const Color &bg, float eps = 0.08f) {
    const int   w  = gfx->getWidth();
    const int   h  = gfx->getHeight();
    const float bl = luma(bg);
    int         n  = 0;
    for (int y = h / 10; y < h * 9 / 10; y += std::max(1, h / 12)) {
        for (int x = w / 10; x < w * 9 / 10; x += std::max(1, w / 12)) {
            if (std::fabs(luma(gfx->getPixel(x, y)) - bl) > eps) ++n;
        }
    }
    return n;
}


[[maybe_unused]] static Texture *makeSolid(Graphics *gfx, uint8_t r, uint8_t g, uint8_t b) {
    uint8_t px[4] = {r, g, b, 255};
    return gfx->newTexture(1, 1, px);
}

/** Deterministic high-contrast palette for atlas tile `id` (0..127). */
[[maybe_unused]] static void atlasTileRgb(int id, uint8_t &r, uint8_t &g, uint8_t &b) {
    [[maybe_unused]] static const uint8_t kPal[][3] = {
        {220, 40, 40},   {40, 220, 40},  {40, 40, 220},  {220, 220, 40}, {220, 40, 220}, {40, 220, 220},
        {240, 240, 240}, {220, 120, 40}, {120, 40, 220}, {40, 160, 120}, {200, 80, 120}, {80, 200, 80},
        {80, 80, 200},   {200, 160, 40}, {160, 40, 80},  {40, 200, 200},
    };
    const int n = int(sizeof(kPal) / sizeof(kPal[0]));
    const int i = ((id % n) + n) % n;
    r           = kPal[i][0];
    g           = kPal[i][1];
    b           = kPal[i][2];
}

/** NxM tile atlas (1px per tile). `tilesPerRow` must match draw call. */
[[maybe_unused]] static Texture *makeTileAtlas(Graphics *gfx, int tilesPerRow, int rows) {
    const int            w = std::max(1, tilesPerRow);
    const int            h = std::max(1, rows);
    std::vector<uint8_t> px(size_t(w * h * 4));
    for (int ty = 0; ty < h; ++ty) {
        for (int tx = 0; tx < w; ++tx) {
            const int id = tx + ty * w;
            uint8_t   r, g, b;
            atlasTileRgb(id, r, g, b);
            const size_t o = size_t((ty * w + tx) * 4);
            px[o + 0]      = r;
            px[o + 1]      = g;
            px[o + 2]      = b;
            px[o + 3]      = 255;
        }
    }
    return gfx->newTexture(w, h, px.data());
}

[[maybe_unused]] static void fillCube(VoxelWorld *world, int x0, int y0, int z0, int s, uint8_t tex) {
    for (int z = 0; z < s; ++z)
        for (int y = 0; y < s; ++y)
            for (int x = 0; x < s; ++x) world->setVoxel(x0 + x, y0 + y, z0 + z, tex);
}

[[maybe_unused]] static void tinyHud(Graphics *gfx) {
    (void)gfx;
    // Keep 2D present path consistent with other 3D tests.
    auto *hud              = Renderable2D::create();
    hud->transform()->x    = 0;
    hud->transform()->y    = 0;
    hud->sprite()->width   = 2;
    hud->sprite()->height  = 2;
    hud->sprite()->r       = 0.f;
    hud->sprite()->g       = 0.f;
    hud->sprite()->b       = 0.f;
    hud->sprite()->a       = 0.f;
    hud->sprite()->visible = true;
}

[[maybe_unused]] static void hideLeftover3D() {
    if (ecs::current()->getManager<Renderable3D>() != nullptr) {
        auto view = ecs::View<Renderable3D, Renderable3D::MeshRenderer>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [mr]   = *it;
            mr->visible = false;
        }
    }
    if (ecs::current()->getManager<Camera3D>() != nullptr) {
        auto camView = ecs::View<Camera3D, Camera3D::Data>();
        for (auto it = camView.begin(); it != camView.end(); ++it) {
            auto [data]  = *it;
            data->active = false;
        }
    }
}

[[maybe_unused]] static void renderVoxelFrame(Graphics *gfx, VoxelWorld *world, Texture *atlas, int tilesPerRow,
                                              const glm::vec3 &eye, const glm::vec3 &target, float viewRange,
                                              bool faceCull) {
    const float     aspect = float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::mat4 view   = glm::lookAtRH(eye, target, glm::vec3(0.f, 1.f, 0.f));
    const glm::mat4 proj   = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 500.f);
    const glm::mat4 vp     = proj * view;

    world->selectVisible(&vp[0][0], eye.x, eye.y, eye.z, viewRange, faceCull);

    gfx->setBackgroundColor(Color(0.05f, 0.06f, 0.08f, 1.f));
    gfx->begin3DFrame();
    if (!gfx->had3DThisFrame()) return;
    gfx->setMesh3DViewProj(vp);
    gfx->setMesh3DCameraPos(eye);
    world->drawVisible(gfx, atlas, tilesPerRow);
    RenderSystem::render(*gfx);  // closes / presents via 2D path
}

[[maybe_unused]] static Color renderVoxelAoSample(Graphics *gfx, Texture *atlas, uint32_t ao) {
    const uint32_t  packed = PackedRect::pack(0, 0, 0, 4, 4, 1).bits;
    const float     aspect = float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::vec3 eye(2.f, 8.f, 2.f);
    const glm::mat4 view = glm::lookAtRH(eye, glm::vec3(2.f, 0.f, 2.f), glm::vec3(0.f, 0.f, -1.f));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f);
    gfx->setBackgroundColor(Color(0.05f, 0.06f, 0.08f, 1.f));
    gfx->begin3DFrame();
    if (gfx->had3DThisFrame()) {
        gfx->setMesh3DViewProj(proj * view);
        gfx->drawVoxelFaceInstances(&packed, 1, 0.f, 0.f, 0.f, "posY", atlas, 1, &ao);
    }
    RenderSystem::render(*gfx);
    return gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
}

/** @brief Vertex AO must shade identically on Vulkan and WebGPU voxel pipelines. */
