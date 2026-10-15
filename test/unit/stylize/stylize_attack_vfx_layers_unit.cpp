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

TEST_CASE("stylize.effects.registryAndSchema") {
    auto* mod = Stylize::create();
    REQUIRE(mod != nullptr);

    for (const char* id : kMeshEffects) {
        CHECK(mod->hasStyle(id));
        CHECK(mod->hasMeshStyle(id));
        CHECK(!mod->supports(id, "post"));
        CHECK(mod->supports(id, "mesh"));
    }
    for (const char* id : kPostEffects) {
        CHECK(mod->hasStyle(id));
        CHECK(!mod->hasMeshStyle(id));
        CHECK(mod->supports(id, "post"));
        CHECK(!mod->supports(id, "mesh"));
    }
    CHECK(!mod->hasStyle("bloom"));

    // Params are clamped to [min,max] and expose defaults.
    std::unique_ptr<StyleInstance> dissolve(mod->newInstance("dissolve"));
    REQUIRE(dissolve.get() != nullptr);
    CHECK(dissolve->hasParam("amount"));
    CHECK(dissolve->hasParam("edgeColorR"));
    CHECK(!dissolve->hasParam("nope"));
    CHECK_EQ(dissolve->getParamName(0), std::string("amount"));
    CHECK_EQ(dissolve->getFloat("amount"), dissolve->getParamDefault("amount"));
    dissolve->setFloat("amount", 5.f);
    CHECK_EQ(dissolve->getFloat("amount"), dissolve->getParamMax("amount"));
    dissolve->setFloat("amount", -3.f);
    CHECK_EQ(dissolve->getFloat("amount"), dissolve->getParamMin("amount"));
    CHECK(dissolve->isOverridden("amount"));
    dissolve->resetAll();
    CHECK(!dissolve->isOverridden("amount"));

    std::unique_ptr<StyleInstance> vignette(mod->newInstance("vignette"));
    REQUIRE(vignette.get() != nullptr);
    CHECK(vignette->hasParam("strength"));
    CHECK_EQ(vignette->getStage(), std::string("afterTonemap"));
    CHECK(vignette->requiresInput("color"));
    CHECK(!vignette->requiresInput("depth"));

    // Unknown styles still throw.
    bool threw = false;
    try {
        mod->newInstance("oil");
    } catch (const eve::Exception&) {
        threw = true;
    }
    CHECK(threw);
}


// stylize_motion_echo_effect.cpp
using namespace eve::stylize;

TEST_CASE("stylize.projectileTrail.resamplesHighSpeedMovement") {
    ProjectileTrailConfig config;
    config.sampleIntervalSeconds = 0.1f;
    config.sampleLifetimeSeconds = 1.f;
    config.minimumDistance       = 0.f;
    ProjectileTrailEffect trail(config);
    auto                  report = trail.update(0.f, {0.f, 0.f, 0.f});
    REQUIRE_EQ(report.captured, 1u);
    report = trail.update(0.35f, {3.5f, 0.f, 0.f});
    REQUIRE_EQ(report.captured, 3u);
    REQUIRE_EQ(trail.size(), 4u);
    const auto mesh = trail.build({0.f, 0.f, -1.f});
    REQUIRE_EQ(mesh.vertices.size(), 8u);
    REQUIRE_EQ(mesh.indices.size(), 18u);
}

TEST_CASE("stylize.projectileTrail.rejectsStationaryDuplicateSamples") {
    ProjectileTrailConfig config;
    config.sampleIntervalSeconds = 0.05f;
    config.minimumDistance       = 0.1f;
    ProjectileTrailEffect trail(config);
    (void)trail.update(0.f, {1.f, 2.f, 3.f});
    const auto report = trail.update(0.2f, {1.f, 2.f, 3.f});
    REQUIRE_EQ(report.captured, 0u);
    REQUIRE_EQ(report.distanceRejected, 4u);
    REQUIRE_EQ(trail.size(), 1u);
}

TEST_CASE("stylize.afterimage.capturesExpiresAndPreservesStableIds") {
    AfterimageEffectConfig config;
    config.sampleIntervalSeconds = 0.1f;
    config.lifetimeSeconds       = 0.3f;
    config.maximumImages         = 4;
    AfterimageEffect afterimage(config);
    const glm::mat4  firstModel = glm::translate(glm::mat4(1.f), {1.f, 0.f, 0.f});
    auto             report     = afterimage.update(0.25f, firstModel);
    REQUIRE_EQ(report.captured, 2u);
    auto images = afterimage.snapshot();
    REQUIRE_EQ(images.size(), 2u);
    REQUIRE_EQ(images[0].stableId, 1u);
    REQUIRE_EQ(images[1].stableId, 2u);

    const glm::mat4 secondModel = glm::translate(glm::mat4(1.f), {2.f, 0.f, 0.f});
    report                      = afterimage.update(0.2f, secondModel);
    REQUIRE_EQ(report.expired, 1u);
    images = afterimage.snapshot();
    REQUIRE_EQ(images.size(), 3u);
    REQUIRE_EQ(images[0].stableId, 2u);
    REQUIRE_EQ(images[1].stableId, 3u);
    REQUIRE_EQ(images[2].stableId, 4u);
}

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


// The Vulkan YSA path must run the same vertex contract as the WebGPU path
// (which uses the backend's standard mesh vertex shader): GPU skinning via the
// skin palette at set 0 binding 21, and the tint * vertexColor multiply.


// Vertex color must reach the Vulkan material color: the WebGPU YSA path runs
// the standard mesh vertex shader, which forwards ubo.tint * inColor.
