#include "action/ActionAttackVfxBlock.h"
#include "action/ActionCameraBlock.h"
#include "action/ActionNotifyRegistry.h"
#include "common/Capability.h"
#include "stylize/AttackVfxLayerExecutor.h"
#include "stylize/AttackVfxRecipe.h"
#include "stylize/AttackVfxRuntime.h"
#include "stylize/action/StylizeAction.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <optional>
#include <string>
#include <string_view>

using namespace eve::stylize;
using namespace eve::stylize_action;

namespace {

eve::LogicalId id(std::string_view value) {
    auto parsed = eve::LogicalId::parse(value);
    REQUIRE(parsed.has_value());
    return std::move(*parsed);
}

const char* kMinimalRecipe = R"({
  "schema":"eve.stylize.attack-vfx","schemaVersion":1,"id":"attackvfx:pulse",
  "phases":[{"kind":"release","durationSeconds":0.5,"layers":[
    {"role":"camera","floatParams":{"posAmp":0.1,"duration":0.08}},
    {"role":"particles","uri":"particles://missing"}
  ]}]
})";

class FakeCameraSink final : public eve::action::IActionCameraCueSink {
public:
    bool supports(const eve::LogicalId&) const noexcept override { return true; }
    eve::Result<void> trigger(const eve::action::ActionCameraCueBinding& binding,
                              const eve::action::ActionNotifyContext&) override {
        ++calls;
        last = binding;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    int calls = 0;
    std::optional<eve::action::ActionCameraCueBinding> last;
};

IAttackVfxLayerExecutor* cameraExecutor() {
    IAttackVfxLayerExecutor* found = nullptr;
    eve::cap::forEach<IAttackVfxLayerExecutor>([&](IAttackVfxLayerExecutor* executor) {
        if (!found && executor && executor->role() == AttackVfxLayerRole::Camera) found = executor;
    });
    return found;
}

}  // namespace

TEST_CASE("stylize.action.attack_vfx binding validates recipe xor uri") {
    auto both = eve::action::ActionAttackVfxBinding::fromPayload(
        {{"recipeId", "attackvfx:pulse"}, {"uri", "json:{}"}, {"lifetimeSeconds", 0.2}},
        eve::action::ActionAttackVfxShape::Instant);
    REQUIRE(!both.ok());

    auto neither = eve::action::ActionAttackVfxBinding::fromPayload(
        {{"lifetimeSeconds", 0.2}}, eve::action::ActionAttackVfxShape::Instant);
    REQUIRE(!neither.ok());

    auto ok = eve::action::ActionAttackVfxBinding::fromPayload(
        {{"uri", std::string("json:") + kMinimalRecipe}, {"lifetimeSeconds", 0.4}},
        eve::action::ActionAttackVfxShape::Instant);
    REQUIRE(ok.ok());
    REQUIRE(!ok.value().recipeId.isValid());
    REQUIRE(!ok.value().uri.empty());
}

TEST_CASE("stylize.action.attack_vfx camera executor routes to optional sink") {
    FakeCameraSink sink;
    eve::cap::addListener<eve::action::IActionCameraCueSink>(&sink);
    auto* module = StylizeAction::create();
    REQUIRE(module != nullptr);
    auto* executor = cameraExecutor();
    REQUIRE(executor != nullptr);

    AttackVfxLayer layer;
    layer.role = AttackVfxLayerRole::Camera;
    layer.floatParams["posAmp"]   = 0.2f;
    layer.floatParams["rotAmp"]   = 1.0f;
    layer.floatParams["duration"] = 0.15f;

    AttackVfxRequest playRequest;
    playRequest.sourceId = 7;
    playRequest.targetId = 9;

    AttackVfxLayerStartRequest request;
    request.layer       = &layer;
    request.playRequest = &playRequest;

    auto started = executor->start(request);
    REQUIRE(started.ok());
    REQUIRE_EQ(sink.calls, 1);
    REQUIRE(sink.last.has_value());
    CHECK_EQ(sink.last->positionAmplitude, 0.2);
    CHECK_EQ(sink.last->duration.seconds(), 0.15);
    REQUIRE(started.value().valid());
    CHECK(executor->stop(started.value(), AttackVfxStopBehavior::ClearImmediately).ok());

    eve::cap::removeListener<eve::action::IActionCameraCueSink>(&sink);
}

