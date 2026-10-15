#include "RtsCompositionFixtures.h"

namespace {
void loadProduction(eve::rts::RTS& rts) {
    auto configured = rts.configureScriptWorld(32, 32, 1.f);
    REQUIRE(configured.ok());
    auto loaded = rts.loadScriptContent(R"({
        "units":[{"id":"marine","producer":"barracks","costResource":"minerals",
                  "cost":50,"buildTime":0.1,"health":80,"speed":0,"radius":0.5}],
        "buildings":[{"id":"barracks","health":500,"buildTime":1}]
    })");
    REQUIRE(loaded.ok());
}
}  // namespace

TEST_CASE("rts.crowd.productionPushPublishesPeerMotion") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    rts;
    loadProduction(rts);
    auto* faction  = rts.newFaction(subject("00000000-0000-7000-8000-000000000801")).takeValue();
    auto* producer = rts.newFactionBuilding(*faction, subject("00000000-0000-7000-8000-000000000802"),
                                            *eve::LogicalId::parse("building:barracks"))
                         .takeValue();
    producer->placement()->worldX = 10.f;
    producer->placement()->worldY = 10.f;
    const auto definition         = *eve::LogicalId::parse("unit:marine");
    auto*      neighbor =
        rts.newFactionUnit(*faction, subject("00000000-0000-7000-8000-000000000803"), definition).takeValue();
    neighbor->motion()->x = 10.f;
    neighbor->motion()->y = 10.f;
    const auto born       = subject("00000000-0000-7000-8000-000000000804");
    auto       credit     = rts.addScriptResource(*faction, "minerals", 100);
    REQUIRE(credit.ok());
    auto queued = rts.queueScriptUnit(*producer, born, definition);
    REQUIRE(queued.ok());
    auto stepped = rts.stepScript(0.1);
    REQUIRE(stepped.ok());
    Unit* created = rts.findUnit(born);
    REQUIRE(created != nullptr);
    if (!created) return;
    CHECK_EQ(created->motion()->x, 10.f);
    CHECK_EQ(created->motion()->y, 10.f);
    CHECK(std::hypot(neighbor->motion()->x - 10.f, neighbor->motion()->y - 10.f) >= 0.999f);
    CHECK_EQ(rts.scriptResource(*faction, "minerals").value(), 50);
    stepped = rts.stepScript(0.1);
    REQUIRE(stepped.ok());
    CHECK(std::hypot(neighbor->motion()->x - created->motion()->x, neighbor->motion()->y - created->motion()->y) >=
          0.999f);
    CHECK_EQ(rts.unitCount(), 2u);
}

