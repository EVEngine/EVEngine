#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "rpg/Party.h"

TEST_CASE("rpg.partyProtobuf.binaryRoundTripAndAtomicFailure") {
    eve::rpg::Party party;
    auto *hero = eve::rpg::RPGActor::createActor();
    auto *companion = eve::rpg::RPGActor::createActor();
    REQUIRE(hero != nullptr);
    REQUIRE(companion != nullptr);
    hero->setBaseAttribute("attack", 20.0);
    companion->setBaseAttribute("attack", 12.0);
    REQUIRE(party.addMember("hero", hero).ok());
    REQUIRE(party.addMember("companion", companion).ok());
    auto bytes = party.checkpointBinary();
    REQUIRE(bytes.ok());

    hero->setBaseAttribute("attack", 1.0);
    companion->setBaseAttribute("attack", 2.0);
    REQUIRE(party.restoreCheckpointBinary(bytes.value()).ok());
    CHECK_EQ(hero->getBaseAttribute("attack"), 20.0);
    CHECK_EQ(companion->getBaseAttribute("attack"), 12.0);

    hero->setBaseAttribute("attack", 7.0);
    CHECK(!party.restoreCheckpointBinary("malformed protobuf").ok());
    CHECK_EQ(hero->getBaseAttribute("attack"), 7.0);
    REQUIRE(party.removeMember("companion").ok());
    CHECK(!party.restoreCheckpointBinary(bytes.value()).ok());
    CHECK_EQ(hero->getBaseAttribute("attack"), 7.0);

    hero->release();
    companion->release();
}
