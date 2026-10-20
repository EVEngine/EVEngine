#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_stylize_attack_vfx_audio_decal)
#include "PrivateTestScope.h"
#include <string_view>
#include "common/Capability.h"
#include "decal/Decal.h"
#include "stylize/AttackVfxLayerExecutor.h"
#include "stylize/AttackVfxRecipe.h"
#include "stylize/AttackVfxRuntime.h"
#include "stylize/Stylize.h"
#include "stylize/action/StylizeAction.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// stylize_attack_vfx_audio_decal.cpp
namespace test_stylize_attack_vfx_audio_decal {
using namespace eve::stylize;
using namespace eve::stylize_action;

namespace {

eve::LogicalId id(std::string_view value) {
    auto parsed = eve::LogicalId::parse(value);
    REQUIRE(parsed.has_value());
    return std::move(*parsed);
}

IAttackVfxLayerExecutor* executorFor(AttackVfxLayerRole role) {
    IAttackVfxLayerExecutor* found = nullptr;
    eve::cap::forEach<IAttackVfxLayerExecutor>([&](IAttackVfxLayerExecutor* executor) {
        if (!found && executor && executor->role() == role) found = executor;
    });
    return found;
}

}  // namespace

TEST_CASE("stylize.attack_vfx.layers audio prefab executors register") {
    auto* module = StylizeAction::create();
    REQUIRE(module != nullptr);
    auto* audio  = executorFor(AttackVfxLayerRole::Audio);
    auto* prefab = executorFor(AttackVfxLayerRole::Prefab);
    REQUIRE(audio != nullptr);
    REQUIRE(prefab != nullptr);

    AttackVfxLayer prefabLayer;
    prefabLayer.role = AttackVfxLayerRole::Prefab;
    prefabLayer.uri  = "prefab://impact-burst";
    AttackVfxLayerStartRequest request;
    request.layer = &prefabLayer;
    auto started  = prefab->start(request);
    REQUIRE(started.ok());
    CHECK(prefab->stop(started.value(), AttackVfxStopBehavior::ClearImmediately).ok());

    // Missing audio file soft-skips via NotFound (runtime path); direct start mirrors that.
    AttackVfxLayer audioLayer;
    audioLayer.role = AttackVfxLayerRole::Audio;
    audioLayer.uri  = "missing://no-such-clip.wav";
    request.layer   = &audioLayer;
    started         = audio->start(request);
    REQUIRE(!started.ok());
    CHECK(started.code() == eve::StatusCode::NotFound || started.code() == eve::StatusCode::Failed);
}

TEST_CASE("stylize.attack_vfx.layers decal solid executor") {
    auto* stylize = Stylize::create();
    auto* decal   = eve::decal::Decal::create();
    REQUIRE(stylize != nullptr);
    REQUIRE(decal != nullptr);

    auto* executor = executorFor(AttackVfxLayerRole::Decal);
    REQUIRE(executor != nullptr);

    AttackVfxLayer layer;
    layer.role                    = AttackVfxLayerRole::Decal;
    layer.uri                     = "decal://scorch";
    layer.floatParams["size"]     = 0.8f;
    layer.floatParams["lifetime"] = 0.2f;
    layer.floatParams["r"]        = 1.f;
    layer.floatParams["g"]        = 0.2f;
    layer.floatParams["b"]        = 0.05f;
    AttackVfxRequest play;
    play.sourceId = 1;
    play.targetId = 2;
    AttackVfxLayerStartRequest request;
    request.layer       = &layer;
    request.playRequest = &play;

    auto started = executor->start(request);
    // Graphics may be unavailable in bare unit_test; NotFound soft-skips in runtime.
    if (!started.ok()) {
        CHECK(started.code() == eve::StatusCode::NotFound);
        return;
    }
    REQUIRE(started.value().valid());
    CHECK(executor->update(started.value(), 0.016, request).ok());
    CHECK(executor->stop(started.value(), AttackVfxStopBehavior::ClearImmediately).ok());
}

TEST_CASE("stylize.attack_vfx.layers stylizeAction play advance api") {
    auto* stylize = Stylize::create();
    auto* module  = StylizeAction::create();
    auto* decal   = eve::decal::Decal::create();
    REQUIRE(stylize != nullptr);
    REQUIRE(module != nullptr);
    REQUIRE(decal != nullptr);

    const char* recipeJson = R"({
      "schema":"eve.stylize.attack-vfx","schemaVersion":1,"id":"attackvfx:combo",
      "phases":[
        {"kind":"anticipate","durationSeconds":0.1,"layers":[
          {"role":"prefab","uri":"prefab://charge"}
        ]},
        {"kind":"release","startCue":"impact","durationSeconds":0.25,"layers":[
          {"role":"meshVfx","uri":"style:slash"},
          {"role":"trail","uri":"trail://blade"},
          {"role":"camera","floatParams":{"posAmp":0.05,"duration":0.08}},
          {"role":"distortion","floatParams":{"strength":0.2}},
          {"role":"decal","uri":"decal://scorch"},
          {"role":"audio","uri":"missing://skip.wav"}
        ]}
      ]
    })";
    REQUIRE(module->registerRecipeJson(recipeJson).ok());

    auto played = module->play("attackvfx:combo", 3, 9);
    REQUIRE(played.ok());
    REQUIRE_EQ(module->activeCount(), 1u);

    auto advanced = module->advance(0.15);
    REQUIRE(advanced.ok());
    REQUIRE_EQ(module->activeCount(), 1u);

    auto signaled = module->signal(played.value().slot, played.value().generation, "impact");
    REQUIRE(signaled.ok());

    auto inspected = module->inspect(played.value().slot, played.value().generation);
    REQUIRE(inspected.ok());
    CHECK(inspected.value().age >= 0.0);

    advanced = module->advance(0.4);
    REQUIRE(advanced.ok());
    REQUIRE_EQ(module->activeCount(), 0u);
}

