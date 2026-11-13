#pragma once

#include "spawn/SpawnWorld.h"
#include "zeroerr/assert.h"

/** @brief Real-domain factory contract shared by RPG and future RTS fixtures; no fake factory implementation. */
namespace spawn_factory_contract {
/** @brief Assert prepare is invisible, commit publishes once and the latest receipt replay is idempotent. */
template <class Fixture>
void creationAndReplay(Fixture& fixture) {
    REQUIRE(fixture.count() == 0);
    Fixture competing;
    REQUIRE(competing.request.source.owner == fixture.request.source.owner);
    REQUIRE(competing.request.source.epoch == fixture.request.source.epoch);
    REQUIRE(competing.request.source.incarnation != fixture.request.source.incarnation);
    // Use a newer ordinal and distinct members: rejection must not rely on receipt replay collisions.
    competing.request.ordinal = eve::spawn::OperationOrdinal(fixture.request.ordinal.value() + 1);
    competing.request.operation =
        competing.request.lifetime.operationIdentity(competing.request.ordinal).expect("foreign operation");
    for (std::size_t i = 0; i < competing.request.units.size(); ++i)
        competing.request.units[i].subject =
            competing.request.lifetime.unitIdentity(competing.request.ordinal, static_cast<std::uint32_t>(i))
                .expect("foreign member");
    auto competingPrepared = fixture.factory->prepare(competing.request, competing.positions);
    REQUIRE(competingPrepared.ok());
    auto prepared = fixture.factory->prepare(fixture.request, fixture.positions);
    REQUIRE(prepared.ok());
    REQUIRE(std::holds_alternative<std::unique_ptr<eve::spawn::IPreparedSpawn>>(prepared.value()));
    CHECK(fixture.count() == 0);
    auto& token = std::get<std::unique_ptr<eve::spawn::IPreparedSpawn>>(prepared.value());
    REQUIRE(token->commit().expect("atomic publication") == eve::spawn::CommitOutcome::Committed);
    CHECK(fixture.count() == fixture.request.units.size());
    auto competingCommit = std::get<std::unique_ptr<eve::spawn::IPreparedSpawn>>(competingPrepared.value())->commit();
    // Spatial wrappers may reject the now-occupied positions before reaching domain admission.
    if (competingCommit.ok()) REQUIRE(competingCommit.value() == eve::spawn::CommitOutcome::PlacementBlocked);
    auto competingRetry = fixture.factory->prepare(competing.request, competing.positions);
    REQUIRE(!competingRetry.ok());
    REQUIRE(fixture.count() == fixture.request.units.size());
    REQUIRE(competing.count() == 0);
    REQUIRE(token->commit().expect("atomic publication") == eve::spawn::CommitOutcome::Committed);
    CHECK(fixture.count() == fixture.request.units.size());
    auto replay = fixture.factory->prepare(fixture.request, fixture.positions);
    REQUIRE(replay.ok());
    REQUIRE(std::get<std::unique_ptr<eve::spawn::IPreparedSpawn>>(replay.value())->commit().expect("receipt replay") ==
            eve::spawn::CommitOutcome::Committed);
    CHECK(fixture.count() == fixture.request.units.size());
    auto changed = fixture.positions;
    changed[0].x += 1;
    auto collision = fixture.factory->prepare(fixture.request, changed);
    if (collision.ok())
        REQUIRE(!std::get<std::unique_ptr<eve::spawn::IPreparedSpawn>>(collision.value())->commit().ok());
    CHECK(fixture.count() == fixture.request.units.size());
}
/** @brief Assert complete, idempotent source cleanup with an explicit ownership policy. */
template <class Fixture>
void removal(Fixture& fixture, eve::spawn::DetachPolicy policy) {
    auto prepared = fixture.factory->prepare(fixture.request, fixture.positions);
    REQUIRE(prepared.ok());
    REQUIRE(std::get<std::unique_ptr<eve::spawn::IPreparedSpawn>>(prepared.value())->commit().expect("publication") ==
            eve::spawn::CommitOutcome::Committed);
    eve::spawn::RemovalRequest request;
    request.operation = fixture.request.operation.child("detach");
    request.source    = fixture.request.source;
    request.policy    = policy;
    for (const auto& unit : fixture.request.units) request.subjects.push_back(unit.subject);
    auto removed = fixture.factory->remove(request);
    REQUIRE(removed.ok());
    CHECK(removed.value().completed == fixture.request.units.size());
    CHECK(fixture.count() == (policy == eve::spawn::DetachPolicy::KeepUnits ? fixture.request.units.size() : 0));
    REQUIRE(fixture.factory->remove(request).ok());
    // A foreign runtime must not gain administrative authority after Keep clears origins.
    request.source.incarnation = eve::spawn::WorldIncarnation(~std::uint64_t{0});
    auto staleRemoval          = fixture.factory->remove(request);
    REQUIRE(!staleRemoval.ok());
    REQUIRE(fixture.count() == (policy == eve::spawn::DetachPolicy::KeepUnits ? fixture.request.units.size() : 0));
}
}  // namespace spawn_factory_contract

namespace spawn_factory_contract {
/** @brief Revoking the real source invalidates every prepared token, even while the domain survives. */
template <class Fixture>
void sourceRevocation(Fixture& fixture) {
    auto prepared = fixture.factory->prepare(fixture.request, fixture.positions);
    REQUIRE(prepared.ok());
    REQUIRE(fixture.request.lifetime.isCurrent());
    REQUIRE(fixture.sourceWorld->detach(fixture.request.source, eve::spawn::DetachPolicy::KeepUnits).ok());
    CHECK(!fixture.request.lifetime.isCurrent());
    REQUIRE(!std::get<std::unique_ptr<eve::spawn::IPreparedSpawn>>(prepared.value())->commit().ok());
    CHECK(fixture.count() == 0);
}
}  // namespace spawn_factory_contract
