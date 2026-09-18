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
    CHECK(current.schemaVersion == eve::SchemaVersion(3));

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
