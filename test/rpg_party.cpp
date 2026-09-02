#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "rpg/Battle.h"
#include "rpg/Party.h"
#include "rpg/RPGActor.h"

namespace {

eve::rpg::RPGActor *makePartyActor() {
    auto *actor = eve::rpg::RPGActor::createActor();
    actor->setBaseAttribute("hp", 100.0);
    actor->setCurrent("hp", 100.0);
    return actor;
}

}  // namespace

TEST_CASE("rpg.party.orderedRosterRejectsDuplicateIdsAndActors") {
    eve::rpg::Party party;
    auto *hero = makePartyActor();
    auto *companion = makePartyActor();

    REQUIRE(party.addMember("hero", hero).ok());
    REQUIRE(party.addMember("companion.ranger", companion).ok());
    CHECK_EQ(party.count(), 2);
    CHECK_EQ(party.getMemberId(0), std::string("hero"));
    CHECK(party.getMemberActor(1) == companion);
    CHECK(party.findMemberActor("hero") == hero);
    CHECK(!party.addMember("hero", companion).ok());
    CHECK(!party.addMember("duplicate.actor", hero).ok());
    CHECK_EQ(party.count(), 2);

    REQUIRE(party.removeMember("companion.ranger").ok());
    CHECK_EQ(party.count(), 1);
    CHECK(companion->getCurrent("hp") == 100.0);
    hero->release();
    companion->release();
}

TEST_CASE("rpg.party.staleRosterFailsBattleCompositionBeforeMutation") {
    eve::rpg::Party party;
    auto *hero = makePartyActor();
    auto *companion = makePartyActor();
    REQUIRE(party.addMember("hero", hero).ok());
    REQUIRE(party.addMember("companion", companion).ok());
    companion->release();

    CHECK(party.hasStaleMembers());
    eve::rpg::Battle battle;
    auto composed = party.addToBattle(&battle, eve::rpg::BattleSide::Party);
    CHECK(!composed.ok());
    CHECK_EQ(battle.getActorCount(), 0);

    party.clear();
    REQUIRE(party.addMember("hero", hero).ok());
    auto valid = party.addToBattle(&battle, eve::rpg::BattleSide::Party);
    REQUIRE(valid.ok());
    CHECK_EQ(valid.value(), 1);
    CHECK_EQ(battle.getActorCount(), 1);
    hero->release();
}

TEST_CASE("rpg.party.checkpointRestoresAllMembersAtomically") {
    eve::rpg::Party party;
    auto *hero = makePartyActor();
    auto *companion = makePartyActor();
    hero->setBaseAttribute("attack", 20.0);
    companion->setBaseAttribute("attack", 12.0);
    REQUIRE(party.addMember("hero", hero).ok());
    REQUIRE(party.addMember("companion", companion).ok());

    auto checkpoint = party.checkpointJson();
    REQUIRE(checkpoint.ok());
    hero->setBaseAttribute("attack", 1.0);
    companion->setBaseAttribute("attack", 2.0);
    REQUIRE(party.restoreCheckpointJson(checkpoint.value()).ok());
    CHECK_EQ(hero->getBaseAttribute("attack"), 20.0);
    CHECK_EQ(companion->getBaseAttribute("attack"), 12.0);

    REQUIRE(party.removeMember("companion").ok());
    hero->setBaseAttribute("attack", 7.0);
    CHECK(!party.restoreCheckpointJson(checkpoint.value()).ok());
    CHECK_EQ(hero->getBaseAttribute("attack"), 7.0);

    hero->release();
    companion->release();
}

TEST_CASE("rpg.party.checkpointRecoveryValidatesWholeRosterBeforeReviving") {
    eve::rpg::Party party;
    auto *hero = makePartyActor();
    auto *companion = makePartyActor();
    hero->setBaseAttribute("mp", 40.0);
    companion->setBaseAttribute("mp", 20.0);
    hero->setCurrent("hp", 0.0);
    companion->setCurrent("hp", 0.0);
    hero->setCurrent("mp", 0.0);
    companion->setCurrent("mp", 0.0);
    REQUIRE(party.addMember("hero", hero).ok());
    REQUIRE(party.addMember("companion", companion).ok());

    CHECK(!party.recoverAtCheckpoint("hp", 0.0, "mp", 0.5).ok());
    CHECK_EQ(hero->getCurrent("hp"), 0.0);
    CHECK_EQ(companion->getCurrent("hp"), 0.0);
    auto recovered = party.recoverAtCheckpoint("hp", 0.5, "mp", 0.25);
    REQUIRE(recovered.ok());
    CHECK_EQ(recovered.value(), 2);
    CHECK_EQ(hero->getCurrent("hp"), 50.0);
    CHECK_EQ(companion->getCurrent("hp"), 50.0);
    CHECK_EQ(hero->getCurrent("mp"), 10.0);
    CHECK_EQ(companion->getCurrent("mp"), 5.0);

    companion->release();
    hero->setCurrent("hp", 3.0);
    CHECK(!party.recoverAtCheckpoint("hp", 1.0, "mp", 1.0).ok());
    CHECK_EQ(hero->getCurrent("hp"), 3.0);
    hero->release();
}
