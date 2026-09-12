#include "action/ActionBlockRuntime.h"
#include "action/ActionNotifyRegistry.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <memory>
#include <string_view>

namespace {

eve::LogicalId id(std::string_view value) {
    auto parsed = eve::LogicalId::parse(value);
    REQUIRE(parsed.has_value());
    return std::move(*parsed);
}

eve::action::ActionTimelineEvent event(eve::action::ActionTimelineEventKind kind, std::string_view type) {
    return {kind,     id("combat-track:gameplay"),        id("combat-notify:test"),
            id(type), eve::Duration::fromNanoseconds(10), {}};
}

class RecordingHandler final : public eve::action::IActionNotifyHandler {
public:
    eve::Result<void> handle(const eve::action::ActionTimelineEvent& incoming,
                             const eve::action::ActionNotifyContext& incomingContext) override {
        ++calls;
        lastType      = incoming.type.format();
        lastExecution = incomingContext.executionId;
        return eve::Result<void>::success();
    }

    eve::Result<void> update(const eve::action::ActionActiveBlock& block,
                             const eve::action::ActionNotifyContext&) override {
        ++updates;
        lastLocalTime = block.localTime;
        return eve::Result<void>::success();
    }

    eve::Result<void> sample(const eve::action::ActionActiveBlock&,
                             const eve::action::ActionNotifyContext& context) const override {
        if (!context.preview || !context.scrubbing)
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "sample context is not preview", "context"));
        ++samples;
        return eve::Result<void>::success();
    }

    int                            calls = 0;
    int                            updates = 0;
    mutable int                    samples = 0;
    std::string                    lastType;
    eve::action::ActionExecutionId lastExecution;
    eve::Duration                  lastLocalTime = eve::Duration::zero();
};

}  // namespace

TEST_CASE("actionNotifyRegistry.builtinsExposeStableEditorContracts") {
    auto registry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());
    const auto descriptors = registry.value().descriptors();
    REQUIRE_EQ(descriptors.size(), 10u);
    CHECK_EQ(descriptors.front().type, "collision:ignore-window");
    CHECK_EQ(descriptors.back().type, "presentation:vfx");

    auto damage = registry.value().descriptor("combat:damage");
    REQUIRE(damage.ok());
    CHECK(static_cast<int>(damage.value().shape) == static_cast<int>(eve::action::ActionNotifyShape::Instant));
    CHECK_EQ(damage.value().requiredPayloadFields.size(), 2u);

    auto hitbox = registry.value().descriptor("combat:hitbox-window");
    REQUIRE(hitbox.ok());
    CHECK(static_cast<int>(hitbox.value().shape) == static_cast<int>(eve::action::ActionNotifyShape::State));
}

TEST_CASE("actionBlockRuntime.routesEnterUpdateExitAndSideEffectFreeSample") {
    eve::action::ActionNotifyRegistry registry;
    REQUIRE(registry
                .registerDescriptor({"project:combat.window", "Window", "Project",
                                     eve::action::ActionNotifyShape::State, {}})
                .ok());
    auto handler = std::make_shared<RecordingHandler>();
    REQUIRE(registry.registerHandler("project:combat.window", handler).ok());
    eve::action::ActionBlockRuntime runtime(registry);

    eve::action::ActionAdvance advance;
    advance.id           = eve::action::ActionExecutionId{7};
    advance.totalElapsed = eve::Duration::fromNanoseconds(25);
    advance.timelineEvents.push_back(event(eve::action::ActionTimelineEventKind::StateEnter,
                                           "project:combat.window"));
    advance.activeBlocks.push_back({id("combat-track:gameplay"), id("combat-notify:test"),
                                    id("project:combat.window"), eve::Duration::fromNanoseconds(15),
                                    eve::Duration::fromNanoseconds(40), {}});
    eve::action::ActionNotifyContext context;
    context.executionId = advance.id;
    REQUIRE(runtime.apply(advance, context).ok());
    CHECK_EQ(handler->calls, 1);
    CHECK_EQ(handler->updates, 1);
    CHECK_EQ(handler->lastLocalTime, eve::Duration::fromNanoseconds(15));
    CHECK_EQ(runtime.executionCount(), 1u);

    REQUIRE(runtime.sample(advance.activeBlocks, context).ok());
    CHECK_EQ(handler->samples, 1);
    CHECK_EQ(runtime.executionCount(), 1u);

    context.time = eve::Duration::fromNanoseconds(30);
    REQUIRE(runtime.interrupt(context).ok());
    CHECK_EQ(handler->calls, 2);
    CHECK_EQ(runtime.executionCount(), 0u);
    auto noOp = runtime.interrupt(context);
    REQUIRE(noOp.ok());
    CHECK_EQ(noOp.status().code(), eve::StatusCode::NoOp);
}

TEST_CASE("actionNotifyRegistry.validatesShapeAndRequiredPayload") {
    auto registry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());

    auto gameplay = event(eve::action::ActionTimelineEventKind::Notify, "gameplay:event");
    CHECK(!registry.value().validate(gameplay).ok());
    gameplay.payload.emplace("tag", eve::Value("Combat.Action.Hit"));
    CHECK(registry.value().validate(gameplay).ok());

    auto hitbox = event(eve::action::ActionTimelineEventKind::StateEnter, "combat:hitbox-window");
    hitbox.payload.emplace("hitbox", eve::Value("weapon.main"));
    CHECK(registry.value().validate(hitbox).ok());
    hitbox.kind = eve::action::ActionTimelineEventKind::StateExit;
    CHECK(registry.value().validate(hitbox).ok());
    hitbox.kind = eve::action::ActionTimelineEventKind::Notify;
    CHECK(!registry.value().validate(hitbox).ok());
}

TEST_CASE("actionNotifyRegistry.rejectsInvalidAndDuplicateDescriptors") {
    eve::action::ActionNotifyRegistry   registry;
    eve::action::ActionNotifyDescriptor descriptor{
        "project:combat.custom", "Custom", "Project", eve::action::ActionNotifyShape::Instant, {"value"}};
    CHECK(registry.registerDescriptor(descriptor).ok());
    CHECK(!registry.registerDescriptor(descriptor).ok());
    descriptor.type = "project..invalid";
    CHECK(!registry.registerDescriptor(std::move(descriptor)).ok());
}

TEST_CASE("actionNotifyRegistry.handlerLifecycleIsExplicitAndObservable") {
    eve::action::ActionNotifyRegistry   registry;
    eve::action::ActionNotifyDescriptor descriptor{
        "project:combat.custom", "Custom", "Project", eve::action::ActionNotifyShape::Instant, {}};
    REQUIRE(registry.registerDescriptor(std::move(descriptor)).ok());
    auto handler = std::make_shared<RecordingHandler>();
    REQUIRE(registry.registerHandler("project:combat.custom", handler).ok());

    auto routed = event(eve::action::ActionTimelineEventKind::Notify, "project:combat.custom");
    eve::action::ActionNotifyContext context;
    context.executionId = eve::action::ActionExecutionId{42};
    REQUIRE(registry.dispatch(routed, context).ok());
    CHECK_EQ(handler->calls, 1);
    CHECK_EQ(handler->lastType, "project:combat.custom");
    CHECK_EQ(handler->lastExecution, context.executionId);

    REQUIRE(registry.unregisterHandler("project:combat.custom").ok());
    CHECK(!registry.dispatch(routed, context).ok());
    CHECK(!registry.unregisterHandler("project:combat.custom").ok());
}
