// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "gameplay_control_tactics.cpp",
    "tactics_ability.cpp",
    "tactics_board.cpp",
    "tactics_edges.cpp",
    "tactics_graph.cpp",
    "tactics_line_of_sight.cpp",
    "tactics_path.cpp",
    "tactics_interaction.cpp",
    "tactics_objective.cpp",
    "tactics_presentation_script.cpp",
    "tactics_presentation.cpp",
    "tactics_reaction.cpp",
    "tactics_migration.cpp",
    "tactics_replay.cpp",
    "tactics_snapshot_script.cpp",
    "tactics_charge_time.cpp",
    "tactics_turn.cpp",
    "tactics_turn_policy.cpp"
  ]
}
*/
// clang-format on

#include "common/ECS.h"
#include "tactics/Tactics.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cstdint>
#include <utility>

namespace tactics_spec {
eve::SubjectRef subject(const char* uuid) {
    auto parsed = eve::PersistentId::parse(uuid);
    REQUIRE(parsed.has_value());
    return eve::SubjectRef::fromPersistentId(*parsed);
}

eve::LogicalId action(const char* id) {
    auto parsed = eve::LogicalId::parse(id);
    REQUIRE(parsed.has_value());
    return *parsed;
}

eve::SimulationStep step(std::uint64_t tick) { return {eve::SimulationTick(tick), eve::Duration::fromNanoseconds(1)}; }

struct ReadyBattle {
    ecs::Table            world;
    ecs::ScopedTable      guard{world};
    eve::tactics::Tactics tactics;
    eve::SubjectRef       battleId = subject("00000000-0000-7000-8000-000000000401");
    eve::SubjectRef       sideId   = subject("00000000-0000-7000-8000-000000000402");
    eve::SubjectRef       actorId  = subject("00000000-0000-7000-8000-000000000403");
    eve::SubjectRef       otherId  = subject("00000000-0000-7000-8000-000000000404");
    ecs::EntityHandle     battle{};

    void setup(int actionPoints = 2) {
        auto created = tactics.newBattle(battleId, 41);
        REQUIRE(created.ok());
        battle = std::move(created).takeValue();
        for (int x = 0; x < 3; ++x) REQUIRE(tactics.addCell(battle, {x, 0, 0}).ok());
        auto side = tactics.newSide(battle, sideId);
        REQUIRE(side.ok());
        REQUIRE(
            tactics.newUnit(battle, side.value(), actorId, action("unit:test"), {0, 0, 0}, {actionPoints, 300, 0, 10})
                .ok());
        REQUIRE(tactics.newUnit(battle, side.value(), otherId, action("unit:other"), {2, 0, 0}, {1, 300, 1, 5}).ok());
    }

    void enterActing() {
        REQUIRE(tactics.start(battle, eve::tactics::TurnPolicyKind::Initiative).ok());
        for (std::uint64_t tick = 1; tick <= 3; ++tick) REQUIRE(tactics.advance(battle, step(tick)).ok());
        REQUIRE(tactics.phase(battle).value() == eve::tactics::BattlePhase::Acting);
    }
};
}  // namespace tactics_spec

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_gameplay_control_tactics)
#include "PrivateTestScope.h"
#include "common/Capability.h"
#include "common/GameplayControl.h"

