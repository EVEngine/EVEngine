#include "water_scene_fixture.h"

#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/Texture.h"

#include <cmath>
#include <cstdint>

using eve::graphics::Camera3D;
using eve::graphics::Graphics;
using eve::graphics::Mesh;
using eve::graphics::Renderable3D;
using eve::graphics::Texture;

namespace {

Texture* solidTexture(Graphics* gfx, uint8_t r, uint8_t g, uint8_t b) {
    const uint8_t pixel[4] = {r, g, b, 255};
    return gfx->newTexture(1, 1, pixel);
}

Renderable3D* prop(Mesh* mesh, Texture* texture, Camera3D* camera, float x, float y, float z, float sx, float sy,
                   float sz) {
    auto* value = Renderable3D::create();
    value->setMesh(mesh);
    value->setTexture(texture);
    value->setPosition(x, y, z);
    value->setScale(sx, sy, sz);
    value->setCamera(camera);
    value->setCastShadow(false);
    value->setMetallic(0.0F);
    value->setRoughness(0.88F);
    return value;
}

}  // namespace

WaterSceneFixture createStylizedWaterScene(Graphics* gfx, Camera3D* camera) {
    WaterSceneFixture result;
    Mesh* cube     = gfx->newMeshCube(1.0F);
    Mesh* rock     = gfx->newMeshSphere(12, 7);
    Mesh* cylinder = gfx->newMeshCylinder(10, 1, true);

    Texture* sand      = solidTexture(gfx, 112, 76, 37);
    Texture* wetSand   = solidTexture(gfx, 62, 47, 28);
    Texture* grass     = solidTexture(gfx, 28, 61, 27);
    Texture* stone     = solidTexture(gfx, 48, 57, 55);
    Texture* darkStone = solidTexture(gfx, 25, 38, 41);
    Texture* wood      = solidTexture(gfx, 58, 31, 16);
    Texture* woodTop   = solidTexture(gfx, 96, 57, 27);
    Texture* reed      = solidTexture(gfx, 42, 83, 30);
    Texture* reedTip   = solidTexture(gfx, 73, 39, 20);
    Texture* cloud     = solidTexture(gfx, 208, 224, 232);

    // Low-poly cloud banks echo the cloud shapes baked into the reflection cubemap.
    const float cloudPuffs[][6] = {
        {-5.8F, 2.8F, -11.5F, 2.2F, 0.72F, 0.85F}, {-4.1F, 3.1F, -11.8F, 1.7F, 0.95F, 0.92F},
        {-2.5F, 2.75F, -11.3F, 2.0F, 0.68F, 0.82F}, {3.5F, 3.4F, -13.0F, 2.5F, 0.82F, 1.0F},
        {5.5F, 3.65F, -13.4F, 1.8F, 1.05F, 0.9F}, {7.0F, 3.3F, -12.8F, 2.1F, 0.70F, 0.85F},
    };
    for (const auto& puff : cloudPuffs)
        prop(rock, cloud, camera, puff[0], puff[1], puff[2], puff[3], puff[4], puff[5]);

    // Layered floor and banks form a readable shallow-to-deep silhouette.
    prop(cube, sand, camera, 0.0F, -1.75F, 0.0F, 15.0F, 0.5F, 15.0F);
    prop(cube, wetSand, camera, -5.35F, -0.68F, -0.8F, 4.1F, 1.45F, 14.0F);
    prop(cube, grass, camera, -6.25F, 0.04F, -0.8F, 2.5F, 0.20F, 14.0F);
    prop(cube, wetSand, camera, 3.7F, -1.05F, -5.7F, 7.0F, 1.1F, 2.6F);

    // Overlapping earth and stones soften the otherwise rectangular bank edge.
    const float bankLobes[][6] = {
        {-3.72F, -0.40F, 4.7F, 1.85F, 0.52F, 1.75F}, {-3.36F, -0.43F, 3.0F, 1.25F, 0.46F, 1.40F},
        {-3.68F, -0.38F, 1.0F, 1.70F, 0.52F, 1.55F}, {-3.30F, -0.46F, -0.8F, 1.18F, 0.42F, 1.30F},
        {-3.70F, -0.40F, -2.7F, 1.62F, 0.50F, 1.60F}, {-3.42F, -0.45F, -4.6F, 1.35F, 0.44F, 1.50F},
    };
    for (const auto& lobe : bankLobes)
        prop(rock, wetSand, camera, lobe[0], lobe[1], lobe[2], lobe[3], lobe[4], lobe[5]);

    const float exposedRocks[][7] = {
        {-4.15F, 0.00F, 3.1F, 0.72F, 0.52F, 0.66F, 17.0F},
        {-3.55F, -0.15F, 1.1F, 0.46F, 0.32F, 0.56F, -11.0F},
        {-4.18F, 0.04F, -2.0F, 0.88F, 0.62F, 0.70F, 31.0F},
        {3.0F, -0.08F, -5.0F, 0.62F, 0.44F, 0.52F, -23.0F},
    };
    for (const auto& r : exposedRocks) {
        auto* value = prop(rock, stone, camera, r[0], r[1], r[2], r[3], r[4], r[5]);
        value->setRotation(r[6], r[6] * 0.37F, 0.0F);
    }

    const float waterlinePebbles[][6] = {
        {-3.05F, -0.34F, 3.0F, 0.42F, 0.28F, 0.50F}, {-2.92F, -0.42F, 2.35F, 0.28F, 0.20F, 0.34F},
        {-3.15F, -0.38F, 0.05F, 0.36F, 0.24F, 0.42F}, {-2.88F, -0.48F, -1.8F, 0.52F, 0.30F, 0.38F},
        {-3.08F, -0.40F, -2.55F, 0.30F, 0.22F, 0.35F},
    };
    for (const auto& pebble : waterlinePebbles)
        prop(rock, stone, camera, pebble[0], pebble[1], pebble[2], pebble[3], pebble[4], pebble[5]);

    const float submergedRocks[][6] = {
        {-1.7F, -0.28F, 1.7F, 0.70F, 0.30F, 0.56F}, {0.0F, -0.48F, 2.8F, 0.48F, 0.28F, 0.65F},
        {2.1F, -0.36F, 1.0F, 0.82F, 0.32F, 0.70F},  {3.2F, -1.22F, 3.5F, 0.55F, 0.38F, 0.46F},
        {-0.2F, -0.58F, -2.5F, 0.62F, 0.34F, 0.58F},
    };
    for (const auto& r : submergedRocks)
        prop(rock, darkStone, camera, r[0], r[1], r[2], r[3], r[4], r[5]);

    // A broken jetty gives the posts a readable purpose and a strong diagonal.
    for (int i = 0; i < 7; ++i) {
        const float x = -2.75F + static_cast<float>(i) * 0.62F;
        auto* plank = prop(cube, i % 3 == 0 ? woodTop : wood, camera, x, 0.18F + 0.025F * float(i % 2), -2.55F,
                           0.54F, 0.13F, 1.25F);
        plank->setRotation(0.0F, float((i % 3) - 1) * 2.2F, float((i % 2) ? 1 : -1));
    }
    for (int i = 0; i < 4; ++i) {
        const float x = -2.55F + static_cast<float>(i) * 1.18F;
        const float z = -2.55F + (i % 2 ? 0.72F : -0.72F);
        auto* post = prop(cylinder, wood, camera, x, -0.12F, z, 0.14F, 0.86F + 0.08F * float(i % 2), 0.14F);
        post->setRotation(float(i - 2) * 1.8F, 0.0F, float(i % 2 ? 2 : -2));
    }

    // Reeds grow in small irregular clumps along the wet bank.
    for (int i = 0; i < 30; ++i) {
        const float cluster = static_cast<float>(i / 6);
        const float strand  = static_cast<float>(i % 6);
        const float x       = -3.48F - 0.12F * strand + 0.09F * std::sin(float(i) * 2.3F);
        const float z       = -4.55F + cluster * 2.02F + 0.28F * std::sin(float(i) * 1.7F);
        const float h       = 0.78F + 0.24F * std::sin(float(i) * 2.1F);
        auto* stem = prop(cylinder, reed, camera, x, h * 0.5F - 0.04F, z, 0.032F, h, 0.032F);
        stem->setRotation(float((i % 5) - 2) * 2.5F, 0.0F, float((i % 3) - 1) * 2.0F);
        prop(cylinder, reedTip, camera, x, h + 0.05F, z, 0.055F, 0.18F, 0.055F);
    }

    auto* log = prop(cylinder, wood, camera, 0.55F, 0.05F, 1.15F, 0.17F, 0.92F, 0.17F);
    log->setRotation(2.0F, 0.0F, 88.0F);
    result.floaters.push_back({log, 0.55F, 0.05F, 1.15F, 0.2F, 88.0F});

    auto* plank = prop(cube, woodTop, camera, 1.75F, 0.07F, 0.15F, 1.05F, 0.10F, 0.34F);
    plank->setRotation(-13.0F, 0.0F, 2.0F);
    result.floaters.push_back({plank, 1.75F, 0.07F, 0.15F, 1.6F, 2.0F});
    return result;
}

void WaterSceneFixture::update(float time) {
    for (auto& floater : floaters) {
        const float bob = std::sin(time * 1.35F + floater.phase);
        floater.renderable->setPosition(floater.x, floater.y + bob * 0.045F, floater.z);
        floater.renderable->setRotation(std::sin(time * 0.42F + floater.phase) * 4.0F, 0.0F,
                                        floater.roll + bob * 2.2F);
    }
}
