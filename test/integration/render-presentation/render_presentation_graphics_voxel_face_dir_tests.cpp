// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "default": "render-window",
  "module": "filesystem",
  "modules": [
    "filesystem",
    "graphics",
    "physics",
    "physics_softbody",
    "stylize",
    "ui",
    "voxel",
    "weather"
  ],
  "origins": [
    "shader.cpp",
    "shader_reload.cpp",
    "shader_reload_vulkan.cpp",
    "softbody.cpp",
    "stylize.cpp",
    "stylize_anime.cpp",
    "stylize_mesh_particles.cpp",
    "ui_texture_bridge.cpp",
    "voxel_face_dir.cpp",
    "voxel_render_atlas.cpp",
    "water_mesh.cpp",
    "weather.cpp"
  ],
  "cases": {
    "physics-backend": [
      "graphics.WaterUnderwaterDisableTrigger.consumesRealBox3DEvents",
      "softbody.cloth.collidesWithWorld",
      "softbody.cloth.momentumExchangeScalesWithMass",
      "softbody.cloth3d.shortGroundContact",
      "softbody.cloth3d.shortMomentumExchangeScalesWithMass",
      "softbody.fluid.emitAndSettle",
      "softbody.fluid.interactRepels"
    ],
    "script-binding": [
      "graphics.waterReflectionMasker.bindsRealVm"
    ]
  }
}
*/
// clang-format on

// Related integration contracts share one translation unit to amortize header parsing.

// Contract section: shader
#include "PrivateTestScope.h"
#include "zeroerr/assert.h"

#include <SDL2/SDL.h>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>
#include "common/Exception.h"
#include "graphics/Canvas.h"
#include "graphics/DrawItem2D.h"
#include "graphics/Graphics.h"
#include "graphics/Light.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/Water.h"
#include "window/Window.h"
#include "zeroerr/unittest.h"
// shader.cpp
namespace test_shader {
#include "graphics/shaders/custom2d_frag_spv.inc"
#include "graphics/shaders/textured_vert_spv.inc"

// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

using namespace eve::graphics;


TEST_CASE("graphics.Shader.customSpvOnSprite") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);

    eve::window::WindowSettings s;
    s.width    = 320;
    s.height   = 240;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    std::vector<uint32_t> frag(custom2d_frag_spv, custom2d_frag_spv + custom2d_frag_spv_count);
    Shader               *shader = gfx->newShaderFromSpv({}, frag);
    REQUIRE(shader != nullptr);
    REQUIRE(shader->gpuHandle != nullptr);
    CHECK_EQ(shader->declareFloat("factor"), 0);
    shader->sendFloat("factor", 0.5f);

    // White 2x2 texture
    const uint8_t px[16] = {255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255};
    Texture      *tex    = gfx->newTexture(2, 2, px);
    REQUIRE(tex != nullptr);

    auto *ent              = Renderable2D::create();
    ent->transform()->x    = 40;
    ent->transform()->y    = 40;
    ent->sprite()->width   = 80;
    ent->sprite()->height  = 80;
    ent->sprite()->texture = tex;
    ent->sprite()->shader  = shader;
    ent->sprite()->visible = true;

    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.f, 0.f, 0.f, 1.f));

    // Warm up a couple frames (swapchain acquire can soft-fail once).
    for (int i = 0; i < 3; ++i) RenderSystem::render(*gfx);

    Color c = gfx->getPixel(80, 80);
    // factor 0.5 on white → ~0.5 RGB (allow tolerance for sRGB/swapchain format)
    CHECK_GT(c.r, 0.15f);
    CHECK_LT(c.r, 0.85f);
    CHECK(std::abs(c.r - c.g) < 0.15f);
}

TEST_CASE("graphics.Shader.newShaderGlslc") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width    = 160;
    s.height   = 120;
    s.centered = true;
    if (!win->setWindowSettings(s)) return;

    const char *frag = R"(#version 450
layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D MainTex;
layout(push_constant) uniform Externals { float data[32]; } u;
void main() {
  outColor = texture(MainTex, fragUV) * fragColor * u.data[0];
}
)";
    Shader     *sh   = nullptr;
    try {
        sh = gfx->newShader(frag);
    } catch (const eve::Exception &) {
        // A build with neither the in-process shaderc archive nor an external glslc
        // cannot compile GLSL at all; every other failure is a real regression.
        REQUIRE(!gfx->supportsRuntimeGlslCompilation());
        return;
    }
    REQUIRE(sh != nullptr);
    sh->declareFloat("factor");
    sh->sendFloat("factor", 1.f);
    gfx->setShader(sh);
    CHECK(gfx->getShader() == sh);
    gfx->setShader();
    CHECK(gfx->getShader() == nullptr);
}

TEST_CASE("graphics.Shader.newShaderCompilesCustomVertexStage") {
    // The vertex stage goes through the same compiler, so a custom vertex source must
    // replace the built-in textured vertex stage rather than being ignored.
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width    = 160;
    s.height   = 120;
    s.centered = true;
    if (!win->setWindowSettings(s)) return;

    const char *vert = R"(#version 450
layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inUV;
layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec2 fragUV;
void main() {
  gl_Position = vec4(inPosition, 0.0, 1.0);
  fragColor = inColor;
  fragUV = vec2(1.0) - inUV;
}
)";
    const char *frag = R"(#version 450
layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
void main() { outColor = fragColor * vec4(fragUV, 0.0, 1.0); }
)";
    Shader     *sh   = nullptr;
    try {
        sh = gfx->newShader(vert, frag);
    } catch (const eve::Exception &) {
        REQUIRE(!gfx->supportsRuntimeGlslCompilation());
        return;
    }
    REQUIRE(sh != nullptr);
    // A compiled custom vertex stage differs from the embedded default; equal stages
    // would mean the vertex source was dropped.
    const std::vector<uint32_t> builtIn(textured_vert_spv, textured_vert_spv + textured_vert_spv_count);
    CHECK(sh->vertexSpirv() != builtIn);
    gfx->setShader(sh);
    CHECK(gfx->getShader() == sh);
    gfx->setShader();
}
}  // namespace test_shader

// Contract section: shader reload
#include "PrivateTestScope.h"

#include "common/Diagnostic.h"
// shader_reload.cpp
namespace test_shader_reload {
#include "graphics/shaders/custom2d_frag_spv.inc"


using eve::DiagnosticCode;
using eve::graphics::Graphics;
using eve::graphics::Shader;

namespace {

Graphics *initializedGraphics() {
    auto                       *window   = eve::window::Window::create();
    auto                       *graphics = Graphics::create();
    eve::window::WindowSettings settings;
    settings.width    = 160;
    settings.height   = 120;
    settings.centered = true;
    settings.vsync    = 0;
    if (!window || !graphics || !window->setWindowSettings(settings)) return nullptr;
    return graphics;
}

std::vector<uint32_t> customFragment() { return {custom2d_frag_spv, custom2d_frag_spv + custom2d_frag_spv_count}; }

}  // namespace

TEST_CASE("graphics.shaderReload.replacementContract") {
    Graphics *graphics = initializedGraphics();
    REQUIRE(graphics != nullptr);
    SUB_CASE("graphics.shaderReload.successKeepsStableFacade") {
        auto    fragment = customFragment();
        Shader *shader   = graphics->newShaderFromSpv({}, fragment);
        REQUIRE(shader != nullptr);
        void *const originalGpuHandle = shader->gpuHandle;
        shader->declareFloat("factor");
        shader->sendFloat("factor", 0.35f);

        auto replaced = graphics->replaceShaderFromSpv(*shader, {}, fragment);
        REQUIRE(replaced.ok());
        REQUIRE(shader->gpuHandle == originalGpuHandle);
        REQUIRE(shader->hasUniform("factor"));
        float factor = 0.f;
        REQUIRE_EQ(shader->getFromVar("factor", &factor, sizeof(factor)), int(sizeof(factor)));
        REQUIRE_EQ(factor, 0.35f);
        REQUIRE_EQ(shader->fragmentSpirv(), fragment);
    };
    SUB_CASE("graphics.shaderReload.failureKeepsLastGoodPipeline") {
        auto    fragment = customFragment();
        Shader *shader   = graphics->newShaderFromSpv({}, fragment);
        REQUIRE(shader != nullptr);
        void *const originalGpuHandle = shader->gpuHandle;
        const auto  originalFragment  = shader->fragmentSpirv();

        std::vector<uint32_t> invalid  = {0xDEADBEEF, 1, 2, 3};
        auto                  replaced = graphics->replaceShaderFromSpv(*shader, {}, invalid);
        REQUIRE(!replaced.ok());
        REQUIRE(!replaced.diagnostics().empty());
        REQUIRE_EQ(replaced.diagnostics().front().code(), DiagnosticCode::ParseError);
        REQUIRE(shader->gpuHandle == originalGpuHandle);
        REQUIRE_EQ(shader->fragmentSpirv(), originalFragment);
    };
    SUB_CASE("graphics.shaderReload.rejectsForeignFacade") {
        Shader foreign;
        auto   replaced = graphics->replaceShaderFromSpv(foreign, {}, customFragment());
        REQUIRE(!replaced.ok());
        REQUIRE(!replaced.diagnostics().empty());
        REQUIRE_EQ(replaced.diagnostics().front().code(), DiagnosticCode::StaleHandle);
    };
    SUB_CASE("graphics.shaderReload.glslFailureKeepsLastGoodPipeline") {
        auto    fragment = customFragment();
        Shader *shader   = graphics->newShaderFromSpv({}, fragment);
        REQUIRE(shader != nullptr);
        void *const originalGpuHandle = shader->gpuHandle;
        const auto  originalFragment  = shader->fragmentSpirv();

        // Invalid source must be reported as a compile diagnostic on every platform now
        // that the engine compiles GLSL in-process instead of rejecting it on Windows.
        auto replaced =
            graphics->replaceShaderFromGlsl(*shader, {}, "#version 450\nthis is deliberately invalid GLSL\n");
        REQUIRE(!replaced.ok());
        REQUIRE(!replaced.diagnostics().empty());
        REQUIRE_EQ(replaced.diagnostics().front().code(), DiagnosticCode::ParseError);
        REQUIRE(shader->gpuHandle == originalGpuHandle);
        REQUIRE_EQ(shader->fragmentSpirv(), originalFragment);
    };
    eve::window::Window::create()->close();
}


}  // namespace test_shader_reload

// Contract section: shader reload vulkan
#include "PrivateTestScope.h"

#include "graphics/vulkan/Graphics.h"
// shader_reload_vulkan.cpp
namespace test_shader_reload_vulkan {
#include "graphics/shaders/custom2d_frag_spv.inc"
#include "graphics/shaders/mesh3d_frag_spv.inc"


using eve::graphics::Graphics;
using eve::graphics::Shader;

namespace {

Graphics *initializedGraphics() {
    auto                       *window   = eve::window::Window::create();
    auto                       *graphics = Graphics::create();
    eve::window::WindowSettings settings;
    settings.width    = 160;
    settings.height   = 120;
    settings.centered = true;
    settings.vsync    = 0;
    if (!window || !graphics || !window->setWindowSettings(settings)) return nullptr;
    return graphics;
}

std::vector<uint32_t> customFragment() { return {custom2d_frag_spv, custom2d_frag_spv + custom2d_frag_spv_count}; }

/** The engine's default Mesh3D fragment stage; MoltenVK cannot translate the 2D one. */
std::vector<uint32_t> meshFragment() { return {mesh3d_frag_spv, mesh3d_frag_spv + mesh3d_frag_spv_count}; }

eve::graphics::vulkan::GpuShader *gpuShaderOf(Shader &shader) {
    return shader.gpuHandle == nullptr ? nullptr : static_cast<eve::graphics::vulkan::GpuShader *>(shader.gpuHandle);
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
    const auto blendedSwapchain = before->swapchainPipeline;
    const auto opaqueSwapchain  = before->swapchainOpaquePipeline;
    const auto blendedOffscreen = before->offscreenPipeline;
    const auto opaqueOffscreen  = before->offscreenOpaquePipeline;
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
}  // namespace test_shader_reload_vulkan

// Contract section: softbody
#include "PrivateTestScope.h"

#include "gpgpu/ComputeShader.h"
#include "gpgpu/Gpgpu.h"
#include "gpgpu/GpuBuffer.h"
#include "physics/Body.h"
#include "physics/Body3D.h"
#include "physics/Fluid2D.h"
#include "physics/Physics.h"
#include "physics/World.h"
#include "physics/World3D.h"
#include "physics/cloth/Cloth.h"
#include "physics/cloth/Cloth3D.h"
#include "physics/cloth/ClothGPU.h"
#include "physics/cloth/ClothModule.h"
// softbody.cpp
namespace test_softbody {
// softbody.cpp
using namespace eve::physics;
using namespace eve::graphics;


TEST_CASE("softbody.cloth.collidesWithWorld") {
    auto                  *mod = Physics::create();
    std::unique_ptr<World> world(mod->newWorld(0.f, 0.f, true));
    auto                  *ground = world->newBody("static", 400.f, 390.f);
    REQUIRE(ground != nullptr);
    ground->newRectangleFixture(800.f, 20.f);

    std::unique_ptr<Cloth> cloth(eve::cloth::Cloth::create()->newCloth(6, 6, 10.f, 300.f, 60.f));
    for (int i = 0; i < cloth->getParticleCount(); ++i) cloth->unpin(i);
    cloth->setGravity(0.f, 600.f);
    cloth->setCollideWorld(world.get());
    const float surfaceY = 400.f;
    for (int i = 0; i < 120; ++i) cloth->update(1.f / 60.f);
    for (int i = 0; i < cloth->getParticleCount(); ++i)
        REQUIRE_LE(cloth->getParticleY(i), surfaceY + cloth->getParticleSize() + 1.f);
    // Sanity: the sheet actually fell onto the box top.
    REQUIRE_GT(cloth->getParticleY(5 * 6 + 3), 300.f);
}


TEST_CASE("softbody.cloth.momentumExchangeScalesWithMass") {
    auto      *mod     = Physics::create();
    const auto runCase = [&](float density) {
        std::unique_ptr<World> world(mod->newWorld(0.f, 0.f, true));
        auto                  *box = world->newBody("dynamic", 400.f, 120.f);
        REQUIRE(box != nullptr);
        box->newRectangleFixture(40.f, 40.f, density, 0.2f, 0.f);
        std::unique_ptr<Cloth> cloth(eve::cloth::Cloth::create()->newCloth(6, 4, 10.f, 340.f, 20.f));
        for (int i = 0; i < cloth->getParticleCount(); ++i) cloth->unpin(i);
        cloth->setGravity(0.f, 600.f);
        cloth->setCollideWorld(world.get());
        for (int i = 0; i < 120; ++i) {
            world->update(1.f / 60.f);
            cloth->update(1.f / 60.f);
        }
        return box->getLinearVelocityY();
    };

    const float vLight = runCase(0.05f);
    const float vHeavy = runCase(5.f);
    // Same cloth impact must push the light body much harder than the heavy one.
    REQUIRE_GT(vLight, 30.f);
    REQUIRE_GT(vLight, vHeavy + 20.f);
}


TEST_CASE("softbody.cloth3d.shortGroundContact") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world3(mod->newWorld3D(0.f, -9.8f, 0.f, true));
    auto                    *ground = world3->newBody("static", 0.f, -0.5f, 0.f);
    REQUIRE(ground != nullptr);
    ground->newBoxShape(8.f, 1.f, 8.f);