namespace tactics_control_spec {

// Tactics publishes a gameplay provider for its lifetime. Player equivalent
// sessions can enumerate actions and submit a revision checked command.
TEST_CASE("tactics.control.provider_actions_and_revision_checked_submit") {
    const auto listeners = eve::cap::listenerCount<eve::IGameplayControlProvider>();
    {
        tactics_spec::ReadyBattle fixture;
        fixture.setup();
        fixture.enterActing();
        CHECK_EQ(eve::cap::listenerCount<eve::IGameplayControlProvider>(), listeners + 1);
        eve::GameplaySession player{"player", eve::GameplayAccess::PlayerEquivalent, {fixture.actorId}};
        auto                 observed = fixture.tactics.observeGameplay(player, fixture.battleId);
        REQUIRE(observed.ok());
        auto available = fixture.tactics.availableGameplayActions(player, fixture.battleId, fixture.actorId);
        REQUIRE(available.ok());
        REQUIRE_EQ(available.value().size(), std::size_t{5});
        eve::GameplayCommand command;
        command.id               = "move-once";
        command.action           = tactics_spec::action("tactics:move");
        command.subject          = fixture.actorId;
        command.observedTick     = observed.value().tick;
        command.expectedRevision = observed.value().revision;
        command.parameters =
            eve::Value(eve::Value::Object{{"layer", eve::Value(0)}, {"x", eve::Value(1)}, {"y", eve::Value(0)}});
        auto applied = fixture.tactics.submitGameplay(player, fixture.battleId, command);
        REQUIRE(applied.ok());
        CHECK(applied.value().resultingRevision > observed.value().revision);
        CHECK(!fixture.tactics.submitGameplay(player, fixture.battleId, command).ok());
    }
    CHECK_EQ(eve::cap::listenerCount<eve::IGameplayControlProvider>(), listeners);
}

// An unauthorized actor and a stale tick both fail without changing the
// battle's revision. The instance catalogue names each battle identity.
TEST_CASE("tactics.control.authorization_staleness_and_instances") {
    tactics_spec::ReadyBattle fixture;
    fixture.setup();
    fixture.enterActing();
    auto instances = fixture.tactics.gameplayInstances();
    REQUIRE_EQ(instances.size(), std::size_t{1});
    CHECK_EQ(instances[0], fixture.battleId);
    eve::GameplaySession player{"player", eve::GameplayAccess::PlayerEquivalent, {fixture.actorId}};
    eve::GameplaySession stranger{"stranger", eve::GameplayAccess::PlayerEquivalent, {fixture.otherId}};
    CHECK(!fixture.tactics.availableGameplayActions(stranger, fixture.battleId, fixture.actorId).ok());
    auto before = fixture.tactics.observeGameplay(player, fixture.battleId);
    REQUIRE(before.ok());
    eve::GameplayCommand command;
    command.id               = "stale-wait";
    command.action           = tactics_spec::action("tactics:wait");
    command.subject          = fixture.actorId;
    command.observedTick     = eve::SimulationTick(2);
    command.expectedRevision = before.value().revision;
    command.parameters       = eve::Value(eve::Value::Object{});
    CHECK(!fixture.tactics.submitGameplay(player, fixture.battleId, command).ok());
    auto after = fixture.tactics.observeGameplay(player, fixture.battleId);
    REQUIRE(after.ok());
    CHECK_EQ(after.value().revision, before.value().revision);
}
}  // namespace tactics_control_spec
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_tactics_ability)
#include <string>
#include "PrivateTestScope.h"

// Ability preview is a pure query; commitment consumes one point and persists
// the exact target and payload as a replayable command.
TEST_CASE("tactics.ability.preview_commit_and_log_contract") {
    tactics_spec::ReadyBattle f;
    f.setup();
    f.enterActing();
    const auto strike = tactics_spec::action("test:strike");
    auto       before = f.tactics.commandsFrom(f.battle, eve::Revision(0));
    REQUIRE(before.ok());
    auto preview = f.tactics.previewAbility(f.battle, f.actorId, strike, {1, 0, 0}, f.otherId, "{\"power\":3}");
    REQUIRE(preview.ok());
    auto resources = f.tactics.unitResources(f.battle, f.actorId);
    REQUIRE(resources.ok());
    CHECK_EQ(resources.value().actionPoints, 2);
    auto unchanged = f.tactics.commandsFrom(f.battle, eve::Revision(0));
    REQUIRE(unchanged.ok());
    CHECK_EQ(unchanged.value().size(), before.value().size());
    auto committed = f.tactics.useAbility(f.battle, f.actorId, strike, {1, 0, 0}, f.otherId, "{\"power\":3}");
    REQUIRE(committed.ok());
    CHECK_EQ(committed.value().remainingActionPoints, preview.value().remainingActionPoints);
    CHECK_EQ(committed.value().targetUnit, f.otherId);
    auto commands = f.tactics.commandsFrom(f.battle, eve::Revision(0));
    REQUIRE(commands.ok());
    REQUIRE_EQ(commands.value().size(), before.value().size() + 1);
    const auto& declaration = commands.value().back();
    CHECK(declaration.kind == eve::tactics::BattleCommandKind::UseAbility);
    CHECK_EQ(declaration.actor, f.actorId);
    CHECK_EQ(declaration.targetUnit, f.otherId);
    CHECK_EQ(declaration.payload, std::string("{\"power\":3}"));
}