TEST_CASE("stylize.attack_vfx.layers multi role recipe soft skips missing audio") {
    auto* stylize = Stylize::create();
    auto* action  = StylizeAction::create();
    REQUIRE(stylize != nullptr);
    REQUIRE(action != nullptr);

    AttackVfxRuntime runtime;
    const char*      recipeJson = R"({
      "schema":"eve.stylize.attack-vfx","schemaVersion":1,"id":"attackvfx:elemental",
      "skin":{"id":"skin:fire","styleHints":{"meshStyle":"slash"},
              "shakeProfile":{"posAmp":0.04,"duration":0.1}},
      "phases":[{"kind":"release","durationSeconds":0.2,"layers":[
        {"role":"meshVfx","uri":"meshvfx://fire/slash"},
        {"role":"trail","uri":"trail://blade"},
        {"role":"prefab","uri":"prefab://ember"},
        {"role":"audio","uri":"missing://fire.wav"},
        {"role":"decal","uri":"decal://burn"}
      ]}]
    })";
    REQUIRE(runtime.registerRecipe(AttackVfxRecipe::fromJson(recipeJson).value()).ok());
    auto played = runtime.play(id("attackvfx:elemental"), {});
    REQUIRE(played.ok());
    REQUIRE_EQ(runtime.activeCount(), 1u);
    auto advanced = runtime.advance(0.3);
    REQUIRE(advanced.ok());
    REQUIRE_EQ(runtime.activeCount(), 0u);
}
}  // namespace test_stylize_attack_vfx_audio_decal
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_stylize_attack_vfx_layers)
#include "PrivateTestScope.h"
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
namespace test_stylize_attack_vfx_layers {
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
}  // namespace test_stylize_attack_vfx_layers
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_tactics_action)
#include "PrivateTestScope.h"
#include "action/Action.h"
#include "common/ECS.h"
#include "tactics/Tactics.h"
#include "tactics/TacticsAction.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// tactics_action.cpp
namespace test_tactics_action {
namespace {

eve::SubjectRef subject(const char* text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

}  // namespace

TEST_CASE("tactics.moveCommitsThroughSharedActionRuntime") {
    ecs::Table            world;
    ecs::ScopedTable      guard(world);
    eve::tactics::Tactics tactics;

    auto battleResult = tactics.newBattle(subject("00000000-0000-0000-0000-000000000040"), 9);
    REQUIRE(battleResult.ok());
    const auto battleHandle = std::move(battleResult).takeValue();
    REQUIRE(tactics.addCell(battleHandle, {0, 0, 0}).ok());
    REQUIRE(tactics.addCell(battleHandle, {1, 0, 0}).ok());
    auto sideResult = tactics.newSide(battleHandle, subject("00000000-0000-0000-0000-000000000041"));
    REQUIRE(sideResult.ok());
    const auto sideHandle  = std::move(sideResult).takeValue();
    const auto unitSubject = subject("00000000-0000-0000-0000-000000000042");
    auto       unitResult  = tactics.newUnit(battleHandle, sideHandle, unitSubject, {}, {0, 0, 0}, {1, 100, 0, 1});
    REQUIRE(unitResult.ok());
    const auto unitHandle = std::move(unitResult).takeValue();

    REQUIRE(tactics.start(battleHandle, eve::tactics::TurnPolicyKind::Initiative).ok());
    REQUIRE(tactics.advance(battleHandle, {eve::SimulationTick(1), eve::Duration::zero()}).ok());
    REQUIRE(tactics.advance(battleHandle, {eve::SimulationTick(2), eve::Duration::zero()}).ok());
    REQUIRE(tactics.advance(battleHandle, {eve::SimulationTick(3), eve::Duration::zero()}).ok());

    auto* battle = dynamic_cast<eve::tactics::Battle*>(ecs::try_get(battleHandle));
    REQUIRE(battle != nullptr);
    eve::tactics::MoveActionExecutor executor(*battle);
    eve::action::ActionServices      services;
    services.effects = &executor;
    eve::action::ActionRuntime runtime(services);
    auto                       request = eve::tactics::makeMoveRequest(unitHandle, {1, 0, 0}, eve::SimulationTick(3));
    REQUIRE(request.ok());
    auto submitted = runtime.submit(eve::tactics::moveActionDefinition(), std::move(request).takeValue());
    REQUIRE(submitted.ok());
    const auto execution = std::move(submitted).takeValue();
    auto       advanced  = runtime.advance(execution, eve::SimulationTick(4), eve::Duration::zero());
    REQUIRE(advanced.ok());
    CHECK_EQ(static_cast<int>(std::move(advanced).takeValue().phase),
             static_cast<int>(eve::action::ActionPhase::Completed));

    const auto position = battle->board()->value.position(unitSubject);
    REQUIRE(position.has_value());
    CHECK((*position == eve::tactics::Cell{1, 0, 0}));
    auto* unit = dynamic_cast<eve::tactics::TacticalUnit*>(ecs::try_get(unitHandle));
    REQUIRE(unit != nullptr);
    CHECK_EQ(unit->turn()->movePoints, 0);

    auto faceRequest = eve::tactics::makeFaceRequest(unitHandle, 2, eve::SimulationTick(4));
    REQUIRE(faceRequest.ok());
    auto faceExecution = runtime.submit(eve::tactics::faceActionDefinition(), std::move(faceRequest).takeValue());
    REQUIRE(faceExecution.ok());
    auto faceAdvanced =
        runtime.advance(std::move(faceExecution).takeValue(), eve::SimulationTick(5), eve::Duration::zero());
    REQUIRE(faceAdvanced.ok());
    CHECK_EQ(unit->position()->facing, 2);

    auto waitRequest = eve::tactics::makeWaitRequest(unitHandle, eve::SimulationTick(5));
    REQUIRE(waitRequest.ok());
    auto waitExecution = runtime.submit(eve::tactics::waitActionDefinition(), std::move(waitRequest).takeValue());
    REQUIRE(waitExecution.ok());
    auto waitAdvanced =
        runtime.advance(std::move(waitExecution).takeValue(), eve::SimulationTick(6), eve::Duration::zero());
    REQUIRE(waitAdvanced.ok());
    CHECK_EQ(static_cast<int>(battle->turn()->phase), static_cast<int>(eve::tactics::BattlePhase::TurnEnd));
}

TEST_CASE("tactics.moveActionPrepareFailureDoesNotMutateBoard") {
    ecs::Table            world;
    ecs::ScopedTable      guard(world);
    eve::tactics::Tactics tactics;

    auto battleResult = tactics.newBattle(subject("00000000-0000-0000-0000-000000000050"));
    REQUIRE(battleResult.ok());
    const auto battleHandle = std::move(battleResult).takeValue();
    REQUIRE(tactics.addCell(battleHandle, {0, 0, 0}).ok());
    REQUIRE(tactics.addCell(battleHandle, {1, 0, 0}).ok());
    auto sideResult = tactics.newSide(battleHandle, subject("00000000-0000-0000-0000-000000000051"));
    REQUIRE(sideResult.ok());
    const auto sideHandle = std::move(sideResult).takeValue();
    const auto actor      = subject("00000000-0000-0000-0000-000000000052");
    auto       unitResult = tactics.newUnit(battleHandle, sideHandle, actor, {}, {0, 0, 0}, {1, 0, 0, 1});
    REQUIRE(unitResult.ok());
    const auto unitHandle = std::move(unitResult).takeValue();
    REQUIRE(tactics.start(battleHandle, eve::tactics::TurnPolicyKind::Initiative).ok());
    REQUIRE(tactics.advance(battleHandle, {eve::SimulationTick(1), eve::Duration::zero()}).ok());
    REQUIRE(tactics.advance(battleHandle, {eve::SimulationTick(2), eve::Duration::zero()}).ok());
    REQUIRE(tactics.advance(battleHandle, {eve::SimulationTick(3), eve::Duration::zero()}).ok());

    auto* battle = dynamic_cast<eve::tactics::Battle*>(ecs::try_get(battleHandle));
    REQUIRE(battle != nullptr);
    eve::tactics::MoveActionExecutor executor(*battle);
    eve::action::ActionServices      services;
    services.effects = &executor;
    eve::action::ActionRuntime runtime(services);
    const auto                 beforeRevision = battle->turn()->revision;
    const auto                 beforeEvents   = battle->events()->values.size();
    const auto                 beforeCommands = battle->commands()->values.size();
    const auto                 beforeStreams  = battle->random()->streams.size();
    auto                       request = eve::tactics::makeMoveRequest(unitHandle, {1, 0, 0}, eve::SimulationTick(3));
    REQUIRE(request.ok());
    auto submitted = runtime.submit(eve::tactics::moveActionDefinition(), std::move(request).takeValue());
    REQUIRE(submitted.ok());
    auto advanced = runtime.advance(std::move(submitted).takeValue(), eve::SimulationTick(4), eve::Duration::zero());
    CHECK(!advanced.ok());
    const auto position = battle->board()->value.position(actor);
    REQUIRE(position.has_value());
    CHECK((*position == eve::tactics::Cell{0, 0, 0}));
    CHECK_EQ(battle->turn()->revision, beforeRevision);
    CHECK_EQ(battle->events()->values.size(), beforeEvents);
    CHECK_EQ(battle->commands()->values.size(), beforeCommands);
    CHECK_EQ(battle->random()->streams.size(), beforeStreams);
}

TEST_CASE("tactics.moveActionRejectsStaleExpectedRevision") {
    ecs::Table            world;
    ecs::ScopedTable      guard(world);
    eve::tactics::Tactics tactics;
    auto                  battleResult = tactics.newBattle(subject("00000000-0000-0000-0000-000000000055"));
    REQUIRE(battleResult.ok());
    const auto battleHandle = std::move(battleResult).takeValue();
    REQUIRE(tactics.addCell(battleHandle, {0, 0, 0}).ok());
    REQUIRE(tactics.addCell(battleHandle, {1, 0, 0}).ok());
    auto side = tactics.newSide(battleHandle, subject("00000000-0000-0000-0000-000000000056"));
    REQUIRE(side.ok());
    const auto actor = subject("00000000-0000-0000-0000-000000000057");
    auto       unit  = tactics.newUnit(battleHandle, side.value(), actor, {}, {0, 0, 0}, {1, 100, 0, 1});
    REQUIRE(unit.ok());
    const auto unitHandle = std::move(unit).takeValue();
    REQUIRE(tactics.start(battleHandle, eve::tactics::TurnPolicyKind::Initiative).ok());
    REQUIRE(tactics.advance(battleHandle, {eve::SimulationTick(1), eve::Duration::zero()}).ok());
    REQUIRE(tactics.advance(battleHandle, {eve::SimulationTick(2), eve::Duration::zero()}).ok());
    REQUIRE(tactics.advance(battleHandle, {eve::SimulationTick(3), eve::Duration::zero()}).ok());
    auto request = eve::tactics::makeMoveRequest(unitHandle, {1, 0, 0}, eve::SimulationTick(3));
    REQUIRE(request.ok());
    REQUIRE(tactics.faceUnit(battleHandle, actor, 1).ok());

    auto* battle = dynamic_cast<eve::tactics::Battle*>(ecs::try_get(battleHandle));
    REQUIRE(battle != nullptr);
    eve::tactics::MoveActionExecutor executor(*battle);
    eve::action::ActionServices      services;
    services.effects = &executor;
    eve::action::ActionRuntime runtime(services);
    auto submitted = runtime.submit(eve::tactics::moveActionDefinition(), std::move(request).takeValue());
    REQUIRE(submitted.ok());
    auto advanced = runtime.advance(std::move(submitted).takeValue(), eve::SimulationTick(4), eve::Duration::zero());
    CHECK(!advanced.ok());
    CHECK_EQ(advanced.error()->code(), eve::DiagnosticCode::Conflict);
    CHECK_EQ(battle->board()->value.position(actor).value(), (eve::tactics::Cell{0, 0, 0}));
}
}  // namespace test_tactics_action
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_tactics_rpg_integration)
#include "PrivateTestScope.h"
#include "common/ECS.h"
#include "tactics/Tactics.h"
#include "tactics/TacticsSettlement.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// tactics_rpg_integration.cpp
namespace test_tactics_rpg_integration {
namespace {

eve::SubjectRef subject(const char* text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

class HealthPolicy final : public eve::settlement::ISettlementPolicy {
public:
    explicit HealthPolicy(double& health) : health_(health) {}

    eve::Result<void> validate(eve::settlement::SettlementContext&) override { return eve::Result<void>::success(); }
    eve::Result<void> sourceModifiers(eve::settlement::SettlementContext&) override {
        return eve::Result<void>::success();
    }
    eve::Result<void> targetMitigation(eve::settlement::SettlementContext&) override {
        return eve::Result<void>::success();
    }
    eve::Result<void> armorShield(eve::settlement::SettlementContext&) override { return eve::Result<void>::success(); }
    eve::Result<void> clamp(eve::settlement::SettlementContext& context) override {
        return context.setClampMax(health_);
    }
    eve::Result<eve::settlement::PreparedApply> prepareApply(
        const eve::settlement::SettlementContext& context) override {
        const double before = health_;
        const double after  = before - context.magnitude();
        return eve::Result<eve::settlement::PreparedApply>::success(eve::settlement::PreparedApply(
            [this, after]() {
                health_ = after;
                return eve::Result<void>::success();
            },
            [this, before]() { health_ = before; }));
    }

private:
    double& health_;
};

}  // namespace

TEST_CASE("tactics.rpgSettlementOutcomeDefeatsUnitAndCompletesObjective") {
    ecs::Table            world;
    ecs::ScopedTable      guard(world);
    eve::tactics::Tactics tactics;
    auto                  battleResult = tactics.newBattle(subject("00000000-0000-0000-0000-0000000000f0"));
    REQUIRE(battleResult.ok());
    const auto battle = std::move(battleResult).takeValue();
    REQUIRE(tactics.addCell(battle, {0, 0, 0}).ok());
    REQUIRE(tactics.addCell(battle, {1, 0, 0}).ok());
    const auto blue     = subject("00000000-0000-0000-0000-0000000000f1");
    const auto red      = subject("00000000-0000-0000-0000-0000000000f2");
    const auto attacker = subject("00000000-0000-0000-0000-0000000000f3");
    const auto defender = subject("00000000-0000-0000-0000-0000000000f4");
    auto       blueSide = tactics.newSide(battle, blue);
    auto       redSide  = tactics.newSide(battle, red);
    REQUIRE(blueSide.ok());
    REQUIRE(redSide.ok());
    REQUIRE(tactics.newUnit(battle, blueSide.value(), attacker, {}, {0, 0, 0}, {1, 300, 0, 10}).ok());
    REQUIRE(tactics.newUnit(battle, redSide.value(), defender, {}, {1, 0, 0}, {1, 300, 0, 5}).ok());
    eve::tactics::ObjectiveSpec objective;
    objective.id              = *eve::LogicalId::parse("test:eliminate-after-settlement");
    objective.kind            = eve::tactics::ObjectiveKind::EliminateSide;
    objective.beneficiarySide = blue;
    objective.targetSide      = red;
    REQUIRE(tactics.addObjective(battle, objective).ok());
    REQUIRE(tactics.start(battle, eve::tactics::TurnPolicyKind::Initiative).ok());
    for (std::uint64_t tick = 1; tick <= 3; ++tick)
        REQUIRE(tactics.advance(battle, {eve::SimulationTick(tick), eve::Duration::fromNanoseconds(1)}).ok());

    const auto strike = eve::LogicalId::parse("test:settlement-strike");
    REQUIRE(strike.has_value());
    auto declared = tactics.useAbility(battle, attacker, *strike, {1, 0, 0}, defender, "{\"power\":12}");
    REQUIRE(declared.ok());

    eve::settlement::SettlementRule armor;
    armor.id           = "tactics.integration.armor";
    armor.source       = "test:defender";
    armor.stage        = eve::settlement::StageKind::TargetMitigation;
    armor.operation    = eve::settlement::RuleOperation::ResistFlat;
    armor.value        = 2.0;
    armor.filter.kinds = {"damage"};
    eve::settlement::SettlementRuleSet rules;
    REQUIRE(rules.configure({armor}).ok());
    eve::tactics::TacticsSettlementRuntime runtime;
    REQUIRE(runtime.configureSettlementRules(rules).ok());

    eve::tactics::AbilitySettlementRequest request;
    request.ability     = declared.value();
    request.kind        = "damage";
    request.magnitude   = 12.0;
    request.tick        = eve::SimulationTick(3);
    double       health = 8.0;
    HealthPolicy policy(health);
    auto         settled = runtime.settle(request, policy);
    REQUIRE(settled.ok());
    CHECK_EQ(settled.value().applied, 8.0);
    REQUIRE(health <= 0.0);

    // Tactics consumes only the canonical settlement outcome. It never reads
    // the health owner's state or retains its policy.
    REQUIRE(tactics.defeatUnit(battle, defender).ok());
    auto status = tactics.status(battle);
    REQUIRE(status.ok());
    CHECK_EQ(static_cast<int>(status.value()), static_cast<int>(eve::tactics::BattleStatus::Ended));
}
}  // namespace test_tactics_rpg_integration
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_tactics_settlement)
#include "PrivateTestScope.h"
#include <array>
#include "tactics/TacticsSettlement.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// tactics_settlement.cpp
namespace test_tactics_settlement {
namespace {

eve::SubjectRef subject(std::uint8_t value) {
    std::array<std::uint8_t, 16> bytes{};
    bytes[15] = value;
    return eve::SubjectRef::fromPersistentId(eve::PersistentId(bytes));
}

class TacticalHealthPolicy final : public eve::settlement::ISettlementPolicy {
public:
    explicit TacticalHealthPolicy(double& health, bool failCommit = false) : health_(health), failCommit_(failCommit) {}
    eve::Result<void> validate(eve::settlement::SettlementContext&) override { return eve::Result<void>::success(); }
    eve::Result<void> sourceModifiers(eve::settlement::SettlementContext&) override {
        return eve::Result<void>::success();
    }
    eve::Result<void> targetMitigation(eve::settlement::SettlementContext&) override {
        return eve::Result<void>::success();
    }
    eve::Result<void> armorShield(eve::settlement::SettlementContext&) override { return eve::Result<void>::success(); }
    eve::Result<void> clamp(eve::settlement::SettlementContext& context) override {
        return context.setClampMax(health_);
    }
    eve::Result<eve::settlement::PreparedApply> prepareApply(
        const eve::settlement::SettlementContext& context) override {
        const double before = health_;
        const double after  = before - context.magnitude();
        return eve::Result<eve::settlement::PreparedApply>::success(eve::settlement::PreparedApply(
            [this, after]() {
                health_ = after;
                if (failCommit_)
                    return eve::Result<void>::failure(
                        eve::Diagnostic::error(eve::DiagnosticCode::Failed, "injected area commit failure"));
                return eve::Result<void>::success();
            },
            [this, before]() { health_ = before; }));
    }

private:
    double& health_;
    bool    failCommit_ = false;
};

}  // namespace

TEST_CASE("tactics.settlement.committedAbilityUsesSharedRules") {
    eve::settlement::SettlementRule highGround;
    highGround.id                  = "tactics.high-ground";
    highGround.source              = "board:height";
    highGround.stage               = eve::settlement::StageKind::SourceModifiers;
    highGround.operation           = eve::settlement::RuleOperation::Multiply;
    highGround.value               = 1.5;
    highGround.filter.kinds        = {"damage"};
    highGround.filter.requiredTags = {"tactics:ability"};
    highGround.when                = "context.range <= 3 && context.target_hp_ratio < 0.5";
    eve::settlement::SettlementRuleSet rules;
    REQUIRE(rules.configure({highGround}).ok());

    eve::tactics::TacticsSettlementRuntime runtime;
    REQUIRE(runtime.configureSettlementRules(rules).ok());
    eve::tactics::AbilitySettlementRequest request;
    request.ability.actor      = subject(1);
    request.ability.targetUnit = subject(2);
    request.kind               = "damage";
    request.magnitude          = 20.0;
    request.tick               = eve::SimulationTick(7);
    eve::Value::Object context;
    context["range"]            = 2.0;
    context["target_hp_ratio"]  = 0.4;
    request.context             = eve::Value(std::move(context));
    double               health = 100.0;
    TacticalHealthPolicy policy(health);
    auto                 settled = runtime.settle(request, policy);
    REQUIRE(settled.ok());
    CHECK_EQ(settled.value().applied, 30.0);
    CHECK_EQ(health, 70.0);
}

TEST_CASE("tactics.settlement.cellOnlyAbilityUsesExplicitStableTarget") {
    eve::tactics::AbilitySettlementRequest request;
    request.ability.actor      = subject(3);
    request.ability.targetCell = {4, 5, 0};
    request.target             = subject(4);
    request.kind               = "hazard";
    request.magnitude          = 12.0;
    request.tick               = eve::SimulationTick(8);

    double                                 durability = 40.0;
    TacticalHealthPolicy                   policy(durability);
    eve::tactics::TacticsSettlementRuntime runtime;
    auto                                   canonical = eve::tactics::makeSettlementRequest(request);
    REQUIRE(canonical.ok());
    CHECK_EQ(canonical.value().target, request.target);
    auto settled = runtime.settle(request, policy);
    REQUIRE(settled.ok());
    CHECK_EQ(durability, 28.0);
}

TEST_CASE("tactics.settlement.areaAbilityUsesAtomicBatchAndRollsBackAllTargets") {
    std::array<eve::tactics::AbilitySettlementRequest, 2> requests;
    for (std::size_t index = 0; index < requests.size(); ++index) {
        requests[index].ability.actor      = subject(5);
        requests[index].ability.targetUnit = subject(static_cast<std::uint8_t>(6 + index));
        requests[index].ability.targetCell = {static_cast<int>(index), 1, 0};
        requests[index].kind               = "damage";
        requests[index].magnitude          = 15.0;
        requests[index].tick               = eve::SimulationTick(9);
    }

    double                                             firstHealth  = 100.0;
    double                                             secondHealth = 100.0;
    TacticalHealthPolicy                               first(firstHealth);
    TacticalHealthPolicy                               second(secondHealth, true);
    std::array<eve::settlement::ISettlementPolicy*, 2> policies = {&first, &second};
    eve::tactics::TacticsSettlementRuntime             runtime;
    auto                                               failed = runtime.settleAtomic(requests, policies);
    CHECK(!failed.ok());
    CHECK_EQ(firstHealth, 100.0);
    CHECK_EQ(secondHealth, 100.0);

    TacticalHealthPolicy secondRetry(secondHealth);
    policies[1]  = &secondRetry;
    auto settled = runtime.settleAtomic(requests, policies);
    REQUIRE(settled.ok());
    REQUIRE_EQ(settled.value().size(), 2u);
    CHECK_EQ(firstHealth, 85.0);
    CHECK_EQ(secondHealth, 85.0);
}
}  // namespace test_tactics_settlement
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_time_subscription_consumers)
#include "PrivateTestScope.h"
#include <cmath>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include "animation/AnimClipRegistry.h"
#include "animation/AnimTrail.h"
#include "animation/Animation.h"
#include "animation/Tween.h"
#include "common/Time.h"
#include "effects/EffectContainer.h"
#include "particles/ParticleEmitter.h"
#include "particles/ParticleRuntime.h"
#include "particles/ParticleSystem.h"
#include "particles/Particles.h"
#include "platform_event/PlatformEvent.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// time_subscription_consumers.cpp
namespace test_time_subscription_consumers {
namespace {

class TestEvent final : public eve::platform_event::PlatformEvent {
public:
    void                          pump() override {}
    eve::platform_event::Message* wait() override { return nullptr; }
};

}  // namespace

TEST_CASE("time.checkedConsumers.useSchedulerTickWithoutPrivateRate") {
    auto* particles = eve::particles::Particles::create();
    auto* emitter   = particles->newEmitter(4);
    emitter->setRandomSeed(7);
    emitter->setParticleLifetime(10.f, 10.f);
    emitter->setDirection(0.f);
    emitter->setSpeed(10.f, 10.f);
    emitter->setPlaybackSpeed(0.f);
    emitter->setFixedTimeStep(0.5f, 1);
    emitter->emit(1);

    auto advanced = emitter->advance({eve::SimulationTick(1), eve::Duration::fromNanoseconds(100000000)});
    REQUIRE(advanced.ok());
    CHECK(std::abs(emitter->sim()->particles[0].x - 1.f) < 1e-5f);
    CHECK(emitter->currentSimulationTick() == eve::SimulationTick(1));

    auto duplicate = emitter->advance({eve::SimulationTick(1), eve::Duration::fromNanoseconds(100000000)});
    CHECK(!duplicate);
    CHECK(static_cast<int>(duplicate.code()) == static_cast<int>(eve::StatusCode::Conflict));
}

TEST_CASE("time.checkedConsumers.effectsAndAnimationRecordTick") {
    eve::effects::EffectContainer  effects;
    eve::effects::EffectDefinition definition;
    definition.id       = "temporary";
    definition.duration = 1.0;
    auto applied        = effects.apply(definition, "subject");
    REQUIRE(applied.ok());
    const std::string id = std::move(applied).takeValue();
    REQUIRE(effects.find(id) != nullptr);

    auto effectStep = effects.advance({eve::SimulationTick(3), eve::Duration::fromNanoseconds(250000000)});
    REQUIRE(effectStep.ok());
    CHECK(effectStep.value().tick == eve::SimulationTick(3));
    CHECK(std::abs(effects.find(id)->remaining - 0.75) < 1e-9);

    auto                                   animation = eve::animation::Animation::create();
    std::unique_ptr<eve::animation::Tween> tween(animation->newTween(1.f));
    tween->setFrom("x", 0.f);
    tween->setTo("x", 10.f);
    tween->start();
    auto animationStep = animation->advance({eve::SimulationTick(3), eve::Duration::fromNanoseconds(500000000)});
    REQUIRE(animationStep.ok());
    CHECK(std::abs(tween->get("x") - 5.f) < 1e-4f);
    CHECK(animation->currentTick() == eve::SimulationTick(3));

    (void)effectStep.value();
}

TEST_CASE("time.checkedConsumers.animationChildrenUseInjectedStep") {
    eve::animation::Tween tween(1.f);
    tween.setFrom("x", 0.f);
    tween.setTo("x", 10.f);
    tween.start();
    auto first = tween.advance({eve::SimulationTick(4), eve::Duration::fromNanoseconds(250000000)});
    REQUIRE(first.ok());
    CHECK(std::abs(tween.get("x") - 2.5f) < 1e-4f);
    CHECK(tween.currentTick() == eve::SimulationTick(4));

    eve::animation::AnimTrail trail;
    trail.addPoint(1.f, 2.f);
    auto trailStep = trail.advance({eve::SimulationTick(1), eve::Duration::fromNanoseconds(100000000)});
    REQUIRE(trailStep.ok());
    CHECK(std::abs(trail.getPointAge(0) - 0.1f) < 1e-5f);
    auto duplicate = trail.advance({eve::SimulationTick(1), eve::Duration::zero()});
    CHECK(!duplicate.ok());

    auto*                                  animation = eve::animation::Animation::create();
    std::unique_ptr<eve::animation::Tween> hosted(animation->newTween(1.f));
    hosted->setFrom("x", 0.f);
    hosted->setTo("x", 10.f);
    hosted->start();
    auto direct = hosted->advance({eve::SimulationTick(8), eve::Duration::zero()});
    REQUIRE(direct.ok());
    auto rejectedHost = animation->advance({eve::SimulationTick(8), eve::Duration::zero()});
    CHECK(!rejectedHost.ok());
    CHECK(std::abs(hosted->get("x")) < 1e-5f);
}

TEST_CASE("time.checkedConsumers.particleStatsCommitAsOneFrame") {
    auto* particles = eve::particles::Particles::create();
    auto* emitter   = particles->newEmitter(4);
    REQUIRE(emitter != nullptr);
    CHECK(emitter->getConfigReloadObservation() == std::string("unbound"));

    auto applied = eve::particles::ParticleSimSystem::advance({eve::SimulationTick(1), eve::Duration::zero()});
    REQUIRE(applied.ok());
    const auto committed = eve::particles::particleFrameStats();

    auto duplicate =
        eve::particles::ParticleSimSystem::advance({eve::SimulationTick(1), eve::Duration::fromNanoseconds(1)});
    CHECK(!duplicate.ok());
    const auto afterDuplicate = eve::particles::particleFrameStats();
    CHECK(afterDuplicate.frameIndex == committed.frameIndex);
    CHECK(afterDuplicate.simulationTick == committed.simulationTick);
    CHECK(afterDuplicate.emittersTotal == committed.emittersTotal);
    CHECK(afterDuplicate.particlesAfter == committed.particlesAfter);

    auto invalid =
        eve::particles::ParticleSimSystem::advance({eve::SimulationTick(2), eve::Duration::fromNanoseconds(-1)});
    CHECK(!invalid.ok());
    const auto afterInvalid = eve::particles::particleFrameStats();
    CHECK(afterInvalid.frameIndex == committed.frameIndex);
    CHECK(afterInvalid.simulationTick == committed.simulationTick);
}

TEST_CASE("subscription.eventPoll.containsCallbackFailureAfterCommit") {
    TestEvent event;
    int       observed = 0;
    auto      throwing = event.subscribePoll([&](const eve::platform_event::Message&) {
        ++observed;
        throw std::runtime_error("listener failure");
    });
    auto      message  = std::make_unique<eve::platform_event::Message>("committed");
    event.push(std::move(message));

    auto polled = event.pollOwned();
    REQUIRE(polled.get() != nullptr);
    CHECK_EQ(polled->name, std::string("committed"));
    CHECK_EQ(observed, 1);
    CHECK_EQ(event.pollObserverFailureCount(), std::uint64_t{1});
    throwing.dispose();
}

TEST_CASE("subscription.eventPoll.supportsReentrantSubscribeAndDispose") {
    TestEvent         event;
    int               first    = 0;
    int               second   = 0;
    bool              replaced = false;
    eve::Subscription replacement;
    auto              primary = event.subscribePoll([&](const eve::platform_event::Message&) {
        ++first;
        if (replaced) return;
        replaced = true;
        replacement.dispose();
        replacement = event.subscribePoll([&](const eve::platform_event::Message&) { ++second; });
    });
    replacement               = event.subscribePoll([&](const eve::platform_event::Message&) { ++second; });

    event.pushData("first");
    auto firstPolled = event.pollOwned();
    REQUIRE(firstPolled.get() != nullptr);
    CHECK_EQ(first, 1);
    CHECK_EQ(second, 0);

    event.pushData("second");
    auto secondPolled = event.pollOwned();
    REQUIRE(secondPolled.get() != nullptr);
    CHECK_EQ(first, 2);
    CHECK_EQ(second, 1);
    primary.dispose();
    replacement.dispose();
}

TEST_CASE("subscription.animClipReload.supportsAbsentProviderAndReentrancy") {
    eve::animation::AnimClipRegistry::clear();
    int               first    = 0;
    int               second   = 0;
    bool              replaced = false;
    eve::Subscription replacement;
    auto              primary =
        eve::animation::AnimClipRegistry::subscribeReload([&](const eve::animation::AnimClipRegistry::ReloadEvent&) {
            ++first;
            if (replaced) return;
            replaced = true;
            replacement.dispose();
            replacement = eve::animation::AnimClipRegistry::subscribeReload(
                [&](const eve::animation::AnimClipRegistry::ReloadEvent&) { ++second; });
        });
    replacement = eve::animation::AnimClipRegistry::subscribeReload(
        [&](const eve::animation::AnimClipRegistry::ReloadEvent&) { ++second; });

    auto absent = eve::animation::AnimClipRegistry::reloadPath("missing.anim.txt");
    REQUIRE(absent.ok());
    const int absentCount = std::move(absent).takeValue();
    CHECK_EQ(absentCount, 0);
    CHECK_EQ(first, 1);
    CHECK_EQ(second, 0);

    auto absentAgain = eve::animation::AnimClipRegistry::reloadPath("missing.anim.txt");
    REQUIRE(absentAgain.ok());
    const int absentAgainCount = std::move(absentAgain).takeValue();
    CHECK_EQ(absentAgainCount, 0);
    CHECK_EQ(first, 2);
    CHECK_EQ(second, 1);
    primary.dispose();
    replacement.dispose();
}
}  // namespace test_time_subscription_consumers
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_ui_motion_bind)
#include "PrivateTestScope.h"
#include <cmath>
#include <string>
#include "animation/Animation.h"
#include "common/Time.h"
#include "ui/UI.h"
#include "ui/UIHost.h"
#include "ui/UiMotionSinks.h"
#include "ui/UiTween.h"
#include "ui/Widget.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// ui_motion_bind.cpp
namespace test_ui_motion_bind {
namespace ui = eve::ui;
using eve::animation::Animation;
using eve::animation::MotionVec2;

namespace {

eve::SimulationStep stepAt(std::uint64_t tick, double seconds) {
    return eve::SimulationStep{eve::SimulationTick{tick}, eve::Duration::fromSeconds(seconds).expect("ui motion dt")};
}

ui::UIHost* resolveHost(ui::UIHostHandle handle) {
    auto host = ui::UIHost::resolve(handle);
    return host ? &host->get() : nullptr;
}

}  // namespace

TEST_CASE("UI.motion.bindHostPosAndOpacity") {
    auto*   anim  = Animation::create();
    ui::UI* uimod = ui::UI::create();
    REQUIRE(ui::UIHost::resolve(
                uimod->mountAs("motion-bind", ui::window("T", {ui::text("hi", "hi"), ui::button("Go", "go")}, "root")))
                .has_value());
    const ui::UIHostHandle handle = uimod->current();
    ui::UIHost*            host   = resolveHost(handle);
    REQUIRE(host != nullptr);
    host->meta()->hasPos = true;
    host->meta()->posX   = 0.f;
    host->meta()->posY   = 0.f;

    ui::UiHostPosSink     posSink(handle);
    ui::UiNodeOpacitySink opacitySink(handle, "go");

    auto pos = anim->motionVec2(MotionVec2{0.f, 0.f}, MotionVec2{100.f, 40.f}, 1.f)
                   .ease("linear")
                   .bind(posSink)
                   .expect("host pos");
    auto fade = anim->motion(1.f, 0.25f, 1.f).ease("linear").bind(opacitySink).expect("opacity");

    {
        auto advanced = anim->advance(stepAt(1, 0.5));
        REQUIRE(advanced.ok());
    }
    CHECK(std::abs(host->meta()->posX - 50.f) < 1e-3f);
    CHECK(std::abs(host->meta()->posY - 20.f) < 1e-3f);
    auto go = host->findById("go");
    REQUIRE(go.has_value());
    CHECK(std::abs(go->get().opacity - 0.625f) < 1e-3f);

    {
        auto advanced = anim->advance(stepAt(2, 0.5));
        REQUIRE(advanced.ok());
    }
    CHECK(std::abs(host->meta()->posX - 100.f) < 1e-3f);
    CHECK(std::abs(host->meta()->posY - 40.f) < 1e-3f);
    CHECK(std::abs(go->get().opacity - 0.25f) < 1e-3f);
    CHECK(!anim->motions().isActive(pos));
    CHECK(!anim->motions().isActive(fade));
}

TEST_CASE("UI.motion.staleHostCancelsOnError") {
    auto* anim   = Animation::create();
    auto  handle = ui::UIHost::createHost("motion-stale");
    {
        auto host = ui::UIHost::resolve(handle);
        REQUIRE(host.has_value());
        host->get().setTree(ui::window("W", {ui::text("hi", "hi")}, "root"));
        host->get().meta()->hasPos = true;
        host->get().meta()->posX   = 0.f;
        host->get().meta()->posY   = 0.f;
    }

    ui::UiHostPosSink sink(handle);
    int               cancelled = 0;
    auto              motion    = anim->motionVec2(MotionVec2{0.f, 0.f}, MotionVec2{10.f, 10.f}, 1.f)
                      .ease("linear")
                      .cancelOnError(true)
                      .onCancel([&] { ++cancelled; })
                      .bind(sink)
                      .expect("spawn");

    {
        auto advanced = anim->advance(stepAt(1, 0.25));
        REQUIRE(advanced.ok());
    }
    {
        auto host = ui::UIHost::resolve(handle);
        REQUIRE(host.has_value());
        ecs::DestroyEntity(&host->get());
    }
    CHECK(!ui::UIHost::resolve(handle).has_value());

    {
        auto advanced = anim->advance(stepAt(2, 0.25));
        // Sink write fails with StaleHandle; cancelOnError recycles the slot.
        CHECK(!advanced.ok());
    }
    CHECK_EQ(cancelled, 1);
    CHECK(!anim->motions().isActive(motion));
}

TEST_CASE("UI.motion.bindHostSizeOverlayAndItemPos") {
    auto*   anim  = Animation::create();
    ui::UI* uimod = ui::UI::create();
    uimod->beginBuild();
    uimod->beginWindow("Panel", "root");
    uimod->beginFlex("row", "flex");
    uimod->addButton("Go", "go");
    uimod->setItemAbsolute(0.f, 0.f, 0.f, 0.f);
    uimod->end();
    uimod->end();
    REQUIRE(uimod->mountBuildAs("motion-size"));
    const ui::UIHostHandle handle = uimod->current();
    ui::UIHost*            host   = resolveHost(handle);
    REQUIRE(host != nullptr);
    host->meta()->hasSize        = true;
    host->meta()->sizeX          = 100.f;
    host->meta()->sizeY          = 50.f;
    host->meta()->overlayBgAlpha = 1.f;

    ui::UiHostSizeSink         sizeSink(handle);
    ui::UiHostOverlayAlphaSink alphaSink(handle);
    ui::UiNodePosSink          posSink(handle, "go");

    anim->motionVec2(MotionVec2{100.f, 50.f}, MotionVec2{200.f, 120.f}, 0.5f)
        .ease("linear")
        .bind(sizeSink)
        .expect("size");
    anim->motion(1.f, 0.f, 0.5f).ease("linear").bind(alphaSink).expect("alpha");
    anim->motionVec2(MotionVec2{0.f, 0.f}, MotionVec2{30.f, 40.f}, 0.5f)
        .ease("linear")
        .bind(posSink)
        .expect("item pos");

    {
        auto advanced = anim->advance(stepAt(1, 0.5));
        REQUIRE(advanced.ok());
    }
    CHECK(std::abs(host->meta()->sizeX - 200.f) < 1e-3f);
    CHECK(std::abs(host->meta()->sizeY - 120.f) < 1e-3f);
    CHECK(std::abs(host->meta()->overlayBgAlpha - 0.f) < 1e-3f);
    auto go = host->findById("go");
    REQUIRE(go.has_value());
    CHECK(go->get().absolute);
    CHECK(std::abs(go->get().posX - 30.f) < 1e-3f);
    CHECK(std::abs(go->get().posY - 40.f) < 1e-3f);
}
}  // namespace test_ui_motion_bind
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_ui_presentation)
#include "PrivateTestScope.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include "TestPropertyAccess.h"
#include "property_access/PropertyAccess.h"
#include "ui/PropertyView.h"
#include "ui/UIHost.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// ui_presentation.cpp
namespace test_ui_presentation {
using namespace eve::property_access;
using namespace eve::ui;
using eve::Value;

namespace {

PropertySchema playerSchema() {
    PropertySchema schema;
    schema.typeId = "game.player";

    PropertyDescriptor alive;
    alive.path         = "state.alive";
    alive.displayName  = "Alive";
    alive.category     = "State";
    alive.kind         = PropertyKind::Bool;
    alive.flags        = PropertyFlag::Runtime;
    alive.defaultValue = true;
    schema.properties.push_back(alive);

    PropertyDescriptor speed;
    speed.path            = "movement.speed";
    speed.displayName     = "Speed";
    speed.description     = "Maximum movement speed";
    speed.category        = "Movement";
    speed.kind            = PropertyKind::Number;
    speed.flags           = PropertyFlag::Runtime;
    speed.defaultValue    = 4.0;
    speed.numeric.minimum = 0.0;
    speed.numeric.maximum = 12.0;
    schema.properties.push_back(speed);

    PropertyDescriptor mode;
    mode.path         = "movement.mode";
    mode.displayName  = "Mode";
    mode.category     = "Movement";
    mode.kind         = PropertyKind::Enum;
    mode.flags        = PropertyFlag::Runtime;
    mode.defaultValue = "walk";
    mode.choices      = {"walk", "run", "fly"};
    schema.properties.push_back(mode);

    PropertyDescriptor debug;
    debug.path         = "debug.label";
    debug.displayName  = "Debug Label";
    debug.kind         = PropertyKind::ReadOnlyText;
    debug.flags        = PropertyFlag::Advanced | PropertyFlag::EditorOnly | PropertyFlag::ReadOnly;
    debug.defaultValue = "Player #1";
    schema.properties.push_back(debug);

    PropertyDescriptor tint;
    tint.path         = "visual.tint";
    tint.displayName  = "Tint";
    tint.category     = "Visual";
    tint.kind         = PropertyKind::Color;
    tint.flags        = PropertyFlag::Runtime;
    tint.defaultValue = Value(Value::Array{Value(1.0), Value(0.0), Value(0.0), Value(1.0)});
    schema.properties.push_back(tint);

    PropertyDescriptor size;
    size.path         = "visual.size";
    size.displayName  = "Size";
    size.category     = "Visual";
    size.kind         = PropertyKind::Vec2;
    size.flags        = PropertyFlag::Runtime;
    size.defaultValue = Value(Value::Array{Value(2.0), Value(4.0)});
    schema.properties.push_back(size);

    PropertyDescriptor tags;
    tags.path         = "state.tags";
    tags.displayName  = "Tags";
    tags.category     = "State";
    tags.kind         = PropertyKind::Array;
    tags.flags        = PropertyFlag::Runtime;
    tags.defaultValue = Value(Value::Array{Value(std::string("player"))});
    schema.properties.push_back(tags);
    return schema;
}

UINode* node(UIHost& host, const std::string& id) {
    auto found = host.findById(id);
    return found ? &found->get() : nullptr;
}

UIHost* createHost(const std::string& name) {
    auto host = UIHost::resolve(UIHost::createHost(name));
    return host ? &host->get() : nullptr;
}

double numericValueApprox(const Value& value) {
    if (const auto* number = value.getIf<double>()) return *number;
    if (const auto* integer = value.getIf<std::int64_t>()) return static_cast<double>(*integer);
    return 0.0;
}

}  // namespace

TEST_CASE("ui.presentation.dynamic_model_validates_and_notifies") {
    TestPropertyAccess          model(playerSchema());
    std::vector<PropertyChange> changes;
    auto subscription = model.subscribe([&changes](const PropertyChange& change) { changes.push_back(change); });

    CHECK(model.write("movement.speed", Value(8.0)).ok());
    REQUIRE_EQ(changes.size(), static_cast<std::size_t>(1));
    CHECK_EQ(changes.front().path, std::string("movement.speed"));
    CHECK_EQ(model.revision(), std::uint64_t(1));
    REQUIRE(model.read("movement.speed").has_value());
    CHECK_EQ(*model.read("movement.speed")->getIf<double>(), 8.0);

    CHECK(!model.write("movement.speed", Value(20.0)).ok());
    CHECK(!model.write("movement.mode", Value("swim")).ok());
    CHECK(!model.write("debug.label", Value("changed")).ok());
    CHECK_EQ(changes.size(), static_cast<std::size_t>(1));

    subscription.dispose();
    CHECK(model.write("state.alive", Value(false)).ok());
    CHECK_EQ(changes.size(), static_cast<std::size_t>(1));
}

TEST_CASE("ui.presentation.generated_view_binds_two_way") {
    TestPropertyAccess  model(playerSchema());
    PropertyViewOptions options;
    options.idPrefix       = "player/";
    options.title          = "Player";
    options.showEditorOnly = false;

    UIHost* host = createHost("presentation-test");
    REQUIRE(host != nullptr);
    host->setTree(buildPropertyView(model, options));

    UINode* alive = node(*host, "player/state_alive");
    UINode* speed = node(*host, "player/movement_speed");
    UINode* mode  = node(*host, "player/movement_mode");
    REQUIRE(alive != nullptr);
    REQUIRE(speed != nullptr);
    REQUIRE(mode != nullptr);
    CHECK(node(*host, "player/debug_label") == nullptr);
    CHECK_EQ(speed->tooltip, std::string("Maximum movement speed"));
    CHECK_EQ(static_cast<int>(speed->accessibilityRole), static_cast<int>(AccessibilityRole::Slider));
    CHECK_EQ(speed->accessibilityName, std::string("Speed"));
    CHECK_EQ(speed->accessibilityDescription, std::string("Maximum movement speed"));

    REQUIRE_GE(alive->handlerToggle, 1u);
    host->tree()->toggleHandlers[alive->handlerToggle - 1](false);
    REQUIRE(model.read("state.alive").has_value());
    CHECK(!*model.read("state.alive")->getIf<bool>());

    REQUIRE_GE(speed->handlerValue, 1u);
    host->tree()->valueHandlers[speed->handlerValue - 1](9.5f);
    REQUIRE(model.read("movement.speed").has_value());
    CHECK_EQ(*model.read("movement.speed")->getIf<double>(), 9.5);

    REQUIRE_GE(mode->handlerValue, 1u);
    host->tree()->valueHandlers[mode->handlerValue - 1](2.0f);
    REQUIRE(model.read("movement.mode").has_value());
    CHECK_EQ(*model.read("movement.mode")->getIf<std::string>(), std::string("fly"));

    CHECK(model.write("movement.speed", Value(3.0)).ok());
    syncPropertyView(*host, model, options);
    CHECK_EQ(node(*host, "player/movement_speed")->value, 3.0f);

    UINode* tintR = node(*host, "player/visual_tint_r");
    UINode* sizeX = node(*host, "player/visual_size_x");
    UINode* tag0  = node(*host, "player/state_tags_0");
    REQUIRE(tintR != nullptr);
    REQUIRE(sizeX != nullptr);
    REQUIRE(tag0 != nullptr);
    REQUIRE_GE(tintR->handlerText, 1u);
    host->tree()->textHandlers[tintR->handlerText - 1]("0.25");
    // Keep optional Values alive: getIf returns a pointer into the optional storage.
    const std::optional<Value> tintValue = model.read("visual.tint");
    REQUIRE(tintValue.has_value());
    const Value::Array* tint = tintValue->getIf<Value::Array>();
    REQUIRE(tint != nullptr);
    CHECK_EQ(numericValueApprox((*tint)[0]), 0.25);

    REQUIRE_GE(sizeX->handlerText, 1u);
    host->tree()->textHandlers[sizeX->handlerText - 1]("8");
    const std::optional<Value> sizeValue = model.read("visual.size");
    REQUIRE(sizeValue.has_value());
    const Value::Array* size = sizeValue->getIf<Value::Array>();
    REQUIRE(size != nullptr);
    CHECK_EQ(numericValueApprox((*size)[0]), 8.0);

    REQUIRE_GE(tag0->handlerText, 1u);
    host->tree()->textHandlers[tag0->handlerText - 1]("hero");
    const std::optional<Value> tagsValue = model.read("state.tags");
    REQUIRE(tagsValue.has_value());
    const Value::Array* tags = tagsValue->getIf<Value::Array>();
    REQUIRE(tags != nullptr);
    const std::string* tag0Text = (*tags)[0].getIf<std::string>();
    REQUIRE(tag0Text != nullptr);
    CHECK_EQ(*tag0Text, std::string("hero"));
}

TEST_CASE("ui.presentation.component_tracks_model_revision") {
    TestPropertyAccess model(playerSchema());
    PropertyComponent  component(&model);
    component.build();
    component.attach(UIHost::createHost("presentation-component"));
    component.rebuild();
    CHECK(!component.isDirty());

    CHECK(model.write("state.alive", Value(false)).ok());
    CHECK(component.isDirty());
    CHECK(component.updateIfDirty());
    CHECK(!component.isDirty());
}
}  // namespace test_ui_presentation
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_vehicle_physics)
#include "PrivateTestScope.h"
#include <cmath>
#include <memory>
#include <string>
#include "physics/Body.h"
#include "physics/Body3D.h"
#include "physics/Physics.h"
#include "physics/World.h"
#include "physics/World3D.h"
#include "vehicle/Vehicle.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// vehicle_physics.cpp
namespace test_vehicle_physics {
using namespace eve::vehicle;

TEST_CASE("vehicle.physics2d.wheelDrivesAndSteers") {
    eve::physics::Physics ph;
    eve::physics::World*  world = ph.newWorld(0.f, 0.f, true);
    REQUIRE(world != nullptr);

    Vehicle mod;
    CHECK_EQ(mod.registerVehiclesFromJson("[{\"id\":\"car\",\"mobility\":\"wheel\",\"maxSpeed\":120,"
                                          "\"accel\":100,\"turnRate\":120,\"radius\":18}]"),
             1);
    VehicleEntity* v = mod.newVehicle("car", 0.f, 0.f, 0.f);
    REQUIRE(v != nullptr);
    CHECK(mod.attachPhysics2D(v, world));
    CHECK(mod.hasPhysics(v));
    CHECK_EQ(mod.getPhysicsSpace(v), std::string("2d"));

    mod.setInput(v, 1.f, 0.f, 0.f);
    for (int i = 0; i < 60; ++i) {
        mod.update(1.f / 60.f);
        world->update(1.f / 60.f);
    }
    CHECK_GT(mod.getX(v), 30.f);
    CHECK_GT(mod.getSpeed(v), 60.f);

    mod.setInput(v, 0.7f, 1.f, 0.f);
    const float h0 = mod.getHeading(v);
    for (int i = 0; i < 60; ++i) {
        mod.update(1.f / 60.f);
        world->update(1.f / 60.f);
    }
    CHECK_GT(mod.getHeading(v), h0 + 10.f);
}

TEST_CASE("vehicle.physics2d.trackRotatesInPlace") {
    eve::physics::Physics ph;
    eve::physics::World*  world = ph.newWorld(0.f, 0.f, true);
    REQUIRE(world != nullptr);

    Vehicle mod;
    CHECK_EQ(mod.registerVehiclesFromJson("[{\"id\":\"tank\",\"mobility\":\"track\",\"maxSpeed\":100,"
                                          "\"accel\":80,\"turnRate\":150,\"radius\":20}]"),
             1);
    VehicleEntity* v = mod.newVehicle("tank", 100.f, 100.f, 0.f);
    REQUIRE(v != nullptr);
    CHECK(mod.attachPhysics2D(v, world));

    mod.setInput(v, 0.f, 1.f, 0.f);
    for (int i = 0; i < 60; ++i) {
        mod.update(1.f / 60.f);
        world->update(1.f / 60.f);
    }
    CHECK_GT(std::fabs(mod.getHeading(v)), 40.f);
    CHECK_LT(std::fabs(mod.getX(v) - 100.f), 8.f);
    CHECK_LT(std::fabs(mod.getY(v) - 100.f), 8.f);
}

TEST_CASE("vehicle.physics3d.suspensionDrives") {
    eve::physics::Physics  ph;
    eve::physics::World3D* world3 = ph.newWorld3D(0.f, -9.8f, 0.f, true);
    REQUIRE(world3 != nullptr);
    eve::physics::Body3D* ground = world3->newBody("static", 0.f, -0.5f, 0.f);
    REQUIRE(ground != nullptr);
    ground->newBoxShape(60.f, 0.5f, 60.f, 1.f, 0.8f, 0.f);

    const char* suv = R"JSON(
[{"id":"suv","mobility":"suspension","maxSpeed":16,"turnRate":60,"radius":1.0,
  "suspension":{"maxTravel":0.25,"driveForce":3000,"lateralGrip":10,
    "wheels":[
      {"x":-0.9,"y":-0.35,"z":-1.2,"radius":0.3,"rest":0.35,"stiffness":30000,"damping":3000,"drive":true,"steer":true},
      {"x":0.9,"y":-0.35,"z":-1.2,"radius":0.3,"rest":0.35,"stiffness":30000,"damping":3000,"drive":true,"steer":true},
      {"x":-0.9,"y":-0.35,"z":1.2,"radius":0.3,"rest":0.35,"stiffness":30000,"damping":3000,"drive":false,"steer":false},
      {"x":0.9,"y":-0.35,"z":1.2,"radius":0.3,"rest":0.35,"stiffness":30000,"damping":3000,"drive":false,"steer":false}]}}]
)JSON";
    Vehicle     mod;
    CHECK_EQ(mod.registerVehiclesFromJson(suv), 1);
    VehicleEntity* v = mod.newVehicle("suv", 0.f, 0.f, 0.f);
    REQUIRE(v != nullptr);
    CHECK(mod.attachPhysics3D(v, world3, 2.0f));
    CHECK_EQ(mod.getPhysicsSpace(v), std::string("3d"));

