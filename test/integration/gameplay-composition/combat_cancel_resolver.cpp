#include "action/input/ActionCancelWindowState.h"
#include "action/input/ActionComboWindowState.h"
#include "action/input/ActionInputBuffer.h"
#include "combat/CombatCancelResolver.h"
#include "combat/ComboGraph.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace {

eve::SubjectRef subject(const char* value) {
    auto parsed = eve::PersistentId::parse(value);
    REQUIRE(parsed.has_value());
    return eve::SubjectRef::fromPersistentId(*parsed);
}

eve::LogicalId id(std::string_view value) {
    auto parsed = eve::LogicalId::parse(value);
    REQUIRE(parsed.has_value());
    return *parsed;
}

}  // namespace

TEST_CASE("combatCancelResolver.consumesBufferAgainstCancelAndComboGraph") {
    const auto        player = subject("11121314-1516-1718-991a-1b1c1d1e1f20");
    ecs::EntityHandle handle{nullptr, typeid(void), 1, 1};

    eve::action::input::ActionCancelWindowState cancels(
        [&](ecs::EntityHandle) { return eve::Result<eve::SubjectRef>::success(player); });
    cancels.setEnabled(true);
    eve::action::input::ActionComboWindowState combos(
        [&](ecs::EntityHandle) { return eve::Result<eve::SubjectRef>::success(player); });
    combos.setEnabled(true);

    eve::action::ActionNotifyContext context;
    context.executionId = eve::action::ActionExecutionId(3);
    context.source      = handle;
    eve::action::ActionTimelineEvent cancelEnter{
        eve::action::ActionTimelineEventKind::StateEnter,
        id("track:input"),
        id("item:cancel"),
        id("input:cancel-window"),
        eve::Duration::zero(),
        {{"allows", eve::Value("heavy-attack")}, {"priority", eve::Value(std::int64_t{5})}}};
    REQUIRE(
        cancels
            .enter({eve::action::ActionStateWindowKind::Cancel, "heavy-attack", std::nullopt, 5}, cancelEnter, context)
            .ok());
    eve::action::ActionTimelineEvent comboEnter{eve::action::ActionTimelineEventKind::StateEnter,
                                                id("track:input"),
                                                id("item:combo"),
                                                id("input:combo-window"),
                                                eve::Duration::zero(),
                                                {{"input", eve::Value("heavy-attack")}}};
    REQUIRE(
        combos.enter({eve::action::ActionStateWindowKind::Combo, "heavy-attack", std::nullopt, 0}, comboEnter, context)
            .ok());

    eve::action::input::ActionInputBuffer buffer(4, 8);
    REQUIRE(buffer.push({player, "heavy-attack", eve::SimulationTick(2), 1}).ok());

    auto light = eve::LogicalId::fromParts("combat-ability", "light-attack");
    auto heavy = eve::LogicalId::fromParts("combat-ability", "heavy-attack");
    REQUIRE(light.has_value());
    REQUIRE(heavy.has_value());
    eve::combat::ComboGraph graph;
    REQUIRE(graph.addEdge({*light, *heavy, "heavy-attack", 1, true, true}).ok());

    eve::combat::CombatCancelResolver resolver;
    resolver.setBuffer(buffer);
    resolver.setCancels(cancels);
    resolver.setCombos(&combos);
    resolver.setGraph(&graph);

    auto resolved = resolver.resolve({player, *light, eve::SimulationTick(2)});
    REQUIRE(resolved.ok());
    CHECK_EQ(resolved.value().consumed.input, std::string("heavy-attack"));
    REQUIRE(resolved.value().graph.has_value());
    CHECK_EQ(resolved.value().graph->to.format(), heavy->format());
    REQUIRE(resolved.value().comboWindow.has_value());
    CHECK_EQ(buffer.size(), 0u);
}
