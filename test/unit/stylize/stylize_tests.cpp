// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "stylize_anime.cpp",
    "pixelfx.cpp",
    "pixelfx_core.cpp",
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
const char *kPostEffects[] = {"vignette", "chromatic", "grain", "glitchAnalog", "glitchBlock",
                              "glitchWave", "kuwahara", "kuwaharaAniso", "dog", "sharpen", "neonEdge"};
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
    SUB_CASE("glitchAnalog defaults -> afterTonemap color-only with near-identity knobs") {
        auto                          *mod = Stylize::create();
        std::unique_ptr<StyleInstance> effect(mod->newInstance("glitchAnalog"));
        REQUIRE(effect != nullptr);
        REQUIRE_EQ(effect->getStage(), std::string("afterTonemap"));
        REQUIRE(effect->requiresInput("color"));
        REQUIRE(!effect->requiresInput("depth"));
        REQUIRE(effect->hasParam("strength"));
        REQUIRE(effect->hasParam("scanLineJitter"));
        REQUIRE(effect->hasParam("verticalJump"));
        REQUIRE(effect->hasParam("horizontalShake"));
        REQUIRE(effect->hasParam("colorDrift"));
        REQUIRE(effect->hasParam("time"));
        REQUIRE_EQ(effect->getFloat("strength"), 1.f);
        REQUIRE_EQ(effect->getFloat("scanLineJitter"), 0.f);
        REQUIRE_EQ(effect->getFloat("verticalJump"), 0.f);
        REQUIRE_EQ(effect->getFloat("horizontalShake"), 0.f);
        REQUIRE_EQ(effect->getFloat("colorDrift"), 0.f);
        effect->setFloat("strength", 0.f);
        REQUIRE_EQ(effect->getFloat("strength"), 0.f);
        effect->setFloat("scanLineJitter", 2.f);
        REQUIRE_EQ(effect->getFloat("scanLineJitter"), effect->getParamMax("scanLineJitter"));
    };
    SUB_CASE("glitchBlock defaults -> afterTonemap with zero intensity near-identity") {
        auto                          *mod = Stylize::create();
        std::unique_ptr<StyleInstance> effect(mod->newInstance("glitchBlock"));
        REQUIRE(effect != nullptr);
        REQUIRE_EQ(effect->getStage(), std::string("afterTonemap"));
        REQUIRE(effect->requiresInput("color"));
        REQUIRE(!effect->requiresInput("depth"));
        REQUIRE(effect->hasParam("strength"));
        REQUIRE(effect->hasParam("intensity"));
        REQUIRE(effect->hasParam("time"));
        REQUIRE(effect->hasParam("cellCountX"));
        REQUIRE(effect->hasParam("cellCountY"));
        REQUIRE_EQ(effect->getFloat("strength"), 1.f);
        REQUIRE_EQ(effect->getFloat("intensity"), 0.f);
        REQUIRE_EQ(effect->getFloat("cellCountX"), 64.f);
        REQUIRE_EQ(effect->getFloat("cellCountY"), 32.f);
        effect->setFloat("intensity", 5.f);
        REQUIRE_EQ(effect->getFloat("intensity"), effect->getParamMax("intensity"));
    };
    SUB_CASE("glitchWave defaults -> afterTonemap with zero amount near-identity") {
        auto                          *mod = Stylize::create();
        std::unique_ptr<StyleInstance> effect(mod->newInstance("glitchWave"));
        REQUIRE(effect != nullptr);
        REQUIRE_EQ(effect->getStage(), std::string("afterTonemap"));
        REQUIRE(effect->requiresInput("color"));
        REQUIRE(!effect->requiresInput("depth"));
        REQUIRE(effect->hasParam("strength"));
        REQUIRE(effect->hasParam("amount"));
        REQUIRE(effect->hasParam("frequency"));
        REQUIRE(effect->hasParam("rgbSplit"));
        REQUIRE(effect->hasParam("speed"));
        REQUIRE(effect->hasParam("direction"));
        REQUIRE(effect->hasParam("infiniteFreq"));
        REQUIRE(effect->hasParam("time"));
        REQUIRE_EQ(effect->getFloat("strength"), 1.f);
        REQUIRE_EQ(effect->getFloat("amount"), 0.f);
        REQUIRE_EQ(effect->getFloat("direction"), 0.f);
        effect->setFloat("amount", 9.f);
        REQUIRE_EQ(effect->getFloat("amount"), effect->getParamMax("amount"));
    };
    SUB_CASE("kuwahara defaults -> afterTonemap with zero strength near-identity") {
        auto                          *mod = Stylize::create();
        std::unique_ptr<StyleInstance> effect(mod->newInstance("kuwahara"));
        REQUIRE(effect != nullptr);
        REQUIRE_EQ(effect->getStage(), std::string("afterTonemap"));
        REQUIRE(effect->requiresInput("color"));
        REQUIRE(!effect->requiresInput("depth"));
        REQUIRE(effect->hasParam("strength"));
        REQUIRE(effect->hasParam("kernelSize"));
        REQUIRE_EQ(effect->getFloat("strength"), 0.f);
        REQUIRE_EQ(effect->getFloat("kernelSize"), 3.f);
        effect->setFloat("kernelSize", 99.f);
        REQUIRE_EQ(effect->getFloat("kernelSize"), effect->getParamMax("kernelSize"));
        REQUIRE_EQ(effect->getPriority(), 220);
    };
    SUB_CASE("kuwaharaAniso defaults -> afterTonemap with zero strength near-identity") {
        auto                          *mod = Stylize::create();
        std::unique_ptr<StyleInstance> effect(mod->newInstance("kuwaharaAniso"));
        REQUIRE(effect != nullptr);
        REQUIRE_EQ(effect->getStage(), std::string("afterTonemap"));
        REQUIRE(effect->requiresInput("color"));
        REQUIRE(!effect->requiresInput("depth"));
        REQUIRE(effect->hasParam("strength"));
        REQUIRE(effect->hasParam("kernelSize"));
        REQUIRE(effect->hasParam("sharpness"));
        REQUIRE(effect->hasParam("hardness"));
        REQUIRE(effect->hasParam("alpha"));
        REQUIRE(effect->hasParam("zeroCrossing"));
        REQUIRE(effect->hasParam("zeta"));
        REQUIRE_EQ(effect->getFloat("strength"), 0.f);
        REQUIRE_EQ(effect->getFloat("kernelSize"), 4.f);
        REQUIRE_EQ(effect->getFloat("sharpness"), 8.f);
        REQUIRE_EQ(effect->getFloat("hardness"), 8.f);
        REQUIRE_EQ(effect->getFloat("zeta"), 0.f);
        effect->setFloat("kernelSize", 99.f);
        REQUIRE_EQ(effect->getFloat("kernelSize"), effect->getParamMax("kernelSize"));
        REQUIRE_EQ(effect->getPriority(), 225);
    };
    SUB_CASE("dog defaults -> afterTonemap with zero strength near-identity") {
        auto                          *mod = Stylize::create();
        std::unique_ptr<StyleInstance> effect(mod->newInstance("dog"));
        REQUIRE(effect != nullptr);
        REQUIRE_EQ(effect->getStage(), std::string("afterTonemap"));
        REQUIRE(effect->requiresInput("color"));
        REQUIRE(!effect->requiresInput("depth"));
        REQUIRE(effect->hasParam("strength"));
        REQUIRE(effect->hasParam("kernelSize"));
        REQUIRE(effect->hasParam("sigma"));
        REQUIRE(effect->hasParam("k"));
        REQUIRE(effect->hasParam("tau"));
        REQUIRE(effect->hasParam("thresholding"));
        REQUIRE(effect->hasParam("useTanh"));
        REQUIRE(effect->hasParam("phi"));
        REQUIRE(effect->hasParam("threshold"));
        REQUIRE(effect->hasParam("invert"));
        REQUIRE_EQ(effect->getFloat("strength"), 0.f);
        REQUIRE_EQ(effect->getFloat("kernelSize"), 5.f);
        REQUIRE_EQ(effect->getFloat("sigma"), 2.f);
        REQUIRE_EQ(effect->getFloat("k"), 1.6f);
        REQUIRE_EQ(effect->getFloat("tau"), 1.f);
        REQUIRE_EQ(effect->getFloat("thresholding"), 1.f);
        REQUIRE_EQ(effect->getFloat("useTanh"), 0.f);
        REQUIRE_EQ(effect->getFloat("invert"), 0.f);
        effect->setFloat("kernelSize", 99.f);
        REQUIRE_EQ(effect->getFloat("kernelSize"), effect->getParamMax("kernelSize"));
        REQUIRE_EQ(effect->getPriority(), 230);
    };
    SUB_CASE("sharpen defaults -> afterTonemap with zero strength near-identity") {
        auto                          *mod = Stylize::create();
        std::unique_ptr<StyleInstance> effect(mod->newInstance("sharpen"));
        REQUIRE(effect != nullptr);
        REQUIRE_EQ(effect->getStage(), std::string("afterTonemap"));
        REQUIRE(effect->requiresInput("color"));
        REQUIRE(!effect->requiresInput("depth"));
        REQUIRE(effect->hasParam("strength"));
        REQUIRE(effect->hasParam("amount"));
        REQUIRE_EQ(effect->getFloat("strength"), 0.f);
        REQUIRE_EQ(effect->getFloat("amount"), 0.5f);
        effect->setFloat("amount", 9.f);
        REQUIRE_EQ(effect->getFloat("amount"), effect->getParamMax("amount"));
        REQUIRE_EQ(effect->getPriority(), 240);
    };
    SUB_CASE("neonEdge defaults -> beforeTonemap with zero strength near-identity") {
        auto                          *mod = Stylize::create();
        std::unique_ptr<StyleInstance> effect(mod->newInstance("neonEdge"));
        REQUIRE(effect != nullptr);
        REQUIRE_EQ(effect->getStage(), std::string("beforeTonemap"));
        REQUIRE(effect->requiresInput("color"));
        REQUIRE(!effect->requiresInput("depth"));
        REQUIRE(effect->hasParam("strength"));
        REQUIRE(effect->hasParam("edgeWidth"));
        REQUIRE(effect->hasParam("neonFade"));
        REQUIRE(effect->hasParam("brightness"));
        REQUIRE(effect->hasParam("backgroundFade"));
        REQUIRE(effect->hasParam("bgR"));
        REQUIRE(effect->hasParam("bgG"));
        REQUIRE(effect->hasParam("bgB"));
        REQUIRE_EQ(effect->getFloat("strength"), 0.f);
        REQUIRE_EQ(effect->getFloat("edgeWidth"), 1.f);
        REQUIRE_EQ(effect->getFloat("neonFade"), 1.f);
        REQUIRE_EQ(effect->getFloat("brightness"), 1.f);
        REQUIRE_EQ(effect->getFloat("backgroundFade"), 0.f);
        effect->setFloat("edgeWidth", 99.f);
        REQUIRE_EQ(effect->getFloat("edgeWidth"), effect->getParamMax("edgeWidth"));
        REQUIRE_EQ(effect->getPriority(), 120);
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
        REQUIRE_EQ(mod->getStyleCount(), 26);
        REQUIRE(mod->supports("cartoon", "mesh"));
        REQUIRE(mod->supports("cartoon", "post"));
        REQUIRE(mod->supports("watercolor", "cpu"));
        REQUIRE(!mod->supports("watercolor", "mesh"));
        REQUIRE(mod->supports("xray", "depth"));
        REQUIRE(!mod->supports("xray", "post"));
        REQUIRE(mod->supports("vignette", "post"));
        REQUIRE(!mod->supports("vignette", "mesh"));
        REQUIRE(mod->supports("glitchAnalog", "post"));
        REQUIRE(!mod->supports("glitchAnalog", "mesh"));
        REQUIRE(mod->supports("glitchBlock", "post"));
        REQUIRE(!mod->supports("glitchBlock", "mesh"));
        REQUIRE(mod->supports("glitchWave", "post"));
        REQUIRE(!mod->supports("glitchWave", "mesh"));
        REQUIRE(mod->supports("kuwahara", "post"));
        REQUIRE(!mod->supports("kuwahara", "mesh"));
        REQUIRE(mod->supports("kuwaharaAniso", "post"));
        REQUIRE(!mod->supports("kuwaharaAniso", "mesh"));
        REQUIRE(mod->supports("dog", "post"));
        REQUIRE(!mod->supports("dog", "mesh"));
        REQUIRE(mod->supports("sharpen", "post"));
        REQUIRE(!mod->supports("sharpen", "mesh"));
        REQUIRE(mod->supports("neonEdge", "post"));
        REQUIRE(!mod->supports("neonEdge", "mesh"));
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

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_pixelfx)
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <vector>
#include "stylize/PixelFx.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
namespace test_pixelfx_unit {

namespace {
using namespace eve::stylize;
std::vector<uint32_t> copy(std::span<const uint32_t> s) { return {s.begin(), s.end()}; }
bool                  any(std::span<const uint32_t> s) {
    return std::any_of(s.begin(), s.end(), [](uint32_t p) { return p != 0; });
}
void sample(PixelFx& fx, double seconds) {
    if (!fx.isSimulated()) {
        fx.render(seconds);
        return;
    }
    auto candidate = fx.prepareSeek(seconds * fx.settings().playbackSpeed);
    if (!candidate) throw std::runtime_error(candidate.status().describe());
    auto published = fx.commitSeek(std::move(candidate).takeValue());
    if (!published) throw std::runtime_error(published.status().describe());
    fx.draw();
}
}  // namespace
TEST_CASE("stylize.pixelfx.seekAndLoop") {
    for (const auto& info : PixelFx::catalogue()) {
        PixelFx fx(info.kind);
        // Source irises have empty covering planes at .125; sample their visible closing phase.
        double sampleTime = .125;
        // Authored Moves delay their child cues; .9 samples the visible attack/impact phase.
        if (info.id.starts_with("moves/"))
            sampleTime = .9;
        else if (info.kind == PixelFxKind::Iris || info.kind == PixelFxKind::StarIris)
            sampleTime = .5;
        sample(fx, sampleTime);
        const auto c = copy(fx.color()), l = copy(fx.light());
        REQUIRE(any(fx.color()));
        sample(fx, 0.25);
        sample(fx, sampleTime);
        REQUIRE(copy(fx.color()) == c);
        REQUIRE(copy(fx.light()) == l);
        if (info.loop) {
            sample(fx, sampleTime + info.duration);
            REQUIRE(copy(fx.color()) == c);
            REQUIRE(copy(fx.light()) == l);
        } else {
            sample(fx, info.duration);
            REQUIRE(fx.done());  // Duration expiry is advisory; source tail pixels may remain.
            sample(fx, sampleTime);
            REQUIRE(copy(fx.color()) == c);
        }
    }
}
TEST_CASE("stylize.pixelfx.parametersAndAtomicRejection") {
    PixelFx fx(PixelFxKind::Sunburst);
    sample(fx, 0.25);
    const auto c = copy(fx.color()), l = copy(fx.light());
    auto       s  = fx.settings();
    s.rays        = 0;
    bool rejected = false;
    try {
        fx.configure(s);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    REQUIRE(rejected);
    REQUIRE(fx.settings().rays == 8);
    REQUIRE(copy(fx.color()) == c);
    s               = fx.settings();
    s.playbackSpeed = std::numeric_limits<double>::quiet_NaN();
    rejected        = false;
    try {
        fx.configure(s);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    REQUIRE(rejected);
    rejected = false;
    try {
        sample(fx, -1);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    REQUIRE(rejected);
    REQUIRE(copy(fx.light()) == l);
    s           = fx.settings();
    s.lightGain = 0;
    fx.configure(s);
    sample(fx, 0.25);
    REQUIRE(!any(fx.light()));
    REQUIRE(copy(fx.color()) == c);
    s.lightGain = 1;
    s.rays      = 12;
    fx.configure(s);
    sample(fx, 0.25);
    REQUIRE(copy(fx.color()) != c);
}
TEST_CASE("stylize.pixelfx.explicitSeedAndPlayback") {
    PixelFx a(PixelFxKind::Halo), b(PixelFxKind::Halo);
    sample(a, 0.3);
    sample(b, 0.3);
    REQUIRE(copy(a.color()) == copy(b.color()));
    auto s = b.settings();
    s.seed = 123456u;
    b.configure(s);
    sample(b, 0.3);
    REQUIRE(copy(a.color()) != copy(b.color()));
    s               = a.settings();
    s.playbackSpeed = 2;
    b.configure(s);
    sample(b, 0.15);
    REQUIRE(copy(a.color()) == copy(b.color()));
    REQUIRE(copy(a.light()) == copy(b.light()));
    PixelFx bolt(PixelFxKind::LaserBolt);
    sample(bolt, 0.2);
    const auto c = copy(bolt.color());
    s            = bolt.settings();
    s.length     = 70;
    bolt.configure(s);
    sample(bolt, 0.2);
    REQUIRE(copy(bolt.color()) != c);
}

// Upstream a84159688af3ca37a4be05a0d422b94451b2e24e default Painter buffers.
// Samples i*duration/8 (loops) or i/30 (impact), i in [0,7].
// Coverage/channel sums tolerate four raster-edge pixels across floating-point backends;
// full byte comparisons are retained separately in the local parity validation.
TEST_CASE("stylize.pixelfx.upstreamGoldenRasterStatistics") {
    using namespace eve::stylize;
    constexpr uint64_t golden[4][16][5] = {
        {
            {94u, 22934u, 22055u, 18673u, 23970u},
            {1034u, 36832u, 32013u, 19537u, 263670u},
            {128u, 31604u, 30450u, 25984u, 32640u},
            {1071u, 44260u, 39444u, 26983u, 273105u},
            {91u, 22169u, 20937u, 16653u, 23205u},
            {1038u, 32294u, 27467u, 14950u, 264690u},
            {107u, 26249u, 25034u, 20801u, 27285u},
            {1081u, 35521u, 30678u, 18083u, 275655u},
            {110u, 27014u, 25919u, 22078u, 28050u},
            {1044u, 38402u, 33566u, 20962u, 266220u},
            {93u, 22679u, 21425u, 17159u, 23715u},
            {1034u, 30512u, 25673u, 13094u, 263670u},
            {105u, 25739u, 24523u, 20262u, 26775u},
            {1038u, 37236u, 32404u, 19867u, 264690u},
            {90u, 21914u, 20836u, 17007u, 22950u},
            {1034u, 34497u, 29669u, 17121u, 263670u},
        },
        {
            {1162u, 292530u, 264187u, 203803u, 296310u},
            {9460u, 220570u, 190477u, 103915u, 2412300u},
            {1160u, 291900u, 264216u, 207376u, 295800u},
            {9660u, 251738u, 216161u, 115655u, 2463300u},
            {1167u, 293715u, 266283u, 209838u, 297585u},
            {9460u, 218661u, 188504u, 102137u, 2412300u},
            {1168u, 293970u, 266039u, 208167u, 297840u},
            {9176u, 187442u, 162751u, 90215u, 2339880u},
            {1198u, 301725u, 273938u, 215145u, 305490u},
            {9460u, 228205u, 198085u, 111616u, 2412300u},
            {1152u, 289875u, 262078u, 205133u, 293760u},
            {9660u, 246980u, 211404u, 110964u, 2463300u},
            {1169u, 294225u, 266601u, 209448u, 298095u},
            {9460u, 217859u, 187694u, 101250u, 2412300u},
            {1182u, 297525u, 269723u, 211940u, 301410u},
            {9176u, 197353u, 172675u, 100252u, 2339880u},
        },
        {
            {231u, 55431u, 29765u, 32542u, 58905u},
            {1441u, 56308u, 18028u, 22725u, 367455u},
            {240u, 57648u, 31746u, 34535u, 61200u},
            {1473u, 61468u, 18890u, 24165u, 375615u},
            {221u, 53271u, 29977u, 32469u, 56355u},
            {1425u, 55786u, 17875u, 22545u, 363375u},
            {244u, 57835u, 30414u, 33438u, 62220u},
            {1431u, 47412u, 14320u, 18394u, 364905u},
            {235u, 55682u, 30166u, 32967u, 59925u},
            {1481u, 53370u, 15366u, 20048u, 377655u},
            {251u, 59840u, 31812u, 34852u, 64005u},
            {1449u, 61116u, 18794u, 24048u, 369495u},
            {233u, 56104u, 30803u, 33515u, 59415u},
            {1422u, 55809u, 17886u, 22551u, 362610u},
            {233u, 55466u, 29519u, 32371u, 59415u},
            {1430u, 46869u, 14174u, 18212u, 364650u},
        },
        {
            {57600u, 7694370u, 7694370u, 7694370u, 14688000u},
            {0u, 0u, 0u, 0u, 0u},
            {57600u, 8104410u, 8104410u, 8104410u, 14688000u},
            {0u, 0u, 0u, 0u, 0u},
            {57600u, 7100475u, 7100475u, 7100475u, 14688000u},
            {0u, 0u, 0u, 0u, 0u},
            {57600u, 6636120u, 6636120u, 6636120u, 14688000u},
            {0u, 0u, 0u, 0u, 0u},
            {19131u, 4878405u, 4878405u, 4878405u, 4878405u},
            {0u, 0u, 0u, 0u, 0u},
            {8799u, 2243745u, 2243745u, 2243745u, 2243745u},
            {0u, 0u, 0u, 0u, 0u},
            {2774u, 707370u, 707370u, 707370u, 707370u},
            {0u, 0u, 0u, 0u, 0u},
            {321u, 81855u, 81855u, 81855u, 81855u},
            {0u, 0u, 0u, 0u, 0u},
        },
    };
    size_t family = 0;
    for (const auto& info : PixelFx::catalogue()) {
        if (family >= 4) break;
        PixelFx fx(info.kind);
        for (int i = 0; i < 8; ++i) {
            sample(fx, info.loop ? double(i) * info.duration / 8 : double(i) / 30);
            size_t plane = 0;
            for (auto pixels : {fx.color(), fx.light()}) {
                std::array<uint64_t, 5> actual{};
                for (auto pixel : pixels) {
                    actual[0] += pixel != 0 ? 1u : 0u;
                    for (size_t channel = 0; channel < 4; ++channel)
                        actual[channel + 1] += (pixel >> (channel * 8)) & 255u;
                }
                for (size_t metric = 0; metric < 5; ++metric) {
                    const auto expected = golden[family][size_t(i) * 2 + plane][metric];
                    const auto delta =
                        actual[metric] > expected ? actual[metric] - expected : expected - actual[metric];
                    REQUIRE(delta <= (metric == 0 ? 4u : 1020u));
                }
                ++plane;
            }
        }
        ++family;
    }
}
TEST_CASE("stylize.pixelfx.preparedConfigurationAtomicPublication") {
    static_assert(!std::is_default_constructible_v<PixelFxPreparedConfig>);
    static_assert(!std::is_copy_constructible_v<PixelFxPreparedConfig>);
    static_assert(std::is_nothrow_move_constructible_v<PixelFxPreparedConfig>);
    for (const auto& effectInfo : PixelFx::catalogue()) {
        PixelFx fx(effectInfo.kind);
        sample(fx, 0.3);
        const auto before = copy(fx.color());
        auto       source = fx.settings();
        source.seed       = 123;
        source.palette    = {0xff000000u, 0xffffffffu};
        auto prepared     = PixelFx::prepareConfiguration(effectInfo.kind, source);
        REQUIRE(prepared);
        REQUIRE(fx.settings().seed != 123u);
        REQUIRE(copy(fx.color()) == before);
        source.palette.clear();
        source.seed  = 44;
        auto token   = std::move(prepared).takeValue();
        auto applied = fx.commitConfiguration(std::move(token));
        REQUIRE(applied);
        REQUIRE(fx.settings().seed == 123u);
        REQUIRE(fx.settings().palette.size() == PixelFx::defaults(effectInfo.kind).palette.size());
        REQUIRE(copy(fx.color()) == before);
        PixelFx expected(effectInfo.kind);
        auto    target = expected.settings();
        target.seed    = 123;
        target.palette = {0xff000000u, 0xffffffffu};
        expected.configure(target);
        sample(expected, 0.3);
        sample(fx, 0.3);
        REQUIRE(copy(fx.color()) == copy(expected.color()));
        REQUIRE(copy(fx.light()) == copy(expected.light()));
        auto duplicate = fx.commitConfiguration(std::move(token));
        REQUIRE(!duplicate);
        REQUIRE(duplicate.status().code() == eve::StatusCode::Rejected);
        REQUIRE(duplicate.status().diagnostics().front().path() == "/candidate");
        REQUIRE(fx.settings().seed == 123u);
    }
}
TEST_CASE("stylize.pixelfx.preparedConfigurationDiagnosticsAndCancellation") {
    PixelFx fx(PixelFxKind::Flamethrower);
    sample(fx, 0.6);
    const auto before   = copy(fx.color());
    const auto defaults = fx.settings();
    struct Invalid {
        PixelFxKind kind;
        double PixelFxFireSettings::* member;
        double                        value;
        const char*                   path;
    };
    for (const auto& bad :
         std::array<Invalid, 5>{{{PixelFxKind::Torch, &PixelFxFireSettings::height, 0, "/options/height"},
                                 {PixelFxKind::Fireball, &PixelFxFireSettings::radius, 21, "/options/radius"},
                                 {PixelFxKind::Flamethrower, &PixelFxFireSettings::speed, 0, "/options/speed"},
                                 {PixelFxKind::FireBreath, &PixelFxFireSettings::spread, 1.1, "/options/spread"},
                                 {PixelFxKind::Flamethrower, &PixelFxFireSettings::reach,
                                  std::numeric_limits<double>::quiet_NaN(), "/options/reach"}}}) {
        auto input             = PixelFx::defaults(bad.kind);
        input.fire.*bad.member = bad.value;
        auto rejected          = PixelFx::prepareConfiguration(bad.kind, input);
        REQUIRE(!rejected);
        REQUIRE(rejected.status().diagnostics().front().code() == eve::DiagnosticCode::InvalidArgument);
        REQUIRE(rejected.status().diagnostics().front().path() == bad.path);
    }
    auto invalid       = defaults;
    invalid.fire.pilot = {0x00ffffffu};
    auto rejected      = PixelFx::prepareConfiguration(fx.info().kind, invalid);
    REQUIRE(!rejected);
    REQUIRE(rejected.status().diagnostics().front().path() == "/options/pilot/0");
    auto missing = PixelFx::prepareConfiguration(static_cast<PixelFxKind>(999), defaults);
    REQUIRE(!missing);
    REQUIRE(missing.status().code() == eve::StatusCode::NotFound);
    {
        auto input     = defaults;
        input.seed     = 987;
        auto cancelled = PixelFx::prepareConfiguration(fx.info().kind, input);
        REQUIRE(cancelled);
        auto token = std::move(cancelled).takeValue();
        // Destroying this candidate cancels; it does not publish.
    }
    REQUIRE(fx.settings().seed == defaults.seed);
    REQUIRE(copy(fx.color()) == before);
}
TEST_CASE("stylize.pixelfx.preparedConfigurationMoveAndEffectIdentity") {
    auto candidate = [] {
        PixelFx destroyedOwner(PixelFxKind::Torch);
        auto    settings = destroyedOwner.settings();
        settings.seed    = 556;
        return PixelFx::prepareConfiguration(PixelFxKind::Torch, settings);
    }();
    REQUIRE(candidate);
    auto    source = std::move(candidate).takeValue();
    auto    moved  = std::move(source);
    PixelFx wrong(PixelFxKind::Candle);
    sample(wrong, 0.2);
    const auto before   = copy(wrong.color());
    auto       mismatch = wrong.commitConfiguration(std::move(moved));
    REQUIRE(!mismatch);
    REQUIRE(mismatch.status().diagnostics().front().path() == "/effect");
    REQUIRE(wrong.settings().seed == 2u);
    REQUIRE(copy(wrong.color()) == before);
    PixelFx target(PixelFxKind::Torch);
    auto    applied = target.commitConfiguration(std::move(moved));
    REQUIRE(applied);
    REQUIRE(target.settings().seed == 556u);
    auto consumed = target.commitConfiguration(std::move(source));
    REQUIRE(!consumed);
    REQUIRE(target.settings().seed == 556u);
}

TEST_CASE("stylize.pixelfx.valueCodecRoundTripAndDescriptors") {
    for (const auto& effectInfo : PixelFx::catalogue()) {
        PixelFx source(effectInfo.kind);
        auto    document = source.configurationValue();
        REQUIRE(document.find("schema")->asString() == "eve.pixelfx.configuration");
        auto described = PixelFx::describeParameters(effectInfo.kind);
        REQUIRE(described);
        const auto* parameters = described.value().find("parameters");
        REQUIRE(parameters->arraySize() == document.find("options")->keys().size());
        for (const auto& parameter : *parameters->getIf<eve::Value::Array>()) {
            const auto& name = parameter.find("name")->asString();
            REQUIRE(parameter.find("default")->operator==(*document.find("options")->find(name)));
            if (name == "seed") {
                REQUIRE(parameter.find("maximum")->isInt64());
                REQUIRE(parameter.find("maximum")->asInt() == int64_t(4294967295u));
            }
        }
        document.find("options")->set("seed", eve::Value(int64_t(123)));
        document.find("options")->set("playbackSpeed", eve::Value(0.75));
        auto json = document.toJson();
        REQUIRE(json);
        auto parsed = eve::Value::fromJson(json.value());
        REQUIRE(parsed);
        auto prepared = PixelFx::prepareConfigurationValue(effectInfo.kind, parsed.value());
        REQUIRE(prepared);
        parsed.value() = eve::Value();  // The token owns all decoded configuration data.
        PixelFx target(effectInfo.kind);
        auto    applied = target.commitConfiguration(std::move(prepared).takeValue());
        REQUIRE(applied);
        REQUIRE(target.configurationValue() == document);
        auto typed          = source.settings();
        typed.seed          = 123;
        typed.playbackSpeed = 0.75;
        source.configure(typed);
        sample(source, 0.4);
        sample(target, 0.4);
        REQUIRE(copy(source.color()) == copy(target.color()));
        REQUIRE(copy(source.light()) == copy(target.light()));
    }
}
TEST_CASE("stylize.pixelfx.actor.stockGeometryAndBody") {
    const auto hash = [](auto values) {
        uint64_t h = 14695981039346656037ull;
        for (auto value : values)
            for (int c = 0; c < 4; ++c) h = (h ^ uint64_t((uint32_t(value) >> (c * 8)) & 255u)) * 1099511628211ull;
        return h;
    };
    // Exact integer data fingerprints from pinned core/actor.js stockActor(1/2).
    const uint64_t pixelHash[]{0xb5d2d2ea04ae5334ull, 0xf4ba7246677aa4efull};
    const uint64_t maskHash[]{0x969f1c15d7473f44ull, 0x65edc7e2ce676015ull};
    size_t         variant = 0;
    for (auto source : {PixelFxActorSource::Hero, PixelFxActorSource::Dummy}) {
        PixelFx fx(PixelFxKind::Halo);
        auto    settings      = fx.settings();
        settings.actor.source = source;
        auto prepared         = PixelFx::prepareConfiguration(fx.info().kind, settings);
        REQUIRE(prepared);
        auto published = fx.commitConfiguration(std::move(prepared).takeValue());
        REQUIRE(published);
        const auto actor = fx.settings().actor;
        REQUIRE(actor.width == (variant == 0 ? 12 : 24));
        REQUIRE(actor.height == (variant == 0 ? 19 : 38));
        REQUIRE(actor.x == -actor.width / 2);
        REQUIRE(actor.y == -actor.height);
        REQUIRE(actor.head[0] == 0);
        REQUIRE(actor.head[1] == actor.y);
        REQUIRE(actor.chest[1] == (variant == 0 ? -8 : -26));
        REQUIRE(actor.eyes.size() == (variant == 0 ? 0u : 2u));
        REQUIRE(hash(actor.pixels) == pixelHash[variant]);
        REQUIRE(hash(actor.mask) == maskHash[variant]);
        std::vector<uint32_t> body(size_t(fx.info().width) * size_t(fx.info().height), 0x12345678u);
        auto                  drawn = fx.drawActor(body);
        REQUIRE(drawn);
        size_t nonzero = 0;
        for (int y = 0; y < actor.height; ++y)
            for (int x = 0; x < actor.width; ++x) {
                const auto expected = actor.pixels[size_t(y) * size_t(actor.width) + size_t(x)];
                const auto actual   = body[size_t(int(actor.y) + y + fx.info().originY) * size_t(fx.info().width) +
                                           size_t(int(actor.x) + x + fx.info().originX)];
                REQUIRE(actual == expected);
                nonzero += expected != 0 ? 1u : 0u;
            }
        REQUIRE(size_t(std::count_if(body.begin(), body.end(), [](uint32_t c) { return c != 0; })) == nonzero);
        std::array<uint32_t, 1> wrong{0x12345678u};
        auto                    rejected = fx.drawActor(wrong);
        REQUIRE(!rejected);
        REQUIRE(wrong[0] == 0x12345678u);
        REQUIRE(fx.effectTime() == 0);
        ++variant;
    }
}
TEST_CASE("stylize.pixelfx.actor.customOwnershipRoundTripAndBounds") {
    PixelFx fx(PixelFxKind::Halo);
    auto    settings = fx.settings();
    settings.actor   = {};
    auto& a          = settings.actor;
    a.source         = PixelFxActorSource::Custom;
    a.width          = 3;
    a.height         = 1;
    a.x              = -2.5;
    a.y              = -10.25;
    a.head           = {0, -11};
    a.chest          = {0, -10};
    a.mask           = {1, 0, 1};
    a.pixels         = {0xff123456u, 0xffabcdefu, 0u};
    a.eyes           = {{{1, 0}}};
    auto prepared    = PixelFx::prepareConfiguration(fx.info().kind, settings);
    REQUIRE(prepared);
    a.pixels.assign(3, 0u);
    a.mask.assign(3, 0u);  // Pending candidate must own both independent arrays.
    auto published = fx.commitConfiguration(std::move(prepared).takeValue());
    REQUIRE(published);
    REQUIRE(fx.settings().actor.pixels[0] == 0xff123456u);
    REQUIRE(fx.settings().actor.mask[1] == 0u);
    sample(fx, .25);  // Height-one custom input must not read a nonexistent next row.
    const auto before = copy(fx.color()), light = copy(fx.light());
    auto       document = fx.configurationValue();
    auto       json     = document.toJson();
    REQUIRE(json);
    auto parsed = eve::Value::fromJson(json.value());
    REQUIRE(parsed);
    auto restored = PixelFx::prepareConfigurationValue(fx.info().kind, parsed.value());
    REQUIRE(restored);
    parsed.value() = eve::Value();
    PixelFx target(fx.info().kind);
    auto    applied = target.commitConfiguration(std::move(restored).takeValue());
    REQUIRE(applied);
    REQUIRE(target.configurationValue() == document);
    sample(target, .25);
    REQUIRE(copy(target.color()) == before);
    REQUIRE(copy(target.light()) == light);
    std::vector<uint32_t> body(size_t(fx.info().width) * size_t(fx.info().height));
    auto                  drawn = fx.drawActor(body);
    REQUIRE(drawn);
    REQUIRE(body[59u * 56u + 25u] == 0xff123456u);
    REQUIRE(body[59u * 56u + 26u] == 0xffabcdefu);
    REQUIRE(body[59u * 56u + 27u] == 0u);
    const auto reject = [&](eve::Value doc, std::string path) {
        auto bad = PixelFx::prepareConfigurationValue(fx.info().kind, doc);
        REQUIRE(!bad);
        REQUIRE(bad.status().diagnostics().front().path() == path);
        REQUIRE(fx.configurationValue() == document);
        REQUIRE(copy(fx.color()) == before);
    };
    auto invalid = document;
    invalid.find("options")->find("actor")->set("width", eve::Value(257));
    reject(invalid, "/options/actor/width");
    invalid = document;
    invalid.find("options")->find("actor")->set("height", eve::Value(0));
    reject(invalid, "/options/actor/height");
    invalid = document;
    invalid.find("options")->find("actor")->set("mask", eve::Value::array({eve::Value(1)}));
    reject(invalid, "/options/actor/mask");
    invalid                                                     = document;
    invalid.find("options")->find("actor")->find("mask")->at(1) = eve::Value(2);
    reject(invalid, "/options/actor/mask/1");
    invalid                                                       = document;
    invalid.find("options")->find("actor")->find("pixels")->at(0) = eve::Value(-1);
    reject(invalid, "/options/actor/pixels/0");
    invalid = document;
    invalid.find("options")->find("actor")->set("x", eve::Value(std::numeric_limits<double>::infinity()));
    reject(invalid, "/options/actor/x");
    invalid = document;
    invalid.find("options")->find("actor")->set("eyes",
                                                eve::Value::array({eve::Value::array({eve::Value(3), eve::Value(0)})}));
    reject(invalid, "/options/actor/eyes/0/0");
    invalid = document;
    invalid.find("options")->find("actor")->set("version", eve::Value(2));
    reject(invalid, "/options/actor/version");
    invalid = document;
    invalid.find("options")->find("actor")->set("a/b~", eve::Value(1));
    reject(invalid, "/options/actor/a~1b~0");
    invalid = document;
    invalid.find("options")->set("actor", eve::Value::object({{"schema", eve::Value("eve.pixelfx.actor")},
                                                              {"version", eve::Value(1)},
                                                              {"source", eve::Value("hero")},
                                                              {"x", eve::Value(0)}}));
    reject(invalid, "/options/actor/x");
    // A fresh disabled source clears stock/custom derived geometry at publication.
    settings              = fx.settings();
    settings.actor.source = PixelFxActorSource::None;
    auto disabled         = PixelFx::prepareConfiguration(fx.info().kind, settings);
    REQUIRE(disabled);
    auto cleared = fx.commitConfiguration(std::move(disabled).takeValue());
    REQUIRE(cleared);
    REQUIRE(fx.settings().actor.pixels.empty());
    REQUIRE(fx.settings().actor.width == 0);
    auto empty = fx.drawActor(body);
    REQUIRE(empty);
    REQUIRE(!any(body));
}

TEST_CASE("stylize.pixelfx.actor.inactiveInputCannotPublishGeometry") {
    PixelFx fx(PixelFxKind::Sunburst);
    auto    settings      = fx.settings();
    settings.actor.source = PixelFxActorSource::Custom;
    settings.actor.width  = 256;
    settings.actor.height = 256;  // Inactive input intentionally has no planes.
    auto prepared         = PixelFx::prepareConfiguration(fx.info().kind, settings);
    REQUIRE(prepared);
    auto applied = fx.commitConfiguration(std::move(prepared).takeValue());
    REQUIRE(applied);
    REQUIRE(fx.settings().actor.source == PixelFxActorSource::None);
    REQUIRE(fx.settings().actor.width == 0);
    std::vector<uint32_t> body(size_t(fx.info().width) * size_t(fx.info().height), 0x12345678u);
    auto                  drawn = fx.drawActor(body);
    REQUIRE(drawn);
    REQUIRE(!any(body));
    auto document = fx.configurationValue();
    REQUIRE(!document.find("options")->find("actor"));
}

TEST_CASE("stylize.pixelfx.valueCodecRejectsInvalidEnvelopeAndOptions") {
    PixelFx fx(PixelFxKind::Flamethrower);
    sample(fx, 0.6);
    const auto before = copy(fx.color()), defaults = copy(fx.light());
    const auto check = [&](const eve::Value& value, const std::string& path) {
        auto rejected = PixelFx::prepareConfigurationValue(fx.info().kind, value);
        REQUIRE(!rejected);
        REQUIRE(rejected.status().diagnostics().front().path() == path);
        REQUIRE(copy(fx.color()) == before);
        REQUIRE(copy(fx.light()) == defaults);
    };
    auto doc = fx.configurationValue();
    doc.set("version", eve::Value(2));
    check(doc, "/version");
    doc = fx.configurationValue();
    doc.set("schema", eve::Value("wrong"));
    check(doc, "/schema");
    doc = fx.configurationValue();
    doc.set("effect", eve::Value("fire/torch"));
    check(doc, "/effect");
    doc = fx.configurationValue();
    doc.set("a/b~", eve::Value(1));
    check(doc, "/a~1b~0");
    doc = fx.configurationValue();
    doc.find("options")->set("height", eve::Value(50));
    check(doc, "/options/height");
    doc = fx.configurationValue();
    doc.find("options")->set("seed", eve::Value(1.5));
    check(doc, "/options/seed");
    doc = fx.configurationValue();
    doc.find("options")->set("reach", eve::Value(0));
    check(doc, "/options/reach");
    doc = fx.configurationValue();
    doc.find("options")->set("speed", eve::Value(std::numeric_limits<double>::infinity()));
    check(doc, "/options/speed");
    doc = fx.configurationValue();
    doc.find("options")->set("pilot", eve::Value::array({eve::Value(0x00ffffff)}));
    check(doc, "/options/pilot/0");
    doc = fx.configurationValue();
    doc.find("options")->set("pilot", eve::Value::array({eve::Value("#ffffff")}));
    check(doc, "/options/pilot/0");
    check(eve::Value::array({}), "/");
    doc = fx.configurationValue();
    doc.find("options")->set("pilot",
                             eve::Value::array({eve::Value(int64_t(0xff000000u)), eve::Value(int64_t(0xffffffffu))}));
    auto valid = PixelFx::prepareConfigurationValue(fx.info().kind, doc);
    REQUIRE(valid);
    auto applied = fx.commitConfiguration(std::move(valid).takeValue());
    REQUIRE(applied);
    REQUIRE(fx.settings().fire.pilot.size() == 3u);
    auto empty = fx.configurationValue();
    empty.set("options", eve::Value::object({}));
    auto reset = PixelFx::prepareConfigurationValue(fx.info().kind, empty);
    REQUIRE(reset);
    auto resetApplied = fx.commitConfiguration(std::move(reset).takeValue());
    REQUIRE(resetApplied);
    REQUIRE(fx.configurationValue() == PixelFx(PixelFxKind::Flamethrower).configurationValue());
}

namespace {
void step(PixelFx& fx, double delta) {
    auto result = fx.advance(delta);
    if (!result) throw std::runtime_error(result.status().describe());
}
}  // namespace
TEST_CASE("stylize.pixelfx.playback.fixedTickCadenceAndFractionRetention") {
    PixelFx fixed(PixelFxKind::FlamePillar), slow(PixelFxKind::FlamePillar), half(PixelFxKind::FlamePillar),
        jitter(PixelFxKind::FlamePillar);
    REQUIRE(fixed.isSimulated());
    step(half, 1.0 / 240);
    REQUIRE(half.activeTickCount() == 0u);
    REQUIRE(std::abs(half.effectTime() - 1.0 / 240) < 1e-12);
    step(half, 1.0 / 240);
    REQUIRE(half.activeTickCount() == 1u);
    for (int i = 0; i < 180; ++i) step(fixed, 1.0 / 120);
    for (int i = 0; i < 45; ++i) step(slow, 1.0 / 30);
    for (int i = 0; i < 358; ++i) step(half, 1.0 / 240);
    for (int i = 0; i < 60; ++i)
        for (double delta : {0.001, 0.003, 0.008, 0.013}) step(jitter, delta);
    fixed.draw();
    slow.draw();
    half.draw();
    jitter.draw();
    REQUIRE(any(fixed.color()));
    REQUIRE(fixed.activeTickCount() == 180u);
    REQUIRE(slow.activeTickCount() == 180u);
    REQUIRE(half.activeTickCount() == 180u);
    REQUIRE(jitter.activeTickCount() == 180u);
    REQUIRE(std::abs(jitter.effectTime() - 1.5) < 1e-12);
    REQUIRE(copy(fixed.color()) == copy(slow.color()));
    REQUIRE(copy(fixed.light()) == copy(slow.light()));
    REQUIRE(copy(fixed.color()) == copy(half.color()));
    REQUIRE(copy(fixed.light()) == copy(half.light()));
    REQUIRE(copy(fixed.color()) == copy(jitter.color()));
    REQUIRE(copy(fixed.light()) == copy(jitter.light()));
}
TEST_CASE("stylize.pixelfx.playback.seekPublicationAndConfigurationGenerations") {
    static_assert(!std::is_default_constructible_v<PixelFxPreparedSeek>);
    static_assert(!std::is_copy_constructible_v<PixelFxPreparedSeek>);
    PixelFx fx(PixelFxKind::FlamePillar);
    step(fx, 0.8);
    fx.draw();
    const auto before = copy(fx.color());
    auto       sought = fx.prepareSeek(1.25);
    REQUIRE(sought);
    REQUIRE(std::abs(fx.effectTime() - 0.8) < 1e-12);
    REQUIRE(copy(fx.color()) == before);
    auto token     = std::move(sought).takeValue();
    auto published = fx.commitSeek(std::move(token));
    REQUIRE(published);
    REQUIRE(fx.effectTime() == 1.25);
    REQUIRE(copy(fx.color()) == before);
    fx.draw();
    PixelFx expected(PixelFxKind::FlamePillar);
    step(expected, 1.25);
    expected.draw();
    REQUIRE(copy(fx.color()) == copy(expected.color()));
    REQUIRE(copy(fx.light()) == copy(expected.light()));
    auto consumed = fx.commitSeek(std::move(token));
    REQUIRE(!consumed);
    auto old = fx.prepareSeek(0.5);
    REQUIRE(old);
    auto       oldToken = std::move(old).takeValue();
    const auto current  = copy(fx.color());
    fx.configure(fx.settings());
    REQUIRE(fx.effectTime() == 0);
    REQUIRE(copy(fx.color()) == current);
    auto stale = fx.commitSeek(std::move(oldToken));
    REQUIRE(!stale);
    REQUIRE(stale.status().diagnostics().front().code() == eve::DiagnosticCode::StaleHandle);
    REQUIRE(fx.effectTime() == 0);
    REQUIRE(copy(fx.color()) == current);
    auto valid = fx.prepareSeek(0.625);
    REQUIRE(valid);
    PixelFx moved(std::move(fx));
    auto    afterMove = moved.commitSeek(std::move(valid).takeValue());
    REQUIRE(afterMove);
    moved.draw();
    REQUIRE(moved.activeTickCount() == 75u);
    REQUIRE(any(moved.color()));
}
TEST_CASE("stylize.pixelfx.playback.invalidDeltaStopAndRestart") {
    PixelFx fx(PixelFxKind::Halo);
    step(fx, 0.2);
    fx.draw();
    const auto before = copy(fx.color()), light = copy(fx.light());
    for (double invalid :
         {-1.0, 61.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
        auto rejected = fx.advance(invalid);
        REQUIRE(!rejected);
        REQUIRE(fx.effectTime() == 0.2);
        REQUIRE(copy(fx.color()) == before);
        REQUIRE(copy(fx.light()) == light);
    }
    fx.stop();
    step(fx, 0.15);
    fx.draw();
    REQUIRE(!fx.done());
    REQUIRE(any(fx.light()));
    fx.stop();
    step(fx, 0.15);
    fx.draw();
    REQUIRE(fx.done());
    REQUIRE(!any(fx.color()));
    REQUIRE(!any(fx.light()));
    auto restart = fx.prepareSeek(0);
    REQUIRE(restart);
    auto applied = fx.commitSeek(std::move(restart).takeValue());
    REQUIRE(applied);
    REQUIRE(!fx.done());
    fx.draw();
    REQUIRE(any(fx.color()));
    auto settings          = fx.settings();
    settings.playbackSpeed = 64;
    fx.configure(settings);
    fx.render(.2);
    const auto retained  = copy(fx.color());
    const auto clock     = fx.effectTime();
    bool       oversized = false;
    try {
        fx.render(1e12);
    } catch (const std::invalid_argument&) {
        oversized = true;
    }
    REQUIRE(oversized);
    REQUIRE(fx.effectTime() == clock);
    REQUIRE(copy(fx.color()) == retained);
}
TEST_CASE("stylize.pixelfx.playback.ownerDestructionAndTerminalClock") {
    auto orphan = [] {
        PixelFx owner(PixelFxKind::FlamePillar);
        return owner.prepareSeek(0.625);
    }();
    REQUIRE(orphan);
    PixelFx target(PixelFxKind::FlamePillar);
    auto    token    = std::move(orphan).takeValue();
    auto    mismatch = target.commitSeek(std::move(token));
    REQUIRE(!mismatch);
    REQUIRE(mismatch.status().diagnostics().front().code() == eve::DiagnosticCode::StaleHandle);
    REQUIRE(target.effectTime() == 0);
    step(target, 60);
    target.draw();
    REQUIRE(target.effectTime() == 60);
    REQUIRE(target.activeTickCount() == 7200u);
    REQUIRE(target.done());
    REQUIRE(!any(target.color()));
    REQUIRE(!any(target.light()));
    auto excessive = target.prepareSeek(1e12);
    REQUIRE(!excessive);
    REQUIRE(target.effectTime() == 60);
    REQUIRE(target.activeTickCount() == 7200u);
    const auto pixels   = copy(target.color());
    bool       rejected = false;
    try {
        target.render(0.5);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    REQUIRE(rejected);
    REQUIRE(target.effectTime() == 60);
    REQUIRE(copy(target.color()) == pixels);
}

}  // namespace test_pixelfx_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_pixelfx_core)
#include <array>
#include <cmath>
#include <vector>
#include "PixelFxColorReference.h"
#include "stylize/pixelfx/Explosion.h"
#include "stylize/pixelfx/Rig.h"
#include "stylize/pixelfx/Simulation.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
namespace test_pixelfx_core_unit {
// Reference: pixelfx a84159688af3ca37a4be05a0d422b94451b2e24e core/math.js and sim.js.
namespace {
using namespace eve::stylize;
using namespace eve::stylize::pixelfx;
std::array<uint64_t, 5> metrics(std::span<const uint32_t> pixels) {
    std::array<uint64_t, 5> out{};
    for (auto p : pixels) {
        out[0] += p != 0 ? 1u : 0u;
        for (size_t i = 0; i < 4; ++i) out[i + 1] += (p >> (i * 8)) & 255u;
    }
    return out;
}
}  // namespace
TEST_CASE("stylize.pixelfx.core.colorReferenceMeasuredRoundingBoundaries") {
    using test_pixelfx_reference::matchesColorReference;
    using test_pixelfx_reference::RoundingPixel;
    constexpr std::array<uint32_t, 3> expected{{0xff102030u, 0x555f6981u, 0xff405060u}};
    constexpr RoundingPixel           reference{1, expected[1], 0b010};
    constexpr uint32_t                hash = 0x39eb7943u;  // Independent three-pixel RGBA fixture FNV-1a.
    REQUIRE(matchesColorReference(expected, hash, reference));
    auto actual = expected;
    actual[1] += 0x100u;
    const auto unchanged = actual;
    REQUIRE(matchesColorReference(actual, hash, reference));
    REQUIRE(actual == unchanged);
    for (uint32_t delta : {0x200u, 1u, 0x10000u, 0x1000000u, 0x101u}) {
        actual = expected;
        actual[1] += delta;
        REQUIRE(!matchesColorReference(actual, hash, reference));
    }
    actual = expected;
    actual[1] -= 0x100u;
    REQUIRE(!matchesColorReference(actual, hash, reference));
    actual    = expected;
    actual[1] = 0;
    REQUIRE(!matchesColorReference(actual, hash, reference));
    actual = expected;
    actual[0] += 0x100u;
    REQUIRE(!matchesColorReference(actual, hash, reference));
    actual = expected;
    actual[2] += 1u;
    REQUIRE(!matchesColorReference(actual, hash, reference));
    REQUIRE(!matchesColorReference(expected, hash, {expected.size(), expected[1], 0b010}));
    REQUIRE(!matchesColorReference(expected, hash, {1, expected[1], 0b1000}));
    actual    = expected;
    actual[1] = 0xfff1ffd7u;
    REQUIRE(matchesColorReference(actual, 0xcaac2ebfu, {1, 0xfff1ffd7u, 0b101}));
    actual[1] += 0x10001u;
    REQUIRE(matchesColorReference(actual, 0xcaac2ebfu, {1, 0xfff1ffd7u, 0b101}));
    actual[1] += 0x100u;
    REQUIRE(!matchesColorReference(actual, 0xcaac2ebfu, {1, 0xfff1ffd7u, 0b101}));
    constexpr std::array<RoundingPixel, 2> multiple{{{0, expected[0], 0, 0b001}, {1, expected[1], 0b010}}};
    actual = expected;
    actual[0] -= 1u;
    actual[1] += 0x100u;
    REQUIRE(matchesColorReference(actual, hash, multiple));
    actual[0] -= 1u;
    REQUIRE(!matchesColorReference(actual, hash, multiple));
    constexpr std::array<RoundingPixel, 2> duplicate{{reference, reference}};
    REQUIRE(!matchesColorReference(expected, hash, duplicate));
    constexpr std::array<RoundingPixel, 2> reversed{{reference, {0, expected[0], 0b001}}};
    REQUIRE(!matchesColorReference(expected, hash, reversed));
    REQUIRE(!matchesColorReference(expected, hash, {1, expected[1], 0, 0b1000}));
    REQUIRE(!matchesColorReference(expected, hash, {1, expected[1], 0b010, 0b010}));
}
TEST_CASE("stylize.pixelfx.core.mathReference") {
    constexpr uint32_t expectedRng[] = {50271532u,   266108690u,  4195786334u, 3002305430u, 2239590375u, 1741702388u,
                                        2002453909u, 1030470827u, 2376515373u, 3134562008u, 1107309403u, 669793123u};
    Rng                rng(7);
    for (auto expected : expectedRng) REQUIRE(uint32_t(rng.next() * 4294967296.0) == expected);
    constexpr double inputs[]       = {-2.3, -0.01, 0, 0.7, 2.4};
    constexpr double expected[5][6] = {
        {0.4473677115067838, -0.4558216773875087, -0.20027834143133408, 0.16060917486745674, 0.10400191374110333,
         0.12084107916295306},
        {-0.9944854583611116, 0.5311356980000104, 0.5240296969253488, 0.14031535954292385, -0.05458151984245263,
         -0.39811032325311146},
        {-0.9946577907539904, 0.5312492563389242, 0.5244974430783519, 0.14022585423663259, -0.05369186820462346,
         -0.41781170084141195},
        {0.016192040618508985, 0.3812467827569166, 0.3485233093389212, -0.35970817715701764, -0.4802116227393877,
         0.1208410791629495},
        {-0.16501995260640978, -0.16720033696095782, -0.272659931588243, -0.08065057257828467, -0.27891651042609833,
         -0.32367807118199554},
    };
    for (size_t i = 0; i < 5; ++i) {
        const double                x = inputs[i];
        const std::array<double, 6> actual{noise(x, 7),
                                           noise2(x, x * 0.7, 11),
                                           fbm(x, x * 0.7, 11),
                                           tileNoise(x, x * 0.7, 4, 3, 9),
                                           tileFbm(x, x * 0.7, 4, 3, 9),
                                           loopNoise(x, 1.5, 3)};
        for (size_t j = 0; j < 6; ++j) REQUIRE(std::abs(actual[j] - expected[i][j]) < 1e-12);
    }
    REQUIRE(jsUint(-1) == 0xffffffffu);
    REQUIRE(rgb(-1, 256, 257) == 0xff0100ffu);
}
TEST_CASE("stylize.pixelfx.core.simulationReference") {
    constexpr double expected[4][12] = {
        {12, 18, -306, 84, -282, 5.04, 5.04, 25.2, 749.2944597732276, 503953399, 0, 0},
        {27, 252.83917742320392, -701.4790994448782, 72.05937116635342, 39.86279107446297, 4.816866468141474,
         8.45019980147481, 44.839728786990364, 1435.76334703248, 1440342454, 0, 0},
        {10, 143.70096373193425, -250.1596766914958, -4.143367979082329, -137.4902727993152, 8.528152950908995,
         10.85315295090899, 12.081816901477207, 622.8311025071889, 1496707351, 1, 14.489117958273388},
        {9, 123.77429914651583, -330.1421451726969, -7.004858463062096, -130.8387958524043, 3.088152950909005,
         10.213152950908988, 8.881816901477208, 523.7565791001543, 1496707351, 1, 14.489117958273388},
    };
    constexpr uint64_t planeExpected[4][2][5] = {
        {{40u, 9610u, 7291u, 4177u, 9328u}, {253u, 6448u, 4904u, 2620u, 64515u}},
        {{127u, 29259u, 20948u, 10459u, 23992u}, {482u, 10512u, 7387u, 2895u, 122910u}},
        {{25u, 2732u, 2501u, 2665u, 2363u}, {0u, 0u, 0u, 0u, 0u}},
        {{31u, 1231u, 1088u, 1380u, 2229u}, {0u, 0u, 0u, 0u, 0u}},
    };
    const PixelFxInfo     info{PixelFxKind::Halo, "probe", 96, 96, 48, 48, 2, false};
    std::vector<uint32_t> color(96 * 96), light(96 * 96);
    Rng                   rng(123);
    Particles             ps;
    for (int k = 0; k < 12; ++k)
        ps.spawn(rng, ParticleKind(k), (k - 5) * 3, -20 - k, 18 - k * 2, -40 + k * 3, 0.2 + k * 0.04, hex(0xffe26b),
                 1 + k * 0.2, k == 0 ? 0.02 : 0,
                 k == int(ParticleKind::Fire) ? ParticleFlag::Smokes | ParticleFlag::Big : ParticleFlag::None);
    int    lands      = 0;
    double landX      = 0;
    size_t checkpoint = 0;
    for (int tick = 0; tick <= 144; ++tick) {
        if (tick == 0 || tick == 24 || tick == 72 || tick == 144) {
            std::array<double, 12> actual{};
            actual[0]  = double(ps.list.size());
            actual[9]  = rng.state;
            actual[10] = lands;
            actual[11] = landX;
            for (const auto& q : ps.list) {
                const std::array<double, 8> values{q.x, q.y, q.vx, q.vy, q.life, q.max, q.size, q.seed};
                for (size_t i = 0; i < values.size(); ++i) actual[i + 1] += values[i];
            }
            for (size_t i = 0; i < actual.size(); ++i) REQUIRE(std::abs(actual[i] - expected[checkpoint][i]) < 1e-8);
            std::fill(color.begin(), color.end(), 0u);
            std::fill(light.begin(), light.end(), 0u);
            Paint painter{info, color, light, 1};
            ps.draw(painter);
            const std::array planes{metrics(color), metrics(light)};
            for (size_t plane = 0; plane < 2; ++plane)
                for (size_t m = 0; m < 5; ++m) {
                    const auto a = planes[plane][m], b = planeExpected[checkpoint][plane][m],
                               delta = a > b ? a - b : b - a;
                    REQUIRE(delta <= (m == 0 ? 4u : 1020u));
                }
            ++checkpoint;
        }
        if (tick < 144)
            ps.update(
                1.0 / 120, rng, [](double) { return 0.0; },
                [&](double x, double) {
                    ++lands;
                    landX += x;
                });
    }
}
TEST_CASE("stylize.pixelfx.core.painterAndFonts") {
    const PixelFxInfo     info{PixelFxKind::Halo, "probe", 64, 64, 32, 32, 1, false};
    std::vector<uint32_t> color(64 * 64), light(64 * 64);
    Paint                 p{info, color, light, 1};
    p.blend(0, 0, hex(0xff0000), 0.5);
    REQUIRE((p.get(0, 0) >> 24) == 127u);
    REQUIRE((p.get(0, 0) & 0xffffffu) == 255u);
    p.blend(0, 0, hex(0x0000ff), 0.5);
    REQUIRE((p.get(0, 0) >> 24) == 191u);
    p.place(3, -2, true, 0);
    p.px(2, 1, WHITE);
    p.place(0, 0);
    REQUIRE(p.get(0, -1) == WHITE);
    p.circle(-12, 0, 5, [](double d) { return d < 0.5 ? WHITE : hex(0xff8000); });
    p.beam(-20, -10, 20, -10, 3, [](double u, double) { return u < 0.5 ? WHITE : hex(0x00ff00); });
    p.ellipse(15, 0, 6, 3, WHITE);
    p.ring(0, 12, 6, 2, WHITE);
    p.arc(0, -16, 6, [](double u) { return 1 + u * 2; }, 0, PI, WHITE);
    const auto pts = star(-12, 14, 2, 5);
    p.poly(pts, WHITE);
    p.tri(14, 10, 20, 15, 12, 16, WHITE);
    p.glowRing(0, 0, 8, 4, hex(0x40a0ff));
    p.glowLine(-20, 20, 20, 20, 4, WHITE);
    REQUIRE(textWidth("I x 27", PixelFont::Mini) == 21);
    REQUIRE(textWidth("", PixelFont::Big) == 0);
    REQUIRE(textWidth("\xE4\xB8\xAD", PixelFont::Mini) == 2);
    TextOptions options;
    options.outline = BLACK;
    options.shadow  = hex(0x303030);
    p.text("27", -4, 24, WHITE, options);
    const std::array<uint8_t, 4> maskData{1, 0, 0, 1};
    const MaskView               mask{2, 2, maskData};
    p.mask(mask, 24, 20, WHITE);
    p.outline(mask, 24, 20, hex(0xff0000));
    REQUIRE(p.get(24, 20) == WHITE);
    REQUIRE(p.get(25, 21) == WHITE);
    REQUIRE(p.get(23, 20) == hex(0xff0000));
    REQUIRE(metrics(color)[0] > 100u);
    REQUIRE(metrics(light)[0] > 100u);
}
// Public pinned rig.js APIs supply these skeletal fixtures; strict sparse snapshots are compared separately.
TEST_CASE("stylize.pixelfx.core.rigPoseReferenceAndRaster") {
    namespace rig                      = eve::stylize::pixelfx::rig;
    constexpr double joints[17][20][3] = {
        {
            // stand
            {0, -19.699999999999999, 0},
            {0, -22.5, 0},
            {0, -27.100000000000001, 0},
            {0, -31.100000000000001, 0},
            {0.12664918882530227, -34.200000000000003, 0.08037401924684949},
            {0.12664918882530227, -37.5, 0.08037401924684949},
            {-3.4274648393409368, -28.33735130331883, 4.7495731462078501},
            {-5.8737071133053584, -21.430843009934904, 6.7684941279424997},
            {-3, -15, 4.5},
            {3.6024281898031751, -28.318313282678812, -4.7492169001586433},
            {1.5885048149376941, -21.225082852756668, -6.5904650388398309},
            {5, -15, -4.5},
            {-1.6457987064189248, -18.599999999999998, 2.2652475842498525},
            {-0.66992880004577127, -10.234255447385188, 4.003636495348541},
            {-2, -2.1999999999999993, 2.3999999999999999},
            {1.999676675724007, -1.0999999999999992, 4.6721325862874616},
            {1.6457987064189248, -18.599999999999998, -2.2652475842498525},
            {4.1707283425601318, -10.393451888470297, -1.7781147217943356},
            {3, -2.1999999999999993, -2.3999999999999999},
            {7.4710206026613122, -1.0999999999999992, -1.318346279728086},
        },
        {
            // pray
            {0, -19.699999999999999, 0},
            {0, -22.5, 0},
            {0, -27.100000000000001, 0},
            {0, -31.100000000000001, 0},
            {0.48868083617745017, -34.163277997030718, 0.10009472144116265},
            {0.89084966941443677, -37.438680297447078, 0.10009472144116265},
            {-5.258123424362469, -29.600000000000001, 2.6762171164109065},
            {-7.6845390580994337, -23.046585170617274, 5.6637346889756932},
            {-1, -26, 4.5},
            {5.258123424362469, -29.600000000000001, -2.6762171164109065},
            {1.2415755262407258, -23.287330893077652, -1.3429685542133005},
            {2, -26, 5.5},
            {-2.086075364733516, -18.599999999999998, 1.8676963277395839},
            {-1.7951551571098772, -10.262954389655102, 3.9579202209685089},
            {-3, -2.1999999999999993, 2.3999999999999999},
            {0.67870143664353844, -1.0999999999999992, 5.1617305697762692},
            {2.086075364733516, -18.599999999999998, -1.8676963277395839},
            {4.6091462144088338, -10.400167778613367, -1.2703145163088734},
            {4, -2.1999999999999993, -2.3999999999999999},
            {8.2974308935708034, -1.0999999999999992, -0.75924172560878445},
        },
        {
            // crouch
            {0.60000000000000009, -11.699999999999999, 0},
            {0.88527070956383591, -14.485430060558862, 0},
            {1.9196244403781246, -18.967629560754112, 0},
            {2.8190624671731586, -22.865194343532593, 0},
            {3.1793326288649948, -25.946328469603444, 0.096529310347404473},
            {3.4410102858080105, -29.235937079127808, 0.096529310347404473},
            {-3.1120647724851027, -20.933941344170382, 3.2870407400759367},
            {-6.5235299033893011, -14.775392930651414, 6.1495897962438519},
            {-9, -8, 4.5},
            {7.5370647405179563, -19.651828733848951, -3.2073305792673357},
            {5.4460742522103232, -12.758090885373424, -5.6289286578722475},
            {11, -8, -4.5},
            {-1.4679129824054566, -10.71560533662708, 1.9505648012619026},
            {-2.8870586091887107, -5.0654356215237053, 8.2768281439082241},
            {-8, -2.2000000000000011, 2.3999999999999999},
            {-4.2553314112193483, -1.100000000000001, 5.0716019838665183},
            {2.5432404417778196, -10.487930059147882, -1.9505648012619026},
            {10.291269726006778, -9.3541572697408597, 1.6050788075283102},
            {9, -2.1999999999999993, -2.3999999999999999},
            {13.335999785006656, -1.0999999999999992, -0.86406189433876568},
        },
        {
            // air
            {-0.6118737498275908, -19.699999999999999, -0.51537414979015284},
            {-0.89867323878192762, -22.474777651254946, -0.75694202685913992},
            {-1.9333478526037382, -26.871361091375917, -1.6284364316413749},
            {-2.8330649081009645, -30.694477126263717, -2.3862576531911439},
            {-2.4583661770815737, -33.746545386401387, -1.9655675101729513},
            {-2.1064834935958974, -37.014317334341214, -1.6691808145867988},
            {-7.9037955668408166, -27.322597432153863, -0.44128419061905788},
            {-10.307059141007414, -20.124694240737664, -0.023535653716195704},
            {-5.1935061544230745, -15, 1.509136781067125},
            {3.5126188492056434, -28.594645392163368, -3.2427144555038834},
            {4.4124769603323761, -21.066726732808476, -3.7725220035587452},
            {6.7231905289920526, -15, -0.22070140659174298},
            {-3.2637337473239487, -18.48035811804867, 0.21271391732043798},
            {-3.7635316100483194, -12.791255064451439, 6.6426679250849414},
            {-3.0758068239394354, -7.1999999999999993, 0.54718587500739035},
            {-2.4732466303312335, -2.800966516284487, 2.1767335491269915},
            {2.1654372523103609, -18.725764775539123, -1.1377962933737096},
            {5.6920477456215473, -12.486040256648224, 3.6149772502341868},
            {3.840649011223924, -5.1999999999999993, 0.097031812230300662},
            {4.869031256908734, -0.80096651628448701, 1.4973654745801079},
        },
        {
            // turned
            {0, -19.699999999999999, 0},
            {0, -22.5, 0},
            {0, -27.100000000000001, 0},
            {0, -31.100000000000001, 0},
            {0.12943848002573088, -34.194117823219862, -0.20537910293364683},
            {0.40963220443226078, -37.469520123636222, -0.49387736502615365},
            {-5.423059096797, -29.642368369581419, -2.6712662980216901},
            {-10.418702861475875, -29.503239959008397, 3.054481396541572},
            {-14.936333549456569, -29.994967356431225, 8.8947910744301417},
            {4.2799420063572651, -29.600000000000001, 4.0610462226153681},
            {8.5102674270230327, -23.308760746311453, 4.5945773727063006},
            {8.8017560838251772, -25, -2.6036685351339375},
            {-1.2422035828028666, -18.599999999999998, -2.509368498023302},
            {-2.7242379373797911, -10.128926928430042, -2.5764146610462744},
            {-0.32824119946452357, -2.1999999999999993, -3.1068082842322426},
            {-4.4578736369700769, -1.0999999999999992, -1.0804480173739663},
            {1.2422035828028666, -18.599999999999998, 2.509368498023302},
            {-0.79369948495403997, -10.467229088550477, 4.4259112188042717},
            {-0.36846550988264171, -2.1999999999999993, 3.8241643751317653},
            {-3.7795824044716797, -1.0999999999999992, 6.9103078474041162},
        },
        {
            // dual
            {0, -19.699999999999999, 0},
            {0, -22.5, 0},
            {0, -27.100000000000001, 0},
            {0, -31.100000000000001, 0},
            {0.13830482241006437, -34.200000000000003, 0.058066996634237526},
            {0.13830482241006437, -37.5, 0.058066996634237526},
            {0.024869571651898181, -29.279465254889942, 5.742155346384747},
            {5.2361641906907721, -23.978037454794588, 7.3224311684970225},
            {12, -25, 4.5},
            {0.6712681015388875, -29.237511696429912, -5.7895339654610627},
            {-4.5837699539203882, -24.820522055907528, -9.0506346310635131},
            {-5, -19, -4.5},
            {-1.1220967326887947, -18.599999999999998, 2.5653262799279806},
            {-0.1149599698066015, -10.154364753538818, 3.8369082963877075},
            {-2, -2.1999999999999993, 2.3999999999999999},
            {2.2597626597392297, -1.0999999999999992, 4.1362091126046305},
            {1.1220967326887947, -18.599999999999998, -2.5653262799279806},
            {3.8706949675376396, -10.45402487379207, -2.345560526027215},
            {3, -2.1999999999999993, -2.3999999999999999},
            {7.5733107675393745, -1.0999999999999992, -1.9051983998526181},
        },
        {
            // sword
            {0, -19.699999999999999, 0},
            {0, -22.5, 0},
            {0, -27.100000000000001, 0},
            {0, -31.100000000000001, 0},
            {0.13764585918281833, -34.200000000000003, 0.059612225674132907},
            {0.13764585918281833, -37.5, 0.059612225674132907},
            {-0.25454447310581552, -29.22672321007731, 5.61738152105755},
            {4.0111726992504657, -22.950989324687516, 5.1945089572335839},
            {11, -24, 3},
            {1.5712495264658415, -29.017970225994478, -4.9305225867926676},
            {3.1488001776367973, -22.604531932222979, -1.170317380294565},
            {7.3525984637485742, -22.457903364457742, 4.9179155351578974},
            {-1.1589171827560174, -18.599999999999998, 2.548903874906399},
            {-0.1493090536995032, -10.159567365884827, 3.8526631988257516},
            {-2, -2.1999999999999993, 2.3999999999999999},
            {2.2446041054262311, -1.0999999999999992, 4.172945568312457},
            {1.1589171827560174, -18.599999999999998, -2.548903874906399},
            {3.896741899361551, -10.450927621024992, -2.3102929292847381},
            {3, -2.1999999999999993, -2.3999999999999999},
            {7.5688653694258825, -1.0999999999999992, -1.8657067883073069},
        },
        {
            // greatsword
            {0, -19.699999999999999, 0},
            {0, -22.5, 0},
            {0, -27.100000000000001, 0},
            {0, -31.100000000000001, 0},
            {0.13764585918281833, -34.200000000000003, 0.059612225674132907},
            {0.13764585918281833, -37.5, 0.059612225674132907},
            {-0.25454447310581552, -29.22672321007731, 5.61738152105755},
            {4.0111726992504657, -22.950989324687516, 5.1945089572335839},
            {11, -24, 3},
            {1.5509115491087457, -28.99838300102223, -4.9040998816725931},
            {2.9794148170714996, -22.575192644638797, -1.1012057128739645},
            {7.0210165059075358, -22.31771276122663, 5.0922714928995223},
            {-1.1589171827560174, -18.599999999999998, 2.548903874906399},
            {-0.1493090536995032, -10.159567365884827, 3.8526631988257516},
            {-2, -2.1999999999999993, 2.3999999999999999},
            {2.2446041054262311, -1.0999999999999992, 4.172945568312457},
            {1.1589171827560174, -18.599999999999998, -2.548903874906399},
            {3.896741899361551, -10.450927621024992, -2.3102929292847381},
            {3, -2.1999999999999993, -2.3999999999999999},
            {7.5688653694258825, -1.0999999999999992, -1.8657067883073069},
        },
        {
            // dagger
            {0, -19.699999999999999, 0},
            {0, -22.5, 0},
            {0, -27.100000000000001, 0},
            {0, -31.100000000000001, 0},
            {0.13764585918281833, -34.200000000000003, 0.059612225674132907},
            {0.13764585918281833, -37.5, 0.059612225674132907},
            {-0.31303744581604742, -29.253887932631969, 5.7236933644210879},
            {3.8346894707618526, -22.8906221438892, 5.9790557095525481},
            {11, -24, 4.5},
            {1.0583100570309985, -29.600000000000001, -5.8043070062830973},
            {-1.2462489773607708, -24.464809757666458, -10.911050472724858},
            {-4, -22, -4.5},
            {-1.1589171827560174, -18.599999999999998, 2.548903874906399},
            {-0.1493090536995032, -10.159567365884827, 3.8526631988257516},
            {-2, -2.1999999999999993, 2.3999999999999999},
            {2.2446041054262311, -1.0999999999999992, 4.172945568312457},
            {1.1589171827560174, -18.599999999999998, -2.548903874906399},
            {3.896741899361551, -10.450927621024992, -2.3102929292847381},
            {3, -2.1999999999999993, -2.3999999999999999},
            {7.5688653694258825, -1.0999999999999992, -1.8657067883073069},
        },
        {
            // spear
            {0, -19.699999999999999, 0},
            {0, -22.5, 0},
            {0, -27.100000000000001, 0},
            {0, -31.100000000000001, 0},
            {0.13764585918281833, -34.200000000000003, 0.059612225674132907},
            {0.13764585918281833, -37.5, 0.059612225674132907},
            {-0.25454447310581552, -29.22672321007731, 5.61738152105755},
            {4.0111726992504657, -22.950989324687516, 5.1945089572335839},
            {11, -24, 3},
            {1.0577035650340674, -28.569092787589959, -4.3287630552032192},
            {0.80478178076784368, -23.980016812711053, 1.7240389626916999},
            {1.052656203859607, -19.989653875421951, 7.9510416043393191},
            {-1.1589171827560174, -18.599999999999998, 2.548903874906399},
            {-0.1493090536995032, -10.159567365884827, 3.8526631988257516},
            {-2, -2.1999999999999993, 2.3999999999999999},
            {2.2446041054262311, -1.0999999999999992, 4.172945568312457},
            {1.1589171827560174, -18.599999999999998, -2.548903874906399},
            {3.896741899361551, -10.450927621024992, -2.3102929292847381},
            {3, -2.1999999999999993, -2.3999999999999999},
            {7.5688653694258825, -1.0999999999999992, -1.8657067883073069},
        },
        {
            // hammer
            {0, -19.699999999999999, 0},
            {0, -22.5, 0},
            {0, -27.100000000000001, 0},
            {0, -31.100000000000001, 0},
            {0.13764585918281833, -34.200000000000003, 0.059612225674132907},
            {0.13764585918281833, -37.5, 0.059612225674132907},
            {-0.25454447310581552, -29.22672321007731, 5.61738152105755},
            {4.0111726992504657, -22.950989324687516, 5.1945089572335839},
            {11, -24, 3},
            {1.4387648818823293, -28.859742410385309, -4.7145534949829733},
            {1.1432516198957832, -22.917392717903617, 0.014204486163115426},
            {5.1973157377818238, -21.546664443455501, 6.051229260478471},
            {-1.1589171827560174, -18.599999999999998, 2.548903874906399},
            {-0.1493090536995032, -10.159567365884827, 3.8526631988257516},
            {-2, -2.1999999999999993, 2.3999999999999999},
            {2.2446041054262311, -1.0999999999999992, 4.172945568312457},
            {1.1589171827560174, -18.599999999999998, -2.548903874906399},
            {3.896741899361551, -10.450927621024992, -2.3102929292847381},
            {3, -2.1999999999999993, -2.3999999999999999},
            {7.5688653694258825, -1.0999999999999992, -1.8657067883073069},
        },
        {
            // scythe
            {0, -19.699999999999999, 0},
            {0, -22.5, 0},
            {0, -27.100000000000001, 0},
            {0, -31.100000000000001, 0},
            {0.13764585918281833, -34.200000000000003, 0.059612225674132907},
            {0.13764585918281833, -37.5, 0.059612225674132907},
            {-0.25454447310581552, -29.22672321007731, 5.61738152105755},
            {4.0111726992504657, -22.950989324687516, 5.1945089572335839},
            {11, -24, 3},
            {1.1476596607637657, -28.573716773449068, -4.3282448845507488},
            {0.68155872555653296, -23.495594902519535, 1.3069506029355633},
            {1.8814961593714372, -20.144758411144359, 7.794788837894739},
            {-1.1589171827560174, -18.599999999999998, 2.548903874906399},
            {-0.1493090536995032, -10.159567365884827, 3.8526631988257516},
            {-2, -2.1999999999999993, 2.3999999999999999},
            {2.2446041054262311, -1.0999999999999992, 4.172945568312457},
            {1.1589171827560174, -18.599999999999998, -2.548903874906399},
            {3.896741899361551, -10.450927621024992, -2.3102929292847381},
            {3, -2.1999999999999993, -2.3999999999999999},
            {7.5688653694258825, -1.0999999999999992, -1.8657067883073069},
        },
        {
            // claw
            {0, -19.699999999999999, 0},
            {0, -22.5, 0},
            {0, -27.100000000000001, 0},
            {0, -31.100000000000001, 0},
            {0.13764585918281833, -34.200000000000003, 0.059612225674132907},
            {0.13764585918281833, -37.5, 0.059612225674132907},
            {-0.31303744581604742, -29.253887932631969, 5.7236933644210879},
            {3.8346894707618526, -22.8906221438892, 5.9790557095525481},
            {11, -24, 4.5},
            {1.0583100570309985, -29.600000000000001, -5.8043070062830973},
            {-1.2462489773607708, -24.464809757666458, -10.911050472724858},
            {-4, -22, -4.5},
            {-1.1589171827560174, -18.599999999999998, 2.548903874906399},
            {-0.1493090536995032, -10.159567365884827, 3.8526631988257516},
            {-2, -2.1999999999999993, 2.3999999999999999},
            {2.2446041054262311, -1.0999999999999992, 4.172945568312457},
            {1.1589171827560174, -18.599999999999998, -2.548903874906399},
            {3.896741899361551, -10.450927621024992, -2.3102929292847381},
            {3, -2.1999999999999993, -2.3999999999999999},
            {7.5688653694258825, -1.0999999999999992, -1.8657067883073069},
        },
        {
            // bow
            {0, -19.699999999999999, 0},
            {0, -22.5, 0},
            {0, -27.100000000000001, 0},
            {0, -31.100000000000001, 0},
            {0.13764585918281833, -34.200000000000003, 0.059612225674132907},
            {0.13764585918281833, -37.5, 0.059612225674132907},
            {-0.31303744581604742, -29.253887932631969, 5.7236933644210879},
            {3.8346894707618526, -22.8906221438892, 5.9790557095525481},
            {11, -24, 4.5},
            {1.0583100570309985, -29.600000000000001, -5.8043070062830973},
            {-1.2462489773607708, -24.464809757666458, -10.911050472724858},
            {-4, -22, -4.5},
            {-1.1589171827560174, -18.599999999999998, 2.548903874906399},
            {-0.1493090536995032, -10.159567365884827, 3.8526631988257516},
            {-2, -2.1999999999999993, 2.3999999999999999},
            {2.2446041054262311, -1.0999999999999992, 4.172945568312457},
            {1.1589171827560174, -18.599999999999998, -2.548903874906399},
            {3.896741899361551, -10.450927621024992, -2.3102929292847381},
            {3, -2.1999999999999993, -2.3999999999999999},
            {7.5688653694258825, -1.0999999999999992, -1.8657067883073069},
        },
        {
            // pistol
            {0, -19.699999999999999, 0},
            {0, -22.5, 0},
            {0, -27.100000000000001, 0},
            {0, -31.100000000000001, 0},
            {0.13764585918281833, -34.200000000000003, 0.059612225674132907},
            {0.13764585918281833, -37.5, 0.059612225674132907},
            {-0.31303744581604742, -29.253887932631969, 5.7236933644210879},
            {3.8346894707618526, -22.8906221438892, 5.9790557095525481},
            {11, -24, 4.5},
            {1.0583100570309985, -29.600000000000001, -5.8043070062830973},
            {-1.2462489773607708, -24.464809757666458, -10.911050472724858},
            {-4, -22, -4.5},
            {-1.1589171827560174, -18.599999999999998, 2.548903874906399},
            {-0.1493090536995032, -10.159567365884827, 3.8526631988257516},
            {-2, -2.1999999999999993, 2.3999999999999999},
            {2.2446041054262311, -1.0999999999999992, 4.172945568312457},
            {1.1589171827560174, -18.599999999999998, -2.548903874906399},
            {3.896741899361551, -10.450927621024992, -2.3102929292847381},
            {3, -2.1999999999999993, -2.3999999999999999},
            {7.5688653694258825, -1.0999999999999992, -1.8657067883073069},
        },
        {
            // launcher
            {0, -19.699999999999999, 0},
            {0, -22.5, 0},
            {0, -27.100000000000001, 0},
            {0, -31.100000000000001, 0},
            {0.13764585918281833, -34.200000000000003, 0.059612225674132907},
            {0.13764585918281833, -37.5, 0.059612225674132907},
            {-0.25454447310581552, -29.22672321007731, 5.61738152105755},
            {4.0111726992504657, -22.950989324687516, 5.1945089572335839},
            {11, -24, 3},
            {1.316148017990586, -28.690245595533732, -4.4816716210839296},
            {0.63195920173060216, -22.949518542745821, 0.45147530244204326},
            {3.53940594857663, -20.845711427299932, 6.9230090491866054},
            {-1.1589171827560174, -18.599999999999998, 2.548903874906399},
            {-0.1493090536995032, -10.159567365884827, 3.8526631988257516},
            {-2, -2.1999999999999993, 2.3999999999999999},
            {2.2446041054262311, -1.0999999999999992, 4.172945568312457},
            {1.1589171827560174, -18.599999999999998, -2.548903874906399},
            {3.896741899361551, -10.450927621024992, -2.3102929292847381},
            {3, -2.1999999999999993, -2.3999999999999999},
            {7.5688653694258825, -1.0999999999999992, -1.8657067883073069},
        },
        {
            // handle
            {0, -19.699999999999999, 0},
            {0, -22.5, 0},
            {0, -27.100000000000001, 0},
            {0, -31.100000000000001, 0},
            {0.13764585918281833, -34.200000000000003, 0.059612225674132907},
            {0.13764585918281833, -37.5, 0.059612225674132907},
            {-0.31303744581604742, -29.253887932631969, 5.7236933644210879},
            {3.8346894707618526, -22.8906221438892, 5.9790557095525481},
            {11, -24, 4.5},
            {1.0583100570309985, -29.600000000000001, -5.8043070062830973},
            {-1.2462489773607708, -24.464809757666458, -10.911050472724858},
            {-4, -22, -4.5},
            {-1.1589171827560174, -18.599999999999998, 2.548903874906399},
            {-0.1493090536995032, -10.159567365884827, 3.8526631988257516},
            {-2, -2.1999999999999993, 2.3999999999999999},
            {2.2446041054262311, -1.0999999999999992, 4.172945568312457},
            {1.1589171827560174, -18.599999999999998, -2.548903874906399},
            {3.896741899361551, -10.450927621024992, -2.3102929292847381},
            {3, -2.1999999999999993, -2.3999999999999999},
            {7.5688653694258825, -1.0999999999999992, -1.8657067883073069},
        },
    };
    const char* documents[] = {
        R"pose({"schema": "eve.pixelfx.pose", "version": 1, "fields": {}})pose",
        R"pose({"schema": "eve.pixelfx.pose", "version": 1, "fields": {"turn": 1.1, "fx": -1, "fy": -26, "bx": 2, "by": -26, "armsFront": true, "hy": 1, "ffx": -3, "fbx": 4}})pose",
        R"pose({"schema": "eve.pixelfx.pose", "version": 1, "fields": {"crouch": 8, "lean": 3, "fx": -9, "fy": -8, "bx": 11, "by": -8, "ffx": -8, "fbx": 9}})pose",
        R"pose({"schema": "eve.pixelfx.pose", "version": 1, "fields": {"y": -8, "ffy": -9, "fby": -7, "toeF": true, "toeB": true, "lean": -4, "yaw": 0.7, "hx": 2}})pose",
        R"pose({"schema": "eve.pixelfx.pose", "version": 1, "fields": {"yaw": -0.8, "face": -1, "fx": 17, "fy": -30, "bx": -8, "by": -25, "hy": -1}})pose",
        R"pose({"schema": "eve.pixelfx.pose", "version": 1, "fields": {"weapon": "dagger", "offWeapon": "pistol", "sa": -0.5, "sb": 2.6, "fx": 12, "fy": -25, "bx": -5, "by": -19}})pose",
        R"pose({"schema": "eve.pixelfx.pose", "version": 1, "fields": {"weapon": "sword", "two": true, "sa": -0.4, "fx": 11, "fy": -24, "bx": -4, "by": -22, "pull": false, "nocked": false, "wl": 0.9, "wz": -1}})pose",
        R"pose({"schema": "eve.pixelfx.pose", "version": 1, "fields": {"weapon": "greatsword", "two": true, "sa": -0.4, "fx": 11, "fy": -24, "bx": -4, "by": -22, "pull": false, "nocked": false, "wl": 0.9, "wz": -1}})pose",
        R"pose({"schema": "eve.pixelfx.pose", "version": 1, "fields": {"weapon": "dagger", "two": false, "sa": -0.4, "fx": 11, "fy": -24, "bx": -4, "by": -22, "pull": false, "nocked": false, "wl": 0.9, "wz": -1}})pose",
        R"pose({"schema": "eve.pixelfx.pose", "version": 1, "fields": {"weapon": "spear", "two": true, "sa": -0.4, "fx": 11, "fy": -24, "bx": -4, "by": -22, "pull": false, "nocked": false, "wl": 0.9, "wz": -1}})pose",
        R"pose({"schema": "eve.pixelfx.pose", "version": 1, "fields": {"weapon": "hammer", "two": true, "sa": -0.4, "fx": 11, "fy": -24, "bx": -4, "by": -22, "pull": false, "nocked": false, "wl": 0.9, "wz": -1}})pose",
        R"pose({"schema": "eve.pixelfx.pose", "version": 1, "fields": {"weapon": "scythe", "two": true, "sa": -0.4, "fx": 11, "fy": -24, "bx": -4, "by": -22, "pull": false, "nocked": false, "wl": 0.9, "wz": -1}})pose",
        R"pose({"schema": "eve.pixelfx.pose", "version": 1, "fields": {"weapon": "claw", "two": false, "sa": -0.4, "fx": 11, "fy": -24, "bx": -4, "by": -22, "pull": false, "nocked": false, "wl": 0.9, "wz": -1}})pose",
        R"pose({"schema": "eve.pixelfx.pose", "version": 1, "fields": {"weapon": "bow", "two": false, "sa": -0.4, "fx": 11, "fy": -24, "bx": -4, "by": -22, "pull": true, "nocked": true, "wl": 0.9, "wz": -1}})pose",
        R"pose({"schema": "eve.pixelfx.pose", "version": 1, "fields": {"weapon": "pistol", "two": false, "sa": -0.4, "fx": 11, "fy": -24, "bx": -4, "by": -22, "pull": false, "nocked": false, "wl": 0.9, "wz": -1}})pose",
        R"pose({"schema": "eve.pixelfx.pose", "version": 1, "fields": {"weapon": "launcher", "two": true, "sa": -0.4, "fx": 11, "fy": -24, "bx": -4, "by": -22, "pull": false, "nocked": false, "wl": 0.9, "wz": -1}})pose",
        R"pose({"schema": "eve.pixelfx.pose", "version": 1, "fields": {"weapon": "handle", "two": false, "sa": -0.4, "fx": 11, "fy": -24, "bx": -4, "by": -22, "pull": false, "nocked": false, "wl": 0.9, "wz": -1}})pose",
    };
    constexpr uint64_t statistics[17][5] = {
        {436u, 36646u, 36847u, 41187u, 111180u}, {490u, 44041u, 44310u, 49087u, 124950u},
        {484u, 44948u, 45219u, 49929u, 123420u}, {517u, 49218u, 49608u, 54694u, 131835u},
        {453u, 38244u, 38453u, 42989u, 115515u}, {543u, 51486u, 51825u, 56680u, 138465u},
        {600u, 63437u, 63510u, 67671u, 153000u}, {694u, 79173u, 79337u, 83741u, 176970u},
        {473u, 43681u, 43745u, 48100u, 120615u}, {599u, 53289u, 49850u, 51694u, 152745u},
        {791u, 92736u, 91415u, 94971u, 201705u}, {707u, 60709u, 61241u, 69677u, 180285u},
        {484u, 44298u, 44597u, 49430u, 123420u}, {634u, 56293u, 53151u, 55441u, 161670u},
        {472u, 42756u, 42868u, 47587u, 120360u}, {599u, 50838u, 52874u, 49816u, 152745u},
        {449u, 40148u, 39571u, 42867u, 114495u},
    };
    rig::Rasterizer raster;
    for (size_t fixture = 0; fixture < 17; ++fixture) {
        auto parsed = eve::Value::fromJson(documents[fixture]);
        REQUIRE(parsed);
        auto pose = rig::decodePose(parsed.value());
        REQUIRE(pose);
        auto skeleton = rig::skeleton(pose.value());
        REQUIRE(skeleton);
        for (size_t i = 0; i < 20; ++i)
            for (size_t c = 0; c < 3; ++c)
                REQUIRE(std::abs(skeleton.value().joints[i][c] - joints[fixture][i][c]) < 1e-10);
        auto rendered = raster.render(pose.value());
        REQUIRE(rendered);
        const auto& pixels = rendered.value();
        REQUIRE(pixels.xs.size() == pixels.color.size());
        REQUIRE(pixels.ys.size() == pixels.color.size());
        REQUIRE(pixels.src.size() == pixels.color.size());
        REQUIRE(pixels.ink.size() == pixels.color.size());
        REQUIRE(pixels.x0 <= pixels.x1);
        REQUIRE(pixels.y0 <= pixels.y1);
        std::array<uint64_t, 5> actual{uint64_t(pixels.color.size()), 0, 0, 0, 0};
        for (size_t i = 0; i < pixels.color.size(); ++i) {
            for (size_t c = 0; c < 4; ++c) actual[c + 1] += (pixels.color[i] >> (c * 8)) & 255u;
            REQUIRE(pixels.src[i] >= -1);
            REQUIRE(pixels.src[i] < 24 * 38);
            REQUIRE(pixels.ink[i] <= 1u);
            REQUIRE(pixels.xs[i] >= pixels.x0);
            REQUIRE(pixels.xs[i] <= pixels.x1);
            REQUIRE(pixels.ys[i] >= pixels.y0);
            REQUIRE(pixels.ys[i] <= pixels.y1);
        }
        for (size_t i = 0; i < 5; ++i) {
            const auto a = actual[i], b = statistics[fixture][i], delta = a > b ? a - b : b - a;
            REQUIRE(delta <= (i == 0 ? 4u : 1020u));
        }
        const auto encoded  = rig::encodePose(pose.value());
        auto       restored = rig::decodePose(encoded);
        REQUIRE(restored);
        auto json = encoded.toJson();
        REQUIRE(json);
        auto decoded = eve::Value::fromJson(json.value());
        REQUIRE(decoded);
        auto repeated = rig::decodePose(decoded.value());
        REQUIRE(repeated);
        REQUIRE(rig::encodePose(repeated.value()) == encoded);
    }
}
TEST_CASE("stylize.pixelfx.core.rigOwnershipFailureAndRecovery") {
    namespace rig = eve::stylize::pixelfx::rig;
    auto owned    = [&]() {
        rig::Rasterizer source;
        auto            pose   = rig::Pose{};
        auto            result = source.render(pose);
        REQUIRE(result);
        return std::move(result).takeValue();
    }();
    REQUIRE(!owned.color.empty());
    const auto      before = owned.color;
    rig::Rasterizer raster;
    rig::Pose       pose;
    pose.lean    = 3000;
    auto outside = raster.render(pose);
    REQUIRE(!outside);
    auto recovered = raster.render(rig::Pose{});
    REQUIRE(recovered);
    REQUIRE(recovered.value().color == before);
    auto moved = std::move(raster);
    auto again = moved.render(rig::Pose{});
    REQUIRE(again);
    REQUIRE(again.value().src == owned.src);
    pose         = {};
    pose.weapon  = rig::Weapon::Bow;
    pose.two     = true;
    auto badGrip = rig::solve(pose);
    REQUIRE(!badGrip);
    REQUIRE(badGrip.status().diagnostics().front().path() == "/pose/two");
    pose               = {};
    pose.wl            = 1.1;
    auto badProjection = rig::solve(pose);
    REQUIRE(!badProjection);
    pose          = {};
    pose.turn     = std::numeric_limits<double>::quiet_NaN();
    auto badAngle = rig::solve(pose);
    REQUIRE(!badAngle);
    auto document = rig::encodePose(rig::Pose{});
    document.set("version", eve::Value(2));
    auto future = rig::decodePose(document);
    REQUIRE(!future);
    REQUIRE(future.status().diagnostics().front().code() == eve::DiagnosticCode::UnknownVersion);
    document = rig::encodePose(rig::Pose{});
    document.find("fields")->set("a/b~", eve::Value(1));
    auto unknown = rig::decodePose(document);
    REQUIRE(!unknown);
    REQUIRE(unknown.status().diagnostics().front().path() == "/pose/fields/a~1b~0");
    document = rig::encodePose(rig::Pose{});
    document.find("fields")->set("two", eve::Value(1));
    auto wrongType = rig::decodePose(document);
    REQUIRE(!wrongType);
    REQUIRE(owned.color == before);
}
TEST_CASE("stylize.pixelfx.core.rigIndependentPartsAndRecovery") {
    namespace rig = eve::stylize::pixelfx::rig;
    rig::Rasterizer raster;
    auto            full = raster.render(rig::Pose{});
    REQUIRE(full);
    auto parts = raster.renderParts(rig::Pose{});
    REQUIRE(parts);
    REQUIRE(parts.value().size() == 22u);
    for (size_t i = 0; i < parts.value().size(); ++i) {
        const auto& part = parts.value()[i];
        REQUIRE(part.part == int(i) + 1);
        REQUIRE(!part.name.empty());
        REQUIRE(!part.pixels.color.empty());
        double x = 0, y = 0;
        for (size_t j = 0; j < part.pixels.color.size(); ++j) {
            x += part.pixels.xs[j];
            y += part.pixels.ys[j];
        }
        REQUIRE(std::abs(part.centerX - x / double(part.pixels.color.size())) < 1e-12);
        REQUIRE(std::abs(part.centerY - y / double(part.pixels.color.size())) < 1e-12);
    }
    auto pose      = rig::Pose{};
    pose.weapon    = rig::Weapon::Sword;
    pose.offWeapon = rig::Weapon::Dagger;
    auto armed     = raster.renderParts(pose);
    REQUIRE(armed);
    REQUIRE(armed.value().size() == 24u);
    REQUIRE(armed.value()[22].name == "weapon");
    REQUIRE(armed.value()[23].name == "offWeapon");
    auto restored = raster.render(rig::Pose{});
    REQUIRE(restored);
    REQUIRE(restored.value().color == full.value().color);
    REQUIRE(restored.value().src == full.value().src);
    pose.lean     = 3000;
    auto rejected = raster.renderParts(pose);
    REQUIRE(!rejected);
    auto recovered = raster.render(rig::Pose{});
    REQUIRE(recovered);
    REQUIRE(recovered.value().color == full.value().color);
    REQUIRE(parts.value()[0].name == "pelvis");  // Earlier owning snapshots retain their data.
}
TEST_CASE("stylize.pixelfx.core.poseTrackHeldLimbsSmoothFloorAndDuplicateKeys") {
    namespace rig = eve::stylize::pixelfx::rig;
    auto doc      = eve::Value::fromJson(
        R"track({"schema":"eve.pixelfx.pose-track","version":1,"keys":[[-0.2,{"x":-3,"fx":-3,"hx":4,"hy":2,"wl":0.4,"weapon":"sword","two":true,"ghost":1}],[0.8,{"x":9,"fx":9,"turn":1.2},"line"],[0.8,{"x":12,"fx":15,"weapon":"hammer","two":true},"snap"],[1.8,{"x":0,"fx":-3},"out"],[2,{},null]]})track");
    REQUIRE(doc);
    auto prepared = rig::PoseTrack::prepare(doc.value());
    REQUIRE(prepared);
    doc.value() = eve::Value();
    auto track  = std::move(prepared).takeValue();
    auto before = track.sample(-.3);
    REQUIRE(before);
    REQUIRE(before.value().fx == -3);
    REQUIRE(before.value().weapon == rig::Weapon::Sword);
    REQUIRE(!before.value().turn);
    auto a = track.sample(0);
    auto b = track.sample(.01);
    REQUIRE(a);
    REQUIRE(b);
    REQUIRE(std::abs(a.value().fx + 1) < 1e-12);
    REQUIRE(a.value().fx == b.value().fx);
    REQUIRE(std::abs(a.value().x + .6) < 1e-12);
    REQUIRE(b.value().x > a.value().x);
    REQUIRE(std::abs(a.value().hx - 10.0 / 3) < 1e-12);
    REQUIRE(std::abs(a.value().wl - .5) < 1e-12);
    REQUIRE(a.value().turn && *a.value().turn == 1.2);
    REQUIRE(a.value().ghost == 1);
    REQUIRE(a.value().weapon == rig::Weapon::Sword);
    REQUIRE(a.value().two);
    auto boundary = track.sample(.8);
    REQUIRE(boundary);
    REQUIRE(boundary.value().fx == 15);
    REQUIRE(boundary.value().x == 12);
    REQUIRE(boundary.value().weapon == rig::Weapon::Hammer);
    REQUIRE(!boundary.value().turn);
    auto last = track.sample(2);
    REQUIRE(last);
    REQUIRE(last.value().fx == -3);
    REQUIRE(last.value().ghost == 0);
    REQUIRE(last.value().weapon == rig::Weapon::None);
    REQUIRE(last.value().wl == 1);
    auto heldBefore = track.isHeld(-.3);
    auto moving     = track.isHeld(0);
    auto heldAfter  = track.isHeld(2);
    REQUIRE(heldBefore);
    REQUIRE(moving);
    REQUIRE(heldAfter);
    REQUIRE(heldBefore.value());
    REQUIRE(!moving.value());
    REQUIRE(heldAfter.value());
    auto moved     = std::move(track);
    auto afterMove = moved.sample(.8);
    REQUIRE(afterMove);
    REQUIRE(afterMove.value().weapon == rig::Weapon::Hammer);
    auto bad = moved.sample(std::numeric_limits<double>::infinity());
    REQUIRE(!bad);
    auto heldBad = moved.isHeld(std::numeric_limits<double>::quiet_NaN());
    REQUIRE(!heldBad);
}
TEST_CASE("stylize.pixelfx.core.poseTrackValidationAndSnapshotLifetime") {
    namespace rig = eve::stylize::pixelfx::rig;
    auto parse    = [](const char* text) {
        auto v = eve::Value::fromJson(text);
        if (!v) throw std::runtime_error(v.status().describe());
        return std::move(v).takeValue();
    };
    auto doc = parse(R"track({"schema":"eve.pixelfx.pose-track","version":1,"keys":[[0,{"fx":4}],[1,{"fx":5}]]})track");
    auto prepared = rig::PoseTrack::prepare(doc);
    REQUIRE(prepared);
    auto snapshot = prepared.value().sample(.2);
    REQUIRE(snapshot);
    const auto reject = [&](eve::Value input, const char* path) {
        auto bad = rig::PoseTrack::prepare(input);
        REQUIRE(!bad);
        REQUIRE(bad.status().diagnostics().front().path() == path);
    };
    auto invalid = doc;
    invalid.set("version", eve::Value(2));
    reject(invalid, "/track/version");
    invalid = doc;
    invalid.set("a/b~", eve::Value(1));
    reject(invalid, "/track/a~1b~0");
    invalid = doc;
    invalid.set("keys", eve::Value::array({}));
    reject(invalid, "/track/keys");
    invalid = doc;
    invalid.set("keys", eve::Value::array(eve::Value::Array(4097)));
    reject(invalid, "/track/keys");
    invalid                           = doc;
    invalid.find("keys")->at(1).at(0) = eve::Value(-1);
    reject(invalid, "/track/keys/1/0");
    invalid = doc;
    invalid.find("keys")->at(1).at(1).set("armsFront", eve::Value(1));
    reject(invalid, "/track/keys/1/1/armsFront");
    invalid = doc;
    invalid.find("keys")->at(0).at(1).set("unknown", eve::Value(1));
    reject(invalid, "/track/keys/0/1/unknown");
    invalid = parse(R"track({"schema":"eve.pixelfx.pose-track","version":1,"keys":[[0,{},"unknown"]]})track");
    reject(invalid, "/track/keys/0/2");
    auto remaining = prepared.value().sample(.2);
    REQUIRE(remaining);
    REQUIRE(remaining.value().fx == snapshot.value().fx);
    auto owned = [&]() {
        auto input = doc;
        auto token = rig::PoseTrack::prepare(input);
        REQUIRE(token);
        return std::move(token).takeValue();
    }();
    doc              = eve::Value();
    auto independent = owned.sample(.2);
    REQUIRE(independent);
    REQUIRE(independent.value().fx == snapshot.value().fx);
}

TEST_CASE("stylize.pixelfx.core.canvasLayersOwnershipAndValidation") {
    for (const auto& info : PixelFx::catalogue()) {
        PixelFx fx(info.kind);
        auto    seek = fx.prepareSeek(info.duration * .37);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        const std::vector<uint32_t> originalColor(fx.color().begin(), fx.color().end());
        const std::vector<uint32_t> originalLight(fx.light().begin(), fx.light().end());
        const auto                  time  = fx.effectTime();
        const auto                  ticks = fx.activeTickCount();
        const size_t                n     = originalColor.size();
        std::vector<uint32_t>       storage(n * 2);
        PixelFxCanvas               canvas{info.width,
                                           info.height,
                                           info.originX,
                                           info.originY,
                                           std::span<uint32_t>(storage.data(), n),
                                           std::span<uint32_t>(storage.data() + n, n)};
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Both, {}));
        REQUIRE(std::vector<uint32_t>(canvas.color.begin(), canvas.color.end()) == originalColor);
        REQUIRE(std::vector<uint32_t>(canvas.light.begin(), canvas.light.end()) == originalLight);
        std::fill(storage.begin(), storage.end(), 0);
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Back, {}));
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Front, {}));
        REQUIRE(std::vector<uint32_t>(canvas.color.begin(), canvas.color.end()) == originalColor);
        REQUIRE(std::vector<uint32_t>(canvas.light.begin(), canvas.light.end()) == originalLight);
        const auto before  = storage;
        auto       invalid = canvas;
        invalid.light      = invalid.color;
        REQUIRE(!fx.drawToCanvas(invalid, PixelFxLayer::Both, {}));
        invalid.light = std::span<uint32_t>(storage.data() + 1, n);
        REQUIRE(!fx.drawToCanvas(invalid, PixelFxLayer::Both, {}));
        invalid       = canvas;
        invalid.width = 0;
        REQUIRE(!fx.drawToCanvas(invalid, PixelFxLayer::Both, {}));
        invalid         = canvas;
        invalid.originX = 4097;
        REQUIRE(!fx.drawToCanvas(invalid, PixelFxLayer::Both, {}));
        invalid       = canvas;
        invalid.color = invalid.color.first(n - 1);
        REQUIRE(!fx.drawToCanvas(invalid, PixelFxLayer::Both, {}));
        REQUIRE(!fx.drawToCanvas(canvas, static_cast<PixelFxLayer>(99), {}));
        PixelFxPlacement placement;
        placement.x = std::numeric_limits<double>::quiet_NaN();
        REQUIRE(!fx.drawToCanvas(canvas, PixelFxLayer::Both, placement));
        REQUIRE(storage == before);
        REQUIRE(fx.effectTime() == time);
        REQUIRE(fx.activeTickCount() == ticks);
        REQUIRE(std::vector<uint32_t>(fx.color().begin(), fx.color().end()) == originalColor);
        REQUIRE(std::vector<uint32_t>(fx.light().begin(), fx.light().end()) == originalLight);
        fx.stop();
        REQUIRE(fx.advance(.6));
        REQUIRE(fx.done());
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Both, {}));
        // A stopped source effect can still write (chain children replace fade; unlight clears light).
        // Keep this canvas contract about equivalent layer entry points on the same retained target.
        auto          expected = before;
        PixelFxCanvas expectedCanvas{info.width,
                                     info.height,
                                     info.originX,
                                     info.originY,
                                     std::span<uint32_t>(expected.data(), n),
                                     std::span<uint32_t>(expected.data() + n, n)};
        REQUIRE(fx.drawToCanvas(expectedCanvas, PixelFxLayer::Back, {}));
        REQUIRE(fx.drawToCanvas(expectedCanvas, PixelFxLayer::Front, {}));
        REQUIRE(storage == expected);
    }
}