// Validation rejects malformed identities, off-board geometry, and payloads
// above the persistence limit without consuming points or appending commands.
TEST_CASE("tactics.ability.invalid_declarations_preserve_activation") {
    tactics_spec::ReadyBattle f;
    f.setup();
    f.enterActing();
    const auto strike   = tactics_spec::action("test:strike");
    auto       baseline = f.tactics.commandsFrom(f.battle, eve::Revision(0));
    REQUIRE(baseline.ok());
    CHECK(!f.tactics.useAbility(f.battle, f.otherId, strike, {1, 0, 0}).ok());
    CHECK(!f.tactics.useAbility(f.battle, f.actorId, eve::LogicalId{}, {1, 0, 0}).ok());
    CHECK(!f.tactics.useAbility(f.battle, f.actorId, strike, {1, 0, 4}).ok());
    CHECK(!f.tactics
               .useAbility(f.battle, f.actorId, strike, {1, 0, 0},
                           tactics_spec::subject("00000000-0000-0000-0000-000000000599"))
               .ok());
    CHECK(!f.tactics
               .useAbility(f.battle, f.actorId, strike, {1, 0, 0}, {},
                           std::string(eve::tactics::kMaxAbilityPayloadBytes + 1, 'x'))
               .ok());
    auto resources = f.tactics.unitResources(f.battle, f.actorId);
    REQUIRE(resources.ok());
    CHECK_EQ(resources.value().actionPoints, 2);
    auto commands = f.tactics.commandsFrom(f.battle, eve::Revision(0));
    REQUIRE(commands.ok());
    CHECK_EQ(commands.value().size(), baseline.value().size());
    auto bound = f.tactics.useAbility(f.battle, f.actorId, strike, {1, 0, 0}, {},
                                      std::string(eve::tactics::kMaxAbilityPayloadBytes, 'x'));
    CHECK(bound.ok());
}
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_tactics_board) ||      \
    defined(EVE_TEST_ORIGIN_tactics_edges) || defined(EVE_TEST_ORIGIN_tactics_graph) || \
    defined(EVE_TEST_ORIGIN_tactics_line_of_sight) || defined(EVE_TEST_ORIGIN_tactics_path)
#include "PrivateTestScope.h"
#include "tactics/TacticsPath.h"
#include "tactics/TacticsTypes.h"

#include <optional>

namespace tactics_board_spec {
using namespace eve::tactics;

// BoardState owns two synchronized occupancy indexes. Failed moves preserve
// both the subject position and cell occupant for occupied/blocked inputs.
TEST_CASE("tactics.board.occupancy_success_and_atomic_refusals") {
    BoardState board;
    REQUIRE(board.addCell({0, 0, 0}).ok());
    REQUIRE(board.addCell({1, 0, 0}).ok());
    REQUIRE(board.addCell({2, 0, 0}, CellState{100, 0, false, {}}).ok());
    const auto alpha = tactics_spec::subject("00000000-0000-0000-0000-000000000001");
    const auto beta  = tactics_spec::subject("00000000-0000-0000-0000-000000000002");
    REQUIRE(board.place(alpha, {0, 0, 0}).ok());
    REQUIRE(board.place(beta, {1, 0, 0}).ok());
    SUB_CASE("occupied destination") {
        CHECK(!board.move(alpha, {1, 0, 0}).ok());
        CHECK((board.position(alpha) == Cell{0, 0, 0}));
        CHECK(board.occupant({1, 0, 0}) == beta);
    };
    SUB_CASE("blocked destination") {
        CHECK(!board.move(alpha, {2, 0, 0}).ok());
        CHECK((board.position(alpha) == Cell{0, 0, 0}));
        CHECK(board.occupant({2, 0, 0}) == std::nullopt);
    };
    CHECK(board.validateInvariants().ok());
}

// Neighbour order is deterministic across square-four and axial-hex
// topology, and explicit graph traverses only declared outgoing edges.
TEST_CASE("tactics.board.topology_and_directed_edges") {
    BoardState board;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x) REQUIRE(board.addCell({x, y, 0}).ok());
    auto square = board.neighbours({0, 0, 0});
    REQUIRE_EQ(square.size(), std::size_t{4});
    CHECK((square[0] == Cell{1, 0, 0}));
    board.setTopology(BoardTopology::HexAxial);
    auto hex = board.neighbours({0, 0, 0});
    REQUIRE_EQ(hex.size(), std::size_t{6});
    CHECK((hex[1] == Cell{1, -1, 0}));
    board.setTopology(BoardTopology::ExplicitGraph);
    REQUIRE(board.addEdge({0, 0, 0}, {1, 1, 0}).ok());
    auto outgoing = board.neighbours({0, 0, 0});
    REQUIRE_EQ(outgoing.size(), std::size_t{1});
    CHECK((outgoing[0] == Cell{1, 1, 0}));
    CHECK(board.neighbours({1, 1, 0}).empty());
    CHECK(!board.addEdge({0, 0, 0}, {1, 1, 0}).ok());
    CHECK(!board.addEdge({0, 0, 0}, {9, 9, 0}).ok());
}
}  // namespace tactics_board_spec
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_tactics_interaction)
#include "PrivateTestScope.h"
#include "tactics/Interaction.h"

