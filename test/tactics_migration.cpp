#include "common/ECS.h"
#include "common/Snapshot.h"
#include "tactics/Tactics.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <utility>

namespace {

eve::SubjectRef subject(const char* text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

eve::SimulationStep step(std::uint64_t tick) {
    return {eve::SimulationTick(tick), eve::Duration::fromNanoseconds(1)};
}

eve::LogicalId action(const char* text) {
    const auto id = eve::LogicalId::parse(text);
    REQUIRE(id.has_value());
    return *id;
}

eve::SnapshotHashProvider testHash() {
    return [](std::string_view input) -> eve::Result<eve::ContentId> {
        std::uint64_t left  = 14695981039346656037ull;
        std::uint64_t right = 1099511628211ull;
        for (const unsigned char byte : input) {
            left  = (left ^ byte) * 1099511628211ull;
            right = (right ^ (static_cast<std::uint64_t>(byte) + 0x9e3779b97f4a7c15ull)) * 14029467366897019727ull;
        }
        eve::ContentId::Bytes bytes{};
        for (int index = 0; index < 8; ++index) {
            bytes[static_cast<std::size_t>(index)]     = static_cast<std::uint8_t>(left >> (56 - index * 8));
            bytes[static_cast<std::size_t>(index + 8)] = static_cast<std::uint8_t>(right >> (56 - index * 8));
        }
        return eve::Result<eve::ContentId>::success(eve::ContentId(bytes));
    };
}

/**
 * @brief Rewrite a current payload into a legacy *version* shape.
 *
 * Every field group introduced after `target` is removed, so the result is exactly what
 * that version would have written:
 *  - v1 predates the board edge set;
 *  - v1-v2 predate the stable policy id (they stored the numeric enum `1`, the frozen
 *    mapping to `initiative`);
 *  - v1-v4 predate the persisted scheduling charge and the explicit round schedule;
 *  - v1-v5 predate the ability target unit and its opaque payload on every command record.
 *
 * Deliberately defensive instead of using REQUIRE: it returns a value, and a
 * failing lookup should degrade to "unchanged payload" so the test fails on the
 * restore assertion rather than on an unrelated precondition.
 */
eve::Value legacyPayload(const eve::Value& current, eve::SchemaVersion target) {
    auto* root = current.getIf<eve::Value::Object>();
    if (root == nullptr) return current;
    eve::Value::Object payload = *root;

    if (target < eve::SchemaVersion(6)) {
        auto commandsIt = payload.find("commands");
        if (commandsIt != payload.end()) {
            if (auto* commands = commandsIt->second.getIf<eve::Value::Object>(); commands != nullptr) {
                auto valuesIt = commands->find("values");
                if (valuesIt != commands->end()) {
                    if (auto* records = valuesIt->second.getIf<eve::Value::Array>(); records != nullptr) {
                        for (auto& record : *records) {
                            if (auto* fields = record.getIf<eve::Value::Object>(); fields != nullptr) {
                                fields->erase("targetUnit");
                                fields->erase("payload");
                            }
                        }
                    }
                }
            }
        }
    }

    if (target == eve::SchemaVersion(1)) {
        auto boardIt = payload.find("board");
        if (boardIt != payload.end()) {
            if (auto* board = boardIt->second.getIf<eve::Value::Object>(); board != nullptr) board->erase("edges");
        }
    }
    if (target < eve::SchemaVersion(3)) {
        payload["policy"] = eve::Value(1);
        auto commandsIt = payload.find("commands");
        if (commandsIt != payload.end()) {
            if (auto* commands = commandsIt->second.getIf<eve::Value::Object>(); commands != nullptr) {
                auto valuesIt = commands->find("values");
                if (valuesIt != commands->end()) {
                    if (auto* records = valuesIt->second.getIf<eve::Value::Array>(); records != nullptr) {
                        for (auto& record : *records) {
                            if (auto* fields = record.getIf<eve::Value::Object>(); fields != nullptr)
                                (*fields)["policy"] = eve::Value(1);
                        }
                    }
                }
            }
        }
    }
    if (target < eve::SchemaVersion(5)) {
        payload.erase("schedule");
        auto unitsIt = payload.find("units");
        if (unitsIt != payload.end()) {
            if (auto* records = unitsIt->second.getIf<eve::Value::Array>(); records != nullptr) {
                for (auto& record : *records) {
                    if (auto* fields = record.getIf<eve::Value::Object>(); fields != nullptr)
                        fields->erase("charge");
                }
            }
        }
    }
    return eve::Value(std::move(payload));
}

/** @brief One battle with a single side and two units, ready to be snapshotted. */
struct Fixture {
    ecs::Table            world;
    ecs::ScopedTable      guard{world};
    eve::tactics::Tactics tactics;
    ecs::EntityHandle     battle{};
    // Canonical subject order puts `slow` first, so an initiative schedule (which
    // picks the higher initiative) is distinguishable from a side/tie schedule.
    eve::SubjectRef slow = subject("00000000-0000-0000-0000-000000000401");
    eve::SubjectRef fast = subject("00000000-0000-0000-0000-000000000402");

    void build() {
        auto created = tactics.newBattle(subject("00000000-0000-0000-0000-000000000400"), 31);
        REQUIRE(created.ok());
        battle = std::move(created).takeValue();
        REQUIRE(tactics.addCell(battle, {0, 0, 0}).ok());
        REQUIRE(tactics.addCell(battle, {1, 0, 0}).ok());
        auto side = tactics.newSide(battle, subject("00000000-0000-0000-0000-000000000403"));
        REQUIRE(side.ok());
        // Same side, so the side index cannot break the tie; initiative must.
        REQUIRE(tactics.newUnit(battle, side.value(), slow, {}, {0, 0, 0}, {1, 0, 0, 10}).ok());
        REQUIRE(tactics.newUnit(battle, side.value(), fast, {}, {1, 0, 0}, {1, 0, 0, 20}).ok());
    }

    /** @brief Advance to Acting and return whoever the schedule selected. */
    eve::SubjectRef firstActor() {
        REQUIRE(tactics.advance(battle, step(1)).ok());
        REQUIRE(tactics.advance(battle, step(2)).ok());
        REQUIRE(tactics.advance(battle, step(3)).ok());
        auto active = tactics.activeUnit(battle);
        REQUIRE(active.ok());
        return std::move(active).takeValue();
    }

    [[nodiscard]] int chargeOf(eve::SubjectRef unit) {
        auto resources = tactics.unitResources(battle, unit);
        REQUIRE(resources.ok());
        return resources.value().charge;
    }
};

}  // namespace

TEST_CASE("tactics.versionOneSnapshotMigratesEdgesAbsentAndPolicyId") {
    auto    hash = testHash();
    Fixture source;
    source.build();
    REQUIRE(source.tactics.start(source.battle, eve::tactics::kInitiativePolicyId).ok());

    auto captured = source.tactics.snapshot(source.battle, hash);
    REQUIRE(captured.ok());
    auto current = std::move(captured).takeValue();
    CHECK(current.schemaVersion == eve::SchemaVersion(6));

    // Version 1 predates board edges, so the legacy payload drops that field.
    auto legacy = eve::makeSnapshotEnvelope(current.type, current.schema, eve::SchemaVersion(1), current.instanceId,
                                            current.revision, current.tick,
                                            legacyPayload(current.payload, eve::SchemaVersion(1)), hash);
    REQUIRE(legacy.ok());

    Fixture target;
    target.build();
    REQUIRE(target.tactics.restore(target.battle, legacy.value(), hash).ok());

    // Assert the migrated id directly, then confirm it is the one actually used.
    auto migrated = target.tactics.policyId(target.battle);
    REQUIRE(migrated.ok());
    CHECK_EQ(migrated.value(), std::string(eve::tactics::kInitiativePolicyId));

    // The numeric 1 must have migrated to `initiative`. With one side the
    // side-alternating policy would tie and fall back to canonical order, so
    // selecting the *fast* unit is only possible if the id migrated correctly.
    CHECK_EQ(target.firstActor(), target.fast);
}

TEST_CASE("tactics.versionTwoSnapshotKeepsEdgesAndMigratesPolicyId") {
    auto    hash = testHash();
    Fixture source;
    source.build();
    REQUIRE(source.tactics.start(source.battle, eve::tactics::kInitiativePolicyId).ok());

    auto captured = source.tactics.snapshot(source.battle, hash);
    REQUIRE(captured.ok());
    auto current = std::move(captured).takeValue();

    // Version 2 already carried edges, so only the policy representation differs.
    auto legacy = eve::makeSnapshotEnvelope(current.type, current.schema, eve::SchemaVersion(2), current.instanceId,
                                            current.revision, current.tick,
                                            legacyPayload(current.payload, eve::SchemaVersion(2)), hash);
    REQUIRE(legacy.ok());

    Fixture target;
    target.build();
    REQUIRE(target.tactics.restore(target.battle, legacy.value(), hash).ok());
    auto migrated = target.tactics.policyId(target.battle);
    REQUIRE(migrated.ok());
    CHECK_EQ(migrated.value(), std::string(eve::tactics::kInitiativePolicyId));
    CHECK_EQ(target.firstActor(), target.fast);
}

TEST_CASE("tactics.futureSnapshotVersionIsRejectedWithoutMutatingTarget") {
    auto    hash = testHash();
    Fixture source;
    source.build();
    auto captured = source.tactics.snapshot(source.battle, hash);
    REQUIRE(captured.ok());
    auto current = std::move(captured).takeValue();

    auto future = eve::makeSnapshotEnvelope(current.type, current.schema, eve::SchemaVersion(99), current.instanceId,
                                            current.revision, current.tick, current.payload, hash);
    REQUIRE(future.ok());

    Fixture target;
    target.build();
    auto restored = target.tactics.restore(target.battle, future.value(), hash);
    CHECK(!restored.ok());
    // A refused version must leave the target untouched, not half-restored.
    auto status = target.tactics.status(target.battle);
    REQUIRE(status.ok());
    CHECK(status.value() == eve::tactics::BattleStatus::Setup);
}

/**
 * @brief The version-3 shape is still readable once version 5 exists.
 *
 * Versions 4 and 5 only *added* fields (a command kind, then charge and the round
 * schedule), so a version-3 payload written by that version must keep restoring. This is
 * the "does not regress" half of the gate: widening a range must not invalidate the
 * older versions.
 */
TEST_CASE("tactics.versionThreeSnapshotStillRestoresCommandlessBattles") {
    auto    hash = testHash();
    Fixture source;
    source.build();
    REQUIRE(source.tactics.start(source.battle, eve::tactics::kInitiativePolicyId).ok());
    REQUIRE(source.tactics.advance(source.battle, step(1)).ok());

    auto captured = source.tactics.snapshot(source.battle, hash);
    REQUIRE(captured.ok());
    auto current = std::move(captured).takeValue();

    auto relabelled = eve::makeSnapshotEnvelope(current.type, current.schema, eve::SchemaVersion(3),
                                                current.instanceId, current.revision, current.tick,
                                                legacyPayload(current.payload, eve::SchemaVersion(3)), hash);
    REQUIRE(relabelled.ok());

    Fixture target;
    target.build();
    REQUIRE(target.tactics.restore(target.battle, relabelled.value(), hash).ok());
    auto migrated = target.tactics.policyId(target.battle);
    REQUIRE(migrated.ok());
    CHECK_EQ(migrated.value(), std::string(eve::tactics::kInitiativePolicyId));
}

/**
 * @brief A version-3 payload may not smuggle in the version-4 command kind.
 *
 * The payload bytes are exactly what version 4 writes; only the declared version
 * differs, so the ability command is the single fact under test. Without the
 * version gate the restore would succeed and a version-3 reader would later see a
 * command kind it cannot interpret.
 */
TEST_CASE("tactics.versionThreeSnapshotRefusesTheAbilityCommandKind") {
    auto    hash = testHash();
    Fixture source;
    source.build();
    REQUIRE(source.tactics.start(source.battle, eve::tactics::kInitiativePolicyId).ok());
    const auto actor = source.firstActor();
    REQUIRE(source.tactics.useAbility(source.battle, actor, action("test:strike"), {0, 0, 0}).ok());

    auto captured = source.tactics.snapshot(source.battle, hash);
    REQUIRE(captured.ok());
    auto current = std::move(captured).takeValue();

    auto relabelled = eve::makeSnapshotEnvelope(current.type, current.schema, eve::SchemaVersion(3),
                                                current.instanceId, current.revision, current.tick,
                                                current.payload, hash);
    REQUIRE(relabelled.ok());

    Fixture target;
    target.build();
    auto restored = target.tactics.restore(target.battle, relabelled.value(), hash);
    CHECK(!restored.ok());
    REQUIRE(restored.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(restored.status().primaryDiagnostic()->code(), eve::DiagnosticCode::ParseError);
    // The refusal must not leave a half-restored battle behind.
    auto status = target.tactics.status(target.battle);
    REQUIRE(status.ok());
    CHECK(status.value() == eve::tactics::BattleStatus::Setup);
    auto commands = target.tactics.commandsFrom(target.battle, eve::Revision(0));
    REQUIRE(commands.ok());
    CHECK(commands.value().empty());
}

/** @brief The version-4 round trip keeps both the cost and the declaration. */
TEST_CASE("tactics.abilityDeclarationSurvivesSnapshotRoundTrip") {
    auto    hash = testHash();
    Fixture source;
    source.build();
    REQUIRE(source.tactics.start(source.battle, eve::tactics::kInitiativePolicyId).ok());
    const auto actor = source.firstActor();
    REQUIRE(source.tactics.useAbility(source.battle, actor, action("test:strike"), {0, 0, 0}).ok());

    auto captured = source.tactics.snapshot(source.battle, hash);
    REQUIRE(captured.ok());
    auto current = std::move(captured).takeValue();
    CHECK(current.schemaVersion == eve::SchemaVersion(6));

    Fixture target;
    target.build();
    REQUIRE(target.tactics.restore(target.battle, current, hash).ok());

    // Both faces of the fact are restored: the spent point and the log entry, since
    // the log is what replay re-applies.
    auto resources = target.tactics.unitResources(target.battle, actor);
    REQUIRE(resources.ok());
    CHECK_EQ(resources.value().actionPoints, 0);

    auto commands = target.tactics.commandsFrom(target.battle, eve::Revision(0));
    REQUIRE(commands.ok());
    const auto values = std::move(commands).takeValue();
    REQUIRE(!values.empty());
    CHECK(values.back().kind == eve::tactics::BattleCommandKind::UseAbility);
    CHECK_EQ(values.back().action.format(), std::string("test:strike"));
    const eve::tactics::Cell declaredTarget{0, 0, 0};
    CHECK(values.back().cell == declaredTarget);
}

/** @brief Scheduling charge survives a round trip, so a saved round resumes where it was. */
TEST_CASE("tactics.chargeSurvivesSnapshotRoundTrip") {
    auto    hash = testHash();
    Fixture source;
    source.build();
    REQUIRE(source.tactics.start(source.battle, eve::tactics::kChargeTimeBattlePolicyId).ok());

    // Four quiet rounds leave the fast unit at 80 and the slow one at 40: a state that
    // decides who activates next, and which the round machine spent charge to reach.
    for (std::uint64_t tick = 1; tick <= 5; ++tick) REQUIRE(source.tactics.advance(source.battle, step(tick)).ok());
    REQUIRE_EQ(source.chargeOf(source.fast), 80);
    REQUIRE_EQ(source.chargeOf(source.slow), 40);

    auto captured = source.tactics.snapshot(source.battle, hash);
    REQUIRE(captured.ok());
    auto current = std::move(captured).takeValue();

    Fixture target;
    target.build();
    REQUIRE(target.tactics.restore(target.battle, current, hash).ok());
    CHECK_EQ(target.chargeOf(target.fast), 80);
    CHECK_EQ(target.chargeOf(target.slow), 40);

    // Continuing the restored battle must select the same unit the source would have:
    // the fifth gain takes the fast unit to exactly the threshold and leaves the slow
    // one short, so only the persisted charge can explain the choice.
    auto advanced = target.tactics.advance(target.battle, step(6));
    REQUIRE(advanced.ok());
    CHECK(advanced.value() == eve::tactics::BattlePhase::TurnStart);
    auto active = target.tactics.activeUnit(target.battle);
    REQUIRE(active.ok());
    CHECK_EQ(active.value(), target.fast);
    CHECK_EQ(target.chargeOf(target.fast), 0);
    CHECK_EQ(target.chargeOf(target.slow), 50);
}

/**
 * @brief A version-4 payload has no charge and no explicit schedule, and is migrated.
 *
 * Version 4 could not express charge-time scheduling, so the documented default is
 * "charge is zero, the roster is the round's queue". Both halves are asserted here:
 * the payload is accepted, and the missing state really is absent afterwards.
 */
TEST_CASE("tactics.versionFourSnapshotMigratesAbsentChargeAndSchedule") {
    auto    hash = testHash();
    Fixture source;
    source.build();
    REQUIRE(source.tactics.start(source.battle, eve::tactics::kChargeTimeBattlePolicyId).ok());
    for (std::uint64_t tick = 1; tick <= 5; ++tick) REQUIRE(source.tactics.advance(source.battle, step(tick)).ok());
    REQUIRE_EQ(source.chargeOf(source.fast), 80);

    auto captured = source.tactics.snapshot(source.battle, hash);
    REQUIRE(captured.ok());
    auto current = std::move(captured).takeValue();

    auto legacy = eve::makeSnapshotEnvelope(current.type, current.schema, eve::SchemaVersion(4), current.instanceId,
                                            current.revision, current.tick,
                                            legacyPayload(current.payload, eve::SchemaVersion(4)), hash);
    REQUIRE(legacy.ok());

    Fixture target;
    target.build();
    REQUIRE(target.tactics.restore(target.battle, legacy.value(), hash).ok());
    // The policy id is a v3 field, so it survives: the battle is still charge-scheduled,
    // it simply restarts its accumulation from the documented default.
    auto policy = target.tactics.policyId(target.battle);
    REQUIRE(policy.ok());
    CHECK_EQ(policy.value(), std::string(eve::tactics::kChargeTimeBattlePolicyId));
    CHECK_EQ(target.chargeOf(target.fast), 0);
    CHECK_EQ(target.chargeOf(target.slow), 0);
    // With charge reset, the fifth gain cannot reach the threshold, so the migrated
    // battle reports a quiet round instead of activating a unit on invented charge.
    auto advanced = target.tactics.advance(target.battle, step(6));
    REQUIRE(advanced.ok());
    CHECK(advanced.code() == eve::StatusCode::NoOp);
    CHECK(advanced.value() == eve::tactics::BattlePhase::RoundStart);
}

/** @brief A version-4 payload may not carry the version-5 fields. */
TEST_CASE("tactics.versionFourSnapshotRefusesVersionFiveFields") {
    auto    hash = testHash();
    Fixture source;
    source.build();
    REQUIRE(source.tactics.start(source.battle, eve::tactics::kChargeTimeBattlePolicyId).ok());
    for (std::uint64_t tick = 1; tick <= 5; ++tick) REQUIRE(source.tactics.advance(source.battle, step(tick)).ok());
    auto captured = source.tactics.snapshot(source.battle, hash);
    REQUIRE(captured.ok());
    auto current = std::move(captured).takeValue();

    // Keep the v5 payload bytes and only declare version 4: the unit charge and the
    // round schedule are then the single facts under test.
    auto mislabelled = eve::makeSnapshotEnvelope(current.type, current.schema, eve::SchemaVersion(4),
                                                 current.instanceId, current.revision, current.tick,
                                                 current.payload, hash);
    REQUIRE(mislabelled.ok());

    Fixture target;
    target.build();
    auto restored = target.tactics.restore(target.battle, mislabelled.value(), hash);
    CHECK(!restored.ok());
    REQUIRE(restored.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(restored.status().primaryDiagnostic()->code(), eve::DiagnosticCode::ParseError);
    auto status = target.tactics.status(target.battle);
    REQUIRE(status.ok());
    CHECK(status.value() == eve::tactics::BattleStatus::Setup);
}

/** @brief The version-5 round trip keeps the targeted unit and the opaque payload. */
TEST_CASE("tactics.abilityTargetAndPayloadSurviveSnapshotRoundTrip") {
    auto    hash = testHash();
    Fixture source;
    source.build();
    REQUIRE(source.tactics.start(source.battle, eve::tactics::kInitiativePolicyId).ok());
    const auto actor = source.firstActor();
    REQUIRE(source.tactics
                .useAbility(source.battle, actor, action("test:strike"), {1, 0, 0}, source.slow, "{\"power\":7}")
                .ok());

    auto captured = source.tactics.snapshot(source.battle, hash);
    REQUIRE(captured.ok());
    auto current = std::move(captured).takeValue();

    Fixture target;
    target.build();
    REQUIRE(target.tactics.restore(target.battle, current, hash).ok());

    auto commands = target.tactics.commandsFrom(target.battle, eve::Revision(0));
    REQUIRE(commands.ok());
    const auto values = std::move(commands).takeValue();
    REQUIRE(!values.empty());
    CHECK(values.back().kind == eve::tactics::BattleCommandKind::UseAbility);
    // The payload is caller-owned effect data: tactics must return it byte-identically,
    // because a replay hands it back to the effect owner.
    CHECK_EQ(values.back().payload, std::string("{\"power\":7}"));
    CHECK_EQ(values.back().targetUnit, target.slow);
    const eve::tactics::Cell declaredTarget{1, 0, 0};
    CHECK(values.back().cell == declaredTarget);
}

/**
 * @brief A version-5 payload has no ability target or payload, and is migrated.
 *
 * Version 5 could not express either, so the documented default is "no target unit, empty
 * payload". Both halves are asserted: the payload restores, and the fields really are
 * empty afterwards instead of being invented.
 */
TEST_CASE("tactics.versionFiveSnapshotMigratesAbsentAbilityTargetAndPayload") {
    auto    hash = testHash();
    Fixture source;
    source.build();
    REQUIRE(source.tactics.start(source.battle, eve::tactics::kInitiativePolicyId).ok());
    const auto actor = source.firstActor();
    REQUIRE(source.tactics
                .useAbility(source.battle, actor, action("test:strike"), {1, 0, 0}, source.slow, "{\"power\":7}")
                .ok());

    auto captured = source.tactics.snapshot(source.battle, hash);
    REQUIRE(captured.ok());
    auto current = std::move(captured).takeValue();

    auto legacy = eve::makeSnapshotEnvelope(current.type, current.schema, eve::SchemaVersion(5), current.instanceId,
                                            current.revision, current.tick,
                                            legacyPayload(current.payload, eve::SchemaVersion(5)), hash);
    REQUIRE(legacy.ok());

    Fixture target;
    target.build();
    REQUIRE(target.tactics.restore(target.battle, legacy.value(), hash).ok());
    auto commands = target.tactics.commandsFrom(target.battle, eve::Revision(0));
    REQUIRE(commands.ok());
    const auto values = std::move(commands).takeValue();
    REQUIRE(!values.empty());
    // The declaration itself survives: only the fields version 5 could not express are
    // migrated to their documented defaults.
    CHECK(values.back().kind == eve::tactics::BattleCommandKind::UseAbility);
    CHECK_EQ(values.back().action.format(), std::string("test:strike"));
    CHECK(!values.back().targetUnit.isValid());
    CHECK(values.back().payload.empty());
}

/** @brief A version-5 payload may not carry the version-6 command fields. */
TEST_CASE("tactics.versionFiveSnapshotRefusesVersionSixFields") {
    auto    hash = testHash();
    Fixture source;
    source.build();
    REQUIRE(source.tactics.start(source.battle, eve::tactics::kInitiativePolicyId).ok());
    const auto actor = source.firstActor();
    REQUIRE(source.tactics
                .useAbility(source.battle, actor, action("test:strike"), {1, 0, 0}, source.slow, "{\"power\":7}")
                .ok());
    auto captured = source.tactics.snapshot(source.battle, hash);
    REQUIRE(captured.ok());
    auto current = std::move(captured).takeValue();

    // Keep the v6 payload bytes and only declare version 5: the two extra command fields
    // are then the single facts under test.
    auto mislabelled = eve::makeSnapshotEnvelope(current.type, current.schema, eve::SchemaVersion(5),
                                                 current.instanceId, current.revision, current.tick,
                                                 current.payload, hash);
    REQUIRE(mislabelled.ok());

    Fixture target;
    target.build();
    auto restored = target.tactics.restore(target.battle, mislabelled.value(), hash);
    CHECK(!restored.ok());
    REQUIRE(restored.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(restored.status().primaryDiagnostic()->code(), eve::DiagnosticCode::ParseError);
    auto status = target.tactics.status(target.battle);
    REQUIRE(status.ok());
    CHECK(status.value() == eve::tactics::BattleStatus::Setup);
}