// Pinned projectiles/arrow.js and energy-disc.js; the spatial oracle checks full raw planes.
TEST_CASE("stylize.pixelfx.core.projectileReferenceAndTypedMaterials") {
    constexpr uint64_t expected[2][18][5] = {
        {
            {105u, 16873u, 14440u, 14299u, 26775u},
            {88u, 1144u, 1144u, 1144u, 22440u},
            {103u, 17417u, 15128u, 14997u, 26265u},
            {0u, 0u, 0u, 0u, 0u},
            {111u, 17647u, 15470u, 15594u, 28305u},
            {0u, 0u, 0u, 0u, 0u},
            {102u, 17099u, 14952u, 14775u, 26010u},
            {0u, 0u, 0u, 0u, 0u},
            {112u, 18718u, 16497u, 16615u, 28560u},
            {88u, 1144u, 1144u, 1144u, 22440u},
            {101u, 16344u, 14141u, 13983u, 25755u},
            {0u, 0u, 0u, 0u, 0u},
            {110u, 16903u, 14504u, 14556u, 28050u},
            {0u, 0u, 0u, 0u, 0u},
            {96u, 15997u, 13752u, 13453u, 24480u},
            {0u, 0u, 0u, 0u, 0u},
            {105u, 16873u, 14440u, 14299u, 26775u},
            {88u, 1144u, 1144u, 1144u, 22440u},
        },
        {
            {468u, 78419u, 93623u, 114459u, 119340u},
            {2379u, 18696u, 37917u, 66200u, 606645u},
            {463u, 77773u, 91528u, 112667u, 118065u},
            {2386u, 18226u, 37440u, 65712u, 608430u},
            {452u, 76718u, 90433u, 110238u, 115260u},
            {2359u, 18707u, 37895u, 66155u, 601545u},
            {458u, 77818u, 91720u, 111680u, 116790u},
            {2363u, 18470u, 37708u, 65972u, 602565u},
            {455u, 76997u, 90935u, 110868u, 116025u},
            {2401u, 18633u, 37819u, 66070u, 612255u},
            {450u, 75525u, 88672u, 109035u, 114750u},
            {2373u, 18469u, 37648u, 65907u, 605115u},
            {444u, 76266u, 88802u, 107552u, 113220u},
            {2345u, 18551u, 37698u, 65931u, 597975u},
            {463u, 79669u, 93345u, 112861u, 118065u},
            {2363u, 18392u, 37588u, 65841u, 602565u},
            {468u, 78419u, 93623u, 114459u, 119340u},
            {2379u, 18696u, 37917u, 66200u, 606645u},
        },
    };
    const std::array<PixelFxKind, 2> kinds{{PixelFxKind::Arrow, PixelFxKind::EnergyDisc}};
    for (size_t e = 0; e < kinds.size(); ++e) {
        PixelFx fx(kinds[e]);
        for (int i = 0; i < 9; ++i) {
            fx.render(std::round(double(i) * fx.info().duration * 120 / 8) / 120);
            size_t plane = 0;
            for (auto pixels : {fx.color(), fx.light()}) {
                std::array<uint64_t, 5> actual{};
                for (auto pixel : pixels) {
                    actual[0] += pixel != 0 ? 1u : 0u;
                    for (size_t c = 0; c < 4; ++c) actual[c + 1] += (pixel >> (c * 8)) & 255u;
                }
                for (size_t m = 0; m < 5; ++m) {
                    const auto a = actual[m], b = expected[e][size_t(i) * 2 + plane][m], delta = a > b ? a - b : b - a;
                    REQUIRE(delta <= (m == 0 ? 4u : 1020u));
                }
                ++plane;
            }
        }
        fx.render(.2);
        const std::vector<uint32_t> before(fx.color().begin(), fx.color().end());
        auto                        document = fx.configurationValue();
        document.find("options")->set("seed", eve::Value(int64_t(123)));
        if (e == 0) {
            document.find("options")->set(
                "wood", eve::Value::array({eve::Value(int64_t(0xff302010)), eve::Value(int64_t(0xffd0b090))}));
            document.find("options")->set("steel", eve::Value::array({eve::Value(int64_t(0xff806040))}));
            document.find("options")->set("wind", eve::Value(int64_t(0xffc0ff80)));
        } else {
            document.find("options")->set("size", eve::Value(12.0));
            document.find("options")->set("teeth", eve::Value(int64_t(13)));
            document.find("options")->set("speed", eve::Value(350.0));
        }
        auto prepared = PixelFx::prepareConfigurationValue(kinds[e], document);
        REQUIRE(prepared);
        REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
        fx.render(.2);
        const std::vector<uint32_t> changed(fx.color().begin(), fx.color().end());
        REQUIRE(changed != before);
        auto invalid = fx.configurationValue();
        if (e == 0)
            invalid.find("options")->set("wind", eve::Value(int64_t(0x00ffffff)));
        else
            invalid.find("options")->set("teeth", eve::Value(3.5));
        REQUIRE(!PixelFx::prepareConfigurationValue(kinds[e], invalid));
        REQUIRE(std::vector<uint32_t>(fx.color().begin(), fx.color().end()) == changed);
        if (e == 0) {
            REQUIRE(fx.settings().projectile.wood.size() == 4u);
            REQUIRE(fx.settings().projectile.steel.size() == 4u);
        } else {
            REQUIRE(fx.settings().projectile.teeth == 13);
            REQUIRE(fx.settings().projectile.size == 12);
        }
    }
}


