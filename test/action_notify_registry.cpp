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

    int                            calls = 0;
    std::string                    lastType;
    eve::action::ActionExecutionId lastExecution;
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