TEST_CASE("rts.crowd.blockedProductionRetainsPaymentAndIdentityForRetry") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    rts;
    loadProduction(rts);
    auto* faction  = rts.newFaction(subject("00000000-0000-7000-8000-000000000811")).takeValue();
    auto* producer = rts.newFactionBuilding(*faction, subject("00000000-0000-7000-8000-000000000812"),
                                            *eve::LogicalId::parse("building:barracks"))
                         .takeValue();
    producer->placement()->worldX = 10.f;
    producer->placement()->worldY = 10.f;
    const auto definition         = *eve::LogicalId::parse("unit:marine");
    auto* held = rts.newFactionUnit(*faction, subject("00000000-0000-7000-8000-000000000813"), definition).takeValue();
    held->motion()->x = 10.f;
    held->motion()->y = 10.f;
    CommandSpec hold;
    hold.kind    = OrderKind::HoldPosition;
    auto ordered = held->orders()->values.replace(hold);
    REQUIRE(ordered.ok());
    const auto born   = subject("00000000-0000-7000-8000-000000000814");
    auto       credit = rts.addScriptResource(*faction, "minerals", 100);
    REQUIRE(credit.ok());
    auto queued = rts.queueScriptUnit(*producer, born, definition);
    REQUIRE(queued.ok());
    for (int i = 0; i < 3; ++i) {
        auto stepped = rts.stepScript(0.1);
        REQUIRE(stepped.ok());
        CHECK(rts.findUnit(born) == nullptr);
        CHECK_EQ(rts.unitCount(), 1u);
        CHECK_EQ(faction->members()->units.size(), 1u);
        CHECK(producer->rally()->productionSpawnBlocked);
        CHECK(producer->rally()->settledProductionTasks.empty());
        auto task = producer->production()->values.taskAt(0);
        REQUIRE(task.has_value());
        CHECK(task->get().settlementRequired);
        CHECK(task->get().state == eve::production::TaskState::ReadyToSettle);
        CHECK(task->get().settlement.settlementId.empty());
        CHECK_EQ(rts.scriptResource(*faction, "minerals").value(), 50);
        CHECK_EQ(held->motion()->x, 10.f);
        CHECK_EQ(held->motion()->y, 10.f);
    }
    auto duplicate = rts.newUnit(born, definition);
    CHECK(!duplicate.ok());
    auto duplicateBuilding = rts.newBuilding(born);
    CHECK(!duplicateBuilding.ok());
    auto duplicateQueue = rts.queueScriptUnit(*producer, born, definition);
    CHECK(!duplicateQueue.ok());
    CHECK_EQ(rts.scriptResource(*faction, "minerals").value(), 50);
    const auto factionSubject  = faction->identity()->subject;
    const auto producerSubject = producer->identity()->subject;
    const auto heldSubject     = held->identity()->subject;
    auto       checkpoint      = rts.captureScriptCheckpoint("blocked-spawn");
    REQUIRE(checkpoint.ok());
    held->motion()->x = 20.f;
    auto stepped      = rts.stepScript(0.1);
    REQUIRE(stepped.ok());
    CHECK(rts.findUnit(born) != nullptr);
    CHECK(!producer->rally()->productionSpawnBlocked);
    CHECK_EQ(producer->rally()->settledProductionTasks.size(), 1u);
    {
        auto task = producer->production()->values.taskAt(0);
        REQUIRE(task.has_value());
        CHECK(task->get().state == eve::production::TaskState::Completed);
        CHECK_EQ(task->get().settlement.settlementId, "rts.unit:" + task->get().id);
    }
    CHECK_EQ(rts.unitCount(), 2u);
    CHECK_EQ(rts.scriptResource(*faction, "minerals").value(), 50);
    auto restored = rts.restoreScriptCheckpoint("blocked-spawn");
    REQUIRE(restored.ok());
    // Restore rebuilds ECS roots; old pointers must not cross this boundary.
    faction  = rts.findFaction(factionSubject);
    producer = rts.findBuilding(producerSubject);
    held     = rts.findUnit(heldSubject);
    REQUIRE(faction != nullptr);
    REQUIRE(producer != nullptr);
    REQUIRE(held != nullptr);
    CHECK(rts.findUnit(born) == nullptr);
    CHECK_EQ(rts.scriptResource(*faction, "minerals").value(), 50);
    stepped = rts.stepScript(0.1);
    REQUIRE(stepped.ok());
    CHECK(producer->rally()->productionSpawnBlocked);
    CHECK_EQ(rts.unitCount(), 1u);
    auto stillReserved = rts.newUnit(born, definition);
    CHECK(!stillReserved.ok());
    held->motion()->x = 20.f;
    stepped           = rts.stepScript(0.1);
    REQUIRE(stepped.ok());
    CHECK(rts.findUnit(born) != nullptr);
    CHECK_EQ(producer->rally()->settledProductionTasks.size(), 1u);
    CHECK_EQ(rts.unitCount(), 2u);
    CHECK_EQ(rts.scriptResource(*faction, "minerals").value(), 50);
}

TEST_CASE("rts.crowd.scriptProfileCrossingReachesBothDestinations") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    rts;
    auto             configured = rts.configureScriptWorld(32, 32, 1.f);
    REQUIRE(configured.ok());
    auto loaded = rts.loadScriptContent(R"({"units":[{"id":"walker","health":100,"speed":2,"radius":0.4}]})");
    REQUIRE(loaded.ok());
    auto       faction    = rts.newFaction(subject("00000000-0000-7000-8000-000000000831")).takeValue();
    const auto definition = *eve::LogicalId::parse("unit:walker");
    Unit* a = rts.newFactionUnit(*faction, subject("00000000-0000-7000-8000-000000000832"), definition).takeValue();
    Unit* b = rts.newFactionUnit(*faction, subject("00000000-0000-7000-8000-000000000833"), definition).takeValue();
    a->motion()->x = 5.5f;
    b->motion()->x = 15.5f;
    a->motion()->y = b->motion()->y = 10.5f;
    a->motion()->arrivalRadius = b->motion()->arrivalRadius = 0.3f;
    CommandSpec move;
    move.kind   = OrderKind::Move;
    move.target = {15.5f, 10.5f};
    auto issued = a->orders()->values.replace(move);
    REQUIRE(issued.ok());
    move.target = {5.5f, 10.5f};
    issued      = b->orders()->values.replace(move);
    REQUIRE(issued.ok());
    float minimumSeparation = 100.f;
    for (int tick = 0; tick < 240; ++tick) {
        auto stepped = rts.stepScript(0.05);
        REQUIRE(stepped.ok());
        minimumSeparation =
            std::min(minimumSeparation, std::hypot(a->motion()->x - b->motion()->x, a->motion()->y - b->motion()->y));
    }
    CHECK(minimumSeparation >= 0.799f);
    CHECK(std::hypot(a->motion()->x - 15.5f, a->motion()->y - 10.5f) < 0.5f);
    CHECK(std::hypot(b->motion()->x - 5.5f, b->motion()->y - 10.5f) < 0.5f);
}