// Default statistics from pinned upstream. Dense raw-frame probes cover every authored flash phase.
TEST_CASE("stylize.pixelfx.core.tracerAndMuzzleReferenceAndLifecycle") {
    constexpr uint64_t expected[3][18][5] = {
        {
            {386u, 74535u, 59220u, 42266u, 70141u},
            {1709u, 65951u, 39425u, 16218u, 435795u},
            {358u, 68979u, 56829u, 41240u, 65323u},
            {1684u, 65992u, 39548u, 16356u, 429420u},
            {359u, 70732u, 57912u, 42877u, 67448u},
            {1703u, 66119u, 39633u, 16369u, 434265u},
            {399u, 75293u, 60006u, 44592u, 71855u},
            {1696u, 65870u, 39428u, 16296u, 432480u},
            {407u, 75164u, 62060u, 47525u, 70176u},
            {1676u, 66038u, 39613u, 16396u, 427380u},
            {387u, 75005u, 61098u, 44370u, 70764u},
            {1708u, 65976u, 39477u, 16238u, 435540u},
            {409u, 74258u, 61932u, 48500u, 67812u},
            {1678u, 65872u, 39343u, 16178u, 427890u},
            {400u, 74054u, 61160u, 46063u, 68149u},
            {1690u, 66005u, 39589u, 16375u, 430950u},
            {386u, 74535u, 59220u, 42266u, 70141u},
            {1709u, 65951u, 39425u, 16218u, 435795u},
        },
        {
            {164u, 40666u, 27186u, 15512u, 41820u},
            {2552u, 89186u, 50904u, 18846u, 650760u},
            {134u, 19926u, 17312u, 17228u, 19279u},
            {124u, 1539u, 1348u, 592u, 31620u},
            {269u, 34499u, 31066u, 33865u, 33428u},
            {55u, 236u, 201u, 125u, 14025u},
            {351u, 40683u, 38175u, 43484u, 36641u},
            {52u, 384u, 334u, 216u, 13260u},
            {223u, 25894u, 24361u, 27683u, 27132u},
            {0u, 0u, 0u, 0u, 0u},
            {194u, 20167u, 18876u, 21659u, 20628u},
            {0u, 0u, 0u, 0u, 0u},
            {120u, 11878u, 11056u, 12643u, 10928u},
            {0u, 0u, 0u, 0u, 0u},
            {47u, 4625u, 4255u, 4810u, 2400u},
            {0u, 0u, 0u, 0u, 0u},
            {0u, 0u, 0u, 0u, 0u},
            {0u, 0u, 0u, 0u, 0u},
        },
        {
            {522u, 130848u, 98092u, 60635u, 133110u},
            {5338u, 254107u, 144898u, 52811u, 1361190u},
            {1275u, 176609u, 164292u, 180101u, 210772u},
            {155u, 1380u, 1201u, 533u, 39525u},
            {2145u, 263716u, 249102u, 283272u, 327464u},
            {15u, 27u, 9u, 0u, 3825u},
            {2573u, 291133u, 273995u, 315580u, 335876u},
            {0u, 0u, 0u, 0u, 0u},
            {2677u, 269375u, 252348u, 294978u, 271379u},
            {0u, 0u, 0u, 0u, 0u},
            {2014u, 183917u, 171347u, 203052u, 148296u},
            {0u, 0u, 0u, 0u, 0u},
            {596u, 52405u, 48331u, 57526u, 29189u},
            {0u, 0u, 0u, 0u, 0u},
            {7u, 1031u, 491u, 262u, 1785u},
            {0u, 0u, 0u, 0u, 0u},
            {0u, 0u, 0u, 0u, 0u},
            {0u, 0u, 0u, 0u, 0u},
        },
    };
    const std::array<PixelFxKind, 3> kinds{
        {PixelFxKind::BulletTracer, PixelFxKind::MuzzlePistol, PixelFxKind::MuzzleShotgun}};
    for (size_t e = 0; e < kinds.size(); ++e) {
        PixelFx fx(kinds[e]);
        for (int i = 0; i < 9; ++i) {
            const double time   = std::round(double(i) * fx.info().duration * 120 / 8) / 120;
            auto         sought = fx.prepareSeek(time);
            REQUIRE(sought);
            REQUIRE(fx.commitSeek(std::move(sought).takeValue()));
            fx.draw();
            size_t plane = 0;
            for (auto pixels : {fx.color(), fx.light()}) {
                std::array<uint64_t, 5> actual{};
                for (auto pixel : pixels) {
                    actual[0] += pixel != 0 ? 1u : 0u;
                    for (size_t c = 0; c < 4; ++c) actual[c + 1] += (pixel >> (c * 8)) & 255u;
                }
                for (size_t m = 0; m < 5; ++m) {
                    const auto a = actual[m], b = expected[e][size_t(i) * 2 + plane][m], delta = a > b ? a - b : b - a;
                    REQUIRE(delta <= (m == 0 ? 4u : 1020u));
                }
                ++plane;
            }
        }
        PixelFx whole(kinds[e]), split(kinds[e]);
        REQUIRE(whole.advance(.75));
        for (int i = 0; i < 90; ++i) REQUIRE(split.advance(1.0 / 120));
        whole.draw();
        split.draw();
        REQUIRE(std::vector<uint32_t>(whole.color().begin(), whole.color().end()) ==
                std::vector<uint32_t>(split.color().begin(), split.color().end()));
        REQUIRE(std::vector<uint32_t>(whole.light().begin(), whole.light().end()) ==
                std::vector<uint32_t>(split.light().begin(), split.light().end()));
        auto config = whole.configurationValue();
        config.find("options")->set("seed", eve::Value(int64_t(123)));
        config.find("options")->set(
            "smoke", eve::Value::array({eve::Value(int64_t(0xff302010)), eve::Value(int64_t(0xffd0b090))}));
        if (e == 0) {
            config.find("options")->set("length", eve::Value(80.0));
            config.find("options")->set("speed", eve::Value(720.0));
        } else
            config.find("options")->set("height", eve::Value(40.0));
        auto prepared = PixelFx::prepareConfigurationValue(kinds[e], config);
        REQUIRE(prepared);
        REQUIRE(whole.commitConfiguration(std::move(prepared).takeValue()));
        REQUIRE(whole.advance(.75));
        whole.draw();
        const std::vector<uint32_t> changed(whole.color().begin(), whole.color().end());
        REQUIRE(changed != std::vector<uint32_t>(split.color().begin(), split.color().end()));
        REQUIRE(whole.settings().projectile.smoke.size() == (e == 0 ? 5u : 7u));
        auto invalid = whole.configurationValue();
        invalid.find("options")->set("smoke", eve::Value::array({eve::Value(int64_t(0x00112233))}));
        REQUIRE(!PixelFx::prepareConfigurationValue(kinds[e], invalid));
        REQUIRE(std::vector<uint32_t>(whole.color().begin(), whole.color().end()) == changed);
        whole.stop();
        REQUIRE(whole.advance(.4));
        REQUIRE(whole.done());
        whole.draw();
        REQUIRE(std::all_of(whole.color().begin(), whole.color().end(), [](uint32_t x) { return x == 0; }));
    }
}


