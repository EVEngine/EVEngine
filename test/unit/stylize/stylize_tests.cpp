#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_stylize_anime)
#include <cmath>
#include "PrivateTestScope.h"
#include "graphics/Shader.h"
#include "stylize/StyleShaders.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// stylize_anime.cpp
namespace test_stylize_anime_unit {
TEST_CASE("stylize.anime.spec.meshMaterialUniforms") {
    using namespace eve::stylize;
    REQUIRE(styleSupports("anime", "mesh"));
    REQUIRE(!styleSupports("anime", "post"));
    REQUIRE(!styleSupports("anime", "cpu"));
    eve::graphics::Shader shader;
    bindMeshUniforms(&shader, "anime");
    REQUIRE(shader.usedFloats() == styleParamCount("anime"));
    REQUIRE(shader.usedFloats() <= int(eve::graphics::Shader::kMaxFloats));
    for (int i = 0; i < styleParamCount("anime"); ++i) {
        const auto *param = styleParameterAt("anime", i);
        REQUIRE(param != nullptr);
        float value = 0.f;
        REQUIRE(shader.getFromVar(param->id, &value, sizeof(value)) == int(sizeof(value)));
        REQUIRE(value == param->defaultValue);
        REQUIRE(value >= param->minValue);
        REQUIRE(value <= param->maxValue);
        REQUIRE(std::isfinite(value));
    }
    auto *skin = findStyleParameter("anime", "skinDetail");
    REQUIRE(skin != nullptr);
    REQUIRE(skin->defaultValue == 1.f);
    REQUIRE(findStyleParameter("anime", "posterize") == nullptr);
}
}  // namespace test_stylize_anime_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_stylize_area_indicator_effect)
#include <glm/geometric.hpp>
#include "PrivateTestScope.h"
#include "stylize/AreaIndicatorEffect.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// stylize_area_indicator_effect.cpp
namespace test_stylize_area_indicator_effect {
using namespace eve::stylize;

TEST_CASE("stylize.areaIndicator.spec.shapeMeshesAndInvalidInput") {
    SUB_CASE("circle radius two with 16 segments makes a closed triangle fan") {
        AreaIndicatorConfig config;
        config.shape    = AreaIndicatorShape::Circle;
        config.radius   = 2.f;
        config.segments = 16;
        auto result     = buildAreaIndicator(config);
        REQUIRE(result.status == AreaIndicatorBuildStatus::Built);
        REQUIRE(result.mesh.vertices.size() == 18);
        REQUIRE(result.mesh.indices.size() == 48);
        REQUIRE(glm::length(result.mesh.vertices[1].position - result.mesh.vertices.back().position) < 1e-4f);
    };
    SUB_CASE("ring radius three and inner radius two makes paired strip vertices") {
        AreaIndicatorConfig config;
        config.shape       = AreaIndicatorShape::Ring;
        config.radius      = 3.f;
        config.innerRadius = 2.f;
        config.segments    = 12;
        auto result        = buildAreaIndicator(config);
        REQUIRE(result.status == AreaIndicatorBuildStatus::Built);
        REQUIRE(result.mesh.vertices.size() == 26);
        REQUIRE(result.mesh.indices.size() == 72);
        REQUIRE(glm::length(result.mesh.vertices[0].position) > 1.99f);
        REQUIRE(glm::length(result.mesh.vertices[1].position) > 2.99f);
    };
    SUB_CASE("sector and rectangle emit expected topology and bounds") {
        AreaIndicatorConfig sector;
        sector.shape              = AreaIndicatorShape::Sector;
        sector.radius             = 4.f;
        sector.sectorAngleRadians = 1.57079632679f;
        sector.segments           = 8;
        auto cone                 = buildAreaIndicator(sector);
        REQUIRE(cone.status == AreaIndicatorBuildStatus::Built);
        REQUIRE(cone.mesh.indices.size() == 24);
        AreaIndicatorConfig rectangle;
        rectangle.shape  = AreaIndicatorShape::Rectangle;
        rectangle.width  = 2.f;
        rectangle.length = 5.f;
        auto box         = buildAreaIndicator(rectangle);
        REQUIRE(box.status == AreaIndicatorBuildStatus::Built);
        REQUIRE(box.mesh.vertices.size() == 4);
        REQUIRE(box.mesh.indices.size() == 6);
        REQUIRE(box.mesh.vertices[0].position.x == -1.f);
        REQUIRE(box.mesh.vertices[3].position.z == 5.f);
    };
    SUB_CASE("ring with equal inner and outer radius rejects without geometry") {
        AreaIndicatorConfig config;
        config.shape       = AreaIndicatorShape::Ring;
        config.radius      = 1.f;
        config.innerRadius = 1.f;
        auto result        = buildAreaIndicator(config);
        REQUIRE(result.status == AreaIndicatorBuildStatus::InvalidConfig);
        REQUIRE(result.mesh.vertices.empty());
    };
}
}  // namespace test_stylize_area_indicator_effect
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_stylize_attack_vfx)
#include <string_view>
#include "PrivateTestScope.h"
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

