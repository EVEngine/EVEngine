#include <filesystem>
#include <fstream>
#include <simplesquirrel/simplesquirrel.hpp>
#include <sstream>
#include "card/Card.h"
#include "common/ECS.h"
#include "common/Module.h"
#include "common/Runtime.h"
#include "economy/EconomySystem.h"
#include "rts/RTS.h"
#include "transaction/AtomicBatch.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace {
std::string readScenario(const std::string& name) {
    const auto    root = std::filesystem::path(__FILE__).parent_path().parent_path();
    std::ifstream stream(root / "examples" / "rts-cardfront" / name, std::ios::binary);
    REQUIRE(stream.is_open());
    std::ostringstream content;
    content << stream.rdbuf();
    return content.str();
}
}  // namespace

TEST_CASE("card.genericBatchComposesWithEconomyWithoutRts") {
    eve::economy::EconomySystem::clear();
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    ssq::VM          vm(8192, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource("transaction <- eve.Transaction();"));
    vm.run(vm.compileSource(R"(
        local money=eve.Economy(), cards=eve.Card();
        assert(!("RTSCard" in eve));
        money.credit(71,"mana",20); money.credit(71,"ore",10);
        assert(cards.registerCardsFromJson("[{\"id\":\"trade\",\"cost\":3}]")==1);
        local card=cards.newCard("trade"), hand=cards.newHand(cards.newConfig()); hand.addCard(card);
        local batch=transaction.newAtomicBatch();
        assert(cards.stagePlay(batch,card).ok);
        assert(money.stagePayment(batch,71,"{\"ore\":2}").ok);
        assert(money.stagePayment(batch,71,"{\"ore\":1}").ok);
        assert(batch.commit().ok);
        assert(hand.count()==0 && card.getState()=="played");
        assert(money.get(71,"mana")==17 && money.get(71,"ore")==7);
        assert(!batch.commit().ok);
    )"));
}

TEST_CASE("card.genericBatchDetectsLedgerResetAndStaleCard") {
    eve::economy::EconomySystem::clear();
    eve::economy::EconomySystem::credit(72, "mana", 20);
    auto                          account = eve::economy::EconomySystem::resourceAccount(72);
    eve::transaction::AtomicBatch payment;
    REQUIRE(payment.bindAccount(account).ok());
    auto cost = eve::resource::CostSpec::single("mana", 3);
    REQUIRE(static_cast<bool>(cost));
    REQUIRE(payment.add({}, std::move(cost).takeValue()).ok());
    eve::economy::EconomySystem::clear();
    CHECK(!payment.commit());
    CHECK_EQ(eve::economy::EconomySystem::get(72, "mana"), 0);

    eve::card::Card cards;
    REQUIRE_EQ(cards.registerCardsFromJson(R"([{"id":"stale","cost":0}])"), 1);
    auto* card = cards.newCard("stale");
    auto* hand = cards.newHand(cards.newConfig());
    hand->addCard(card);
    eve::transaction::AtomicBatch play;
    REQUIRE(cards.stagePlay(play, *card).ok());
    ecs::DestroyEntity(card);
    CHECK(!play.commit());
}

TEST_CASE("card.genericBatchRetainsScriptOwnersAndReleasesPendingStagesAtVmShutdown") {
    eve::economy::EconomySystem::clear();
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    ssq::VM          vm(8192, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        transaction <- eve.Transaction();
        local cards=eve.Card(), money=eve.Economy();
        assert(cards.registerCardsFromJson("[{\"id\":\"retain\",\"cost\":2}]")==1);
        local card=cards.newCard("retain"), hand=cards.newHand(cards.newConfig()); hand.addCard(card);
        money.credit(73,"mana",20); money.credit(73,"ore",10);
        local batch=transaction.newAtomicBatch();
        assert(cards.stagePlay(batch,card).ok);
        assert(money.stagePayment(batch,73,"{\"ore\":1}").ok);
        cards=null; card=null; hand=null; money=null; collectgarbage();
        assert(batch.commit().ok);
        assert(eve.Economy().get(73,"mana")==18);
        local owner=eve.Card(); owner.registerCardsFromJson("[{\"id\":\"pending\",\"cost\":0}]");
        local pending=owner.newCard("pending"), pendingHand=owner.newHand(owner.newConfig()); pendingHand.addCard(pending);
        pendingBatch <- transaction.newAtomicBatch();
        assert(owner.stagePlay(pendingBatch,pending).ok);
        // Deliberately leave the owning batch uncommitted until VM destruction.
    )"));
}

TEST_CASE("card.genericBatchCompensatesAndProtectsMergedCosts") {
    eve::economy::EconomySystem::clear();
    eve::economy::EconomySystem::credit(74, "mana", 20);
    eve::card::Card cards;
    REQUIRE_EQ(cards.registerCardsFromJson(R"([{"id":"atomic","cost":3}])"), 1);
    auto* card = cards.newCard("atomic");
    auto* hand = cards.newHand(cards.newConfig());
    hand->addCard(card);
    auto                          account = eve::economy::EconomySystem::resourceAccount(74);
    eve::transaction::AtomicBatch protectedPlay;
    REQUIRE(cards.stagePlay(protectedPlay, *card).ok());
    REQUIRE(protectedPlay.bindAccount(account).ok());
    auto floor = eve::resource::ResourceCost::create("mana", 18);
    REQUIRE(floor.ok());
    REQUIRE(protectedPlay.protect(std::move(floor).takeValue()).ok());
    CHECK(!protectedPlay.commit());
    CHECK_EQ(hand->count(), 1);
    CHECK_EQ(eve::economy::EconomySystem::get(74, "mana"), 20);

    class LateFailure final : public eve::transaction::ITransactionParticipant {
        eve::Result<void> prepare(const eve::transaction::TransactionContext&) override {
            return eve::Result<void>::success();
        }
        eve::Result<void> commit(const eve::transaction::TransactionContext&) override {
            return eve::Result<void>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Failed, "injected later failure", "test"));
        }
        eve::Result<void> rollback(const eve::transaction::TransactionContext&) override {
            return eve::Result<void>::success();
        }
        eve::Result<void> compensate(const eve::transaction::TransactionContext&) override {
            return eve::Result<void>::success();
        }
    };
    eve::transaction::AtomicBatch failedPlay;
    REQUIRE(cards.stagePlay(failedPlay, *card).ok());
    REQUIRE(failedPlay.bindAccount(account).ok());
    std::vector<std::unique_ptr<eve::transaction::ITransactionParticipant>> effects;
    effects.push_back(std::make_unique<LateFailure>());
    REQUIRE(failedPlay.add(std::move(effects)).ok());
    CHECK(!failedPlay.commit());
    CHECK_EQ(hand->count(), 1);
    CHECK(card->state()->phase == eve::card::CardState::Hand);
    CHECK_EQ(eve::economy::EconomySystem::get(74, "mana"), 20);
}