    std::unique_ptr<Cloth3D> cloth(eve::cloth::Cloth::create()->newCloth3D(8, 6, 0.5f, 0.f, 0.5f, 0.f));
    for (int i = 0; i < cloth->getParticleCount(); ++i) cloth->unpin(i);
    cloth->setGravity(0.f, -6.f, 0.f);
    cloth->setCollideWorld(world3.get());
    const float surfaceY = 0.f;
    const float initialY = cloth->getParticleY(5 * 8 + 3);
    REQUIRE_GT(initialY, cloth->getParticleSize() + 0.1f);
    float minimumY = initialY;
    for (int step = 0; step < 24; ++step) {
        cloth->update(1.f / 60.f);
        minimumY = std::min(minimumY, cloth->getParticleY(5 * 8 + 3));
        for (int i = 0; i < cloth->getParticleCount(); ++i)
            REQUIRE_GE(cloth->getParticleY(i), surfaceY - cloth->getParticleSize() - 0.05f);
    }
    // collideWorld projects to the shell, then applies restitution and an impulse kick.
    // Arrival must occur during the trajectory; its last sample may already be rebounding.
    REQUIRE_LT(cloth->getParticleY(5 * 8 + 3), initialY - 0.02f);
    REQUIRE_LE(minimumY, cloth->getParticleSize() + 0.03f);
}

TEST_CASE("softbody.cloth3d.shortMomentumExchangeScalesWithMass") {
    auto      *mod     = Physics::create();
    const auto runCase = [&](zeroerr::TestContext *_ZEROERR_TEST_CONTEXT, float density) {
        std::unique_ptr<World3D> world3(mod->newWorld3D(0.f, 0.f, 0.f, true));
        auto                    *box = world3->newBody("dynamic", 0.f, 1.2f, 0.f);
        REQUIRE(box != nullptr);
        box->newBoxShape(1.f, 0.8f, 1.f, density, 0.2f, 0.f);
        std::unique_ptr<Cloth3D> cloth(eve::cloth::Cloth::create()->newCloth3D(6, 4, 0.4f, -0.6f, 1.8f, -0.6f));
        for (int i = 0; i < cloth->getParticleCount(); ++i) cloth->unpin(i);
        cloth->setGravity(0.f, -5.f, 0.f);
        cloth->setCollideWorld(world3.get());
        for (int i = 0; i < 30; ++i) {
            world3->update(1.f / 60.f);
            cloth->update(1.f / 60.f);
        }
        return box->getLinearVelocityY();
    };

    const float vLight = runCase(_ZEROERR_TEST_CONTEXT, 0.05f);
    const float vHeavy = runCase(_ZEROERR_TEST_CONTEXT, 5.f);
    // +Y up: the light box is pushed downward noticeably, the heavy one barely.
    REQUIRE_LT(vLight, -0.2f);
    REQUIRE_LT(vLight, vHeavy - 0.1f);
}


TEST_CASE("softbody.fluid.emitAndSettle") {
    auto                    *mod = Physics::create();
    std::unique_ptr<Fluid2D> fluid(mod->newFluid2D(256));
    REQUIRE(fluid.get() != nullptr);
    fluid->setBounds(0.f, 0.f, 400.f, 400.f);
    fluid->setGravity(0.f, 900.f);
    int added = fluid->emit(200.f, 80.f, 40, 0.f, 0.f);
    CHECK_EQ(added, 40);
    CHECK_EQ(fluid->getParticleCount(), 40);

    float y0 = 0.f;
    for (int i = 0; i < fluid->getParticleCount(); ++i) y0 += fluid->getParticleY(i);
    y0 /= float(fluid->getParticleCount());

    for (int i = 0; i < 120; ++i) fluid->update(1.f / 60.f);

    float y1 = 0.f;
    for (int i = 0; i < fluid->getParticleCount(); ++i) y1 += fluid->getParticleY(i);
    y1 /= float(fluid->getParticleCount());

    CHECK_GT(y1, y0 + 10.f);
    // Stay inside container.
    for (int i = 0; i < fluid->getParticleCount(); ++i) {
        CHECK_GE(fluid->getParticleX(i), -1.f);
        CHECK_LE(fluid->getParticleX(i), 401.f);
        CHECK_GE(fluid->getParticleY(i), -1.f);
        CHECK_LE(fluid->getParticleY(i), 401.f);
    }
}

TEST_CASE("softbody.fluid.interactRepels") {
    auto                    *mod = Physics::create();
    std::unique_ptr<Fluid2D> fluid(mod->newFluid2D(128));
    fluid->setBounds(0.f, 0.f, 300.f, 300.f);
    fluid->setGravity(0.f, 0.f);
    fluid->emit(150.f, 150.f, 25, 0.f, 0.f);

    float avgDist0 = 0.f;
    for (int i = 0; i < fluid->getParticleCount(); ++i) {
        const float dx = fluid->getParticleX(i) - 150.f;
        const float dy = fluid->getParticleY(i) - 150.f;
        avgDist0 += std::sqrt(dx * dx + dy * dy);
    }
    avgDist0 /= float(fluid->getParticleCount());

    for (int i = 0; i < 45; ++i) {
        fluid->interactAt(150.f, 150.f, 80.f, -4000.f);
        fluid->update(1.f / 60.f);
    }

    float avgDist1 = 0.f;
    for (int i = 0; i < fluid->getParticleCount(); ++i) {
        const float dx = fluid->getParticleX(i) - 150.f;
        const float dy = fluid->getParticleY(i) - 150.f;
        avgDist1 += std::sqrt(dx * dx + dy * dy);
    }
    avgDist1 /= float(fluid->getParticleCount());
    CHECK_GT(avgDist1, avgDist0 + 2.f);
}


// softbody_gpu.cpp
using namespace eve::physics;
using namespace eve::gpgpu;

namespace {

[[maybe_unused]] bool tryInitGpuWindow() {
    auto *win = eve::window::Window::create();
    auto *gfx = eve::graphics::Graphics::create();
    if (!win || !gfx) return false;
    eve::window::WindowSettings s;
    s.width    = 320;
    s.height   = 240;
    s.centered = true;
    return win->setWindowSettings(s);
}

[[maybe_unused]] std::unique_ptr<ClothGPU> tryMakeCloth(eve::gpgpu::Gpgpu *gpgpu, int cols, int rows, float spacing,
                                                        float ox, float oy) {
    try {
        return std::unique_ptr<ClothGPU>(new ClothGPU(gpgpu, cols, rows, spacing, ox, oy));
    } catch (...) {
        return nullptr;  // no compute compiler / backend — skip GPU path
    }
}

}  // namespace

TEST_CASE("softbody.gpu.clothFallsAndPinsHold") {
    if (!tryInitGpuWindow()) return;
    std::unique_ptr<ClothGPU> cloth;
    try {
        cloth = std::unique_ptr<ClothGPU>(eve::cloth::Cloth::create()->newClothGPU(8, 6, 10.f, 100.f, 40.f));
    } catch (...) {
        return;  // no shader compiler — skip
    }
    REQUIRE(cloth.get() != nullptr);
    REQUIRE_EQ(cloth->getParticleCount(), 48);
    REQUIRE(cloth->isPinned(0));
    REQUIRE(cloth->isPinned(7));
    const float pinX0 = cloth->getParticleX(0);
    const float pinY0 = cloth->getParticleY(0);

    // Pinned top row holds under gravity.
    for (int i = 0; i < 60; ++i) cloth->update(1.f / 60.f);
    REQUIRE(std::fabs(cloth->getParticleX(0) - pinX0) < 0.5f);
    REQUIRE(std::fabs(cloth->getParticleY(0) - pinY0) < 0.5f);

    // Unpin everything → free fall, like the CPU cloth test.
    for (int i = 0; i < cloth->getParticleCount(); ++i) cloth->unpin(i);
    const float freeY0 = cloth->getParticleY(20);
    for (int i = 0; i < 45; ++i) cloth->update(1.f / 60.f);
    REQUIRE_GT(cloth->getParticleY(20), freeY0 + 20.f);
    for (int i = 0; i < cloth->getParticleCount(); ++i) {
        REQUIRE(std::isfinite(cloth->getParticleX(i)));
        REQUIRE(std::isfinite(cloth->getParticleY(i)));
    }
}

TEST_CASE("softbody.gpu.clothBoundsAndInteract") {
    if (!tryInitGpuWindow()) return;
    auto *gpgpu = Gpgpu::create();
    REQUIRE(gpgpu != nullptr);
    if (!gpgpu->isAvailable()) return;

    std::unique_ptr<ClothGPU> cloth = tryMakeCloth(gpgpu, 6, 6, 12.f, 50.f, 50.f);
    if (!cloth) return;
    REQUIRE_EQ(cloth->getParticleCount(), 36);
    for (int i = 0; i < cloth->getParticleCount(); ++i) cloth->unpin(i);
    cloth->setGravity(0.f, 0.f);
    cloth->setBounds(0.f, 0.f, 300.f, 300.f);

    float avg0 = 0.f;
    for (int i = 0; i < cloth->getParticleCount(); ++i) {
        const float dx = cloth->getParticleX(i) - 86.f;
        const float dy = cloth->getParticleY(i) - 86.f;
        avg0 += std::sqrt(dx * dx + dy * dy);
    }
    avg0 /= float(cloth->getParticleCount());

    for (int i = 0; i < 60; ++i) {
        cloth->interactAt(86.f, 86.f, 90.f, -3600.f);
        cloth->update(1.f / 60.f);
    }

    float avg1 = 0.f;
    for (int i = 0; i < cloth->getParticleCount(); ++i) {
        const float dx = cloth->getParticleX(i) - 86.f;
        const float dy = cloth->getParticleY(i) - 86.f;
        avg1 += std::sqrt(dx * dx + dy * dy);
    }
    avg1 /= float(cloth->getParticleCount());
    REQUIRE_GT(avg1, avg0 + 4.f);
    for (int i = 0; i < cloth->getParticleCount(); ++i) {
        REQUIRE_GE(cloth->getParticleX(i), -1.f);
        REQUIRE_LE(cloth->getParticleX(i), 301.f);
        REQUIRE_GE(cloth->getParticleY(i), -1.f);
        REQUIRE_LE(cloth->getParticleY(i), 301.f);
    }
}

TEST_CASE("softbody.gpu.clothSelfCollisionSeparates") {
    if (!tryInitGpuWindow()) return;
    auto *gpgpu = Gpgpu::create();
    REQUIRE(gpgpu != nullptr);
    if (!gpgpu->isAvailable()) return;

    std::unique_ptr<ClothGPU> cloth;
    try {
        cloth = std::unique_ptr<ClothGPU>(eve::cloth::Cloth::create()->newClothGPU(10, 8, 12.f, 20.f, 20.f));
    } catch (...) {
        return;
    }
    REQUIRE(cloth.get() != nullptr);
    for (int i = 0; i < cloth->getParticleCount(); ++i) cloth->unpin(i);
    cloth->setGravity(0.f, 320.f);
    cloth->setBounds(0.f, 0.f, 600.f, 600.f);
    cloth->setParticleSize(3.f);
    cloth->setStiffness(0.9f);
    cloth->setSelfCollision(true);
    REQUIRE(cloth->getSelfCollision());
    const float minDist = cloth->getParticleSize() * 2.f;

    // Alternating wind + pointer repulsion folds the sheet onto itself.
    for (int f = 0; f < 240; ++f) {
        if ((f / 40) % 2 == 0)
            cloth->applyForce(260.f, 0.f);
        else
            cloth->applyForce(-260.f, 0.f);
        cloth->interactAt(300.f + float(f % 60) * 1.5f, 220.f, 90.f, -2600.f);
        cloth->update(1.f / 60.f);
    }

    float minSeen = 1e9f;
    for (int i = 0; i < cloth->getParticleCount(); ++i) {
        for (int j = i + 1; j < cloth->getParticleCount(); ++j) {
            const float dx = cloth->getParticleX(j) - cloth->getParticleX(i);
            const float dy = cloth->getParticleY(j) - cloth->getParticleY(i);
            minSeen        = std::min(minSeen, std::sqrt(dx * dx + dy * dy));
        }
    }
    REQUIRE_GT(minSeen, minDist * 0.5f);
}


}  // namespace test_softbody

// Contract section: stylize
#include "PrivateTestScope.h"

#include <cstdio>
#include <optional>
#include <string>
#include "graphics/RenderSystem3D.h"
#include "image/Image.h"
#include "image/ImageData.h"
#include "stylize/StyleInstance.h"
#include "stylize/StyleRecipe.h"
#include "stylize/Stylize.h"
// stylize.cpp
namespace test_stylize {
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

using eve::graphics::Camera3D;
using eve::graphics::Canvas;
using eve::graphics::Graphics;
using eve::graphics::Mesh;
using eve::graphics::Renderable2D;
using eve::graphics::Renderable3D;
using eve::graphics::RenderSystem;
using eve::graphics::RenderSystem3D;
using eve::graphics::Shader;
using eve::graphics::Texture;
using eve::image::ImageData;
using eve::stylize::StyleChain;
using eve::stylize::StyleInstance;
using eve::stylize::StylePass;
using eve::stylize::StyleRecipe;
using eve::stylize::Stylize;
using Colorf = ImageData::Colorf;

namespace {

[[maybe_unused]] bool near(float a, float b, float eps = 0.08f) { return std::fabs(a - b) <= eps; }

[[maybe_unused]] float luma(const eve::graphics::Color &c) { return (c.r + c.g + c.b) / 3.f; }

[[maybe_unused]] ImageData *makeGradient(int w, int h) {
    auto *img = new ImageData(w, h, "RGBA8");
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float  u = float(x) / float(std::max(w - 1, 1));
            float  v = float(y) / float(std::max(h - 1, 1));
            Colorf c{u, 0.35f + 0.4f * v, 1.f - u, 1.f};
            // Hard edge for Sobel / outline styles.
            if (x > w / 2 && y > h / 3 && y < 2 * h / 3) c = Colorf{0.95f, 0.85f, 0.2f, 1.f};
            img->setPixel(x, y, c);
        }
    }
    return img;
}

[[maybe_unused]] Texture *makeCylinderAlbedo(Graphics *gfx, int size = 256) {
    // Bold horizontal stripes only (no vertical seams — seams become false ink/toon outlines).
    // Avoid near-white bands so PBR/toon highlights keep readable form.
    std::vector<uint8_t> px(size_t(size) * size_t(size) * 4);
    const int            bandH         = size / 5;
    const uint8_t        palette[3][3] = {
        {210, 92, 58},    // terracotta
        {232, 186, 110},  // warm sand
        {48, 148, 158},   // teal
    };
    for (int y = 0; y < size; ++y) {
        const int band = (y / std::max(bandH, 1)) % 3;
        for (int x = 0; x < size; ++x) {
            const size_t i = size_t(y * size + x) * 4;
            px[i + 0]      = palette[band][0];
            px[i + 1]      = palette[band][1];
            px[i + 2]      = palette[band][2];
            px[i + 3]      = 255;
        }
    }
    return gfx->newTexture(size, size, px.data());
}

[[maybe_unused]] void resetScene3D() {
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
    if (ecs::current()->getManager<Renderable2D>() != nullptr) {
        auto view = ecs::View<Renderable2D, Renderable2D::Sprite>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [sp]   = *it;
            sp->visible = false;
        }
    }
}

