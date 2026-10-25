// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "stylize_anime.cpp",
    "stylize_area_indicator_effect.cpp",
    "stylize_attack_vfx.cpp",
    "stylize_attack_vfx_layers.cpp",
    "stylize_beam_effect.cpp",
    "stylize_mesh_particles.cpp",
    "stylize_mesh_vfx_benchmark.cpp",
    "stylize.cpp"
  ]
}
*/
// clang-format on

#include "zeroerr/assert.h"

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
namespace test_stylize_anime_unit {
TEST_CASE("stylize.anime.materialContract") {
    using namespace eve::stylize;
    REQUIRE(styleSupports("anime", "mesh"));
    REQUIRE(!styleSupports("anime", "post"));
    REQUIRE(!styleSupports("anime", "cpu"));
    eve::graphics::Shader shader;
    bindMeshUniforms(&shader, "anime");
    REQUIRE_EQ(shader.usedFloats(), styleParamCount("anime"));
    REQUIRE(shader.usedFloats() <= int(eve::graphics::Shader::kMaxFloats));
    for (int i = 0; i < styleParamCount("anime"); ++i) {
        const auto *param = styleParameterAt("anime", i);
        REQUIRE(param != nullptr);
        float value = 0.f;
        REQUIRE_EQ(shader.getFromVar(param->id, &value, sizeof(value)), int(sizeof(value)));
        REQUIRE_EQ(value, param->defaultValue);
        REQUIRE(value >= param->minValue);
        REQUIRE(value <= param->maxValue);
        REQUIRE(std::isfinite(value));
    }
    REQUIRE_EQ(findStyleParameter("anime", "skinDetail")->defaultValue, 1.f);
    REQUIRE(findStyleParameter("anime", "posterize") == nullptr);
}
}  // namespace test_stylize_anime_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_stylize_area_indicator_effect)
#include "PrivateTestScope.h"
#include <glm/geometric.hpp>
#include "stylize/AreaIndicatorEffect.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// stylize_area_indicator_effect.cpp
namespace test_stylize_area_indicator_effect {
using namespace eve::stylize;

TEST_CASE("stylize.areaIndicator.circleBuildsClosedFan") {
    AreaIndicatorConfig config;
    config.shape      = AreaIndicatorShape::Circle;
    config.radius     = 2.f;
    config.segments   = 16;
    const auto result = buildAreaIndicator(config);
    REQUIRE_EQ(static_cast<int>(result.status), static_cast<int>(AreaIndicatorBuildStatus::Built));
    REQUIRE_EQ(result.mesh.vertices.size(), 18u);
    REQUIRE_EQ(result.mesh.indices.size(), 48u);
    REQUIRE(glm::length(result.mesh.vertices[1].position - result.mesh.vertices.back().position) < 1e-4f);
}

TEST_CASE("stylize.areaIndicator.ringBuildsInnerOuterStrip") {
    AreaIndicatorConfig config;
    config.shape       = AreaIndicatorShape::Ring;
    config.radius      = 3.f;
    config.innerRadius = 2.f;
    config.segments    = 12;
    const auto result  = buildAreaIndicator(config);
    REQUIRE_EQ(static_cast<int>(result.status), static_cast<int>(AreaIndicatorBuildStatus::Built));
    REQUIRE_EQ(result.mesh.vertices.size(), 26u);
    REQUIRE_EQ(result.mesh.indices.size(), 72u);
    REQUIRE(glm::length(result.mesh.vertices[0].position) > 1.99f);
    REQUIRE(glm::length(result.mesh.vertices[1].position) > 2.99f);
}

TEST_CASE("stylize.areaIndicator.sectorAndRectangleRespectBounds") {
    AreaIndicatorConfig sector;
    sector.shape              = AreaIndicatorShape::Sector;
    sector.radius             = 4.f;
    sector.sectorAngleRadians = 1.57079632679f;
    sector.segments           = 8;
    const auto cone           = buildAreaIndicator(sector);
    REQUIRE_EQ(cone.mesh.indices.size(), 24u);

    AreaIndicatorConfig rectangle;
    rectangle.shape  = AreaIndicatorShape::Rectangle;
    rectangle.width  = 2.f;
    rectangle.length = 5.f;
    const auto box   = buildAreaIndicator(rectangle);
    REQUIRE_EQ(box.mesh.vertices.size(), 4u);
    REQUIRE_EQ(box.mesh.indices.size(), 6u);
    REQUIRE(box.mesh.vertices[0].position.x == -1.f);
    REQUIRE(box.mesh.vertices[3].position.z == 5.f);
}

TEST_CASE("stylize.areaIndicator.rejectsInvalidDimensions") {
    AreaIndicatorConfig ring;
    ring.shape        = AreaIndicatorShape::Ring;
    ring.radius       = 1.f;
    ring.innerRadius  = 1.f;
    const auto result = buildAreaIndicator(ring);
    REQUIRE_EQ(static_cast<int>(result.status), static_cast<int>(AreaIndicatorBuildStatus::InvalidConfig));
    REQUIRE(result.mesh.vertices.empty());
}
}  // namespace test_stylize_area_indicator_effect
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_stylize_attack_vfx)
#include "PrivateTestScope.h"
#include <string_view>
#include "stylize/AttackVfxRecipe.h"
#include "stylize/AttackVfxRuntime.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// stylize_attack_vfx.cpp
namespace test_stylize_attack_vfx {
using namespace eve::stylize;

namespace {

eve::LogicalId id(std::string_view value) {
    auto parsed = eve::LogicalId::parse(value);
    REQUIRE(parsed.has_value());
    return std::move(*parsed);
}

const char *kWaterWhipRecipe = R"({
  "schema":"eve.stylize.attack-vfx",
  "schemaVersion":1,
  "id":"attackvfx:water_whip",
  "skinId":"skin:water",
  "skin":{
    "id":"skin:water",
    "palette":{"primary":[0.2,0.6,1.0],"secondary":[0.5,0.9,1.0],"emissive":[0.4,0.8,1.0]},
    "styleHints":{"meshStyle":"slash"},
    "shakeProfile":{"posAmp":0.05,"duration":0.12},
    "statusOverlayUri":"meshvfx://status/wet"
  },
  "budget":{"lod":0,"maxParticles":256,"allowDistortion":true},
  "phases":[
    {
      "kind":"release",
      "startOffsetSeconds":0,
      "durationSeconds":0.4,
      "layers":[
        {"role":"meshVfx","uri":"meshvfx://water/whip","spatial":{"attachment":"followTarget","anchor":"source","bone":"weapon.tip"}},
        {"role":"particles","uri":"particles://water/droplets"}
      ]
    },
    {
      "kind":"impact",
      "startCue":"impact",
      "durationSeconds":0.35,
      "layers":[
        {"role":"particles","uri":"particles://water/impact"},
        {"role":"camera","floatParams":{"posAmp":0.08,"duration":0.1}},
        {"role":"distortion","floatParams":{"strength":0.4}}
      ]
    },
    {
      "kind":"status",
      "startCue":"impact",
      "durationSeconds":1.0,
      "layers":[
        {"role":"meshVfx","uri":"meshvfx://status/wet","spatial":{"attachment":"followTarget","anchor":"target"}}
      ]
    }
  ]
})";

}  // namespace

TEST_CASE("stylize.attack_vfx parses layered recipe and inline skin") {
    auto parsed = AttackVfxRecipe::fromJson(kWaterWhipRecipe);
    REQUIRE(parsed.ok());
    auto recipe = std::move(parsed).takeValue();
    REQUIRE_EQ(recipe.id.format(), "attackvfx:water_whip");
    REQUIRE_EQ(recipe.phases.size(), 3u);
    REQUIRE(recipe.skin.has_value());
    REQUIRE_EQ(recipe.skin->id.format(), "skin:water");
    REQUIRE_EQ(recipe.phases[0].layers.size(), 2u);
    REQUIRE(recipe.phases[1].layers[1].uri.empty());  // camera may omit uri
    REQUIRE_EQ(recipe.phases[1].startCue, "impact");
}

