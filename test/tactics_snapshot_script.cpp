#include "common/Capability.h"
#include "common/ECS.h"
#include "common/SnapshotHash.h"
#include "tactics/Tactics.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <memory>
#include <string>
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

/** @brief A hasher that counts calls and delegates, to prove the registration is used. */
class CountingHasher final : public eve::ISnapshotContentHasher {
public:
    [[nodiscard]] std::string_view id() const noexcept override { return "test-counting-noncrypto"; }
    [[nodiscard]] eve::ContentDigestKind digestKind() const noexcept override {
        return eve::ContentDigestKind::NonCryptographic;
    }
    [[nodiscard]] eve::Result<eve::ContentId> hash(std::string_view canonicalInput) const override {
        ++calls;
        return eve::builtinSnapshotContentHasher()->hash(canonicalInput);
    }

    mutable int calls = 0;
};

struct Fixture {
    ecs::Table            world;
    ecs::ScopedTable      guard{world};
    eve::tactics::Tactics tactics;
    ecs::EntityHandle     battle{};
    eve::SubjectRef       unitSubject = subject("00000000-0000-0000-0000-000000000900");

    void build() {
        auto created = tactics.newBattle(subject("00000000-0000-0000-0000-000000000901"), 11);
        REQUIRE(created.ok());
        battle = std::move(created).takeValue();
        REQUIRE(tactics.addCell(battle, {0, 0, 0}).ok());
        REQUIRE(tactics.addCell(battle, {1, 0, 0}).ok());
        auto side = tactics.newSide(battle, subject("00000000-0000-0000-0000-000000000902"));
        REQUIRE(side.ok());
        REQUIRE(tactics.newUnit(battle, side.value(), unitSubject, {}, {0, 0, 0}, {2, 300, 0, 10}).ok());
        REQUIRE(tactics.start(battle, eve::tactics::kInitiativePolicyId).ok());
        REQUIRE(tactics.advance(battle, step(1)).ok());
        REQUIRE(tactics.advance(battle, step(2)).ok());
        REQUIRE(tactics.advance(battle, step(3)).ok());
    }
};

}  // namespace

TEST_CASE("tactics.builtinSnapshotHasherSaysWhatItIsAndRegistersOnceByIdempotence") {
    const auto builtin = eve::builtinSnapshotContentHasher();
    REQUIRE(builtin != nullptr);
    // The name states the guarantee, and the kind reports it: a caller never has to guess
    // whether a weak digest was used for an integrity check or claimed as security.
    CHECK_EQ(std::string(builtin->id()), std::string("fnv1a64x2-noncrypto"));
    CHECK(builtin->digestKind() == eve::ContentDigestKind::NonCryptographic);
    CHECK_EQ(eve::contentDigestKindName(builtin->digestKind()), std::string_view("non_cryptographic"));
    // It is deterministic: the same canonical input always produces the same id.
    CHECK(eve::builtinSnapshotContentHasher()->hash("canonical").value() ==
          eve::builtinSnapshotContentHasher()->hash("canonical").value());
    CHECK(eve::builtinSnapshotContentHasher()->hash("canonical").value() !=
          eve::builtinSnapshotContentHasher()->hash("canonical2").value());

    // Host boot registers it; a second call must not replace anything and says so with NoOp.
    auto first = eve::registerBuiltinSnapshotHasher();
    REQUIRE(first.ok());
    CHECK(first.code() == eve::StatusCode::Applied);
    auto second = eve::registerBuiltinSnapshotHasher();
    REQUIRE(second.ok());
    CHECK(second.code() == eve::StatusCode::NoOp);
    CHECK(eve::registeredSnapshotContentHasher() == builtin.get());
    CHECK_EQ(eve::activeSnapshotHashAlgorithm(), std::string_view("fnv1a64x2-noncrypto"));
}