/** Render cylinder with optional mesh shader; return screen ImageData (caller owns). */
[[maybe_unused]] ImageData *renderCylinderFrame(Graphics *gfx, Mesh *mesh, Texture *albedo, Shader *meshShader) {
    resetScene3D();

    auto *ent                    = Renderable3D::create();
    ent->meshRenderer()->mesh    = mesh;
    ent->meshRenderer()->texture = albedo;
    ent->meshRenderer()->shader  = meshShader;
    ent->meshRenderer()->visible = true;
    ent->setTint(1.f, 1.f, 1.f, 1.f);
    ent->transform()->yaw   = 0.55f;
    ent->transform()->pitch = -0.15f;

    auto *cam = Camera3D::createCamera();
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setEye(2.4f, 1.5f, 3.2f);
    cam->data()->nearZ = 0.1f;
    cam->data()->farZ  = 50.f;
    // Balanced fill so form reads without crushing shadows to black.
    cam->data()->ambientR = 0.28f;
    cam->data()->ambientG = 0.30f;
    cam->data()->ambientB = 0.34f;

    // Tiny 2D sprite keeps RenderSystem happy / present path consistent with model3d tests.
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

    gfx->setScreenReadbackEnabled(true);
    // Lifted slate so object silhouettes separate cleanly for outline/ink.
    gfx->setBackgroundColor(eve::graphics::Color(0.14f, 0.16f, 0.20f, 1.f));
    // Empirically +Z / camera-side lights the visible face (see RenderSystem3D.lightingAffectsPixels).
    // Keep intensity near 1 so highlights don't blow cel/watercolor pigment to pure white.
    RenderSystem3D::setDirectionalLight(0.55f, 0.95f, 1.15f, 1.05f, 1.0f, 0.95f);

    for (int i = 0; i < 8; ++i) {
        ent->transform()->yaw = 0.45f + float(i) * 0.04f;
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }

    eve::graphics::Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.12f);
    CHECK(luma(mid) < 0.92f);

    [[maybe_unused]] auto *const imageModule = eve::image::Image::create();
    ImageData                   *frame       = gfx->newImageData();
    REQUIRE(frame != nullptr);
    return frame;
}

[[maybe_unused]] ImageData *applyPostToFrame(Graphics *gfx, ImageData *frame, StylePass *pass) {
    REQUIRE(frame != nullptr);
    REQUIRE(pass != nullptr);
    Texture *tex = gfx->newTexture(frame);
    REQUIRE(tex != nullptr);

    Canvas *rt = gfx->newCanvas(frame->getWidth(), frame->getHeight());
    REQUIRE(rt != nullptr);
    gfx->setCanvas(rt);
    gfx->clear(eve::graphics::Color(0.14f, 0.16f, 0.20f, 1.f), std::nullopt, std::nullopt);
    if (pass->hasParam("time") && pass->getFloat("time") <= 0.f) pass->setTime(0.35f);
    pass->apply(gfx, tex);
    gfx->setCanvas();

    ImageData *out = rt->newImageData();
    REQUIRE(out != nullptr);
    return out;
}

}  // namespace


TEST_CASE("stylize.api.passFromShaderAndChain") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width    = 128;
    s.height   = 96;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    auto   *mod = Stylize::create();
    Shader *raw = mod->newPostShader(gfx, "pixel");
    REQUIRE(raw != nullptr);
    StylePass *custom = mod->newPassFromShader("custom_pixel", raw);
    REQUIRE(custom != nullptr);
    CHECK_EQ(custom->getStyle(), std::string("custom_pixel"));

    StylePass *a = mod->newPass(gfx, "cartoon");
    StylePass *b = mod->newPass(gfx, "pixel");
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    StyleChain *chain = mod->newChain();
    REQUIRE(chain != nullptr);
    chain->add(a);
    chain->add(b);
    CHECK_EQ(chain->getPassCount(), 2);
    CHECK(chain->getPass(0) == a);

    std::unique_ptr<ImageData> img(makeGradient(64, 48));
    Texture                   *tex  = gfx->newTexture(img.get());
    Canvas                    *dest = gfx->newCanvas(64, 48);
    Canvas                    *temp = gfx->newCanvas(64, 48);
    REQUIRE(tex != nullptr);
    REQUIRE(dest != nullptr);
    REQUIRE(temp != nullptr);

    custom->applyTo(gfx, tex, dest);
    eve::graphics::Color p0 = dest->getPixel(32, 24);
    CHECK_GT(p0.a, 0.5f);

    chain->apply(gfx, tex, dest, temp);
    eve::graphics::Color p1 = dest->getPixel(32, 24);
    CHECK_GT(p1.a, 0.5f);

    std::unique_ptr<StyleInstance> watercolor(mod->newInstance("watercolor"));
    std::unique_ptr<StyleInstance> pixel(mod->newInstance("pixel"));
    std::unique_ptr<StyleRecipe>   recipe(mod->newRecipe());
    recipe->add(pixel.get());
    recipe->add(watercolor.get());
    recipe->compile(gfx);
    CHECK(recipe->isCompiled());
    CHECK_EQ(recipe->getStage(), std::string("afterTonemap"));
    CHECK_EQ(recipe->getStyle(0)->getStyle(), std::string("watercolor"));
    recipe->apply(gfx, tex, dest);
    eve::graphics::Color p2 = dest->getPixel(32, 24);
    CHECK_GT(p2.a, 0.5f);

    delete chain;
    delete custom;
    delete a;
    delete b;
    win->close();
}

TEST_CASE("stylize.gpu.postPassAndMeshShader") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width    = 256;
    s.height   = 192;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    auto *mod = Stylize::create();

    for (const char *style : {"cartoon", "watercolor", "ink", "pixel"}) {
        StylePass *pass = mod->newPass(gfx, style);
        REQUIRE(pass != nullptr);
        CHECK_EQ(pass->getStyle(), std::string(style));
        REQUIRE(pass->getShader() != nullptr);
        REQUIRE(pass->getShader()->gpuHandle != nullptr);
        CHECK(pass->hasParam("texelW"));
        if (pass->hasParam("time")) pass->setTime(0.25f);

        std::unique_ptr<ImageData> img(makeGradient(64, 48));
        auto                      *tex = gfx->newTexture(img.get());
        REQUIRE(tex != nullptr);

        Canvas *rt = gfx->newCanvas(128, 96);
        REQUIRE(rt != nullptr);
        gfx->setCanvas(rt);
        gfx->clear(eve::graphics::Color(0.1f, 0.1f, 0.1f, 1.f), std::nullopt, std::nullopt);
        pass->apply(gfx, tex);
        gfx->setCanvas();

        eve::graphics::Color p = rt->getPixel(64, 48);
        CHECK_GT(p.a, 0.5f);
        delete pass;
    }

    Shader *toonMesh = mod->newMeshShader(gfx, "cartoon");
    REQUIRE(toonMesh != nullptr);
    REQUIRE(toonMesh->gpuHandle != nullptr);
    CHECK(toonMesh->hasUniform("bands"));

    Shader *inkMesh = mod->newMeshShader(gfx, "ink");
    REQUIRE(inkMesh != nullptr);
    REQUIRE(inkMesh->gpuHandle != nullptr);
    CHECK(inkMesh->hasUniform("washLevels"));

    bool threw = false;
    try {
        mod->newMeshShader(gfx, "watercolor");
    } catch (const eve::Exception &) {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE("stylize.render.cylinderStyleGallery") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width    = 512;
    s.height   = 384;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    Mesh *cylinder = gfx->newMeshCylinder(64, 1, true);
    REQUIRE(cylinder != nullptr);
    CHECK_GT(cylinder->indexCount, 0);

    Texture *albedo = makeCylinderAlbedo(gfx, 128);
    REQUIRE(albedo != nullptr);

    auto *mod = Stylize::create();

    std::unique_ptr<ImageData> baseFrame(renderCylinderFrame(gfx, cylinder, albedo, nullptr));
    REQUIRE(baseFrame.get() != nullptr);

    // Cartoon uses object-space cel mesh first, then outline post (UTS-like).
    Shader *toonMesh = mod->newMeshShader(gfx, "cartoon");
    REQUIRE(toonMesh != nullptr);
    toonMesh->sendFloat("bands", 3.f);
    toonMesh->sendFloat("rimPower", 2.6f);
    toonMesh->sendFloat("rimStrength", 0.45f);
    toonMesh->sendFloat("posterize", 5.f);
    std::unique_ptr<ImageData> cartoonBase(renderCylinderFrame(gfx, cylinder, albedo, toonMesh));

    const char *styles[] = {"cartoon", "ink", "watercolor", "pixel"};
    for (const char *id : styles) {
        StylePass *pass = mod->newPass(gfx, id);
        REQUIRE(pass != nullptr);
        ImageData *srcFrame = baseFrame.get();
        if (std::string(id) == "cartoon") {
            srcFrame = cartoonBase.get();
            // Mesh already did cel; post lightly reinforces bands + silhouette ink.
            pass->setFloat("bands", 3.f);
            pass->setFloat("outlineStrength", 1.35f);
            pass->setFloat("outlineThreshold", 0.08f);
            pass->setFloat("posterize", 5.f);
            pass->setFloat("outlineWidth", 2.0f);
            pass->setFloat("shadowLift", 0.18f);
            pass->setFloat("softEdge", 0.12f);
        } else if (std::string(id) == "watercolor") {
            pass->setFloat("blurAmount", 2.2f);
            pass->setFloat("edgeDarken", 1.2f);
            pass->setFloat("paperStrength", 0.6f);
            pass->setFloat("distortion", 0.55f);
            pass->setFloat("bleed", 0.58f);
            pass->setFloat("saturation", 0.95f);
            pass->setFloat("granulation", 0.45f);
        } else if (std::string(id) == "ink") {
            pass->setFloat("inkContrast", 1.35f);
            pass->setFloat("washLevels", 5.f);
            pass->setFloat("edgeThreshold", 0.18f);
            pass->setFloat("diffusion", 3.5f);
            pass->setFloat("inkDensity", 0.75f);
            pass->setFloat("edgeStrength", 1.1f);
            pass->setFloat("paperR", 0.96f);
            pass->setFloat("paperG", 0.93f);
            pass->setFloat("paperB", 0.86f);
        } else if (std::string(id) == "pixel") {
            pass->setFloat("pixelSize", 6.f);
            pass->setFloat("paletteSteps", 5.f);
            pass->setFloat("ditherStrength", 0.12f);
            pass->setFloat("toonBands", 3.f);
            pass->setFloat("sharpness", 1.f);
            pass->setFloat("outline", 0.95f);
        }

        std::unique_ptr<ImageData> styled(applyPostToFrame(gfx, srcFrame, pass));
        delete pass;
        REQUIRE(styled.get() != nullptr);

        // Sanity: some non-background pixels exist (cylinder albedo is warm).
        int       warm    = 0;
        int       changed = 0;
        const int w       = styled->getWidth();
        const int h       = styled->getHeight();
        for (int y = h / 4; y < 3 * h / 4; y += 4) {
            for (int x = w / 4; x < 3 * w / 4; x += 4) {
                Colorf c = styled->getPixel(x, y);
                if (c.r + c.g + c.b > 0.2f) ++warm;
                const auto original = srcFrame->getPixel(x, y);
                if (std::fabs(c.r - original.r) + std::fabs(c.g - original.g) + std::fabs(c.b - original.b) > 0.04f)
                    ++changed;
            }
        }
        REQUIRE(warm > 20);
        REQUIRE(changed > 20);
    }

    win->close();
}
}  // namespace test_stylize