TEST_CASE("stylize.attack_vfx rejects unknown fields and invalid timing") {
    auto unknown = AttackVfxRecipe::fromJson(
        R"({"schema":"eve.stylize.attack-vfx","schemaVersion":1,"id":"attackvfx:x","phases":[{"kind":"release","typo":1}]})");
    REQUIRE(!unknown.ok());

    auto badDuration = AttackVfxRecipe::fromJson(
        R"({"schema":"eve.stylize.attack-vfx","schemaVersion":1,"id":"attackvfx:x","phases":[{"kind":"release","durationSeconds":-1,"layers":[{"role":"particles","uri":"p://a"}]}]})");
    REQUIRE(!badDuration.ok());

    auto missingUri = AttackVfxRecipe::fromJson(
        R"({"schema":"eve.stylize.attack-vfx","schemaVersion":1,"id":"attackvfx:x","phases":[{"kind":"release","layers":[{"role":"particles"}]}]})");
    REQUIRE(!missingUri.ok());
}

TEST_CASE("stylize.attack_vfx round-trips canonical JSON") {
    auto parsed = AttackVfxRecipe::fromJson(kWaterWhipRecipe);
    REQUIRE(parsed.ok());
    auto encoded = parsed.value().toJson();
    REQUIRE(encoded.ok());
    auto again = AttackVfxRecipe::fromJson(encoded.value());
    REQUIRE(again.ok());
    REQUIRE_EQ(again.value().id.format(), "attackvfx:water_whip");
    REQUIRE_EQ(again.value().phases.size(), 3u);
    REQUIRE_EQ(again.value().phases[1].layers.size(), 3u);
}

TEST_CASE("stylize.attack_vfx recipe slot reload is transactional") {
    auto parsed = AttackVfxRecipe::fromJson(kWaterWhipRecipe);
    REQUIRE(parsed.ok());
    AttackVfxRecipeSlot slot(std::move(parsed).takeValue());
    auto                failed = slot.reload(R"({"schemaVersion":99})");
    REQUIRE(!failed.ok());
    REQUIRE_EQ(slot.revision(), 1u);
    REQUIRE_EQ(slot.recipe().id.format(), "attackvfx:water_whip");

    auto ok = slot.reload(R"({
      "schema":"eve.stylize.attack-vfx","schemaVersion":1,"id":"attackvfx:fire_burst",
      "phases":[{"kind":"release","layers":[{"role":"particles","uri":"particles://fire/burst"}]}]
    })");
    REQUIRE(ok.ok());
    REQUIRE_EQ(ok.value(), 2u);
    REQUIRE_EQ(slot.recipe().id.format(), "attackvfx:fire_burst");
}

TEST_CASE("stylize.attack_vfx runtime plays release then impact on cue") {
    auto parsed = AttackVfxRecipe::fromJson(kWaterWhipRecipe);
    REQUIRE(parsed.ok());
    AttackVfxRuntime runtime;
    REQUIRE(runtime.configurePool(4).ok());
    REQUIRE(runtime.registerRecipe(parsed.value()).ok());

    AttackVfxRequest request;
    request.sourceId = 1;
    request.targetId = 2;
    auto played      = runtime.play(id("attackvfx:water_whip"), request);
    REQUIRE(played.ok());
    auto handle = std::move(played).takeValue();
    REQUIRE_EQ(runtime.activeCount(), 1u);

    auto snap = runtime.inspect(handle);
    REQUIRE(snap.has_value());
    REQUIRE(snap->activeSkinId.has_value());
    REQUIRE_EQ(snap->activeSkinId->format(), "skin:water");
    REQUIRE(snap->phases[0].active);   // release
    REQUIRE(!snap->phases[1].active);  // impact waits for cue
    REQUIRE(!snap->phases[2].active);  // status waits for cue

    auto signaled = runtime.signal(handle, "impact");
    REQUIRE(signaled.ok());
    bool sawImpactEnter = false;
    bool sawStatusEnter = false;
    bool sawCueConsumed = false;
    for (const auto &event : signaled.value().events) {
        if (event.kind == AttackVfxFrameEvent::Kind::PhaseEnter && event.phase == AttackVfxPhaseKind::Impact)
            sawImpactEnter = true;
        if (event.kind == AttackVfxFrameEvent::Kind::PhaseEnter && event.phase == AttackVfxPhaseKind::Status)
            sawStatusEnter = true;
        if (event.kind == AttackVfxFrameEvent::Kind::CueConsumed) sawCueConsumed = true;
    }
    REQUIRE(sawImpactEnter);
    REQUIRE(sawStatusEnter);
    REQUIRE(sawCueConsumed);

    auto advanced = runtime.advance(0.2);
    REQUIRE(advanced.ok());
    // release duration 0.4 may still be active; impact/status entered on cue.
    snap = runtime.inspect(handle);
    REQUIRE(snap.has_value());
    REQUIRE(snap->phases[1].active);
    REQUIRE(snap->phases[2].active);

    advanced = runtime.advance(0.3);
    REQUIRE(advanced.ok());
    snap = runtime.inspect(handle);
    REQUIRE(snap.has_value());
    REQUIRE(snap->phases[0].completed);  // release 0.5s total >= 0.4
    REQUIRE(snap->phases[1].completed);  // impact 0.5s total >= 0.35
    REQUIRE(snap->phases[2].active);     // status duration 1.0

    advanced = runtime.advance(1.0);
    REQUIRE(advanced.ok());
    REQUIRE_EQ(runtime.activeCount(), 0u);
    bool sawStopped = false;
    for (const auto &event : advanced.value().events) {
        if (event.kind == AttackVfxFrameEvent::Kind::InstanceStopped) sawStopped = true;
    }
    REQUIRE(sawStopped);
}

TEST_CASE("stylize.attack_vfx runtime skin override and cancel") {
    auto recipe = AttackVfxRecipe::fromJson(R"({
      "schema":"eve.stylize.attack-vfx","schemaVersion":1,"id":"attackvfx:sweep",
      "phases":[{"kind":"release","durationSeconds":2.0,"layers":[{"role":"trail","uri":"trail://blade"}]}]
    })");
    REQUIRE(recipe.ok());
    auto fire = AttackVfxSkin::fromJson(R"({
      "schema":"eve.stylize.attack-vfx-skin","schemaVersion":1,"id":"skin:fire",
      "palette":{"primary":[1,0.3,0.1],"secondary":[1,0.6,0.2],"emissive":[1,0.8,0.2]}
    })");
    REQUIRE(fire.ok());

    AttackVfxRuntime runtime;
    REQUIRE(runtime.registerRecipe(recipe.value()).ok());
    REQUIRE(runtime.registerSkin(fire.value()).ok());

    AttackVfxRequest request;
    request.skinOverride = id("skin:fire");
    auto played          = runtime.play(id("attackvfx:sweep"), request);
    REQUIRE(played.ok());
    auto handle = std::move(played).takeValue();
    auto snap   = runtime.inspect(handle);
    REQUIRE(snap.has_value());
    REQUIRE_EQ(snap->activeSkinId->format(), "skin:fire");

    auto cancelled = runtime.signal(handle, "cancel");
    REQUIRE(cancelled.ok());
    REQUIRE_EQ(runtime.activeCount(), 0u);
    bool sawStopped = false;
    for (const auto &event : cancelled.value().events) {
        if (event.kind == AttackVfxFrameEvent::Kind::InstanceStopped) sawStopped = true;
    }
    REQUIRE(sawStopped);
}

TEST_CASE("stylize.attack_vfx runtime rejects stale handles and missing recipes") {
    AttackVfxRuntime runtime;
    auto             missing = runtime.play(id("attackvfx:missing"), {});
    REQUIRE(!missing.ok());

    auto recipe = AttackVfxRecipe::fromJson(R"({
      "schema":"eve.stylize.attack-vfx","schemaVersion":1,"id":"attackvfx:one_shot",
      "phases":[{"kind":"release","durationSeconds":0.1,"layers":[{"role":"audio","uri":"audio://whoosh"}]}]
    })");
    REQUIRE(recipe.ok());
    REQUIRE(runtime.registerRecipe(recipe.value()).ok());
    auto played = runtime.play(id("attackvfx:one_shot"), {});
    REQUIRE(played.ok());
    auto handle = played.value();
    REQUIRE(runtime.stop(handle, AttackVfxStopMode::ClearImmediately).ok());
    auto stale = runtime.signal(handle, "impact");
    REQUIRE(!stale.ok());
}

TEST_CASE("stylize.attack_vfx pool resize bumps generation") {
    AttackVfxRuntime runtime;
    REQUIRE(runtime.configurePool(2).ok());
    auto recipe = AttackVfxRecipe::fromJson(R"({
      "schema":"eve.stylize.attack-vfx","schemaVersion":1,"id":"attackvfx:gen",
      "phases":[{"kind":"release","durationSeconds":0.05,"layers":[{"role":"trail","uri":"trail://x"}]}]
    })");
    REQUIRE(recipe.ok());
    REQUIRE(runtime.registerRecipe(recipe.value()).ok());
    auto played = runtime.play(id("attackvfx:gen"), {});
    REQUIRE(played.ok());
    auto handle = std::move(played).takeValue();
    REQUIRE(runtime.stop(handle, AttackVfxStopMode::ClearImmediately).ok());
    REQUIRE(runtime.configurePool(4).ok());
    auto again = runtime.play(id("attackvfx:gen"), {});
    REQUIRE(again.ok());
    CHECK(again.value().generation != handle.generation);
    CHECK(!runtime.inspect(handle).has_value());
    REQUIRE(runtime.stop(again.value(), AttackVfxStopMode::ClearImmediately).ok());
}