TEST_CASE("tactics.snapshotBridgeUsesTheRegisteredHasherAndFallsBackExplicitly") {
    Fixture fixture;
    fixture.build();

    // Provider absent: the engine's built-in hasher is the documented default, and the caller
    // can see that from the reported algorithm id.
    CHECK(eve::registeredSnapshotContentHasher() == nullptr);
    CHECK_EQ(fixture.tactics.snapshotHashAlgorithm(), std::string_view("fnv1a64x2-noncrypto"));
    auto builtinSnapshot = fixture.tactics.snapshotJson(fixture.battle);
    REQUIRE(builtinSnapshot.ok());

    // Provider present: the registered hasher is the one that answers, and the algorithm id
    // changes with it. This is the "present" half of the optional-provider contract.
    CountingHasher counting;
    eve::cap::provide<eve::ISnapshotContentHasher>(&counting);
    CHECK_EQ(fixture.tactics.snapshotHashAlgorithm(), std::string_view("test-counting-noncrypto"));
    const int callsBefore = counting.calls;
    auto registeredSnapshot = fixture.tactics.snapshotJson(fixture.battle);
    REQUIRE(registeredSnapshot.ok());
    CHECK(counting.calls > callsBefore);
    // The digests agree because the counting hasher delegates; the point is which one ran.
    CHECK_EQ(registeredSnapshot.value(), builtinSnapshot.value());
    // A snapshot sealed by one hasher still verifies while that hasher is active.
    CHECK(fixture.tactics.restoreJson(fixture.battle, registeredSnapshot.value()).ok());

    // Provider absent again: revoking returns to the documented default rather than leaving the
    // bridge with a dangling provider.
    eve::cap::revoke<eve::ISnapshotContentHasher>(&counting);
    CHECK(eve::registeredSnapshotContentHasher() == nullptr);
    CHECK_EQ(fixture.tactics.snapshotHashAlgorithm(), std::string_view("fnv1a64x2-noncrypto"));
    CHECK(fixture.tactics.restoreJson(fixture.battle, builtinSnapshot.value()).ok());
}

TEST_CASE("tactics.snapshotJsonRoundTripRefusesTamperingAndForeignDocuments") {
    Fixture fixture;
    fixture.build();
    auto sealed = fixture.tactics.snapshotJson(fixture.battle);
    REQUIRE(sealed.ok());
    const std::string document = std::move(sealed).takeValue();

    // Play forward, then restore: the battle really does go back.
    REQUIRE(fixture.tactics.moveUnit(fixture.battle, fixture.unitSubject, {1, 0, 0}).ok());
    auto moved = fixture.tactics.unitResources(fixture.battle, fixture.unitSubject);
    REQUIRE(moved.ok());
    const int spent = moved.value().movePoints;
    REQUIRE(fixture.tactics.restoreJson(fixture.battle, document).ok());
    auto restored = fixture.tactics.unitResources(fixture.battle, fixture.unitSubject);
    REQUIRE(restored.ok());
    CHECK(restored.value().movePoints > spent);

    // A tampered document is refused by the digest, and the battle keeps its current state.
    std::string tampered = document;
    const auto position = tampered.find("\"moveCost\":100");
    REQUIRE(position != std::string::npos);
    tampered.replace(position, 15, "\"moveCost\":900");
    auto refused = fixture.tactics.restoreJson(fixture.battle, tampered);
    CHECK(!refused.ok());
    REQUIRE(refused.status().primaryDiagnostic() != nullptr);
    CHECK(refused.status().primaryDiagnostic()->code() == eve::DiagnosticCode::HashMismatch);
    auto afterRefusal = fixture.tactics.unitResources(fixture.battle, fixture.unitSubject);
    REQUIRE(afterRefusal.ok());
    CHECK_EQ(afterRefusal.value().movePoints, restored.value().movePoints);

    // Text that is not an envelope at all is a parse failure, not a silent success.
    auto malformed = fixture.tactics.restoreJson(fixture.battle, "{\"not\":\"a snapshot\"}");
    CHECK(!malformed.ok());
    REQUIRE(malformed.status().primaryDiagnostic() != nullptr);
    CHECK(malformed.status().primaryDiagnostic()->code() == eve::DiagnosticCode::ParseError);
}