// Contract section: stylize anime
#include "PrivateTestScope.h"

#include "stylize/StyleShaders.h"
// stylize_anime.cpp
namespace test_stylize_anime {
TEST_CASE("stylize.anime.realGpuPipeline") {
    auto                       *window   = eve::window::Window::create();
    auto                       *graphics = eve::graphics::Graphics::create();
    eve::window::WindowSettings settings;
    settings.width  = 160;
    settings.height = 120;
    REQUIRE(window->setWindowSettings(settings));
    auto *shader = eve::stylize::createMeshShader(graphics, "anime");
    REQUIRE(shader != nullptr);
    REQUIRE(shader->gpuHandle != nullptr);
    REQUIRE_EQ(shader->getKind(), eve::graphics::Shader::Kind::eMesh3D);
    REQUIRE(shader->hasUniform("skin"));
    REQUIRE(shader->hasUniform("hair"));
    REQUIRE(shader->hasUniform("unlit"));
}
}  // namespace test_stylize_anime

// Contract section: stylize mesh particles
#include "PrivateTestScope.h"

#include <array>
#include "stylize/MeshEffect.h"
#include "stylize/MeshEffectRenderer.h"
#include "stylize/MeshParticleEmitter.h"
#include "stylize/MeshVfxAsset.h"
#include "stylize/MeshVfxRenderBatch.h"
#include "stylize/MeshVfxScalability.h"
#include "stylize/SkillMeshEffect.h"
#include "stylize/TrailEffect.h"
// stylize_mesh_particles.cpp
namespace test_stylize_mesh_particles {
// stylize_mesh_particles.cpp
using namespace eve::stylize;


// stylize_mesh_vfx.cpp
using eve::stylize::MeshEffectPlayback;
using eve::stylize::MeshEffectState;
using eve::stylize::MeshEffectTargetHandle;
using eve::stylize::prepareTrailUpload;
using eve::stylize::SkillMeshEffectKind;
using eve::stylize::Stylize;
using eve::stylize::TrailAppendResult;
using eve::stylize::TrailEmitter;
using eve::stylize::TrailSettings;


TEST_CASE("stylize.meshVfx.gpuRibbonSubmission") {
    auto *window   = eve::window::Window::create();
    auto *graphics = eve::graphics::Graphics::create();
    REQUIRE(window != nullptr);
    REQUIRE(graphics != nullptr);
    eve::window::WindowSettings settings;
    settings.width    = 64;
    settings.height   = 64;
    settings.centered = true;
    settings.vsync    = 0;
    REQUIRE(window->setWindowSettings(settings));

    auto *stylize = Stylize::create();
    auto  effect  = stylize->createMeshEffect("slash");
    effect->setPlayback(MeshEffectPlayback{0.f, 1.f, 0.f, false});
    effect->play();
    auto renderer = stylize->createMeshEffectRenderer(*graphics);

    TrailEmitter trail(TrailSettings{8, 1.f, 0.f, 2.f});
    (void)trail.append({-0.8f, -0.25f, 0.f}, {-0.8f, 0.25f, 0.f});
    (void)trail.append({0.8f, -0.25f, 0.f}, {0.8f, 0.25f, 0.f});

    graphics->setMesh3DViewProj(glm::mat4(1.f));
    graphics->setMesh3DView(glm::mat4(1.f));
    graphics->setMesh3DCameraPos({0.f, 0.f, 2.f});
    auto *target = graphics->newCanvas(64, 64);
    REQUIRE(target != nullptr);
    graphics->begin3DFrameToCanvas(target);
    auto submitted = renderer->submitTrail(*effect, trail.buildMesh());
    REQUIRE(submitted.ok());
    CHECK(static_cast<int>(submitted.value()) == static_cast<int>(eve::stylize::MeshEffectSubmitStatus::Drawn));
    graphics->end3DFrameToCanvas();

    auto descriptor = graphics->describeMesh(renderer->trailMesh());
    REQUIRE(descriptor.has_value());
    CHECK_EQ(descriptor->vertexCount, 4u);
    CHECK_EQ(descriptor->indexCount, 6u);
    const eve::graphics::Color center        = target->getPixel(32, 32);
    const bool                 renderedColor = center.r > 0.05f || center.g > 0.05f || center.b > 0.05f;
    CHECK(renderedColor);
    window->close();
}


TEST_CASE("stylize.meshVfx.gpuBatchQueueSubmission") {
    auto *window   = eve::window::Window::create();
    auto *graphics = eve::graphics::Graphics::create();
    REQUIRE(window != nullptr);
    REQUIRE(graphics != nullptr);
    eve::window::WindowSettings settings;
    settings.width  = 64;
    settings.height = 64;
    REQUIRE(window->setWindowSettings(settings));

    auto *stylize = Stylize::create();
    auto  effect  = stylize->createMeshEffect("slash");
    effect->setPlayback(MeshEffectPlayback{0.f, 1.f, 0.f, false});
    effect->play();
    auto renderer = stylize->createMeshEffectRenderer(*graphics);

    TrailEmitter trail(TrailSettings{8, 1.f, 0.f, 2.f});
    (void)trail.append({-0.8f, -0.25f, 0.f}, {-0.8f, 0.25f, 0.f});
    (void)trail.append({0.8f, -0.25f, 0.f}, {0.8f, 0.25f, 0.f});
    eve::stylize::MeshVfxRendererCommand command;
    command.stableInstanceId = 17;
    command.kind             = eve::stylize::MeshVfxRendererCommand::Kind::Trail;
    command.effect           = effect.get();
    command.trail            = trail.buildMesh();

    eve::stylize::MeshVfxRenderQueue queue;
    eve::stylize::MeshVfxRenderBatch batch;
    batch.key.blend = eve::stylize::MeshVfxBatchBlend::Additive;
    batch.draws.push_back({17, eve::stylize::MeshVfxLodTier::Full, eve::stylize::MeshVfxMeshUpdate::Refresh, 1});
    queue.batches.push_back(std::move(batch));

    graphics->setMesh3DViewProj(glm::mat4(1.f));
    graphics->setMesh3DView(glm::mat4(1.f));
    graphics->setMesh3DCameraPos({0.f, 0.f, 2.f});
    auto *target = graphics->newCanvas(64, 64);
    REQUIRE(target != nullptr);
    graphics->begin3DFrameToCanvas(target);
    const auto submitted =
        renderer->submitQueue(queue, std::span<const eve::stylize::MeshVfxRendererCommand>(&command, 1));
    REQUIRE(submitted.ok());
    CHECK(static_cast<int>(submitted.value().status) ==
          static_cast<int>(eve::stylize::MeshVfxQueueSubmitStatus::Complete));
    CHECK_EQ(submitted.value().acceptedBatches, 1u);
    CHECK_EQ(submitted.value().acceptedDraws, 1u);
    graphics->end3DFrameToCanvas();
    window->close();
}

// stylize_mesh_vfx_asset.cpp
using namespace eve::stylize;


namespace {
class MeshVfxAttachmentSource final : public eve::IAttachmentPointSource {
public:
    eve::Result<eve::AttachmentPoint> sampleAttachmentPoint(std::string_view     name,
                                                            eve::AttachmentPoint offset) const override {
        if (name == "weapon.root") return eve::Result<eve::AttachmentPoint>::success({offset.x, offset.y, offset.z});
        if (name == "weapon.tip")
            return eve::Result<eve::AttachmentPoint>::success({offset.x + 1.f, offset.y, offset.z});
        return eve::Result<eve::AttachmentPoint>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "missing test attachment"));
    }
};
}  // namespace


namespace {
class MeshVfxAnimationEvents final : public eve::IAnimationEventSource {
public:
    std::vector<std::string> names;
    std::size_t              animationEventCount() const noexcept override { return names.size(); }
    std::string              animationEventName(std::size_t index) const override {
        return index < names.size() ? names[index] : std::string{};
    }
};
}  // namespace


// stylize_mesh_vfx_batch.cpp
using namespace eve::stylize;

namespace {

[[maybe_unused]] MeshVfxLodDecision visible(std::uint64_t id, MeshVfxLodTier tier, std::uint32_t interval = 1) {
    MeshVfxLodDecision decision;
    decision.stableInstanceId   = id;
    decision.tier               = tier;
    decision.meshUpdateInterval = interval;
    decision.trailSampleStride  = interval;
    return decision;
}

[[maybe_unused]] MeshVfxBatchKey key(std::uint64_t material, MeshVfxBatchBlend blend) {
    return MeshVfxBatchKey{1, material, 3, 0, blend};
}

}  // namespace
}  // namespace test_stylize_mesh_particles

// Contract section: ui texture bridge
#include "PrivateTestScope.h"

#include <imgui.h>
#include "ui/UI.h"
#include "ui/UIBackend.h"
#include "ui/UIHost.h"
#include "ui/Widget.h"
#include "window/sdl/Window.h"
// ui_texture_bridge.cpp
namespace test_ui_texture_bridge {
// ui_texture_bridge.cpp
using eve::graphics::Canvas;
using eve::graphics::Color;
using eve::graphics::Graphics;
using eve::graphics::RenderSystem;
using eve::ui::image;
using eve::ui::imageButton;
using eve::ui::UI;
using eve::ui::UIHost;
using eve::ui::UIHostHandle;
using eve::ui::viewport;
using eve::ui::window;

TEST_CASE("UI.textureBridge.viewportPixelsReachSwapchain") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    auto *ui  = UI::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    REQUIRE(ui != nullptr);

    eve::window::WindowSettings settings;
    settings.width    = 640;
    settings.height   = 480;
    settings.centered = true;
    settings.vsync    = 0;
    REQUIRE(win->setWindowSettings(settings));
    REQUIRE(ui->initBackend());

    gfx->setBackgroundColor(Color(0.02f, 0.03f, 0.04f, 1.f));
    gfx->setScreenReadbackEnabled(true);
    const UIHostHandle textureHost = ui->mountAs(
        "texture-pixels", window("Texture bridge",
                                 {viewport("engine-texture", 320.f, 200.f), image("engine-image", 80.f, 48.f),
                                  imageButton("engine-image-button", 80.f, 48.f)},
                                 "root"));
    REQUIRE(UIHost::resolve(textureHost).has_value());

    Canvas  *canvas    = nullptr;
    uint64_t textureId = 0;
    for (int frame = 0; frame < 4; ++frame) {
        ui->beginFrameAndRender();
        canvas = ui->viewportCanvas("engine-texture");
        REQUIRE(canvas != nullptr);
        if (textureId == 0) {
            textureId = ui->registerTexture(canvas->getTexture());
            REQUIRE(textureId != 0);
            ui->setImageTextureId("engine-image", textureId);
            ui->setImageTextureId("engine-image-button", textureId);
        }

        gfx->setCanvas(canvas);
        gfx->clearScreen();
        gfx->drawSolidRectRGBA(0.f, 0.f, float(canvas->getWidth()), float(canvas->getHeight()), 0.92f, 0.06f, 0.74f,
                               1.f);
        gfx->setCanvas(nullptr);

        RenderSystem::render(*gfx);
        ui->dispatchEvents();

        SDL_Event event;
        while (SDL_PollEvent(&event)) ui->processEvent(&event);
    }

    int magentaPixels = 0;
    for (int y = 0; y < gfx->getHeight(); y += 3) {
        for (int x = 0; x < gfx->getWidth(); x += 3) {
            const Color pixel = gfx->getPixel(x, y);
            REQUIRE(std::isfinite(pixel.r));
            if (pixel.r > 0.65f && pixel.g < 0.25f && pixel.b > 0.45f) ++magentaPixels;
        }
    }
    CHECK_GT(magentaPixels, 1000);

    ui->setImageTextureId("engine-image", 0);
    ui->setImageTextureId("engine-image-button", 0);
    ui->unregisterTexture(textureId);
    gfx->setScreenReadbackEnabled(false);
    win->close();
}

