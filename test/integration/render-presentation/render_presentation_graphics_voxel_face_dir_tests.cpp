// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "default": "render-window",
  "cases": {
    "physics-backend": [
      "softbody.cloth.collidesWithWorld",
      "softbody.cloth.momentumExchangeScalesWithMass",
      "softbody.cloth3d.collidesWithWorld3D",
      "softbody.cloth3d.momentumExchangeScalesWithMass",
      "softbody.fluid.emitAndSettle",
      "softbody.fluid.interactRepels",
      "graphics.WaterUnderwaterDisableTrigger.consumesRealBox3DEvents"
    ],
    "script-binding": [
      "graphics.waterReflectionMasker.bindsRealVm"
    ]
  },
  "module": "filesystem",
  "modules": [
    "filesystem",
    "graphics",
    "physics",
    "physics_softbody",
    "spatial",
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
    "spatial.cpp",
    "stylize.cpp",
    "stylize_anime.cpp",
    "stylize_mesh_particles.cpp",
    "ui_texture_bridge.cpp",
    "voxel_face_dir.cpp",
    "voxel_render_atlas.cpp",
    "water_mesh.cpp",
    "weather.cpp"
  ]
}
*/
// clang-format on

#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_shader)
#include "PrivateTestScope.h"
#include <SDL2/SDL.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>
#include "common/Exception.h"
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
#include "graphics/Light.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/Outline.h"
#include "graphics/Quad.h"
#include "graphics/RenderControl.h"
#include "graphics/RenderSystem.h"
#include "graphics/ScreenSpaceReflection.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
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
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_shader_reload)
#include "PrivateTestScope.h"
#include <cstdint>
#include <vector>
#include "common/Diagnostic.h"
#include "graphics/Graphics.h"
#include "graphics/Shader.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
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
    if (!window || !graphics || !window->setWindowSettings(settings)) return nullptr;
    return graphics;
}

std::vector<uint32_t> customFragment() { return {custom2d_frag_spv, custom2d_frag_spv + custom2d_frag_spv_count}; }

}  // namespace

TEST_CASE("graphics.shaderReload.successKeepsStableFacade") {
    Graphics *graphics = initializedGraphics();
    REQUIRE(graphics != nullptr);
    auto    fragment = customFragment();
    Shader *shader   = graphics->newShaderFromSpv({}, fragment);
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
    auto    fragment = customFragment();
    Shader *shader   = graphics->newShaderFromSpv({}, fragment);
    REQUIRE(shader != nullptr);
    void *const originalGpuHandle = shader->gpuHandle;
    const auto  originalFragment  = shader->fragmentSpirv();

    std::vector<uint32_t> invalid  = {0xDEADBEEF, 1, 2, 3};
    auto                  replaced = graphics->replaceShaderFromSpv(*shader, {}, invalid);
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
    auto   replaced = graphics->replaceShaderFromSpv(foreign, {}, customFragment());
    REQUIRE(!replaced.ok());
    REQUIRE(!replaced.diagnostics().empty());
    CHECK_EQ(replaced.diagnostics().front().code(), DiagnosticCode::StaleHandle);
}

TEST_CASE("graphics.shaderReload.glslFailureKeepsLastGoodPipeline") {
    Graphics *graphics = initializedGraphics();
    REQUIRE(graphics != nullptr);
    auto    fragment = customFragment();
    Shader *shader   = graphics->newShaderFromSpv({}, fragment);
    REQUIRE(shader != nullptr);
    void *const originalGpuHandle = shader->gpuHandle;
    const auto  originalFragment  = shader->fragmentSpirv();

    // Invalid source must be reported as a compile diagnostic on every platform now
    // that the engine compiles GLSL in-process instead of rejecting it on Windows.
    auto replaced = graphics->replaceShaderFromGlsl(*shader, {}, "#version 450\nthis is deliberately invalid GLSL\n");
    REQUIRE(!replaced.ok());
    REQUIRE(!replaced.diagnostics().empty());
    CHECK_EQ(replaced.diagnostics().front().code(), DiagnosticCode::ParseError);
    CHECK(shader->gpuHandle == originalGpuHandle);
    CHECK_EQ(shader->fragmentSpirv(), originalFragment);
}
}  // namespace test_shader_reload
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_shader_reload_vulkan)
#include "PrivateTestScope.h"
#include <cstdint>
#include <vector>
#include "graphics/Graphics.h"
#include "graphics/Shader.h"
#include "graphics/vulkan/Graphics.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
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
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_softbody)
#include "PrivateTestScope.h"
#include <SDL2/SDL.h>
#include <cmath>
#include <memory>
#include "gpgpu/ComputeShader.h"
#include "gpgpu/Gpgpu.h"
#include "gpgpu/GpuBuffer.h"
#include "graphics/AmbientOcclusion.h"
#include "graphics/AntiAliasing.h"
#include "graphics/Canvas.h"
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
#include "graphics/ScreenSpaceReflection.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"
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
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
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


TEST_CASE("softbody.cloth3d.collidesWithWorld3D") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world3(mod->newWorld3D(0.f, -9.8f, 0.f, true));
    auto                    *ground = world3->newBody("static", 0.f, -0.5f, 0.f);
    REQUIRE(ground != nullptr);
    ground->newBoxShape(8.f, 1.f, 8.f);

    std::unique_ptr<Cloth3D> cloth(eve::cloth::Cloth::create()->newCloth3D(8, 6, 0.5f, 0.f, 4.f, 0.f));
    for (int i = 0; i < cloth->getParticleCount(); ++i) cloth->unpin(i);
    cloth->setGravity(0.f, -6.f, 0.f);
    cloth->setCollideWorld(world3.get());
    const float surfaceY = 0.f;
    for (int i = 0; i < 180; ++i) cloth->update(1.f / 60.f);
    for (int i = 0; i < cloth->getParticleCount(); ++i)
        REQUIRE_GE(cloth->getParticleY(i), surfaceY - cloth->getParticleSize() - 0.05f);
    // Sanity: the sheet fell from y = 4 toward the ground.
    REQUIRE_LT(cloth->getParticleY(5 * 8 + 3), 3.5f);
}

TEST_CASE("softbody.cloth3d.momentumExchangeScalesWithMass") {
    auto      *mod     = Physics::create();
    const auto runCase = [&](float density) {
        std::unique_ptr<World3D> world3(mod->newWorld3D(0.f, 0.f, 0.f, true));
        auto                    *box = world3->newBody("dynamic", 0.f, 1.2f, 0.f);
        REQUIRE(box != nullptr);
        box->newBoxShape(1.f, 0.8f, 1.f, density, 0.2f, 0.f);
        std::unique_ptr<Cloth3D> cloth(eve::cloth::Cloth::create()->newCloth3D(6, 4, 0.4f, -0.6f, 3.f, -0.6f));
        for (int i = 0; i < cloth->getParticleCount(); ++i) cloth->unpin(i);
        cloth->setGravity(0.f, -5.f, 0.f);
        cloth->setCollideWorld(world3.get());
        for (int i = 0; i < 180; ++i) {
            world3->update(1.f / 60.f);
            cloth->update(1.f / 60.f);
        }
        return box->getLinearVelocityY();
    };

    const float vLight = runCase(0.05f);
    const float vHeavy = runCase(5.f);
    // +Y up: the light box is pushed downward noticeably, the heavy one barely.
    REQUIRE_LT(vLight, -0.2f);
    REQUIRE_LT(vLight, vHeavy - 0.1f);
}

namespace {

[[maybe_unused]] float testClosestPointTriangle(float ax, float ay, float az, float bx, float by, float bz, float cx,
                                                float cy, float cz, float px, float py, float pz, float &qx, float &qy,
                                                float &qz) {
    // Mirrors Cloth3D's Ericson closest-point-on-triangle (test-only).
    const float abx = bx - ax, aby = by - ay, abz = bz - az;
    const float acx = cx - ax, acy = cy - ay, acz = cz - az;
    const float apx = px - ax, apy = py - ay, apz = pz - az;
    const float d1 = abx * apx + aby * apy + abz * apz;
    const float d2 = acx * apx + acy * apy + acz * apz;
    if (d1 <= 0.f && d2 <= 0.f) {
        qx = ax;
        qy = ay;
        qz = az;
        return std::sqrt((px - ax) * (px - ax) + (py - ay) * (py - ay) + (pz - az) * (pz - az));
    }
    const float bpx = px - bx, bpy = py - by, bpz = pz - bz;
    const float d3 = abx * bpx + aby * bpy + abz * bpz;
    const float d4 = acx * bpx + acy * bpy + acz * bpz;
    if (d3 >= 0.f && d4 <= d3) {
        qx = bx;
        qy = by;
        qz = bz;
        return std::sqrt((px - bx) * (px - bx) + (py - by) * (py - by) + (pz - bz) * (pz - bz));
    }
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.f && d1 >= 0.f && d3 <= 0.f) {
        const float t = d1 / (d1 - d3);
        qx            = ax + abx * t;
        qy            = ay + aby * t;
        qz            = az + abz * t;
        return std::sqrt((px - qx) * (px - qx) + (py - qy) * (py - qy) + (pz - qz) * (pz - qz));
    }
    const float cpx = px - cx, cpy = py - cy, cpz = pz - cz;
    const float d5 = abx * cpx + aby * cpy + abz * cpz;
    const float d6 = acx * cpx + acy * cpy + acz * cpz;
    if (d6 >= 0.f && d5 <= d6) {
        qx = cx;
        qy = cy;
        qz = cz;
        return std::sqrt((px - cx) * (px - cx) + (py - cy) * (py - cy) + (pz - cz) * (pz - cz));
    }
    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.f && d2 >= 0.f && d6 <= 0.f) {
        const float t = d2 / (d2 - d6);
        qx            = ax + acx * t;
        qy            = ay + acy * t;
        qz            = az + acz * t;
        return std::sqrt((px - qx) * (px - qx) + (py - qy) * (py - qy) + (pz - qz) * (pz - qz));
    }
    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.f && (d4 - d3) >= 0.f && (d5 - d6) >= 0.f) {
        const float t = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        qx            = bx + (cx - bx) * t;
        qy            = by + (cy - by) * t;
        qz            = bz + (cz - bz) * t;
        return std::sqrt((px - qx) * (px - qx) + (py - qy) * (py - qy) + (pz - qz) * (pz - qz));
    }
    const float denom = 1.f / (va + vb + vc);
    const float v     = vb * denom;
    const float w     = vc * denom;
    qx                = ax + abx * v + acx * w;
    qy                = ay + aby * v + acy * w;
    qz                = az + abz * v + acz * w;
    return std::sqrt((px - qx) * (px - qx) + (py - qy) * (py - qy) + (pz - qz) * (pz - qz));
}

}  // namespace


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

TEST_CASE("softbody.render.clothAndFluid2DPreview") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width    = 720;
    s.height   = 420;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    auto                  *mod = Physics::create();
    std::unique_ptr<Cloth> cloth(eve::cloth::Cloth::create()->newCloth(14, 10, 12.f, 40.f, 30.f));
    cloth->setBounds(0.f, 0.f, 720.f, 420.f);
    cloth->setColor(0.78f, 0.84f, 0.98f, 1.f);

    std::unique_ptr<Fluid2D> fluid(mod->newFluid2D(400));
    fluid->setBounds(360.f, 40.f, 320.f, 340.f);
    fluid->setGravity(0.f, 980.f);
    fluid->setColor(0.2f, 0.55f, 0.95f, 0.9f);
    fluid->emit(520.f, 90.f, 120, 0.f, 40.f);

    gfx->setBackgroundColorRGBA(0.07f, 0.08f, 0.11f, 1.f);
    for (int frame = 0; frame < 90; ++frame) {
        if (frame == 30) cloth->grabAt(cloth->getParticleX(5 * 14 + 7), cloth->getParticleY(5 * 14 + 7), 30.f);
        if (frame >= 30 && frame < 60) cloth->moveGrab(180.f + float(frame - 30) * 2.f, 220.f);
        if (frame == 60) cloth->releaseGrab();
        if ((frame % 15) == 0) fluid->emit(500.f, 70.f, 8, 20.f, 60.f);
        fluid->interactAt(520.f, 200.f, 70.f, -2500.f);

        cloth->update(1.f / 60.f);
        fluid->update(1.f / 60.f);

        gfx->clearScreen();
        // Fluid2D tank outline.
        gfx->drawSolidRectRGBA(360.f, 40.f, 320.f, 340.f, 0.12f, 0.14f, 0.18f, 1.f);
        cloth->draw(gfx);
        fluid->draw(gfx);
        gfx->present();

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) break;
        }
        SDL_Delay(16);
    }

    CHECK_GT(cloth->getParticleY(5 * 14 + 7), 40.f);
    CHECK_GT(fluid->getParticleCount(), 100);
    win->close();
}

