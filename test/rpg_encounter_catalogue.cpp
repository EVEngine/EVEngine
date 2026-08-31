#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "rpg/EncounterCatalogue.h"
#include "rpg/Quest.h"
#include "rpg/RPGActor.h"
#include "rpg/Skill.h"

namespace {

void registerEncounterDependencies() {
    eve::rpg::SkillRegistry::clear();
    eve::rpg::QuestRegistry::clear();
    eve::rpg::SkillDefinition skill;
    skill.id = "skill.enemy_claw";
    eve::rpg::SkillRegistry::registerSkill(skill);
    eve::rpg::QuestDefinition quest;
    quest.id = "quest.slayer";
    eve::rpg::QuestRegistry::registerQuest(quest);
}

constexpr const char *kValidEncounter = R"([
  {
    "id":"slime.west",
    "displayName":"West Slime",
    "members":[{"id":"west","displayName":"West Slime","skillId":"skill.enemy_claw",
      "attack":12,"defense":2.8,"maxHp":42,"speed":5}],
    "xpReward":40,
    "xpGrowth":1.2,
    "goldReward":10,
    "requiredQuestId":"quest.slayer",
    "notifyTopic":"kill",
    "notifyTarget":"slime",
    "notifyAmount":1,
    "defeatCounterId":"kills",
    "defeatCounterAmount":1,
    "levelPointAttributeId":"statPoints",
    "pointsPerLevel":3
  }
])";

}  // namespace

TEST_CASE("rpg.encounter.strictCatalogueCreatesExactActor") {
    registerEncounterDependencies();
    eve::rpg::EncounterCatalogue::clear();

    auto replaced = eve::rpg::EncounterCatalogue::replaceFromJsonStrict(kValidEncounter);
    REQUIRE(replaced.ok());
    CHECK_EQ(replaced.value(), 1);
    const auto *definition = eve::rpg::EncounterCatalogue::find("slime.west");
    REQUIRE(definition != nullptr);
    CHECK_EQ(definition->displayName, std::string("West Slime"));
    CHECK_EQ(definition->xpReward, 40.0);
    CHECK_EQ(definition->members.size(), std::size_t(1));
    CHECK_EQ(eve::rpg::EncounterCatalogue::memberCount("slime.west"), 1);

    eve::rpg::RPGActor *actor = eve::rpg::EncounterCatalogue::createActor("slime.west");
    REQUIRE(actor != nullptr);
    CHECK_EQ(actor->getFinalAttribute("attack"), 12.0);
    CHECK_EQ(actor->getFinalAttribute("defense"), 2.8);
    CHECK_EQ(actor->getFinalAttribute("hp"), 42.0);
    CHECK_EQ(actor->getFinalAttribute("speed"), 5.0);
    CHECK_EQ(actor->getCurrent("hp"), 42.0);
    CHECK(actor->knowsSkill("skill.enemy_claw"));
    actor->release();

    eve::rpg::EncounterCatalogue::clear();
    eve::rpg::QuestRegistry::clear();
    eve::rpg::SkillRegistry::clear();
}

TEST_CASE("rpg.encounter.invalidReplacementPreservesPublishedCatalogue") {
    registerEncounterDependencies();
    eve::rpg::EncounterCatalogue::clear();
    REQUIRE(eve::rpg::EncounterCatalogue::replaceFromJsonStrict(kValidEncounter).ok());

    auto unknownField = eve::rpg::EncounterCatalogue::replaceFromJsonStrict(
        R"([{"id":"bad","displayName":"Bad","members":[{"id":"bad","displayName":"Bad","skillId":"skill.enemy_claw","attack":1,"defense":0,"maxHp":1,"speed":0}],"xpReward":1,"xpGrowth":1.2,"goldReward":0,"requiredQuestId":"quest.slayer","notifyTopic":"kill","notifyTarget":"slime","notifyAmount":1,"defeatCounterId":"kills","defeatCounterAmount":1,"levelPointAttributeId":"statPoints","pointsPerLevel":0,"typo":true}])");
    CHECK(!unknownField.ok());
    CHECK(eve::rpg::EncounterCatalogue::find("slime.west") != nullptr);
    CHECK_EQ(eve::rpg::EncounterCatalogue::count(), 1);

    auto missingSkill = eve::rpg::EncounterCatalogue::replaceFromJsonStrict(
        R"([{"id":"bad","displayName":"Bad","members":[{"id":"bad","displayName":"Bad","skillId":"skill.missing","attack":1,"defense":0,"maxHp":1,"speed":0}],"xpReward":1,"xpGrowth":1.2,"goldReward":0,"requiredQuestId":"quest.slayer","notifyTopic":"kill","notifyTarget":"slime","notifyAmount":1,"defeatCounterId":"kills","defeatCounterAmount":1,"levelPointAttributeId":"statPoints","pointsPerLevel":0}])");
    CHECK(!missingSkill.ok());
    CHECK(eve::rpg::EncounterCatalogue::find("slime.west") != nullptr);

    eve::rpg::EncounterCatalogue::clear();
    eve::rpg::QuestRegistry::clear();
    eve::rpg::SkillRegistry::clear();
}