TEST_CASE("stylize.attackVfx.recipe.spec.schemaAndReload") {
    SUB_CASE("valid layered JSON -> typed phases, optional URI, and canonical roundtrip") {
        auto parsed = AttackVfxRecipe::fromJson(kWaterWhipRecipe);
        REQUIRE(parsed.ok());
        const auto &recipe = parsed.value();
        REQUIRE_EQ(recipe.id.format(), "attackvfx:water_whip");
        REQUIRE_EQ(recipe.phases.size(), 3u);
        REQUIRE_EQ(recipe.phases[0].layers.size(), 2u);
        REQUIRE_EQ(recipe.phases[1].startCue, "impact");
        REQUIRE(recipe.phases[1].layers[1].uri.empty());
        REQUIRE(recipe.skin.has_value());
        REQUIRE_EQ(recipe.skin->id.format(), "skin:water");
        auto encoded = recipe.toJson();
        REQUIRE(encoded.ok());
        auto restored = AttackVfxRecipe::fromJson(encoded.value());
        REQUIRE(restored.ok());
        REQUIRE_EQ(restored.value().phases[1].layers.size(), 3u);
    };
    SUB_CASE("unknown field, negative duration, missing URI, oversized budget -> error") {
        REQUIRE(
            !AttackVfxRecipe::fromJson(
                 R"({"schema":"eve.stylize.attack-vfx","schemaVersion":1,"id":"attackvfx:x","phases":[{"kind":"release","typo":1}]})")
                 .ok());
        REQUIRE(
            !AttackVfxRecipe::fromJson(
                 R"({"schema":"eve.stylize.attack-vfx","schemaVersion":1,"id":"attackvfx:x","phases":[{"kind":"release","durationSeconds":-1,"layers":[{"role":"particles","uri":"p://a"}]}]})")
                 .ok());
        REQUIRE(
            !AttackVfxRecipe::fromJson(
                 R"({"schema":"eve.stylize.attack-vfx","schemaVersion":1,"id":"attackvfx:x","phases":[{"kind":"release","layers":[{"role":"particles"}]}]})")
                 .ok());
        REQUIRE(
            !AttackVfxRecipe::fromJson(
                 R"({"schema":"eve.stylize.attack-vfx","schemaVersion":1,"id":"attackvfx:x","budget":{"maxParticles":5000000000},"phases":[{"kind":"release","layers":[{"role":"particles","uri":"p://a"}]}]})")
                 .ok());
    };
    SUB_CASE("slot invalid reload -> original revision; valid reload -> replacement") {
        auto parsed = AttackVfxRecipe::fromJson(kWaterWhipRecipe);
        REQUIRE(parsed.ok());
        AttackVfxRecipeSlot slot(std::move(parsed).takeValue());
        REQUIRE(!slot.reload(R"({"schemaVersion":99})").ok());
        REQUIRE_EQ(slot.revision(), 1u);
        REQUIRE_EQ(slot.recipe().id.format(), "attackvfx:water_whip");
        auto replaced = slot.reload(
            R"({"schema":"eve.stylize.attack-vfx","schemaVersion":1,"id":"attackvfx:fire_burst","phases":[{"kind":"release","layers":[{"role":"particles","uri":"particles://fire/burst"}]}]})");
        REQUIRE(replaced.ok());
        REQUIRE_EQ(replaced.value(), 2u);
        REQUIRE_EQ(slot.recipe().id.format(), "attackvfx:fire_burst");
    };
}

TEST_CASE("stylize.attackVfx.runtime.spec.cuesAndLifetime") {
    SUB_CASE("release -> impact cue -> two active phases -> completion and stop event") {
        auto recipe = AttackVfxRecipe::fromJson(kWaterWhipRecipe);
        REQUIRE(recipe.ok());
        AttackVfxRuntime runtime;
        REQUIRE(runtime.configurePool(4).ok());
        REQUIRE(runtime.registerRecipe(recipe.value()).ok());
        AttackVfxRequest request;
        request.sourceId = 1;
        request.targetId = 2;
        auto played      = runtime.play(id("attackvfx:water_whip"), request);
        REQUIRE(played.ok());
        auto handle = played.value();
        auto snap   = runtime.inspect(handle);
        REQUIRE(snap.has_value());
        REQUIRE_EQ(snap->activeSkinId->format(), "skin:water");
        REQUIRE(snap->phases[0].active);
        REQUIRE(!snap->phases[1].active);
        auto cued = runtime.signal(handle, "impact");
        REQUIRE(cued.ok());
        bool impact = false, status = false, consumed = false;
        for (const auto &event : cued.value().events) {
            impact |= event.kind == AttackVfxFrameEvent::Kind::PhaseEnter && event.phase == AttackVfxPhaseKind::Impact;
            status |= event.kind == AttackVfxFrameEvent::Kind::PhaseEnter && event.phase == AttackVfxPhaseKind::Status;
            consumed |= event.kind == AttackVfxFrameEvent::Kind::CueConsumed;
        }
        REQUIRE(impact);
        REQUIRE(status);
        REQUIRE(consumed);
        REQUIRE(runtime.advance(0.5).ok());
        snap = runtime.inspect(handle);
        REQUIRE(snap.has_value());
        REQUIRE(snap->phases[0].completed);
        REQUIRE(snap->phases[1].completed);
        REQUIRE(snap->phases[2].active);
        auto final = runtime.advance(1.0);
        REQUIRE(final.ok());
        REQUIRE_EQ(runtime.activeCount(), 0u);
        bool stopped = false;
        for (const auto &event : final.value().events)
            stopped |= event.kind == AttackVfxFrameEvent::Kind::InstanceStopped;
        REQUIRE(stopped);
    };
    SUB_CASE("skin override and cancel cue -> selected skin and no active instance") {
        auto recipe = AttackVfxRecipe::fromJson(
            R"({"schema":"eve.stylize.attack-vfx","schemaVersion":1,"id":"attackvfx:sweep","phases":[{"kind":"release","durationSeconds":2,"layers":[{"role":"trail","uri":"trail://blade"}]}]})");
        auto skin = AttackVfxSkin::fromJson(
            R"({"schema":"eve.stylize.attack-vfx-skin","schemaVersion":1,"id":"skin:fire","palette":{"primary":[1,0.3,0.1],"secondary":[1,0.6,0.2],"emissive":[1,0.8,0.2]}})");
        REQUIRE(recipe.ok());
        REQUIRE(skin.ok());
        AttackVfxRuntime runtime;
        REQUIRE(runtime.registerRecipe(recipe.value()).ok());
        REQUIRE(runtime.registerSkin(skin.value()).ok());
        AttackVfxRequest request;
        request.skinOverride = id("skin:fire");
        auto played          = runtime.play(id("attackvfx:sweep"), request);
        REQUIRE(played.ok());
        auto snap = runtime.inspect(played.value());
        REQUIRE(snap.has_value());
        REQUIRE_EQ(snap->activeSkinId->format(), "skin:fire");
        REQUIRE(runtime.signal(played.value(), "cancel").ok());
        REQUIRE_EQ(runtime.activeCount(), 0u);
    };
    SUB_CASE("unknown recipe and stale handle -> errors") {
        AttackVfxRuntime runtime;
        REQUIRE(!runtime.play(id("attackvfx:missing"), {}).ok());
        auto recipe = AttackVfxRecipe::fromJson(
            R"({"schema":"eve.stylize.attack-vfx","schemaVersion":1,"id":"attackvfx:one_shot","phases":[{"kind":"release","durationSeconds":0.1,"layers":[{"role":"audio","uri":"audio://whoosh"}]}]})");
        REQUIRE(recipe.ok());
        REQUIRE(runtime.registerRecipe(recipe.value()).ok());
        auto played = runtime.play(id("attackvfx:one_shot"), {});
        REQUIRE(played.ok());
        auto handle = played.value();
        REQUIRE(runtime.stop(handle, AttackVfxStopMode::ClearImmediately).ok());
        REQUIRE(!runtime.signal(handle, "impact").ok());
    };
    SUB_CASE("pool resize -> prior generation stays invalid") {
        AttackVfxRuntime runtime;
        REQUIRE(runtime.configurePool(2).ok());
        auto recipe = AttackVfxRecipe::fromJson(
            R"({"schema":"eve.stylize.attack-vfx","schemaVersion":1,"id":"attackvfx:gen","phases":[{"kind":"release","durationSeconds":0.05,"layers":[{"role":"trail","uri":"trail://x"}]}]})");
        REQUIRE(recipe.ok());
        REQUIRE(runtime.registerRecipe(recipe.value()).ok());
        auto played = runtime.play(id("attackvfx:gen"), {});
        REQUIRE(played.ok());
        auto old = played.value();
        REQUIRE(runtime.stop(old, AttackVfxStopMode::ClearImmediately).ok());
        REQUIRE(runtime.configurePool(4).ok());
        auto next = runtime.play(id("attackvfx:gen"), {});
        REQUIRE(next.ok());
        REQUIRE(next.value().generation != old.generation);
        REQUIRE(!runtime.inspect(old).has_value());
        REQUIRE(runtime.stop(next.value(), AttackVfxStopMode::ClearImmediately).ok());
    };
    SUB_CASE("live recipe replacement -> error; delayed phase -> partial elapsed time") {
        AttackVfxRuntime runtime;
        auto             recipe = AttackVfxRecipe::fromJson(
            R"({"schema":"eve.stylize.attack-vfx","schemaVersion":1,"id":"attackvfx:delay","phases":[{"kind":"release","startOffsetSeconds":0.08,"durationSeconds":0.1,"layers":[{"role":"trail","uri":"trail://x"}]}]})");
        REQUIRE(recipe.ok());
        REQUIRE(runtime.registerRecipe(recipe.value()).ok());
        auto played = runtime.play(id("attackvfx:delay"), {});
        REQUIRE(played.ok());
        REQUIRE(!runtime.registerRecipe(recipe.value()).ok());
        REQUIRE(runtime.advance(0.1).ok());
        auto snap = runtime.inspect(played.value());
        REQUIRE(snap.has_value());
        REQUIRE(snap->phases[0].active);
        REQUIRE(snap->phases[0].localTime < 0.1);
        REQUIRE(snap->phases[0].localTime + 1e-9 >= 0.02);
    };
}
}  // namespace test_stylize_attack_vfx
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_stylize_attack_vfx_layers)
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include <string>
#include "PrivateTestScope.h"
#include "common/Exception.h"
#include "graphics/Shader.h"
#include "stylize/MotionEchoEffect.h"
#include "stylize/StyleInstance.h"
#include "stylize/StyleShaders.h"
#include "stylize/Stylize.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
namespace test_stylize_attack_vfx_layers_unit {
using namespace eve::stylize;
namespace {
const char *kMeshEffects[] = {"rim", "dissolve", "hologram", "snow", "slash", "ember", "aura"};
const char *kPostEffects[] = {"vignette", "chromatic", "grain"};
}  // namespace

TEST_CASE("stylize.layerEffects.spec.registryAndParameters") {
    SUB_CASE("mesh and post effect IDs -> distinct stages and support") {
        auto *mod = Stylize::create();
        REQUIRE(mod != nullptr);
        for (const char *name : kMeshEffects) {
            REQUIRE(mod->hasStyle(name));
            REQUIRE(mod->hasMeshStyle(name));
            REQUIRE(mod->supports(name, "mesh"));
            REQUIRE(!mod->supports(name, "post"));
        }
        for (const char *name : kPostEffects) {
            REQUIRE(mod->hasStyle(name));
            REQUIRE(!mod->hasMeshStyle(name));
            REQUIRE(mod->supports(name, "post"));
            REQUIRE(!mod->supports(name, "mesh"));
        }
        REQUIRE(!mod->hasStyle("bloom"));
    };
    SUB_CASE("dissolve amount outside range -> clamped; reset -> default") {
        auto                          *mod = Stylize::create();
        std::unique_ptr<StyleInstance> effect(mod->newInstance("dissolve"));
        REQUIRE(effect != nullptr);
        REQUIRE(effect->hasParam("amount"));
        REQUIRE(!effect->hasParam("nope"));
        effect->setFloat("amount", 5.f);
        REQUIRE_EQ(effect->getFloat("amount"), effect->getParamMax("amount"));
        effect->setFloat("amount", -3.f);
        REQUIRE_EQ(effect->getFloat("amount"), effect->getParamMin("amount"));
        REQUIRE(effect->isOverridden("amount"));
        effect->resetAll();
        REQUIRE(!effect->isOverridden("amount"));
        REQUIRE_EQ(effect->getFloat("amount"), effect->getParamDefault("amount"));
    };
    SUB_CASE("post instance -> color input; unknown style -> exception") {
        auto                          *mod = Stylize::create();
        std::unique_ptr<StyleInstance> effect(mod->newInstance("vignette"));
        REQUIRE(effect != nullptr);
        REQUIRE_EQ(effect->getStage(), std::string("afterTonemap"));
        REQUIRE(effect->requiresInput("color"));
        REQUIRE(!effect->requiresInput("depth"));
        bool threw = false;
        try {
            mod->newInstance("oil");
        } catch (const eve::Exception &) {
            threw = true;
        }
        REQUIRE(threw);
    };
}

TEST_CASE("stylize.motionEcho.spec.samplingAndExpiration") {
    SUB_CASE("moving projectile at 3.5 units/0.35s -> three interpolated samples and ribbon") {
        ProjectileTrailConfig config;
        config.sampleIntervalSeconds = 0.1f;
        config.sampleLifetimeSeconds = 1.f;
        config.minimumDistance       = 0.f;
        ProjectileTrailEffect trail(config);
        const auto            first = trail.update(0.f, {0.f, 0.f, 0.f});
        REQUIRE_EQ(first.captured, 1u);
        const auto moved = trail.update(0.35f, {3.5f, 0.f, 0.f});
        REQUIRE_EQ(moved.captured, 3u);
        REQUIRE_EQ(trail.size(), 4u);
        const auto mesh = trail.build({0.f, 0.f, -1.f});
        REQUIRE_EQ(mesh.vertices.size(), 8u);
        REQUIRE_EQ(mesh.indices.size(), 18u);
    };
    SUB_CASE("stationary samples below distance threshold -> rejected without duplicate") {
        ProjectileTrailConfig config;
        config.sampleIntervalSeconds = 0.05f;
        config.minimumDistance       = 0.1f;
        ProjectileTrailEffect trail(config);
        const auto            first = trail.update(0.f, {1.f, 2.f, 3.f});
        REQUIRE_EQ(first.captured, 1u);
        auto report = trail.update(0.2f, {1.f, 2.f, 3.f});
        REQUIRE_EQ(report.captured, 0u);
        REQUIRE_EQ(report.distanceRejected, 4u);
        REQUIRE_EQ(trail.size(), 1u);
    };
    SUB_CASE("afterimage expiry -> stable IDs advance without reuse") {
        AfterimageEffectConfig config;
        config.sampleIntervalSeconds = 0.1f;
        config.lifetimeSeconds       = 0.3f;
        config.maximumImages         = 4;
        AfterimageEffect effect(config);
        auto             first   = glm::translate(glm::mat4(1.f), {1.f, 0.f, 0.f});
        const auto       initial = effect.update(0.25f, first);
        REQUIRE_EQ(initial.captured, 2u);
        auto images = effect.snapshot();
        REQUIRE_EQ(images.size(), 2u);
        REQUIRE_EQ(images[0].stableId, 1u);
        REQUIRE_EQ(images[1].stableId, 2u);
        auto       second  = glm::translate(glm::mat4(1.f), {2.f, 0.f, 0.f});
        const auto expired = effect.update(0.2f, second);
        REQUIRE_EQ(expired.expired, 1u);
        images = effect.snapshot();
        REQUIRE_EQ(images.size(), 3u);
        REQUIRE_EQ(images[0].stableId, 2u);
        REQUIRE_EQ(images[2].stableId, 4u);
    };
}

TEST_CASE("stylize.ysa.spec.independentMeshUniformSchema") {
    REQUIRE(styleSupports("ysa", "mesh"));
    REQUIRE(!styleSupports("ysa", "post"));
    REQUIRE(!styleSupports("ysa", "cpu"));
    eve::graphics::Shader shader;
    bindMeshUniforms(&shader, "ysa");
    REQUIRE_EQ(shader.usedFloats(), styleParamCount("ysa"));
    REQUIRE(shader.usedFloats() <= int(eve::graphics::Shader::kMaxFloats));
    for (int index = 0; index < styleParamCount("ysa"); ++index) {
        const auto *param = styleParameterAt("ysa", index);
        REQUIRE(param != nullptr);
        float value = 0.f;
        REQUIRE_EQ(shader.getFromVar(param->id, &value, sizeof(value)), int(sizeof(value)));
        REQUIRE_EQ(value, param->defaultValue);
        REQUIRE(std::isfinite(value));
        REQUIRE(value >= param->minValue);
        REQUIRE(value <= param->maxValue);
    }
    REQUIRE(findStyleParameter("ysa", "bands") == nullptr);
    REQUIRE(findStyleParameter("cartoon", "lightMidpoint") == nullptr);
}
}  // namespace test_stylize_attack_vfx_layers_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_stylize_beam_effect)
#include <array>
#include <glm/geometric.hpp>
#include "PrivateTestScope.h"
#include "stylize/BeamEffect.h"
#include "stylize/BeamSkillEffect.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// stylize_beam_effect.cpp
namespace test_stylize_beam_effect {
// stylize_beam_effect.cpp
using namespace eve::stylize;

TEST_CASE("stylize.beamGeometry.spec.inputToMesh") {
    SUB_CASE("eight straight segments -> paired vertices and six indices per segment") {
        BeamEffectConfig config;
        config.segments       = 8;
        config.noiseAmplitude = 0.f;
        auto result           = buildBeamEffect({0.f, 0.f, 0.f}, {0.f, 0.f, 4.f}, {0.f, -1.f, 0.f}, config);
        REQUIRE(static_cast<int>(result.status) == static_cast<int>(BeamBuildStatus::Built));
        REQUIRE_EQ(result.mesh.vertices.size(), 18u);
        REQUIRE_EQ(result.mesh.indices.size(), 48u);
        REQUIRE(glm::length(result.mesh.vertices[0].position - result.mesh.vertices[1].position) > 0.11f);
    };
    SUB_CASE("same seed and branched path -> identical vertex positions and alpha") {
        BeamEffectConfig config;
        config.segments       = 10;
        config.noiseAmplitude = 0.2f;
        config.randomSeed     = 91;
        config.branchCount    = 3;
        config.branchSegments = 4;
        auto first            = buildBeamEffect({1.f, 2.f, 3.f}, {5.f, 4.f, 9.f}, {0.f, 0.f, -1.f}, config);
        auto second           = buildBeamEffect({1.f, 2.f, 3.f}, {5.f, 4.f, 9.f}, {0.f, 0.f, -1.f}, config);
        REQUIRE_EQ(first.mesh.vertices.size(), 52u);
        REQUIRE_EQ(first.mesh.indices.size(), 132u);
        REQUIRE_EQ(first.mesh.vertices.size(), second.mesh.vertices.size());
        for (std::size_t index = 0; index < first.mesh.vertices.size(); ++index) {
            REQUIRE_EQ(first.mesh.vertices[index].position, second.mesh.vertices[index].position);
            REQUIRE_EQ(first.mesh.vertices[index].alpha, second.mesh.vertices[index].alpha);
        }
    };
    SUB_CASE("zero-length path or negative width -> empty mesh and explicit status") {
        auto zero = buildBeamEffect({1.f, 1.f, 1.f}, {1.f, 1.f, 1.f}, {0.f, 0.f, -1.f});
        REQUIRE(static_cast<int>(zero.status) == static_cast<int>(BeamBuildStatus::DegenerateInput));
        REQUIRE(zero.mesh.vertices.empty());
        BeamEffectConfig invalid;
        invalid.widthStart = -1.f;
        auto rejected      = buildBeamEffect({0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 0.f, -1.f}, invalid);
        REQUIRE(static_cast<int>(rejected.status) == static_cast<int>(BeamBuildStatus::InvalidConfig));
    };
}

TEST_CASE("stylize.beamSkill.spec.presetsPathAndPlayback") {
    SUB_CASE("laser and lightning presets -> distinct noise and loop defaults") {
        BeamSkillEffect laser(BeamSkillPreset::Laser);
        BeamSkillEffect lightning(BeamSkillPreset::Lightning);
        REQUIRE_EQ(laser.geometry().noiseAmplitude, 0.f);
        REQUIRE(lightning.geometry().noiseAmplitude > 0.f);
        REQUIRE(lightning.geometry().branchCount > 0u);
        REQUIRE(laser.effect().playback().loop);
        REQUIRE(!lightning.effect().playback().loop);
    };
    SUB_CASE("chain path -> valid rebased mesh indices") {
        BeamSkillEffect  chain(BeamSkillPreset::ChainLightning);
        const std::array points{glm::vec3(0.f, 0.f, 0.f), glm::vec3(1.f, 1.f, 0.f), glm::vec3(2.f, 0.f, 1.f),
                                glm::vec3(3.f, 1.f, 1.f)};
        chain.setPath(points);
        auto built = chain.build({0.f, 0.f, -1.f});
        REQUIRE(static_cast<int>(built.status) == static_cast<int>(BeamBuildStatus::Built));
        REQUIRE(!built.mesh.vertices.empty());
        REQUIRE(!built.mesh.indices.empty());
        for (auto index : built.mesh.indices) REQUIRE(index < built.mesh.vertices.size());
    };
    SUB_CASE("play then immediate stop -> finished state; no path -> degenerate") {
        BeamSkillEffect lightning(BeamSkillPreset::Lightning);
        REQUIRE(static_cast<int>(lightning.effect().state()) == static_cast<int>(MeshEffectState::Stopped));
        lightning.play();
        lightning.update(0.05f);
        REQUIRE(lightning.effect().intensity() > 0.f);
        lightning.stop(0.f);
        REQUIRE(static_cast<int>(lightning.effect().state()) == static_cast<int>(MeshEffectState::Finished));
        BeamSkillEffect laser(BeamSkillPreset::Laser);
        REQUIRE(static_cast<int>(laser.build({0.f, 0.f, -1.f}).status) ==
                static_cast<int>(BeamBuildStatus::DegenerateInput));
    };
}
}  // namespace test_stylize_beam_effect
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_stylize_mesh_particles)
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>
#include "PrivateTestScope.h"
#include "common/Exception.h"
#include "stylize/MeshEffect.h"
#include "stylize/MeshParticleEmitter.h"
#include "stylize/MeshVfxAsset.h"
#include "stylize/MeshVfxRenderBatch.h"
#include "stylize/MeshVfxScalability.h"
#include "stylize/SkillMeshEffect.h"
#include "stylize/Stylize.h"
#include "stylize/TrailEffect.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// stylize_mesh_particles.cpp
namespace test_stylize_mesh_particles_unit {
// stylize_mesh_particles.cpp
using namespace eve::stylize;

TEST_CASE("stylize.meshParticles.spec.fixedStepAndCapacity") {
    SUB_CASE("0.3s once versus 0.1s + 0.2s -> identical seeded burst particles") {
        MeshParticleEmitterConfig config;
        config.capacity     = 32;
        config.emissionRate = 10.f;
        config.lifetimeMin = config.lifetimeMax = 2.f;
        config.speedMin = config.speedMax = 1.f;
        config.fixedStepSeconds           = 0.1f;
        config.maximumSubsteps            = 8;
        config.randomSeed                 = 42;
        config.bursts                     = {{0.2f, 3}};
        MeshParticleEmitter once(config), split(config);
        once.start();
        split.start();
        const auto onceReport = once.advance(0.3f);
        REQUIRE_EQ(onceReport.alive, 6u);
        const auto initialReport = split.advance(0.1f);
        const auto finalReport   = split.advance(0.2f);
        REQUIRE(initialReport.simulatedSteps > 0u);
        REQUIRE_EQ(finalReport.alive, 6u);
        const auto a = once.snapshot(), b = split.snapshot();
        REQUIRE_EQ(a.size(), b.size());
        for (std::size_t i = 0; i < a.size(); ++i) {
            REQUIRE_EQ(a[i].stableId, b[i].stableId);
            REQUIRE_EQ(a[i].model, b[i].model);
            REQUIRE_EQ(a[i].color, b[i].color);
        }
    };
    SUB_CASE("burst exceeds capacity -> accepted four, dropped three") {
        MeshParticleEmitterConfig config;
        config.capacity         = 4;
        config.fixedStepSeconds = 0.1f;
        config.bursts           = {{0.f, 7}};
        MeshParticleEmitter emitter(config);
        emitter.start();
        auto report = emitter.advance(0.1f);
        REQUIRE_EQ(report.spawned, 4u);
        REQUIRE_EQ(report.dropped, 3u);
        REQUIRE_EQ(report.alive, 4u);
    };
    SUB_CASE("frame exceeds maximum two substeps -> discarded time reported") {
        MeshParticleEmitterConfig config;
        config.fixedStepSeconds = 0.1f;
        config.maximumSubsteps  = 2;
        MeshParticleEmitter emitter(config);
        emitter.start();
        auto report = emitter.advance(0.55f);
        REQUIRE_EQ(report.simulatedSteps, 2u);
        REQUIRE(report.discardedTimeSeconds > 0.29f);
        REQUIRE(report.discardedTimeSeconds < 0.31f);
    };
}

TEST_CASE("stylize.meshParticles.spec.curvesShapesAndRenderBridge") {
    SUB_CASE("half-life -> interpolated red/blue color and unit scale") {
        MeshParticleEmitterConfig config;
        config.capacity    = 1;
        config.lifetimeMin = config.lifetimeMax = 1.f;
        config.fixedStepSeconds                 = 0.25f;
        config.rotationMinRadians = config.rotationMaxRadians = 0.f;
        config.angularVelocityMin = config.angularVelocityMax = 2.f;
        config.scaleCurve                                     = {{1.f, 0.f}, {0.f, 2.f}, {0.5f, 1.f}};
        config.colorGradient = {{0.f, {1.f, 0.f, 0.f, 1.f}}, {1.f, {0.f, 0.f, 1.f, 0.f}}};
        MeshParticleEmitter emitter(config);
        const auto          emitted = emitter.emit(1);
        REQUIRE_EQ(emitted, 1u);
        const auto advanced = emitter.advance(0.5f);
        REQUIRE_EQ(advanced.simulatedSteps, 2u);
        auto particles = emitter.snapshot();
        REQUIRE_EQ(particles.size(), 1u);
        REQUIRE(std::abs(particles[0].color.r - 0.5f) < 0.01f);
        REQUIRE(std::abs(particles[0].color.b - 0.5f) < 0.01f);
        REQUIRE(std::abs(glm::length(glm::vec3(particles[0].model[0])) - 1.f) < 0.01f);
    };
    SUB_CASE("sphere and box emitters -> all positions within authored bounds") {
        MeshParticleEmitterConfig sphere;
        sphere.capacity     = 64;
        sphere.shape        = MeshParticleShape::Sphere;
        sphere.sphereRadius = 3.f;
        sphere.randomSeed   = 7;
        MeshParticleEmitter sphereEmitter(sphere);
        const auto          sphereCount = sphereEmitter.emit(64);
        REQUIRE_EQ(sphereCount, 64u);
        for (const auto &particle : sphereEmitter.snapshot())
            REQUIRE(glm::length(glm::vec3(particle.model[3])) <= 3.0001f);
        MeshParticleEmitterConfig box;
        box.capacity   = 64;
        box.shape      = MeshParticleShape::Box;
        box.boxExtents = {1.f, 2.f, 3.f};
        box.randomSeed = 9;
        MeshParticleEmitter boxEmitter(box);
        const auto          boxCount = boxEmitter.emit(64);
        REQUIRE_EQ(boxCount, 64u);
        for (const auto &particle : boxEmitter.snapshot()) {
            const glm::vec3 position(particle.model[3]);
            REQUIRE(std::abs(position.x) <= 1.0001f);
            REQUIRE(std::abs(position.y) <= 2.0001f);
            REQUIRE(std::abs(position.z) <= 3.0001f);
        }
    };
    SUB_CASE("three particles -> three aligned LOD, render, and command inputs") {
        MeshParticleEmitterConfig config;
        config.capacity         = 3;
        config.fixedStepSeconds = 0.1f;
        MeshParticleEmitter emitter(config);
        emitter.setOrigin({0.f, 0.f, 5.f});
        const auto emitted = emitter.emit(3);
        REQUIRE_EQ(emitted, 3u);
        MeshEffectInstance    effect("slash");
        const MeshVfxBatchKey key{11, 12, 13, 0, MeshVfxBatchBlend::Alpha};
        auto                  inputs =
            buildMeshParticleRenderInputs(emitter.snapshot(), &effect, reinterpret_cast<eve::graphics::Mesh *>(1),
                                          nullptr, key, 77, glm::vec3(0.f), 100.f, 50.f, 2);
        REQUIRE_EQ(inputs.lodCandidates.size(), 3u);
        REQUIRE_EQ(inputs.renderItems.size(), 3u);
        REQUIRE_EQ(inputs.commands.size(), 3u);
        REQUIRE_EQ(inputs.commands[0].stableInstanceId, inputs.renderItems[0].stableInstanceId);
        MeshVfxScalabilityPolicy policy;
        policy.maximumDistance = 50.f;
        policy.workUnitBudget  = 12;
        auto queue             = MeshVfxRenderBatchPlanner{}.build(inputs.renderItems,
                                                                   MeshVfxScalabilityPlanner(policy).plan(inputs.lodCandidates), 0);
        REQUIRE_EQ(queue.stats.queuedDraws, 3u);
        REQUIRE_EQ(queue.batches.size(), 1u);
    };
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

TEST_CASE("stylize.meshEffect.spec.playbackAndTarget") {
    SUB_CASE("bind, play, advance, stop -> target and intensity lifecycle") {
        auto effect = Stylize::create()->createMeshEffect("ember");
        REQUIRE(effect != nullptr);
        REQUIRE(!effect->isBound());
        effect->bindTarget(MeshEffectTargetHandle(7, 2));
        REQUIRE(effect->isBound());
        REQUIRE_EQ(effect->target().index(), 7u);
        REQUIRE_EQ(effect->target().generation(), 2u);
        effect->setPlayback(MeshEffectPlayback{0.1f, 0.2f, 0.1f, false});
        effect->play();
        REQUIRE(static_cast<int>(effect->state()) == static_cast<int>(MeshEffectState::FadingIn));
        effect->update(0.05f);
        REQUIRE(std::abs(effect->intensity() - 0.5f) < 0.001f);
        effect->update(0.10f);
        REQUIRE(static_cast<int>(effect->state()) == static_cast<int>(MeshEffectState::Active));
        REQUIRE(std::abs(effect->style().getFloat("time") - 0.15f) < 0.001f);
        effect->stop(0.1f);
        effect->update(0.1f);
        REQUIRE(static_cast<int>(effect->state()) == static_cast<int>(MeshEffectState::Finished));
        effect->unbindTarget();
        REQUIRE(!effect->isBound());
    };
    SUB_CASE("skill presets -> slash owns trail, impact has no trail") {
        auto *module = Stylize::create();
        auto  slash  = module->createSkillMeshEffect(SkillMeshEffectKind::WeaponSlash);
        REQUIRE(slash != nullptr);
        REQUIRE(slash->hasTrail());
        REQUIRE_EQ(slash->effect().style().getStyle(), "slash");
        slash->bindTarget(MeshEffectTargetHandle(4, 9));
        slash->play();
        REQUIRE(static_cast<int>(slash->appendBlade({0.f, 0.f, 0.f}, {0.f, 1.f, 0.f})) ==
                static_cast<int>(TrailAppendResult::StartedSegment));
        slash->update(0.03f);
        REQUIRE(slash->effect().intensity() > 0.f);
        REQUIRE_EQ(slash->trail().sampleCount(), 1u);
        auto impact = module->createSkillMeshEffect(SkillMeshEffectKind::ImpactFlash);
        REQUIRE(impact != nullptr);
        REQUIRE(!impact->hasTrail());
        REQUIRE_EQ(impact->effect().style().getStyle(), "rim");
        impact->play();
        impact->update(0.2f);
        REQUIRE(static_cast<int>(impact->effect().state()) == static_cast<int>(MeshEffectState::Finished));
        bool rejected = false;
        try {
            (void)impact->appendBlade({0.f, 0.f, 0.f}, {0.f, 1.f, 0.f});
        } catch (const eve::Exception &) {
            rejected = true;
        }
        REQUIRE(rejected);
    };
    SUB_CASE("post style as mesh effect -> exception") {
        bool rejected = false;
        try {
            (void)Stylize::create()->createMeshEffect("vignette");
        } catch (const eve::Exception &) {
            rejected = true;
        }
        REQUIRE(rejected);
    };
}

TEST_CASE("stylize.trail.spec.samplesDiscontinuityAndUpload") {
    SUB_CASE("two samples -> quad and upload; teleport -> new disconnected segment") {
        TrailEmitter trail(TrailSettings{8, 1.f, 0.01f, 1.f});
        REQUIRE(static_cast<int>(trail.append({0.f, 0.f, 0.f}, {0.f, 1.f, 0.f})) ==
                static_cast<int>(TrailAppendResult::StartedSegment));
        REQUIRE(static_cast<int>(trail.append({0.5f, 0.f, 0.f}, {0.5f, 1.f, 0.f})) ==
                static_cast<int>(TrailAppendResult::Added));
        auto mesh = trail.buildMesh();
        REQUIRE_EQ(mesh.vertices.size(), 4u);
        REQUIRE_EQ(mesh.indices.size(), 6u);
        REQUIRE_EQ(mesh.vertices[0].uv.x, 0.f);
        REQUIRE_EQ(mesh.vertices[2].uv.x, 1.f);
        auto upload = prepareTrailUpload(mesh);
        REQUIRE(upload.ok());
        REQUIRE_EQ(upload.value().positions.size(), 12u);
        REQUIRE_EQ(upload.value().normals.size(), 12u);
        REQUIRE_EQ(upload.value().uvs.size(), 8u);
        REQUIRE_EQ(upload.value().indices.size(), 6u);
        REQUIRE(static_cast<int>(trail.append({4.f, 0.f, 0.f}, {4.f, 1.f, 0.f})) ==
                static_cast<int>(TrailAppendResult::StartedSegment));
        REQUIRE_EQ(trail.buildMesh().indices.size(), 6u);
        trail.update(1.f);
        REQUIRE_EQ(trail.sampleCount(), 0u);
    };
    SUB_CASE("capacity one -> invalid trail configuration") {
        bool rejected = false;
        try {
            TrailEmitter invalid(TrailSettings{1, 1.f, 0.f, 1.f});
        } catch (const eve::Exception &) {
            rejected = true;
        }
        REQUIRE(rejected);
    };
}

// stylize_mesh_vfx_asset.cpp
using namespace eve::stylize;

TEST_CASE("stylize.meshAsset.spec.schemaMigrationAndReload") {
    SUB_CASE("version one layered asset -> two live layers and trail") {
        auto parsed = MeshVfxAsset::fromJson(
            R"({"schema":"eve.stylize.mesh-vfx","schemaVersion":1,"layers":[{"style":"slash","playback":{"fadeIn":0.02,"hold":0.1,"fadeOut":0.2},"parameters":{"intensity":2}},{"style":"rim","playback":{"loop":true}}],"trail":{"maxSamples":48,"lifetime":0.25,"minDistance":0.01,"teleportDistance":3}})");
        REQUIRE(parsed.ok());
        REQUIRE_EQ(parsed.value().layers.size(), 2u);
        REQUIRE(parsed.value().trail.has_value());
        auto instance = MeshVfxAssetInstance::create(parsed.value());
        REQUIRE(instance.ok());
        REQUIRE_EQ(instance.value()->layerCount(), 2u);
        REQUIRE(instance.value()->hasTrail());
    };
    SUB_CASE("version zero -> one migrated layer and canonical serialisation") {
        auto legacy = MeshVfxAsset::fromJson(
            R"({"schemaVersion":0,"style":"aura","playback":{"hold":0.5},"events":[{"time":0.5,"name":"pulse"}]})");
        REQUIRE(legacy.ok());
        REQUIRE_EQ(legacy.value().layers.size(), 1u);
        REQUIRE_EQ(legacy.value().layers[0].style, "aura");
        auto encoded = legacy.value().toJson();
        REQUIRE(encoded.ok());
        auto decoded = MeshVfxAsset::fromJson(encoded.value());
        REQUIRE(decoded.ok());
        REQUIRE_EQ(decoded.value().layers[0].style, "aura");
        REQUIRE_EQ(decoded.value().events[0].name, "pulse");
    };
    SUB_CASE("unknown field, negative hold, unordered curve -> parse errors") {
        REQUIRE(!MeshVfxAsset::fromJson(
                     R"({"schema":"eve.stylize.mesh-vfx","schemaVersion":1,"layers":[{"style":"rim","typo":1}]})")
                     .ok());
        REQUIRE(
            !MeshVfxAsset::fromJson(
                 R"({"schema":"eve.stylize.mesh-vfx","schemaVersion":1,"layers":[{"style":"rim","playback":{"hold":-1}}]})")
                 .ok());
        REQUIRE(
            !MeshVfxAsset::fromJson(
                 R"({"schema":"eve.stylize.mesh-vfx","schemaVersion":1,"layers":[{"style":"rim","curves":{"intensity":[[0.5,1],[0.25,2]]}}]})")
                 .ok());
        REQUIRE(
            !MeshVfxAsset::fromJson(
                 R"({"schema":"eve.stylize.mesh-vfx","schemaVersion":1,"layers":[{"style":"rim"}],"animationTriggers":{"play":["same"],"stop":["same"]}})")
                 .ok());
    };
    SUB_CASE("invalid reload -> revision unchanged; valid reload -> replacement") {
        auto parsed =
            MeshVfxAsset::fromJson(R"({"schema":"eve.stylize.mesh-vfx","schemaVersion":1,"layers":[{"style":"rim"}]})");
        REQUIRE(parsed.ok());
        MeshVfxAssetSlot slot(std::move(parsed).takeValue());
        REQUIRE(!slot.reload(R"({"schemaVersion":99})").ok());
        REQUIRE_EQ(slot.revision(), 1u);
        REQUIRE_EQ(slot.asset().layers[0].style, "rim");
        auto replaced =
            slot.reload(R"({"schema":"eve.stylize.mesh-vfx","schemaVersion":1,"layers":[{"style":"ember"}]})");
        REQUIRE(replaced.ok());
        REQUIRE_EQ(replaced.value(), 2u);
        REQUIRE_EQ(slot.asset().layers[0].style, "ember");
    };
    SUB_CASE("parameter curve at quarter and three-quarter -> interpolated one") {
        MeshVfxFloatCurve curve{{{0.f, 0.f}, {0.5f, 2.f}, {1.f, 0.f}}};
        REQUIRE_EQ(curve.evaluate(0.25f), 1.f);
        REQUIRE_EQ(curve.evaluate(0.75f), 1.f);
    };
}

TEST_CASE("stylize.meshAsset.spec.timelineEvents") {
    auto parsed = MeshVfxAsset::fromJson(
        R"({"schema":"eve.stylize.mesh-vfx","schemaVersion":1,"layers":[{"style":"rim","playback":{"fadeIn":0,"hold":1,"fadeOut":0}}],"events":[{"time":0,"name":"cast"},{"time":0.25,"name":"hit"},{"time":0.75,"name":"recover"}]})");
    REQUIRE(parsed.ok());
    auto created = MeshVfxAssetInstance::create(parsed.value());
    REQUIRE(created.ok());
    auto instance = std::move(created).takeValue();
    instance->play();
    auto events = instance->drainEvents();
    REQUIRE_EQ(events.size(), 1u);
    REQUIRE_EQ(events[0], "cast");
    instance->update(0.3f);
    events = instance->drainEvents();
    REQUIRE_EQ(events.size(), 1u);
    REQUIRE_EQ(events[0], "hit");
    instance->update(0.5f);
    events = instance->drainEvents();
    REQUIRE_EQ(events.size(), 1u);
    REQUIRE_EQ(events[0], "recover");
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

TEST_CASE("stylize.meshAsset.spec.externalMotionSources") {
    SUB_CASE("named root and tip attachment -> authored trail sample") {
        auto parsed = MeshVfxAsset::fromJson(
            R"({"schema":"eve.stylize.mesh-vfx","schemaVersion":1,"layers":[{"style":"slash"}],"trail":{"maxSamples":8,"lifetime":0.2,"minDistance":0,"teleportDistance":5,"rootAttachment":"weapon.root","tipAttachment":"weapon.tip","rootOffset":[0,0.25,0],"tipOffset":[0,0.25,0]}})");
        REQUIRE(parsed.ok());
        auto created = MeshVfxAssetInstance::create(parsed.value());
        REQUIRE(created.ok());
        auto                    instance = std::move(created).takeValue();
        MeshVfxAttachmentSource source;
        REQUIRE(instance->sampleTrail(source).ok());
        REQUIRE(instance->trail().sampleCount() > 0u);
    };
    SUB_CASE("animation start, cancel, end -> play, break, stop counts") {
        auto parsed = MeshVfxAsset::fromJson(
            R"({"schema":"eve.stylize.mesh-vfx","schemaVersion":1,"layers":[{"style":"rim"}],"trail":{"rootAttachment":"root","tipAttachment":"tip"},"animationTriggers":{"play":["attack.start"],"stop":["attack.end"],"trailBreak":["attack.cancel"]}})");
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
    };
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

TEST_CASE("stylize.meshBatch.spec.lodOrderingAndRefresh") {
    SUB_CASE("additive items -> group equal state and cull hidden LOD") {
        const std::array items{MeshVfxRenderItem{3, key(8, MeshVfxBatchBlend::Additive), 2.f},
                               MeshVfxRenderItem{1, key(7, MeshVfxBatchBlend::Additive), 4.f},
                               MeshVfxRenderItem{2, key(7, MeshVfxBatchBlend::Additive), 8.f},
                               MeshVfxRenderItem{4, key(9, MeshVfxBatchBlend::Additive), 1.f}};
        const std::array decisions{visible(1, MeshVfxLodTier::Full), visible(2, MeshVfxLodTier::Reduced),
                                   visible(3, MeshVfxLodTier::Minimal), visible(4, MeshVfxLodTier::Culled)};
        auto             queue = MeshVfxRenderBatchPlanner{}.build(items, decisions, 0);
        REQUIRE_EQ(queue.stats.queuedDraws, 3u);
        REQUIRE_EQ(queue.stats.culledDraws, 1u);
        REQUIRE_EQ(queue.batches.size(), 2u);
        REQUIRE_EQ(queue.batches[0].draws.size(), 2u);
        REQUIRE_EQ(queue.batches[0].draws[0].stableInstanceId, 1u);
        REQUIRE_EQ(queue.batches[0].draws[1].stableInstanceId, 2u);
    };
    SUB_CASE("alpha items -> back-to-front order, no merge across intervening state") {
        std::array       items{MeshVfxRenderItem{1, key(7, MeshVfxBatchBlend::Alpha), 2.f},
                         MeshVfxRenderItem{2, key(8, MeshVfxBatchBlend::Alpha), 9.f},
                         MeshVfxRenderItem{3, key(7, MeshVfxBatchBlend::Alpha), 5.f}};
        const std::array decisions{visible(1, MeshVfxLodTier::Full), visible(2, MeshVfxLodTier::Full),
                                   visible(3, MeshVfxLodTier::Full)};
        auto             queue = MeshVfxRenderBatchPlanner{}.build(items, decisions, 0);
        REQUIRE_EQ(queue.batches.size(), 2u);
        REQUIRE_EQ(queue.batches[0].draws[0].stableInstanceId, 2u);
        REQUIRE_EQ(queue.batches[1].draws[0].stableInstanceId, 3u);
        REQUIRE_EQ(queue.batches[1].draws[1].stableInstanceId, 1u);
        items[2].viewDepth = 10.f;
        queue              = MeshVfxRenderBatchPlanner{}.build(items, decisions, 0);
        REQUIRE_EQ(queue.batches.size(), 3u);
        REQUIRE_EQ(queue.batches[0].draws[0].stableInstanceId, 3u);
        REQUIRE_EQ(queue.batches[1].draws[0].stableInstanceId, 2u);
        REQUIRE_EQ(queue.batches[2].draws[0].stableInstanceId, 1u);
    };
    SUB_CASE("reduced LOD at frame four -> one refresh and one reuse; missing ID tracked") {
        const std::array items{MeshVfxRenderItem{1, key(7, MeshVfxBatchBlend::Additive), 1.f},
                               MeshVfxRenderItem{2, key(7, MeshVfxBatchBlend::Additive), 1.f},
                               MeshVfxRenderItem{9, key(7, MeshVfxBatchBlend::Additive), 1.f}};
        const std::array decisions{visible(1, MeshVfxLodTier::Reduced, 2), visible(2, MeshVfxLodTier::Reduced, 2)};
        auto             queue = MeshVfxRenderBatchPlanner{}.build(items, decisions, 4);
        REQUIRE_EQ(queue.stats.queuedDraws, 2u);
        REQUIRE_EQ(queue.stats.missingDecisions, 1u);
        REQUIRE_EQ(queue.stats.dynamicMeshRefreshes, 1u);
        REQUIRE(static_cast<int>(queue.batches[0].draws[0].meshUpdate) == static_cast<int>(MeshVfxMeshUpdate::Reuse));
        REQUIRE(static_cast<int>(queue.batches[0].draws[1].meshUpdate) == static_cast<int>(MeshVfxMeshUpdate::Refresh));
    };
}
}  // namespace test_stylize_mesh_particles_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_stylize_mesh_vfx_benchmark)
#include <array>
#include <cstdint>
#include <vector>
#include "PrivateTestScope.h"
#include "stylize/MeshVfxBatchSubmission.h"
#include "stylize/MeshVfxRenderBatch.h"
#include "stylize/MeshVfxScalability.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
namespace test_stylize_mesh_vfx_benchmark_unit {
using namespace eve::stylize;

TEST_CASE("stylize.meshLod.spec.budgetOrderingAndCull") {
    SUB_CASE("four candidates and budget seven -> full, reduced, minimal, culled totaling seven") {
        MeshVfxScalabilityPolicy policy;
        policy.workUnitBudget = 7;
        const std::array candidates{
            MeshVfxLodCandidate{30, 8.f, 120.f, 0, true}, MeshVfxLodCandidate{10, 4.f, 120.f, 10, true},
            MeshVfxLodCandidate{20, 6.f, 60.f, 5, true}, MeshVfxLodCandidate{40, 10.f, 20.f, -1, true}};
        auto decisions = MeshVfxScalabilityPlanner(policy).plan(candidates);
        REQUIRE_EQ(decisions.size(), 4u);
        REQUIRE(static_cast<int>(decisions[0].tier) == static_cast<int>(MeshVfxLodTier::Minimal));
        REQUIRE(static_cast<int>(decisions[1].tier) == static_cast<int>(MeshVfxLodTier::Full));
        REQUIRE(static_cast<int>(decisions[2].tier) == static_cast<int>(MeshVfxLodTier::Reduced));
        REQUIRE(static_cast<int>(decisions[3].tier) == static_cast<int>(MeshVfxLodTier::Culled));
        REQUIRE_EQ(decisions[0].meshUpdateInterval, 4u);
        REQUIRE_EQ(decisions[2].trailSampleStride, 2u);
        std::uint32_t used = 0;
        for (const auto &decision : decisions) used += decision.workUnits;
        REQUIRE_EQ(used, policy.workUnitBudget);
    };
    SUB_CASE("tie order reversed -> stable ID receives same tier") {
        MeshVfxScalabilityPolicy policy;
        policy.workUnitBudget = 4;
        const std::array          forward{MeshVfxLodCandidate{2, 5.f, 100.f, 0, true},
                                 MeshVfxLodCandidate{1, 5.f, 100.f, 0, true}};
        const std::array          reverse{forward[1], forward[0]};
        MeshVfxScalabilityPlanner planner(policy);
        auto                      a = planner.plan(forward);
        auto                      b = planner.plan(reverse);
        REQUIRE(static_cast<int>(a[0].tier) == static_cast<int>(MeshVfxLodTier::Culled));
        REQUIRE(static_cast<int>(a[1].tier) == static_cast<int>(MeshVfxLodTier::Full));
        REQUIRE(static_cast<int>(b[0].tier) == static_cast<int>(MeshVfxLodTier::Full));
        REQUIRE(static_cast<int>(b[1].tier) == static_cast<int>(MeshVfxLodTier::Culled));
    };
    SUB_CASE("invisible, distant, invalid candidates -> zero work") {
        const std::array candidates{MeshVfxLodCandidate{1, 1.f, 100.f, 0, false},
                                    MeshVfxLodCandidate{2, 81.f, 100.f, 0, true},
                                    MeshVfxLodCandidate{3, -1.f, 100.f, 0, true}};
        auto             decisions = MeshVfxScalabilityPlanner{}.plan(candidates);
        for (const auto &decision : decisions) {
            REQUIRE(static_cast<int>(decision.tier) == static_cast<int>(MeshVfxLodTier::Culled));
            REQUIRE_EQ(decision.workUnits, 0u);
        }
    };
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

TEST_CASE("stylize.meshSubmission.spec.sinkResults") {
    SUB_CASE("accepted batch -> ordered two draws and balanced begin/end") {
        RecordingSink sink;
        auto          report = MeshVfxBatchExecutor{}.submit(queueWithTwoDraws(), sink);
        REQUIRE(static_cast<int>(report.status) == static_cast<int>(MeshVfxQueueSubmitStatus::Complete));
        REQUIRE_EQ(report.acceptedBatches, 1u);
        REQUIRE_EQ(report.acceptedDraws, 2u);
        REQUIRE_EQ(sink.begins, 1u);
        REQUIRE_EQ(sink.ends, 1u);
        REQUIRE_EQ(sink.draws[0], 1u);
        REQUIRE_EQ(sink.draws[1], 2u);
    };
    SUB_CASE("begin skipped -> no draws and no end") {
        RecordingSink sink;
        sink.beginResult = MeshVfxSubmitStatus::Skipped;
        auto report      = MeshVfxBatchExecutor{}.submit(queueWithTwoDraws(), sink);
        REQUIRE(static_cast<int>(report.status) == static_cast<int>(MeshVfxQueueSubmitStatus::Partial));
        REQUIRE_EQ(report.skippedBatches, 1u);
        REQUIRE_EQ(report.skippedDraws, 2u);
        REQUIRE_EQ(sink.ends, 0u);
        REQUIRE(sink.draws.empty());
    };
    SUB_CASE("device unavailable on draw -> stop without end") {
        RecordingSink sink;
        sink.drawResult = MeshVfxSubmitStatus::DeviceUnavailable;
        auto report     = MeshVfxBatchExecutor{}.submit(queueWithTwoDraws(), sink);
        REQUIRE(static_cast<int>(report.status) == static_cast<int>(MeshVfxQueueSubmitStatus::DeviceUnavailable));
        REQUIRE_EQ(report.acceptedDraws, 0u);
        REQUIRE_EQ(sink.draws.size(), 1u);
        REQUIRE_EQ(sink.ends, 0u);
    };
}
}  // namespace test_stylize_mesh_vfx_benchmark_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_stylize)
#include <algorithm>
#include <cmath>
#include <memory>
#include "PrivateTestScope.h"
#include "common/Exception.h"
#include "image/ImageData.h"
#include "stylize/StyleInstance.h"
#include "stylize/Stylize.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
namespace test_stylize_unit {
using eve::image::ImageData;
using eve::stylize::StyleInstance;
using eve::stylize::Stylize;
using Colorf = ImageData::Colorf;

namespace {
std::unique_ptr<ImageData> gradient(int width, int height) {
    auto image = std::make_unique<ImageData>(width, height, "RGBA8");
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            float u = float(x) / float(std::max(width - 1, 1));
            float v = float(y) / float(std::max(height - 1, 1));
            image->setPixel(x, y, Colorf{u, 0.35f + 0.4f * v, 1.f - u, 1.f});
        }
    return image;
}
}  // namespace

TEST_CASE("stylize.styles.spec.registryAndInstance") {
    SUB_CASE("known style IDs -> declared mesh, post, CPU, and depth support") {
        auto *mod = Stylize::create();
        REQUIRE(mod != nullptr);
        REQUIRE_EQ(mod->getName(), std::string("Stylize"));
        REQUIRE_EQ(mod->getStyleCount(), 18);
        REQUIRE(mod->supports("cartoon", "mesh"));
        REQUIRE(mod->supports("cartoon", "post"));
        REQUIRE(mod->supports("watercolor", "cpu"));
        REQUIRE(!mod->supports("watercolor", "mesh"));
        REQUIRE(mod->supports("xray", "depth"));
        REQUIRE(!mod->supports("xray", "post"));
        REQUIRE(mod->supports("vignette", "post"));
        REQUIRE(!mod->supports("vignette", "mesh"));
        REQUIRE(!mod->hasStyle("oil"));
        REQUIRE(mod->getStyleParamCount("pixel") > 0);
        REQUIRE_EQ(mod->getStyleParamName("pixel", 0), "pixelSize");
        REQUIRE(mod->getStyleParamName("pixel", 999).empty());
    };
    SUB_CASE("pixelSize set beyond max then reset -> clamp, override, default") {
        std::unique_ptr<StyleInstance> pixel(Stylize::create()->newInstance("pixel"));
        REQUIRE(pixel != nullptr);
        REQUIRE_EQ(pixel->getStyle(), "pixel");
        REQUIRE_EQ(pixel->getStage(), "afterTonemap");
        REQUIRE(pixel->requiresInput("color"));
        REQUIRE(!pixel->requiresInput("depth"));
        REQUIRE_EQ(pixel->getFloat("pixelSize"), 5.f);
        pixel->setFloat("pixelSize", 999.f);
        REQUIRE_EQ(pixel->getFloat("pixelSize"), pixel->getParamMax("pixelSize"));
        REQUIRE(pixel->isOverridden("pixelSize"));
        pixel->reset("pixelSize");
        REQUIRE(!pixel->isOverridden("pixelSize"));
        REQUIRE_EQ(pixel->getFloat("pixelSize"), pixel->getParamDefault("pixelSize"));
    };
}

TEST_CASE("stylize.cpuImage.spec.dimensionsAndAppearance") {
    SUB_CASE("four CPU styles on RGBA8 gradient -> dimensions retained and pixels changed") {
        auto  source = gradient(32, 24);
        auto *mod    = Stylize::create();
        for (const char *style : {"cartoon", "watercolor", "ink", "pixel"}) {
            std::unique_ptr<ImageData> output(mod->processImage(source.get(), style));
            REQUIRE(output != nullptr);
            REQUIRE_EQ(output->getWidth(), 32);
            REQUIRE_EQ(output->getHeight(), 24);
            REQUIRE_EQ(output->getFormat(), "RGBA8");
            REQUIRE(output->getPixel(8, 8).a > 0.9f);
            bool changed = false;
            for (int y = 0; y < 24; ++y)
                for (int x = 0; x < 32; ++x) {
                    auto before = source->getPixel(x, y), after = output->getPixel(x, y);
                    changed |= std::abs(before.r - after.r) > 0.02f;
                    changed |= std::abs(before.g - after.g) > 0.02f;
                    changed |= std::abs(before.b - after.b) > 0.02f;
                }
            REQUIRE(changed);
        }
    };
    SUB_CASE("ink wash -> majority low-chroma pixels") {
        auto                       source = gradient(48, 32);
        std::unique_ptr<ImageData> ink(Stylize::create()->processImage(source.get(), "ink"));
        REQUIRE(ink != nullptr);
        int nearGray = 0;
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 48; ++x) {
                auto  color    = ink->getPixel(x, y);
                float largest  = std::max({color.r, color.g, color.b});
                float smallest = std::min({color.r, color.g, color.b});
                if (largest - smallest < 0.25f) ++nearGray;
            }
        REQUIRE(nearGray > 48 * 32 / 2);
    };
    SUB_CASE("pixel style -> neighboring UV samples quantized to same color") {
        auto                       source = gradient(64, 32);
        std::unique_ptr<ImageData> pixels(Stylize::create()->processImage(source.get(), "pixel"));
        REQUIRE(pixels != nullptr);
        auto first = pixels->getPixel(0, 0), next = pixels->getPixel(1, 1);
        REQUIRE(std::abs(first.r - next.r) < 0.001f);
        REQUIRE(std::abs(first.g - next.g) < 0.001f);
        REQUIRE(std::abs(first.b - next.b) < 0.001f);
    };
    SUB_CASE("unknown CPU style -> exception") {
        auto source   = gradient(8, 8);
        bool rejected = false;
        try {
            (void)Stylize::create()->processImage(source.get(), "unknown");
        } catch (const eve::Exception &) {
            rejected = true;
        }
        REQUIRE(rejected);
    };
}
}  // namespace test_stylize_unit
#endif