TEST_CASE("softbody.render.cloth3DPreview") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width    = 720;
    s.height   = 420;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    std::unique_ptr<Cloth3D> cloth(eve::cloth::Cloth::create()->newCloth3D(14, 10, 0.4f, -1.5f, 2.5f, -1.f));
    cloth->setColor(0.78f, 0.84f, 0.98f, 1.f);
    cloth->setFoldStiffness(0.7f);

    gfx->setBackgroundColorRGBA(0.07f, 0.08f, 0.11f, 1.f);
    for (int frame = 0; frame < 45; ++frame) {
        if (frame == 15)
            cloth->grabAt(cloth->getParticleX(5 * 14 + 7), cloth->getParticleY(5 * 14 + 7),
                          cloth->getParticleZ(5 * 14 + 7), 0.4f);
        if (frame >= 15 && frame < 30) cloth->moveGrab(0.8f, 1.2f, -0.4f);
        if (frame == 30) cloth->releaseGrab();
        cloth->applyForce(0.8f, 0.f, 0.f);
        cloth->update(1.f / 60.f);

        gfx->begin3DFrame();
        cloth->draw(gfx);
        gfx->present();

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) break;
        }
        SDL_Delay(8);
    }

    REQUIRE_LT(cloth->getParticleY(5 * 14 + 7), 2.2f);
    win->close();
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

TEST_CASE("softbody.gpu.clothLargeSelfCollision") {
    if (!tryInitGpuWindow()) return;
    auto *gpgpu = Gpgpu::create();
    REQUIRE(gpgpu != nullptr);
    if (!gpgpu->isAvailable()) return;

    std::unique_ptr<ClothGPU> cloth;
    try {
        cloth = std::unique_ptr<ClothGPU>(eve::cloth::Cloth::create()->newClothGPU(100, 80, 12.f, 20.f, 20.f));
    } catch (...) {
        return;
    }
    REQUIRE(cloth.get() != nullptr);
    REQUIRE_EQ(cloth->getParticleCount(), 8000);
    // Keep the top row pinned so the 8000-particle sheet hangs like a curtain
    // under gravity. This exercises the spatially-hashed self-collision at
    // scale and verifies solver stability, not perfect separation in a pile.
    cloth->setGravity(0.f, 180.f);
    cloth->setBounds(0.f, 0.f, 1200.f, 1000.f);
    cloth->setParticleSize(3.f);
    cloth->setStiffness(0.9f);
    cloth->setIterations(24);
    cloth->setDamping(0.3f);
    cloth->setSelfCollision(true);

    for (int f = 0; f < 120; ++f) {
        cloth->update(1.f / 60.f);
    }

    for (int i = 0; i < 200; ++i) {
        const int idx = (i * 79) % cloth->getParticleCount();
        REQUIRE(std::isfinite(cloth->getParticleX(idx)));
        REQUIRE(std::isfinite(cloth->getParticleY(idx)));
    }
    // The upper half of the hanging sheet stays near its rest column (x = 620)
    // and rest heights — the solver is stable at 8000 particles.
    for (int r = 1; r <= 40; r += 3) {
        const int idx = r * 100 + 50;
        REQUIRE(std::fabs(cloth->getParticleX(idx) - 620.f) < 30.f);
        REQUIRE(std::fabs(cloth->getParticleY(idx) - (20.f + float(r) * 12.f)) < 15.f);
    }
    // Everything stays inside the bounds.
    for (int i = 0; i < 400; ++i) {
        const int idx = (i * 131) % cloth->getParticleCount();
        REQUIRE_GE(cloth->getParticleX(idx), 0.f);
        REQUIRE_LE(cloth->getParticleX(idx), 1200.f);
        REQUIRE_GE(cloth->getParticleY(idx), 0.f);
        REQUIRE_LE(cloth->getParticleY(idx), 1000.f);
    }
}

TEST_CASE("softbody.gpu.renderClothPreview") {
    if (!tryInitGpuWindow()) return;
    auto *gpgpu = Gpgpu::create();
    REQUIRE(gpgpu != nullptr);
    if (!gpgpu->isAvailable()) return;

    auto *gfx = eve::graphics::Graphics::create();
    REQUIRE(gfx != nullptr);
    std::unique_ptr<ClothGPU> cloth = tryMakeCloth(gpgpu, 14, 10, 12.f, 40.f, 30.f);
    if (!cloth) return;
    cloth->setBounds(0.f, 0.f, 320.f, 240.f);
    cloth->setColor(0.78f, 0.84f, 0.98f, 1.f);

    gfx->setBackgroundColorRGBA(0.07f, 0.08f, 0.11f, 1.f);
    for (int frame = 0; frame < 45; ++frame) {
        cloth->applyForce(40.f, 0.f);
        cloth->update(1.f / 60.f);
        gfx->clearScreen();
        cloth->draw(gfx);
        gfx->present();
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) break;
        }
        SDL_Delay(4);
    }
    REQUIRE_GT(cloth->getParticleY(5 * 14 + 7), 40.f);
}
}  // namespace test_softbody
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_spatial)
#include "PrivateTestScope.h"
#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <set>
#include <vector>
#include "common/Exception.h"
#include "graphics/AmbientOcclusion.h"
#include "graphics/AntiAliasing.h"
#include "graphics/Canvas.h"
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
#include "graphics/ScreenSpaceReflection.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"
#include "spatial/BSPTree2D.h"
#include "spatial/BSPTree3D.h"
#include "spatial/Octree.h"
#include "spatial/QuadTree.h"
#include "spatial/Spatial.h"
#include "spatial/SpatialHash2D.h"
#include "spatial/SpatialHash3D.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// spatial.cpp
namespace test_spatial {
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

using namespace eve::spatial;
using namespace eve::graphics;

[[maybe_unused]] static std::set<int> collectResults(int count, const std::function<int(int)> &getId) {
    std::set<int> out;
    for (int i = 0; i < count; ++i) out.insert(getId(i));
    return out;
}


TEST_CASE("spatial.quadtree.queryPreview") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width    = 520;
    s.height   = 520;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    const float               world = 400.f;
    const float               pad   = 60.f;
    std::unique_ptr<QuadTree> tree(new QuadTree(0, 0, world, world, 8, 4));

    struct Item {
        int   id;
        float minX, minY, maxX, maxY;
        Color color;
    };
    std::vector<Item> items = {
        {1, 40, 40, 90, 90, Color(0.9f, 0.4f, 0.35f, 1.f)},
        {2, 160, 50, 210, 110, Color(0.35f, 0.75f, 0.95f, 1.f)},
        {3, 280, 30, 360, 80, Color(0.45f, 0.9f, 0.5f, 1.f)},
        {4, 60, 180, 120, 240, Color(0.95f, 0.85f, 0.35f, 1.f)},
        {5, 200, 200, 260, 270, Color(0.8f, 0.5f, 0.95f, 1.f)},
        {6, 310, 170, 370, 230, Color(0.95f, 0.55f, 0.75f, 1.f)},
        {7, 30, 300, 100, 370, Color(0.55f, 0.85f, 0.8f, 1.f)},
        {8, 180, 320, 250, 380, Color(0.7f, 0.7f, 0.4f, 1.f)},
        {9, 300, 300, 380, 380, Color(0.6f, 0.6f, 0.95f, 1.f)},
    };
    for (const Item &it : items) REQUIRE(tree->insert(it.id, it.minX, it.minY, it.maxX, it.maxY));

    gfx->setBackgroundColorRGBA(0.07f, 0.08f, 0.1f, 1.f);
    int hitFrames = 0;

    for (int frame = 0; frame < 100; ++frame) {
        const float t  = float(frame) * 0.05f;
        const float qw = 90.f;
        const float qh = 90.f;
        const float qx = 40.f + (world - qw - 40.f) * (0.5f + 0.5f * std::sin(t));
        const float qy = 40.f + (world - qh - 40.f) * (0.5f + 0.5f * std::cos(t * 0.8f));

        const int     n    = tree->queryRect(qx, qy, qx + qw, qy + qh);
        std::set<int> hits = collectResults(n, [&](int i) { return tree->getResultId(i); });
        if (!hits.empty()) ++hitFrames;

        gfx->clearScreen();
        // World frame
        gfx->drawSolidRect(pad - 2.f, pad - 2.f, world + 4.f, world + 4.f, Color(0.2f, 0.22f, 0.28f, 1.f));
        gfx->drawSolidRect(pad, pad, world, world, Color(0.1f, 0.11f, 0.14f, 1.f));

        for (const Item &it : items) {
            const bool hit = hits.count(it.id) > 0;
            Color      c   = it.color;
            if (hit) {
                c.r = std::min(1.f, c.r + 0.25f);
                c.g = std::min(1.f, c.g + 0.25f);
                c.b = std::min(1.f, c.b + 0.25f);
            }
            gfx->drawSolidRect(pad + it.minX, pad + it.minY, it.maxX - it.minX, it.maxY - it.minY, c);
            if (hit) {
                gfx->drawSolidRect(pad + it.minX - 2.f, pad + it.minY - 2.f, it.maxX - it.minX + 4.f, 3.f,
                                   Color(1.f, 1.f, 1.f, 1.f));
            }
        }

        // Query window outline
        gfx->drawSolidRect(pad + qx, pad + qy, qw, 3.f, Color(1.f, 0.95f, 0.4f, 1.f));
        gfx->drawSolidRect(pad + qx, pad + qy + qh - 3.f, qw, 3.f, Color(1.f, 0.95f, 0.4f, 1.f));
        gfx->drawSolidRect(pad + qx, pad + qy, 3.f, qh, Color(1.f, 0.95f, 0.4f, 1.f));
        gfx->drawSolidRect(pad + qx + qw - 3.f, pad + qy, 3.f, qh, Color(1.f, 0.95f, 0.4f, 1.f));

        gfx->present();
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) break;
        }
        SDL_Delay(16);
    }

    CHECK_GT(hitFrames, 20);
    win->close();
}
}  // namespace test_spatial
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_stylize)
#include "PrivateTestScope.h"
#include <SDL2/SDL.h>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "ManualPreview.h"
#include "common/Exception.h"
#include "filesystem/FileData.h"
#include "graphics/AmbientOcclusion.h"
#include "graphics/AntiAliasing.h"
#include "graphics/Canvas.h"
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
#include "image/Image.h"
#include "image/ImageData.h"
#include "stylize/StyleInstance.h"
#include "stylize/StyleRecipe.h"
#include "stylize/Stylize.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
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

[[maybe_unused]] void saveImageDataPng(ImageData *frame, const std::string &path) {
    REQUIRE(frame != nullptr);
    [[maybe_unused]] auto *const imageModule = eve::image::Image::create();
    eve::filesystem::FileData   *png = frame->encode(medialoader::FormatHandler::ENCODED_PNG, "stylize.png", false);
    REQUIRE(png != nullptr);
    REQUIRE(png->getSize() > 0);

    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    {
        std::ofstream out(path, std::ios::binary);
        REQUIRE(out.good());
        out.write(static_cast<const char *>(png->getData()), static_cast<std::streamsize>(png->getSize()));
        REQUIRE(out.good());
    }
    delete png;
    std::printf("stylize render saved: %s\n", path.c_str());
}

