#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "inventory/Bag.h"
#include "inventory/Equipment.h"
#include "inventory/Item.h"
#include "rpg/GameState.h"
#include "rpg/Party.h"
#include "rpg/RPGActor.h"
#include "rpg/RpgDialect.h"

#include <string>

namespace {

using namespace eve::rpg;

/** @brief Borrowed RPG objects one story runs against, plus their owning actor. */
struct StoryFixture {
    GameState                     state;
    Party                         party;
    eve::inventory::Bag           bag{8};
    eve::inventory::EquipmentSet  equipment;
    RPGActor*                     hero = nullptr;

    StoryFixture() {
        hero = RPGActor::createActor();
        party.addMember("hero", hero).ignore("story fixture");
        equipment.defineSlot("weapon");
        hero->setBaseAttribute("attack", 10.0);

        eve::inventory::ItemDefinition potion;
        potion.id         = "potion";
        potion.displayName = "Potion";
        potion.maxStack   = 99;
        potion.tags       = {"consumable"};
        eve::inventory::ItemRegistry::registerItem(potion);

        eve::inventory::ItemDefinition sword;
        sword.id         = "iron-sword";
        sword.displayName = "Iron Sword";
        sword.maxStack   = 1;
        sword.equipSlot  = "weapon";
        sword.tags       = {"weapon"};
        eve::inventory::ItemRegistry::registerItem(sword);

        // The `equipment` step equips *from the bag*, so the sword starts there.
        bag.addItem("iron-sword", 1);
    }

    ~StoryFixture() {
        eve::inventory::ItemRegistry::clear();
        if (hero) hero->release();
    }

    StoryFixture(const StoryFixture&)            = delete;
    StoryFixture& operator=(const StoryFixture&) = delete;

    RpgStoryBinding binding() {
        RpgStoryBinding value;
        value.gameState = &state;
        value.party     = &party;
        value.bag       = &bag;
        value.equipment = &equipment;
        return value;
    }
};

}  // namespace

TEST_CASE("rpg.dnut.runsDomainAndPresentationSteps") {
    StoryFixture fixture;
    RpgStoryBinding binding = fixture.binding();

    RpgStoryCatalogue::clear();
    REQUIRE(RpgStoryCatalogue::replaceFromDnutStrict(R"(
story village.arrival {
    move actor=hero x=10 y=4 duration=0.5
    skill actor=hero learn=fireball
    attribute actor=hero name=attack op=add value=5
    item add=potion count=2
    equipment actor=hero equip=iron-sword slot=weapon
    flag name=village.visited state=true
    variable name=gold op=add value=25
    message text=Arrived
}
)",
                                                     "village.dnut")
                .ok());
    CHECK_EQ(RpgStoryCatalogue::count(), 1);
    CHECK(RpgStoryCatalogue::contains("village.arrival"));

    RpgStorySession session;
    REQUIRE(session.begin("village.arrival", &binding).ok());
    CHECK(session.isActive());
    CHECK(session.isBlocked());
    CHECK_EQ(session.getStepKind(), std::string("move"));
    CHECK_EQ(session.getStepPayload().find("actor")->asString(), std::string("hero"));

    // Presentation step: the domain effects have not run yet.
    CHECK(!fixture.hero->knowsSkill("fireball"));

    REQUIRE(session.advance().ok());
    CHECK_EQ(session.getStepKind(), std::string("message"));
    CHECK(fixture.hero->knowsSkill("fireball"));
    CHECK_EQ(fixture.hero->getBaseAttribute("attack"), 15.0);
    CHECK_EQ(fixture.bag.countItem("potion"), 2);
    CHECK_EQ(fixture.equipment.getSlotItemId("weapon"), std::string("iron-sword"));
    CHECK(fixture.state.isSwitchOn("village.visited"));
    CHECK_EQ(fixture.state.getVariable("gold"), 25.0);

    REQUIRE(session.advance().ok());
    CHECK(!session.isActive());
    CHECK(fixture.state.hasSelfVariable("story.village.arrival", "completed"));

    RpgStoryCatalogue::clear();
}

TEST_CASE("rpg.dnut.rejectsInvalidStepPayloads") {
    RpgStoryCatalogue::clear();
    const auto rejected = [](const std::string& body) {
        return !RpgStoryCatalogue::replaceFromDnutStrict("story probe {\n" + body + "\n}\n", "probe.dnut").ok();
    };

    CHECK(rejected("    teleport actor=hero"));
    CHECK(rejected("    skill actor=hero"));
    CHECK(rejected("    skill actor=hero learn=fireball forget=fireball"));
    CHECK(rejected("    attribute actor=hero name=attack op=bogus value=1"));
    CHECK(rejected("    attribute actor=hero name=attack op=add"));
    CHECK(rejected("    equipment actor=hero equip=iron-sword"));
    CHECK(rejected("    item add=potion remove=potion"));
    CHECK(rejected("    move actor=hero x=1 y=2 z=3"));
    CHECK(rejected("    variable name=gold op=multiply value=2"));
    CHECK(rejected("    if switch.gate == true {\n        teleport\n    }"));

    // A document without any story block must not silently wipe the catalogue.
    CHECK(!RpgStoryCatalogue::replaceFromDnutStrict("pool village {\n    elder: \"hi\"\n}\n", "pool.dnut").ok());
    CHECK_EQ(RpgStoryCatalogue::count(), 0);
}