// Pinned upstream raw defaults; dense spatial comparisons additionally cover every loop tick.
TEST_CASE("stylize.pixelfx.core.smgAndPlasmaReferenceAndLoopOwnership") {
    constexpr uint64_t expected[2][18][5] = {
        {
            {628u, 98519u, 75592u, 66273u, 80719u},
            {2065u, 63930u, 36137u, 12822u, 526575u},
            {551u, 75071u, 61689u, 60784u, 63911u},
            {1723u, 15924u, 8978u, 3097u, 439365u},
            {632u, 97877u, 74615u, 66259u, 79762u},
            {2139u, 64473u, 36609u, 13100u, 545445u},
            {532u, 72686u, 60148u, 59627u, 61904u},
            {1739u, 18423u, 11168u, 4461u, 443445u},
            {590u, 95140u, 72652u, 63546u, 76880u},
            {2068u, 64679u, 36737u, 13177u, 527340u},
            {590u, 79729u, 66151u, 65774u, 65007u},
            {1727u, 18468u, 11236u, 4574u, 440385u},
            {663u, 102084u, 79203u, 70988u, 79343u},
            {2039u, 63991u, 36194u, 12831u, 519945u},
            {602u, 78704u, 65935u, 66448u, 63849u},
            {1708u, 16286u, 9323u, 3337u, 435540u},
            {628u, 98519u, 75592u, 66273u, 80719u},
            {2065u, 63930u, 36137u, 12822u, 526575u},
        },
        {
            {317u, 34007u, 69880u, 41180u, 80835u},
            {3181u, 17270u, 91751u, 51154u, 811155u},
            {339u, 35358u, 74181u, 43198u, 86445u},
            {3304u, 16840u, 91055u, 50522u, 842520u},
            {319u, 32671u, 69392u, 40222u, 81345u},
            {3411u, 16383u, 88384u, 48648u, 869805u},
            {313u, 33758u, 69253u, 40582u, 79815u},
            {3211u, 16825u, 90533u, 50278u, 818805u},
            {314u, 33560u, 69577u, 40443u, 80070u},
            {3185u, 17085u, 91242u, 50765u, 812175u},
            {315u, 33485u, 69488u, 40431u, 80325u},
            {3169u, 16519u, 88459u, 48751u, 808095u},
            {325u, 34379u, 71333u, 41495u, 82875u},
            {3269u, 16795u, 89944u, 49788u, 833595u},
            {337u, 35857u, 74503u, 43046u, 85935u},
            {3214u, 17163u, 92865u, 51864u, 819570u},
            {317u, 34007u, 69880u, 41180u, 80835u},
            {3181u, 17270u, 91751u, 51154u, 811155u},
        },
    };
    const std::array<PixelFxKind, 2> kinds{{PixelFxKind::MuzzleSmg, PixelFxKind::PlasmaShot}};
    for (size_t e = 0; e < kinds.size(); ++e) {
        PixelFx fx(kinds[e]);
        REQUIRE(!fx.isSimulated());
        for (int i = 0; i < 9; ++i) {
            fx.render(std::round(double(i) * fx.info().duration * 120 / 8) / 120);
            size_t plane = 0;
            for (auto pixels : {fx.color(), fx.light()}) {
                std::array<uint64_t, 5> actual{};
                for (auto pixel : pixels) {
                    actual[0] += pixel != 0 ? 1u : 0u;
                    for (size_t c = 0; c < 4; ++c) actual[c + 1] += (pixel >> (c * 8)) & 255u;
                }
                for (size_t m = 0; m < 5; ++m) {
                    const auto a = actual[m], b = expected[e][size_t(i) * 2 + plane][m], delta = a > b ? a - b : b - a;
                    REQUIRE(delta <= (m == 0 ? 4u : 1020u));
                }
                ++plane;
            }
        }
        fx.render(0);
        const std::vector<uint32_t> start(fx.color().begin(), fx.color().end());
        fx.render(fx.info().duration);
        REQUIRE(std::vector<uint32_t>(fx.color().begin(), fx.color().end()) == start);
        fx.render(.2);
        const std::vector<uint32_t> before(fx.color().begin(), fx.color().end());
        auto                        document = fx.configurationValue();
        document.find("options")->set("seed", eve::Value(int64_t(123)));
        if (e == 0) {
            document.find("options")->set("height", eve::Value(40.0));
            document.find("options")->set("brass", eve::Value::array({eve::Value(int64_t(0xffa08060))}));
        } else {
            document.find("options")->set("size", eve::Value(9.0));
            document.find("options")->set("speed", eve::Value(350.0));
        }
        auto prepared = PixelFx::prepareConfigurationValue(kinds[e], document);
        REQUIRE(prepared);
        REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
        fx.render(.2);
        const std::vector<uint32_t> changed(fx.color().begin(), fx.color().end());
        REQUIRE(changed != before);
        auto invalid = fx.configurationValue();
        invalid.find("options")->set(e == 0 ? "height" : "size", eve::Value(std::numeric_limits<double>::quiet_NaN()));
        REQUIRE(!PixelFx::prepareConfigurationValue(kinds[e], invalid));
        REQUIRE(std::vector<uint32_t>(fx.color().begin(), fx.color().end()) == changed);
        fx.render(.1);
        fx.render(.2);
        REQUIRE(std::vector<uint32_t>(fx.color().begin(), fx.color().end()) == changed);
        PixelFx other(kinds[e]);
        other.render(.2);
        REQUIRE(std::vector<uint32_t>(other.color().begin(), other.color().end()) == before);
    }
}