[[maybe_unused]] std::string testOutDir() { return std::string(EVENGINE_TEST_BINARY_DIR) + "/out"; }

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

    auto             *mod    = Stylize::create();
    const std::string outDir = testOutDir();

    std::unique_ptr<ImageData> baseFrame(renderCylinderFrame(gfx, cylinder, albedo, nullptr));
    REQUIRE(baseFrame.get() != nullptr);
    saveImageDataPng(baseFrame.get(), outDir + "/cylinder_base.png");

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

        const std::string name = std::string("cylinder_") + id + ".png";
        saveImageDataPng(styled.get(), outDir + "/" + name);

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

        if (!eve::test::manualPreviewEnabled()) continue;

        // Live gallery: hold each style on the window so the look is visible.
        Texture *preview = gfx->newTexture(styled.get());
        REQUIRE(preview != nullptr);
        gfx->setBackgroundColor(eve::graphics::Color(0.08f, 0.08f, 0.1f, 1.f));
        for (int frame = 0; frame < 40; ++frame) {
            gfx->clearScreen();
            gfx->drawTexturedRect(preview, 0.f, 0.f, float(gfx->getWidth()), float(gfx->getHeight()),
                                  eve::graphics::Color(1.f, 1.f, 1.f, 1.f));
            gfx->present();
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT) break;
            }
            SDL_Delay(16);
        }
    }

    win->close();
}
}  // namespace test_stylize
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_stylize_anime)
#include "PrivateTestScope.h"
#include <cmath>
#include "graphics/Graphics.h"
#include "graphics/Shader.h"
#include "stylize/StyleShaders.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
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
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_stylize_mesh_particles)
#include "PrivateTestScope.h"
#include <array>
#include <cmath>
#include "common/Exception.h"
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "stylize/MeshEffect.h"
#include "stylize/MeshEffectRenderer.h"
#include "stylize/MeshParticleEmitter.h"
#include "stylize/MeshVfxAsset.h"
#include "stylize/MeshVfxRenderBatch.h"
#include "stylize/MeshVfxScalability.h"
#include "stylize/SkillMeshEffect.h"
#include "stylize/Stylize.h"
#include "stylize/TrailEffect.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
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
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_ui_texture_bridge)
#include "PrivateTestScope.h"
#include <SDL2/SDL.h>
#include <imgui.h>
#include <cmath>
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/RenderSystem.h"
#include "ui/UI.h"
#include "ui/UIBackend.h"
#include "ui/UIHost.h"
#include "ui/Widget.h"
#include "window/Window.h"
#include "window/sdl/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
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
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_voxel_face_dir)
#include "PrivateTestScope.h"
#include <cstdio>
#include <memory>
#include <string>
#include "SceneColor.h"
#include "VoxelRenderFixtures.h"
#include "voxel/Chunk.h"
#include "voxel/FaceDir.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// voxel_face_dir.cpp
namespace test_voxel_face_dir {
// voxel_face_dir.cpp
using namespace eve::voxel;


// voxel_mesher_shapes.cpp
using namespace eve::voxel;


// voxel_render.cpp
TEST_CASE("voxel.render.smokeDrawVisibleProducesPixels") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    // Small solid block near origin so a +Z camera sees it.
    for (int z = 0; z < 4; ++z)
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x) world->setVoxel(x, y, z, 1);
    world->remeshDirty();
    CHECK(world->getOrCreateChunk(0, 0, 0)->totalRectCount() > 0);

    Texture *atlas = makeSolid(gfx, 230, 60, 50);
    REQUIRE(atlas != nullptr);

    const glm::vec3 eye(2.f, 2.f, 12.f);
    const glm::vec3 target(2.f, 2.f, 2.f);
    for (int i = 0; i < 3; ++i) renderVoxelFrame(gfx, world.get(), atlas, 1, eye, target, 200.f, true);

    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    // Background is dark slate; a lit red face should raise luma / red channel.
    CHECK(mid.r > 0.15f);
    CHECK(luma(mid) > 0.08f);
    CHECK(luma(mid) < 0.98f);
}

TEST_CASE("voxel.render.invalidFaceDirThrows") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 160, 120);

    const glm::mat4 vp(1.f);
    gfx->begin3DFrame();
    if (!gfx->had3DThisFrame()) {
        RenderSystem::render(*gfx);
        return;
    }
    gfx->setMesh3DViewProj(vp);
    uint32_t dummy = PackedRect::pack(0, 0, 0, 1, 1, 0).bits;
    bool     threw = false;
    try {
        gfx->drawVoxelFaceInstances(&dummy, 1, 0.f, 0.f, 0.f, "sideways", nullptr, 1);
    } catch (...) {
        threw = true;
    }
    CHECK(threw);
    // Recover present path.
    RenderSystem::render(*gfx);
}

TEST_CASE("voxel.render.faceCullChangesCoverage") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

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
}

TEST_CASE("voxel.render.outOfRangeDrawsNothingBright") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    world->getOrCreateChunk(0, 0, 0)->fill(1);
    world->remeshDirty();
    Texture *atlas = makeSolid(gfx, 255, 255, 255);

    const glm::vec3 eye(16.f, 16.f, 80.f);
    const glm::vec3 target(16.f, 16.f, 16.f);
    // Tiny range → selectVisible yields no batches.
    renderVoxelFrame(gfx, world.get(), atlas, 1, eye, target, 1.f, true);
    REQUIRE_EQ(world->getVisibleBatchCount(), 0);

    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    // Should be near the dark clear color.
    REQUIRE(luma(testSceneLinearColor(mid)) < 0.2f);
}

TEST_CASE("voxel.render.faceDirAliasesDraw") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 240, 180);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);
    Texture       *atlas      = makeSolid(gfx, 220, 180, 140);
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
    for (const auto &direction : directions) {
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
    }
    win->close();
}

TEST_CASE("voxel.render.farChunkOrigin") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    // Chunk (2,0,0) origin at x=64.
    for (int z = 0; z < 4; ++z)
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x) world->setVoxel(64 + x, y, z, 1);
    world->remeshDirty();
    CHECK(world->hasChunk(2, 0, 0));

    Texture        *atlas = makeSolid(gfx, 220, 80, 200);
    const glm::vec3 eye(66.f, 2.f, 16.f);
    const glm::vec3 target(66.f, 2.f, 2.f);
    renderVoxelFrame(gfx, world.get(), atlas, 1, eye, target, 200.f, true);
    CHECK(world->getVisibleChunkCount() >= 1);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.05f);
}

TEST_CASE("voxel.render.depthNearOccludesFar") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

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

TEST_CASE("voxel.render.allSixFacesFromOrbit") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 256, 192);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    for (int z = 0; z < 6; ++z)
        for (int y = 0; y < 6; ++y)
            for (int x = 0; x < 6; ++x) world->setVoxel(x, y, z, 1);
    world->remeshDirty();
    Texture *atlas = makeSolid(gfx, 210, 200, 190);

    struct Cam {
        glm::vec3 eye;
        glm::vec3 target;
        FaceDir   expect;
    };
    const Cam cams[] = {
        {{20.f, 3.f, 3.f}, {3.f, 3.f, 3.f}, FaceDir::PosX}, {{-8.f, 3.f, 3.f}, {3.f, 3.f, 3.f}, FaceDir::NegX},
        {{3.f, 20.f, 3.f}, {3.f, 3.f, 3.f}, FaceDir::PosY}, {{3.f, -8.f, 3.f}, {3.f, 3.f, 3.f}, FaceDir::NegY},
        {{3.f, 3.f, 20.f}, {3.f, 3.f, 3.f}, FaceDir::PosZ}, {{3.f, 3.f, -8.f}, {3.f, 3.f, 3.f}, FaceDir::NegZ},
    };

    for (const auto &c : cams) {
        // For top/bottom views, renderVoxelFrame uses +Y up which is fine for ±X/±Z;
        // for ±Y eye, lookAtRH with up=(0,1,0) degenerates — use custom path.
        const float aspect = float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
        glm::vec3   up(0.f, 1.f, 0.f);
        if (std::fabs(c.eye.y - c.target.y) > 10.f) up = glm::vec3(0.f, 0.f, 1.f);
        const glm::mat4 view = glm::lookAtRH(c.eye, c.target, up);
        const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 200.f);
        const glm::mat4 vp   = proj * view;
        world->selectVisible(&vp[0][0], c.eye.x, c.eye.y, c.eye.z, 200.f, true);

        bool saw = false;
        for (int i = 0; i < world->getVisibleBatchCount(); ++i)
            if (world->getVisibleBatch(i).dir == c.expect) saw = true;
        CHECK(saw);

        gfx->setBackgroundColor(Color(0.05f, 0.06f, 0.08f, 1.f));
        gfx->begin3DFrame();
        if (gfx->had3DThisFrame()) {
            gfx->setMesh3DViewProj(vp);
            gfx->setMesh3DCameraPos(c.eye);
            world->drawVisible(gfx, atlas, 1);
        }
        RenderSystem::render(*gfx);
        Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
        CHECK(luma(mid) > 0.05f);
    }
}

TEST_CASE("voxel.render.singleFaceManualDraw") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 256, 192);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    // Large +Z quad covering much of the view.
    uint32_t packed = PackedRect::pack(0, 0, 0, 8, 8, 1).bits;
    Texture *atlas  = makeSolid(gfx, 50, 200, 80);

    const float     aspect = float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::vec3 eye(4.f, 4.f, 20.f);
    const glm::mat4 view = glm::lookAtRH(eye, glm::vec3(4.f, 4.f, 1.f), glm::vec3(0, 1, 0));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(45.f), aspect, 0.1f, 100.f);
    const glm::mat4 vp   = proj * view;

    gfx->setBackgroundColor(Color(0.05f, 0.05f, 0.07f, 1.f));
    gfx->begin3DFrame();
    if (gfx->had3DThisFrame()) {
        gfx->setMesh3DViewProj(vp);
        gfx->drawVoxelFaceInstances(&packed, 1, 0.f, 0.f, 0.f, "posZ", atlas, 1);
    }
    RenderSystem::render(*gfx);

    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(mid.g > mid.r);
    CHECK(mid.g > 0.08f);

    // Corners of screen may still be background depending on FOV; center must be lit.
    Color corner = gfx->getPixel(2, 2);
    CHECK(luma(mid) > luma(corner));
}

TEST_CASE("voxel.render.posXWallFromSide") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    for (int z = 0; z < 8; ++z)
        for (int y = 0; y < 8; ++y) world->setVoxel(0, y, z, 1);
    world->remeshDirty();
    Texture *atlas = makeSolid(gfx, 230, 120, 50);

    const glm::vec3 eye(12.f, 4.f, 4.f);
    const glm::vec3 target(0.f, 4.f, 4.f);
    renderVoxelFrame(gfx, world.get(), atlas, 1, eye, target, 100.f, true);

    bool sawPosX = false;
    for (int i = 0; i < world->getVisibleBatchCount(); ++i)
        if (world->getVisibleBatch(i).dir == FaceDir::PosX) sawPosX = true;
    CHECK(sawPosX);

    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(mid.r > 0.1f);
}

TEST_CASE("voxel.render.clearColorWhenCulled") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 240, 180);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    world->getOrCreateChunk(0, 0, 0)->fill(1);
    world->remeshDirty();
    Texture *atlas = makeSolid(gfx, 255, 255, 255);

    // Camera behind looking away from chunk.
    const glm::vec3 eye(16.f, 16.f, 16.f);
    const glm::vec3 target(16.f, 16.f, 100.f);  // look +Z, chunk is around same place but...
    // Better: put eye far and look opposite.
    renderVoxelFrame(gfx, world.get(), atlas, 1, glm::vec3(-40.f, 16.f, 16.f), glm::vec3(-80.f, 16.f, 16.f), 200.f,
                     true);
    // Chunk at origin not in front of camera looking toward -X further.
    REQUIRE_EQ(world->getVisibleChunkCount(), 0);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    REQUIRE(luma(testSceneLinearColor(mid)) < 0.22f);
}

TEST_CASE("voxel.render.posYFromAbove") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    for (int z = 0; z < 6; ++z)
        for (int x = 0; x < 6; ++x) world->setVoxel(x, 0, z, 1);
    world->remeshDirty();
    Texture *atlas = makeSolid(gfx, 200, 180, 60);

    const float     aspect = float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::vec3 eye(3.f, 18.f, 3.f);
    const glm::mat4 view = glm::lookAtRH(eye, glm::vec3(3.f, 0.f, 3.f), glm::vec3(0, 0, -1));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f);
    const glm::mat4 vp   = proj * view;
    world->selectVisible(&vp[0][0], eye.x, eye.y, eye.z, 100.f, true);

    bool sawPosY = false;
    for (int i = 0; i < world->getVisibleBatchCount(); ++i)
        if (world->getVisibleBatch(i).dir == FaceDir::PosY) sawPosY = true;
    CHECK(sawPosY);

    gfx->setBackgroundColor(Color(0.05f, 0.06f, 0.08f, 1.f));
    gfx->begin3DFrame();
    if (gfx->had3DThisFrame()) {
        gfx->setMesh3DViewProj(vp);
        world->drawVisible(gfx, atlas, 1);
    }
    RenderSystem::render(*gfx);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.05f);
}

