#include "zeroerr/unittest.h"

#include "inventory/Bag.h"
#include "inventory/Equipment.h"
#include "rpg/GameState.h"
#include "rpg/Quest.h"
#include "rpg/RPGActor.h"
#include "rpg/RPGSaveSession.h"
#include "rpg/Tracker.h"

#include <utility>

TEST_CASE("rpg.saveSession.preparedRestorePublishesOnlyAtCommit") {
    eve::rpg::QuestRegistry::clear();
    eve::rpg::GameState state;
    state.setVariable("gold", 25.0);
    eve::rpg::Tracker            tracker;
    auto*                        actor = eve::rpg::RPGActor::createActor();
    eve::inventory::Bag          bag(2);
    eve::inventory::EquipmentSet equipment;
    eve::rpg::RPGSaveSession     session;
    session.bind(state, tracker, *actor, bag, equipment);
    REQUIRE(session.setContentVersion("test.prepared.v1").ok());
    auto saved = session.snapshotJson();
    REQUIRE(saved.ok());

    state.setVariable("gold", 99.0);
    auto prepared = session.prepareRestoreSnapshotJson(saved.value());
    REQUIRE(prepared.ok());
    CHECK_EQ(state.getVariable("gold"), 99.0);

    session.commitPrepared(std::move(prepared).takeValue());
    CHECK_EQ(state.getVariable("gold"), 25.0);
    actor->release();
}