TEST_CASE("rpg.dnut.gatesNonRepeatableStoriesOnCompletion") {
    StoryFixture    fixture;
    RpgStoryBinding binding = fixture.binding();
    RpgStoryCatalogue::clear();
    REQUIRE(RpgStoryCatalogue::replaceFromDnutStrict(R"(
story once {
    variable name=once op=set value=1
}
story again repeatable {
    variable name=again op=add value=1
}
)",
                                                     "gates.dnut")
                .ok());

    RpgStorySession session;
    REQUIRE(session.begin("once", &binding).ok());
    CHECK(!session.isActive());
    CHECK_EQ(fixture.state.getVariable("once"), 1.0);
    auto repeat = session.begin("once", &binding);
    CHECK(!repeat.ok());
    const auto* repeatDiagnostic = repeat.error();
    REQUIRE(repeatDiagnostic != nullptr);
    CHECK(repeatDiagnostic->message().find("already complete") != std::string::npos);

    REQUIRE(session.begin("again", &binding).ok());
    REQUIRE(session.begin("again", &binding).ok());
    CHECK_EQ(fixture.state.getVariable("again"), 2.0);

    RpgStoryCatalogue::clear();
}

TEST_CASE("rpg.dnut.branchesOnGameState") {
    StoryFixture    fixture;
    RpgStoryBinding binding = fixture.binding();
    RpgStoryCatalogue::clear();
    REQUIRE(RpgStoryCatalogue::replaceFromDnutStrict(R"(
story gate.check repeatable {
    if switch.gate == true {
        variable name=path op=set value=1
    } else {
        variable name=path op=set value=2
    }
}
)",
                                                     "gate.dnut")
                .ok());

    RpgStorySession session;
    REQUIRE(session.begin("gate.check", &binding).ok());
    CHECK_EQ(fixture.state.getVariable("path"), 2.0);

    fixture.state.switchOn("gate");
    REQUIRE(session.begin("gate.check", &binding).ok());
    CHECK_EQ(fixture.state.getVariable("path"), 1.0);

    RpgStoryCatalogue::clear();
}

TEST_CASE("rpg.dnut.selectsChoiceRoutes") {
    StoryFixture    fixture;
    RpgStoryBinding binding = fixture.binding();
    RpgStoryCatalogue::clear();
    REQUIRE(RpgStoryCatalogue::replaceFromDnutStrict(R"(
story shop.buy repeatable {
    choice {
        option "Buy" when variable.gold >= 10 {
            variable name=bought op=set value=1
        }
        option "Leave" {
            variable name=bought op=set value=0
        }
    }
}
)",
                                                     "shop.dnut")
                .ok());

    RpgStorySession session;
    REQUIRE(session.begin("shop.buy", &binding).ok());
    CHECK_EQ(session.getStepKind(), std::string("choice"));
    REQUIRE_EQ(session.getChoiceLabels().size(), 2u);
    CHECK_EQ(session.getChoiceLabels()[0], std::string("Buy"));

    // The conditional route is rejected while the state does not satisfy it.
    CHECK(!session.select("Buy").ok());
    CHECK(session.isBlocked());
    CHECK(!session.select("Nothing").ok());

    REQUIRE(session.select("Leave").ok());
    CHECK_EQ(fixture.state.getVariable("bought"), 0.0);

    fixture.state.setVariable("gold", 20.0);
    REQUIRE(session.begin("shop.buy", &binding).ok());
    REQUIRE(session.select("Buy").ok());
    CHECK_EQ(fixture.state.getVariable("bought"), 1.0);

    RpgStoryCatalogue::clear();
}

TEST_CASE("rpg.dnut.reportFailureWhenDomainStateIsMissing") {
    StoryFixture    fixture;
    RpgStoryBinding binding = fixture.binding();
    RpgStoryCatalogue::clear();
    REQUIRE(RpgStoryCatalogue::replaceFromDnutStrict(R"(
story broke {
    item remove=potion count=3
}
)",
                                                     "broke.dnut")
                .ok());

    RpgStorySession session;
    auto             start = session.begin("broke", &binding);
    CHECK(!start.ok());
    CHECK(!session.isActive());
    const auto* diagnostic = start.error();
    REQUIRE(diagnostic != nullptr);
    CHECK(diagnostic->message().find("does not hold") != std::string::npos);

    RpgStoryCatalogue::clear();
}

TEST_CASE("rpg.dnut.capturesAndRestoresCursor") {
    StoryFixture    fixture;
    RpgStoryBinding binding = fixture.binding();
    RpgStoryCatalogue::clear();
    REQUIRE(RpgStoryCatalogue::replaceFromDnutStrict(R"(
story resume.me {
    variable name=step op=set value=1
    message text=pause
    variable name=step op=set value=2
}
)",
                                                     "resume.dnut")
                .ok());

    RpgStorySession session;
    REQUIRE(session.begin("resume.me", &binding).ok());
    CHECK_EQ(session.getStepKind(), std::string("message"));
    CHECK_EQ(fixture.state.getVariable("step"), 1.0);

    eve::Value captured;
    REQUIRE(session.captureState(captured).ok());

    RpgStorySession restored;
    REQUIRE(restored.restoreState("resume.me", captured, &binding).ok());
    CHECK_EQ(restored.getStepKind(), std::string("message"));
    REQUIRE(restored.advance().ok());
    CHECK_EQ(fixture.state.getVariable("step"), 2.0);
    CHECK(!restored.isActive());

    RpgStoryCatalogue::clear();
}