TEST_CASE("voxel.render.faceCullRectCountAboutHalf") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 240, 180);
    tinyHud(gfx);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    world->getOrCreateChunk(0, 0, 0)->fill(1);
    world->remeshDirty();

    const float     aspect = float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::vec3 eye(40.f, 16.f, 16.f);
    const glm::mat4 view = glm::lookAtRH(eye, glm::vec3(16.f, 16.f, 16.f), glm::vec3(0, 1, 0));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 200.f);
    const glm::mat4 vp   = proj * view;

    world->selectVisible(&vp[0][0], eye.x, eye.y, eye.z, 200.f, false);
    CHECK_EQ(world->getVisibleRectCount(), 6);

    world->selectVisible(&vp[0][0], eye.x, eye.y, eye.z, 200.f, true);
    CHECK(world->getVisibleRectCount() >= 1);
    CHECK(world->getVisibleRectCount() <= 5);
    CHECK(world->getVisibleRectCount() < 6);
}

TEST_CASE("voxel.render.chunkSeamDoubleFaceStillDraws") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    // Voxels on both sides of x=32 seam.
    for (int y = 0; y < 4; ++y)
        for (int z = 0; z < 4; ++z) {
            world->setVoxel(31, y, z, 1);
            world->setVoxel(32, y, z, 1);
        }
    world->remeshDirty();
    Texture *atlas = makeSolid(gfx, 200, 160, 100);

    const glm::vec3 eye(48.f, 2.f, 2.f);
    const glm::vec3 target(32.f, 2.f, 2.f);
    renderVoxelFrame(gfx, world.get(), atlas, 1, eye, target, 200.f, true);
    CHECK(world->getVisibleChunkCount() >= 1);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.04f);
}

TEST_CASE("voxel.render.negYFromBelow") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    for (int z = 0; z < 6; ++z)
        for (int x = 0; x < 6; ++x) world->setVoxel(x, 4, z, 1);
    world->remeshDirty();
    Texture *atlas = makeSolid(gfx, 180, 120, 60);

    const float     aspect = float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::vec3 eye(3.f, -10.f, 3.f);
    const glm::mat4 view = glm::lookAtRH(eye, glm::vec3(3.f, 4.f, 3.f), glm::vec3(0, 0, -1));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f);
    const glm::mat4 vp   = proj * view;
    world->selectVisible(&vp[0][0], eye.x, eye.y, eye.z, 100.f, true);

    bool sawNegY = false;
    for (int i = 0; i < world->getVisibleBatchCount(); ++i)
        if (world->getVisibleBatch(i).dir == FaceDir::NegY) sawNegY = true;
    CHECK(sawNegY);

    gfx->setBackgroundColor(Color(0.05f, 0.06f, 0.08f, 1.f));
    gfx->begin3DFrame();
    if (gfx->had3DThisFrame()) {
        gfx->setMesh3DViewProj(vp);
        world->drawVisible(gfx, atlas, 1);
    }
    RenderSystem::render(*gfx);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.04f);
}

TEST_CASE("voxel.render.posZWallFromFront") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    uint32_t        packed = PackedRect::pack(0, 0, 0, 6, 6, 1).bits;
    Texture        *atlas  = makeSolid(gfx, 80, 200, 220);
    const float     aspect = float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::vec3 eye(3.f, 3.f, 14.f);
    const glm::mat4 view = glm::lookAtRH(eye, glm::vec3(3.f, 3.f, 1.f), glm::vec3(0, 1, 0));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f);
    const glm::mat4 vp   = proj * view;

    gfx->setBackgroundColor(Color(0.05f, 0.05f, 0.07f, 1.f));
    gfx->begin3DFrame();
    if (gfx->had3DThisFrame()) {
        gfx->setMesh3DViewProj(vp);
        gfx->drawVoxelFaceInstances(&packed, 1, 0.f, 0.f, 0.f, "posZ", atlas, 1);
    }
    RenderSystem::render(*gfx);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.05f);
}

TEST_CASE("voxel.render.zeroCountAfterLargeDraw") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 240, 180);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::vector<uint32_t> many;
    for (int i = 0; i < 64; ++i) many.push_back(PackedRect::pack(i % 8, 0, i / 8, 1, 1, 1).bits);
    Texture        *atlas  = makeSolid(gfx, 200, 200, 200);
    const float     aspect = float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::vec3 eye(4.f, 10.f, 4.f);
    const glm::mat4 view = glm::lookAtRH(eye, glm::vec3(4.f, 0.f, 4.f), glm::vec3(0, 1, 0));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f);
    const glm::mat4 vp   = proj * view;

    gfx->setBackgroundColor(Color(0.05f, 0.06f, 0.08f, 1.f));
    gfx->begin3DFrame();
    if (gfx->had3DThisFrame()) {
        gfx->setMesh3DViewProj(vp);
        gfx->drawVoxelFaceInstances(many.data(), int(many.size()), 0.f, 0.f, 0.f, "posY", atlas, 1);
        gfx->drawVoxelFaceInstances(many.data(), 0, 0.f, 0.f, 0.f, "posY", atlas, 1);
    }
    RenderSystem::render(*gfx);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.04f);
}

TEST_CASE("voxel.render.rangeCullDarkensDistant") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

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
}

TEST_CASE("voxel.render.fullChunkSixFacesOrbitPixel") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    world->getOrCreateChunk(0, 0, 0)->fill(1);
    world->remeshDirty();
    CHECK_EQ(world->getChunk(0, 0, 0)->totalRectCount(), 6);
    Texture *atlas = makeSolid(gfx, 160, 170, 180);

    const glm::vec3 eye(48.f, 40.f, 48.f);
    const glm::vec3 target(16.f, 16.f, 16.f);
    renderVoxelFrame(gfx, world.get(), atlas, 1, eye, target, 300.f, true);
    CHECK(world->getVisibleBatchCount() >= 2);
    CHECK(world->getVisibleBatchCount() <= 3);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.05f);
}

TEST_CASE("voxel.render.thinRectanglesAcrossAxes") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 220);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);
    struct Rectangle {
        int         width, height;
        const char *direction;
        glm::vec3   eye, target, up;
        uint8_t     red, green, blue;
        Color       background;
        float       minLuma;
    };
    // The +Y face lies at y=1; aim at its center rather than the plane below it.
    const Rectangle cases[] = {
        {32,
         1,
         "posY",
         {16.f, 10.f, 16.f},
         {16.f, 1.f, 0.5f},
         {0, 0, -1},
         230,
         180,
         40,
         Color(0.05f, 0.06f, 0.08f, 1.f),
         0.05f},
        {1,
         32,
         "posX",
         {20.f, 16.f, 0.5f},
         {1.f, 16.f, 0.5f},
         {0, 1, 0},
         200,
         60,
         200,
         Color(0.05f, 0.05f, 0.07f, 1.f),
         0.04f},
    };
    for (const auto &input : cases) {
        std::printf("thin face=%s size=%dx%d\n", input.direction, input.width, input.height);
        const uint32_t packed = PackedRect::pack(0, 0, 0, input.width, input.height, 1).bits;
        Texture       *atlas  = makeSolid(gfx, input.red, input.green, input.blue);
        REQUIRE(atlas != nullptr);
        const float     aspect = float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
        const glm::mat4 vp     = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f) *
                             glm::lookAtRH(input.eye, input.target, input.up);
        gfx->setBackgroundColor(input.background);
        gfx->begin3DFrame();
        REQUIRE(gfx->had3DThisFrame());
        gfx->setMesh3DViewProj(vp);
        gfx->drawVoxelFaceInstances(&packed, 1, 0.f, 0.f, 0.f, input.direction, atlas, 1);
        RenderSystem::render(*gfx);
        const Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
        REQUIRE(luma(mid) > input.minLuma);
        REQUIRE(mid.r > 0.1f);
        if (input.green > input.blue) {
            REQUIRE(mid.g > mid.b);
            REQUIRE(mid.r > mid.b);
        } else {
            REQUIRE(mid.b > mid.g);
            REQUIRE(mid.r > mid.g);
        }
    }
    win->close();
}

TEST_CASE("voxel.render.negXWallFromLeft") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 300, 220);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    for (int z = 0; z < 6; ++z)
        for (int y = 0; y < 6; ++y) world->setVoxel(4, y, z, 1);
    world->remeshDirty();
    Texture *atlas = makeSolid(gfx, 230, 100, 50);

    const glm::vec3 eye(-6.f, 3.f, 3.f);
    const glm::vec3 target(4.f, 3.f, 3.f);
    renderVoxelFrame(gfx, world.get(), atlas, 1, eye, target, 100.f, true);
    bool sawNegX = false;
    for (int i = 0; i < world->getVisibleBatchCount(); ++i)
        if (world->getVisibleBatch(i).dir == FaceDir::NegX) sawNegX = true;
    CHECK(sawNegX);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.05f);
}

TEST_CASE("voxel.render.frustumCulledChunkDark") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 280, 200);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

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
}

TEST_CASE("voxel.render.carveHoleDarkensCenter") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    fillCube(world.get(), 0, 0, 0, 8, 1);
    world->remeshDirty();
    Texture        *atlas = makeSolid(gfx, 230, 230, 240);
    const glm::vec3 eye(4.f, 4.f, 20.f);
    const glm::vec3 target(4.f, 4.f, 4.f);

    renderVoxelFrame(gfx, world.get(), atlas, 1, eye, target, 100.f, true);
    Color solid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(solid) > 0.08f);

    // Carve a tunnel through the center along Z so the mid pixel looks through.
    for (int z = 0; z < 8; ++z)
        for (int y = 3; y <= 4; ++y)
            for (int x = 3; x <= 4; ++x) world->setVoxel(x, y, z, 0);
    world->remeshDirty();
    renderVoxelFrame(gfx, world.get(), atlas, 1, eye, target, 100.f, true);
    Color carved = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(carved) < luma(solid));
}

TEST_CASE("voxel.render.nearPlaneOccludesFarDifferentColor") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    Texture *red   = makeSolid(gfx, 240, 30, 30);
    Texture *green = makeSolid(gfx, 30, 240, 30);
    uint32_t nearR = PackedRect::pack(0, 0, 0, 6, 6, 1).bits;
    uint32_t farG  = PackedRect::pack(0, 0, 0, 6, 6, 1).bits;

    const float     aspect = float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::vec3 eye(3.f, 3.f, 20.f);
    const glm::mat4 view = glm::lookAtRH(eye, glm::vec3(3.f, 3.f, 0.f), glm::vec3(0, 1, 0));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f);
    const glm::mat4 vp   = proj * view;

    for (bool nearFirst : {false, true}) {
        gfx->setBackgroundColor(Color(0.05f, 0.06f, 0.08f, 1.f));
        gfx->begin3DFrame();
        REQUIRE(gfx->had3DThisFrame());
        gfx->setMesh3DViewProj(vp);
        if (nearFirst) gfx->drawVoxelFaceInstances(&nearR, 1, 0.f, 0.f, 8.f, "posZ", red, 1);
        gfx->drawVoxelFaceInstances(&farG, 1, 0.f, 0.f, 0.f, "posZ", green, 1);
        if (!nearFirst) gfx->drawVoxelFaceInstances(&nearR, 1, 0.f, 0.f, 8.f, "posZ", red, 1);
        RenderSystem::render(*gfx);
        const Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
        REQUIRE(nearFirst ? mid.g > mid.r : mid.r > mid.g);
        REQUIRE(nearFirst ? mid.g > 0.08f : mid.r > 0.08f);
        std::swap(red, green);  // Exercise both dominant colors, as the old stacked-depth case did.
    }
}

TEST_CASE("voxel.render.fullLayerTopFromAbove") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    for (int z = 0; z < 32; ++z)
        for (int x = 0; x < 32; ++x) world->setVoxel(x, 0, z, 1);
    world->remeshDirty();
    CHECK_EQ(world->getChunk(0, 0, 0)->faceRectCount(FaceDir::PosY), 1);
    Texture *atlas = makeSolid(gfx, 200, 160, 80);

    const glm::vec3 eye(16.f, 40.f, 16.f);
    const glm::mat4 view   = glm::lookAtRH(eye, glm::vec3(16.f, 0.f, 16.f), glm::vec3(0, 0, -1));
    const float     aspect = float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::mat4 proj   = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 200.f);
    const glm::mat4 vp     = proj * view;
    world->selectVisible(&vp[0][0], eye.x, eye.y, eye.z, 200.f, true);
    gfx->setBackgroundColor(Color(0.05f, 0.06f, 0.08f, 1.f));
    gfx->begin3DFrame();
    if (gfx->had3DThisFrame()) {
        gfx->setMesh3DViewProj(vp);
        world->drawVisible(gfx, atlas, 1);
    }
    RenderSystem::render(*gfx);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.05f);
}

