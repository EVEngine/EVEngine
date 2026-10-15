#include <array>
#include <memory>
#include <vector>
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "image/ImageData.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

TEST_CASE("graphics.skinPalette.largeJointMovesRenderedPixels") {
    using namespace eve::graphics;
    auto* gfx = Graphics::create();
    REQUIRE(gfx != nullptr);
    gfx->initHeadless(128, 128);
    Canvas* canvas = gfx->newCanvas(128, 128);
    REQUIRE(canvas != nullptr);
    const float    positions[] = {-0.2f, -0.2f, 0.5f, 0.2f, -0.2f, 0.5f, 0.f, 0.2f, 0.5f};
    const uint32_t indices[]   = {0, 1, 2, 2, 1, 0};
    auto*          mesh        = gfx->newMeshFromArrays(positions, nullptr, nullptr, 3, indices, 6);
    REQUIRE(mesh != nullptr);
    const float weights[] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
    gfx->setMesh3DViewProj(glm::mat4(1.f));
    std::vector<unsigned char> first;
    for (int count : {129, 204, 1024, 4096, 204}) {
        const uint16_t last     = static_cast<uint16_t>(count - 1);
        const uint16_t joints[] = {last, 0, 0, 0, last, 0, 0, 0, last, 0, 0, 0};
        REQUIRE(gfx->setMeshSkinningData(mesh, joints, weights, 3));
        std::vector<float> palette(size_t(count) * 16, 0.f);
        for (int i = 0; i < count; ++i)
            for (int j : {0, 5, 10, 15}) palette[size_t(i) * 16 + j] = 1.f;
        for (float translation : {-0.5f, 0.5f}) {
            palette[size_t(count - 1) * 16 + 12] = translation;
            REQUIRE(mesh->setSkinPalette(palette.data(), count).ok());
            gfx->begin3DFrameToCanvas(canvas);
            gfx->drawMesh(mesh, glm::mat4(1.f), nullptr, Color(1.f, 0.2f, 0.1f, 1.f));
            gfx->end3DFrameToCanvas();
            std::unique_ptr<eve::image::ImageData> pixels(canvas->newImageData());
            REQUIRE(pixels != nullptr);
            const auto*  data = static_cast<const unsigned char*>(pixels->getData());
            const size_t size = 128u * 128u * 4u;
            if (translation < 0)
                first.assign(data, data + size);
            else {
                size_t changed = 0;
                for (size_t i = 0; i < size; i += 4)
                    if (data[i] != first[i] || data[i + 1] != first[i + 1] || data[i + 2] != first[i + 2]) ++changed;
                REQUIRE(changed > 200);
            }
        }
    }
}

TEST_CASE("graphics.skinPalette.influenceLimitChangesRenderedDeformation") {
    using namespace eve::graphics;
    auto* gfx = Graphics::create();
    REQUIRE(gfx != nullptr);
    gfx->initHeadless(128, 128);
    Canvas* canvas = gfx->newCanvas(128, 128);
    REQUIRE(canvas != nullptr);
    const float positions[] = {-0.15f, -0.15f, 0.5f, 0.15f, -0.15f, 0.5f, 0.f, 0.15f, 0.5f};
    const uint32_t indices[] = {0, 1, 2, 2, 1, 0};
    Mesh* mesh = gfx->newMeshFromArrays(positions, nullptr, nullptr, 3, indices, 6);
    REQUIRE(mesh != nullptr);
    const uint16_t joints[] = {0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0};
    const float weights[] = {.75f, .25f, 0.f, 0.f, .75f, .25f, 0.f, 0.f,
                             .75f, .25f, 0.f, 0.f};
    REQUIRE(gfx->setMeshSkinningData(mesh, joints, weights, 3));
    std::array<float, 32> palette{};
    for (int bone = 0; bone < 2; ++bone)
        for (int diagonal : {0, 5, 10, 15}) palette[static_cast<size_t>(bone) * 16 + diagonal] = 1.f;
    palette[12] = -.45f;
    palette[16 + 12] = .45f;
    REQUIRE(mesh->setSkinPalette(palette.data(), 2).ok());
    gfx->setMesh3DViewProj(glm::mat4(1.f));

    std::vector<unsigned char> oneInfluence;
    for (SkinInfluenceLimit limit : {SkinInfluenceLimit::One, SkinInfluenceLimit::Two}) {
        gfx->setMesh3DSkinInfluenceLimit(limit);
        gfx->begin3DFrameToCanvas(canvas);
        gfx->drawMesh(mesh, glm::mat4(1.f), nullptr, Color(1.f, .2f, .1f, 1.f));
        gfx->end3DFrameToCanvas();
        std::unique_ptr<eve::image::ImageData> pixels(canvas->newImageData());
        REQUIRE(pixels != nullptr);
        const auto* data = static_cast<const unsigned char*>(pixels->getData());
        if (limit == SkinInfluenceLimit::One) {
            oneInfluence.assign(data, data + 128u * 128u * 4u);
        } else {
            size_t changed = 0;
            for (size_t i = 0; i < oneInfluence.size(); i += 4)
                if (data[i] != oneInfluence[i] || data[i + 1] != oneInfluence[i + 1] ||
                    data[i + 2] != oneInfluence[i + 2])
                    ++changed;
            REQUIRE(changed > 150);
        }
    }
}