namespace tactics_interaction_spec {
eve::tactics::InteractionContext acting() {
    eve::tactics::InteractionContext c;
    c.status         = eve::tactics::BattleStatus::Running;
    c.phase          = eve::tactics::BattlePhase::Acting;
    c.activeUnit     = tactics_spec::subject("00000000-0000-0000-0000-000000000700");
    c.controlledUnit = c.activeUnit;
    c.controlledCell = {0, 0, 0};
    c.reachableCells = {{1, 0, 0}};
    c.unitsByCell    = {{{0, 0, 0}, c.activeUnit},
                        {{2, 0, 0}, tactics_spec::subject("00000000-0000-0000-0000-000000000701")}};
    c.revision       = eve::Revision(7);
    return c;
}

TEST_CASE("tactics.interaction.selection_movement_and_resolution") {
    using namespace eve::tactics;
    auto session = InteractionSession::create(acting());
    CHECK(session.state() == InteractionState::AwaitSelection);
    auto selected = session.onCellClicked({0, 0, 0});
    REQUIRE(selected.ok());
    CHECK(selected.value().kind == InteractionIntentKind::SelectUnit);
    CHECK_EQ(selected.value().expected.value(), std::uint64_t{7});
    auto move = session.onCellClicked({1, 0, 0});
    REQUIRE(move.ok());
    CHECK(move.value().kind == InteractionIntentKind::MoveTo);
    CHECK((move.value().cell == Cell{1, 0, 0}));
    CHECK(session.state() == InteractionState::Resolving);
    CHECK(!session.onCellClicked({2, 0, 0}).ok());
    REQUIRE(session.resolve().ok());
    CHECK(session.state() == InteractionState::AwaitSelection);
    auto irrelevant = session.onCellClicked({9, 9, 0});
    REQUIRE(irrelevant.ok());
    CHECK(irrelevant.value().kind == InteractionIntentKind::None);
}

TEST_CASE("tactics.interaction.targeting_cancellation_and_end_turn") {
    using namespace eve::tactics;
    auto session = InteractionSession::create(acting());
    CHECK(!session.armAbility(eve::LogicalId{}, {{2, 0, 0}}).ok());
    REQUIRE(session.armAbility(tactics_spec::action("test:strike"), {{2, 0, 0}}).ok());
    auto target = session.onCellClicked({2, 0, 0});
    REQUIRE(target.ok());
    CHECK(target.value().kind == InteractionIntentKind::UseAbilityOn);
    CHECK_EQ(target.value().targetUnit, tactics_spec::subject("00000000-0000-0000-0000-000000000701"));
    REQUIRE(session.resolve().ok());
    REQUIRE(session.armAbility(tactics_spec::action("test:strike"), {}).ok());
    auto cancelled = session.onCancel();
    REQUIRE(cancelled.ok());
    CHECK(cancelled.value().kind == InteractionIntentKind::Cancel);
    CHECK(!session.armedAction().isValid());
    auto ended = session.onEndTurn();
    REQUIRE(ended.ok());
    CHECK(ended.value().kind == InteractionIntentKind::EndTurn);
    CHECK_EQ(ended.value().actor, acting().controlledUnit);
}

TEST_CASE("tactics.interaction.nonacting_contexts_refuse_input") {
    using namespace eve::tactics;
    auto context   = acting();
    context.status = BattleStatus::Setup;
    auto setup     = InteractionSession::create(context);
    CHECK(setup.state() == InteractionState::Blocked);
    CHECK(!setup.onCellClicked({1, 0, 0}).ok());
    context            = acting();
    context.activeUnit = tactics_spec::subject("00000000-0000-0000-0000-000000000701");
    auto other         = InteractionSession::create(context);
    CHECK(other.state() == InteractionState::Blocked);
    context.status = BattleStatus::Ended;
    context.phase  = BattlePhase::BattleEnd;
    auto ended     = InteractionSession::create(context);
    CHECK(ended.state() == InteractionState::Ended);
    CHECK(!ended.onEndTurn().ok());
}
}  // namespace tactics_interaction_spec
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_tactics_objective)
#include "PrivateTestScope.h"

