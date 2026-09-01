#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cstdint>
#include <vector>

#include "common/Diagnostic.h"
#include "graphics/Graphics.h"
#include "graphics/Shader.h"
#include "graphics/shaders/custom2d_frag_spv.inc"
#include "window/Window.h"

using eve::DiagnosticCode;
using eve::graphics::Graphics;
using eve::graphics::Shader;

namespace {

Graphics *initializedGraphics() {
    auto *window = eve::window::Window::create();
    auto *graphics = Graphics::create();
    eve::window::WindowSettings settings;
    settings.width = 160;
    settings.height = 120;
    settings.centered = true;
    if (!window || !graphics || !window->setWindowSettings(settings)) return nullptr;
    return graphics;
}

std::vector<uint32_t> customFragment() {
    return {custom2d_frag_spv, custom2d_frag_spv + custom2d_frag_spv_count};
}

}  // namespace

TEST_CASE("graphics.shaderReload.successKeepsStableFacade") {
    Graphics *graphics = initializedGraphics();
    REQUIRE(graphics != nullptr);
    auto fragment = customFragment();
    Shader *shader = graphics->newShaderFromSpv({}, fragment);
    REQUIRE(shader != nullptr);
    void *const originalGpuHandle = shader->gpuHandle;
    shader->declareFloat("factor");
    shader->sendFloat("factor", 0.35f);

    auto replaced = graphics->replaceShaderFromSpv(*shader, {}, fragment);
    REQUIRE(replaced.ok());
    CHECK(shader->gpuHandle == originalGpuHandle);
    CHECK(shader->hasUniform("factor"));
    float factor = 0.f;
    REQUIRE_EQ(shader->getFromVar("factor", &factor, sizeof(factor)), int(sizeof(factor)));
    CHECK_EQ(factor, 0.35f);
    CHECK_EQ(shader->fragmentSpirv(), fragment);
}

TEST_CASE("graphics.shaderReload.failureKeepsLastGoodPipeline") {
    Graphics *graphics = initializedGraphics();
    REQUIRE(graphics != nullptr);
    auto fragment = customFragment();
    Shader *shader = graphics->newShaderFromSpv({}, fragment);
    REQUIRE(shader != nullptr);
    void *const originalGpuHandle = shader->gpuHandle;
    const auto originalFragment = shader->fragmentSpirv();

    std::vector<uint32_t> invalid = {0xDEADBEEF, 1, 2, 3};
    auto replaced = graphics->replaceShaderFromSpv(*shader, {}, invalid);
    REQUIRE(!replaced.ok());
    REQUIRE(!replaced.diagnostics().empty());
    CHECK_EQ(replaced.diagnostics().front().code(), DiagnosticCode::ParseError);
    CHECK(shader->gpuHandle == originalGpuHandle);
    CHECK_EQ(shader->fragmentSpirv(), originalFragment);
}

TEST_CASE("graphics.shaderReload.rejectsForeignFacade") {
    Graphics *graphics = initializedGraphics();
    REQUIRE(graphics != nullptr);
    Shader foreign;
    auto replaced = graphics->replaceShaderFromSpv(foreign, {}, customFragment());
    REQUIRE(!replaced.ok());
    REQUIRE(!replaced.diagnostics().empty());
    CHECK_EQ(replaced.diagnostics().front().code(), DiagnosticCode::StaleHandle);
}

#if !defined(_WIN32)
TEST_CASE("graphics.shaderReload.glslFailureKeepsLastGoodPipeline") {
    Graphics *graphics = initializedGraphics();
    REQUIRE(graphics != nullptr);
    auto fragment = customFragment();
    Shader *shader = graphics->newShaderFromSpv({}, fragment);
    REQUIRE(shader != nullptr);
    void *const originalGpuHandle = shader->gpuHandle;
    const auto originalFragment = shader->fragmentSpirv();

    auto replaced = graphics->replaceShaderFromGlsl(
        *shader, {}, "#version 450\nthis is deliberately invalid GLSL\n");
    REQUIRE(!replaced.ok());
    REQUIRE(!replaced.diagnostics().empty());
    CHECK_EQ(replaced.diagnostics().front().code(), DiagnosticCode::ParseError);
    CHECK(shader->gpuHandle == originalGpuHandle);
    CHECK_EQ(shader->fragmentSpirv(), originalFragment);
}
#endif
