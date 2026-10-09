#include <array>
#include <cmath>
#include <memory>
#include <vector>

#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/Light.h"
#include "graphics/Mesh.h"
#include "graphics/Shader.h"
#include "image/ImageData.h"
#include "stylize/StyleShaders.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace {

constexpr int kYsaTestSize = 128;

/** @brief Draw one YSA frame into an offscreen canvas and read the pixels back. */
std::vector<unsigned char> drawYsaFrame(eve::graphics::Graphics *gfx, eve::graphics::Canvas *canvas,
                                        eve::graphics::Mesh *mesh, eve::graphics::Shader *shader,
                                        const glm::vec4 &tint) {
    gfx->begin3DFrameToCanvas(canvas);
    gfx->drawMeshShader(mesh, glm::mat4(1.f), nullptr, tint, shader);
    gfx->end3DFrameToCanvas();
    std::unique_ptr<eve::image::ImageData> pixels(canvas->newImageData());
    if (!pixels) return {};
    const auto *data = static_cast<const unsigned char *>(pixels->getData());
    const size_t size = size_t(kYsaTestSize) * size_t(kYsaTestSize) * 4u;
    return std::vector<unsigned char>(data, data + size);
}

size_t countDifferentPixels(const std::vector<unsigned char> &a, const std::vector<unsigned char> &b) {
    size_t changed = 0;
    for (size_t i = 0; i + 3 < a.size() && i + 3 < b.size(); i += 4)
        if (a[i] != b[i] || a[i + 1] != b[i + 1] || a[i + 2] != b[i + 2]) ++changed;
    return changed;
}

long totalLuma(const std::vector<unsigned char> &pixels) {
    long sum = 0;
    for (size_t i = 0; i + 3 < pixels.size(); i += 4)
        sum += long(pixels[i]) + long(pixels[i + 1]) + long(pixels[i + 2]);
    return sum;
}

}  // namespace

TEST_CASE("stylize.ysa.independentMaterialContract") {
    using namespace eve::stylize;
    REQUIRE(styleSupports("ysa", "mesh"));
    REQUIRE(!styleSupports("ysa", "post"));
    REQUIRE(!styleSupports("ysa", "cpu"));
    eve::graphics::Shader shader;
    bindMeshUniforms(&shader, "ysa");
    REQUIRE_EQ(shader.usedFloats(), styleParamCount("ysa"));
    REQUIRE(shader.usedFloats() <= int(eve::graphics::Shader::kMaxFloats));
    for (int i = 0; i < styleParamCount("ysa"); ++i) {
        const auto* parameter = styleParameterAt("ysa", i);
        REQUIRE(parameter != nullptr);
        float value = 0.f;
        REQUIRE_EQ(shader.getFromVar(parameter->id, &value, sizeof(value)), int(sizeof(value)));
        REQUIRE_EQ(value, parameter->defaultValue);
        REQUIRE(std::isfinite(value));
        REQUIRE(value >= parameter->minValue);
        REQUIRE(value <= parameter->maxValue);
    }
    // The new material must not alias the existing toon parameter layout.
    REQUIRE(findStyleParameter("ysa", "bands") == nullptr);
    REQUIRE(findStyleParameter("cartoon", "lightMidpoint") == nullptr);
    eve::graphics::Shader legacy;
    bindMeshUniforms(&legacy, "cartoon");
    float bands = 0.f;
    REQUIRE_EQ(legacy.getFromVar("bands", &bands, sizeof(bands)), int(sizeof(bands)));
    REQUIRE_EQ(bands, 4.f);
}

TEST_CASE("stylize.ysa.realGpuPipeline") {
    auto*                       window   = eve::window::Window::create();
    auto*                       graphics = eve::graphics::Graphics::create();
    eve::window::WindowSettings settings;
    settings.width  = 160;
    settings.height = 120;
    REQUIRE(window->setWindowSettings(settings));
    auto* shader = eve::stylize::createMeshShader(graphics, "ysa");
    REQUIRE(shader != nullptr);
    REQUIRE(shader->gpuHandle != nullptr);
    REQUIRE_EQ(shader->getKind(), eve::graphics::Shader::Kind::eMesh3D);
    REQUIRE(shader->hasUniform("lightMidpoint"));
    REQUIRE(shader->hasUniform("rimDynamic"));
}