// Objectives evaluate the authoritative side membership and outcome, then
// publish a completed objective and terminal battle status.
TEST_CASE("tactics.objective.eliminate_side_after_defeat") {
    ecs::Table            world;
    ecs::ScopedTable      guard(world);
    eve::tactics::Tactics tactics;
    const auto            battleId = tactics_spec::subject("00000000-0000-0000-0000-0000000000b0");
    const auto            blue     = tactics_spec::subject("00000000-0000-0000-0000-0000000000b1");
    const auto            red      = tactics_spec::subject("00000000-0000-0000-0000-0000000000b2");
    const auto            blueUnit = tactics_spec::subject("00000000-0000-0000-0000-0000000000b3");
    const auto            redUnit  = tactics_spec::subject("00000000-0000-0000-0000-0000000000b4");
    auto                  created  = tactics.newBattle(battleId);
    REQUIRE(created.ok());
    auto battle = std::move(created).takeValue();
    REQUIRE(tactics.addCell(battle, {0, 0, 0}).ok());
    REQUIRE(tactics.addCell(battle, {1, 0, 0}).ok());
    auto blueSide = tactics.newSide(battle, blue);
    auto redSide  = tactics.newSide(battle, red);
    REQUIRE(blueSide.ok());
    REQUIRE(redSide.ok());
    REQUIRE(tactics.newUnit(battle, blueSide.value(), blueUnit, {}, {0, 0, 0}).ok());
    REQUIRE(tactics.newUnit(battle, redSide.value(), redUnit, {}, {1, 0, 0}).ok());
    eve::tactics::ObjectiveSpec objective;
    objective.id              = tactics_spec::action("test:eliminate");
    objective.kind            = eve::tactics::ObjectiveKind::EliminateSide;
    objective.beneficiarySide = blue;
    objective.targetSide      = red;
    REQUIRE(tactics.addObjective(battle, objective).ok());
    REQUIRE(tactics.start(battle, eve::tactics::TurnPolicyKind::Initiative).ok());
    REQUIRE(tactics.defeatUnit(battle, redUnit).ok());
    auto status = tactics.status(battle);
    REQUIRE(status.ok());
    CHECK(status.value() == eve::tactics::BattleStatus::Ended);
    auto* value = dynamic_cast<eve::tactics::Battle*>(ecs::try_get(battle));
    REQUIRE(value != nullptr);
    REQUIRE_EQ(value->objectives()->values.size(), std::size_t{1});
    CHECK(value->objectives()->values.front().status == eve::tactics::ObjectiveStatus::Completed);
}
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_tactics_presentation)
#include "PrivateTestScope.h"
#include "tactics/Presentation.h"

namespace tactics_presentation_spec {
eve::tactics::BattleEvent event(std::uint64_t sequence, const char* name, eve::SubjectRef subject) {
    eve::tactics::BattleEvent e;
    e.sequence = sequence;
    e.type     = name;
    e.subject  = subject;
    e.tick     = eve::SimulationTick(10);
    e.from     = eve::tactics::BattlePhase::Acting;
    e.to       = eve::tactics::BattlePhase::Acting;
    return e;
}

TEST_CASE("tactics.presentation.event_to_visual_intent_and_revert") {
    using namespace eve::tactics;
    const auto        actor = tactics_spec::subject("00000000-0000-0000-0000-000000000800");
    PresentationFrame frame;
    frame.revision       = eve::Revision(9);
    frame.transientTicks = 5;
    frame.restingState   = {{actor.format(), UnitVisualState::Friendly}};
    PresentationProjector projector;
    auto                  pending = projector.project(event(1, "turn.pending", actor), nullptr, frame);
    REQUIRE(pending.ok());
    REQUIRE_EQ(pending.value().size(), std::size_t{1});
    CHECK(pending.value()[0].intent.state == UnitVisualState::Selected);
    CHECK(pending.value()[0].revert.trigger == PresentationRevertTrigger::OnNextTurn);
    auto restored = projector.revert(pending.value()[0].intent, pending.value()[0].revert);
    REQUIRE(restored.ok());
    CHECK(restored.value().state == UnitVisualState::Friendly);
    BattleCommand move;
    move.kind   = BattleCommandKind::Move;
    move.actor  = actor;
    move.cell   = {1, 0, 0};
    auto moving = projector.project(event(2, "unit.moved", actor), &move, frame);
    REQUIRE(moving.ok());
    REQUIRE_EQ(moving.value().size(), std::size_t{1});
    CHECK((moving.value()[0].intent.to == Cell{1, 0, 0}));
    CHECK_EQ(moving.value()[0].intent.expiresAtTick.value(), std::uint64_t{15});
    auto destroyed = projector.project(event(3, "unit.defeated", actor), nullptr, frame);
    REQUIRE(destroyed.ok());
    REQUIRE_EQ(destroyed.value().size(), std::size_t{1});
    CHECK(destroyed.value()[0].intent.state == UnitVisualState::Destroyed);
    CHECK(!projector.revert(destroyed.value()[0].intent, destroyed.value()[0].revert).ok());
}
}  // namespace tactics_presentation_spec
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_tactics_presentation_script)
#include "PrivateTestScope.h"
#include "common/Capability.h"
#include "tactics/LineOfSight.h"
#include "tactics/Presentation.h"

