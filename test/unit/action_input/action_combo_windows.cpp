#include "action/ActionBlockRuntime.h"
#include "action/ActionNotifyRegistry.h"
#include "action/input/ActionComboWindowState.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <string_view>

namespace {

eve::LogicalId id(std::string_view value) {
    auto parsed = eve::LogicalId::parse(value);
    REQUIRE(parsed.has_value());
    return *parsed;
}

eve::SubjectRef subject(std::string_view value) {
    auto parsed = eve::PersistentId::parse(value);
    REQUIRE(parsed.has_value());
    return eve::SubjectRef::fromPersistentId(*parsed);
}

eve::action::ActionActiveBlock active(const eve::action::ActionTimelineEvent& event) {
    return {event.trackId, event.itemId, event.type, eve::Duration::zero(),
            eve::Duration::fromSeconds(0.5).takeValue(), event.payload};
}

}  // namespace

TEST_CASE("actionComboWindows.matchSemanticInputAndInterruptWithoutEntity") {
    auto registry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());
    const eve::action::ActionExecutionId execution(601);
    ecs::EntityHandle sourceHandle{nullptr, typeid(void), 7, 2};
    const auto stableSubject = subject("21222324-2526-2728-a92a-2b2c2d2e2f30");
    bool resolverAlive = true;
    eve::action::input::ActionComboWindowState combos(
        [&](ecs::EntityHandle handle) -> eve::Result<eve::SubjectRef> {
            if (resolverAlive && handle.id == sourceHandle.id)
                return eve::Result<eve::SubjectRef>::success(stableSubject);
            return eve::Result<eve::SubjectRef>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "combo subject is stale", "entity"));
        });

    eve::action::ActionTimelineEvent first{
        eve::action::ActionTimelineEventKind::StateEnter, id("input-track:combo"), id("combo-window:first"),
        id("input:combo-window"), eve::Duration::zero(), {{"input", eve::Value("attack.heavy")}}};
    auto second = first;
    second.itemId = id("combo-window:second");
    eve::action::ActionNotifyContext context;
    context.executionId = execution;
    context.source = sourceHandle;
    CHECK(!registry.value().dispatch(first, context).ok());

    combos.setEnabled(true);
    eve::action::ActionBlockRuntime runtime(registry.value());
    eve::action::ActionAdvance advance;
    advance.id = execution;
    advance.phase = eve::action::ActionPhase::Active;
    advance.timelineEvents = {second, first};
    advance.activeBlocks = {active(second), active(first)};
    REQUIRE(runtime.apply(advance, context).ok());
    CHECK_EQ(combos.activeCount(), 2U);
    auto inputs = combos.availableInputs(stableSubject);
    REQUIRE_EQ(inputs.size(), 1U);
    CHECK_EQ(inputs.front(), std::string("attack.heavy"));
    auto match = combos.match(stableSubject, "attack.heavy");
    REQUIRE(match.ok());
    CHECK_EQ(match.value().executionId, execution);
    CHECK_EQ(match.value().itemId, id("combo-window:first"));
    CHECK(!combos.match(stableSubject, "attack.light").ok());

    resolverAlive = false;
    REQUIRE(runtime.interrupt(context).ok());
    CHECK_EQ(combos.activeCount(), 0U);
    CHECK(!combos.match(stableSubject, "attack.heavy").ok());
    combos.setEnabled(false);
}
