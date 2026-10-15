#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "rpg/RPGActor.h"
#include "rpg/Skill.h"

using namespace eve::rpg;

TEST_CASE("rpg.actorCheckpoint.roundTripAndAtomicFailure") {
    SkillRegistry::clear();
    SkillDefinition strike;
    strike.id = "skill.strike";
    SkillRegistry::registerSkill(strike);

    RPGActor *source = RPGActor::createActor();
    REQUIRE(source != nullptr);
    source->setBaseAttribute("attack", 18.0);
    source->setBaseAttribute("hp", 100.0);
    source->setCurrent("hp", 47.0);
    source->learnSkill("skill.strike");
    source->setSkillCooldown("skill.strike", 5.0f);
    REQUIRE(source->restoreProgression(4, 25.0, 120.0).ok());
    auto encoded = source->checkpointJson();
    REQUIRE(encoded.ok());

    RPGActor *restored = RPGActor::createActor();
    REQUIRE(restored != nullptr);
    restored->setBaseAttribute("attack", 1.0);
    restored->setBaseAttribute("hp", 100.0);
    restored->setCurrent("hp", 100.0);
    REQUIRE(restored->restoreCheckpointJson(encoded.value()).ok());
    CHECK_EQ(restored->getBaseAttribute("attack"), 18.0);
    CHECK_EQ(restored->getCurrent("hp"), 47.0);
    CHECK_EQ(restored->getLevel(), 4);
    CHECK_EQ(restored->getXp(), 25.0);
    CHECK(restored->knowsSkill("skill.strike"));
    CHECK_EQ(restored->getSkillCooldown("skill.strike"), 0.0f);

    auto malformed = restored->restoreCheckpointJson(
        R"({"schema":"eve.rpg.actor-checkpoint","version":1,"baseAttributes":[],"class":{"id":"","skillsSyncedUpTo":0},"learnedSkills":[],"progression":{"level":0,"xp":0,"xpToNext":1},"traits":[],"vitals":{}})");
    CHECK(!malformed.ok());
    CHECK_EQ(restored->getBaseAttribute("attack"), 18.0);
    CHECK_EQ(restored->getLevel(), 4);
    CHECK_EQ(restored->getCurrent("hp"), 47.0);

    source->release();
    restored->release();
    SkillRegistry::clear();
}

TEST_CASE("rpg.actorCheckpoint.rejectsUnsafeCapture") {
    SkillRegistry::clear();
    SkillDefinition channel;
    channel.id = "skill.channel";
    channel.castTime = 2.0f;
    SkillRegistry::registerSkill(channel);
    RPGActor *actor = RPGActor::createActor();
    REQUIRE(actor != nullptr);
    actor->learnSkill("skill.channel");
    REQUIRE(actor->beginCastSkill("skill.channel", actor));
    CHECK(!actor->checkpointJson().ok());
    actor->cancelCastSkill();
    CHECK(actor->checkpointJson().ok());
    actor->release();
    SkillRegistry::clear();
}