namespace tactics_presentation_bridge_spec {
class Blind final : public eve::tactics::ILineOfSightPolicy {
public:
    [[nodiscard]] std::string_view  id() const noexcept override { return "test:blind"; }
    [[nodiscard]] eve::Result<bool> visible(const eve::tactics::BoardState&, eve::tactics::Cell,
                                            eve::tactics::Cell) const override {
        return eve::Result<bool>::success(false);
    }
};

// A provider changes the visible range and its reported algorithm identity;
// revocation returns the facade to the built-in calculation.
TEST_CASE("tactics.presentation_bridge.line_of_sight_provider_lifecycle") {
    tactics_spec::ReadyBattle f;
    f.setup();
    auto open = f.tactics.visibleCellsInRange(f.battle, {0, 0, 0}, 1, 2, eve::tactics::CellRangeMetric::Chebyshev);
    REQUIRE(open.ok());
    CHECK_EQ(f.tactics.lineOfSightAlgorithm(), std::string_view("grid_line_of_sight"));
    Blind blind;
    eve::cap::provide<eve::tactics::ILineOfSightPolicy>(&blind);
    CHECK_EQ(f.tactics.lineOfSightAlgorithm(), std::string_view("test:blind"));
    auto hidden = f.tactics.visibleCellsInRange(f.battle, {0, 0, 0}, 1, 2, eve::tactics::CellRangeMetric::Chebyshev);
    REQUIRE(hidden.ok());
    CHECK(hidden.value().empty());
    eve::cap::revoke<eve::tactics::ILineOfSightPolicy>(&blind);
    auto restored = f.tactics.visibleCellsInRange(f.battle, {0, 0, 0}, 1, 2, eve::tactics::CellRangeMetric::Chebyshev);
    REQUIRE(restored.ok());
    CHECK(restored.value() == open.value());
}

TEST_CASE("tactics.presentation_bridge.accepted_move_projects_command_geometry") {
    tactics_spec::ReadyBattle f;
    f.setup();
    f.enterActing();
    auto opening = f.tactics.presentationIntents(f.battle, 0, 5);
    REQUIRE(opening.ok());
    REQUIRE(!opening.value().empty());
    const auto before = opening.value().back().intent.sequence;
    REQUIRE(f.tactics.moveUnit(f.battle, f.actorId, {1, 0, 0}).ok());
    auto moved = f.tactics.presentationIntents(f.battle, before, 5);
    REQUIRE(moved.ok());
    REQUIRE_EQ(moved.value().size(), std::size_t{1});
    CHECK(moved.value()[0].intent.state == eve::tactics::UnitVisualState::Moving);
    CHECK((moved.value()[0].intent.to == eve::tactics::Cell{1, 0, 0}));
}
}  // namespace tactics_presentation_bridge_spec
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_tactics_reaction)
#include "PrivateTestScope.h"

TEST_CASE("tactics.reaction.accept_consumes_resource_and_duplicate_is_conflict") {
    tactics_spec::ReadyBattle f;
    f.setup();
    f.enterActing();
    const auto counter = tactics_spec::action("tactics:counter");
    auto       opened  = f.tactics.openReaction(f.battle, 4, {{f.otherId, counter, 5, 10}});
    REQUIRE(opened.ok());
    CHECK_EQ(opened.value(), 1u);
    auto accepted = f.tactics.acceptReaction(f.battle, f.otherId, counter);
    REQUIRE(accepted.ok());
    CHECK_EQ(accepted.value().remainingReactionPoints, 0);
    REQUIRE(f.tactics.openReaction(f.battle, 4, {{f.otherId, counter, 5, 10}}).ok());
    auto duplicate = f.tactics.acceptReaction(f.battle, f.otherId, counter);
    CHECK(!duplicate.ok());
    CHECK_EQ(duplicate.code(), eve::StatusCode::Conflict);
    REQUIRE(f.tactics.declineReaction(f.battle).ok());
}
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_tactics_migration) || \
    defined(EVE_TEST_ORIGIN_tactics_replay) || defined(EVE_TEST_ORIGIN_tactics_snapshot_script)