// Pinned source retains exactly 28 light cells at D=1.9; done() never suppresses raw draws.
TEST_CASE("stylize.pixelfx.core.magicMissileEndpointAndContinuation") {
    constexpr uint64_t expected[18][5] = {
        {19u, 2860u, 1834u, 4089u, 4845u},
        {0u, 0u, 0u, 0u, 0u},
        {126u, 22440u, 16487u, 28452u, 32130u},
        {1069u, 26824u, 14431u, 38241u, 272595u},
        {349u, 55266u, 36286u, 74757u, 88995u},
        {1555u, 36629u, 20692u, 51825u, 396525u},
        {345u, 59694u, 40666u, 78094u, 87975u},
        {1753u, 35574u, 19953u, 49654u, 447015u},
        {385u, 83882u, 69360u, 93503u, 98175u},
        {5332u, 114363u, 60472u, 167164u, 1359660u},
        {120u, 24315u, 19523u, 28245u, 30600u},
        {2633u, 25167u, 17659u, 32022u, 671415u},
        {50u, 10170u, 9418u, 10702u, 12750u},
        {595u, 7483u, 6704u, 8065u, 151725u},
        {21u, 3931u, 3431u, 4331u, 5355u},
        {275u, 1592u, 1256u, 1866u, 70125u},
        {0u, 0u, 0u, 0u, 0u},
        {28u, 75u, 56u, 88u, 7140u},
    };
    PixelFx fx(PixelFxKind::MagicMissile);
    for (int i = 0; i < 9; ++i) {
        auto token = fx.prepareSeek(std::round(double(i) * 1.9 * 120 / 8) / 120);
        REQUIRE(token);
        REQUIRE(fx.commitSeek(std::move(token).takeValue()));
        fx.draw();
        size_t plane = 0;
        for (auto pixels : {fx.color(), fx.light()}) {
            std::array<uint64_t, 5> actual{};
            for (auto pixel : pixels) {
                actual[0] += pixel != 0 ? 1u : 0u;
                for (size_t c = 0; c < 4; ++c) actual[c + 1] += (pixel >> (c * 8)) & 255u;
            }
            for (size_t m = 0; m < 5; ++m) {
                const auto a = actual[m], b = expected[size_t(i) * 2 + plane][m], delta = a > b ? a - b : b - a;
                REQUIRE(delta <= (m == 0 ? 4u : 1020u));
            }
            ++plane;
        }
    }
    REQUIRE(fx.done());
    REQUIRE(fx.activeTickCount() == 228u);
    REQUIRE(std::count_if(fx.light().begin(), fx.light().end(), [](uint32_t x) { return x != 0; }) == 28);
    const auto            effectInfo = fx.info();
    std::vector<uint32_t> color(fx.color().size()), light(color.size());
    REQUIRE(fx.drawToCanvas({effectInfo.width, effectInfo.height, effectInfo.originX, effectInfo.originY, color, light},
                            PixelFxLayer::Both, {}));
    REQUIRE(light == std::vector<uint32_t>(fx.light().begin(), fx.light().end()));
    auto excessive = fx.prepareSeek(1e12);
    REQUIRE(!excessive);
    REQUIRE(fx.effectTime() == 1.9);
    REQUIRE(fx.activeTickCount() == 228u);
    REQUIRE(fx.advance(.2));
    fx.draw();
    REQUIRE(fx.done());
    REQUIRE(fx.activeTickCount() == 252u);
    PixelFx expectedTail(PixelFxKind::MagicMissile);
    auto    sought = expectedTail.prepareSeek(2.1);
    REQUIRE(sought);
    REQUIRE(expectedTail.commitSeek(std::move(sought).takeValue()));
    expectedTail.draw();
    REQUIRE(std::vector<uint32_t>(fx.color().begin(), fx.color().end()) ==
            std::vector<uint32_t>(expectedTail.color().begin(), expectedTail.color().end()));
    REQUIRE(std::vector<uint32_t>(fx.light().begin(), fx.light().end()) ==
            std::vector<uint32_t>(expectedTail.light().begin(), expectedTail.light().end()));
    fx.stop();
    REQUIRE(fx.advance(.5));
    fx.draw();
    REQUIRE(fx.activeTickCount() == 312u);
    REQUIRE(std::all_of(fx.light().begin(), fx.light().end(), [](uint32_t x) { return x == 0; }));
    auto config = fx.configurationValue();
    config.find("options")->set("height", eve::Value(40.0));
    config.find("options")->set("reach", eve::Value(150.0));
    config.find("options")->set("seed", eve::Value(int64_t(123)));
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::MagicMissile, config);
    REQUIRE(prepared);
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    REQUIRE(!fx.done());
    REQUIRE(fx.advance(.6));
    fx.draw();
    const std::vector<uint32_t> before(fx.color().begin(), fx.color().end());
    auto                        invalid = fx.configurationValue();
    invalid.find("options")->set("reach", eve::Value(171.0));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::MagicMissile, invalid));
    REQUIRE(fx.effectTime() == .6);
    REQUIRE(std::vector<uint32_t>(fx.color().begin(), fx.color().end()) == before);
}


