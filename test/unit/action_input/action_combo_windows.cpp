// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "action_combo_windows.cpp"
  ]
}
*/
// clang-format on

#include "action/input/ActionComboWindowState.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <string_view>
#include <utility>

namespace {
eve::LogicalId id(std::string_view text) {
    auto parsed = eve::LogicalId::parse(text);
    REQUIRE(parsed.has_value());
    return *parsed;
}

eve::SubjectRef subject(std::string_view text) {
    auto parsed = eve::PersistentId::parse(text);
    REQUIRE(parsed.has_value());
    return eve::SubjectRef::fromPersistentId(*parsed);
}

eve::action::ActionTimelineEvent event(std::string_view item) {
    eve::action::ActionTimelineEvent result;
    result.itemId = id(item);
    return result;
}

eve::action::ActionStateWindowBinding combo(std::string input) {
    eve::action::ActionStateWindowBinding binding;
    binding.kind     = eve::action::ActionStateWindowKind::Combo;
    binding.resource = std::move(input);
    return binding;
}
}  // namespace

TEST_CASE("action.combo.enter_exposes_sorted_unique_inputs_and_first_matching_window") {
    const auto                                 owner = subject("21222324-2526-2728-a92a-2b2c2d2e2f30");
    ecs::EntityHandle                          handle{nullptr, typeid(void), 7, 2};
    eve::action::input::ActionComboWindowState state([&](ecs::EntityHandle source) -> eve::Result<eve::SubjectRef> {
        CHECK_EQ(source.id, handle.id);
        return eve::Result<eve::SubjectRef>::success(owner);
    });
    eve::action::ActionNotifyContext context;
    context.executionId = eve::action::ActionExecutionId(601);
    context.source      = handle;
    const auto first    = event("combo-window:first");
    const auto second   = event("combo-window:second");
    const auto heavy    = combo("attack.heavy");
    const auto light    = combo("attack.light");

    REQUIRE(state.enter(heavy, second, context).ok());
    REQUIRE(state.enter(light, first, context).ok());
    CHECK_EQ(state.activeCount(), 2U);
    const auto inputs = state.availableInputs(owner);
    REQUIRE_EQ(inputs.size(), 2U);
    CHECK_EQ(inputs[0], std::string("attack.heavy"));
    CHECK_EQ(inputs[1], std::string("attack.light"));

    const auto match = state.match(owner, "attack.heavy");
    REQUIRE(match.ok());
    CHECK_EQ(match.value().executionId, context.executionId);
    CHECK_EQ(match.value().itemId, second.itemId);
    CHECK(!state.match(owner, "attack.unknown").ok());

    CHECK(!state.enter(heavy, first, context).ok());  // active key has different input
    CHECK_EQ(state.activeCount(), 2U);
    REQUIRE(state.exit(light, first, context).ok());
    REQUIRE(state.enter(heavy, first, context).ok());
    CHECK_EQ(state.availableInputs(owner).size(), 1U);
    CHECK_EQ(state.match(owner, "attack.heavy").value().itemId, first.itemId);
}

TEST_CASE("action.combo.enter_is_idempotent_and_exit_is_keyed_by_execution_and_item") {
    const auto                                 owner = subject("21222324-2526-2728-a92a-2b2c2d2e2f30");
    ecs::EntityHandle                          handle{nullptr, typeid(void), 7, 2};
    eve::action::input::ActionComboWindowState state(
        [&](ecs::EntityHandle) { return eve::Result<eve::SubjectRef>::success(owner); });
    eve::action::ActionNotifyContext context;
    context.executionId = eve::action::ActionExecutionId(601);
    context.source      = handle;
    const auto window   = event("combo-window:first");
    const auto binding  = combo("attack.heavy");

    REQUIRE(state.enter(binding, window, context).ok());
    REQUIRE(state.enter(binding, window, context).ok());
    CHECK_EQ(state.activeCount(), 1U);
    auto otherExecution        = context;
    otherExecution.executionId = eve::action::ActionExecutionId(602);
    REQUIRE(state.exit(binding, window, otherExecution).ok());
    CHECK_EQ(state.activeCount(), 1U);
    REQUIRE(state.exit(binding, window, context).ok());
    CHECK_EQ(state.activeCount(), 0U);
    CHECK(!state.match(owner, "attack.heavy").ok());
    REQUIRE(state.exit(binding, window, context).ok());
}

TEST_CASE("action.combo.enter_rejects_missing_source_and_failed_resolution_without_state") {
    eve::action::input::ActionComboWindowState state([](ecs::EntityHandle) -> eve::Result<eve::SubjectRef> {
        return eve::Result<eve::SubjectRef>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "stale", "entity"));
    });
    eve::action::ActionNotifyContext           context;
    context.executionId = eve::action::ActionExecutionId(601);
    const auto window   = event("combo-window:first");
    const auto binding  = combo("attack.heavy");
    CHECK(!state.enter(binding, window, context).ok());
    context.source = ecs::EntityHandle{nullptr, typeid(void), 7, 2};
    CHECK(!state.enter(binding, window, context).ok());
    REQUIRE_EQ(state.activeCount(), 0U);
}