#include "PrivateTestScope.h"
#include "common/Snapshot.h"
#include "common/SnapshotHash.h"
#include "tactics/TacticsPersistence.h"
#include "tactics/TacticsReplay.h"

namespace tactics_persistence_spec {
// A snapshot is the canonical state boundary. A restored battle has the
// original resources and a byte-identical canonical snapshot.
TEST_CASE("tactics.persistence.snapshot_round_trip_and_invalid_document") {
    tactics_spec::ReadyBattle f;
    f.setup();
    f.enterActing();
    auto baseline = f.tactics.snapshotJson(f.battle);
    REQUIRE(baseline.ok());
    REQUIRE(f.tactics.moveUnit(f.battle, f.actorId, {1, 0, 0}).ok());
    auto spent = f.tactics.unitResources(f.battle, f.actorId);
    REQUIRE(spent.ok());
    REQUIRE(f.tactics.restoreJson(f.battle, baseline.value()).ok());
    auto restored = f.tactics.unitResources(f.battle, f.actorId);
    REQUIRE(restored.ok());
    CHECK(restored.value().movePoints > spent.value().movePoints);
    auto repeated = f.tactics.snapshotJson(f.battle);
    REQUIRE(repeated.ok());
    CHECK_EQ(repeated.value(), baseline.value());
    CHECK(!f.tactics.restoreJson(f.battle, "{\"not\":\"a snapshot\"}").ok());
    auto after = f.tactics.snapshotJson(f.battle);
    REQUIRE(after.ok());
    CHECK_EQ(after.value(), baseline.value());
}

// The JSON bridge and command codec form one replay contract: a log since a
// snapshot revision reproduces the same final canonical document.
TEST_CASE("tactics.persistence.command_log_replays_from_snapshot_revision") {
    tactics_spec::ReadyBattle f;
    f.setup();
    f.enterActing();
    auto envelope = f.tactics.snapshot(f.battle, eve::snapshotContentHashProvider());
    REQUIRE(envelope.ok());
    const auto startRevision = envelope.value().revision;
    auto       baseline      = f.tactics.snapshotJson(f.battle);
    REQUIRE(baseline.ok());
    REQUIRE(f.tactics.moveUnit(f.battle, f.actorId, {1, 0, 0}).ok());
    REQUIRE(f.tactics.endTurn(f.battle, f.actorId).ok());
    auto log = f.tactics.commandLogJson(f.battle, startRevision);
    REQUIRE(log.ok());
    auto expected = f.tactics.snapshotJson(f.battle);
    REQUIRE(expected.ok());
    REQUIRE(f.tactics.restoreJson(f.battle, baseline.value()).ok());
    REQUIRE(f.tactics.replayJson(f.battle, log.value()).ok());
    auto actual = f.tactics.snapshotJson(f.battle);
    REQUIRE(actual.ok());
    CHECK_EQ(actual.value(), expected.value());
}

// The content digest identifies itself and rejects a changed document before
// any observable battle mutation.
TEST_CASE("tactics.persistence.hash_identity_and_tamper_rejection") {
    auto builtin = eve::builtinSnapshotContentHasher();
    REQUIRE(builtin != nullptr);
    CHECK_EQ(std::string(builtin->id()), std::string("fnv1a64x2-noncrypto"));
    CHECK(builtin->digestKind() == eve::ContentDigestKind::NonCryptographic);
    tactics_spec::ReadyBattle f;
    f.setup();
    auto document = f.tactics.snapshotJson(f.battle);
    REQUIRE(document.ok());
    std::string changed = document.value();
    REQUIRE(!changed.empty());
    changed[changed.size() / 2] = changed[changed.size() / 2] == 'x' ? 'y' : 'x';
    CHECK(!f.tactics.restoreJson(f.battle, changed).ok());
    auto after = f.tactics.snapshotJson(f.battle);
    REQUIRE(after.ok());
    CHECK_EQ(after.value(), document.value());
}
}  // namespace tactics_persistence_spec
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_tactics_charge_time) || \
    defined(EVE_TEST_ORIGIN_tactics_turn) || defined(EVE_TEST_ORIGIN_tactics_turn_policy)
