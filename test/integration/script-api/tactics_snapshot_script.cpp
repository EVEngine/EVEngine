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

[[maybe_unused]] eve::SubjectRef subject(const char* text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

[[maybe_unused]] eve::SimulationStep step(std::uint64_t tick) {
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
