#include <SDL2/SDL.h>
#include <array>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include "common/Capability.h"
#include "common/Exception.h"
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/Light.h"
#include "graphics/Mesh.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "image/ImageData.h"
#include "stylize/AttackVfxLayerExecutor.h"
#include "stylize/AttackVfxRecipe.h"
#include "stylize/AttackVfxRuntime.h"
#include "stylize/MotionEchoEffect.h"
#include "stylize/StyleInstance.h"
#include "stylize/StylePass.h"
#include "stylize/StyleShaders.h"
#include "stylize/Stylize.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// stylize_attack_vfx_layers.cpp
using namespace eve::stylize;

namespace {

[[maybe_unused]] eve::LogicalId id(std::string_view value) {
    auto parsed = eve::LogicalId::parse(value);
    REQUIRE(parsed.has_value());
    return std::move(*parsed);
}

[[maybe_unused]] IAttackVfxLayerExecutor* executorFor(AttackVfxLayerRole role) {
    IAttackVfxLayerExecutor* found = nullptr;
    eve::cap::forEach<IAttackVfxLayerExecutor>([&](IAttackVfxLayerExecutor* executor) {
        if (!found && executor && executor->role() == role) found = executor;
    });
    return found;
}

}  // namespace

TEST_CASE("stylize.attack_vfx.layers mesh style and skill executors start") {
    auto* module = Stylize::create();
    REQUIRE(module != nullptr);
    auto* mesh = executorFor(AttackVfxLayerRole::MeshVfx);
    REQUIRE(mesh != nullptr);

    AttackVfxLayer layer;
    layer.role = AttackVfxLayerRole::MeshVfx;
    layer.uri  = "style:slash";
    AttackVfxLayerStartRequest request;
    request.layer = &layer;
    auto started  = mesh->start(request);
    REQUIRE(started.ok());
    REQUIRE(started.value().valid());
    CHECK(mesh->update(started.value(), 0.016, request).ok());
    CHECK(mesh->stop(started.value(), AttackVfxStopBehavior::ClearImmediately).ok());

    layer.uri = "skill:weaponSlash";
    started   = mesh->start(request);
    REQUIRE(started.ok());
    CHECK(mesh->stop(started.value(), AttackVfxStopBehavior::StopEmitting).ok());
}

TEST_CASE("stylize.attack_vfx.layers trail and distortion executors") {
    auto* module = Stylize::create();
    REQUIRE(module != nullptr);
    auto* trail      = executorFor(AttackVfxLayerRole::Trail);
    auto* distortion = executorFor(AttackVfxLayerRole::Distortion);
    REQUIRE(trail != nullptr);
    REQUIRE(distortion != nullptr);

    AttackVfxLayer trailLayer;
    trailLayer.role                    = AttackVfxLayerRole::Trail;
    trailLayer.uri                     = "trail://blade";
    trailLayer.floatParams["lifetime"] = 0.2f;
    AttackVfxRequest play;
    play.sourceId = 3;
    play.targetId = 8;
    AttackVfxLayerStartRequest request;
    request.layer       = &trailLayer;
    request.playRequest = &play;
    auto started        = trail->start(request);
    REQUIRE(started.ok());
    CHECK(trail->update(started.value(), 0.05, request).ok());
    CHECK(trail->stop(started.value(), AttackVfxStopBehavior::ClearImmediately).ok());

    AttackVfxLayer distortionLayer;
    distortionLayer.role                    = AttackVfxLayerRole::Distortion;
    distortionLayer.floatParams["strength"] = 0.35f;
    request.layer                           = &distortionLayer;
    started                                 = distortion->start(request);
    REQUIRE(started.ok());
    CHECK(distortion->stop(started.value(), AttackVfxStopBehavior::ClearImmediately).ok());
}