TEST_CASE("stylize.attack_vfx rejects recipe replace while live") {
    AttackVfxRuntime runtime;
    auto             recipe = AttackVfxRecipe::fromJson(R"({
      "schema":"eve.stylize.attack-vfx","schemaVersion":1,"id":"attackvfx:live",
      "phases":[{"kind":"release","durationSeconds":2.0,"layers":[{"role":"trail","uri":"trail://x"}]}]
    })");
    REQUIRE(recipe.ok());
    REQUIRE(runtime.registerRecipe(recipe.value()).ok());
    auto played = runtime.play(id("attackvfx:live"), {});
    REQUIRE(played.ok());
    auto replaced = runtime.registerRecipe(recipe.value());
    REQUIRE(!replaced.ok());
    REQUIRE(runtime.stop(played.value(), AttackVfxStopMode::ClearImmediately).ok());
}

TEST_CASE("stylize.attack_vfx delayed phase uses partial dt") {
    AttackVfxRuntime runtime;
    auto             recipe = AttackVfxRecipe::fromJson(R"({
      "schema":"eve.stylize.attack-vfx","schemaVersion":1,"id":"attackvfx:delay",
      "phases":[{"kind":"release","startOffsetSeconds":0.08,"durationSeconds":0.1,
                 "layers":[{"role":"trail","uri":"trail://x"}]}]
    })");
    REQUIRE(recipe.ok());
    REQUIRE(runtime.registerRecipe(recipe.value()).ok());
    auto played = runtime.play(id("attackvfx:delay"), {});
    REQUIRE(played.ok());
    auto handle = std::move(played).takeValue();
    REQUIRE(runtime.advance(0.1).ok());
    auto snap = runtime.inspect(handle);
    REQUIRE(snap.has_value());
    REQUIRE(snap->phases[0].active);
    // Entered 0.02s into the phase; a full 0.1s credit would have already completed it.
    CHECK(snap->phases[0].localTime + 1e-9 < 0.1);
    CHECK(snap->phases[0].localTime + 1e-9 >= 0.02);
    REQUIRE(runtime.stop(handle, AttackVfxStopMode::ClearImmediately).ok());
}