TEST_CASE("card.genericBatchRejectsAdvancedRtsContextBeforePayment") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    ssq::VM          vm(8192, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        local sim=eve.RTS(), cards=eve.Card(), tx=eve.Transaction();
        local faction="00000000-0000-7000-8000-00000000ad01";
        assert(sim.configureScriptWorld(4,4,1.0,0.0,0.0).ok);
        assert(sim.newFaction(faction).ok && sim.addScriptResource(faction,"ore",10).ok);
        cards.registerCardsFromJson("[{\"id\":\"guarded\",\"cost\":0}]");
        local card=cards.newCard("guarded"), hand=cards.newHand(cards.newConfig()); hand.addCard(card);
        local batch=tx.newAtomicBatch();
        assert(cards.stagePlay(batch,card).ok);
        assert(sim.stagePayment(batch,faction,"{\"ore\":2}").ok);
        assert(sim.stepScript(0.033333).ok);
        assert(!batch.commit().ok);
        assert(sim.scriptResource(faction,"ore").value==10 && hand.count()==1);
        assert(card.getState()=="hand");
    )"));
}

TEST_CASE("card.rtsScriptScenarioCollectsBuildsProducesAndPlaysAtomicSkills") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    ssq::VM          vm(8192, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource("transaction <- eve.Transaction();"));
    vm.set("contentJson", readScenario("data/content.json"));
    vm.set("cardsJson", readScenario("data/cards.json"));
    vm.run(vm.compileSource(R"(
        eve_init <- null; eve_update <- null; eve_render <- null; eve_reload <- null;
        gfx <- { setBackgroundColor=function(r,g,b,a){} };
        fs <- { readText=function(path){ return path=="data/content.json"?contentJson:cardsJson; } };
    )"));
    const auto scenario = readScenario("main.nut");
    vm.run(vm.compileSource(scenario.c_str()));
    vm.run(vm.compileSource(R"(
        initPresentation=function() {}; eve_init();
        local untouchedMana=mission.mana;
        playNamed("grenade",23.5,8.0); // No caster: no payment/card loss.
        assert(hand.count()==4 && mission.mana==untouchedMana && mission.skillPlays==0);
        for(local i=0;i<1500 && mission.gathered<100;++i) stepSimulation(1.0/30.0);
        if(mission.gathered<100) throw "collection stalled: income="+mission.gathered+" balance="+mission.minerals+" x="+frame.units[0].x+" y="+frame.units[0].y+" cargo="+frame.units[0].cargo+" order="+frame.units[0].order+" workers="+frame.resourceNodes[0].workers;
        assert(mission.gathered>=100 && mission.minerals>=100);
        buildBarracks();
        for(local i=0;i<600 && !mission.built;++i) stepSimulation(1.0/30.0);
        assert(mission.built);
        produceMarine();
        for(local i=0;i<100;++i) stepSimulation(1.0/30.0);
        assert(mission.produced==1);
        for(local i=0;i<600 && mission.minerals<15;++i) stepSimulation(1.0/30.0);
        selectArmy();
        local manaBefore=mission.mana, mineralsBefore=mission.minerals;
        local card=hand.getCard(0);
        playNamed("grenade",29.0,17.0); // Range rejection preserves hand/account/cooldown.
        assert(card.getState()=="hand" && mission.mana==manaBefore && mission.minerals==mineralsBefore);
        local caster=unitById(selected[0]);
        playNamed("grenade",caster.x,caster.y);
        assert(card.getState()=="played" && mission.skillPlays==1);
        assert(mission.mana==manaBefore-10 && mission.minerals==mineralsBefore-15);
        stepSimulation(1.0/30.0);
        for(local i=0;i<900 && mission.minerals<40;++i) stepSimulation(1.0/30.0);
        playNamed("reinforcement",0.0,0.0);
        assert(mission.skillPlays==2);
        for(local i=0;i<100;++i) stepSimulation(1.0/30.0);
        assert(mission.produced==2);
        assault(); updateProjection();
        assert(frame.units[frame.units.len()-1].stance=="aggressive");
        for(local i=0;i<1200 && mission.skillPlays==2;++i) {
            playNamed("grenade",23.5,8.0);
            stepSimulation(1.0/30.0);
        }
        assert(mission.skillPlays==3);
        for(local i=0;i<3000 && mission.outcome=="running";++i) stepSimulation(1.0/30.0);
        if(mission.outcome!="victory" || mission.enemyHealth!=0.0) throw "battle stalled: outcome="+mission.outcome+" hp="+mission.enemyHealth+" marines="+mission.produced+" skills="+mission.skillPlays+" message="+message;
        resetGame();
        for(local i=0;i<10;++i) stepSimulation(1.0/30.0);
        assert(mission.tick==10 && mission.produced==0 && hand.count()==4);
        result <- "ok";
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
}

TEST_CASE("card.rtsScriptBindingsRejectNullAndMalformedCostsWithoutMutation") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    ssq::VM          vm(8192, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource("transaction <- eve.Transaction();"));
    vm.run(vm.compileSource(R"(
        local sim=eve.RTS(), cards=eve.Card();
        assert(!("RTSCard" in eve));
        assert(sim.configureScriptWorld(4,4,1.0,0.0,0.0).ok);
        local faction="00000000-0000-7000-8000-00000000ac01";
        assert(sim.newFaction(faction).ok && sim.addScriptResource(faction,"ore",10).ok);
        assert(cards.registerCardsFromJson("[{\"id\":\"free\",\"cost\":0}]")==1);
        local hand=cards.newHand(cards.newConfig()), card=cards.newCard("free"); hand.addCard(card);
        local pay=function(json) {
            local batch=transaction.newAtomicBatch();
            assert(cards.stagePlay(batch,card).ok);
            local staged=sim.stagePayment(batch,faction,json);
            if(!staged.ok) { assert(!batch.commit().ok); return staged; }
            return batch.commit();
        };
        local nullRejected=false; try { sim.stagePayment(null,faction,"{\"ore\":1}"); } catch(problem) { nullRejected=true; } assert(nullRejected);
        assert(!pay("{\"ore\":-1}").ok);
        assert(!pay("{}").ok);
        assert(sim.scriptResource(faction,"ore").value==10 && hand.count()==1);
        assert(pay("{\"ore\":3}").ok);
        assert(sim.scriptResource(faction,"ore").value==7 && hand.count()==0 && card.getState()=="played");
        result <- "ok";
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
}

TEST_CASE("card.preparedRtsAbilityCompensatesHandChannelCooldownAndCard") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    rts;
    REQUIRE(rts.loadScriptContent(readScenario("data/content.json")).ok());
    REQUIRE(rts.configureScriptWorld(8, 8, 1).ok());
    const auto subject = [](const char* text) {
        return eve::SubjectRef::fromPersistentId(*eve::PersistentId::parse(text));
    };
    auto* faction = rts.newFaction(subject("00000000-0000-7000-8000-00000000ad01")).value();
    auto* unit    = rts.newFactionUnit(*faction, subject("00000000-0000-7000-8000-00000000ad02"),
                                       *eve::LogicalId::parse("unit:marine"))
                     .value();
    eve::card::Card cards;
    REQUIRE_EQ(cards.registerCardsFromJson(R"([{"id":"repair","cost":0}])"), 1);
    auto* hand = cards.newHand(cards.newConfig());
    auto* card = cards.newCard("repair");
    hand->addCard(card);
    auto stage = rts.prepareScriptAbility(*unit, "repair", {}, {});
    REQUIRE(stage.ok());
    auto handStage = cards.prepareHandConsumption(*card);
    REQUIRE(handStage.ok());
    struct Failure final : eve::transaction::ITransactionParticipant {
        std::string_view  name() const noexcept override { return "injected-failure"; }
        eve::Result<void> prepare(const eve::transaction::TransactionContext&) override {
            return eve::Result<void>::success();
        }
        eve::Result<void> commit(const eve::transaction::TransactionContext&) override {
            return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::Failed, "injected", "test"));
        }
        eve::Result<void> rollback(const eve::transaction::TransactionContext&) override {
            return eve::Result<void>::success();
        }
        eve::Result<void> compensate(const eve::transaction::TransactionContext&) override {
            return eve::Result<void>::success();
        }
    } failure;
    eve::card::CardPlayComposition composition;
    composition.effect            = stage.value().effect.get();
    composition.additionalEffects = {handStage.value().get(), &failure};
    auto account                  = rts.scriptAccount(*faction);
    REQUIRE(account.ok());
    auto played = cards.play(*card, account.value().get(), std::move(composition));
    CHECK(!played.ok());
    CHECK_EQ(hand->count(), 1);
    CHECK(!unit->abilities()->channel);
    CHECK(unit->abilities()->cooldowns.empty());
    CHECK_EQ(static_cast<int>(card->state()->phase), static_cast<int>(eve::card::CardState::Hand));
}