TEST_CASE("stylize.attack_vfx.layers water whip starts mesh trail camera distortion") {
    auto* stylize = Stylize::create();
    REQUIRE(stylize != nullptr);

    const char* recipeJson = R"({
      "schema":"eve.stylize.attack-vfx","schemaVersion":1,"id":"attackvfx:whip",
      "skin":{
        "id":"skin:water",
        "styleHints":{"meshStyle":"slash"},
        "shakeProfile":{"posAmp":0.05,"duration":0.1}
      },
      "phases":[{
        "kind":"release","durationSeconds":0.3,
        "layers":[
          {"role":"meshVfx","uri":"meshvfx://water/whip"},
          {"role":"trail","uri":"trail://blade"},
          {"role":"distortion","floatParams":{"strength":0.4}},
          {"role":"particles","uri":"particles://missing"}
        ]
      }]
    })";

    AttackVfxRuntime runtime;
    REQUIRE(runtime.registerRecipe(AttackVfxRecipe::fromJson(recipeJson).value()).ok());
    auto played = runtime.play(id("attackvfx:whip"), {});
    REQUIRE(played.ok());
    // Mesh/trail/distortion executors are registered; particles soft-skips.
    // Play succeeds and keeps one live instance through the release window.
    REQUIRE_EQ(runtime.activeCount(), 1u);
    auto advanced = runtime.advance(0.4);
    REQUIRE(advanced.ok());
    REQUIRE_EQ(runtime.activeCount(), 0u);
}

// stylize_effects.cpp
// Effect / material shaders ported from common Unity and Godot projects.
// Covers registry/schema (no GPU), GPU post passes, and GPU mesh-shader
// creation. Per-module, process-isolated like the rest of test/stylize.cpp.


using eve::graphics::Canvas;
using eve::graphics::Graphics;
using eve::graphics::Shader;
using eve::graphics::Texture;
using eve::image::ImageData;
using eve::stylize::StyleInstance;
using eve::stylize::StylePass;
using eve::stylize::Stylize;
using Colorf = ImageData::Colorf;

namespace {

const char* kMeshEffects[] = {"rim", "dissolve", "hologram", "snow", "slash", "ember", "aura"};
const char* kPostEffects[] = {"vignette", "chromatic", "grain"};

[[maybe_unused]] bool near(float a, float b, float eps = 0.02f) { return std::fabs(a - b) <= eps; }

[[maybe_unused]] ImageData* makeGradient(int w, int h) {
    auto* img = new ImageData(w, h, "RGBA8");
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float u = float(x) / float(std::max(w - 1, 1));
            float v = float(y) / float(std::max(h - 1, 1));
            img->setPixel(x, y, Colorf{u, 0.35f + 0.4f * v, 1.f - u, 1.f});
        }
    }
    return img;
}

}  // namespace


TEST_CASE("stylize.effects.gpu.postPasses") {
    auto* win = eve::window::Window::create();
    auto* gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width    = 128;
    s.height   = 96;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    auto* mod = Stylize::create();

    for (const char* id : kPostEffects) {
        StylePass* pass = mod->newPass(gfx, id);
        REQUIRE(pass != nullptr);
        CHECK_EQ(pass->getStyle(), std::string(id));
        REQUIRE(pass->getShader() != nullptr);
        REQUIRE(pass->getShader()->gpuHandle != nullptr);
        if (pass->hasParam("time")) pass->setTime(0.25f);

        std::unique_ptr<ImageData> img(makeGradient(64, 48));
        auto*                      tex = gfx->newTexture(img.get());
        REQUIRE(tex != nullptr);

        Canvas* rt = gfx->newCanvas(128, 96);
        REQUIRE(rt != nullptr);
        gfx->setCanvas(rt);
        gfx->clear(eve::graphics::Color(0.1f, 0.1f, 0.1f, 1.f), std::nullopt, std::nullopt);
        pass->apply(gfx, tex);
        gfx->setCanvas();

        eve::graphics::Color p = rt->getPixel(64, 48);
        CHECK_GT(p.a, 0.5f);
        delete pass;
    }

    win->close();
}

