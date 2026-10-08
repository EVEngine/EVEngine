#include "common/Capability.h"
#include "stylize/AttackVfxLayerExecutor.h"
#include "stylize/AttackVfxRecipe.h"
#include "stylize/AttackVfxRuntime.h"
#include "stylize/Stylize.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <string_view>

using namespace eve::stylize;

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
    auto* trail = executorFor(AttackVfxLayerRole::Trail);
    auto* distortion = executorFor(AttackVfxLayerRole::Distortion);
    REQUIRE(trail != nullptr);
    REQUIRE(distortion != nullptr);

    AttackVfxLayer trailLayer;
    trailLayer.role = AttackVfxLayerRole::Trail;
    trailLayer.uri  = "trail://blade";
    trailLayer.floatParams["lifetime"] = 0.2f;
    AttackVfxRequest play;
    play.sourceId = 3;
    play.targetId = 8;
    AttackVfxLayerStartRequest request;
    request.layer       = &trailLayer;
    request.playRequest = &play;
    auto started = trail->start(request);
    REQUIRE(started.ok());
    CHECK(trail->update(started.value(), 0.05, request).ok());
    CHECK(trail->stop(started.value(), AttackVfxStopBehavior::ClearImmediately).ok());

    AttackVfxLayer distortionLayer;
    distortionLayer.role = AttackVfxLayerRole::Distortion;
    distortionLayer.floatParams["strength"] = 0.35f;
    request.layer = &distortionLayer;
    started = distortion->start(request);
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