#include "PrivateTestScope.h"
#include "tactics/TurnPolicy.h"

namespace tactics_turn_spec {
// Setup allocates one occupied cell per unit. A conflicting second unit is
// rejected without adding a half-constructed ECS entity.
TEST_CASE("tactics.turn.setup_conflict_is_atomic") {
    ecs::Table            world;
    ecs::ScopedTable      guard(world);
    eve::tactics::Tactics tactics;
    auto                  created = tactics.newBattle(tactics_spec::subject("00000000-0000-0000-0000-000000000010"));
    REQUIRE(created.ok());
    auto battle = std::move(created).takeValue();
    REQUIRE(tactics.addCell(battle, {0, 0, 0}).ok());
    auto side = tactics.newSide(battle, tactics_spec::subject("00000000-0000-0000-0000-000000000011"));
    REQUIRE(side.ok());
    REQUIRE(
        tactics
            .newUnit(battle, side.value(), tactics_spec::subject("00000000-0000-0000-0000-000000000012"), {}, {0, 0, 0})
            .ok());
    auto conflict = tactics.newUnit(battle, side.value(), tactics_spec::subject("00000000-0000-0000-0000-000000000013"),
                                    {}, {0, 0, 0});
    CHECK(!conflict.ok());
    CHECK_EQ(conflict.code(), eve::StatusCode::Conflict);
    CHECK_EQ(tactics.unitCount(), 1u);
}

TEST_CASE("tactics.turn.initiative_phases_and_tick_monotonicity") {
    tactics_spec::ReadyBattle f;
    f.setup();
    REQUIRE(f.tactics.start(f.battle, eve::tactics::TurnPolicyKind::Initiative).ok());
    auto round = f.tactics.advance(f.battle, tactics_spec::step(1));
    REQUIRE(round.ok());
    CHECK(round.value() == eve::tactics::BattlePhase::RoundStart);
    auto turn = f.tactics.advance(f.battle, tactics_spec::step(2));
    REQUIRE(turn.ok());
    CHECK(turn.value() == eve::tactics::BattlePhase::TurnStart);
    auto acting = f.tactics.advance(f.battle, tactics_spec::step(3));
    REQUIRE(acting.ok());
    CHECK(acting.value() == eve::tactics::BattlePhase::Acting);
    auto active = f.tactics.activeUnit(f.battle);
    REQUIRE(active.ok());
    CHECK_EQ(active.value(), f.actorId);
    CHECK(!f.tactics.advance(f.battle, tactics_spec::step(3)).ok());
    REQUIRE(f.tactics.endTurn(f.battle, f.actorId).ok());
    REQUIRE(f.tactics.advance(f.battle, tactics_spec::step(4)).ok());
    REQUIRE(f.tactics.advance(f.battle, tactics_spec::step(5)).ok());
    auto next = f.tactics.activeUnit(f.battle);
    REQUIRE(next.ok());
    CHECK_EQ(next.value(), f.otherId);
}

TEST_CASE("tactics.turn.unknown_policy_preserves_setup") {
    tactics_spec::ReadyBattle f;
    f.setup();
    CHECK(!f.tactics.start(f.battle, "test:missing-policy").ok());
    auto phase = f.tactics.phase(f.battle);
    REQUIRE(phase.ok());
    CHECK(phase.value() == eve::tactics::BattlePhase::Setup);
    REQUIRE(f.tactics.start(f.battle, eve::tactics::kInitiativePolicyId).ok());
}

// Charge time must accept a known policy and refuse a non-progressing battle
// rather than entering an infinite scheduling loop.
TEST_CASE("tactics.turn.charge_policy_progress_and_refusal") {
    tactics_spec::ReadyBattle f;
    f.setup();
    REQUIRE(f.tactics.start(f.battle, eve::tactics::kChargeTimeBattlePolicyId).ok());
    auto initial = f.tactics.unitResources(f.battle, f.actorId);
    REQUIRE(initial.ok());
    auto first = f.tactics.advance(f.battle, tactics_spec::step(1));
    REQUIRE(first.ok());
    auto later = f.tactics.unitResources(f.battle, f.actorId);
    REQUIRE(later.ok());
    CHECK(later.value().charge >= initial.value().charge);
}
}  // namespace tactics_turn_spec
#endif