TEST_CASE("voxel.render.chunkOriginNegativeDraw") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 300, 220);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

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
    CHECK(world->getVisibleChunkCount() >= 1);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.04f);
}

// voxel_render_scenes.cpp
[[maybe_unused]] static uint32_t voxelHash(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

[[maybe_unused]] static float voxelH21(int x, int z) {
    return float(voxelHash(uint32_t(x) * 0x9E3779B9u ^ uint32_t(z) * 0x85EBCA6Bu) & 0xFFFFu) / 65535.f;
}

[[maybe_unused]] static float voxelValueNoise(float x, float z) {
    const int   x0 = int(std::floor(x));
    const int   z0 = int(std::floor(z));
    const float tx = x - float(x0);
    const float tz = z - float(z0);
    const float sx = tx * tx * (3.f - 2.f * tx);
    const float sz = tz * tz * (3.f - 2.f * tz);
    const float a  = voxelH21(x0, z0);
    const float b  = voxelH21(x0 + 1, z0);
    const float c  = voxelH21(x0, z0 + 1);
    const float d  = voxelH21(x0 + 1, z0 + 1);
    return (a * (1.f - sx) + b * sx) * (1.f - sz) + (c * (1.f - sx) + d * sx) * sz;
}

[[maybe_unused]] static float voxelFbm(float x, float z) {
    float sum = 0.f, amp = 1.f, freq = 1.f, norm = 0.f;
    for (int i = 0; i < 4; ++i) {
        sum += amp * voxelValueNoise(x * freq, z * freq);
        norm += amp;
        amp *= 0.5f;
        freq *= 2.03f;
    }
    return sum / std::max(norm, 1e-4f);
}

[[maybe_unused]] static Texture *makeEarthAtlas(Graphics *gfx) {
    static const uint8_t pal[16][3] = {
        {20, 22, 28},  {86, 158, 62}, {145, 96, 55},   {128, 128, 132}, {210, 186, 120}, {48, 96, 168},
        {118, 78, 42}, {46, 118, 48}, {236, 240, 245}, {96, 96, 100},   {168, 52, 42},   {210, 196, 168},
        {72, 48, 32},  {148, 72, 56}, {212, 168, 64},  {64, 68, 76},
    };
    std::vector<uint8_t> px(16u * 4u);
    for (int i = 0; i < 16; ++i) {
        px[size_t(i) * 4u + 0] = pal[i][0];
        px[size_t(i) * 4u + 1] = pal[i][1];
        px[size_t(i) * 4u + 2] = pal[i][2];
        px[size_t(i) * 4u + 3] = 255;
    }
    return gfx->newTexture(4, 4, px.data());
}

[[maybe_unused]] static void fillBox(VoxelWorld *w, int x0, int y0, int z0, int x1, int y1, int z1, uint8_t tex) {
    for (int z = z0; z < z1; ++z)
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x) w->setVoxel(x, y, z, tex);
}

[[maybe_unused]] static void fillHollowBox(VoxelWorld *w, int x0, int y0, int z0, int x1, int y1, int z1, uint8_t tex) {
    fillBox(w, x0, y0, z0, x1, y1, z1, tex);
    if (x1 - x0 > 2 && y1 - y0 > 1 && z1 - z0 > 2) fillBox(w, x0 + 1, y0, z0 + 1, x1 - 1, y1, z1 - 1, 0);
}

[[maybe_unused]] static void placeTree(VoxelWorld *w, int x, int groundY, int z) {
    const int h = 3 + int(voxelH21(x, z) * 3.f);
    for (int y = 1; y <= h; ++y) w->setVoxel(x, groundY + y, z, 6);
    const int cy = groundY + h;
    for (int dz = -2; dz <= 2; ++dz) {
        for (int dx = -2; dx <= 2; ++dx) {
            for (int dy = 0; dy <= 2; ++dy) {
                if (dx * dx + dy * dy + dz * dz > 6) continue;
                w->setVoxel(x + dx, cy + dy, z + dz, 7);
            }
        }
    }
}

[[maybe_unused]] static void placeHouse(VoxelWorld *w, int x, int z, int wdt, int dpt, int hgt, uint8_t wall,
                                        uint8_t roof) {
    fillHollowBox(w, x, 1, z, x + wdt, 1 + hgt, z + dpt, wall);
    fillBox(w, x, 1 + hgt, z, x + wdt, 1 + hgt + 1, z + dpt, roof);
    const int ridgeZ = z + dpt / 2;
    for (int i = 0; i < wdt; ++i) w->setVoxel(x + i, 2 + hgt, ridgeZ, roof);
}

[[maybe_unused]] static std::string voxelSceneOutDir() {
    return std::string(EVENGINE_TEST_BINARY_DIR) + "/out/voxel_scenes";
}

[[maybe_unused]] static void renderVoxelScene(Graphics *gfx, VoxelWorld *world, Texture *atlas, const glm::vec3 &eye,
                                              const glm::vec3 &target, float farZ, float viewRange) {
    const float     aspect = float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::mat4 view   = glm::lookAtRH(eye, target, glm::vec3(0.f, 1.f, 0.f));
    const glm::mat4 proj   = perspectiveVulkanRH_ZO(glm::radians(48.f), aspect, 0.2f, farZ);
    const glm::mat4 vp     = proj * view;
    world->selectVisible(&vp[0][0], eye.x, eye.y, eye.z, viewRange, true);
    const Color sky(0.42f, 0.58f, 0.78f, 1.f);
    gfx->setBackgroundColor(sky);
    gfx->clear(sky, std::nullopt, std::nullopt);
    gfx->begin3DFrame();
    if (!gfx->had3DThisFrame()) return;
    gfx->setMesh3DViewProj(vp);
    gfx->setMesh3DCameraPos(eye);
    world->drawVisible(gfx, atlas, 4);
    RenderSystem::render(*gfx);
}

[[maybe_unused]] static void saveVoxelScenePng(Graphics *gfx, const std::string &name) {
    std::unique_ptr<ImageData> img(gfx->newImageData());
    REQUIRE(img.get() != nullptr);
    const std::string dir = voxelSceneOutDir();
    std::error_code   ec;
    std::filesystem::create_directories(dir, ec);
    const std::string path = dir + "/" + name + ".png";
    REQUIRE(saveImagePng(*img, path));
    std::printf("voxel scene: %s\n", path.c_str());
}

TEST_CASE("voxel.render.sceneHillsIsland") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 960, 540);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    constexpr int               kN   = 160;
    constexpr int               kSea = 6;
    for (int z = 0; z < kN; ++z) {
        for (int x = 0; x < kN; ++x) {
            const float n      = voxelFbm(float(x) * 0.035f, float(z) * 0.035f);
            const float ridge  = std::fabs(voxelFbm(float(x) * 0.018f + 40.f, float(z) * 0.018f) * 2.f - 1.f);
            const float h01    = n * 0.62f + (1.f - ridge) * 0.38f;
            const float cx     = (float(x) - 80.f) / 88.f;
            const float cz     = (float(z) - 80.f) / 88.f;
            float       island = std::clamp(1.12f - std::sqrt(cx * cx + cz * cz), 0.f, 1.f);
            island *= island;
            int h = int(2.f + h01 * 20.f * island);
            if (h < 1) h = 1;
            if (h > 24) h = 24;
            for (int y = 0; y <= h; ++y) {
                uint8_t tex = 3;
                if (y == h) {
                    if (h <= kSea + 1)
                        tex = 4;
                    else if (h >= 18)
                        tex = 8;
                    else
                        tex = 1;
                } else if (y >= h - 2) {
                    tex = 2;
                }
                world->setVoxel(x, y, z, tex);
            }
            if (h < kSea) {
                for (int y = h + 1; y <= kSea; ++y) world->setVoxel(x, y, z, 5);
            }
        }
    }
    for (int z = 8; z < kN - 8; z += 11) {
        for (int x = 8; x < kN - 8; x += 13) {
            if (voxelH21(x * 3, z * 5) < 0.78f) continue;
            uint8_t top = 0;
            int     gy  = 24;
            while (gy > 0 && (top = world->getVoxel(x, gy, z)) == 0) --gy;
            if (top != 1) continue;
            placeTree(world.get(), x, gy, z);
        }
    }

    const int remeshed = world->remeshDirty();
    CHECK(remeshed >= 20);
    CHECK(world->getChunkCount() >= 20);

    Texture *atlas = makeEarthAtlas(gfx);
    REQUIRE(atlas != nullptr);
    const glm::vec3 eye(78.f, 52.f, 168.f);
    const glm::vec3 target(80.f, 8.f, 78.f);
    for (int i = 0; i < 2; ++i) renderVoxelScene(gfx, world.get(), atlas, eye, target, 420.f, 360.f);

    CHECK(world->getVisibleRectCount() > 80);
    const Color sky(0.42f, 0.58f, 0.78f, 1.f);
    CHECK(countNonBgSamples(gfx, sky) >= 20);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.06f);
    saveVoxelScenePng(gfx, "hills_island");
}

TEST_CASE("voxel.render.sceneWalledTown") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 960, 540);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    constexpr int               kN = 96;
    fillBox(world.get(), 0, 0, 0, kN, 1, kN, 2);
    fillBox(world.get(), 2, 1, 2, kN - 2, 2, kN - 2, 9);

    const int wallH = 8;
    for (int i = 4; i < kN - 4; ++i) {
        for (int y = 1; y <= wallH; ++y) {
            world->setVoxel(i, y, 4, 3);
            world->setVoxel(i, y, kN - 5, 3);
            world->setVoxel(4, y, i, 3);
            world->setVoxel(kN - 5, y, i, 3);
        }
        if ((i % 2) == 0) {
            world->setVoxel(i, wallH + 1, 4, 3);
            world->setVoxel(i, wallH + 1, kN - 5, 3);
            world->setVoxel(4, wallH + 1, i, 3);
            world->setVoxel(kN - 5, wallH + 1, i, 3);
        }
    }
    // Gate on +Z wall
    fillBox(world.get(), 44, 1, 4, 52, wallH + 2, 6, 0);
    fillBox(world.get(), 43, 1, 3, 44, wallH + 2, 7, 12);
    fillBox(world.get(), 52, 1, 3, 53, wallH + 2, 7, 12);

    auto tower = [&](int x, int z) {
        fillHollowBox(world.get(), x, 1, z, x + 7, 14, z + 7, 3);
        fillBox(world.get(), x, 14, z, x + 7, 15, z + 7, 10);
        fillBox(world.get(), x + 1, 15, z + 1, x + 6, 16, z + 6, 10);
        fillBox(world.get(), x + 2, 16, z + 2, x + 5, 17, z + 5, 10);
        world->setVoxel(x + 3, 17, z + 3, 14);
    };
    tower(2, 2);
    tower(kN - 9, 2);
    tower(2, kN - 9);
    tower(kN - 9, kN - 9);

    fillHollowBox(world.get(), 36, 1, 36, 60, 16, 60, 13);
    fillBox(world.get(), 36, 16, 36, 60, 17, 60, 10);
    fillBox(world.get(), 38, 17, 38, 58, 18, 58, 10);
    fillBox(world.get(), 46, 18, 46, 50, 22, 50, 12);
    world->setVoxel(48, 22, 48, 14);
    fillBox(world.get(), 46, 1, 36, 50, 8, 37, 0);

    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 4; ++col) {
            const int x = 10 + col * 8;
            const int z = 12 + row * 8;
            if (x + 6 >= 34) continue;
            placeHouse(world.get(), x, z, 6, 6, 4 + ((col + row) % 3), 11, 10);
        }
    }
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            placeHouse(world.get(), 64 + col * 8, 12 + row * 9, 7, 7, 3 + (row % 2), 11, 10);
        }
    }
    fillBox(world.get(), 12, 1, 70, 30, 2, 88, 4);
    for (int i = 0; i < 5; ++i) placeTree(world.get(), 16 + i * 3, 1, 74 + (i % 2) * 4);

    const int remeshed = world->remeshDirty();
    CHECK(remeshed >= 4);
    CHECK(world->getChunkCount() >= 4);

    Texture *atlas = makeEarthAtlas(gfx);
    REQUIRE(atlas != nullptr);
    const glm::vec3 eye(118.f, 68.f, 118.f);
    const glm::vec3 target(48.f, 6.f, 48.f);
    for (int i = 0; i < 2; ++i) renderVoxelScene(gfx, world.get(), atlas, eye, target, 320.f, 280.f);

    CHECK(world->getVisibleRectCount() > 40);
    const Color sky(0.42f, 0.58f, 0.78f, 1.f);
    CHECK(countNonBgSamples(gfx, sky) >= 20);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.06f);
    saveVoxelScenePng(gfx, "walled_town");
}