// ui_texture_order.cpp
TEST_CASE("UI.textureBridge.preservesPainterOrderAndClipping") {
    auto *win = eve::window::Window::create();
    auto *gfx = eve::graphics::Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings settings;
    settings.width  = 320;
    settings.height = 240;
    REQUIRE(win->setWindowSettings(settings));
    auto *native = dynamic_cast<eve::window::sdl::Window *>(win);
    REQUIRE(native != nullptr);
    auto backend = eve::ui::createImGuiBackend();
    REQUIRE(backend->init(static_cast<SDL_Window *>(native->getHandle()), gfx));
    const uint8_t red[]      = {255, 0, 0, 255};
    const uint8_t blue[]     = {0, 0, 255, 255};
    auto         *background = gfx->newTexture(1, 1, red);
    auto         *foreground = gfx->newTexture(1, 1, blue);
    const auto    redId      = backend->registerTexture(background);
    const auto    blueId     = backend->registerTexture(foreground);
    REQUIRE(redId != 0);
    REQUIRE(blueId != 0);
    gfx->setScreenReadbackEnabled(true);
    for (int frame = 0; frame < 4; ++frame) {
        backend->newFrame();
        ImGui::SetNextWindowPos(ImVec2(0.f, 0.f));
        ImGui::SetNextWindowSize(ImVec2(320.f, 240.f));
        ImGui::Begin("ordered textures", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
        auto      *list  = ImGui::GetWindowDrawList();
        const auto paint = [&](uint64_t id) {
            if (backend->usesQueuedTextureDraws()) {
                backend->queueTextureDraw(id, 20.f, 20.f, 180.f, 160.f, 0.f, 0.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, false);
            } else {
                list->AddImage(backend->textureHandle(id), ImVec2(20.f, 20.f), ImVec2(200.f, 180.f));
            }
        };
        paint(redId);
        list->AddRectFilled(ImVec2(30.f, 30.f), ImVec2(190.f, 170.f), IM_COL32(0, 255, 0, 255));
        list->PushClipRect(ImVec2(80.f, 80.f), ImVec2(140.f, 140.f), true);
        paint(blueId);
        list->PopClipRect();
        list->AddRectFilled(ImVec2(95.f, 95.f), ImVec2(125.f, 125.f), IM_COL32_WHITE);
        ImGui::End();
        eve::graphics::RenderSystem::render(*gfx);
        SDL_Event event;
        while (SDL_PollEvent(&event)) backend->processEvent(&event);
    }
    const auto margin = gfx->getPixel(25, 25);
    const auto middle = gfx->getPixel(50, 50);
    const auto front  = gfx->getPixel(85, 85);
    const auto last   = gfx->getPixel(110, 110);
    REQUIRE_GT(margin.r, 0.8f);
    REQUIRE_LT(margin.g, 0.2f);
    REQUIRE_LT(margin.b, 0.2f);
    REQUIRE_LT(middle.r, 0.2f);
    REQUIRE_GT(middle.g, 0.8f);
    REQUIRE_LT(middle.b, 0.2f);
    REQUIRE_LT(front.r, 0.2f);
    REQUIRE_LT(front.g, 0.2f);
    REQUIRE_GT(front.b, 0.8f);
    REQUIRE_GT(last.r, 0.8f);
    REQUIRE_GT(last.g, 0.8f);
    REQUIRE_GT(last.b, 0.8f);
    backend->unregisterTexture(redId);
    backend->unregisterTexture(blueId);
    backend->shutdown();
    gfx->releaseTexture(background);
    gfx->releaseTexture(foreground);
    gfx->setScreenReadbackEnabled(false);
    win->close();
}
}  // namespace test_ui_texture_bridge

// Contract section: voxel render contracts
#include "PrivateTestScope.h"

#include "SceneColor.h"
#include "VoxelRenderFixtures.h"
#include "voxel/Chunk.h"
#include "voxel/FaceDir.h"

namespace test_voxel_render_contracts {
using namespace eve::voxel;

namespace {
struct VoxelContractFixture {
    eve::window::Window *window = nullptr;
    Graphics            *gfx    = nullptr;
    explicit VoxelContractFixture(zeroerr::TestContext *_ZEROERR_TEST_CONTEXT) {
        // Keep initialization assertions in this context. openGfxWindow's
        // context-free REQUIRE can jump out without failing the parent case.
        REQUIRE(std::getenv("EVENGINE_TEST_NO_RENDER") == nullptr);
        const char *viewWidth  = std::getenv("EVENGINE_TEST_VIEW_W");
        const char *viewHeight = std::getenv("EVENGINE_TEST_VIEW_H");
        const int   width      = viewWidth && viewWidth[0] ? std::atoi(viewWidth) : 240;
        const int   height     = viewHeight && viewHeight[0] ? std::atoi(viewHeight) : 180;
        REQUIRE_GT(width, 0);
        REQUIRE_GT(height, 0);
        window = eve::window::Window::create();
        gfx    = Graphics::create();
        REQUIRE(window != nullptr);
        REQUIRE(gfx != nullptr);
        eve::window::WindowSettings settings;
        settings.width    = static_cast<uint16_t>(width);
        settings.height   = static_cast<uint16_t>(height);
        settings.centered = true;
        settings.vsync    = 0;
        REQUIRE(window->setWindowSettings(settings));
        hideLeftover3D();
        tinyHud(gfx);
        gfx->setScreenReadbackEnabled(true);
    }
    ~VoxelContractFixture() {
        if (window) window->close();
    }
};

glm::mat4 viewProjection(Graphics *gfx, glm::vec3 eye, glm::vec3 target) {
    const float aspect = float(gfx->getPixelWidth()) / float(gfx->getPixelHeight());
    return perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 500.f) *
           glm::lookAtRH(eye, target, glm::vec3(0, 1, 0));
}

void beginVoxelFrame(Graphics *gfx, const glm::mat4 &vp, zeroerr::TestContext *_ZEROERR_TEST_CONTEXT) {
    gfx->setBackgroundColor(Color(0.05f, 0.06f, 0.08f, 1.f));
    gfx->begin3DFrame();
    REQUIRE(gfx->had3DThisFrame());
    gfx->setMesh3DViewProj(vp);
}

Color pixelAt(Graphics *gfx, const glm::mat4 &vp, glm::vec3 position, zeroerr::TestContext *_ZEROERR_TEST_CONTEXT) {
    const glm::vec4 clip = vp * glm::vec4(position, 1.f);
    REQUIRE_GT(clip.w, 0.f);
    const int x = int((clip.x / clip.w * 0.5f + 0.5f) * float(gfx->getWidth()));
    const int y = int((clip.y / clip.w * 0.5f + 0.5f) * float(gfx->getHeight()));
    REQUIRE_GE(x, 0);
    REQUIRE_LT(x, gfx->getWidth());
    REQUIRE_GE(y, 0);
    REQUIRE_LT(y, gfx->getHeight());
    return gfx->getPixel(x, y);
}

void requireGreen(Color pixel, zeroerr::TestContext *_ZEROERR_TEST_CONTEXT) {
    REQUIRE_GT(pixel.g, 0.12f);
    REQUIRE_GT(pixel.g, pixel.r);
    REQUIRE_GT(pixel.g, pixel.b);
}
void requireBlue(Color pixel, zeroerr::TestContext *_ZEROERR_TEST_CONTEXT) {
    REQUIRE_GT(pixel.b, 0.12f);
    REQUIRE_GT(pixel.b, pixel.r);
    REQUIRE_GT(pixel.b, pixel.g);
}

void requireSubmissionRecovery(Graphics *gfx, zeroerr::TestContext *_ZEROERR_TEST_CONTEXT) {
    const uint32_t quad = PackedRect::pack(0, 0, 0, 3, 3, 0).bits;
    beginVoxelFrame(gfx, viewProjection(gfx, {1.5f, 1.5f, 10.f}, {1.5f, 1.5f, 0.f}), _ZEROERR_TEST_CONTEXT);
    gfx->drawVoxelFaceInstances(&quad, 1, 0.f, 0.f, 0.f, "posZ", nullptr, 1);
    RenderSystem::render(*gfx);
    const Color recovered = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    REQUIRE_GT(recovered.r, 0.2f);
    REQUIRE_GT(recovered.g, 0.2f);
    REQUIRE_GT(recovered.b, 0.2f);
}

static Color renderVoxelAoSample(Graphics *gfx, Texture *atlas, uint32_t ao) {
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

}  // namespace

TEST_CASE("voxel.render.faceDirAliasesDraw") {
    VoxelContractFixture fixture(_ZEROERR_TEST_CONTEXT);
    auto                *gfx   = fixture.gfx;
    Texture             *atlas = makeSolid(gfx, 220, 180, 140);
    REQUIRE(atlas != nullptr);
    VoxelWorld world;
    fillCube(&world, 0, 0, 0, 4, 1);
    world.remeshDirty();
    auto          *worldPtr   = &world;
    const uint32_t packed     = PackedRect::pack(0, 0, 0, 4, 4, 1).bits;
    const float    aspect     = float(gfx->getPixelWidth()) / float(gfx->getPixelHeight());
    const auto     projection = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f);
    struct Direction {
        const char *canonical;
        const char *alias;
        glm::vec3   eye, target, up;
    };
    const Direction directions[] = {
        {"posX", "+x", {10, 2, 2}, {1, 2, 2}, {0, 1, 0}},  {"negX", "-x", {-10, 2, 2}, {0, 2, 2}, {0, 1, 0}},
        {"posY", "+y", {2, 10, 2}, {2, 1, 2}, {0, 0, -1}}, {"negY", "-y", {2, -10, 2}, {2, 0, 2}, {0, 0, 1}},
        {"posZ", "+z", {2, 2, 10}, {2, 2, 1}, {0, 1, 0}},  {"negZ", "-z", {2, 2, -10}, {2, 2, 0}, {0, 1, 0}},
    };
    auto checkDirection = [gfx, atlas, projection, packed, worldPtr](const Direction      &direction,
                                                                     zeroerr::TestContext *_ZEROERR_TEST_CONTEXT) {
        Color canonical;
        for (const char *name : {direction.canonical, direction.alias}) {
            gfx->setBackgroundColor(Color(0.04f, 0.05f, 0.06f, 1.f));
            gfx->begin3DFrame();
            REQUIRE(gfx->had3DThisFrame());
            gfx->setMesh3DViewProj(projection * glm::lookAtRH(direction.eye, direction.target, direction.up));
            gfx->drawVoxelFaceInstances(&packed, 1, 0.f, 0.f, 0.f, name, atlas, 1);
            RenderSystem::render(*gfx);
            const Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
            REQUIRE(luma(mid) > 0.08f);
            if (name == direction.canonical)
                canonical = mid;
            else {
                REQUIRE(std::fabs(mid.r - canonical.r) < 0.01f);
                REQUIRE(std::fabs(mid.g - canonical.g) < 0.01f);
                REQUIRE(std::fabs(mid.b - canonical.b) < 0.01f);
            }
        }
        // Also exercise VoxelWorld's enum-to-face submission for every axis.
        const glm::mat4 vp = projection * glm::lookAtRH(direction.eye, direction.target, direction.up);
        worldPtr->selectVisible(&vp[0][0], direction.eye.x, direction.eye.y, direction.eye.z, 100.f, true);
        REQUIRE_GT(worldPtr->getVisibleBatchCount(), 0);
        beginVoxelFrame(gfx, vp, _ZEROERR_TEST_CONTEXT);
        gfx->setMesh3DCameraPos(direction.eye);
        worldPtr->drawVisible(gfx, atlas, 1);
        RenderSystem::render(*gfx);
        const Color automatic = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
        REQUIRE(std::fabs(automatic.r - canonical.r) < 0.02f);
        REQUIRE(std::fabs(automatic.g - canonical.g) < 0.02f);
        REQUIRE(std::fabs(automatic.b - canonical.b) < 0.02f);
    };
    SUB_CASE("+X") { checkDirection(directions[0], _ZEROERR_TEST_CONTEXT); };
    SUB_CASE("-X") { checkDirection(directions[1], _ZEROERR_TEST_CONTEXT); };
    SUB_CASE("+Y") { checkDirection(directions[2], _ZEROERR_TEST_CONTEXT); };
    SUB_CASE("-Y") { checkDirection(directions[3], _ZEROERR_TEST_CONTEXT); };
    SUB_CASE("+Z") { checkDirection(directions[4], _ZEROERR_TEST_CONTEXT); };
    SUB_CASE("-Z") { checkDirection(directions[5], _ZEROERR_TEST_CONTEXT); };
}

