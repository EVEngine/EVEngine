#pragma once
#include "RTSSpawnFixture.h"
#include "rts/spawn/RTSSpawnCapture.h"
#include "spawn/SpawnCheckpoint.h"
#include "spawn/SpawnRestoreState.h"
namespace test_rts_spawn {
// Test-only deterministic digest, not an integrity/security recommendation for shipped saves.
inline eve::Result<eve::ContentId> restoreHash(std::string_view text) {
    std::uint64_t state = 14695981039346656037ULL;
    for (unsigned char byte : text) state = (state ^ byte) * 1099511628211ULL;
    eve::ContentId::Bytes bytes{};
    for (auto& byte : bytes) {
        byte  = static_cast<std::uint8_t>(state);
        state = (state ^ byte) * 1099511628211ULL;
    }
    return eve::Result<eve::ContentId>::success(eve::ContentId(bytes));
}
struct SavedRTSSpawn {
    eve::SnapshotEnvelope                                  core;
    std::shared_ptr<const eve::rts_spawn::RTSSpawnCapture> provenance;
    eve::rts::RTSStateSnapshot                             roots;
    std::vector<eve::spawn::SavedLifecycleInput>           pending;
    std::vector<eve::rts::RTSFrameDamageEvent>             damage;
};
inline SavedRTSSpawn freezeSpawn(Fixture& source, eve::PersistentId epoch) {
    auto core       = source.sourceWorld->capture(epoch, eve::Revision(1)).expect("core capture");
    auto provenance = source.factory->capture(epoch, core.unitReferences()).expect("provenance capture");
    auto roots      = source.domain->snapshotState().expect("native roots");
    return {core.seal(restoreHash).expect("core seal"),
            std::make_shared<const eve::rts_spawn::RTSSpawnCapture>(std::move(provenance)),
            std::move(roots),
            {},
            {}};
}
// Native source binding fixture: root reconstruction is real; other game participants are intentionally separate.
struct RestoredRTSSpawn {
    std::shared_ptr<ecs::Table>                      table = std::make_shared<ecs::Table>();
    eve::crowd::Crowd                                crowd;
    std::unique_ptr<eve::rts::RTS>                   domain = std::make_unique<eve::rts::RTS>(table);
    std::shared_ptr<eve::rts_spawn::RTSSpawnFactory> factory;
    std::shared_ptr<eve::spawn::SpawnProviders>      providers;
    std::unique_ptr<eve::spawn::SpawnWorld>          core;
    explicit RestoredRTSSpawn(const SavedRTSSpawn& saved) {
        crowd.setClampToField(false);
        domain->setCrowdProvider(&crowd);
        ecs::ScopedTable scope(*table);
        domain->rebuildState(saved.roots).expect("isolated native root rebuild");
        factory   = eve::rts_spawn::RTSSpawnFactory::prepareRestored(*domain, saved.provenance).expect("saved factory");
        providers = std::make_shared<eve::spawn::SpawnProviders>(
            eve::spawn::SpawnProviders{std::make_shared<Space>(), factory});
        auto admitted =
            eve::spawn::SpawnRestoreState::decode(saved.core, saved.provenance->saveEpoch(), restoreHash, *factory)
                .expect("canonical core admission using saved future recipes");
        core = std::make_unique<eve::spawn::SpawnWorld>(
            eve::spawn::SpawnWorld::restore(admitted, providers).expect("fresh core source incarnation"));
    }
};
}  // namespace test_rts_spawn
