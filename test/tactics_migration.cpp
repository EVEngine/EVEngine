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
 * Versions 1 and 2 stored the turn policy as the numeric enum `1` (the frozen
 * mapping to `initiative`), and version 1 had no board edge set. Nothing else
 * changes, so this isolates exactly the fields the migrations own.
 *
 * Deliberately defensive instead of using REQUIRE: it returns a value, and a
 * failing lookup should degrade to "unchanged payload" so the test fails on the
 * restore assertion rather than on an unrelated precondition.
 */
eve::Value legacyPayload(const eve::Value& current, bool keepEdges) {
    auto* root = current.getIf<eve::Value::Object>();
    if (root == nullptr) return current;
    eve::Value::Object payload = *root;

    payload["policy"] = eve::Value(1);
    if (!keepEdges) {
        auto boardIt = payload.find("board");
        if (boardIt != payload.end()) {
            if (auto* board = boardIt->second.getIf<eve::Value::Object>(); board != nullptr) board->erase("edges");
        }
    }
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
    CHECK(current.schemaVersion == eve::SchemaVersion(4));

    // Version 1 predates board edges, so the legacy payload drops that field.
    auto legacy = eve::makeSnapshotEnvelope(current.type, current.schema, eve::SchemaVersion(1), current.instanceId,
                                            current.revision, current.tick, legacyPayload(current.payload, false), hash);
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
                                            current.revision, current.tick, legacyPayload(current.payload, true), hash);
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
 * @brief The version-3 shape is still readable once version 4 exists.
 *
 * Version 4 widened only *which* command kinds are legal, so a version-3 payload
 * with no ability declaration must keep restoring. This is the "does not regress"
 * half of the gate: widening a range must not invalidate the older versions.
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
                                                current.payload, hash);
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
    CHECK(current.schemaVersion == eve::SchemaVersion(4));

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
