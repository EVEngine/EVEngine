#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Capability.h"
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/IRayTracing.h"
#include "image/Image.h"
#include "image/ImageData.h"

#include <cstdio>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include <vector>

TEST_CASE("graphics.raytracing.reflectionHitVisibility") {
    using namespace eve::graphics;
    REQUIRE(eve::image::Image::create() != nullptr);
    auto* gfx = Graphics::create();
    gfx->initHeadless(64, 64);
    auto* rt = eve::cap::query<IRayTracing>();
    if (!rt || !rt->isAvailable()) {
        std::fprintf(stderr, "RT pixel probe: hardware/provider unavailable; no GPU output assertion executed\n");
        return;
    }

    // A tilted mirror reflects an off-center sample towards a translated vertical
    // triangle. Identity projection makes projected hit coordinates observable.
    const float    positions[] = {0.f, -2.f, -2.f, 0.f, 2.f, -2.f, 0.f, 0.f, 2.f};
    const uint32_t indices[]   = {0, 1, 2};
    rt->clearScene();
    auto added =
        rt->addTriangleMesh(positions, 3, indices, 3, glm::translate(glm::mat4(1.f), glm::vec3(0.5f, 0.f, 0.f)));
    REQUIRE(added.ok());
    auto built = rt->rebuildScene();
    REQUIRE(built.ok());

    std::vector<uint8_t> scene(64 * 64 * 4, 255);
    std::vector<uint8_t> normals(64 * 64 * 4, 255);
    for (int i = 0; i < 64 * 64; ++i) {
        scene[i * 4] = scene[i * 4 + 1] = 0;
        normals[i * 4] = normals[i * 4 + 2] = 218;
        normals[i * 4 + 1]                  = 128;
        normals[i * 4 + 3]                  = 56;  // smooth metal: packed G-buffer PBR
    }
    for (int y = 16; y <= 20; ++y)
        for (int x = 46; x <= 50; ++x) {
            scene[(y * 64 + x) * 4]     = 255;
            scene[(y * 64 + x) * 4 + 2] = 0;
        }
    auto* color = gfx->newTexture(64, 64, scene.data());
    auto* dest  = gfx->newHDRCanvas(64, 64);
    for (int variant : {0, 1, 2}) {
        const bool occluded   = variant == 1;
        const bool dielectric = variant == 2;
        if (dielectric)
            for (int i = 0; i < 64 * 64; ++i) normals[i * 4 + 3] = 0;
        auto*                normal = gfx->newTexture(64, 64, normals.data());
        std::vector<uint8_t> depths(64 * 64 * 4, 255);
        depths[(18 * 64 + 21) * 4] = 64;
        for (int y = 16; y <= 20; ++y)
            for (int x = 46; x <= 50; ++x) depths[(y * 64 + x) * 4] = occluded ? 26 : 64;
        auto*       depth   = gfx->newTexture(64, 64, depths.data());
        const float centerX = 21.5f / 64.f * 2.f - 1.f;
        const float centerY = 18.5f / 64.f * 2.f - 1.f;
        auto        applied = rt->applyReflections(gfx, color, depth, normal, dest, glm::mat4(1.f), glm::mat4(1.f),
                                                   glm::vec3(centerX, centerY, 1.f));
        REQUIRE(applied.ok());
        REQUIRE(applied.code() != eve::StatusCode::NoOp);
        std::unique_ptr<eve::image::ImageData> image(dest->newHDRImageData());
        REQUIRE(image != nullptr);
        const auto pixel = image->getPixel(21, 18);
        if (occluded) {
            REQUIRE(pixel.a < 0.1f);
        } else if (dielectric) {
            REQUIRE(pixel.a > 0.02f);
            REQUIRE(pixel.a < 0.06f);
            REQUIRE(std::abs(pixel.r - pixel.a) < 0.002f);
        } else {
            REQUIRE(pixel.a > 0.9f);
            REQUIRE(pixel.r > 0.9f);
            REQUIRE(pixel.g < 0.1f);
        }
    }
    rt->clearScene();
}