// Pinned source defaults. Dense spatial probes additionally cover flight, flash and impact transitions.
TEST_CASE("stylize.pixelfx.core.artilleryRocketAndHomingReference") {
    constexpr uint64_t expected[3][18][5] = {
        {
            {477u, 119776u, 94069u, 68334u, 121635u},
            {4792u, 291744u, 215101u, 134071u, 1221960u},
            {3088u, 536912u, 516018u, 547243u, 515669u},
            {386u, 6755u, 5845u, 2968u, 98430u},
            {5027u, 822601u, 792831u, 855544u, 748490u},
            {122u, 712u, 347u, 62u, 31110u},
            {5821u, 907857u, 873817u, 950995u, 755132u},
            {0u, 0u, 0u, 0u, 0u},
            {6052u, 874797u, 838946u, 923084u, 651793u},
            {0u, 0u, 0u, 0u, 0u},
            {5401u, 735787u, 703677u, 780535u, 434125u},
            {0u, 0u, 0u, 0u, 0u},
            {2892u, 368082u, 350833u, 392904u, 159462u},
            {0u, 0u, 0u, 0u, 0u},
            {0u, 0u, 0u, 0u, 0u},
            {0u, 0u, 0u, 0u, 0u},
            {0u, 0u, 0u, 0u, 0u},
            {0u, 0u, 0u, 0u, 0u},
        },
        {
            {4389u, 537569u, 461695u, 475715u, 662826u},
            {7377u, 177417u, 97848u, 29534u, 1881135u},
            {4475u, 543860u, 465153u, 481373u, 658380u},
            {7462u, 189806u, 103453u, 30514u, 1902810u},
            {4346u, 531523u, 456628u, 471404u, 647100u},
            {7315u, 163931u, 89507u, 26343u, 1865325u},
            {4386u, 529895u, 453625u, 469229u, 653542u},
            {7331u, 164462u, 90125u, 26801u, 1869405u},
            {4113u, 520721u, 444618u, 451823u, 643834u},
            {7255u, 153494u, 84611u, 25424u, 1850025u},
            {4278u, 534863u, 454309u, 464902u, 664940u},
            {7410u, 182417u, 99491u, 29294u, 1889550u},
            {4383u, 545381u, 466881u, 481691u, 665077u},
            {7263u, 143995u, 78954u, 23247u, 1852065u},
            {4270u, 536245u, 456446u, 468819u, 654385u},
            {7332u, 171109u, 93965u, 28154u, 1869660u},
            {4389u, 537569u, 461695u, 475715u, 662826u},
            {7377u, 177417u, 97848u, 29534u, 1881135u},
        },
        {
            {46u, 7315u, 5995u, 5760u, 11730u},
            {2432u, 105894u, 57678u, 17278u, 620160u},
            {444u, 70374u, 58614u, 55183u, 73533u},
            {967u, 26621u, 16447u, 6429u, 246585u},
            {1148u, 155085u, 136192u, 141294u, 165022u},
            {956u, 25785u, 15552u, 5925u, 243780u},
            {3843u, 597775u, 411401u, 354736u, 709088u},
            {4788u, 155569u, 102393u, 47652u, 1220940u},
            {4144u, 453744u, 389543u, 443532u, 540153u},
            {670u, 5644u, 4669u, 1954u, 170850u},
            {3616u, 354689u, 331832u, 392239u, 440265u},
            {55u, 247u, 121u, 18u, 14025u},
            {2831u, 259651u, 242735u, 288259u, 241274u},
            {0u, 0u, 0u, 0u, 0u},
            {28u, 564u, 440u, 724u, 7140u},
            {0u, 0u, 0u, 0u, 0u},
            {0u, 0u, 0u, 0u, 0u},
            {0u, 0u, 0u, 0u, 0u},
        },
    };
    const std::array<PixelFxKind, 3> kinds{{PixelFxKind::Cannonball, PixelFxKind::Rocket, PixelFxKind::HomingMissile}};
    for (size_t e = 0; e < kinds.size(); ++e) {
        PixelFx fx(kinds[e]);
        for (int i = 0; i < 9; ++i) {
            auto prepared = fx.prepareSeek(std::round(double(i) * fx.info().duration * 120 / 8) / 120);
            REQUIRE(prepared);
            REQUIRE(fx.commitSeek(std::move(prepared).takeValue()));
            fx.draw();
            size_t plane = 0;
            for (auto pixels : {fx.color(), fx.light()}) {
                std::array<uint64_t, 5> actual{};
                for (auto pixel : pixels) {
                    actual[0] += pixel != 0 ? 1u : 0u;
                    for (size_t c = 0; c < 4; ++c) actual[c + 1] += (pixel >> (c * 8)) & 255u;
                }
                for (size_t m = 0; m < 5; ++m) {
                    const auto a = actual[m], b = expected[e][size_t(i) * 2 + plane][m], delta = a > b ? a - b : b - a;
                    REQUIRE(delta <= (m == 0 ? 4u : 1020u));
                }
                ++plane;
            }
        }
        auto initial = fx.prepareSeek(.6);
        REQUIRE(initial);
        REQUIRE(fx.commitSeek(std::move(initial).takeValue()));
        fx.draw();
        const std::vector<uint32_t> before(fx.color().begin(), fx.color().end());
        auto                        document = fx.configurationValue();
        auto*                       options  = document.find("options");
        options->set("seed", eve::Value(int64_t(123)));
        options->set("smoke", eve::Value::array({eve::Value(int64_t(0xff403020)), eve::Value(int64_t(0xffd0b090))}));
        if (e == 0) {
            options->set("height", eve::Value(30.0));
            options->set("iron", eve::Value::array({eve::Value(int64_t(0xff806040))}));
        } else {
            options->set("metal",
                         eve::Value::array({eve::Value(int64_t(0xff806040)), eve::Value(int64_t(0xffe0c0a0))}));
            if (e == 1) {
                options->set("paint", eve::Value::array({eve::Value(int64_t(0xff60a0d0))}));
                options->set("speed", eve::Value(190.0));
            } else
                options->set("stone", eve::Value(int64_t(0xff907050)));
        }
        auto candidate = PixelFx::prepareConfigurationValue(kinds[e], document);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        REQUIRE(fx.advance(.6));
        fx.draw();
        const std::vector<uint32_t> changed(fx.color().begin(), fx.color().end());
        REQUIRE(changed != before);
        REQUIRE(fx.settings().projectile.smoke.size() == 7u);
        if (e == 0)
            REQUIRE(fx.settings().projectile.iron.size() == 4u);
        else
            REQUIRE(fx.settings().projectile.metal.size() == (e == 1 ? 7u : 5u));
        auto invalid = fx.configurationValue();
        invalid.find("options")->set(e == 0 ? "iron" : "metal", eve::Value::array({eve::Value(int64_t(0x00112233))}));
        REQUIRE(!PixelFx::prepareConfigurationValue(kinds[e], invalid));
        REQUIRE(fx.effectTime() == .6);
        REQUIRE(std::vector<uint32_t>(fx.color().begin(), fx.color().end()) == changed);
        PixelFx fixed(kinds[e]);
        fixed.configure(fx.settings());
        for (int i = 0; i < 72; ++i) REQUIRE(fixed.advance(1.0 / 120));
        fixed.draw();
        REQUIRE(std::vector<uint32_t>(fixed.color().begin(), fixed.color().end()) == changed);
        REQUIRE(std::vector<uint32_t>(fixed.light().begin(), fixed.light().end()) ==
                std::vector<uint32_t>(fx.light().begin(), fx.light().end()));
    }
}


