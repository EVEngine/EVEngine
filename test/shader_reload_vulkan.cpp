#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cstdint>
#include <vector>

#include "graphics/Graphics.h"
#include "graphics/Shader.h"
#include "graphics/shaders/custom2d_frag_spv.inc"
#include "graphics/shaders/mesh3d_frag_spv.inc"
#include "graphics/vulkan/Graphics.h"
#include "window/Window.h"

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

/** The engine's default Mesh3D fragment stage; MoltenVK cannot translate the 2D one. */
std::vector<uint32_t> meshFragment() {
    return {mesh3d_frag_spv, mesh3d_frag_spv + mesh3d_frag_spv_count};
}

eve::graphics::vulkan::GpuShader *gpuShaderOf(Shader &shader) {
    return shader.gpuHandle == nullptr ? nullptr
                                       : static_cast<eve::graphics::vulkan::GpuShader *>(shader.gpuHandle);
}

}  // namespace

TEST_CASE("graphics.shaderReload.vulkan.rebuildsEveryPipelineOfTheShader") {
    // Regression guard for a stale-pipeline bug: the 2D factories build a blended and an
    // opaque pipeline per render pass, and the renderer picks the opaque one for opaque
    // batches (Graphics2D's textured batch bind). A replacement that rebuilds only the
    // blended pipelines therefore keeps drawing the previous fragment stage for opaque
    // draws, which is invisible to a replacement test that only checks the facade.
    Graphics *graphics = initializedGraphics();
    REQUIRE(graphics != nullptr);
    Shader *shader = graphics->newShaderFromSpv({}, customFragment());
    REQUIRE(shader != nullptr);

    auto *before = gpuShaderOf(*shader);
    REQUIRE(before != nullptr);
    const auto blendedSwapchain   = before->swapchainPipeline;
    const auto opaqueSwapchain    = before->swapchainOpaquePipeline;
    const auto blendedOffscreen   = before->offscreenPipeline;
    const auto opaqueOffscreen    = before->offscreenOpaquePipeline;
    // The blended swapchain pipeline always exists; the opaque one is what the renderer
    // binds for opaque batches, so it must exist too for this guard to mean anything.
    REQUIRE(blendedSwapchain);
    REQUIRE(opaqueSwapchain);

    auto replaced = graphics->replaceShaderFromSpv(*shader, {}, customFragment());
    REQUIRE(replaced.ok());

    auto *after = gpuShaderOf(*shader);
    REQUIRE(after == before);  // the facade keeps its GPU resource, only pipelines change
    REQUIRE(after->swapchainPipeline != blendedSwapchain);
    REQUIRE(after->swapchainOpaquePipeline != opaqueSwapchain);
    if (blendedOffscreen) REQUIRE(after->offscreenPipeline != blendedOffscreen);
    if (opaqueOffscreen) REQUIRE(after->offscreenOpaquePipeline != opaqueOffscreen);
}

TEST_CASE("graphics.shaderReload.vulkan.meshShaderKeepsItsSurfacePipelines") {
    // The mesh path rebuilds its own pipeline set; the 2D pipelines stay untouched there.
    // It uses the engine's own Mesh3D fragment stage: a 2D stage is not part of that
    // contract and MoltenVK refuses to translate it (SPIR-V to MSL conversion error).
    Graphics *graphics = initializedGraphics();
    REQUIRE(graphics != nullptr);
    Shader *shader = graphics->newMeshShaderFromSpv({}, meshFragment());
    REQUIRE(shader != nullptr);

    auto *before = gpuShaderOf(*shader);
    REQUIRE(before != nullptr);
    const auto meshPipeline = before->mesh3dPipeline;
    const auto xrayPipeline = before->mesh3dXrayPipeline;
    REQUIRE(meshPipeline);
    REQUIRE(before->isMesh3D);

    auto replaced = graphics->replaceShaderFromSpv(*shader, {}, meshFragment());
    REQUIRE(replaced.ok());

    auto *after = gpuShaderOf(*shader);
    REQUIRE(after == before);
    REQUIRE(after->mesh3dPipeline != meshPipeline);
    if (xrayPipeline) REQUIRE(after->mesh3dXrayPipeline != xrayPipeline);
    // A mesh shader owns no 2D pipelines, so the 2D swap must not invent any.
    REQUIRE(!after->swapchainPipeline);
    REQUIRE(!after->swapchainOpaquePipeline);
}