TEST_CASE("voxel.render.registryMultiFaceTex") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    // grass：顶面绿色 tile(1)，其余面红色 tile(0)；方向性炉子：前脸蓝色 tile(2)
    CubeTypeRegistry reg;
    CubeType         grass;
    grass.name = "grass";
    for (int i = 0; i < 6; ++i) grass.faceTex[i] = 0;
    grass.faceTex[2] = 1;  // 顶 = green
    reg.add(grass);

    CubeType furnace;
    furnace.name = "furnace";
    for (int i = 0; i < 6; ++i) furnace.faceTex[i] = 0;
    furnace.faceTex[0]  = 2;  // +X = blue
    furnace.directional = true;
    reg.add(furnace);

    VoxelWorld world(reg);
    for (int z = 0; z < 3; ++z)
        for (int y = 0; y < 3; ++y)
            for (int x = 0; x < 3; ++x) world.setVoxelByName(x, y, z, "grass");
    world.remeshDirty();
    Chunk *c = world.getChunk(0, 0, 0);
    REQUIRE(c != nullptr);
    CHECK_EQ(c->faceRects(FaceDir::PosY)[0].tex(), 1);  // 顶面解析为 green
    CHECK_EQ(c->faceRects(FaceDir::PosX)[0].tex(), 0);  // 侧面 red

    // 4×4 图集：tile0=red, tile1=green, tile2=blue
    Texture        *atlas = makeTileAtlas(gfx, 4, 4);
    const glm::vec3 eye(1.5f, 1.5f, 10.f);
    const glm::vec3 target(1.5f, 1.5f, 1.5f);
    renderVoxelFrame(gfx, &world, atlas, 4, eye, target, 100.f, true);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.04f);
}
}  // namespace test_voxel_face_dir
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_voxel_render_atlas)
#include "PrivateTestScope.h"
#include "SceneColor.h"
#include "VoxelRenderFixtures.h"
// voxel_render_atlas.cpp
namespace test_voxel_render_atlas {
// voxel_render_atlas.cpp
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

/** @brief Vertex AO must shade identically on Vulkan and WebGPU voxel pipelines. */
TEST_CASE("voxel.render.vertexAoDarkensPixels") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 300, 220);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);
    Texture *atlas = makeSolid(gfx, 220, 220, 220);

    const Color bright = renderVoxelAoSample(gfx, atlas, 0xFFu);
    const Color dark   = renderVoxelAoSample(gfx, atlas, 0x00u);
    REQUIRE(luma(bright) > 0.25f);
    // AO multiplies linear radiance; ACES and sRGB compress the displayed ratio.
    REQUIRE(luma(testSceneLinearColor(dark)) < luma(testSceneLinearColor(bright)) * 0.6f);
}

// NOTE: Graphics is a process-wide singleton — reuse one window for these cases.

TEST_CASE("voxel.render.atlasTexIndexTintVisible") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    // Texture id 1 (air is 0); solid red 1×1 atlas.
    world->setVoxel(0, 0, 0, 1);
    world->remeshDirty();

    Texture        *red = makeSolid(gfx, 240, 40, 40);
    const glm::vec3 eye(0.5f, 0.5f, 6.f);
    const glm::vec3 target(0.5f, 0.5f, 0.5f);
    for (int i = 0; i < 3; ++i) renderVoxelFrame(gfx, world.get(), red, 1, eye, target, 100.f, true);

    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(mid.r > mid.g);
    CHECK(mid.r > mid.b);
    CHECK(mid.r > 0.12f);
}

TEST_CASE("voxel.render.nullAtlasUsesWhite") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);
    for (int size : {2, 4}) {
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
    win->close();
}

TEST_CASE("voxel.render.tilesPerRowAtlasSample") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);
    struct Sample {
        int     size;
        uint8_t tile;
        uint8_t blue;
    };
    for (const auto sample : {Sample{3, 1, 200}, Sample{4, 2, 220}}) {
        std::printf("2x2 atlas tile=%d\n", sample.tile);
        const uint8_t px[16] = {200, 40, 40, 255, 40, 200, 40, 255, 40, 40, sample.blue, 255, 200, 200, 40, 255};
        Texture      *atlas  = gfx->newTexture(2, 2, px);
        REQUIRE(atlas != nullptr);
        std::unique_ptr<VoxelWorld> world(new VoxelWorld());
        fillCube(world.get(), 0, 0, 0, sample.size, sample.tile);
        world->remeshDirty();
        const float center = float(sample.size) * 0.5f;
        renderVoxelFrame(gfx, world.get(), atlas, 2, glm::vec3(center, center, float(sample.size * 2 + 4)),
                         glm::vec3(center), 100.f, true);
        const Color mid      = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
        const float dominant = sample.tile == 1 ? mid.g : mid.b;
        const float other    = sample.tile == 1 ? mid.b : mid.g;
        REQUIRE(dominant > mid.r);
        REQUIRE(dominant > other);
        REQUIRE(dominant > 0.1f);
    }
    win->close();
}

TEST_CASE("voxel.render.alternatingAtlasFrames") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    for (int z = 0; z < 4; ++z)
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x) world->setVoxel(x, y, z, 1);
    world->remeshDirty();

    Texture        *red  = makeSolid(gfx, 230, 40, 40);
    Texture        *blue = makeSolid(gfx, 40, 40, 230);
    const glm::vec3 eye(2.f, 2.f, 12.f);
    const glm::vec3 target(2.f, 2.f, 2.f);

    renderVoxelFrame(gfx, world.get(), red, 1, eye, target, 100.f, true);
    Color cRed = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(cRed.r > cRed.b);

    renderVoxelFrame(gfx, world.get(), blue, 1, eye, target, 100.f, true);
    Color cBlue = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(cBlue.b > cBlue.r);
}

TEST_CASE("voxel.render.tilesPerRowZeroClamped") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 240, 180);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    uint32_t        packed = PackedRect::pack(0, 0, 0, 4, 4, 1).bits;
    Texture        *atlas  = makeSolid(gfx, 210, 90, 50);
    const float     aspect = float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::vec3 eye(2.f, 8.f, 2.f);
    const glm::mat4 view = glm::lookAtRH(eye, glm::vec3(2.f, 0.f, 2.f), glm::vec3(0, 0, -1));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f);
    const glm::mat4 vp   = proj * view;

    gfx->setBackgroundColor(Color(0.05f, 0.06f, 0.08f, 1.f));
    gfx->begin3DFrame();
    if (gfx->had3DThisFrame()) {
        gfx->setMesh3DViewProj(vp);
        // tilesPerRow <= 0 should clamp to 1 and still sample the solid atlas.
        gfx->drawVoxelFaceInstances(&packed, 1, 0.f, 0.f, 0.f, "posY", atlas, 0);
    }
    RenderSystem::render(*gfx);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.04f);
}

TEST_CASE("voxel.render.atlas4x4_eachPaletteChannel") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 280, 200);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    Texture *atlas = makeTileAtlas(gfx, 4, 4);
    gfx->setTextureSampler(atlas, TextureSampler::nearest());
    const glm::vec3 eye(2.f, 2.f, 14.f);
    const glm::vec3 target(2.f, 2.f, 2.f);

    // tex 1 green, 2 blue, 3 yellow, 4 magenta, 5 cyan
    struct Expect {
        uint8_t tex;
        char    channel;  // 'r','g','b' dominant or 'y' r&g, 'm' r&b, 'c' g&b
    };
    const Expect cases[] = {{1, 'g'}, {2, 'b'}, {3, 'y'}, {4, 'm'}, {5, 'c'}};
    for (const Expect &e : cases) {
        std::unique_ptr<VoxelWorld> world(new VoxelWorld());
        fillCube(world.get(), 0, 0, 0, 4, e.tex);
        world->remeshDirty();
        renderVoxelFrame(gfx, world.get(), atlas, 4, eye, target, 100.f, true);
        Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
        REQUIRE(luma(mid) > 0.04f);
        if (e.channel == 'g') {
            REQUIRE(mid.g > 0.08f);
            REQUIRE(mid.g > mid.r);
            REQUIRE(mid.g > mid.b);
        } else if (e.channel == 'b') {
            REQUIRE(mid.b > mid.r);
            REQUIRE(mid.b > mid.g);
        } else if (e.channel == 'y') {
            REQUIRE(mid.r > mid.b);
            REQUIRE(mid.g > mid.b);
        } else if (e.channel == 'm') {
            REQUIRE(mid.r > mid.g);
            REQUIRE(mid.b > mid.g);
        } else if (e.channel == 'c') {
            REQUIRE(mid.g > mid.r);
            REQUIRE(mid.b > mid.r);
        }
    }
}

TEST_CASE("voxel.render.atlas8x8_highTexIndex") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    // 8×8 = 64 tiles; tex 15 uses palette slot 15 → cyan-ish (40,200,200)
    Texture                    *atlas = makeTileAtlas(gfx, 8, 8);
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    fillCube(world.get(), 0, 0, 0, 5, 15);
    world->remeshDirty();
    CHECK_EQ(world->getChunk(0, 0, 0)->faceRects(FaceDir::PosZ)[0].tex(), 15);

    const glm::vec3 eye(2.5f, 2.5f, 16.f);
    renderVoxelFrame(gfx, world.get(), atlas, 8, eye, glm::vec3(2.5f, 2.5f, 2.5f), 100.f, true);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(mid.g > mid.r);
    CHECK(mid.b > mid.r);
    CHECK(luma(mid) > 0.05f);
}

TEST_CASE("voxel.render.atlas16x8_tex63") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    // tilesPerRow=16, 8 rows → indices 0..127
    Texture                    *atlas = makeTileAtlas(gfx, 16, 8);
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    fillCube(world.get(), 0, 0, 0, 4, 63);
    world->remeshDirty();
    REQUIRE(world->getChunk(0, 0, 0)->faceRectCount(FaceDir::PosY) >= 1);
    CHECK_EQ(world->getChunk(0, 0, 0)->faceRects(FaceDir::PosY)[0].tex(), 63);

    const glm::vec3 eye(2.f, 10.f, 2.f);
    const glm::mat4 view   = glm::lookAtRH(eye, glm::vec3(2.f, 2.f, 2.f), glm::vec3(0, 0, -1));
    const float     aspect = float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::mat4 proj   = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f);
    const glm::mat4 vp     = proj * view;
    world->selectVisible(&vp[0][0], eye.x, eye.y, eye.z, 100.f, true);
    gfx->setBackgroundColor(Color(0.05f, 0.06f, 0.08f, 1.f));
    gfx->begin3DFrame();
    if (gfx->had3DThisFrame()) {
        gfx->setMesh3DViewProj(vp);
        world->drawVisible(gfx, atlas, 16);
    }
    RenderSystem::render(*gfx);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.04f);
}

TEST_CASE("voxel.render.swapTexIdChangesAtlasColor") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    Texture *atlas = makeTileAtlas(gfx, 4, 4);
    gfx->setTextureSampler(atlas, TextureSampler::nearest());
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    fillCube(world.get(), 0, 0, 0, 4, 1);  // green
    world->remeshDirty();
    const glm::vec3 eye(2.f, 2.f, 14.f);
    const glm::vec3 target(2.f, 2.f, 2.f);

    renderVoxelFrame(gfx, world.get(), atlas, 4, eye, target, 100.f, true);
    Color green = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    REQUIRE(green.g > green.r);

    for (int z = 0; z < 4; ++z)
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x) world->setVoxel(x, y, z, 2);  // blue
    world->remeshDirty();
    renderVoxelFrame(gfx, world.get(), atlas, 4, eye, target, 100.f, true);
    Color blue = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    REQUIRE(blue.b > blue.g);
    REQUIRE(blue.b > green.b);
}