TEST_CASE("stylize.effects.gpu.meshShaders") {
    auto* win = eve::window::Window::create();
    auto* gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width    = 128;
    s.height   = 96;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    auto* mod = Stylize::create();

    Shader* rim = mod->newMeshShader(gfx, "rim");
    REQUIRE(rim != nullptr);
    REQUIRE(rim->gpuHandle != nullptr);
    CHECK(rim->hasUniform("rimPower"));

    Shader* dissolve = mod->newMeshShader(gfx, "dissolve");
    REQUIRE(dissolve != nullptr);
    REQUIRE(dissolve->gpuHandle != nullptr);
    CHECK(dissolve->hasUniform("amount"));

    Shader* hologram = mod->newMeshShader(gfx, "hologram");
    REQUIRE(hologram != nullptr);
    REQUIRE(hologram->gpuHandle != nullptr);
    CHECK(hologram->hasUniform("scanDensity"));

    Shader* snow = mod->newMeshShader(gfx, "snow");
    REQUIRE(snow != nullptr);
    REQUIRE(snow->gpuHandle != nullptr);
    CHECK(snow->hasUniform("snowAmount"));

    Shader* slash = mod->newMeshShader(gfx, "slash");
    REQUIRE(slash != nullptr);
    REQUIRE(slash->gpuHandle != nullptr);
    CHECK(slash->hasUniform("coreR"));
    CHECK(slash->hasUniform("intensity"));

    Shader* ember = mod->newMeshShader(gfx, "ember");
    REQUIRE(ember != nullptr);
    REQUIRE(ember->gpuHandle != nullptr);
    CHECK(ember->hasUniform("burnAmount"));

    Shader* aura = mod->newMeshShader(gfx, "aura");
    REQUIRE(aura != nullptr);
    REQUIRE(aura->gpuHandle != nullptr);
    CHECK(aura->hasUniform("auraR"));

    bool threw = false;
    try {
        mod->newMeshShader(gfx, "vignette");
    } catch (const eve::Exception&) {
        threw = true;
    }
    CHECK(threw);

    win->close();
}

// stylize_motion_echo_effect.cpp
using namespace eve::stylize;


// stylize_ysa.cpp
namespace {

constexpr int kYsaTestSize = 128;

/** @brief Draw one YSA frame into an offscreen canvas and read the pixels back. */
[[maybe_unused]] std::vector<unsigned char> drawYsaFrame(eve::graphics::Graphics* gfx, eve::graphics::Canvas* canvas,
                                        eve::graphics::Mesh* mesh, eve::graphics::Shader* shader,
                                        const glm::vec4& tint) {
    gfx->begin3DFrameToCanvas(canvas);
    gfx->drawMeshShader(mesh, glm::mat4(1.f), nullptr, tint, shader);
    gfx->end3DFrameToCanvas();
    std::unique_ptr<eve::image::ImageData> pixels(canvas->newImageData());
    if (!pixels) return {};
    const auto*  data = static_cast<const unsigned char*>(pixels->getData());
    const size_t size = size_t(kYsaTestSize) * size_t(kYsaTestSize) * 4u;
    return std::vector<unsigned char>(data, data + size);
}

[[maybe_unused]] size_t countDifferentPixels(const std::vector<unsigned char>& a, const std::vector<unsigned char>& b) {
    size_t changed = 0;
    for (size_t i = 0; i + 3 < a.size() && i + 3 < b.size(); i += 4)
        if (a[i] != b[i] || a[i + 1] != b[i + 1] || a[i + 2] != b[i + 2]) ++changed;
    return changed;
}

[[maybe_unused]] long totalLuma(const std::vector<unsigned char>& pixels) {
    long sum = 0;
    for (size_t i = 0; i + 3 < pixels.size(); i += 4)
        sum += long(pixels[i]) + long(pixels[i + 1]) + long(pixels[i + 2]);
    return sum;
}

}  // namespace


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
    const float    grey[]      = {0.2f, 0.2f, 0.2f, 1.f, 0.2f, 0.2f, 0.2f, 1.f, 0.2f, 0.2f, 0.2f, 1.f};
    auto*          whiteMesh   = gfx->newMeshFromArraysColored(positions, normals, uvs, white, 3, indices, 3);
    auto*          greyMesh    = gfx->newMeshFromArraysColored(positions, normals, uvs, grey, 3, indices, 3);
    REQUIRE(whiteMesh != nullptr);
    REQUIRE(greyMesh != nullptr);

    gfx->setMesh3DViewProj(glm::mat4(1.f));
    Lighting3DPack lighting{};
    lighting.ambient = glm::vec4(0.5f, 0.5f, 0.5f, 0.f);
    gfx->setMesh3DLighting(lighting);

    const std::vector<unsigned char> whiteFrame = drawYsaFrame(gfx, canvas, whiteMesh, shader, glm::vec4(1.f));
    const std::vector<unsigned char> greyFrame  = drawYsaFrame(gfx, canvas, greyMesh, shader, glm::vec4(1.f));
    REQUIRE(whiteFrame.size() == size_t(kYsaTestSize) * size_t(kYsaTestSize) * 4u);
    REQUIRE(greyFrame.size() == whiteFrame.size());
    REQUIRE(countDifferentPixels(whiteFrame, greyFrame) > 200);
    REQUIRE(totalLuma(whiteFrame) > totalLuma(greyFrame));
}
