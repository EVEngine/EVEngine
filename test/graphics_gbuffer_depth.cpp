#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include "Fixtures.h"
#include "graphics/ClipSpace.h"
#include "image/ImageData.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

TEST_CASE("graphics.gbufferPlaneDepthMatchesCameraRay") {
    eve::graphics::Graphics *gfx = nullptr;
    openHeadlessGfx(gfx, 160, 120);
    const float    positions[] = {-30, 0, -20, 30, 0, -20, 30, 0, 10, -30, 0, 10};
    const float    normals[]   = {0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0};
    const float    uv[]        = {0, 0, 1, 0, 1, 1, 0, 1};
    const uint32_t indices[]   = {0, 2, 1, 0, 3, 2};
    auto          *mesh        = gfx->newMeshFromArrays(positions, normals, uv, 4, indices, 6);
    REQUIRE(mesh != nullptr);
    const glm::vec3 eye(0, 7, 16);
    const auto      view       = glm::lookAtRH(eye, glm::vec3(0), glm::vec3(0, 1, 0));
    const auto      projection = eve::graphics::perspectiveVulkanRH_ZO(glm::radians(55.f), 160.f / 120.f, 0.1f, 100.f);
    const auto      vp         = projection * view;
    gfx->beginGBufferPass(160, 120);
    gfx->drawMeshGBuffer(mesh, glm::mat4(1.f), vp, 0.1f, 100.f);
    gfx->endGBufferPass();
    std::unique_ptr<eve::image::ImageData> depth(gfx->readGBufferToImageData("depth"));
    REQUIRE(depth != nullptr);
    const auto inverse = glm::inverse(vp);
    // Ground depth depends on the camera ray, not the triangle diagonal or
    // perspective interpolation of vertex z/w. RGBA8 readback allows 2 LSB.
    for (int y : {60, 75, 90}) {
        for (int x : {45, 80, 115}) {
            auto        farPoint  = inverse * glm::vec4((x + 0.5f) / 80.f - 1.f, (y + 0.5f) / 60.f - 1.f, 1, 1);
            const auto  direction = glm::vec3(farPoint) / farPoint.w - eye;
            const auto  hit       = eye + direction * (-eye.y / direction.y);
            const float viewDepth = -(view * glm::vec4(hit, 1)).z;
            const float expected  = (viewDepth - 0.1f) / 99.9f;
            const float actual    = depth->getPixel(x, y).r;
            CHECK(std::fabs(actual - expected) <= 2.f / 255.f);
        }
    }
}