TEST_CASE("stylize.attack_vfx budget rejects oversized maxParticles") {
    auto parsed = AttackVfxRecipe::fromJson(R"({
      "schema":"eve.stylize.attack-vfx","schemaVersion":1,"id":"attackvfx:budget",
      "budget":{"maxParticles":5000000000},
      "phases":[{"kind":"release","layers":[{"role":"particles","uri":"p://a"}]}]
    })");
    REQUIRE(!parsed.ok());
}
}  // namespace test_stylize_attack_vfx
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
namespace test_stylize_attack_vfx_layers_unit {
// stylize_attack_vfx_layers.cpp
using namespace eve::stylize;

namespace {

[[maybe_unused]] eve::LogicalId id(std::string_view value) {
    auto parsed = eve::LogicalId::parse(value);
    REQUIRE(parsed.has_value());
    return std::move(*parsed);
}

[[maybe_unused]] IAttackVfxLayerExecutor *executorFor(AttackVfxLayerRole role) {
    IAttackVfxLayerExecutor *found = nullptr;
    eve::cap::forEach<IAttackVfxLayerExecutor>([&](IAttackVfxLayerExecutor *executor) {
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

const char *kMeshEffects[] = {"rim", "dissolve", "hologram", "snow", "slash", "ember", "aura"};
const char *kPostEffects[] = {"vignette", "chromatic", "grain"};

[[maybe_unused]] bool near(float a, float b, float eps = 0.02f) { return std::fabs(a - b) <= eps; }

[[maybe_unused]] ImageData *makeGradient(int w, int h) {
    auto *img = new ImageData(w, h, "RGBA8");
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
    auto *mod = Stylize::create();
    REQUIRE(mod != nullptr);

    for (const char *id : kMeshEffects) {
        CHECK(mod->hasStyle(id));
        CHECK(mod->hasMeshStyle(id));
        CHECK(!mod->supports(id, "post"));
        CHECK(mod->supports(id, "mesh"));
    }
    for (const char *id : kPostEffects) {
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
    } catch (const eve::Exception &) {
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
[[maybe_unused]] std::vector<unsigned char> drawYsaFrame(eve::graphics::Graphics *gfx, eve::graphics::Canvas *canvas,
                                                         eve::graphics::Mesh *mesh, eve::graphics::Shader *shader,
                                                         const glm::vec4 &tint) {
    gfx->begin3DFrameToCanvas(canvas);
    gfx->drawMeshShader(mesh, glm::mat4(1.f), nullptr, tint, shader);
    gfx->end3DFrameToCanvas();
    std::unique_ptr<eve::image::ImageData> pixels(canvas->newImageData());
    if (!pixels) return {};
    const auto  *data = static_cast<const unsigned char *>(pixels->getData());
    const size_t size = size_t(kYsaTestSize) * size_t(kYsaTestSize) * 4u;
    return std::vector<unsigned char>(data, data + size);
}

[[maybe_unused]] size_t countDifferentPixels(const std::vector<unsigned char> &a, const std::vector<unsigned char> &b) {
    size_t changed = 0;
    for (size_t i = 0; i + 3 < a.size() && i + 3 < b.size(); i += 4)
        if (a[i] != b[i] || a[i + 1] != b[i + 1] || a[i + 2] != b[i + 2]) ++changed;
    return changed;
}

[[maybe_unused]] long totalLuma(const std::vector<unsigned char> &pixels) {
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
        const auto *parameter = styleParameterAt("ysa", i);
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
}  // namespace test_stylize_attack_vfx_layers_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_stylize_beam_effect)
#include "PrivateTestScope.h"
#include <array>
#include <glm/geometric.hpp>
#include "stylize/BeamEffect.h"
#include "stylize/BeamSkillEffect.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// stylize_beam_effect.cpp
namespace test_stylize_beam_effect {
// stylize_beam_effect.cpp
using namespace eve::stylize;

TEST_CASE("stylize.beamEffect.buildsExpectedRibbonTopology") {
    BeamEffectConfig config;
    config.segments       = 8;
    config.noiseAmplitude = 0.f;
    const auto beam       = buildBeamEffect({0.f, 0.f, 0.f}, {0.f, 0.f, 4.f}, {0.f, -1.f, 0.f}, config);
    REQUIRE_EQ(static_cast<int>(beam.status), static_cast<int>(BeamBuildStatus::Built));
    REQUIRE_EQ(beam.mesh.vertices.size(), 18u);
    REQUIRE_EQ(beam.mesh.indices.size(), 48u);
    REQUIRE(glm::length(beam.mesh.vertices.front().position - beam.mesh.vertices[1].position) > 0.11f);
}

TEST_CASE("stylize.beamEffect.noiseAndBranchesAreDeterministic") {
    BeamEffectConfig config;
    config.segments       = 10;
    config.noiseAmplitude = 0.2f;
    config.randomSeed     = 91;
    config.branchCount    = 3;
    config.branchSegments = 4;
    const auto first      = buildBeamEffect({1.f, 2.f, 3.f}, {5.f, 4.f, 9.f}, {0.f, 0.f, -1.f}, config);
    const auto second     = buildBeamEffect({1.f, 2.f, 3.f}, {5.f, 4.f, 9.f}, {0.f, 0.f, -1.f}, config);
    REQUIRE_EQ(first.mesh.vertices.size(), 52u);
    REQUIRE_EQ(first.mesh.indices.size(), 132u);
    REQUIRE_EQ(first.mesh.vertices.size(), second.mesh.vertices.size());
    for (std::size_t i = 0; i < first.mesh.vertices.size(); ++i) {
        REQUIRE_EQ(first.mesh.vertices[i].position, second.mesh.vertices[i].position);
        REQUIRE_EQ(first.mesh.vertices[i].alpha, second.mesh.vertices[i].alpha);
    }
}

TEST_CASE("stylize.beamEffect.rejectsDegenerateAndInvalidInputs") {
    const auto degenerate = buildBeamEffect({1.f, 1.f, 1.f}, {1.f, 1.f, 1.f}, {0.f, 0.f, -1.f});
    REQUIRE_EQ(static_cast<int>(degenerate.status), static_cast<int>(BeamBuildStatus::DegenerateInput));
    REQUIRE(degenerate.mesh.vertices.empty());

    BeamEffectConfig invalid;
    invalid.widthStart  = -1.f;
    const auto rejected = buildBeamEffect({0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 0.f, -1.f}, invalid);
    REQUIRE_EQ(static_cast<int>(rejected.status), static_cast<int>(BeamBuildStatus::InvalidConfig));
}

// stylize_beam_skill_effect.cpp
using namespace eve::stylize;

TEST_CASE("stylize.beamSkill.presetsExposeProductionDefaults") {
    BeamSkillEffect laser(BeamSkillPreset::Laser);
    BeamSkillEffect lightning(BeamSkillPreset::Lightning);
    REQUIRE(laser.geometry().noiseAmplitude == 0.f);
    REQUIRE(lightning.geometry().noiseAmplitude > 0.f);
    REQUIRE(lightning.geometry().branchCount > 0u);
    REQUIRE(laser.effect().playback().loop);
    REQUIRE(!lightning.effect().playback().loop);
}

TEST_CASE("stylize.beamSkill.chainMergesSegmentsWithRebasedIndices") {
    BeamSkillEffect  chain(BeamSkillPreset::ChainLightning);
    const std::array points{glm::vec3(0.f, 0.f, 0.f), glm::vec3(1.f, 1.f, 0.f), glm::vec3(2.f, 0.f, 1.f),
                            glm::vec3(3.f, 1.f, 1.f)};
    chain.setPath(points);
    const auto built = chain.build({0.f, 0.f, -1.f});
    REQUIRE_EQ(static_cast<int>(built.status), static_cast<int>(BeamBuildStatus::Built));
    REQUIRE(!built.mesh.vertices.empty());
    REQUIRE(!built.mesh.indices.empty());
    for (const auto index : built.mesh.indices) REQUIRE(index < built.mesh.vertices.size());
}

TEST_CASE("stylize.beamSkill.lifecycleControlsSubmissionEnvelope") {
    BeamSkillEffect lightning(BeamSkillPreset::Lightning);
    REQUIRE_EQ(static_cast<int>(lightning.effect().state()), static_cast<int>(MeshEffectState::Stopped));
    lightning.play();
    lightning.update(0.05f);
    REQUIRE(lightning.effect().intensity() > 0.f);
    lightning.stop(0.f);
    REQUIRE_EQ(static_cast<int>(lightning.effect().state()), static_cast<int>(MeshEffectState::Finished));
}

TEST_CASE("stylize.beamSkill.rejectsMissingPath") {
    BeamSkillEffect laser(BeamSkillPreset::Laser);
    const auto      built = laser.build({0.f, 0.f, -1.f});
    REQUIRE_EQ(static_cast<int>(built.status), static_cast<int>(BeamBuildStatus::DegenerateInput));
}
}  // namespace test_stylize_beam_effect
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
namespace test_stylize_mesh_particles_unit {
// stylize_mesh_particles.cpp
using namespace eve::stylize;

TEST_CASE("stylize.meshParticles.fixedStepDeterminismAndBurst") {
    MeshParticleEmitterConfig config;
    config.capacity         = 32;
    config.emissionRate     = 10.f;
    config.lifetimeMin      = 2.f;
    config.lifetimeMax      = 2.f;
    config.speedMin         = 1.f;
    config.speedMax         = 1.f;
    config.fixedStepSeconds = 0.1f;
    config.maximumSubsteps  = 8;
    config.randomSeed       = 42;
    config.bursts           = {{0.2f, 3}};

    MeshParticleEmitter first(config);
    MeshParticleEmitter second(config);
    first.start();
    second.start();
    auto firstReport  = first.advance(0.3f);
    auto secondReport = second.advance(0.1f);
    secondReport      = second.advance(0.2f);
    REQUIRE_EQ(firstReport.alive, 6u);
    REQUIRE_EQ(first.snapshot().size(), second.snapshot().size());
    const auto a = first.snapshot();
    const auto b = second.snapshot();
    for (std::size_t i = 0; i < a.size(); ++i) {
        REQUIRE_EQ(a[i].stableId, b[i].stableId);
        REQUIRE_EQ(a[i].model, b[i].model);
        REQUIRE_EQ(a[i].color, b[i].color);
    }
}

TEST_CASE("stylize.meshParticles.capacityReportsDroppedSpawns") {
    MeshParticleEmitterConfig config;
    config.capacity         = 4;
    config.fixedStepSeconds = 0.1f;
    config.bursts           = {{0.f, 7}};
    MeshParticleEmitter emitter(config);
    emitter.start();
    const auto report = emitter.advance(0.1f);
    REQUIRE_EQ(report.spawned, 4u);
    REQUIRE_EQ(report.dropped, 3u);
    REQUIRE_EQ(report.alive, 4u);
}

TEST_CASE("stylize.meshParticles.substepGuardReportsDiscardedTime") {
    MeshParticleEmitterConfig config;
    config.fixedStepSeconds = 0.1f;
    config.maximumSubsteps  = 2;
    MeshParticleEmitter emitter(config);
    emitter.start();
    const auto report = emitter.advance(0.55f);
    REQUIRE_EQ(report.simulatedSteps, 2u);
    REQUIRE(report.discardedTimeSeconds > 0.29f);
    REQUIRE(report.discardedTimeSeconds < 0.31f);
}

TEST_CASE("stylize.meshParticles.bridgeUsesSharedLodAndAlphaOrdering") {
    MeshParticleEmitterConfig config;
    config.capacity         = 3;
    config.fixedStepSeconds = 0.1f;
    MeshParticleEmitter emitter(config);
    emitter.setOrigin({0.f, 0.f, 5.f});
    const auto emitted = emitter.emit(3);
    REQUIRE(emitted == std::size_t{3});
    MeshEffectInstance    effect("slash");
    const MeshVfxBatchKey key{11, 12, 13, 0, MeshVfxBatchBlend::Alpha};
    const auto            inputs =
        buildMeshParticleRenderInputs(emitter.snapshot(), &effect, reinterpret_cast<eve::graphics::Mesh *>(1), nullptr,
                                      key, 77, glm::vec3(0.f), 100.f, 50.f, 2);
    REQUIRE_EQ(inputs.lodCandidates.size(), 3u);
    REQUIRE_EQ(inputs.renderItems.size(), 3u);
    REQUIRE_EQ(inputs.commands.size(), 3u);
    REQUIRE_EQ(inputs.commands[0].stableInstanceId, inputs.renderItems[0].stableInstanceId);
    REQUIRE_EQ(static_cast<int>(inputs.commands[0].kind), static_cast<int>(MeshVfxRendererCommand::Kind::Particle));

    MeshVfxScalabilityPolicy policy;
    policy.maximumDistance = 50.f;
    policy.workUnitBudget  = 12;
    const auto decisions   = MeshVfxScalabilityPlanner(policy).plan(inputs.lodCandidates);
    const auto queue       = MeshVfxRenderBatchPlanner{}.build(inputs.renderItems, decisions, 0);
    REQUIRE_EQ(queue.stats.queuedDraws, 3u);
    REQUIRE_EQ(queue.batches.size(), 1u);
}

TEST_CASE("stylize.meshParticles.curvesDriveScaleColorAndRotation") {
    MeshParticleEmitterConfig config;
    config.capacity           = 1;
    config.lifetimeMin        = 1.f;
    config.lifetimeMax        = 1.f;
    config.fixedStepSeconds   = 0.25f;
    config.rotationMinRadians = 0.f;
    config.rotationMaxRadians = 0.f;
    config.angularVelocityMin = 2.f;
    config.angularVelocityMax = 2.f;
    config.scaleCurve         = {{1.f, 0.f}, {0.f, 2.f}, {0.5f, 1.f}};
    config.colorGradient      = {{0.f, {1.f, 0.f, 0.f, 1.f}}, {1.f, {0.f, 0.f, 1.f, 0.f}}};
    MeshParticleEmitter emitter(config);
    const auto          emitted = emitter.emit(1);
    REQUIRE(emitted == std::size_t{1});
    const auto report = emitter.advance(0.5f);
    REQUIRE_EQ(report.simulatedSteps, 2u);
    const auto particles = emitter.snapshot();
    REQUIRE_EQ(particles.size(), 1u);
    REQUIRE(particles[0].color.r > 0.49f);
    REQUIRE(particles[0].color.r < 0.51f);
    REQUIRE(particles[0].color.b > 0.49f);
    REQUIRE(particles[0].color.b < 0.51f);
    const float axisScale = glm::length(glm::vec3(particles[0].model[0]));
    REQUIRE(axisScale > 0.99f);
    REQUIRE(axisScale < 1.01f);
}

TEST_CASE("stylize.meshParticles.volumeShapesStayInsideConfiguredBounds") {
    MeshParticleEmitterConfig sphere;
    sphere.capacity     = 64;
    sphere.shape        = MeshParticleShape::Sphere;
    sphere.sphereRadius = 3.f;
    sphere.randomSeed   = 7;
    MeshParticleEmitter sphereEmitter(sphere);
    const auto          sphereEmitted = sphereEmitter.emit(64);
    REQUIRE(sphereEmitted == std::size_t{64});
    for (const auto &particle : sphereEmitter.snapshot()) {
        REQUIRE(glm::length(glm::vec3(particle.model[3])) <= 3.0001f);
    }

    MeshParticleEmitterConfig box;
    box.capacity   = 64;
    box.shape      = MeshParticleShape::Box;
    box.boxExtents = {1.f, 2.f, 3.f};
    box.randomSeed = 9;
    MeshParticleEmitter boxEmitter(box);
    const auto          boxEmitted = boxEmitter.emit(64);
    REQUIRE(boxEmitted == std::size_t{64});
    for (const auto &particle : boxEmitter.snapshot()) {
        const glm::vec3 position(particle.model[3]);
        REQUIRE(std::abs(position.x) <= 1.0001f);
        REQUIRE(std::abs(position.y) <= 2.0001f);
        REQUIRE(std::abs(position.z) <= 3.0001f);
    }
}

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

TEST_CASE("stylize.meshVfx.playbackAndBinding") {
    auto *module = Stylize::create();
    auto  effect = module->createMeshEffect("ember");
    REQUIRE(effect.get() != nullptr);
    CHECK(!effect->isBound());
    effect->bindTarget(MeshEffectTargetHandle(7, 2));
    CHECK(effect->isBound());
    CHECK_EQ(effect->target().index(), 7u);
    CHECK_EQ(effect->target().generation(), 2u);
    effect->setPlayback(MeshEffectPlayback{0.1f, 0.2f, 0.1f, false});
    effect->play();
    CHECK(static_cast<int>(effect->state()) == static_cast<int>(MeshEffectState::FadingIn));
    effect->update(0.05f);
    CHECK(std::fabs(effect->intensity() - 0.5f) < 0.001f);
    effect->update(0.10f);
    CHECK(static_cast<int>(effect->state()) == static_cast<int>(MeshEffectState::Active));
    CHECK(std::fabs(effect->style().getFloat("time") - 0.15f) < 0.001f);
    effect->stop(0.1f);
    effect->update(0.05f);
    CHECK(std::fabs(effect->intensity() - 0.5f) < 0.001f);
    effect->update(0.05f);
    CHECK(static_cast<int>(effect->state()) == static_cast<int>(MeshEffectState::Finished));
    effect->unbindTarget();
    CHECK(!effect->isBound());
}

TEST_CASE("stylize.meshVfx.ribbonGeometryAndDiscontinuity") {
    TrailEmitter trail(TrailSettings{8, 1.f, 0.01f, 1.f});
    CHECK(static_cast<int>(trail.append({0.f, 0.f, 0.f}, {0.f, 1.f, 0.f})) ==
          static_cast<int>(TrailAppendResult::StartedSegment));
    CHECK(static_cast<int>(trail.append({0.5f, 0.f, 0.f}, {0.5f, 1.f, 0.f})) ==
          static_cast<int>(TrailAppendResult::Added));
    auto mesh = trail.buildMesh();
    CHECK_EQ(mesh.vertices.size(), 4u);
    CHECK_EQ(mesh.indices.size(), 6u);
    CHECK_EQ(mesh.vertices[0].uv.x, 0.f);
    CHECK_EQ(mesh.vertices[2].uv.x, 1.f);
    CHECK(static_cast<int>(trail.append({4.f, 0.f, 0.f}, {4.f, 1.f, 0.f})) ==
          static_cast<int>(TrailAppendResult::StartedSegment));
    mesh = trail.buildMesh();
    CHECK_EQ(mesh.vertices.size(), 6u);
    CHECK_EQ(mesh.indices.size(), 6u);
    trail.update(1.f);
    CHECK_EQ(trail.sampleCount(), 0u);
    CHECK(trail.buildMesh().indices.empty());
}

TEST_CASE("stylize.meshVfx.rejectsInvalidConfiguration") {
    bool threw = false;
    try {
        TrailEmitter invalid(TrailSettings{1, 1.f, 0.f, 1.f});
    } catch (const eve::Exception &) {
        threw = true;
    }
    CHECK(threw);
    threw = false;
    try {
        auto *module = Stylize::create();
        auto  effect = module->createMeshEffect("vignette");
        (void)effect;
    } catch (const eve::Exception &) {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE("stylize.meshVfx.preparesBackendUpload") {
    TrailEmitter trail(TrailSettings{8, 1.f, 0.f, 2.f});
    (void)trail.append({0.f, 0.f, 0.f}, {0.f, 1.f, 0.f});
    (void)trail.append({0.5f, 0.f, 0.f}, {0.5f, 1.f, 0.f});
    auto prepared = prepareTrailUpload(trail.buildMesh());
    REQUIRE(prepared.ok());
    const auto &upload = prepared.value();
    CHECK_EQ(upload.positions.size(), 12u);
    CHECK_EQ(upload.normals.size(), 12u);
    CHECK_EQ(upload.uvs.size(), 8u);
    CHECK_EQ(upload.indices.size(), 6u);
    CHECK(std::fabs(upload.normals[2]) > 0.9f);
}


TEST_CASE("stylize.meshVfx.skillRecipesComposeRuntime") {
    auto *stylize = Stylize::create();
    auto  slash   = stylize->createSkillMeshEffect(SkillMeshEffectKind::WeaponSlash);
    REQUIRE(slash.get() != nullptr);
    CHECK(slash->hasTrail());
    CHECK_EQ(slash->effect().style().getStyle(), "slash");
    slash->bindTarget(MeshEffectTargetHandle(4, 9));
    slash->play();
    CHECK(static_cast<int>(slash->appendBlade({0.f, 0.f, 0.f}, {0.f, 1.f, 0.f})) ==
          static_cast<int>(TrailAppendResult::StartedSegment));
    slash->update(0.03f);
    CHECK(slash->effect().intensity() > 0.f);
    CHECK_EQ(slash->trail().sampleCount(), 1u);

    auto impact = stylize->createSkillMeshEffect(SkillMeshEffectKind::ImpactFlash);
    REQUIRE(impact.get() != nullptr);
    CHECK(!impact->hasTrail());
    CHECK_EQ(impact->effect().style().getStyle(), "rim");
    impact->play();
    impact->update(0.2f);
    CHECK(static_cast<int>(impact->effect().state()) == static_cast<int>(MeshEffectState::Finished));

    bool rejectedBladeSample = false;
    try {
        (void)impact->appendBlade({0.f, 0.f, 0.f}, {0.f, 1.f, 0.f});
    } catch (const eve::Exception &) {
        rejectedBladeSample = true;
    }
    CHECK(rejectedBladeSample);
}


// stylize_mesh_vfx_asset.cpp
using namespace eve::stylize;

TEST_CASE("stylize.mesh_vfx_asset parses layered version one assets") {
    auto parsed = MeshVfxAsset::fromJson(R"({
        "schema":"eve.stylize.mesh-vfx","schemaVersion":1,
        "layers":[
          {"style":"slash","playback":{"fadeIn":0.02,"hold":0.1,"fadeOut":0.2},"parameters":{"intensity":2}},
          {"style":"rim","playback":{"loop":true}}
        ],
        "trail":{"maxSamples":48,"lifetime":0.25,"minDistance":0.01,"teleportDistance":3}
    })");
    REQUIRE(parsed.ok());
    auto asset = std::move(parsed).takeValue();
    REQUIRE_EQ(asset.layers.size(), 2u);
    REQUIRE(asset.trail.has_value());
    auto created = MeshVfxAssetInstance::create(asset);
    REQUIRE(created.ok());
    auto instance = std::move(created).takeValue();
    REQUIRE_EQ(instance->layerCount(), 2u);
    REQUIRE(instance->hasTrail());
}

TEST_CASE("stylize.mesh_vfx_asset migrates legacy version zero") {
    auto parsed = MeshVfxAsset::fromJson(
        R"({"schemaVersion":0,"style":"aura","playback":{"hold":0.5},"parameters":{"power":1.5}})");
    REQUIRE(parsed.ok());
    REQUIRE_EQ(parsed.value().layers.size(), 1u);
    REQUIRE_EQ(parsed.value().layers.front().style, "aura");
}

TEST_CASE("stylize.mesh_vfx_asset rejects unknown and invalid fields") {
    auto unknown = MeshVfxAsset::fromJson(
        R"({"schema":"eve.stylize.mesh-vfx","schemaVersion":1,"layers":[{"style":"rim","typo":1}]})");
    REQUIRE(!unknown.ok());
    auto invalid = MeshVfxAsset::fromJson(
        R"({"schema":"eve.stylize.mesh-vfx","schemaVersion":1,"layers":[{"style":"rim","playback":{"hold":-1}}]})");
    REQUIRE(!invalid.ok());
}

TEST_CASE("stylize.mesh_vfx_asset reload is transactional") {
    auto parsed =
        MeshVfxAsset::fromJson(R"({"schema":"eve.stylize.mesh-vfx","schemaVersion":1,"layers":[{"style":"rim"}]})");
    REQUIRE(parsed.ok());
    MeshVfxAssetSlot slot(std::move(parsed).takeValue());
    auto             failed = slot.reload(R"({"schemaVersion":99})");
    REQUIRE(!failed.ok());
    REQUIRE_EQ(slot.revision(), 1u);
    REQUIRE_EQ(slot.asset().layers.front().style, "rim");
    auto applied = slot.reload(R"({"schema":"eve.stylize.mesh-vfx","schemaVersion":1,"layers":[{"style":"ember"}]})");
    REQUIRE(applied.ok());
    REQUIRE_EQ(applied.value(), 2u);
    REQUIRE_EQ(slot.asset().layers.front().style, "ember");
}

TEST_CASE("stylize.mesh_vfx_asset evaluates authored parameter curves") {
    MeshVfxFloatCurve curve{{{0.f, 0.f}, {0.5f, 2.f}, {1.f, 0.f}}};
    REQUIRE_EQ(curve.evaluate(0.25f), 1.f);
    REQUIRE_EQ(curve.evaluate(0.75f), 1.f);
    auto invalid = MeshVfxAsset::fromJson(
        R"({"schema":"eve.stylize.mesh-vfx","schemaVersion":1,"layers":[{"style":"rim","curves":{"intensity":[[0.5,1],[0.25,2]]}}]})");
    REQUIRE(!invalid.ok());
}

TEST_CASE("stylize.mesh_vfx_asset emits ordered timeline events without callbacks") {
    auto parsed = MeshVfxAsset::fromJson(R"({
      "schema":"eve.stylize.mesh-vfx","schemaVersion":1,
      "layers":[{"style":"rim","playback":{"fadeIn":0,"hold":1,"fadeOut":0}}],
      "events":[{"time":0,"name":"cast"},{"time":0.25,"name":"hit"},{"time":0.75,"name":"recover"}]
    })");
    REQUIRE(parsed.ok());
    auto created = MeshVfxAssetInstance::create(parsed.value());
    REQUIRE(created.ok());
    auto instance = std::move(created).takeValue();
    instance->play();
    auto events = instance->drainEvents();
    REQUIRE_EQ(events.size(), 1u);
    REQUIRE_EQ(events.front(), "cast");
    instance->update(0.3f);
    events = instance->drainEvents();
    REQUIRE_EQ(events.size(), 1u);
    REQUIRE_EQ(events.front(), "hit");
    instance->update(0.5f);
    events = instance->drainEvents();
    REQUIRE_EQ(events.size(), 1u);
    REQUIRE_EQ(events.front(), "recover");
}

TEST_CASE("stylize.mesh_vfx_asset serializes migrated assets in canonical schema") {
    auto legacy = MeshVfxAsset::fromJson(
        R"({"schemaVersion":0,"style":"aura","playback":{"hold":0.5},"events":[{"time":0.5,"name":"pulse"}]})");
    REQUIRE(legacy.ok());
    auto encoded = legacy.value().toJson();
    REQUIRE(encoded.ok());
    auto decoded = MeshVfxAsset::fromJson(encoded.value());
    REQUIRE(decoded.ok());
    REQUIRE_EQ(decoded.value().layers.front().style, "aura");
    REQUIRE_EQ(decoded.value().events.front().name, "pulse");
}

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

TEST_CASE("stylize.mesh_vfx_asset samples authored weapon attachments into its trail") {
    auto parsed = MeshVfxAsset::fromJson(R"({
      "schema":"eve.stylize.mesh-vfx","schemaVersion":1,
      "layers":[{"style":"slash"}],
      "trail":{"maxSamples":8,"lifetime":0.2,"minDistance":0,"teleportDistance":5,
               "rootAttachment":"weapon.root","tipAttachment":"weapon.tip",
               "rootOffset":[0,0.25,0],"tipOffset":[0,0.25,0]}
    })");
    REQUIRE(parsed.ok());
    auto created = MeshVfxAssetInstance::create(parsed.value());
    REQUIRE(created.ok());
    auto                    instance = std::move(created).takeValue();
    MeshVfxAttachmentSource source;
    auto                    sampled = instance->sampleTrail(source);
    REQUIRE(sampled.ok());
    REQUIRE(instance->trail().sampleCount() > 0u);
    auto encoded = parsed.value().toJson();
    REQUIRE(encoded.ok());
    REQUIRE(MeshVfxAsset::fromJson(encoded.value()).ok());
}

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

TEST_CASE("stylize.mesh_vfx_asset maps animation notifications to lifecycle actions") {
    auto parsed = MeshVfxAsset::fromJson(R"({
      "schema":"eve.stylize.mesh-vfx","schemaVersion":1,
      "layers":[{"style":"rim"}],
      "trail":{"rootAttachment":"root","tipAttachment":"tip"},
      "animationTriggers":{"play":["attack.start"],"stop":["attack.end"],"trailBreak":["attack.cancel"]}
    })");
    REQUIRE(parsed.ok());
    auto created = MeshVfxAssetInstance::create(parsed.value());
    REQUIRE(created.ok());
    auto                   instance = std::move(created).takeValue();
    MeshVfxAnimationEvents events;
    events.names  = {"ignored", "attack.start"};
    auto dispatch = instance->processAnimationEvents(events);
    REQUIRE_EQ(dispatch.handled, 1u);
    REQUIRE_EQ(dispatch.played, 1u);
    events.names = {"attack.cancel", "attack.end"};
    dispatch     = instance->processAnimationEvents(events);
    REQUIRE_EQ(dispatch.trailBreaks, 1u);
    REQUIRE_EQ(dispatch.stopped, 1u);

    auto ambiguous = MeshVfxAsset::fromJson(R"({
      "schema":"eve.stylize.mesh-vfx","schemaVersion":1,"layers":[{"style":"rim"}],
      "animationTriggers":{"play":["same"],"stop":["same"]}
    })");
    REQUIRE(!ambiguous.ok());
}

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

TEST_CASE("stylize.mesh_vfx_batch filters LOD and groups additive state") {
    const std::array items{
        MeshVfxRenderItem{3, key(8, MeshVfxBatchBlend::Additive), 2.0f},
        MeshVfxRenderItem{1, key(7, MeshVfxBatchBlend::Additive), 4.0f},
        MeshVfxRenderItem{2, key(7, MeshVfxBatchBlend::Additive), 8.0f},
        MeshVfxRenderItem{4, key(9, MeshVfxBatchBlend::Additive), 1.0f},
    };
    const std::array decisions{
        visible(1, MeshVfxLodTier::Full),
        visible(2, MeshVfxLodTier::Reduced),
        visible(3, MeshVfxLodTier::Minimal),
        visible(4, MeshVfxLodTier::Culled),
    };

    const auto queue = MeshVfxRenderBatchPlanner{}.build(items, decisions, 0);
    REQUIRE_EQ(queue.stats.queuedDraws, 3u);
    REQUIRE_EQ(queue.stats.culledDraws, 1u);
    REQUIRE_EQ(queue.batches.size(), 2u);
    REQUIRE_EQ(queue.batches[0].draws.size(), 2u);
    REQUIRE_EQ(queue.batches[0].draws[0].stableInstanceId, 1u);
    REQUIRE_EQ(queue.batches[0].draws[1].stableInstanceId, 2u);
}

TEST_CASE("stylize.mesh_vfx_batch preserves alpha back-to-front order") {
    const std::array items{
        MeshVfxRenderItem{1, key(7, MeshVfxBatchBlend::Alpha), 2.0f},
        MeshVfxRenderItem{2, key(8, MeshVfxBatchBlend::Alpha), 9.0f},
        MeshVfxRenderItem{3, key(7, MeshVfxBatchBlend::Alpha), 5.0f},
    };
    const std::array decisions{
        visible(1, MeshVfxLodTier::Full),
        visible(2, MeshVfxLodTier::Full),
        visible(3, MeshVfxLodTier::Full),
    };
    const auto queue = MeshVfxRenderBatchPlanner{}.build(items, decisions, 0);
    REQUIRE_EQ(queue.batches.size(), 2u);
    REQUIRE_EQ(queue.batches[0].draws.size(), 1u);
    REQUIRE_EQ(queue.batches[1].draws.size(), 2u);
    REQUIRE_EQ(queue.batches[0].draws[0].stableInstanceId, 2u);
    REQUIRE_EQ(queue.batches[1].draws[0].stableInstanceId, 3u);
    REQUIRE_EQ(queue.batches[1].draws[1].stableInstanceId, 1u);

    // Equal material keys must not merge across an intervening alpha draw.
    auto interleavedItems         = items;
    interleavedItems[2].viewDepth = 10.0f;
    const auto interleaved        = MeshVfxRenderBatchPlanner{}.build(interleavedItems, decisions, 0);
    REQUIRE_EQ(interleaved.batches.size(), 3u);
    for (const auto &batch : interleaved.batches) {
        REQUIRE_EQ(batch.draws.size(), 1u);
    }
    REQUIRE_EQ(interleaved.batches[0].draws[0].stableInstanceId, 3u);
    REQUIRE_EQ(interleaved.batches[1].draws[0].stableInstanceId, 2u);
    REQUIRE_EQ(interleaved.batches[2].draws[0].stableInstanceId, 1u);
}

TEST_CASE("stylize.mesh_vfx_batch staggers reduced mesh refreshes") {
    const std::array items{
        MeshVfxRenderItem{1, key(7, MeshVfxBatchBlend::Additive), 1.0f},
        MeshVfxRenderItem{2, key(7, MeshVfxBatchBlend::Additive), 1.0f},
        MeshVfxRenderItem{9, key(7, MeshVfxBatchBlend::Additive), 1.0f},
    };
    const std::array decisions{
        visible(1, MeshVfxLodTier::Reduced, 2),
        visible(2, MeshVfxLodTier::Reduced, 2),
    };
    const auto queue = MeshVfxRenderBatchPlanner{}.build(items, decisions, 4);
    REQUIRE_EQ(queue.stats.queuedDraws, 2u);
    REQUIRE_EQ(queue.stats.missingDecisions, 1u);
    REQUIRE_EQ(queue.stats.dynamicMeshRefreshes, 1u);
    REQUIRE_EQ(static_cast<int>(queue.batches[0].draws[0].meshUpdate), static_cast<int>(MeshVfxMeshUpdate::Reuse));
    REQUIRE_EQ(static_cast<int>(queue.batches[0].draws[1].meshUpdate), static_cast<int>(MeshVfxMeshUpdate::Refresh));
}
}  // namespace test_stylize_mesh_particles_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_stylize_mesh_vfx_benchmark)
#include "PrivateTestScope.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <glm/mat4x4.hpp>
#include <iostream>
#include <random>
#include <vector>
#include "Fixtures.h"
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/Texture.h"
#include "stylize/MeshEffect.h"
#include "stylize/MeshEffectRenderer.h"
#include "stylize/MeshVfxBatchSubmission.h"
#include "stylize/MeshVfxRenderBatch.h"
#include "stylize/MeshVfxScalability.h"
#include "stylize/TrailEffect.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// stylize_mesh_vfx_benchmark.cpp
namespace test_stylize_mesh_vfx_benchmark_unit {
// stylize_mesh_vfx_benchmark.cpp
using namespace eve::stylize;


// stylize_mesh_vfx_refraction.cpp
using namespace eve::graphics;
using namespace eve::stylize;


// stylize_mesh_vfx_scalability.cpp
using namespace eve::stylize;

TEST_CASE("stylize.mesh_vfx_scalability selects deterministic tiers within budget") {
    MeshVfxScalabilityPolicy policy;
    policy.workUnitBudget = 7;
    MeshVfxScalabilityPlanner planner(policy);

    const std::array candidates{
        MeshVfxLodCandidate{30, 8.0f, 120.0f, 0, true},
        MeshVfxLodCandidate{10, 4.0f, 120.0f, 10, true},
        MeshVfxLodCandidate{20, 6.0f, 60.0f, 5, true},
        MeshVfxLodCandidate{40, 10.0f, 20.0f, -1, true},
    };

    const auto decisions = planner.plan(candidates);
    REQUIRE_EQ(decisions.size(), candidates.size());
    REQUIRE_EQ(static_cast<int>(decisions[0].tier), static_cast<int>(MeshVfxLodTier::Minimal));
    REQUIRE_EQ(static_cast<int>(decisions[1].tier), static_cast<int>(MeshVfxLodTier::Full));
    REQUIRE_EQ(static_cast<int>(decisions[2].tier), static_cast<int>(MeshVfxLodTier::Reduced));
    REQUIRE_EQ(static_cast<int>(decisions[3].tier), static_cast<int>(MeshVfxLodTier::Culled));
    REQUIRE_EQ(decisions[2].trailSampleStride, 2u);
    REQUIRE_EQ(decisions[0].meshUpdateInterval, 4u);
    REQUIRE_EQ(decisions[0].workUnits, 1u);
    REQUIRE_EQ(decisions[1].workUnits, 4u);
    REQUIRE_EQ(decisions[2].workUnits, 2u);
    REQUIRE_EQ(decisions[3].workUnits, 0u);
    std::uint32_t usedWorkUnits = 0;
    for (const auto &decision : decisions) {
        usedWorkUnits += decision.workUnits;
    }
    REQUIRE_EQ(usedWorkUnits, policy.workUnitBudget);
}

TEST_CASE("stylize.mesh_vfx_scalability is independent of candidate iteration order") {
    MeshVfxScalabilityPolicy policy;
    policy.workUnitBudget = 4;
    MeshVfxScalabilityPlanner planner(policy);

    const std::array forward{
        MeshVfxLodCandidate{2, 5.0f, 100.0f, 0, true},
        MeshVfxLodCandidate{1, 5.0f, 100.0f, 0, true},
    };
    const std::array reverse{forward[1], forward[0]};
    const auto       a = planner.plan(forward);
    const auto       b = planner.plan(reverse);

    REQUIRE_EQ(static_cast<int>(a[0].tier), static_cast<int>(MeshVfxLodTier::Culled));
    REQUIRE_EQ(static_cast<int>(a[1].tier), static_cast<int>(MeshVfxLodTier::Full));
    REQUIRE_EQ(static_cast<int>(b[0].tier), static_cast<int>(MeshVfxLodTier::Full));
    REQUIRE_EQ(static_cast<int>(b[1].tier), static_cast<int>(MeshVfxLodTier::Culled));
}

TEST_CASE("stylize.mesh_vfx_scalability culls invalid and out-of-range candidates") {
    MeshVfxScalabilityPlanner planner;
    const std::array          candidates{
        MeshVfxLodCandidate{1, 1.0f, 100.0f, 0, false},
        MeshVfxLodCandidate{2, 81.0f, 100.0f, 0, true},
        MeshVfxLodCandidate{3, -1.0f, 100.0f, 0, true},
    };
    const auto decisions = planner.plan(candidates);
    for (const auto &decision : decisions) {
        REQUIRE_EQ(static_cast<int>(decision.tier), static_cast<int>(MeshVfxLodTier::Culled));
        REQUIRE_EQ(decision.workUnits, 0u);
    }
}

// stylize_mesh_vfx_submission.cpp
using namespace eve::stylize;

namespace {

class RecordingSink final : public IMeshVfxBatchSink {
public:
    MeshVfxSubmitStatus        beginResult = MeshVfxSubmitStatus::Accepted;
    MeshVfxSubmitStatus        drawResult  = MeshVfxSubmitStatus::Accepted;
    MeshVfxSubmitStatus        endResult   = MeshVfxSubmitStatus::Accepted;
    std::vector<std::uint64_t> draws;
    std::uint32_t              begins = 0;
    std::uint32_t              ends   = 0;

    MeshVfxSubmitStatus beginBatch(const MeshVfxBatchKey &) override {
        ++begins;
        return beginResult;
    }

    MeshVfxSubmitStatus submitDraw(const MeshVfxBatchedDraw &draw) override {
        draws.push_back(draw.stableInstanceId);
        return drawResult;
    }

    MeshVfxSubmitStatus endBatch() override {
        ++ends;
        return endResult;
    }
};

[[maybe_unused]] MeshVfxRenderQueue queueWithTwoDraws() {
    MeshVfxRenderQueue queue;
    MeshVfxRenderBatch batch;
    batch.draws.push_back(MeshVfxBatchedDraw{1, MeshVfxLodTier::Full, MeshVfxMeshUpdate::Refresh, 1});
    batch.draws.push_back(MeshVfxBatchedDraw{2, MeshVfxLodTier::Reduced, MeshVfxMeshUpdate::Reuse, 2});
    queue.batches.push_back(std::move(batch));
    return queue;
}

}  // namespace

TEST_CASE("stylize.mesh_vfx_submission executes ordered batches") {
    RecordingSink sink;
    const auto    report = MeshVfxBatchExecutor{}.submit(queueWithTwoDraws(), sink);
    REQUIRE_EQ(static_cast<int>(report.status), static_cast<int>(MeshVfxQueueSubmitStatus::Complete));
    REQUIRE_EQ(report.acceptedBatches, 1u);
    REQUIRE_EQ(report.acceptedDraws, 2u);
    REQUIRE_EQ(sink.begins, 1u);
    REQUIRE_EQ(sink.ends, 1u);
    REQUIRE_EQ(sink.draws[0], 1u);
    REQUIRE_EQ(sink.draws[1], 2u);
}

TEST_CASE("stylize.mesh_vfx_submission skips unsupported batches atomically") {
    RecordingSink sink;
    sink.beginResult  = MeshVfxSubmitStatus::Skipped;
    const auto report = MeshVfxBatchExecutor{}.submit(queueWithTwoDraws(), sink);
    REQUIRE_EQ(static_cast<int>(report.status), static_cast<int>(MeshVfxQueueSubmitStatus::Partial));
    REQUIRE_EQ(report.skippedBatches, 1u);
    REQUIRE_EQ(report.skippedDraws, 2u);
    REQUIRE_EQ(sink.ends, 0u);
    REQUIRE(sink.draws.empty());
}

TEST_CASE("stylize.mesh_vfx_submission stops immediately when device is unavailable") {
    RecordingSink sink;
    sink.drawResult   = MeshVfxSubmitStatus::DeviceUnavailable;
    const auto report = MeshVfxBatchExecutor{}.submit(queueWithTwoDraws(), sink);
    REQUIRE_EQ(static_cast<int>(report.status), static_cast<int>(MeshVfxQueueSubmitStatus::DeviceUnavailable));
    REQUIRE_EQ(report.acceptedDraws, 0u);
    REQUIRE_EQ(sink.draws.size(), 1u);
    REQUIRE_EQ(sink.ends, 0u);
}
}  // namespace test_stylize_mesh_vfx_benchmark_unit
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
namespace test_stylize_unit {
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

TEST_CASE("stylize.styles.registry") {
    auto *mod = Stylize::create();
    REQUIRE(mod != nullptr);
    CHECK_EQ(mod->getName(), std::string("Stylize"));
    CHECK_EQ(mod->getStyleCount(), 18);
    CHECK(mod->hasStyle("cartoon"));
    CHECK(mod->hasStyle("watercolor"));
    CHECK(mod->hasStyle("ink"));
    CHECK(mod->hasStyle("pixel"));
    CHECK(mod->hasStyle("xray"));
    CHECK(mod->hasStyle("rim"));
    CHECK(mod->hasStyle("dissolve"));
    CHECK(mod->hasStyle("hologram"));
    CHECK(mod->hasStyle("snow"));
    CHECK(mod->hasStyle("vignette"));
    CHECK(mod->hasStyle("chromatic"));
    CHECK(mod->hasStyle("grain"));
    CHECK(!mod->hasStyle("oil"));
    CHECK(mod->hasMeshStyle("cartoon"));
    CHECK(mod->hasMeshStyle("ink"));
    CHECK(!mod->hasMeshStyle("watercolor"));
    CHECK(!mod->hasMeshStyle("pixel"));
    CHECK(mod->hasMeshStyle("xray"));
    CHECK(mod->hasMeshStyle("rim"));
    CHECK(mod->hasMeshStyle("dissolve"));
    CHECK(mod->hasMeshStyle("hologram"));
    CHECK(mod->hasMeshStyle("snow"));
    CHECK(!mod->hasMeshStyle("vignette"));
    CHECK(!mod->hasMeshStyle("chromatic"));
    CHECK(!mod->hasMeshStyle("grain"));

    CHECK(mod->supports("cartoon", "post"));
    CHECK(mod->supports("cartoon", "mesh"));
    CHECK(mod->supports("watercolor", "cpu"));
    CHECK(!mod->supports("watercolor", "mesh"));
    CHECK(!mod->supports("ink", "gbuffer"));  // current shader is color/Sobel only
    CHECK(mod->supports("xray", "depth"));
    CHECK(!mod->supports("xray", "post"));
    CHECK(!mod->supports("xray", "cpu"));
    CHECK(!mod->supports("oil", "post"));

    CHECK(mod->supports("rim", "mesh"));
    CHECK(!mod->supports("rim", "post"));
    CHECK(mod->supports("vignette", "post"));
    CHECK(!mod->supports("vignette", "mesh"));
    CHECK(!mod->supports("vignette", "cpu"));

    CHECK_GT(mod->getStyleParamCount("pixel"), 0);
    CHECK_EQ(mod->getStyleParamName("pixel", 0), std::string("pixelSize"));
    CHECK(mod->getStyleParamName("pixel", 999).empty());

    bool sawCartoon = false;
    for (int i = 0; i < mod->getStyleCount(); ++i) {
        if (mod->getStyleId(i) == "cartoon") sawCartoon = true;
    }
    CHECK(sawCartoon);
}

TEST_CASE("stylize.processImage.cpuAllStyles") {
    auto                      *mod = Stylize::create();
    std::unique_ptr<ImageData> src(makeGradient(32, 24));

    for (const char *style : {"cartoon", "watercolor", "ink", "pixel"}) {
        std::unique_ptr<ImageData> out(mod->processImage(src.get(), style));
        REQUIRE(out.get() != nullptr);
        CHECK_EQ(out->getWidth(), 32);
        CHECK_EQ(out->getHeight(), 24);
        CHECK_EQ(out->getFormat(), std::string("RGBA8"));
        Colorf p = out->getPixel(8, 8);
        CHECK(p.a > 0.9f);
        // Must differ from a flat identity copy somewhere in the image.
        bool changed = false;
        for (int y = 0; y < 24 && !changed; ++y) {
            for (int x = 0; x < 32 && !changed; ++x) {
                Colorf a = src->getPixel(x, y);
                Colorf b = out->getPixel(x, y);
                if (!near(a.r, b.r, 0.02f) || !near(a.g, b.g, 0.02f) || !near(a.b, b.b, 0.02f)) changed = true;
            }
        }
        CHECK(changed);
    }

    bool threw = false;
    try {
        mod->processImage(src.get(), "unknown");
    } catch (const eve::Exception &) {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE("stylize.instance.parameterSchema") {
    auto                          *mod = Stylize::create();
    std::unique_ptr<StyleInstance> pixel(mod->newInstance("pixel"));
    REQUIRE(pixel.get() != nullptr);
    CHECK_EQ(pixel->getStyle(), std::string("pixel"));
    CHECK_EQ(pixel->getStage(), std::string("afterTonemap"));
    CHECK(pixel->requiresInput("color"));
    CHECK(!pixel->requiresInput("depth"));
    CHECK(pixel->hasParam("pixelSize"));
    CHECK(!pixel->hasParam("texelW"));
    CHECK_EQ(pixel->getFloat("pixelSize"), 5.f);

    pixel->setFloat("pixelSize", 999.f);
    CHECK_EQ(pixel->getFloat("pixelSize"), pixel->getParamMax("pixelSize"));
    CHECK(pixel->isOverridden("pixelSize"));
    pixel->reset("pixelSize");
    CHECK(!pixel->isOverridden("pixelSize"));
    CHECK_EQ(pixel->getFloat("pixelSize"), pixel->getParamDefault("pixelSize"));
}

TEST_CASE("stylize.processImage.inkIsDesaturated") {
    auto                      *mod = Stylize::create();
    std::unique_ptr<ImageData> src(makeGradient(48, 32));
    std::unique_ptr<ImageData> ink(mod->processImage(src.get(), "ink"));
    REQUIRE(ink.get() != nullptr);
    // Ink wash should collapse toward gray/sepia paper — chroma reduced.
    int nearGray = 0;
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 48; ++x) {
            Colorf c  = ink->getPixel(x, y);
            float  mx = std::max(c.r, std::max(c.g, c.b));
            float  mn = std::min(c.r, std::min(c.g, c.b));
            if (mx - mn < 0.25f) ++nearGray;
        }
    }
    CHECK_GT(nearGray, 48 * 32 / 2);
}

TEST_CASE("stylize.processImage.pixelQuantizes") {
    auto                      *mod = Stylize::create();
    std::unique_ptr<ImageData> src(makeGradient(64, 32));
    std::unique_ptr<ImageData> pix(mod->processImage(src.get(), "pixel"));
    REQUIRE(pix.get() != nullptr);
    // Neighboring pixels inside a 4x4 block should match (UV snap).
    Colorf a = pix->getPixel(0, 0);
    Colorf b = pix->getPixel(1, 1);
    CHECK(near(a.r, b.r, 0.001f));
    CHECK(near(a.g, b.g, 0.001f));
    CHECK(near(a.b, b.b, 0.001f));
}
}  // namespace test_stylize_unit
#endif