TEST_CASE("voxel.render.visibleWorldSubmission") {
    VoxelContractFixture fixture(_ZEROERR_TEST_CONTEXT);
    auto                *gfx = fixture.gfx;
    SUB_CASE("voxel.render.faceCullChangesCoverage") {
        std::unique_ptr<VoxelWorld> world(new VoxelWorld());
        for (int z = 0; z < 8; ++z)
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x) world->setVoxel(x, y, z, 1);
        world->remeshDirty();
        Texture *atlas = makeSolid(gfx, 200, 200, 210);

        // Camera outside the +X chunk boundary; with face cull, back faces dropped — still should see the +X wall.
        const glm::vec3 eye(48.f, 4.f, 4.f);
        const glm::vec3 target(4.f, 4.f, 4.f);

        renderVoxelFrame(gfx, world.get(), atlas, 1, eye, target, 200.f, true);
        REQUIRE(world->getVisibleBatchCount() >= 1);
        bool sawPosX = false, sawNegX = false;
        for (int i = 0; i < world->getVisibleBatchCount(); ++i) {
            if (world->getVisibleBatch(i).dir == FaceDir::PosX) sawPosX = true;
            if (world->getVisibleBatch(i).dir == FaceDir::NegX) sawNegX = true;
        }
        REQUIRE(sawPosX);
        REQUIRE(!sawNegX);

        Color withCull = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);

        renderVoxelFrame(gfx, world.get(), atlas, 1, eye, target, 200.f, false);
        REQUIRE(world->getVisibleRectCount() >= 6);
        Color noCull = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);

        // Both frames should show geometry (not pure background).
        REQUIRE(luma(withCull) > 0.06f);
        REQUIRE(luma(noCull) > 0.06f);
    };
    SUB_CASE("voxel.render.rangeCullDarkensDistant") {
        std::unique_ptr<VoxelWorld> world(new VoxelWorld());
        for (int z = 0; z < 4; ++z)
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 4; ++x) world->setVoxel(x + 64, y, z, 1);
        world->remeshDirty();
        Texture        *atlas = makeSolid(gfx, 240, 240, 250);
        const glm::vec3 eye(66.f, 2.f, 20.f);
        const glm::vec3 target(66.f, 2.f, 2.f);

        renderVoxelFrame(gfx, world.get(), atlas, 1, eye, target, 200.f, true);
        Color nearEnough = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
        REQUIRE(luma(nearEnough) > 0.06f);

        renderVoxelFrame(gfx, world.get(), atlas, 1, eye, target, 5.f, true);
        Color culled = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
        REQUIRE(luma(culled) < luma(nearEnough));
        REQUIRE(luma(testSceneLinearColor(culled)) < 0.22f);
    };
    SUB_CASE("voxel.render.frustumCulledChunkDark") {
        std::unique_ptr<VoxelWorld> world(new VoxelWorld());
        // Chunk far to the side — outside typical forward frustum when looking +Z.
        fillCube(world.get(), 200, 0, 200, 4, 1);
        world->remeshDirty();
        Texture *atlas = makeSolid(gfx, 255, 255, 255);

        const glm::vec3 eye(0.f, 2.f, 0.f);
        const glm::vec3 target(0.f, 2.f, 10.f);
        renderVoxelFrame(gfx, world.get(), atlas, 1, eye, target, 500.f, true);
        REQUIRE_EQ(world->getVisibleChunkCount(), 0);
        Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
        REQUIRE(luma(testSceneLinearColor(mid)) < 0.22f);
    };
    SUB_CASE("voxel.render.farChunkOrigin") {
        std::unique_ptr<VoxelWorld> world(new VoxelWorld());
        // Chunk (2,0,0) origin at x=64.
        for (int z = 0; z < 4; ++z)
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 4; ++x) world->setVoxel(64 + x, y, z, 1);
        world->remeshDirty();
        REQUIRE(world->hasChunk(2, 0, 0));

        Texture        *atlas = makeSolid(gfx, 220, 80, 200);
        const glm::vec3 eye(66.f, 2.f, 16.f);
        const glm::vec3 target(66.f, 2.f, 2.f);
        renderVoxelFrame(gfx, world.get(), atlas, 1, eye, target, 200.f, true);
        REQUIRE(world->getVisibleChunkCount() >= 1);
        Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
        REQUIRE(luma(mid) > 0.05f);
    };
    SUB_CASE("voxel.render.chunkOriginNegativeDraw") {
        std::unique_ptr<VoxelWorld> world(new VoxelWorld());
        // Local (0,0,0) in chunk (-1,0,0) → world x in [-32,-1]
        world->setVoxel(-16, 0, 0, 1);
        world->setVoxel(-15, 0, 0, 1);
        world->setVoxel(-16, 1, 0, 1);
        world->setVoxel(-15, 1, 0, 1);
        world->remeshDirty();
        Texture        *atlas = makeSolid(gfx, 220, 180, 60);
        const glm::vec3 eye(-15.5f, 1.f, 12.f);
        renderVoxelFrame(gfx, world.get(), atlas, 1, eye, glm::vec3(-15.5f, 1.f, 0.f), 100.f, true);
        REQUIRE(world->getVisibleChunkCount() >= 1);
        Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
        REQUIRE(luma(mid) > 0.04f);
    };
    SUB_CASE("voxel.render.drawVisibleMatchesManualBatches") {
        VoxelWorld world;
        fillCube(&world, 0, 0, 0, 3, 1);
        world.remeshDirty();
        Texture *atlas = makeSolid(gfx, 90, 140, 220);
        REQUIRE(atlas != nullptr);
        const glm::vec3 eye(1.5f, 1.5f, 10.f), target(1.5f, 1.5f, 1.5f);
        const glm::mat4 vp = viewProjection(gfx, eye, target);
        renderVoxelFrame(gfx, &world, atlas, 1, eye, target, 200.f, true);
        REQUIRE_GT(world.getVisibleBatchCount(), 0);
        std::vector<Color> automatic;
        for (int y = 1; y < 4; ++y)
            for (int x = 1; x < 4; ++x)
                automatic.push_back(gfx->getPixel(gfx->getWidth() * x / 4, gfx->getHeight() * y / 4));
        requireBlue(automatic[4], _ZEROERR_TEST_CONTEXT);
        beginVoxelFrame(gfx, vp, _ZEROERR_TEST_CONTEXT);
        gfx->setMesh3DCameraPos(eye);
        for (int i = 0; i < world.getVisibleBatchCount(); ++i) {
            const DrawBatch &batch = world.getVisibleBatch(i);
            gfx->drawVoxelFaceInstances(batch.packed, batch.count, batch.chunk->originX(), batch.chunk->originY(),
                                        batch.chunk->originZ(), faceDirName(batch.dir), atlas, 1);
        }
        RenderSystem::render(*gfx);
        size_t sample = 0;
        for (int y = 1; y < 4; ++y)
            for (int x = 1; x < 4; ++x) {
                const Color manual   = gfx->getPixel(gfx->getWidth() * x / 4, gfx->getHeight() * y / 4);
                const Color expected = automatic[sample++];
                REQUIRE(std::fabs(manual.r - expected.r) < 0.02f);
                REQUIRE(std::fabs(manual.g - expected.g) < 0.02f);
                REQUIRE(std::fabs(manual.b - expected.b) < 0.02f);
            }
    };
}

TEST_CASE("voxel.render.depthNearOccludesFar") {
    VoxelContractFixture fixture(_ZEROERR_TEST_CONTEXT);
    auto                *gfx = fixture.gfx;
    // Near green wall at z=2, far blue wall at z=0; camera looks down -Z.
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) {
            world->setVoxel(x, y, 0, 2);  // far blue
            world->setVoxel(x, y, 2, 1);  // near
        }
    world->remeshDirty();

    // Different atlas tiles distinguish occlusion from merely drawing any wall.
    Texture *atlas = makeTileAtlas(gfx, 4, 4);
    gfx->setTextureSampler(atlas, TextureSampler::nearest());
    const glm::vec3 eye(2.f, 2.f, 12.f);
    const glm::vec3 target(2.f, 2.f, 2.f);
    renderVoxelFrame(gfx, world.get(), atlas, 4, eye, target, 200.f, true);
    Color withNear = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    REQUIRE(withNear.g > withNear.r);
    REQUIRE(withNear.g > withNear.b);
    REQUIRE(withNear.g > 0.12f);

    // Remove near wall voxels.
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) world->setVoxel(x, y, 2, 0);
    world->remeshDirty();
    renderVoxelFrame(gfx, world.get(), atlas, 4, eye, target, 200.f, true);
    Color farOnly = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    // Removing the near wall must reveal the blue far wall.
    REQUIRE(farOnly.b > farOnly.r);
    REQUIRE(farOnly.b > farOnly.g);
    REQUIRE(farOnly.b > 0.08f);
    REQUIRE(luma(withNear) > 0.05f);
}

TEST_CASE("voxel.render.atlasBoundaryIndices") {
    VoxelContractFixture fixture(_ZEROERR_TEST_CONTEXT);
    auto                *gfx = fixture.gfx;
    struct Sample {
        int   columns;
        int   rows;
        int   tile;
        int   size;
        float eyeY;
    };
    // Index zero is valid in packed faces even though voxel zero means air.
    // Keep the first tile, a wide-atlas row edge, and the maximum packed index.
    auto checkSample = [gfx](Sample sample, zeroerr::TestContext *_ZEROERR_TEST_CONTEXT) {
        std::printf("atlas columns=%d rows=%d tile=%d\n", sample.columns, sample.rows, sample.tile);
        std::vector<uint8_t> pixels(size_t(sample.columns * sample.rows * 4), 0);
        for (size_t i = 3; i < pixels.size(); i += 4) pixels[i] = 255;
        // tilesPerRow divides both UV axes; rows is the texture's pixel height,
        // not an independently configurable number of logical tile rows.
        const int    pixelRow = (sample.tile / sample.columns) * sample.rows / sample.columns;
        const size_t target   = size_t((pixelRow * sample.columns + sample.tile % sample.columns) * 4);
        pixels[target]        = 220;
        pixels[target + 1]    = 40;
        pixels[target + 2]    = 40;
        Texture *atlas        = gfx->newTexture(sample.columns, sample.rows, pixels.data());
        REQUIRE(atlas != nullptr);
        gfx->setTextureSampler(atlas, TextureSampler::nearest());
        const uint32_t  packed = PackedRect::pack(0, 0, 0, sample.size, sample.size, sample.tile).bits;
        const float     center = float(sample.size) * 0.5f;
        const float     aspect = float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
        const glm::mat4 view =
            glm::lookAtRH(glm::vec3(center, sample.eyeY, center), glm::vec3(center, 0.f, center), glm::vec3(0, 0, -1));
        const glm::mat4 vp = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f) * view;
        gfx->setBackgroundColor(Color(0.05f, 0.06f, 0.08f, 1.f));
        gfx->begin3DFrame();
        REQUIRE(gfx->had3DThisFrame());
        gfx->setMesh3DViewProj(vp);
        gfx->drawVoxelFaceInstances(&packed, 1, 0.f, 0.f, 0.f, "posY", atlas, sample.columns);
        RenderSystem::render(*gfx);
        const Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
        REQUIRE(luma(mid) > 0.04f);
        REQUIRE(mid.r > mid.g);
        REQUIRE(mid.r > mid.b);
        REQUIRE(mid.r > 0.08f);
    };
    SUB_CASE("first packed index") { checkSample(Sample{4, 4, 0, 4, 10.f}, _ZEROERR_TEST_CONTEXT); };
    SUB_CASE("wide atlas row edge") { checkSample(Sample{32, 2, 31, 3, 8.f}, _ZEROERR_TEST_CONTEXT); };
    SUB_CASE("maximum packed index") { checkSample(Sample{16, 8, 127, 4, 10.f}, _ZEROERR_TEST_CONTEXT); };
    SUB_CASE("voxel.render.registryMultiFaceTex") {
        CubeTypeRegistry registry;
        CubeType         grass;
        grass.name = "grass";
        for (int face = 0; face < 6; ++face) grass.faceTex[face] = 0;
        grass.faceTex[int(FaceDir::PosY)] = 1;
        REQUIRE_NE(registry.add(grass), 0);
        VoxelWorld world(registry);
        for (int z = 0; z < 3; ++z)
            for (int y = 0; y < 3; ++y)
                for (int x = 0; x < 3; ++x) world.setVoxelByName(x, y, z, "grass");
        world.remeshDirty();
        Texture *atlas = makeTileAtlas(gfx, 4, 4);
        REQUIRE(atlas != nullptr);
        gfx->setTextureSampler(atlas, TextureSampler::nearest());
        const float aspect = float(gfx->getWidth()) / float(gfx->getHeight());
        for (bool fromAbove : {false, true}) {
            const glm::vec3 eye = fromAbove ? glm::vec3(1.5f, 10.f, 1.5f) : glm::vec3(1.5f, 1.5f, 10.f);
            const glm::vec3 up  = fromAbove ? glm::vec3(0, 0, -1) : glm::vec3(0, 1, 0);
            const glm::mat4 vp  = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f) *
                                 glm::lookAtRH(eye, glm::vec3(1.5f), up);
            world.selectVisible(&vp[0][0], eye.x, eye.y, eye.z, 100.f, true);
            REQUIRE_GT(world.getVisibleBatchCount(), 0);
            beginVoxelFrame(gfx, vp, _ZEROERR_TEST_CONTEXT);
            world.drawVisible(gfx, atlas, 4);
            RenderSystem::render(*gfx);
            const Color pixel = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
            if (fromAbove) {
                requireGreen(pixel, _ZEROERR_TEST_CONTEXT);
            } else {
                REQUIRE_GT(pixel.r, 0.12f);
                REQUIRE_GT(pixel.r, pixel.g);
                REQUIRE_GT(pixel.r, pixel.b);
            }
        }
    };
}