TEST_CASE("card.rtsScriptGatheringUsesConfiguredResourceAndDropoff") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    ssq::VM          vm(8192, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource("transaction <- eve.Transaction();"));
    vm.run(vm.compileSource(R"(
        local sim=eve.RTS();
        assert(sim.loadScriptContent("{\"units\":[{\"id\":\"miner\",\"role\":\"worker\",\"gatherResource\":\"ore\",\"cargoCapacity\":3,\"gatherRate\":10,\"speed\":4}],\"buildings\":[{\"id\":\"deposit\",\"dropoff\":true,\"dropoffResources\":[\"ore\"]}]}").ok);
        assert(sim.configureScriptWorld(8,8,1.0,0.0,0.0).ok);
        local faction="00000000-0000-7000-8000-00000000ae01";
        assert(sim.newFaction(faction).ok);
        local deposit=sim.newBuilding("00000000-0000-7000-8000-00000000ae02","building:deposit",faction,1.0,2.0);
        local worker=sim.newUnit("00000000-0000-7000-8000-00000000ae03","unit:miner",faction,2.0,2.0);
        local node=sim.newResourceNode("00000000-0000-7000-8000-00000000ae04","ore",100.0,3.0,2.0,2);
        assert(deposit.ok && worker.ok && node.ok && sim.assignWorker(worker.value,node.value,deposit.value).ok);
        for(local i=0;i<200;++i) assert(sim.stepScript(1.0/30.0).ok);
        assert(sim.scriptResource(faction,"ore").value>0 && sim.scriptResource(faction,"minerals").value==0);
        local economy=sim.inspectScriptEconomy(faction); assert(economy.ok && economy.value.resources[0].income>0);
        result <- "ok";
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
}