    mod.setInput(v, 0.8f, 0.f, 0.f);
    for (int i = 0; i < 240; ++i) {
        mod.update(1.f / 60.f);
        world3->update(1.f / 60.f);
    }
    CHECK_GT(mod.getY(v), 5.f);
    CHECK_GT(mod.getHeight(v), 0.15f);
    CHECK_LT(mod.getHeight(v), 2.5f);
}

TEST_CASE("vehicle.physicsLink.detachInvalidatesBodyAndProjection") {
    eve::physics::Physics ph;
    auto*                 world = ph.newWorld(0.f, 0.f, true);
    REQUIRE(world != nullptr);

    Vehicle mod;
    REQUIRE_EQ(mod.registerVehiclesFromJson("[{\"id\":\"car\",\"mobility\":\"wheel\",\"maxSpeed\":20,\"radius\":2}]"),
               1);
    auto* vehicle = mod.newVehicle("car", 0.f, 0.f);
    REQUIRE(vehicle != nullptr);
    REQUIRE(mod.attachPhysics2D(vehicle, world));
    auto* body = world->findBody(eve::physics::PhysicsBodyHandle(1u, 1u));
    REQUIRE(body != nullptr);

    CHECK(mod.detachPhysics(vehicle));
    CHECK(!mod.hasPhysics(vehicle));
    CHECK_EQ(mod.getPhysicsSpace(vehicle), std::string{});
    CHECK(!body->isValid());

    delete body;
    delete world;
}