TEST_CASE("stylize.action.attack_vfx runtime plays with soft-skipped missing backends") {
    FakeCameraSink sink;
    eve::cap::addListener<eve::action::IActionCameraCueSink>(&sink);
    auto* module = StylizeAction::create();
    REQUIRE(module != nullptr);
    REQUIRE(module->registerRecipeJson(kMinimalRecipe).ok());

    // Camera executor is present; particles executor is not (Particles module unloaded).
    // Play must succeed: missing particles role soft-skips, camera impulse fires.
    auto played = module->runtime().play(id("attackvfx:pulse"), {});
    REQUIRE(played.ok());
    REQUIRE_EQ(sink.calls, 1);
    REQUIRE_EQ(module->runtime().activeCount(), 1u);

    auto advanced = module->runtime().advance(1.0);
    REQUIRE(advanced.ok());
    REQUIRE_EQ(module->runtime().activeCount(), 0u);

    eve::cap::removeListener<eve::action::IActionCameraCueSink>(&sink);
}

TEST_CASE("stylize.action.attack_vfx action notify plays registered recipe") {
    FakeCameraSink sink;
    eve::cap::addListener<eve::action::IActionCameraCueSink>(&sink);
    auto* module = StylizeAction::create();
    REQUIRE(module != nullptr);
    REQUIRE(module->registerRecipeJson(kMinimalRecipe).ok());

    auto registry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());
    REQUIRE(registry.value().hasHandler("presentation:attack-vfx"));

    eve::action::ActionTimelineEvent event;
    event.kind    = eve::action::ActionTimelineEventKind::Notify;
    event.type    = id("presentation:attack-vfx");
    event.itemId  = id("item:pulse");
    event.payload = {{"recipeId", "attackvfx:pulse"}, {"lifetimeSeconds", 0.25}};

    eve::action::ActionNotifyContext context;
    context.executionId = eve::action::ActionExecutionId{1};
    context.time        = eve::Duration::fromSeconds(0.0).value();

    auto handled = registry.value().dispatch(event, context);
    REQUIRE(handled.ok());
    REQUIRE_EQ(module->runtime().activeCount(), 1u);
    REQUIRE_EQ(sink.calls, 1);

    context.time = eve::Duration::fromSeconds(0.3).value();
    auto advanced = registry.value().advanceHandlers(context);
    REQUIRE(advanced.ok());
    REQUIRE_EQ(module->runtime().activeCount(), 0u);

    // Second advance after the instant recipe already completed must not fail on stale stop.
    context.time = eve::Duration::fromSeconds(0.6).value();
    advanced = registry.value().advanceHandlers(context);
    REQUIRE(advanced.ok());

    eve::cap::removeListener<eve::action::IActionCameraCueSink>(&sink);
}

TEST_CASE("stylize.action.attack_vfx cues are sorted by offset") {
    auto binding = eve::action::ActionAttackVfxBinding::fromPayload(
        {{"uri", std::string("json:") + kMinimalRecipe},
         {"lifetimeSeconds", 1.0},
         {"cues", eve::Value::Array{
                      eve::Value::Object{{"offsetSeconds", 0.5}, {"cue", "late"}},
                      eve::Value::Object{{"offsetSeconds", 0.1}, {"cue", "early"}},
                  }}},
        eve::action::ActionAttackVfxShape::Instant);
    REQUIRE(binding.ok());
    REQUIRE_EQ(binding.value().cues.size(), 2u);
    CHECK_EQ(binding.value().cues[0].cue, "early");
    CHECK_EQ(binding.value().cues[1].cue, "late");
}

TEST_CASE("stylize.action.attack_vfx concurrent executions use isolated clocks") {
    FakeCameraSink sink;
    eve::cap::addListener<eve::action::IActionCameraCueSink>(&sink);
    auto* module = StylizeAction::create();
    REQUIRE(module != nullptr);
    REQUIRE(module->registerRecipeJson(kMinimalRecipe).ok());

    auto registry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());

    eve::action::ActionTimelineEvent event;
    event.kind    = eve::action::ActionTimelineEventKind::Notify;
    event.type    = id("presentation:attack-vfx");
    event.itemId  = id("item:pulse");
    event.payload = {{"recipeId", "attackvfx:pulse"}, {"lifetimeSeconds", 0.5}};

    eve::action::ActionNotifyContext a;
    a.executionId = eve::action::ActionExecutionId{10};
    a.time        = eve::Duration::fromSeconds(1.0).value();
    REQUIRE(registry.value().dispatch(event, a).ok());

    eve::action::ActionNotifyContext b;
    b.executionId = eve::action::ActionExecutionId{11};
    b.time        = eve::Duration::fromSeconds(0.0).value();
    REQUIRE(registry.value().dispatch(event, b).ok());
    REQUIRE_EQ(module->runtime().activeCount(), 2u);

    // Advancing B at t=0 must not apply A's large clock jump as dt.
    b.time = eve::Duration::fromSeconds(0.05).value();
    REQUIRE(registry.value().advanceHandlers(b).ok());
    REQUIRE_EQ(module->runtime().activeCount(), 2u);

    eve::cap::removeListener<eve::action::IActionCameraCueSink>(&sink);
}