TEST_CASE("voxel.render.multiTexStripesDoNotMerge") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    Texture                    *atlas = makeTileAtlas(gfx, 4, 4);
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    // Horizontal stripes of alternating tex 1 and 2 on a slab.
    for (int z = 0; z < 8; ++z)
        for (int x = 0; x < 8; ++x) world->setVoxel(x, 0, z, (z % 2) ? 1 : 2);
    world->remeshDirty();
    Chunk *c = world->getChunk(0, 0, 0);
    REQUIRE(c != nullptr);
    CHECK(c->faceRectCount(FaceDir::PosY) >= 4);

    const glm::vec3 eye(4.f, 12.f, 4.f);
    const glm::mat4 view   = glm::lookAtRH(eye, glm::vec3(4.f, 0.f, 4.f), glm::vec3(0, 0, -1));
    const float     aspect = float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::mat4 proj   = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f);
    const glm::mat4 vp     = proj * view;
    world->selectVisible(&vp[0][0], eye.x, eye.y, eye.z, 100.f, true);
    gfx->setBackgroundColor(Color(0.05f, 0.06f, 0.08f, 1.f));
    gfx->begin3DFrame();
    if (gfx->had3DThisFrame()) {
        gfx->setMesh3DViewProj(vp);
        world->drawVisible(gfx, atlas, 4);
    }
    RenderSystem::render(*gfx);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.04f);
}

TEST_CASE("voxel.render.manualDrawDifferentTexIndices") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    Texture *atlas = makeTileAtlas(gfx, 4, 4);
    // Two quads side by side: tex1 green @ x=0, tex2 blue @ origin shifted.
    uint32_t green = PackedRect::pack(0, 0, 0, 3, 3, 1).bits;
    uint32_t blue  = PackedRect::pack(0, 0, 0, 3, 3, 2).bits;

    const float     aspect = float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::vec3 eye(1.5f, 8.f, 1.5f);
    const glm::mat4 view = glm::lookAtRH(eye, glm::vec3(1.5f, 0.f, 1.5f), glm::vec3(0, 0, -1));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f);
    const glm::mat4 vp   = proj * view;

    gfx->setBackgroundColor(Color(0.05f, 0.06f, 0.08f, 1.f));
    gfx->begin3DFrame();
    if (gfx->had3DThisFrame()) {
        gfx->setMesh3DViewProj(vp);
        gfx->drawVoxelFaceInstances(&green, 1, 0.f, 0.f, 0.f, "posY", atlas, 4);
    }
    RenderSystem::render(*gfx);
    Color cGreen = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(cGreen.g > cGreen.b);

    gfx->begin3DFrame();
    if (gfx->had3DThisFrame()) {
        gfx->setMesh3DViewProj(vp);
        gfx->drawVoxelFaceInstances(&blue, 1, 0.f, 0.f, 0.f, "posY", atlas, 4);
    }
    RenderSystem::render(*gfx);
    Color cBlue = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(cBlue.b > cBlue.g);
}

TEST_CASE("voxel.render.atlasBoundaryIndices") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);
    struct Sample {
        int   columns;
        int   rows;
        int   tile;
        int   size;
        float eyeY;
    };
    // Index zero is valid in packed faces even though voxel zero means air.
    // Keep the first tile, a wide-atlas row edge, and the maximum packed index.
    for (const auto sample : {Sample{4, 4, 0, 4, 10.f}, Sample{32, 2, 31, 3, 8.f}, Sample{16, 8, 127, 4, 10.f}}) {
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
    }
    win->close();
}

TEST_CASE("voxel.render.fourTexBlocksSameFrame") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 360, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    Texture                    *atlas = makeTileAtlas(gfx, 4, 4);
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    fillCube(world.get(), 0, 0, 0, 3, 1);
    fillCube(world.get(), 5, 0, 0, 3, 2);
    fillCube(world.get(), 0, 0, 5, 3, 3);
    fillCube(world.get(), 5, 0, 5, 3, 4);
    world->remeshDirty();
    // Four different top-face textures → at least 4 PosY rects.
    CHECK(world->getChunk(0, 0, 0)->faceRectCount(FaceDir::PosY) >= 4);

    const glm::vec3 eye(4.f, 16.f, 4.f);
    const glm::mat4 view   = glm::lookAtRH(eye, glm::vec3(4.f, 0.f, 4.f), glm::vec3(0, 0, -1));
    const float     aspect = float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::mat4 proj   = perspectiveVulkanRH_ZO(glm::radians(55.f), aspect, 0.1f, 100.f);
    const glm::mat4 vp     = proj * view;
    world->selectVisible(&vp[0][0], eye.x, eye.y, eye.z, 100.f, true);
    gfx->setBackgroundColor(Color(0.05f, 0.06f, 0.08f, 1.f));
    gfx->begin3DFrame();
    if (gfx->had3DThisFrame()) {
        gfx->setMesh3DViewProj(vp);
        world->drawVisible(gfx, atlas, 4);
    }
    RenderSystem::render(*gfx);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.03f);
}

TEST_CASE("voxel.render.atlasYellowThenMagentaSwap") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 300, 220);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    Texture                    *atlas = makeTileAtlas(gfx, 4, 4);
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    fillCube(world.get(), 0, 0, 0, 5, 3);  // yellow
    world->remeshDirty();
    const glm::vec3 eye(2.5f, 2.5f, 16.f);
    const glm::vec3 target(2.5f, 2.5f, 2.5f);

    renderVoxelFrame(gfx, world.get(), atlas, 4, eye, target, 100.f, true);
    Color yellow = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(yellow.r > yellow.b);
    CHECK(yellow.g > yellow.b);

    for (int z = 0; z < 5; ++z)
        for (int y = 0; y < 5; ++y)
            for (int x = 0; x < 5; ++x) world->setVoxel(x, y, z, 4);  // magenta
    world->remeshDirty();
    renderVoxelFrame(gfx, world.get(), atlas, 4, eye, target, 100.f, true);
    Color magenta = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(magenta.r > magenta.g);
    CHECK(magenta.b > magenta.g);
}

TEST_CASE("voxel.render.twoChunksDifferentAtlasTiles") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    Texture *atlas = makeTileAtlas(gfx, 4, 4);
    gfx->setTextureSampler(atlas, TextureSampler::nearest());
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    fillCube(world.get(), 0, 0, 0, 4, 1);   // green
    fillCube(world.get(), 32, 0, 0, 4, 2);  // blue, next chunk
    world->remeshDirty();
    REQUIRE_EQ(world->getChunkCount(), 2);

    // Look at green chunk.
    renderVoxelFrame(gfx, world.get(), atlas, 4, glm::vec3(2.f, 2.f, 14.f), glm::vec3(2.f, 2.f, 2.f), 100.f, true);
    Color green = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    REQUIRE(green.g > green.b);

    // Look at blue chunk.
    renderVoxelFrame(gfx, world.get(), atlas, 4, glm::vec3(34.f, 2.f, 14.f), glm::vec3(34.f, 2.f, 2.f), 100.f, true);
    Color blue = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    REQUIRE(blue.b > blue.g);
}

TEST_CASE("voxel.render.multiFrameAtlasSwapStable") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 300, 220);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    Texture                    *atlasA = makeTileAtlas(gfx, 4, 4);
    Texture                    *atlasB = makeSolid(gfx, 40, 40, 230);
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    fillCube(world.get(), 0, 0, 0, 4, 1);
    world->remeshDirty();
    const glm::vec3 eye(2.f, 2.f, 14.f);
    const glm::vec3 target(2.f, 2.f, 2.f);

    for (int i = 0; i < 4; ++i) {
        renderVoxelFrame(gfx, world.get(), (i % 2) ? atlasB : atlasA, (i % 2) ? 1 : 4, eye, target, 100.f, true);
        Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
        CHECK(luma(mid) > 0.04f);
    }
}

TEST_CASE("voxel.render.sparsePillarsMultiTex") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    Texture                    *atlas = makeTileAtlas(gfx, 4, 4);
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    for (int z = 0; z < 12; z += 3)
        for (int x = 0; x < 12; x += 3)
            for (int y = 0; y < 3; ++y) world->setVoxel(x, y, z, uint8_t(1 + ((x + z) % 4)));
    world->remeshDirty();
    CHECK(world->getChunk(0, 0, 0)->totalRectCount() > 10);

    const glm::vec3 eye(6.f, 8.f, 22.f);
    renderVoxelFrame(gfx, world.get(), atlas, 4, eye, glm::vec3(6.f, 1.f, 6.f), 100.f, true);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.03f);
}

TEST_CASE("voxel.render.tilesPerRowMismatchStillDraws") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 280, 200);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    // 4×4 atlas but claim tilesPerRow=8 — UVs still sample something opaque.
    Texture        *atlas  = makeTileAtlas(gfx, 4, 4);
    uint32_t        packed = PackedRect::pack(0, 0, 0, 3, 3, 1).bits;
    const float     aspect = float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::vec3 eye(1.5f, 8.f, 1.5f);
    const glm::mat4 view = glm::lookAtRH(eye, glm::vec3(1.5f, 0.f, 1.5f), glm::vec3(0, 0, -1));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f);
    const glm::mat4 vp   = proj * view;

    gfx->setBackgroundColor(Color(0.05f, 0.06f, 0.08f, 1.f));
    gfx->begin3DFrame();
    if (gfx->had3DThisFrame()) {
        gfx->setMesh3DViewProj(vp);
        gfx->drawVoxelFaceInstances(&packed, 1, 0.f, 0.f, 0.f, "posY", atlas, 8);
    }
    RenderSystem::render(*gfx);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.03f);
}

// voxel_render_lifecycle.cpp
TEST_CASE("voxel.render.emptyInstanceInputsAreNoOps") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 200, 150);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);
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
    win->close();
}

TEST_CASE("voxel.render.multiBatchSameFrame") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    // Two separated pillars → multiple face batches.
    for (int y = 0; y < 6; ++y) {
        world->setVoxel(2, y, 2, 1);
        world->setVoxel(10, y, 10, 1);
    }
    world->remeshDirty();
    Texture *atlas = makeSolid(gfx, 180, 170, 90);

    const glm::vec3 eye(6.f, 8.f, 22.f);
    const glm::vec3 target(6.f, 3.f, 6.f);
    renderVoxelFrame(gfx, world.get(), atlas, 1, eye, target, 200.f, true);
    CHECK(world->getVisibleBatchCount() >= 2);
    CHECK(world->getVisibleRectCount() >= 2);

    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.05f);
}

TEST_CASE("voxel.render.drawVisibleMatchesManualBatches") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 240, 180);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    for (int z = 0; z < 3; ++z)
        for (int y = 0; y < 3; ++y)
            for (int x = 0; x < 3; ++x) world->setVoxel(x, y, z, 1);
    world->remeshDirty();
    Texture *atlas = makeSolid(gfx, 90, 140, 220);

    const float     aspect = float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::vec3 eye(1.5f, 1.5f, 10.f);
    const glm::vec3 target(1.5f, 1.5f, 1.5f);
    const glm::mat4 view = glm::lookAtRH(eye, target, glm::vec3(0, 1, 0));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 200.f);
    const glm::mat4 vp   = proj * view;

    world->selectVisible(&vp[0][0], eye.x, eye.y, eye.z, 200.f, true);
    const int batches = world->getVisibleBatchCount();
    REQUIRE(batches > 0);

    gfx->setBackgroundColor(Color(0.05f, 0.06f, 0.08f, 1.f));
    gfx->begin3DFrame();
    if (gfx->had3DThisFrame()) {
        gfx->setMesh3DViewProj(vp);
        gfx->setMesh3DCameraPos(eye);
        for (int i = 0; i < batches; ++i) {
            const DrawBatch &b = world->getVisibleBatch(i);
            gfx->drawVoxelFaceInstances(b.packed, b.count, b.chunk->originX(), b.chunk->originY(), b.chunk->originZ(),
                                        faceDirName(b.dir), atlas, 1);
        }
    }
    RenderSystem::render(*gfx);

    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.06f);
}

TEST_CASE("voxel.render.requiresBegin3DFrame") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 160, 120);

    uint32_t dummy = PackedRect::pack(0, 0, 0, 1, 1, 1).bits;
    bool     threw = false;
    try {
        gfx->drawVoxelFaceInstances(&dummy, 1, 0.f, 0.f, 0.f, "posZ", nullptr, 1);
    } catch (...) {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE("voxel.render.largeInstanceCount") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    // Many non-merged top faces: sparse pillars.
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    for (int z = 0; z < 16; ++z)
        for (int x = 0; x < 16; ++x)
            if (((x * 3 + z * 5) % 7) == 0) world->setVoxel(x, 0, z, 1);
    world->remeshDirty();
    CHECK(world->getOrCreateChunk(0, 0, 0)->totalRectCount() > 20);

    Texture        *atlas = makeSolid(gfx, 200, 180, 60);
    const glm::vec3 eye(8.f, 10.f, 28.f);
    const glm::vec3 target(8.f, 0.f, 8.f);
    renderVoxelFrame(gfx, world.get(), atlas, 1, eye, target, 200.f, true);
    CHECK(world->getVisibleRectCount() > 10);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.04f);
}