TEST_CASE("voxel.render.batchAtlasAndGrowthContract") {
    VoxelContractFixture fixture(_ZEROERR_TEST_CONTEXT);
    auto                *gfx = fixture.gfx;
    SUB_CASE("voxel.render.sameFrameBatchIsolation") {
        Texture *greenAtlas = makeSolid(gfx, 40, 220, 40);
        Texture *blueAtlas  = makeSolid(gfx, 40, 40, 220);
        REQUIRE(greenAtlas != nullptr);
        REQUIRE(blueAtlas != nullptr);
        const uint32_t  quad = PackedRect::pack(0, 0, 0, 3, 3, 0).bits;
        const glm::mat4 vp   = viewProjection(gfx, {0.f, 1.5f, 14.f}, {0.f, 1.5f, 0.f});
        beginVoxelFrame(gfx, vp, _ZEROERR_TEST_CONTEXT);
        gfx->drawVoxelFaceInstances(&quad, 1, -4.f, 0.f, 0.f, "posZ", greenAtlas, 1);
        gfx->drawVoxelFaceInstances(&quad, 1, 1.f, 0.f, 0.f, "posZ", blueAtlas, 1);
        RenderSystem::render(*gfx);
        requireGreen(pixelAt(gfx, vp, {-2.5f, 1.5f, 1.f}, _ZEROERR_TEST_CONTEXT), _ZEROERR_TEST_CONTEXT);
        requireBlue(pixelAt(gfx, vp, {2.5f, 1.5f, 1.f}, _ZEROERR_TEST_CONTEXT), _ZEROERR_TEST_CONTEXT);
    };
    SUB_CASE("voxel.render.frameSlotAtlasUpdates") {
        Texture *green = makeTileAtlas(gfx, 4, 4);
        Texture *blue  = makeSolid(gfx, 40, 40, 230);
        REQUIRE(green != nullptr);
        REQUIRE(blue != nullptr);
        VoxelWorld world;
        fillCube(&world, 0, 0, 0, 4, 1);
        world.remeshDirty();
        // Eight consecutive presents revisit the supported backend frame slots.
        // Color and atlas layout both change; brightness alone cannot prove rebinding.
        for (int frame = 0; frame < 8; ++frame) {
            const bool useBlue = (frame / 2) % 2 != 0;
            renderVoxelFrame(gfx, &world, useBlue ? blue : green, useBlue ? 1 : 4, {2.f, 2.f, 14.f}, {2.f, 2.f, 2.f},
                             100.f, true);
            const Color pixel = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
            if (useBlue)
                requireBlue(pixel, _ZEROERR_TEST_CONTEXT);
            else
                requireGreen(pixel, _ZEROERR_TEST_CONTEXT);
        }
    };
    SUB_CASE("voxel.render.instanceBufferGrowth") {
        Texture *atlas = makeTileAtlas(gfx, 4, 4);
        REQUIRE(atlas != nullptr);
        gfx->setTextureSampler(atlas, TextureSampler::nearest());
        const uint32_t        first = PackedRect::pack(0, 0, 0, 3, 3, 1).bits;
        std::vector<uint32_t> many;
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x) many.push_back(PackedRect::pack(x, y, 0, 1, 1, 2).bits);
        const glm::mat4 vp = viewProjection(gfx, {7.f, 8.f, 36.f}, {7.f, 8.f, 0.f});
        // Warm both draw slots with small buffers, then grow the second slot on
        // reuse. Two draws within one frame use distinct slots, not one buffer.
        for (int frame = 0; frame < 16; ++frame) {
            const bool grow = frame >= 8;
            beginVoxelFrame(gfx, vp, _ZEROERR_TEST_CONTEXT);
            gfx->drawVoxelFaceInstances(&first, 1, -6.f, 6.f, 0.f, "posZ", atlas, 4);
            gfx->drawVoxelFaceInstances(many.data(), grow ? int(many.size()) : 1, 0.f, 0.f, 0.f, "posZ", atlas, 4);
            RenderSystem::render(*gfx);
            // The earlier green batch must survive the later blue submission.
            requireGreen(pixelAt(gfx, vp, {-4.5f, 7.5f, 1.f}, _ZEROERR_TEST_CONTEXT), _ZEROERR_TEST_CONTEXT);
            if (grow) requireBlue(pixelAt(gfx, vp, {8.f, 8.f, 1.f}, _ZEROERR_TEST_CONTEXT), _ZEROERR_TEST_CONTEXT);
        }
    };
}


TEST_CASE("voxel.render.remeshPublication") {
    VoxelContractFixture fixture(_ZEROERR_TEST_CONTEXT);
    auto                *gfx = fixture.gfx;

    Texture *atlas = makeTileAtlas(gfx, 4, 4);
    REQUIRE(atlas != nullptr);
    gfx->setTextureSampler(atlas, TextureSampler::nearest());
    VoxelWorld      world;
    const glm::vec3 eye(2.f, 2.f, 14.f), target(2.f, 2.f, 2.f);
    auto            draw = [&]() {
        world.remeshDirty();
        renderVoxelFrame(gfx, &world, atlas, 4, eye, target, 100.f, true);
        return gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    };
    const Color empty = draw();
    REQUIRE_EQ(world.getVisibleBatchCount(), 0);
    fillCube(&world, 0, 0, 0, 4, 1);
    requireGreen(draw(), _ZEROERR_TEST_CONTEXT);
    REQUIRE_GT(world.getVisibleBatchCount(), 0);
    fillCube(&world, 0, 0, 0, 4, 2);
    requireBlue(draw(), _ZEROERR_TEST_CONTEXT);
    world.clear();
    const Color cleared = draw();
    REQUIRE_EQ(world.getVisibleBatchCount(), 0);
    REQUIRE(std::fabs(cleared.r - empty.r) < 0.01f);
    REQUIRE(std::fabs(cleared.g - empty.g) < 0.01f);
    REQUIRE(std::fabs(cleared.b - empty.b) < 0.01f);
}

TEST_CASE("voxel.render.vertexAoDarkensPixels") {
    VoxelContractFixture fixture(_ZEROERR_TEST_CONTEXT);
    auto                *gfx   = fixture.gfx;
    Texture             *atlas = makeSolid(gfx, 220, 220, 220);

    const Color bright = renderVoxelAoSample(gfx, atlas, 0xFFu);
    const Color dark   = renderVoxelAoSample(gfx, atlas, 0x00u);
    REQUIRE(luma(bright) > 0.25f);
    // AO multiplies linear radiance; ACES and sRGB compress the displayed ratio.
    REQUIRE(luma(testSceneLinearColor(dark)) < luma(testSceneLinearColor(bright)) * 0.6f);
}

TEST_CASE("voxel.render.submissionValidationAndRecovery") {
    VoxelContractFixture fixture(_ZEROERR_TEST_CONTEXT);
    auto                *gfx = fixture.gfx;
    SUB_CASE("voxel.render.requiresBegin3DFrame") {
        uint32_t dummy = PackedRect::pack(0, 0, 0, 1, 1, 1).bits;
        bool     threw = false;
        try {
            gfx->drawVoxelFaceInstances(&dummy, 1, 0.f, 0.f, 0.f, "posZ", nullptr, 1);
        } catch (...) {
            threw = true;
        }
        // WebGPU ignores a missing begin; Vulkan throws. Neither may prevent
        // a subsequent correctly begun frame from rendering.
        REQUIRE_EQ(threw, gfx->getBackendName() != "webgpu");
        requireSubmissionRecovery(gfx, _ZEROERR_TEST_CONTEXT);
    };
    SUB_CASE("voxel.render.invalidFaceDirThrows") {
        const glm::mat4 vp(1.f);
        gfx->begin3DFrame();
        REQUIRE(gfx->had3DThisFrame());
        gfx->setMesh3DViewProj(vp);
        uint32_t dummy = PackedRect::pack(0, 0, 0, 1, 1, 0).bits;
        bool     threw = false;
        try {
            gfx->drawVoxelFaceInstances(&dummy, 1, 0.f, 0.f, 0.f, "sideways", nullptr, 1);
        } catch (...) {
            threw = true;
        }
        REQUIRE(threw);
        // Recover present path.
        RenderSystem::render(*gfx);
        requireSubmissionRecovery(gfx, _ZEROERR_TEST_CONTEXT);
    };
    SUB_CASE("voxel.render.emptyInstanceInputsAreNoOps") {
        Texture *white = makeSolid(gfx, 255, 255, 255);
        REQUIRE(white != nullptr);
        const uint32_t  packed = PackedRect::pack(0, 0, 0, 1, 1, 0).bits;
        const float     aspect = float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
        const glm::mat4 vp     = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f) *
                             glm::lookAtRH(glm::vec3(0.5f, 0.5f, 5.f), glm::vec3(0.5f, 0.5f, 1.f), glm::vec3(0, 1, 0));
        struct EmptyInput {
            const uint32_t *data;
            int             count;
            const char     *direction;
            Texture        *atlas;
        };
        const EmptyInput inputs[] = {{nullptr, 0, "posZ", nullptr},
                                     {nullptr, 10, "posY", white},
                                     {nullptr, 0, "posY", white},
                                     {&packed, 0, "posZ", nullptr}};
        const Color      background(0.1f, 0.1f, 0.12f, 1.f);
        // Compare against the actual cleared frame, including the backend's display transform.
        gfx->setBackgroundColor(background);
        gfx->begin3DFrame();
        REQUIRE(gfx->had3DThisFrame());
        RenderSystem::render(*gfx);
        const Color cleared = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
        for (const auto &input : inputs) {
            std::printf("empty input null=%d count=%d face=%s atlas=%d\n", input.data == nullptr, input.count,
                        input.direction, input.atlas != nullptr);
            gfx->setBackgroundColor(background);
            gfx->begin3DFrame();
            REQUIRE(gfx->had3DThisFrame());
            gfx->setMesh3DViewProj(vp);
            gfx->drawVoxelFaceInstances(input.data, input.count, 0.f, 0.f, 0.f, input.direction, input.atlas, 1);
            RenderSystem::render(*gfx);
            const Color empty = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
            REQUIRE(std::fabs(empty.r - cleared.r) < 0.01f);
            REQUIRE(std::fabs(empty.g - cleared.g) < 0.01f);
            REQUIRE(std::fabs(empty.b - cleared.b) < 0.01f);

            // A no-op must not prevent a subsequent valid draw in the same frame.
            gfx->begin3DFrame();
            REQUIRE(gfx->had3DThisFrame());
            gfx->setMesh3DViewProj(vp);
            gfx->drawVoxelFaceInstances(input.data, input.count, 0.f, 0.f, 0.f, input.direction, input.atlas, 1);
            gfx->drawVoxelFaceInstances(&packed, 1, 0.f, 0.f, 0.f, "posZ", nullptr, 1);
            RenderSystem::render(*gfx);
            const Color drawn = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
            REQUIRE(drawn.r > 0.2f);
            REQUIRE(drawn.g > 0.2f);
            REQUIRE(drawn.b > 0.2f);
        }
        requireSubmissionRecovery(gfx, _ZEROERR_TEST_CONTEXT);
    };
    SUB_CASE("voxel.render.nullAtlasUsesWhite") {
        {
            constexpr int size = 4;
            std::printf("null atlas cube size=%d\n", size);
            std::unique_ptr<VoxelWorld> world(new VoxelWorld());
            fillCube(world.get(), 0, 0, 0, size, 1);
            world->remeshDirty();
            const float center = float(size) * 0.5f;
            renderVoxelFrame(gfx, world.get(), nullptr, 1, glm::vec3(center, center, float(size * 2 + 4)),
                             glm::vec3(center), 100.f, true);
            const Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
            REQUIRE(luma(mid) > 0.05f);
            REQUIRE(mid.r > 0.2f);
            REQUIRE(mid.g > 0.2f);
            REQUIRE(mid.b > 0.2f);
        }
        requireSubmissionRecovery(gfx, _ZEROERR_TEST_CONTEXT);
    };
    SUB_CASE("voxel.render.tilesPerRowZeroClamped") {
        uint32_t        packed = PackedRect::pack(0, 0, 0, 4, 4, 1).bits;
        Texture        *atlas  = makeSolid(gfx, 210, 90, 50);
        const float     aspect = float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
        const glm::vec3 eye(2.f, 8.f, 2.f);
        const glm::mat4 view = glm::lookAtRH(eye, glm::vec3(2.f, 0.f, 2.f), glm::vec3(0, 0, -1));
        const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f);
        const glm::mat4 vp   = proj * view;

        gfx->setBackgroundColor(Color(0.05f, 0.06f, 0.08f, 1.f));
        gfx->begin3DFrame();
        REQUIRE(gfx->had3DThisFrame());
        {
            gfx->setMesh3DViewProj(vp);
            // tilesPerRow <= 0 should clamp to 1 and still sample the solid atlas.
            gfx->drawVoxelFaceInstances(&packed, 1, 0.f, 0.f, 0.f, "posY", atlas, 0);
        }
        RenderSystem::render(*gfx);
        Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
        REQUIRE_GT(mid.r, 0.12f);
        REQUIRE_GT(mid.r, mid.g);
        REQUIRE_GT(mid.r, mid.b);
        requireSubmissionRecovery(gfx, _ZEROERR_TEST_CONTEXT);
    };
}

}  // namespace test_voxel_render_contracts

// Contract section: water mesh
#include "PrivateTestScope.h"