// Pinned upstream source statistics; full tick-by-tick raw planes verify emission and landed-brand ordering.
TEST_CASE("stylize.pixelfx.core.flareAndGroundExplosionReference") {
    constexpr uint64_t expected[2][18][5] = {
        {
            {7u, 1707u, 1028u, 1101u, 1785u},
            {1348u, 74680u, 22904u, 28004u, 343740u},
            {948u, 118289u, 97236u, 111661u, 133463u},
            {654u, 19459u, 8220u, 9291u, 166770u},
            {1967u, 258038u, 183743u, 213912u, 228379u},
            {12881u, 361991u, 82049u, 115728u, 3284655u},
            {1360u, 202319u, 123296u, 144811u, 161866u},
            {12812u, 330784u, 75317u, 106054u, 3267060u},
            {1111u, 154617u, 95810u, 114007u, 140742u},
            {12683u, 306818u, 70888u, 99138u, 3234165u},
            {866u, 119132u, 72769u, 87211u, 112294u},
            {12646u, 292692u, 64600u, 91911u, 3224730u},
            {557u, 63167u, 44946u, 53964u, 71868u},
            {4540u, 28080u, 4975u, 7500u, 1157700u},
            {356u, 27493u, 24336u, 30228u, 32931u},
            {12u, 20u, 3u, 4u, 3060u},
            {0u, 0u, 0u, 0u, 0u},
            {0u, 0u, 0u, 0u, 0u},
        },
        {
            {1262u, 319990u, 279290u, 197090u, 321810u},
            {9598u, 361701u, 289000u, 214233u, 2447490u},
            {5736u, 632428u, 438925u, 530174u, 1051058u},
            {2322u, 33590u, 25440u, 12722u, 592110u},
            {7827u, 502091u, 443905u, 556496u, 1073209u},
            {1579u, 13256u, 9572u, 3921u, 402645u},
            {5677u, 237168u, 208659u, 270339u, 549785u},
            {313u, 1262u, 601u, 91u, 79815u},
            {2524u, 66173u, 58326u, 76490u, 110487u},
            {0u, 0u, 0u, 0u, 0u},
            {95u, 1825u, 1422u, 2359u, 24225u},
            {0u, 0u, 0u, 0u, 0u},
            {95u, 1825u, 1422u, 2359u, 24225u},
            {0u, 0u, 0u, 0u, 0u},
            {46u, 878u, 684u, 1136u, 11730u},
            {0u, 0u, 0u, 0u, 0u},
            {0u, 0u, 0u, 0u, 0u},
            {0u, 0u, 0u, 0u, 0u},
        },
    };
    const std::array<PixelFxKind, 2> kinds{{PixelFxKind::SignalFlare, PixelFxKind::SmallExplosion}};
    for (size_t e = 0; e < kinds.size(); ++e) {
        PixelFx fx(kinds[e]);
        for (int i = 0; i < 9; ++i) {
            auto seek = fx.prepareSeek(std::round(double(i) * fx.info().duration * 120 / 8) / 120);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            size_t plane = 0;
            for (auto pixels : {fx.color(), fx.light()}) {
                std::array<uint64_t, 5> actual{};
                for (auto pixel : pixels) {
                    actual[0] += pixel != 0 ? 1u : 0u;
                    for (size_t c = 0; c < 4; ++c) actual[c + 1] += (pixel >> (c * 8)) & 255u;
                }
                for (size_t m = 0; m < 5; ++m) {
                    const auto a = actual[m], b = expected[e][size_t(i) * 2 + plane][m], delta = a > b ? a - b : b - a;
                    REQUIRE(delta <= (m == 0 ? 4u : 1020u));
                }
                ++plane;
            }
        }
        auto reset = fx.prepareSeek(.8);
        REQUIRE(reset);
        REQUIRE(fx.commitSeek(std::move(reset).takeValue()));
        fx.draw();
        const std::vector<uint32_t> before(fx.color().begin(), fx.color().end());
        auto                        config  = fx.configurationValue();
        auto*                       options = config.find("options");
        options->set("seed", eve::Value(int64_t(123)));
        if (e == 0) {
            options->set("height", eve::Value(110.0));
            options->set("smoke",
                         eve::Value::array({eve::Value(int64_t(0xff403020)), eve::Value(int64_t(0xffd0b090))}));
        } else {
            options->set("size", eve::Value(1.4));
            options->set("stone", eve::Value(int64_t(0xff907050)));
        }
        auto prepared = PixelFx::prepareConfigurationValue(kinds[e], config);
        REQUIRE(prepared);
        REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
        REQUIRE(fx.advance(.8));
        fx.draw();
        const std::vector<uint32_t> changed(fx.color().begin(), fx.color().end());
        REQUIRE(changed != before);
        if (e == 0)
            REQUIRE(fx.settings().projectile.smoke.size() == 7u);
        else {
            REQUIRE(fx.settings().explosion.size == 1.4);
            REQUIRE(fx.settings().explosion.stone == 0xff907050u);
        }
        auto invalid = fx.configurationValue();
        invalid.find("options")->set(e == 0 ? "height" : "size", eve::Value(0.0));
        REQUIRE(!PixelFx::prepareConfigurationValue(kinds[e], invalid));
        REQUIRE(fx.effectTime() == .8);
        REQUIRE(std::vector<uint32_t>(fx.color().begin(), fx.color().end()) == changed);
        PixelFx split(kinds[e]);
        split.configure(fx.settings());
        for (int i = 0; i < 96; ++i) REQUIRE(split.advance(1.0 / 120));
        split.draw();
        REQUIRE(std::vector<uint32_t>(split.color().begin(), split.color().end()) == changed);
        REQUIRE(std::vector<uint32_t>(split.light().begin(), split.light().end()) ==
                std::vector<uint32_t>(fx.light().begin(), fx.light().end()));
    }
}


TEST_CASE("stylize.pixelfx.core.explosionPlumeColumnReference") {
    using namespace explosion;
    // Independent upstream _lib.js raw-plane FNV-1a hashes and RNG states.
    // Seed 11/123; smooth/cel+inner+hollow; identity/rotated+flipped; nine times.
    const std::array<std::array<uint32_t, 3>, 72> expected{
        {{{1193708239u, 959371049u, 842142669u}},   {{3156554005u, 1116453401u, 842142669u}},
         {{929061995u, 4151876200u, 842142669u}},   {{537564357u, 1159456042u, 4121627113u}},
         {{4275245935u, 4068590092u, 3610097537u}}, {{3493712308u, 4249470633u, 1579131833u}},
         {{1121592147u, 61632377u, 3090991661u}},   {{2630356452u, 1135010757u, 3090991661u}},
         {{1135010757u, 1135010757u, 3090991661u}}, {{1197186549u, 468496899u, 842142669u}},
         {{2101671422u, 408587209u, 842142669u}},   {{4215340460u, 4042000255u, 842142669u}},
         {{379562221u, 4142775899u, 4121627113u}},  {{456118228u, 37125232u, 3610097537u}},
         {{324323970u, 1018459246u, 1579131833u}},  {{2732080842u, 3832768470u, 3090991661u}},
         {{2878886483u, 1135010757u, 3090991661u}}, {{1135010757u, 1135010757u, 3090991661u}},
         {{1193708239u, 1295344841u, 842142669u}},  {{2788431904u, 172039126u, 842142669u}},
         {{2244425397u, 3866385829u, 842142669u}},  {{3974895391u, 1572079872u, 4121627113u}},
         {{3256244901u, 506648913u, 3610097537u}},  {{726987207u, 3347311547u, 1579131833u}},
         {{2568387616u, 2827304320u, 3090991661u}}, {{2207204611u, 1135010757u, 3090991661u}},
         {{1135010757u, 1135010757u, 3090991661u}}, {{1197186549u, 2747082041u, 842142669u}},
         {{1882998461u, 1558087395u, 842142669u}},  {{4215340460u, 1424823578u, 842142669u}},
         {{1902764095u, 742635360u, 4121627113u}},  {{1185708699u, 998263139u, 3610097537u}},
         {{292984462u, 1146909788u, 1579131833u}},  {{2830404776u, 3606327547u, 3090991661u}},
         {{2843435685u, 1135010757u, 3090991661u}}, {{1135010757u, 1135010757u, 3090991661u}},
         {{621769146u, 3324800923u, 842142781u}},   {{2169602884u, 975209549u, 842142781u}},
         {{1055279110u, 3577261954u, 842142781u}},  {{2465535081u, 2064711627u, 4121627225u}},
         {{4020815735u, 2270666542u, 3610097649u}}, {{716230566u, 1992010195u, 1579131945u}},
         {{756023360u, 61632377u, 3090991773u}},    {{1617606778u, 1135010757u, 3090991773u}},
         {{1135010757u, 1135010757u, 3090991773u}}, {{1899113129u, 2975316021u, 842142781u}},
         {{4199123210u, 3621901347u, 842142781u}},  {{654831388u, 657595363u, 842142781u}},
         {{3719014467u, 1136677350u, 4121627225u}}, {{3364075872u, 3865992409u, 3610097649u}},
         {{3633189733u, 1645165038u, 1579131945u}}, {{3384289090u, 3832768470u, 3090991773u}},
         {{593076061u, 1135010757u, 3090991773u}},  {{1135010757u, 1135010757u, 3090991773u}},
         {{621769146u, 4052947833u, 842142781u}},   {{2169602884u, 448422154u, 842142781u}},
         {{2526588137u, 3601610431u, 842142781u}},  {{1929731146u, 905365094u, 4121627225u}},
         {{3557655580u, 1245098089u, 3610097649u}}, {{3767566888u, 3310949361u, 1579131945u}},
         {{1371478525u, 912445276u, 3090991773u}},  {{951184074u, 1135010757u, 3090991773u}},
         {{1135010757u, 1135010757u, 3090991773u}}, {{1899113129u, 3244680030u, 842142781u}},
         {{2569309713u, 2822301025u, 842142781u}},  {{2461280945u, 4109914732u, 842142781u}},
         {{2739782316u, 3529115981u, 4121627225u}}, {{2639948894u, 1697693062u, 3610097649u}},
         {{1807899540u, 2188397325u, 1579131945u}}, {{518620884u, 201619192u, 3090991773u}},
         {{2783919173u, 1135010757u, 3090991773u}}, {{1135010757u, 1135010757u, 3090991773u}}}};
    const std::array<uint32_t, 9> FR{{WHITE, hex(0xfff3a6), hex(0xffe26b), hex(0xffc23d), hex(0xff8a2a), hex(0xf2561d),
                                      hex(0xc9281f), hex(0x871f19), hex(0x4a1712)}};
    const std::array<uint32_t, 5> SM{{hex(0x83777f), hex(0x6e6470), hex(0x575060), hex(0x3f3947), hex(0x29242f)}};
    const PixelFxInfo             effectInfo{PixelFxKind::SmallExplosion, "fixture", 360, 300, 180, 262, 7.2, false};
    std::vector<uint32_t>         colors(360 * 300), lights(360 * 300);
    const auto                    checksum = [](std::span<const uint32_t> pixels) {
        uint32_t h = 2166136261u;
        for (auto c : pixels)
            for (int shift = 0; shift < 32; shift += 8) h = (h ^ ((c >> shift) & 255u)) * 16777619u;
        return h;
    };
    size_t sampleIndex = 0;
    for (uint32_t seed : {11u, 123u})
        for (bool variant : {false, true})
            for (bool transform : {false, true}) {
                Rng          rng(seed);
                PlumeOptions o;
                o.y       = -34;
                o.rise    = 118;
                o.tau     = 1.25;
                o.tube    = {9, 21};
                o.ring    = {2, 19};
                o.grow    = 1;
                o.n       = 42;
                o.size    = {6, 11};
                o.swell   = .9;
                o.spin    = 2.6;
                o.spinTau = 1.7;
                o.heat    = 1.2;
                o.cool    = .42;
                o.tilt    = .22;
                o.squash  = .72;
                o.fade    = {4.4, 6.4};
                o.spread  = 22;
                o.style   = variant ? 1 : 0;
                o.inner   = variant;
                o.hollow  = variant;
                Plume         plume(rng, o);
                ColumnOptions co;
                co.w     = 5;
                co.foot  = 18;
                co.speed = 85;
                co.size  = {7, 11};
                co.n     = 28;
                Column column(co);
                int    tick = 0;
                for (int target : {0, 1, 24, 60, 120, 240, 420, 600, 828}) {
                    while (tick < target) {
                        plume.update(1. / 120);
                        const auto S = plume.shape();
                        column.update(1. / 120, S.y + S.tube * .45, plume.t > .22 && plume.t < 2.5 ? 1 : 0, rng);
                        ++tick;
                    }
                    std::fill(colors.begin(), colors.end(), 0);
                    std::fill(lights.begin(), lights.end(), 0);
                    Paint painter{effectInfo, colors, lights, 1};
                    painter.place(transform ? 7 : 0, transform ? -3 : 0, transform, transform ? .2 : 0);
                    const auto S = plume.shape();
                    column.draw(painter, S.y + S.tube * .45, FR, SM, 1 - plume.t / 1.4, plume.hot());
                    plume.draw(painter, FR, SM);
                    const std::array<uint32_t, 3> actual{{checksum(colors), checksum(lights), rng.state}};
                    REQUIRE(actual == expected[sampleIndex]);
                    ++sampleIndex;
                }
            }
    REQUIRE(sampleIndex == expected.size());
}


TEST_CASE("stylize.pixelfx.core.bigExplosionReferenceAndParameters") {
    // Upstream explosions/big-explosion.js: nine raw color/light FNV-1a pairs.
    const std::array<uint32_t, 18> expected{{3578870388u, 832311920u, 2836011393u, 203372687u, 3042941391u, 1249780999u,
                                             697466308u, 2442836508u, 4268267839u, 1591954280u, 3955770329u,
                                             1476827188u, 1399111704u, 1019046782u, 3863879033u, 1135010757u,
                                             1135010757u, 1135010757u}};
    const auto                     checksum = [](std::span<const uint32_t> pixels) {
        uint32_t h = 2166136261u;
        for (auto c : pixels)
            for (int shift = 0; shift < 32; shift += 8) h = (h ^ ((c >> shift) & 255u)) * 16777619u;
        return h;
    };
    PixelFx fx(PixelFxKind::BigExplosion);
    REQUIRE(fx.info().duration == 7.2);
    REQUIRE(!fx.info().loop);
    for (int i = 0; i < 9; ++i) {
        auto seek = fx.prepareSeek(std::round(i * 7.2 * 120 / 8) / 120);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(checksum(fx.color()) == expected[size_t(i) * 2]);
        REQUIRE(checksum(fx.light()) == expected[size_t(i) * 2 + 1]);
    }
    REQUIRE(fx.done());
    auto reset = fx.prepareSeek(.8);
    REQUIRE(reset);
    REQUIRE(fx.commitSeek(std::move(reset).takeValue()));
    fx.draw();
    const std::vector<uint32_t> original(fx.color().begin(), fx.color().end());
    auto                        config = fx.configurationValue();
    config.find("options")->set("seed", eve::Value(int64_t(123)));
    config.find("options")->set("stone", eve::Value(int64_t(0xff907050)));
    auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::BigExplosion, config);
    REQUIRE(candidate);
    REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
    REQUIRE(fx.advance(.8));
    fx.draw();
    const std::vector<uint32_t> changed(fx.color().begin(), fx.color().end());
    REQUIRE(changed != original);
    REQUIRE(fx.settings().explosion.stone == 0xff907050u);
    PixelFx split(PixelFxKind::BigExplosion);
    split.configure(fx.settings());
    for (int i = 0; i < 96; ++i) REQUIRE(split.advance(1. / 120));
    split.draw();
    REQUIRE(std::vector<uint32_t>(split.color().begin(), split.color().end()) == changed);
    REQUIRE(std::vector<uint32_t>(split.light().begin(), split.light().end()) ==
            std::vector<uint32_t>(fx.light().begin(), fx.light().end()));
    auto invalid = config;
    invalid.find("options")->set("stone", eve::Value(int64_t(0x00907050)));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::BigExplosion, invalid));
    invalid = config;
    invalid.find("options")->set("size", eve::Value(1.4));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::BigExplosion, invalid));
    REQUIRE(fx.effectTime() == .8);
    REQUIRE(std::vector<uint32_t>(fx.color().begin(), fx.color().end()) == changed);
    fx.stop();
    REQUIRE(fx.advance(.3));
    fx.draw();
    REQUIRE(fx.done());
    for (auto c : fx.color()) REQUIRE(c == 0);
    for (auto c : fx.light()) REQUIRE(c == 0);
}


TEST_CASE("stylize.pixelfx.core.barrelExplosionReferenceAndMaterial") {
    // Pinned upstream barrel-explosion.js raw color/light FNV-1a pairs.
    const std::array<uint32_t, 18> expected{{343368601u, 458659517u, 282784868u, 3322925284u, 1899693837u, 1405375398u,
                                             35524388u, 3935566784u, 1260474823u, 3911455064u, 1521232739u, 2744375045u,
                                             1187150289u, 3351715732u, 1669703251u, 2819667973u, 2811896261u,
                                             2811896261u}};
    const auto                     checksum = [](std::span<const uint32_t> pixels) {
        uint32_t h = 2166136261u;
        for (auto c : pixels)
            for (int shift = 0; shift < 32; shift += 8) h = (h ^ ((c >> shift) & 255u)) * 16777619u;
        return h;
    };
    PixelFx fx(PixelFxKind::BarrelExplosion);
    REQUIRE(fx.info().duration == 5.2);
    REQUIRE(!fx.info().loop);
    REQUIRE(fx.settings().explosion.barrel.size() == 6u);
    for (int i = 0; i < 9; ++i) {
        auto seek = fx.prepareSeek(std::round(i * 5.2 * 120 / 8) / 120);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(checksum(fx.color()) == expected[size_t(i) * 2]);
        REQUIRE(checksum(fx.light()) == expected[size_t(i) * 2 + 1]);
    }
    REQUIRE(fx.done());
    auto reset = fx.prepareSeek(.5);
    REQUIRE(reset);
    REQUIRE(fx.commitSeek(std::move(reset).takeValue()));
    fx.draw();
    const std::vector<uint32_t> original(fx.color().begin(), fx.color().end());
    auto                        config = fx.configurationValue();
    config.find("options")->set("seed", eve::Value(int64_t(123)));
    config.find("options")->set("barrel",
                                eve::Value::array({eve::Value(int64_t(0xff403020)), eve::Value(int64_t(0xffd0b090))}));
    auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::BarrelExplosion, config);
    REQUIRE(candidate);
    REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
    REQUIRE(fx.settings().explosion.barrel.size() == 6u);
    config.find("options")->set("barrel", eve::Value::array({}));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::BarrelExplosion, config));
    REQUIRE(fx.advance(.5));
    fx.draw();
    const std::vector<uint32_t> changed(fx.color().begin(), fx.color().end());
    REQUIRE(changed != original);
    auto serialized = fx.configurationValue();
    auto decoded    = PixelFx::prepareConfigurationValue(PixelFxKind::BarrelExplosion, serialized);
    REQUIRE(decoded);
    PixelFx restored(PixelFxKind::BarrelExplosion);
    REQUIRE(restored.commitConfiguration(std::move(decoded).takeValue()));
    REQUIRE(restored.settings().explosion.barrel == fx.settings().explosion.barrel);
    auto invalid = serialized;
    invalid.find("options")->set("barrel", eve::Value::array({eve::Value(int64_t(0x00403020))}));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::BarrelExplosion, invalid));
    invalid = serialized;
    invalid.find("options")->set("size", eve::Value(1.4));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::BarrelExplosion, invalid));
    REQUIRE(fx.effectTime() == .5);
    REQUIRE(std::vector<uint32_t>(fx.color().begin(), fx.color().end()) == changed);
    PixelFx split(PixelFxKind::BarrelExplosion);
    split.configure(fx.settings());
    for (int i = 0; i < 60; ++i) REQUIRE(split.advance(1. / 120));
    split.draw();
    REQUIRE(std::vector<uint32_t>(split.color().begin(), split.color().end()) == changed);
    REQUIRE(std::vector<uint32_t>(split.light().begin(), split.light().end()) ==
            std::vector<uint32_t>(fx.light().begin(), fx.light().end()));
    fx.stop();
    REQUIRE(fx.advance(.3));
    fx.draw();
    REQUIRE(fx.done());
    for (auto c : fx.color()) REQUIRE(c == 0);
    for (auto c : fx.light()) REQUIRE(c == 0);
}


TEST_CASE("stylize.pixelfx.core.dirtAndImplosionReferenceAndConfiguration") {
    // Independent pinned upstream raw-plane FNV-1a pairs at nine samples per recipe.
    const std::array<std::array<uint32_t, 18>, 2> expected{
        {{{3465346181u, 323181241u, 3311179352u, 1329441221u, 1238419689u, 1329441221u, 692589633u, 1329441221u,
           4179786461u, 1329441221u, 1939173722u, 1329441221u, 2724719030u, 1329441221u, 824042725u, 1329441221u,
           1329441221u, 1329441221u}},
         {{3301831761u, 250395925u, 1750583860u, 3434625339u, 3797526405u, 3717235900u, 3800001838u, 4051915106u,
           2471841160u, 4166389813u, 3786992708u, 357429252u, 1882353670u, 3875410411u, 1439997685u, 999866252u,
           3547600325u, 3547600325u}}}};
    const auto checksum = [](std::span<const uint32_t> pixels) {
        uint32_t h = 2166136261u;
        for (auto c : pixels)
            for (int shift = 0; shift < 32; shift += 8) h = (h ^ ((c >> shift) & 255u)) * 16777619u;
        return h;
    };
    const std::array<PixelFxKind, 2> kinds{PixelFxKind::DirtExplosion, PixelFxKind::Implosion};
    for (size_t e = 0; e < kinds.size(); ++e) {
        PixelFx fx(kinds[e]);
        REQUIRE(!fx.info().loop);
        for (int i = 0; i < 9; ++i) {
            auto seek = fx.prepareSeek(std::round(i * fx.info().duration * 120 / 8) / 120);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == expected[e][size_t(i) * 2]);
            REQUIRE(checksum(fx.light()) == expected[e][size_t(i) * 2 + 1]);
        }
        REQUIRE(fx.done());
        auto reset = fx.prepareSeek(.5);
        REQUIRE(reset);
        REQUIRE(fx.commitSeek(std::move(reset).takeValue()));
        fx.draw();
        const std::vector<uint32_t> original(fx.color().begin(), fx.color().end());
        auto                        config = fx.configurationValue();
        config.find("options")->set("seed", eve::Value(int64_t(123)));
        if (e == 0)
            config.find("options")->set(
                "fire", eve::Value::array({eve::Value(int64_t(0xff403020)), eve::Value(int64_t(0xffd0b090))}));
        auto candidate = PixelFx::prepareConfigurationValue(kinds[e], config);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        REQUIRE(fx.advance(.5));
        fx.draw();
        const std::vector<uint32_t> changed(fx.color().begin(), fx.color().end());
        REQUIRE(changed != original);
        if (e == 0) REQUIRE(fx.settings().explosion.fire.size() == 8u);
        auto serialized = fx.configurationValue();
        auto decoded    = PixelFx::prepareConfigurationValue(kinds[e], serialized);
        REQUIRE(decoded);
        PixelFx restored(kinds[e]);
        REQUIRE(restored.commitConfiguration(std::move(decoded).takeValue()));
        REQUIRE(restored.settings().explosion.fire == fx.settings().explosion.fire);
        auto invalid = serialized;
        invalid.find("options")->set(e == 0 ? "fire" : "barrel", eve::Value::array({}));
        REQUIRE(!PixelFx::prepareConfigurationValue(kinds[e], invalid));
        if (e == 0) {
            invalid = serialized;
            invalid.find("options")->set("fire", eve::Value::array({eve::Value(int64_t(0x00403020))}));
            REQUIRE(!PixelFx::prepareConfigurationValue(kinds[e], invalid));
        }
        invalid = serialized;
        invalid.find("options")->set("size", eve::Value(1.4));
        REQUIRE(!PixelFx::prepareConfigurationValue(kinds[e], invalid));
        REQUIRE(fx.effectTime() == .5);
        REQUIRE(std::vector<uint32_t>(fx.color().begin(), fx.color().end()) == changed);
        PixelFx split(kinds[e]);
        split.configure(fx.settings());
        for (int i = 0; i < 60; ++i) REQUIRE(split.advance(1. / 120));
        split.draw();
        REQUIRE(std::vector<uint32_t>(split.color().begin(), split.color().end()) == changed);
        REQUIRE(std::vector<uint32_t>(split.light().begin(), split.light().end()) ==
                std::vector<uint32_t>(fx.light().begin(), fx.light().end()));
        fx.stop();
        REQUIRE(fx.advance(.3));
        fx.draw();
        REQUIRE(fx.done());
        for (auto c : fx.color()) REQUIRE(c == 0);
        for (auto c : fx.light()) REQUIRE(c == 0);
    }
}