TEST_CASE("voxel.render.multiFrameSlotReuse") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 240, 180);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    world->setVoxel(0, 0, 0, 1);
    world->remeshDirty();
    Texture        *atlas = makeSolid(gfx, 160, 160, 200);
    const glm::vec3 eye(0.5f, 0.5f, 5.f);
    const glm::vec3 target(0.5f, 0.5f, 0.5f);

    for (int frame = 0; frame < 6; ++frame) {
        renderVoxelFrame(gfx, world.get(), atlas, 1, eye, target, 100.f, true);
        Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
        CHECK(luma(mid) > 0.04f);
    }
}

TEST_CASE("voxel.render.editRemeshChangesPixels") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    Texture                    *atlas = makeSolid(gfx, 230, 230, 240);
    const glm::vec3             eye(4.f, 4.f, 18.f);
    const glm::vec3             target(4.f, 4.f, 4.f);

    // Empty world → dark
    world->remeshDirty();
    renderVoxelFrame(gfx, world.get(), atlas, 1, eye, target, 200.f, true);
    REQUIRE_EQ(world->getVisibleRectCount(), 0);
    Color empty = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    REQUIRE(luma(testSceneLinearColor(empty)) < 0.2f);

    for (int z = 0; z < 8; ++z)
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x) world->setVoxel(x, y, z, 1);
    world->remeshDirty();
    renderVoxelFrame(gfx, world.get(), atlas, 1, eye, target, 200.f, true);
    Color filled = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    REQUIRE(luma(filled) > luma(empty) + 0.03f);
}

TEST_CASE("voxel.render.emptyWorldDrawVisible") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 200, 150);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    Texture                    *atlas = makeSolid(gfx, 255, 255, 255);
    const glm::vec3             eye(0.f, 0.f, 5.f);
    const glm::vec3             target(0.f, 0.f, 0.f);
    renderVoxelFrame(gfx, world.get(), atlas, 1, eye, target, 100.f, true);
    REQUIRE_EQ(world->getVisibleBatchCount(), 0);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    REQUIRE(luma(testSceneLinearColor(mid)) < 0.25f);
}

TEST_CASE("voxel.render.manyChunksBatched") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    for (int cx = 0; cx < 3; ++cx)
        for (int cz = 0; cz < 3; ++cz) {
            for (int y = 0; y < 2; ++y)
                for (int x = 0; x < 2; ++x)
                    for (int z = 0; z < 2; ++z) world->setVoxel(cx * 32 + x + 8, y, cz * 32 + z + 8, 1);
        }
    world->remeshDirty();
    CHECK_EQ(world->getChunkCount(), 9);

    Texture        *atlas = makeSolid(gfx, 120, 160, 200);
    const glm::vec3 eye(48.f, 20.f, 120.f);
    const glm::vec3 target(48.f, 1.f, 48.f);
    renderVoxelFrame(gfx, world.get(), atlas, 1, eye, target, 300.f, true);
    CHECK(world->getVisibleChunkCount() >= 3);
    CHECK(world->getVisibleBatchCount() >= 3);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.04f);
}

TEST_CASE("voxel.render.instanceCountGrowth") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 240, 180);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    Texture        *atlas  = makeSolid(gfx, 200, 200, 210);
    const float     aspect = float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::vec3 eye(8.f, 8.f, 40.f);
    const glm::mat4 view = glm::lookAtRH(eye, glm::vec3(8.f, 0.f, 8.f), glm::vec3(0, 1, 0));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 200.f);
    const glm::mat4 vp   = proj * view;

    // Grow instance buffer across draws in one frame: 1, then many.
    std::vector<uint32_t> many;
    many.reserve(64);
    for (int z = 0; z < 8; ++z)
        for (int x = 0; x < 8; ++x) many.push_back(PackedRect::pack(x, 0, z, 1, 1, 1).bits);

    gfx->setBackgroundColor(Color(0.05f, 0.06f, 0.08f, 1.f));
    gfx->begin3DFrame();
    if (gfx->had3DThisFrame()) {
        gfx->setMesh3DViewProj(vp);
        uint32_t one = many[0];
        gfx->drawVoxelFaceInstances(&one, 1, 0.f, 0.f, 0.f, "posY", atlas, 1);
        gfx->drawVoxelFaceInstances(many.data(), int(many.size()), 0.f, 0.f, 0.f, "posY", atlas, 1);
    }
    RenderSystem::render(*gfx);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.04f);
}

TEST_CASE("voxel.render.twoOriginsSameFrame") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    uint32_t a     = PackedRect::pack(0, 0, 0, 2, 2, 1).bits;
    uint32_t b     = PackedRect::pack(0, 0, 0, 2, 2, 1).bits;
    Texture *atlas = makeSolid(gfx, 90, 200, 120);

    const float     aspect = float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::vec3 eye(4.f, 4.f, 20.f);
    const glm::mat4 view = glm::lookAtRH(eye, glm::vec3(4.f, 1.f, 4.f), glm::vec3(0, 1, 0));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f);
    const glm::mat4 vp   = proj * view;

    gfx->setBackgroundColor(Color(0.05f, 0.06f, 0.08f, 1.f));
    gfx->begin3DFrame();
    if (gfx->had3DThisFrame()) {
        gfx->setMesh3DViewProj(vp);
        gfx->drawVoxelFaceInstances(&a, 1, 0.f, 0.f, 0.f, "posY", atlas, 1);
        gfx->drawVoxelFaceInstances(&b, 1, 6.f, 0.f, 6.f, "posY", atlas, 1);
    }
    RenderSystem::render(*gfx);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.04f);
}

TEST_CASE("voxel.render.removeVoxelsDarkens") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    for (int z = 0; z < 6; ++z)
        for (int y = 0; y < 6; ++y)
            for (int x = 0; x < 6; ++x) world->setVoxel(x, y, z, 1);
    world->remeshDirty();
    Texture        *atlas = makeSolid(gfx, 240, 240, 250);
    const glm::vec3 eye(3.f, 3.f, 16.f);
    const glm::vec3 target(3.f, 3.f, 3.f);

    renderVoxelFrame(gfx, world.get(), atlas, 1, eye, target, 100.f, true);
    Color filled = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    REQUIRE(luma(filled) > 0.08f);

    world->clear();
    world->remeshDirty();
    renderVoxelFrame(gfx, world.get(), atlas, 1, eye, target, 100.f, true);
    Color empty = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    REQUIRE(luma(empty) < luma(filled));
    REQUIRE(luma(testSceneLinearColor(empty)) < 0.22f);
}

TEST_CASE("voxel.render.stressHundredsOfInstances") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::vector<uint32_t> many;
    many.reserve(256);
    for (int z = 0; z < 16; ++z)
        for (int x = 0; x < 16; ++x) many.push_back(PackedRect::pack(x, 0, z, 1, 1, 1).bits);
    CHECK_EQ(int(many.size()), 256);

    Texture        *atlas  = makeSolid(gfx, 180, 190, 200);
    const float     aspect = float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::vec3 eye(8.f, 12.f, 30.f);
    const glm::mat4 view = glm::lookAtRH(eye, glm::vec3(8.f, 0.f, 8.f), glm::vec3(0, 1, 0));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 200.f);
    const glm::mat4 vp   = proj * view;

    gfx->setBackgroundColor(Color(0.05f, 0.06f, 0.08f, 1.f));
    gfx->begin3DFrame();
    if (gfx->had3DThisFrame()) {
        gfx->setMesh3DViewProj(vp);
        gfx->drawVoxelFaceInstances(many.data(), int(many.size()), 0.f, 0.f, 0.f, "posY", atlas, 1);
    }
    RenderSystem::render(*gfx);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.04f);
}

TEST_CASE("voxel.render.manualAllSixDirsOneFrame") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    uint32_t    packed = PackedRect::pack(0, 0, 0, 2, 2, 1).bits;
    Texture    *atlas  = makeSolid(gfx, 220, 220, 40);
    const float aspect = float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    // Orbit-ish view so several faces of a unit cube-ish placement are on screen.
    const glm::vec3 eye(8.f, 8.f, 8.f);
    const glm::mat4 view = glm::lookAtRH(eye, glm::vec3(1.f, 1.f, 1.f), glm::vec3(0, 1, 0));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(55.f), aspect, 0.1f, 100.f);
    const glm::mat4 vp   = proj * view;

    const char *dirs[6] = {"posX", "negX", "posY", "negY", "posZ", "negZ"};
    gfx->setBackgroundColor(Color(0.05f, 0.06f, 0.08f, 1.f));
    gfx->begin3DFrame();
    if (gfx->had3DThisFrame()) {
        gfx->setMesh3DViewProj(vp);
        for (const char *d : dirs) gfx->drawVoxelFaceInstances(&packed, 1, 0.f, 0.f, 0.f, d, atlas, 1);
    }
    RenderSystem::render(*gfx);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.03f);
}

TEST_CASE("voxel.render.remeshIncreasesThenDecreasesRects") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 300, 220);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    world->setVoxel(2, 2, 2, 1);
    world->remeshDirty();
    const int one = world->getChunk(0, 0, 0)->totalRectCount();
    CHECK_EQ(one, 6);

    fillCube(world.get(), 0, 0, 0, 5, 1);
    world->remeshDirty();
    const int many = world->getChunk(0, 0, 0)->totalRectCount();
    CHECK(many < one * 5);  // greedy merges
    CHECK(many >= 6);

    Texture *atlas = makeSolid(gfx, 200, 200, 210);
    renderVoxelFrame(gfx, world.get(), atlas, 1, glm::vec3(2.5f, 2.5f, 14.f), glm::vec3(2.5f, 2.5f, 2.5f), 100.f, true);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.05f);

    world->clear();
    world->setVoxel(2, 2, 2, 1);
    world->remeshDirty();
    CHECK_EQ(world->getChunk(0, 0, 0)->totalRectCount(), 6);
}

// ---------------------------------------------------------------------------
// Large visual scenes (PNG under test/out/voxel_scenes/)
// ---------------------------------------------------------------------------
}  // namespace test_voxel_render_atlas
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_water_mesh)
#include "PrivateTestScope.h"
#include <cmath>
#include <limits>
#include <memory>
#include <simplesquirrel/simplesquirrel.hpp>
#include "Fixtures.h"
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/WaterPlanarReflection.h"
#include "graphics/WaterReflectionMasker.h"
#include "graphics/WaterStyleConfig.h"
#include "graphics/WaterSystem.h"
#include "graphics/WaterUnderwaterEffects.h"
#include "image/ImageData.h"
#include "physics/Body3D.h"
#include "physics/Physics.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
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

namespace {
[[maybe_unused]] UnderwaterColorGradient depthGradient() {
    UnderwaterColorGradient gradient;
    REQUIRE(static_cast<bool>(gradient.addStop(0.0F, 0.4F, 0.7F, 1.0F)));
    REQUIRE(static_cast<bool>(gradient.addStop(1.0F, 0.03F, 0.08F, 0.18F)));
    return gradient;
}
[[maybe_unused]] UnderwaterColorGradient timeGradient() {
    UnderwaterColorGradient gradient;
    REQUIRE(static_cast<bool>(gradient.addStop(0.0F, 0.1F, 0.2F, 0.4F)));
    REQUIRE(static_cast<bool>(gradient.addStop(1.0F, 0.8F, 0.5F, 0.2F)));
    return gradient;
}
[[maybe_unused]] UnderwaterScalarCurve exposureCurve() {
    UnderwaterScalarCurve curve;
    REQUIRE(static_cast<bool>(curve.addKey(0.0F, -0.5F)));
    REQUIRE(static_cast<bool>(curve.addKey(1.0F, 0.5F)));
    return curve;
}
}  // namespace

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
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_weather)
#include "PrivateTestScope.h"
#include <cmath>
#include <limits>
#include "graphics/Graphics.h"
#include "graphics/Material.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/Texture.h"
#include "weather/PcgInteriorWeatherVolume.h"
#include "weather/Weather.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
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
#endif
