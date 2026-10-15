#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "../proto/generated/rpg_save_session.pb.h"
#include "inventory/Bag.h"
#include "inventory/Equipment.h"
#include "rpg/GameState.h"
#include "rpg/Party.h"
#include "rpg/Quest.h"
#include "rpg/RPGActor.h"
#include "rpg/RPGSaveSession.h"
#include "rpg/Skill.h"
#include "rpg/Tracker.h"

TEST_CASE("rpg.saveSessionProtobuf.binaryIntegrityAndAtomicRestore") {
    eve::rpg::QuestRegistry::clear();
    eve::rpg::GameState state;
    state.setVariable("gold", 25.0);
    eve::rpg::Tracker tracker;
    auto             *actor = eve::rpg::RPGActor::createActor();
    REQUIRE(actor != nullptr);
    actor->setBaseAttribute("hp", 100.0);
    actor->setCurrent("hp", 70.0);
    eve::inventory::Bag          bag(4);
    eve::inventory::EquipmentSet equipment;
    eve::rpg::RPGSaveSession     session;
    session.bind(state, tracker, *actor, bag, equipment);
    REQUIRE(session.setContentVersion("content.v1").ok());
    auto bytes = session.snapshotBinary();
    REQUIRE(bytes.ok());

    state.setVariable("gold", 99.0);
    actor->setCurrent("hp", 1.0);
    eve::rpg::proto::RPGSaveEnvelope corrupted;
    REQUIRE(corrupted.ParseFromString(bytes.value()));
    auto damaged = corrupted.payload_wire();
    REQUIRE(!damaged.empty());
    damaged[0] = static_cast<char>(damaged[0] ^ 1);
    corrupted.set_payload_wire(std::move(damaged));
    std::string invalid;
    REQUIRE(corrupted.SerializeToString(&invalid));
    auto rejected = session.restoreSnapshotBinary(invalid);
    CHECK(!rejected.ok());
    REQUIRE(rejected.error() != nullptr);
    CHECK_EQ(static_cast<int>(rejected.error()->code()), static_cast<int>(eve::DiagnosticCode::HashMismatch));
    CHECK_EQ(state.getVariable("gold"), 99.0);
    CHECK_EQ(actor->getCurrent("hp"), 1.0);

    auto future = corrupted;
    future.set_version(4);
    std::string futureBytes;
    REQUIRE(future.SerializeToString(&futureBytes));
    CHECK(!session.restoreSnapshotBinary(futureBytes).ok());
    std::string unknown = bytes.value();
    unknown.append("\x98\x06\x01", 3);  // Field 99, varint 1.
    CHECK(!session.restoreSnapshotBinary(unknown).ok());
    CHECK_EQ(state.getVariable("gold"), 99.0);

    REQUIRE(session.restoreSnapshotBinary(bytes.value()).ok());
    CHECK_EQ(state.getVariable("gold"), 25.0);
    CHECK_EQ(actor->getCurrent("hp"), 70.0);
    actor->release();
}

TEST_CASE("rpg.saveSessionProtobuf.skillMigrationRewritesEveryPartyMember") {
    using namespace eve::rpg;
    QuestRegistry::clear();
    SkillRegistry::clear();
    SkillDefinition oldSkill;
    oldSkill.id = "skill.old";
    SkillRegistry::registerSkill(oldSkill);
    GameState state;
    Tracker   tracker;
    auto     *hero      = RPGActor::createActor();
    auto     *companion = RPGActor::createActor();
    REQUIRE(hero != nullptr);
    REQUIRE(companion != nullptr);
    hero->learnSkill("skill.old");
    companion->learnSkill("skill.old");
    Party party;
    REQUIRE(party.addMember("hero", hero).ok());
    REQUIRE(party.addMember("companion", companion).ok());
    eve::inventory::Bag          bag(2);
    eve::inventory::EquipmentSet equipment;
    RPGSaveSession               oldSession;
    oldSession.bindParty(state, tracker, party, bag, equipment);
    REQUIRE(oldSession.setContentVersion("content.v1").ok());
    auto bytes = oldSession.snapshotBinary();
    REQUIRE(bytes.ok());

    SkillRegistry::clear();
    SkillDefinition newSkill;
    newSkill.id = "skill.new";
    SkillRegistry::registerSkill(newSkill);
    RPGSaveSession migrated;
    migrated.bindParty(state, tracker, party, bag, equipment);
    REQUIRE(migrated.setContentVersion("content.v2").ok());
    REQUIRE(migrated.addIdRenameMigration("content.v1", RPGSaveIdDomain::Skill, "skill.old", "skill.new").ok());
    REQUIRE(migrated.restoreSnapshotBinary(bytes.value()).ok());
    CHECK(hero->knowsSkill("skill.new"));
    CHECK(companion->knowsSkill("skill.new"));
    hero->release();
    companion->release();
    SkillRegistry::clear();
}