// The Vulkan YSA path must run the same vertex contract as the WebGPU path
// (which uses the backend's standard mesh vertex shader): GPU skinning via the
// skin palette at set 0 binding 21, and the tint * vertexColor multiply.
TEST_CASE("stylize.ysa.skinnedMeshFollowsPalette") {
    using namespace eve::graphics;
    auto* gfx = Graphics::create();
    REQUIRE(gfx != nullptr);
    if (!gfx->isHeadless()) gfx->initHeadless(kYsaTestSize, kYsaTestSize);
    Canvas* canvas = gfx->newCanvas(kYsaTestSize, kYsaTestSize);
    REQUIRE(canvas != nullptr);
    auto* shader = eve::stylize::createMeshShader(gfx, "ysa");
    REQUIRE(shader != nullptr);

    const float    positions[] = {-0.3f, -0.3f, 0.5f, 0.3f, -0.3f, 0.5f, 0.f, 0.3f, 0.5f};
    const float    normals[]   = {0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f};
    const float    uvs[]       = {0.f, 0.f, 1.f, 0.f, 0.5f, 1.f};
    const uint32_t indices[]   = {0, 1, 2};
    auto*          mesh        = gfx->newMeshFromArrays(positions, normals, uvs, 3, indices, 3);
    REQUIRE(mesh != nullptr);
    const uint16_t joints[]  = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    const float    weights[] = {1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f};
    REQUIRE(gfx->setMeshSkinningData(mesh, joints, weights, 3));

    std::array<float, 16> palette{};
    for (int diagonal : {0, 5, 10, 15}) palette[size_t(diagonal)] = 1.f;
    gfx->setMesh3DViewProj(glm::mat4(1.f));
    Lighting3DPack lighting{};
    lighting.ambient = glm::vec4(0.5f, 0.5f, 0.5f, 0.f);
    gfx->setMesh3DLighting(lighting);

    std::vector<unsigned char> first;
    for (float translation : {-0.5f, 0.5f}) {
        palette[12] = translation;
        REQUIRE(mesh->setSkinPalette(palette.data(), 1).ok());
        std::vector<unsigned char> frame = drawYsaFrame(gfx, canvas, mesh, shader, glm::vec4(1.f));
        REQUIRE(frame.size() == size_t(kYsaTestSize) * size_t(kYsaTestSize) * 4u);
        if (translation < 0.f)
            first = std::move(frame);
        else
            REQUIRE(countDifferentPixels(first, frame) > 200);
    }
}

// Vertex color must reach the Vulkan material color: the WebGPU YSA path runs
// the standard mesh vertex shader, which forwards ubo.tint * inColor.
TEST_CASE("stylize.ysa.vertexColorFeedsMaterialColor") {
    using namespace eve::graphics;
    auto* gfx = Graphics::create();
    REQUIRE(gfx != nullptr);
    if (!gfx->isHeadless()) gfx->initHeadless(kYsaTestSize, kYsaTestSize);
    Canvas* canvas = gfx->newCanvas(kYsaTestSize, kYsaTestSize);
    REQUIRE(canvas != nullptr);
    auto* shader = eve::stylize::createMeshShader(gfx, "ysa");
    REQUIRE(shader != nullptr);

    const float    positions[] = {-0.4f, -0.4f, 0.5f, 0.4f, -0.4f, 0.5f, 0.f, 0.4f, 0.5f};
    const float    normals[]   = {0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f};
    const float    uvs[]       = {0.f, 0.f, 1.f, 0.f, 0.5f, 1.f};
    const uint32_t indices[]   = {0, 1, 2};
    const float    white[]     = {1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f};
    const float    grey[]      = {0.2f, 0.2f, 0.2f, 1.f, 0.2f, 0.2f, 0.2f, 1.f,
                                  0.2f, 0.2f, 0.2f, 1.f};
    auto*          whiteMesh   = gfx->newMeshFromArraysColored(positions, normals, uvs, white, 3, indices, 3);
    auto*          greyMesh    = gfx->newMeshFromArraysColored(positions, normals, uvs, grey, 3, indices, 3);
    REQUIRE(whiteMesh != nullptr);
    REQUIRE(greyMesh != nullptr);

    gfx->setMesh3DViewProj(glm::mat4(1.f));
    Lighting3DPack lighting{};
    lighting.ambient = glm::vec4(0.5f, 0.5f, 0.5f, 0.f);
    gfx->setMesh3DLighting(lighting);

    const std::vector<unsigned char> whiteFrame =
        drawYsaFrame(gfx, canvas, whiteMesh, shader, glm::vec4(1.f));
    const std::vector<unsigned char> greyFrame = drawYsaFrame(gfx, canvas, greyMesh, shader, glm::vec4(1.f));
    REQUIRE(whiteFrame.size() == size_t(kYsaTestSize) * size_t(kYsaTestSize) * 4u);
    REQUIRE(greyFrame.size() == whiteFrame.size());
    REQUIRE(countDifferentPixels(whiteFrame, greyFrame) > 200);
    REQUIRE(totalLuma(whiteFrame) > totalLuma(greyFrame));
}