TEST_CASE("vehicle.physicsLink.externalBodyDestructionBecomesStale") {
    eve::physics::Physics ph;
    auto*                 world = ph.newWorld(0.f, 0.f, true);
    REQUIRE(world != nullptr);

    Vehicle mod;
    REQUIRE_EQ(mod.registerVehiclesFromJson("[{\"id\":\"car\",\"mobility\":\"wheel\",\"maxSpeed\":20,\"radius\":2}]"),
               1);
    auto* vehicle = mod.newVehicle("car", 0.f, 0.f);
    REQUIRE(vehicle != nullptr);
    REQUIRE(mod.attachPhysics2D(vehicle, world));
    auto* body = world->findBody(eve::physics::PhysicsBodyHandle(1u, 1u));
    REQUIRE(body != nullptr);

    body->destroy();
    CHECK(!mod.hasPhysics(vehicle));
    CHECK_EQ(mod.getPhysicsSpace(vehicle), std::string{});
    mod.update(1.f / 60.f);
    CHECK(mod.detachPhysics(vehicle));
    CHECK_EQ(mod.getPhysicsSpace(vehicle), std::string{});

    delete body;
    delete world;
}

TEST_CASE("vehicle.physicsLink.worldFirstDestructionIsSafe") {
    eve::physics::Physics ph;
    auto*                 world = ph.newWorld(0.f, 0.f, true);
    REQUIRE(world != nullptr);

    Vehicle mod;
    REQUIRE_EQ(mod.registerVehiclesFromJson("[{\"id\":\"car\",\"mobility\":\"wheel\",\"maxSpeed\":20,\"radius\":2}]"),
               1);
    auto* vehicle = mod.newVehicle("car", 0.f, 0.f);
    REQUIRE(vehicle != nullptr);
    REQUIRE(mod.attachPhysics2D(vehicle, world));
    auto* body = world->findBody(eve::physics::PhysicsBodyHandle(1u, 1u));
    REQUIRE(body != nullptr);

    delete world;
    CHECK(!mod.hasPhysics(vehicle));
    CHECK_EQ(mod.getPhysicsSpace(vehicle), std::string{});
    mod.update(1.f / 60.f);
    CHECK(mod.detachPhysics(vehicle));
    CHECK(!body->isValid());
    delete body;
}

