#include "common/Capability.h"
#include "decal/Decal.h"
#include "stylize/AttackVfxLayerExecutor.h"
#include "stylize/AttackVfxRecipe.h"
#include "stylize/AttackVfxRuntime.h"
#include "stylize/Stylize.h"
#include "stylize/action/StylizeAction.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <string_view>

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
    CHECK(started.code() == eve::StatusCode::NotFound ||
          started.code() == eve::StatusCode::Failed);
}

TEST_CASE("stylize.attack_vfx.layers decal solid executor") {
    auto* stylize = Stylize::create();
    auto* decal   = eve::decal::Decal::create();
    REQUIRE(stylize != nullptr);
    REQUIRE(decal != nullptr);

    auto* executor = executorFor(AttackVfxLayerRole::Decal);
    REQUIRE(executor != nullptr);

    AttackVfxLayer layer;
    layer.role = AttackVfxLayerRole::Decal;
    layer.uri  = "decal://scorch";
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
        {"kind":"windup","durationSeconds":0.1,"layers":[
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
    const char* recipeJson = R"({
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