TEST_CASE("tactics.commandLogJsonReplaysThroughTheSameCodecAsRestore") {
    Fixture fixture;
    fixture.build();
    // The snapshot's own revision is the anchor a replay log has to start from; taking it from
    // the envelope keeps the two in step without a second accessor that could drift.
    auto envelope = fixture.tactics.snapshot(fixture.battle, eve::snapshotContentHashProvider());
    REQUIRE(envelope.ok());
    const eve::Revision baselineRevision = envelope.value().revision;
    auto baseline = fixture.tactics.snapshotJson(fixture.battle);
    REQUIRE(baseline.ok());
    const std::string document = std::move(baseline).takeValue();

    // A log with nothing to replay is valid, empty, and replayed as a no-op.
    auto emptyLog = fixture.tactics.commandLogJson(fixture.battle, baselineRevision);
    REQUIRE(emptyLog.ok());
    CHECK(fixture.tactics.replayJson(fixture.battle, emptyLog.value()).ok());

    // Play, capture the log, then come back and replay it.
    REQUIRE(fixture.tactics.moveUnit(fixture.battle, fixture.unitSubject, {1, 0, 0}).ok());
    REQUIRE(fixture.tactics.endTurn(fixture.battle, fixture.unitSubject).ok());
    auto log = fixture.tactics.commandLogJson(fixture.battle, baselineRevision);
    REQUIRE(log.ok());
    const std::string logText = std::move(log).takeValue();
    auto direct = fixture.tactics.snapshotJson(fixture.battle);
    REQUIRE(direct.ok());

    REQUIRE(fixture.tactics.restoreJson(fixture.battle, document).ok());
    auto replay = fixture.tactics.replayJson(fixture.battle, logText);
    REQUIRE(replay.ok());
    auto replayed = fixture.tactics.snapshotJson(fixture.battle);
    REQUIRE(replayed.ok());
    // Byte-identical after a restore + replay, which is what makes the log a replay substrate
    // rather than a debug view.
    CHECK_EQ(replayed.value(), direct.value());

    // A log captured against a different point in the battle is refused instead of being
    // applied onto state its commands were never accepted against.
    REQUIRE(fixture.tactics.advance(fixture.battle, step(9)).ok());
    auto staleLog = fixture.tactics.replayJson(fixture.battle, logText);
    CHECK(!staleLog.ok());
    // Garbage is a parse failure, not an empty replay.
    CHECK(!fixture.tactics.replayJson(fixture.battle, "[]").ok());
    CHECK(!fixture.tactics.replayJson(fixture.battle, "not json").ok());
}

TEST_CASE("tactics.snapshotScriptSurfaceRoundTripsThroughSquirrel") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    ssq::VM          vm(4096, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local module = eve.Tactics();
        local created = module.newBattle("00000000-0000-0000-0000-000000000910", 12);
        local battle = created.ok ? created.value : null;
        if (battle != null) {
            local unitId = "00000000-0000-0000-0000-000000000911";
            local sideId = "00000000-0000-0000-0000-000000000912";
            local c0 = battle.addCell(0, 0, 0, 100);
            local c1 = battle.addCell(1, 0, 0, 100);
            local side = battle.addSide(sideId);
            local u = battle.addUnit(unitId, sideId, "test:unit", 0, 0, 0, 2, 300, 0, 10);
            local started = battle.start("initiative");
            local p1 = battle.advance(1, 1);
            local p2 = battle.advance(2, 1);
            local p3 = battle.advance(3, 1);

            // Capture the acting state and the log anchor, then play forward. The log must start
            // at the revision the snapshot was taken at, or replay is refused by design.
            local baselineRevision = battle.revision();
            local snapshot = battle.snapshotJson();
            local log = battle.commandLogJson(baselineRevision);
            local algorithm = battle.snapshotAlgorithm();
            local moved = battle.move(unitId, 1, 0, 0);
            local ended = battle.endTurn(unitId);
            local directCell = battle.unitCell(unitId);
            local directPhase = battle.phase().value;

            // Restore and replay the captured log: the battle must come back byte for byte.
            local restored = battle.restoreJson(snapshot.value);
            local replayed = battle.replayJson(log.value);
            local replayCell = battle.unitCell(unitId);
            local replayPhase = battle.phase().value;

            if (c0.ok && c1.ok && side.ok && u.ok && started.ok &&
                p1.ok && p2.ok && p3.ok && moved.ok && ended.ok &&
                snapshot.ok && log.ok && restored.ok && replayed.ok &&
                algorithm == "fnv1a64x2-noncrypto" &&
                directCell.value.x == 1 && replayCell.value.x == 1 &&
                directPhase == "turn_end" && replayPhase == "turn_end") {
                result = "ok";
            }
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
}