TEST_CASE("vehicle.physicsLink.vehicleFirstDestructionReleasesBody") {
    eve::physics::Physics ph;
    auto*                 world = ph.newWorld(0.f, 0.f, true);
    REQUIRE(world != nullptr);

    auto mod = std::make_unique<Vehicle>();
    REQUIRE_EQ(mod->registerVehiclesFromJson("[{\"id\":\"car\",\"mobility\":\"wheel\",\"maxSpeed\":20,\"radius\":2}]"),
               1);
    auto* vehicle = mod->newVehicle("car", 0.f, 0.f);
    REQUIRE(vehicle != nullptr);
    REQUIRE(mod->attachPhysics2D(vehicle, world));
    auto* body = world->findBody(eve::physics::PhysicsBodyHandle(1u, 1u));
    REQUIRE(body != nullptr);

    mod.reset();
    CHECK(!body->isValid());
    CHECK(world->findBody(eve::physics::PhysicsBodyHandle(1u, 1u)) == nullptr);

    delete body;
    delete world;
}

TEST_CASE("vehicle.physicsLink.worldFirstDestructionIsSafe3D") {
    eve::physics::Physics ph;
    auto*                 world = ph.newWorld3D(0.f, -9.8f, 0.f, true);
    REQUIRE(world != nullptr);

    Vehicle mod;
    REQUIRE_EQ(mod.registerVehiclesFromJson("[{\"id\":\"car\",\"mobility\":\"wheel\",\"maxSpeed\":20,\"radius\":2}]"),
               1);
    auto* vehicle = mod.newVehicle("car", 0.f, 0.f);
    REQUIRE(vehicle != nullptr);
    REQUIRE(mod.attachPhysics3D(vehicle, world, 2.f));
    auto* body = world->findBody(eve::physics::PhysicsBodyHandle(1u, 1u));
    REQUIRE(body != nullptr);

    delete world;
    CHECK(!mod.hasPhysics(vehicle));
    CHECK_EQ(mod.getPhysicsSpace(vehicle), std::string{});
    CHECK_EQ(mod.getHeight(vehicle), 0.f);
    mod.update(1.f / 60.f);
    CHECK(mod.detachPhysics(vehicle));
    CHECK(!body->isValid());
    delete body;
}

TEST_CASE("vehicle.physicsLink.vehicleFirstDestructionReleasesBody3D") {
    eve::physics::Physics ph;
    auto*                 world = ph.newWorld3D(0.f, -9.8f, 0.f, true);
    REQUIRE(world != nullptr);

    auto mod = std::make_unique<Vehicle>();
    REQUIRE_EQ(mod->registerVehiclesFromJson("[{\"id\":\"car\",\"mobility\":\"wheel\",\"maxSpeed\":20,\"radius\":2}]"),
               1);
    auto* vehicle = mod->newVehicle("car", 0.f, 0.f);
    REQUIRE(vehicle != nullptr);
    REQUIRE(mod->attachPhysics3D(vehicle, world, 2.f));
    auto* body = world->findBody(eve::physics::PhysicsBodyHandle(1u, 1u));
    REQUIRE(body != nullptr);

    mod.reset();
    CHECK(!body->isValid());
    CHECK(world->findBody(eve::physics::PhysicsBodyHandle(1u, 1u)) == nullptr);

    delete body;
    delete world;
}
}  // namespace test_vehicle_physics
#endif
