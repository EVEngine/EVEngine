#include "stylize/AttackVfxRecipe.h"
#include "stylize/AttackVfxRuntime.h"

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

const char* kWaterWhipRecipe = R"({
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
    auto failed = slot.reload(R"({"schemaVersion":99})");
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
    auto played = runtime.play(id("attackvfx:water_whip"), request);
    REQUIRE(played.ok());
    auto handle = std::move(played).takeValue();
    REQUIRE_EQ(runtime.activeCount(), 1u);

    auto snap = runtime.inspect(handle);
    REQUIRE(snap.has_value());
    REQUIRE(snap->activeSkinId.has_value());
    REQUIRE_EQ(snap->activeSkinId->format(), "skin:water");
    REQUIRE(snap->phases[0].active);     // release
    REQUIRE(!snap->phases[1].active);    // impact waits for cue
    REQUIRE(!snap->phases[2].active);    // status waits for cue

    auto signaled = runtime.signal(handle, "impact");
    REQUIRE(signaled.ok());
    bool sawImpactEnter = false;
    bool sawStatusEnter = false;
    bool sawCueConsumed = false;
    for (const auto& event : signaled.value().events) {
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
    for (const auto& event : advanced.value().events) {
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
    auto played = runtime.play(id("attackvfx:sweep"), request);
    REQUIRE(played.ok());
    auto handle = std::move(played).takeValue();
    auto snap = runtime.inspect(handle);
    REQUIRE(snap.has_value());
    REQUIRE_EQ(snap->activeSkinId->format(), "skin:fire");

    auto cancelled = runtime.signal(handle, "cancel");
    REQUIRE(cancelled.ok());
    REQUIRE_EQ(runtime.activeCount(), 0u);
    bool sawStopped = false;
    for (const auto& event : cancelled.value().events) {
        if (event.kind == AttackVfxFrameEvent::Kind::InstanceStopped) sawStopped = true;
    }
    REQUIRE(sawStopped);
}

TEST_CASE("stylize.attack_vfx runtime rejects stale handles and missing recipes") {
    AttackVfxRuntime runtime;
    auto missing = runtime.play(id("attackvfx:missing"), {});
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