#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>
#include "Fixtures.h"
#include "graphics/WaterPlanarReflection.h"
#include "graphics/WaterReflectionMasker.h"
#include "graphics/WaterStyleConfig.h"
#include "graphics/WaterSystem.h"
#include "graphics/WaterUnderwaterEffects.h"
#include "physics/Shape3D.h"
// water_mesh.cpp
namespace test_water_mesh {
// water_mesh.cpp
using namespace eve::graphics;


TEST_CASE("graphics.WaterGradient.bakesAndPreservesTextureOnFailure") {
    auto *window   = eve::window::Window::create();
    auto *graphics = eve::graphics::Graphics::create();
    REQUIRE(window != nullptr);
    REQUIRE(graphics != nullptr);
    eve::window::WindowSettings windowSettings;
    windowSettings.width  = 64;
    windowSettings.height = 64;
    REQUIRE(window->setWindowSettings(windowSettings));
    Water *water = graphics->newWater();
    REQUIRE(water != nullptr);
    WaterDepthGradient gradient;
    REQUIRE(static_cast<bool>(gradient.addColorStop(0.0F, 0.05F, 0.5F, 0.7F)));
    REQUIRE(static_cast<bool>(gradient.addColorStop(1.0F, 0.0F, 0.04F, 0.12F)));
    REQUIRE(static_cast<bool>(gradient.addAlphaStop(0.0F, 0.35F)));
    REQUIRE(static_cast<bool>(gradient.addAlphaStop(1.0F, 1.0F)));
    auto baked = water->setDepthGradient(gradient, 8);
    REQUIRE(static_cast<bool>(baked));
    CHECK(baked.value() == 64);
    auto *published = water->getDepthGradientTexture();
    REQUIRE(published != nullptr);
    CHECK(published->getWidth() == 8);
    CHECK(published->getHeight() == 8);
    auto failed = water->setDepthGradient(gradient, 1);
    CHECK(!failed);
    CHECK(water->getDepthGradientTexture() == published);
    delete water;
    window->close();
}

// water_planar_reflection.cpp
using namespace eve::graphics;


// water_reflection_masker.cpp
using namespace eve::graphics;


TEST_CASE("graphics.waterReflectionMasker.bindsRealVm") {
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeWaterPlanarReflectionBindings(table);
    exposeWaterReflectionMaskerBindings(table);
    vm.run(vm.compileSource(R"(
      local m=eve.WaterReflectionMasker();local s=eve.WaterPlanarReflectionSettings();s.enabled=true;
      assert(m.configure(0,0.35,1.0,true).ok);m.clearMask();
      local r=m.evaluate(0.0,0.0,0.0,0.0,100.0,100.0,s);assert(r.ok && r.value==2);
      assert(!m.getEnabled() && !m.getHeightFeaturesEnabled() && !s.enabled);
    )"));
}

// water_style_config.cpp
using eve::DiagnosticCode;
using eve::graphics::WaterStyleConfig;


// water_system.cpp
using namespace eve::graphics;


// water_underwater_effects.cpp
using namespace eve::graphics;

TEST_CASE("graphics.WaterUnderwaterDisableTrigger.consumesRealBox3DEvents") {
    constexpr int                          zoneTag   = 8101;
    constexpr int                          playerTag = 8102;
    auto                                  *physics   = eve::physics::Physics::create();
    std::unique_ptr<eve::physics::World3D> world(physics->newWorld3D(0.0F, 0.0F, 0.0F, false));
    auto                                  *zone   = world->newBody("static", 0.0F, 0.0F, 0.0F);
    auto                                  *sensor = zone->newBoxShape(4.0F, 4.0F, 4.0F);
    sensor->setTag(zoneTag);
    sensor->setSensor(true);
    auto *player = world->newBody("dynamic", 0.0F, 0.0F, 0.0F);
    player->newSphereShape(0.5F)->setTag(playerTag);

    WaterUnderwaterDisableTriggerState state;
    world->update(1.0F / 60.0F);
    REQUIRE_EQ(world->getBeginTriggerCount(), 1);
    WaterUnderwaterTriggerEvent enter{world->getBeginTriggerSensorShapeTag(0), world->getBeginTriggerVisitorShapeTag(0),
                                      true, false};
    REQUIRE(static_cast<bool>(advanceWaterUnderwaterDisableTrigger(state, enter, zoneTag, playerTag)));
    CHECK(!state.effectsEnabled);

    const auto                  before = state;
    WaterUnderwaterTriggerEvent malformed{zoneTag, playerTag, true, true};
    CHECK(!advanceWaterUnderwaterDisableTrigger(state, malformed, zoneTag, playerTag));
    CHECK(state.effectsEnabled == before.effectsEnabled);
    WaterUnderwaterTriggerEvent unrelated{zoneTag, 9999, false, true};
    REQUIRE(static_cast<bool>(advanceWaterUnderwaterDisableTrigger(state, unrelated, zoneTag, playerTag)));
    CHECK(!state.effectsEnabled);

    player->setPosition(10.0F, 0.0F, 0.0F);
    world->update(1.0F / 60.0F);
    REQUIRE_EQ(world->getEndTriggerCount(), 1);
    WaterUnderwaterTriggerEvent exit{world->getEndTriggerSensorShapeTag(0), world->getEndTriggerVisitorShapeTag(0),
                                     false, true};
    REQUIRE(static_cast<bool>(advanceWaterUnderwaterDisableTrigger(state, exit, zoneTag, playerTag)));
    CHECK(state.effectsEnabled);
}


TEST_CASE("graphics.WaterUnderwaterFog.appliesAndRestoresVolumetricProvider") {
    eve::window::Window *window   = nullptr;
    Graphics            *graphics = nullptr;
    openGfxWindow(window, graphics);
    auto *volumetric = graphics->newVolumetric();
    REQUIRE(volumetric != nullptr);

    WaterUnderwaterOutput output;
    output.isUnderwater = true;
    output.fogColor     = {0.12F, 0.32F, 0.48F};
    output.fogDensity   = 0.045F;
    output.fogStart     = -4.0F;
    output.fogEnd       = 45.0F;
    output.fogHeight    = 12.0F;
    WaterSurfaceFogSnapshot surface;
    surface.color   = {0.5F, 0.6F, 0.7F};
    surface.density = 0.006F;

    REQUIRE(static_cast<bool>(applyWaterUnderwaterFog(volumetric, output, surface)));
    CHECK(volumetric->getMode() == "fog");
    CHECK(volumetric->getFloat("fogR") == 0.12F);
    CHECK(volumetric->getFloat("fogG") == 0.32F);
    CHECK(volumetric->getFloat("density") == 0.045F);

    output.isUnderwater = false;
    REQUIRE(static_cast<bool>(applyWaterUnderwaterFog(volumetric, output, surface)));
    CHECK(volumetric->getFloat("fogR") == 0.5F);
    CHECK(volumetric->getFloat("fogB") == 0.7F);
    CHECK(volumetric->getFloat("density") == 0.006F);

    auto *camera                        = Camera3D::createCamera();
    output.isUnderwater                 = true;
    output.postFx                       = true;
    output.postExposure                 = -1.25F;
    output.postColor                    = {0.2F, 0.6F, 0.8F};
    output.transitionFx                 = true;
    output.transitionVignette           = 0.25F;
    output.transitionVignetteSmoothness = 0.8F;
    output.transitionLensDistortion     = 0.252F;
    output.transitionLensScale          = 1.02F;
    output.transitionLift               = {-0.02F, -0.01F, 0.005F};
    output.transitionInverseGamma       = {1.35F, 1.36F, 1.27F};
    output.transitionGain               = {0.67F, 0.69F, 0.72F};
    output.transitionColorFilter        = {0.6F, 0.6F, 0.6F};
    WaterSurfacePostFxSnapshot surfacePostFx;
    surfacePostFx.exposureEv = 0.4F;
    surfacePostFx.color      = {0.9F, 0.8F, 0.7F};
    REQUIRE(static_cast<bool>(applyWaterUnderwaterPostFx(graphics, camera, output, surfacePostFx)));
    CHECK(camera->getExposure() == -1.25F);
    CHECK(graphics->getSceneColorFilter() == glm::vec3(0.12F, 0.36F, 0.48F));
    CHECK(graphics->getSceneTransitionVignette() == 0.25F);
    CHECK(graphics->getSceneTransitionVignetteSmoothness() == 0.8F);
    CHECK(graphics->getSceneTransitionLensDistortion() == 0.252F);
    CHECK(graphics->getSceneTransitionLensScale() == 1.02F);
    CHECK(graphics->getSceneLift() == output.transitionLift);
    CHECK(graphics->getSceneInverseGamma() == output.transitionInverseGamma);
    CHECK(graphics->getSceneGain() == output.transitionGain);
    output.isUnderwater = false;
    REQUIRE(static_cast<bool>(applyWaterUnderwaterPostFx(graphics, camera, output, surfacePostFx)));
    CHECK(camera->getExposure() == 0.4F);
    CHECK(graphics->getSceneColorFilter() == surfacePostFx.color);
    CHECK(graphics->getSceneTransitionVignette() == 0.0F);
    CHECK(graphics->getSceneTransitionVignetteSmoothness() == 0.2F);
    CHECK(graphics->getSceneTransitionLensDistortion() == 0.0F);
    CHECK(graphics->getSceneTransitionLensScale() == 1.0F);
    CHECK(graphics->getSceneLift() == glm::vec3(0.0F));
    CHECK(graphics->getSceneInverseGamma() == glm::vec3(1.0F));
    CHECK(graphics->getSceneGain() == glm::vec3(1.0F));

    surfacePostFx.color.r = -1.0F;
    CHECK(!applyWaterUnderwaterPostFx(graphics, camera, output, surfacePostFx));
    CHECK(camera->getExposure() == 0.4F);
    CHECK(graphics->getSceneColorFilter() == glm::vec3(0.9F, 0.8F, 0.7F));

    auto *horizon  = Renderable3D::create();
    output.horizon = true;
    REQUIRE(static_cast<bool>(applyWaterUnderwaterHorizon(horizon, output)));
    CHECK(horizon->getVisible());
    output.horizon = false;
    REQUIRE(static_cast<bool>(applyWaterUnderwaterHorizon(horizon, output)));
    CHECK(!horizon->getVisible());

    WaterSurfaceMaterialSnapshot surfaceMaterial;
    surfaceMaterial.color          = {0.12F, 0.34F, 0.56F};
    surfaceMaterial.alpha          = 0.8F;
    output.isUnderwater            = true;
    output.underwaterMaterialColor = {0.7F, 0.8F, 0.9F};
    REQUIRE(static_cast<bool>(applyWaterUnderwaterMaterial(horizon, output, surfaceMaterial)));
    CHECK(horizon->getTintR() == 0.7F);
    CHECK(horizon->getTintG() == 0.8F);
    output.isUnderwater = false;
    REQUIRE(static_cast<bool>(applyWaterUnderwaterMaterial(horizon, output, surfaceMaterial)));
    CHECK(horizon->getTintR() == 0.12F);
    CHECK(horizon->getTintB() == 0.56F);
    surfaceMaterial.alpha = 2.0F;
    CHECK(!applyWaterUnderwaterMaterial(horizon, output, surfaceMaterial));
    CHECK(horizon->getTintR() == 0.12F);

    auto *cookie = graphics->newCanvas(8, 8);
    REQUIRE(cookie != nullptr);
    CHECK(static_cast<bool>(
        volumetric->projectDirectionalCookie(graphics, cookie->getTexture(), cookie->getTexture(), 15.0F, 0.5F)));
    CHECK(!volumetric->projectDirectionalCookie(graphics, cookie->getTexture(), cookie->getTexture(), 0.0F, 0.5F));

    surface.end = surface.start;
    CHECK(!applyWaterUnderwaterFog(volumetric, output, surface));
    CHECK(volumetric->getFloat("density") == 0.006F);
    delete volumetric;
    window->close();
}
}  // namespace test_water_mesh

// Contract section: weather
#include "PrivateTestScope.h"

#include "weather/PcgInteriorWeatherVolume.h"
#include "weather/Weather.h"
// weather.cpp
namespace test_weather {
using namespace eve::weather;

TEST_CASE("weather.backendShaderInitialization") {
    auto *window   = eve::window::Window::create();
    auto *graphics = eve::graphics::Graphics::create();
    REQUIRE(window != nullptr);
    REQUIRE(graphics != nullptr);
    eve::window::WindowSettings settings;
    settings.width    = 128;
    settings.height   = 128;
    settings.centered = true;
    settings.vsync    = 0;
    REQUIRE(window->setWindowSettings(settings));

    Weather weather;
    weather.init(graphics);
    // The renderer reads the material's albedo when a material is present.
    // Attaching the texture only to Renderable3D silently samples white.
    int  texturedFields = 0;
    auto fields         = ecs::View<eve::graphics::Renderable3D, eve::graphics::Renderable3D::MeshRenderer>();
    for (auto it = fields.begin(); it != fields.end(); ++it) {
        auto [renderer] = *it;
        if (!renderer->material) continue;
        auto *texture = renderer->material->getAlbedoTexture();
        if (!texture) continue;
        CHECK(texture->getPixelWidth() > 1);
        CHECK(texture->getPixelHeight() > 1);
        CHECK(!renderer->material->getCastShadow());
        ++texturedFields;
    }
    CHECK(texturedFields == 2);
    weather.setPreset("storm");
    weather.strike();
    weather.update(1.f / 60.f, graphics);
    CHECK(weather.getIntensity() >= 0.f);
    CHECK(weather.getFlash() >= 0.f);
    window->close();
}


TEST_CASE("weather.pcgInteriorVolumeControlsRealPrecipitationRenderables") {
    auto *window   = eve::window::Window::create();
    auto *graphics = eve::graphics::Graphics::create();
    REQUIRE(window != nullptr);
    REQUIRE(graphics != nullptr);
    eve::window::WindowSettings settings;
    settings.width  = 96;
    settings.height = 96;
    REQUIRE(window->setWindowSettings(settings));

    Weather weather;
    weather.setPreset("rain");
    weather.setIntensity(1.f);
    weather.update(1.f, graphics);
    auto visibleWeatherFields = [] {
        int  count  = 0;
        auto fields = ecs::View<eve::graphics::Renderable3D, eve::graphics::Renderable3D::MeshRenderer>();
        for (auto it = fields.begin(); it != fields.end(); ++it) {
            auto [renderer] = *it;
            if (renderer->visible) ++count;
        }
        return count;
    };
    CHECK(visibleWeatherFields() == 1);

    PcgInteriorWeatherVolume volume;
    REQUIRE(volume.configureBox(0.f, 0.f, 0.f, 20.f, 20.f, 20.f, 1, 2, 0).ok());
    REQUIRE(weather.applyInteriorVolume(volume, 0.f, 0.f, 0.f).ok());
    weather.update(1.f / 60.f, graphics);
    CHECK(visibleWeatherFields() == 0);
    REQUIRE(weather.applyInteriorVolume(volume, 20.f, 0.f, 0.f).ok());
    weather.update(1.f / 60.f, graphics);
    CHECK(visibleWeatherFields() == 1);
    window->close();
}
}  // namespace test_weather