TEST_CASE("stylize.pixelfx.core.fireworksReferenceAndMaterials") {
    // Pinned upstream fireworks: nine independent raw color/light FNV-1a pairs each.
    const std::array<std::array<uint32_t, 18>, 2> expected{
        {{{3411241948u, 1447093461u, 2699529247u, 561436267u, 1766358889u, 797201402u, 3296876110u, 264752186u,
           2467603027u, 1638991063u, 1739537081u, 2876065689u, 1690158145u, 3459074337u, 2269994842u, 324240837u,
           324240837u, 324240837u}},
         {{1327716529u, 1668531749u, 4196123676u, 4099079016u, 2984163u, 2044801898u, 1894655706u, 1914436595u,
           2535878751u, 752687996u, 2108427268u, 1491254193u, 3375936300u, 2479238451u, 164235836u, 2726928565u,
           45744837u, 3683687843u}}}};
    const auto checksum = [](std::span<const uint32_t> pixels) {
        uint32_t h = 2166136261u;
        for (auto c : pixels)
            for (int shift = 0; shift < 32; shift += 8) h = (h ^ ((c >> shift) & 255u)) * 16777619u;
        return h;
    };
    const std::array<PixelFxKind, 2> kinds{PixelFxKind::FireworkBurst, PixelFxKind::FireworkRocket};
    for (size_t e = 0; e < kinds.size(); ++e) {
        PixelFx fx(kinds[e]);
        REQUIRE(!fx.info().loop);
        for (int i = 0; i < 9; ++i) {
            auto seek = fx.prepareSeek(std::round(i * fx.info().duration * 120 / 8) / 120);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == expected[e][size_t(i) * 2]);
            REQUIRE(checksum(fx.light()) == expected[e][size_t(i) * 2 + 1]);
        }
        REQUIRE(fx.done());
        auto reset = fx.prepareSeek(.5);
        REQUIRE(reset);
        REQUIRE(fx.commitSeek(std::move(reset).takeValue()));
        fx.draw();
        const std::vector<uint32_t> original(fx.color().begin(), fx.color().end());
        const char*                 ramp   = e == 0 ? "core" : "tips";
        auto                        config = fx.configurationValue();
        config.find("options")->set("seed", eve::Value(int64_t(123)));
        config.find("options")->set(
            ramp, eve::Value::array({eve::Value(int64_t(0xff403020)), eve::Value(int64_t(0xffd0b090))}));
        auto candidate = PixelFx::prepareConfigurationValue(kinds[e], config);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        REQUIRE(fx.advance(.5));
        fx.draw();
        const std::vector<uint32_t> changed(fx.color().begin(), fx.color().end());
        REQUIRE(changed != original);
        const auto settings = fx.settings();
        REQUIRE((e == 0 ? settings.explosion.core : settings.explosion.tips).size() == (e == 0 ? 7u : 6u));
        auto serialized = fx.configurationValue();
        auto decoded    = PixelFx::prepareConfigurationValue(kinds[e], serialized);
        REQUIRE(decoded);
        PixelFx restored(kinds[e]);
        REQUIRE(restored.commitConfiguration(std::move(decoded).takeValue()));
        REQUIRE(restored.settings().explosion.core == settings.explosion.core);
        REQUIRE(restored.settings().explosion.tips == settings.explosion.tips);
        auto invalid = serialized;
        invalid.find("options")->set(ramp, eve::Value::array({}));
        REQUIRE(!PixelFx::prepareConfigurationValue(kinds[e], invalid));
        invalid = serialized;
        invalid.find("options")->set(ramp, eve::Value::array({eve::Value(int64_t(0x00403020))}));
        REQUIRE(!PixelFx::prepareConfigurationValue(kinds[e], invalid));
        invalid = serialized;
        invalid.find("options")->set("size", eve::Value(1.4));
        REQUIRE(!PixelFx::prepareConfigurationValue(kinds[e], invalid));
        REQUIRE(fx.effectTime() == .5);
        REQUIRE(std::vector<uint32_t>(fx.color().begin(), fx.color().end()) == changed);
        PixelFx split(kinds[e]);
        split.configure(settings);
        for (int i = 0; i < 60; ++i) REQUIRE(split.advance(1. / 120));
        split.draw();
        REQUIRE(std::vector<uint32_t>(split.color().begin(), split.color().end()) == changed);
        REQUIRE(std::vector<uint32_t>(split.light().begin(), split.light().end()) ==
                std::vector<uint32_t>(fx.light().begin(), fx.light().end()));
        fx.stop();
        REQUIRE(fx.advance(.3));
        fx.draw();
        REQUIRE(fx.done());
        for (auto c : fx.color()) REQUIRE(c == 0);
        for (auto c : fx.light()) REQUIRE(c == 0);
    }
}


TEST_CASE("stylize.pixelfx.core.plasmaAndPowReferenceAndConfiguration") {
    // Pinned upstream raw color/light FNV-1a pairs at nine samples per recipe.
    const std::array<std::array<uint32_t, 18>, 2> expected{
        {{{86387716u, 2339403523u, 869771233u, 1234243067u, 1626319295u, 3233432455u, 2101860980u, 2746310917u,
           3086947786u, 2709774786u, 3630930865u, 2775439443u, 3200627249u, 2231790070u, 1752616389u, 1752616389u,
           1752616389u, 1752616389u}},
         {{2127971067u, 1115003077u, 3985030397u, 1033259087u, 4094314631u, 1907309511u, 746341142u, 739666797u,
           2171089606u, 653306029u, 4252445555u, 653306029u, 1014308091u, 1478890913u, 1273339709u, 869696469u,
           4252415429u, 4252415429u}}}};
    const auto checksum = [](std::span<const uint32_t> pixels) {
        uint32_t h = 2166136261u;
        for (auto c : pixels)
            for (int shift = 0; shift < 32; shift += 8) h = (h ^ ((c >> shift) & 255u)) * 16777619u;
        return h;
    };
    const std::array<PixelFxKind, 2> kinds{PixelFxKind::PlasmaExplosion, PixelFxKind::CartoonPow};
    for (size_t e = 0; e < kinds.size(); ++e) {
        PixelFx fx(kinds[e]);
        REQUIRE(!fx.info().loop);
        for (int i = 0; i < 9; ++i) {
            auto seek = fx.prepareSeek(std::round(i * fx.info().duration * 120 / 8) / 120);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == expected[e][size_t(i) * 2]);
            REQUIRE(checksum(fx.light()) == expected[e][size_t(i) * 2 + 1]);
        }
        REQUIRE(fx.done());
        auto reset = fx.prepareSeek(.2);
        REQUIRE(reset);
        REQUIRE(fx.commitSeek(std::move(reset).takeValue()));
        fx.draw();
        const std::vector<uint32_t> original(fx.color().begin(), fx.color().end());
        auto                        config = fx.configurationValue();
        config.find("options")->set("seed", eve::Value(int64_t(123)));
        if (e == 0)
            config.find("options")->set(
                "accent", eve::Value::array({eve::Value(int64_t(0xff403020)), eve::Value(int64_t(0xffd0b090))}));
        else
            config.find("options")->set("text", eve::Value("BAM?"));
        auto candidate = PixelFx::prepareConfigurationValue(kinds[e], config);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        REQUIRE(fx.advance(.2));
        fx.draw();
        const std::vector<uint32_t> changed(fx.color().begin(), fx.color().end());
        REQUIRE(changed != original);
        auto serialized = fx.configurationValue();
        auto decoded    = PixelFx::prepareConfigurationValue(kinds[e], serialized);
        REQUIRE(decoded);
        PixelFx restored(kinds[e]);
        REQUIRE(restored.commitConfiguration(std::move(decoded).takeValue()));
        REQUIRE(restored.settings().explosion.accent == fx.settings().explosion.accent);
        REQUIRE(restored.settings().explosion.text == fx.settings().explosion.text);
        auto invalid = serialized;
        invalid.find("options")->set(e == 0 ? "accent" : "text", eve::Value(int64_t(17)));
        REQUIRE(!PixelFx::prepareConfigurationValue(kinds[e], invalid));
        REQUIRE(fx.effectTime() == .2);
        REQUIRE(std::vector<uint32_t>(fx.color().begin(), fx.color().end()) == changed);
        PixelFx split(kinds[e]);
        split.configure(fx.settings());
        for (int i = 0; i < 24; ++i) REQUIRE(split.advance(1. / 120));
        split.draw();
        REQUIRE(std::vector<uint32_t>(split.color().begin(), split.color().end()) == changed);
        REQUIRE(std::vector<uint32_t>(split.light().begin(), split.light().end()) ==
                std::vector<uint32_t>(fx.light().begin(), fx.light().end()));
        fx.stop();
        REQUIRE(fx.advance(.3));
        fx.draw();
        REQUIRE(fx.done());
        for (auto c : fx.color()) REQUIRE(c == 0);
        for (auto c : fx.light()) REQUIRE(c == 0);
    }
}
TEST_CASE("stylize.pixelfx.core.cartoonTextUtf8BoundsAndOwnership") {
    auto metadata = PixelFx::describeParameters(PixelFxKind::CartoonPow);
    REQUIRE(metadata);
    bool found = false;
    for (const auto& p : *metadata.value().find("parameters")->getIf<eve::Value::Array>())
        if (p.find("name")->asString() == "text") {
            found = true;
            REQUIRE(p.find("type")->asString() == "string");
            REQUIRE(p.find("lengthUnit")->asString() == "unicode-scalars");
            REQUIRE(p.find("encoding")->asString() == "utf-8");
            REQUIRE(p.find("minimum")->asInt() == 0);
            REQUIRE(p.find("maximum")->asInt() == 64);
        }
    REQUIRE(found);
    PixelFx fx(PixelFxKind::CartoonPow);
    auto    config = fx.configurationValue();
    config.find("options")->set("text", eve::Value("BAM?"));
    auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::CartoonPow, config);
    REQUIRE(candidate);
    config.find("options")->set("text", eve::Value("changed later"));
    REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
    REQUIRE(fx.settings().explosion.text == "BAM?");
    REQUIRE(fx.advance(.2));
    fx.draw();
    const std::vector<uint32_t> before(fx.color().begin(), fx.color().end());
    auto                        settings = fx.settings();
    settings.explosion.text              = "";
    REQUIRE(PixelFx::prepareConfiguration(PixelFxKind::CartoonPow, settings));
    settings.explosion.text = std::string(64, 'A');
    REQUIRE(PixelFx::prepareConfiguration(PixelFxKind::CartoonPow, settings));
    settings.explosion.text += "!";
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::CartoonPow, settings));
    std::string unicode;
    for (int i = 0; i < 64; ++i) unicode += "\xf0\x9f\x98\x80";
    settings.explosion.text = unicode;
    REQUIRE(PixelFx::prepareConfiguration(PixelFxKind::CartoonPow, settings));
    settings.explosion.text += "!";
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::CartoonPow, settings));
    for (const std::string& bad : {std::string("\xc0\xaf"), std::string("\xed\xa0\x80"),
                                   std::string("\xf4\x90\x80\x80"), std::string("\xf0\x9f")}) {
        settings.explosion.text = bad;
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::CartoonPow, settings));
    }
    REQUIRE(fx.effectTime() == .2);
    REQUIRE(std::vector<uint32_t>(fx.color().begin(), fx.color().end()) == before);
}

TEST_CASE("stylize.pixelfx.core.grenadeAndSmokeReferenceAndMaterials") {
    // Pinned upstream grenade/smoke: nine independent raw color/light FNV-1a pairs each.
    const std::array<std::array<uint32_t, 18>, 2> expected{
        {{{1574027761u, 56570441u, 2883747750u, 2016152873u, 2858798792u, 2648408874u, 4001389254u, 987260279u,
           859776821u, 369753554u, 984067546u, 2835575395u, 1684475048u, 2183910127u, 518385794u, 2277878414u,
           996707269u, 996707269u}},
         {{628212009u, 4212613393u, 1679576980u, 3296235047u, 2303950351u, 2693869890u, 3658667663u, 4199864773u,
           4230083673u, 4199864773u, 4113443602u, 4199864773u, 2729516228u, 4199864773u, 2138126154u, 4199864773u,
           4199864773u, 4199864773u}}}};
    const auto checksum = [](std::span<const uint32_t> pixels) {
        uint32_t h = 2166136261u;
        for (auto c : pixels)
            for (int shift = 0; shift < 32; shift += 8) h = (h ^ ((c >> shift) & 255u)) * 16777619u;
        return h;
    };
    const std::array<PixelFxKind, 2> kinds{PixelFxKind::Grenade, PixelFxKind::SmokeBomb};
    for (size_t e = 0; e < kinds.size(); ++e) {
        PixelFx fx(kinds[e]);
        REQUIRE(!fx.info().loop);
        for (int i = 0; i < 9; ++i) {
            auto seek = fx.prepareSeek(std::round(i * fx.info().duration * 120 / 8) / 120);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == expected[e][size_t(i) * 2]);
            REQUIRE(checksum(fx.light()) == expected[e][size_t(i) * 2 + 1]);
        }
        REQUIRE(fx.done());
        auto reset = fx.prepareSeek(.5);
        REQUIRE(reset);
        REQUIRE(fx.commitSeek(std::move(reset).takeValue()));
        fx.draw();
        const std::vector<uint32_t> original(fx.color().begin(), fx.color().end());
        const char*                 ramp   = e == 0 ? "body" : "canister";
        auto                        config = fx.configurationValue();
        config.find("options")->set("seed", eve::Value(int64_t(123)));
        config.find("options")->set(
            ramp, eve::Value::array({eve::Value(int64_t(0xff403020)), eve::Value(int64_t(0xffd0b090))}));
        auto candidate = PixelFx::prepareConfigurationValue(kinds[e], config);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        REQUIRE(fx.advance(.5));
        fx.draw();
        const std::vector<uint32_t> changed(fx.color().begin(), fx.color().end());
        REQUIRE(changed != original);
        const auto settings = fx.settings();
        REQUIRE((e == 0 ? settings.explosion.body : settings.explosion.canister).size() == 5u);
        auto serialized = fx.configurationValue();
        auto decoded    = PixelFx::prepareConfigurationValue(kinds[e], serialized);
        REQUIRE(decoded);
        PixelFx restored(kinds[e]);
        REQUIRE(restored.commitConfiguration(std::move(decoded).takeValue()));
        REQUIRE(restored.settings().explosion.body == settings.explosion.body);
        REQUIRE(restored.settings().explosion.canister == settings.explosion.canister);
        auto invalid = serialized;
        invalid.find("options")->set(ramp, eve::Value::array({}));
        REQUIRE(!PixelFx::prepareConfigurationValue(kinds[e], invalid));
        invalid = serialized;
        invalid.find("options")->set(ramp, eve::Value::array({eve::Value(int64_t(0x00403020))}));
        REQUIRE(!PixelFx::prepareConfigurationValue(kinds[e], invalid));
        invalid = serialized;
        invalid.find("options")->set("size", eve::Value(1.4));
        REQUIRE(!PixelFx::prepareConfigurationValue(kinds[e], invalid));
        REQUIRE(fx.effectTime() == .5);
        REQUIRE(std::vector<uint32_t>(fx.color().begin(), fx.color().end()) == changed);
        PixelFx split(kinds[e]);
        split.configure(settings);
        for (int i = 0; i < 60; ++i) REQUIRE(split.advance(1. / 120));
        split.draw();
        REQUIRE(std::vector<uint32_t>(split.color().begin(), split.color().end()) == changed);
        REQUIRE(std::vector<uint32_t>(split.light().begin(), split.light().end()) ==
                std::vector<uint32_t>(fx.light().begin(), fx.light().end()));
        fx.stop();
        REQUIRE(fx.advance(.3));
        fx.draw();
        REQUIRE(fx.done());
        for (auto c : fx.color()) REQUIRE(c == 0);
        for (auto c : fx.light()) REQUIRE(c == 0);
    }
}


TEST_CASE("stylize.pixelfx.core.smokeSparkRampResolvesSourceOptions") {
    auto metadata = PixelFx::describeParameters(PixelFxKind::SmokeBomb);
    REQUIRE(metadata);
    bool found = false;
    for (const auto& p : *metadata.value().find("parameters")->getIf<eve::Value::Array>())
        if (p.find("name")->asString() == "sparks") {
            found = true;
            REQUIRE(p.find("rampSteps")->asInt() == 4);
            REQUIRE(p.find("minimum")->asInt() == 1);
            REQUIRE(p.find("maximum")->asInt() == 64);
        }
    REQUIRE(found);
    PixelFx fx(PixelFxKind::SmokeBomb);
    REQUIRE(fx.settings().explosion.sparks.size() == 4u);
    for (size_t n : {1u, 3u, 5u, 64u}) {
        auto settings = fx.settings();
        settings.explosion.sparks.clear();
        for (size_t i = 0; i < n; ++i) settings.explosion.sparks.push_back(0xff302010u + uint32_t(i));
        const auto expected  = fit(settings.explosion.sparks, 4);
        auto       candidate = PixelFx::prepareConfiguration(PixelFxKind::SmokeBomb, settings);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        REQUIRE(fx.settings().explosion.sparks == expected);
        auto encoded = fx.configurationValue();
        auto decoded = PixelFx::prepareConfigurationValue(PixelFxKind::SmokeBomb, encoded);
        REQUIRE(decoded);
        PixelFx restored(PixelFxKind::SmokeBomb);
        REQUIRE(restored.commitConfiguration(std::move(decoded).takeValue()));
        REQUIRE(restored.settings().explosion.sparks == expected);
    }
    auto settings = fx.settings();
    settings.explosion.sparks.clear();
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::SmokeBomb, settings));
    settings.explosion.sparks.resize(65, WHITE);
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::SmokeBomb, settings));
    settings.explosion.sparks = {WHITE, 0x00302010u, WHITE};
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::SmokeBomb, settings));
    REQUIRE(fx.settings().explosion.sparks.size() == 4u);
}


TEST_CASE("stylize.pixelfx.core.nukeReferenceFrayAndDust") {
    // Pinned upstream nuke.js hashes include fraying at 4.5/5.625/6.75 seconds.
    const std::array<uint32_t, 18> expected{{3622216021u, 588179501u, 2742435809u, 3526265298u, 2083569722u,
                                             1521851665u, 2544689264u, 2630103473u, 672373420u, 1375329551u,
                                             3825821247u, 1765989829u, 330318834u, 1765989829u, 3662002528u,
                                             1765989829u, 1765989829u, 1765989829u}};
    const auto                     checksum = [](std::span<const uint32_t> pixels) {
        uint32_t h = 2166136261u;
        for (auto c : pixels)
            for (int shift = 0; shift < 32; shift += 8) h = (h ^ ((c >> shift) & 255u)) * 16777619u;
        return h;
    };
    PixelFx fx(PixelFxKind::Nuke);
    for (int i = 0; i < 9; ++i) {
        auto seek = fx.prepareSeek(i * 9. / 8);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(checksum(fx.color()) == expected[size_t(i) * 2]);
        REQUIRE(checksum(fx.light()) == expected[size_t(i) * 2 + 1]);
    }
    REQUIRE(fx.done());
    auto reset = fx.prepareSeek(.5);
    REQUIRE(reset);
    REQUIRE(fx.commitSeek(std::move(reset).takeValue()));
    fx.draw();
    const std::vector<uint32_t> original(fx.color().begin(), fx.color().end());
    auto                        config = fx.configurationValue();
    config.find("options")->set("seed", eve::Value(int64_t(123)));
    config.find("options")->set("dust", eve::Value(int64_t(0xffc08050)));
    auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::Nuke, config);
    REQUIRE(candidate);
    REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
    REQUIRE(fx.advance(.5));
    fx.draw();
    const std::vector<uint32_t> changed(fx.color().begin(), fx.color().end());
    REQUIRE(changed != original);
    REQUIRE(fx.settings().explosion.dust == 0xffc08050u);
    auto invalid = config;
    invalid.find("options")->set("dust", eve::Value(int64_t(0x00c08050)));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::Nuke, invalid));
    REQUIRE(fx.effectTime() == .5);
    REQUIRE(std::vector<uint32_t>(fx.color().begin(), fx.color().end()) == changed);
    PixelFx split(PixelFxKind::Nuke);
    split.configure(fx.settings());
    for (int i = 0; i < 60; ++i) REQUIRE(split.advance(1. / 120));
    split.draw();
    REQUIRE(std::vector<uint32_t>(split.color().begin(), split.color().end()) == changed);
    REQUIRE(std::vector<uint32_t>(split.light().begin(), split.light().end()) ==
            std::vector<uint32_t>(fx.light().begin(), fx.light().end()));
    fx.stop();
    REQUIRE(fx.advance(.3));
    fx.draw();
    REQUIRE(fx.done());
    for (auto c : fx.color()) REQUIRE(c == 0);
    for (auto c : fx.light()) REQUIRE(c == 0);
}

}  // namespace test_pixelfx_core_unit
#endif
