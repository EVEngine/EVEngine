#include "rpg/RPG.h"
#include "common/SquirrelBinding.h"
#include "rpg/Battle.h"
#include "rpg/BattleVictory.h"
#include "rpg/BattleSystem.h"
#include "rpg/BattleTactics.h"
#include "rpg/Class.h"
#include "rpg/Effect.h"
#include "rpg/EquipmentSystem.h"
#include "rpg/EncounterCatalogue.h"
#include "rpg/GameState.h"
#include "rpg/LevelSystem.h"
#include "rpg/Party.h"
#include "rpg/LootSystem.h"
#include "rpg/Quest.h"
#include "rpg/QuestReward.h"
#include "rpg/RPGSaveSession.h"
#include "rpg/Skill.h"
#include "rpg/StatusSystem.h"
#include "rpg/StoryEvent.h"
#include "rpg/SkillSystem.h"
#include "rpg/Settlement.h"
#include "rpg/ShopCatalogue.h"
#include "rpg/ShopTransaction.h"
#include "rpg/Tracker.h"
#include "rpg/Trait.h"
#include "rpg/VitalsSystem.h"
#include "rpg/WorldState.h"
#include "rpg/WorldInteraction.h"

#include <random>

#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::rpg {

Module_IMPL(RPG, new RPG());

Party *RPG::newParty() { return new Party(); }

RPGActor *RPG::newActor() { return RPGActor::createActor(); }

int RPG::registerEffectsFromJson(const std::string &json) {
    return EffectRegistry::loadFromJson(json, nullptr);
}

void RPG::clearEffectDefinitions() { EffectRegistry::clear(); }

int RPG::getEffectDefinitionCount() { return EffectRegistry::count(); }

int RPG::registerSkillsFromJson(const std::string &json) {
    return SkillRegistry::loadFromJson(json, nullptr);
}

void RPG::clearSkillDefinitions() { SkillRegistry::clear(); }

int RPG::getSkillDefinitionCount() { return SkillRegistry::count(); }

int RPG::registerQuestsFromJson(const std::string &json) {
    return QuestRegistry::loadFromJson(json, nullptr);
}

eve::Result<int> RPG::replaceQuestsFromJson(const std::string &json) {
    return QuestRegistry::replaceFromJsonStrict(json);
}

void RPG::clearQuestDefinitions() { QuestRegistry::clear(); }

int RPG::getQuestDefinitionCount() { return QuestRegistry::count(); }

bool RPG::hasQuestDefinition(const std::string &id) const { return QuestRegistry::contains(id); }

eve::Result<int> RPG::claimQuestRewards(Tracker *tracker, GameState *gameState,
                                        inventory::Bag *bag, const std::string &questId) {
    if (!tracker || !gameState || !bag)
        return eve::Result<int>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument,
            "claimQuestRewards requires tracker, gameState, and bag", "participants",
            {}, "rpg.claim-quest-rewards"));
    return QuestReward::claim(*tracker, *gameState, *bag, questId);
}

eve::Result<int> RPG::collectWorldLoot(Tracker *tracker, GameState *gameState,
                                       inventory::Bag *bag, const WorldLootRequest &request) {
    if (!tracker || !gameState || !bag)
        return eve::Result<int>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument,
            "collectWorldLoot requires tracker, gameState, and bag", "participants", {},
            "rpg.collect-world-loot"));
    return WorldInteraction::collectLoot(*gameState, *tracker, *bag, request);
}

eve::Result<BattleVictoryReceipt>
RPG::settleBattleVictory(RPGActor *actor, Tracker *tracker, GameState *gameState,
                         const BattleVictoryRequest &request) {
    if (!actor || !tracker || !gameState)
        return eve::Result<BattleVictoryReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument,
            "settleBattleVictory requires actor, tracker, and gameState", "participants", {},
            "rpg.settle-battle-victory"));
    return BattleVictory::settle(*actor, *gameState, *tracker, request);
}

eve::Result<int> RPG::buyFromShop(GameState *gameState, inventory::Bag *bag,
                                  const std::string &currencyId, const std::string &itemId,
                                  int quantity, double unitPrice) {
    if (!gameState || !bag)
        return eve::Result<int>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "buyFromShop requires gameState and bag",
            "participants", {}, "rpg.buy-from-shop"));
    return ShopTransaction::buy(*gameState, *bag, currencyId, itemId, quantity, unitPrice);
}

eve::Result<int> RPG::sellToShop(GameState *gameState, inventory::Bag *bag,
                                 const std::string &currencyId, const std::string &itemId,
                                 int quantity, double unitPrice) {
    if (!gameState || !bag)
        return eve::Result<int>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "sellToShop requires gameState and bag",
            "participants", {}, "rpg.sell-to-shop"));
    return ShopTransaction::sell(*gameState, *bag, currencyId, itemId, quantity, unitPrice);
}

eve::Result<int> RPG::replaceShopOffersFromJson(const std::string &json) {
    return ShopCatalogue::replaceFromJsonStrict(json);
}

int RPG::getShopOfferCount() const { return ShopCatalogue::count(); }

void RPG::clearShopOffers() { ShopCatalogue::clear(); }

bool RPG::hasShopOffer(const std::string &offerId) const {
    return ShopCatalogue::find(offerId) != nullptr;
}

std::string RPG::getShopOfferId(int index) const {
    const auto *offer = ShopCatalogue::at(index);
    return offer ? offer->id : std::string{};
}

std::string RPG::getShopOfferItemId(int index) const {
    const auto *offer = ShopCatalogue::at(index);
    return offer ? offer->itemId : std::string{};
}

std::string RPG::getShopOfferName(int index) const {
    const auto *offer = ShopCatalogue::at(index);
    return offer ? offer->displayName : std::string{};
}

std::string RPG::getShopOfferDescription(int index) const {
    const auto *offer = ShopCatalogue::at(index);
    return offer ? offer->description : std::string{};
}

int RPG::getShopOfferBuyPrice(int index) const {
    const auto *offer = ShopCatalogue::at(index);
    return offer ? offer->buyPrice : 0;
}

int RPG::getShopOfferSellPrice(int index) const {
    const auto *offer = ShopCatalogue::at(index);
    return offer ? offer->sellPrice : 0;
}

eve::Result<int> RPG::buyShopOffer(GameState *gameState, inventory::Bag *bag,
                                  const std::string &currencyId,
                                  const std::string &offerId, int quantity) {
    if (!gameState || !bag)
        return eve::Result<int>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "buyShopOffer requires gameState and bag",
            "participants", {}, "rpg.buy-shop-offer"));
    return ShopTransaction::buyOffer(*gameState, *bag, currencyId, offerId, quantity);
}

eve::Result<int> RPG::sellShopOffer(GameState *gameState, inventory::Bag *bag,
                                   const std::string &currencyId,
                                   const std::string &offerId, int quantity) {
    if (!gameState || !bag)
        return eve::Result<int>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "sellShopOffer requires gameState and bag",
            "participants", {}, "rpg.sell-shop-offer"));
    return ShopTransaction::sellOffer(*gameState, *bag, currencyId, offerId, quantity);
}

eve::Result<int> RPG::replaceEncountersFromJson(const std::string &json) {
    return EncounterCatalogue::replaceFromJsonStrict(json);
}
void RPG::clearEncounters() { EncounterCatalogue::clear(); }
bool RPG::hasEncounter(const std::string &encounterId) const {
    return EncounterCatalogue::find(encounterId) != nullptr;
}
int RPG::getEncounterMemberCount(const std::string &encounterId) const {
    return EncounterCatalogue::memberCount(encounterId);
}
RPGActor *RPG::newEncounterActor(const std::string &encounterId) {
    return EncounterCatalogue::createActor(encounterId);
}
RPGActor *RPG::newEncounterMemberActor(const std::string &encounterId, int memberIndex) {
    return EncounterCatalogue::createMemberActor(encounterId, memberIndex);
}
std::string RPG::getEncounterDisplayName(const std::string &encounterId) const {
    const auto *definition = EncounterCatalogue::find(encounterId);
    return definition ? definition->displayName : std::string{};
}
double RPG::getEncounterMaxHp(const std::string &encounterId) const {
    const auto *definition = EncounterCatalogue::find(encounterId);
    return definition && !definition->members.empty() ? definition->members.front().maxHp : 0.0;
}
std::string RPG::getEncounterMemberDisplayName(const std::string &encounterId,
                                               int memberIndex) const {
    const auto *definition = EncounterCatalogue::find(encounterId);
    if (!definition || memberIndex < 0 ||
        static_cast<std::size_t>(memberIndex) >= definition->members.size())
        return {};
    return definition->members[static_cast<std::size_t>(memberIndex)].displayName;
}
double RPG::getEncounterMemberMaxHp(const std::string &encounterId, int memberIndex) const {
    const auto *definition = EncounterCatalogue::find(encounterId);
    if (!definition || memberIndex < 0 ||
        static_cast<std::size_t>(memberIndex) >= definition->members.size())
        return 0.0;
    return definition->members[static_cast<std::size_t>(memberIndex)].maxHp;
}

eve::Result<int> RPG::replaceBattleTacticsFromJson(const std::string &json) {
    return BattleTacticsCatalogue::replaceFromJsonStrict(json);
}
void RPG::clearBattleTactics() { BattleTacticsCatalogue::clear(); }
eve::Result<std::string> RPG::queueBattleTactic(Battle *battle, RPGActor *actor,
                                                 const std::string &tacticsId) {
    return BattleTacticsCatalogue::queueAction(battle, actor, tacticsId);
}
eve::Result<int> RPG::replaceStoryEventsFromJson(const std::string &json) {
    return StoryEventCatalogue::replaceFromJsonStrict(json);
}
void RPG::clearStoryEvents() { StoryEventCatalogue::clear(); }
int RPG::getStoryEventCount() const { return StoryEventCatalogue::count(); }
bool RPG::hasStoryEvent(const std::string &eventId) const {
    return StoryEventCatalogue::contains(eventId);
}
StoryEventSession *RPG::newStoryEventSession() { return new StoryEventSession(); }
eve::Result<BattleVictoryReceipt>
RPG::settleEncounterVictory(RPGActor *actor, Tracker *tracker, GameState *gameState,
                            const std::string &encounterId, const std::string &mapId,
                            const std::string &objectId) {
    if (!actor || !tracker || !gameState)
        return eve::Result<BattleVictoryReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument,
            "settleEncounterVictory requires actor, tracker, and gameState", "participants"));
    const auto *definition = EncounterCatalogue::find(encounterId);
    if (!definition)
        return eve::Result<BattleVictoryReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "unknown encounter definition", "encounterId"));
    BattleVictoryRequest request;
    request.mapId = mapId;
    request.objectId = objectId;
    request.requiredQuestId = definition->requiredQuestId;
    request.xpAmount = definition->xpReward;
    request.xpGrowth = definition->xpGrowth;
    request.attributeId = "gold";
    request.attributeAmount = definition->goldReward;
    request.notifyTopic = definition->notifyTopic;
    request.notifyTarget = definition->notifyTarget;
    request.notifyAmount = definition->notifyAmount;
    request.defeatCounterId = definition->defeatCounterId;
    request.defeatCounterAmount = definition->defeatCounterAmount;
    request.levelPointAttributeId = definition->levelPointAttributeId;
    request.pointsPerLevel = definition->pointsPerLevel;
    return BattleVictory::settle(*actor, *gameState, *tracker, request);
}

Tracker *RPG::newTracker() { return new Tracker(); }

int RPG::getLevelUpEventCount() const { return int(levelUps_.size()); }

RPGActor *RPG::getLevelUpEventActor(int index) const {
    if (index < 0 || size_t(index) >= levelUps_.size()) return nullptr;
    return levelUps_[size_t(index)].actor;
}

int RPG::getLevelUpEventPreviousLevel(int index) const {
    if (index < 0 || size_t(index) >= levelUps_.size()) return 0;
    return levelUps_[size_t(index)].previousLevel;
}

int RPG::getLevelUpEventNewLevel(int index) const {
    if (index < 0 || size_t(index) >= levelUps_.size()) return 0;
    return levelUps_[size_t(index)].newLevel;
}

int RPG::getVitalsEventCount() const { return int(vitals_.size()); }

RPGActor *RPG::getVitalsEventActor(int index) const {
    if (index < 0 || size_t(index) >= vitals_.size()) return nullptr;
    return vitals_[size_t(index)].actor;
}

std::string RPG::getVitalsEventResource(int index) const {
    if (index < 0 || size_t(index) >= vitals_.size()) return {};
    return vitals_[size_t(index)].resource;
}

std::string RPG::getVitalsEventAction(int index) const {
    if (index < 0 || size_t(index) >= vitals_.size()) return {};
    return vitals_[size_t(index)].action;
}

double RPG::getVitalsEventAmount(int index) const {
    if (index < 0 || size_t(index) >= vitals_.size()) return 0.0;
    return vitals_[size_t(index)].amount;
}

std::string RPG::getVitalsEventSource(int index) const {
    if (index < 0 || size_t(index) >= vitals_.size()) return {};
    return vitals_[size_t(index)].source;
}

double RPG::getVitalsEventCurrent(int index) const {
    if (index < 0 || size_t(index) >= vitals_.size()) return 0.0;
    return vitals_[size_t(index)].current;
}

double RPG::getVitalsEventMax(int index) const {
    if (index < 0 || size_t(index) >= vitals_.size()) return 0.0;
    return vitals_[size_t(index)].max;
}

int RPG::registerItemStatsFromJson(const std::string &itemId, const std::string &json) {
    return EquipmentSystem::registerItemStatsFromJson(itemId, json);
}

void RPG::clearItemStats(const std::string &itemId) { EquipmentSystem::clearItemStats(itemId); }

void RPG::clearAllItemStats() { EquipmentSystem::clearAll(); }

int RPG::getItemStatCount() { return EquipmentSystem::statItemCount(); }

int RPG::syncEquipModifiers(RPGActor *actor, inventory::EquipmentSet *equip) {
    return EquipmentSystem::syncEquipModifiers(actor, equip);
}

int RPG::registerLootTablesFromJson(const std::string &json) {
    return LootSystem::registerTablesFromJson(json);
}

void RPG::clearLootTables() { LootSystem::clear(); }

int RPG::getLootTableCount() { return LootSystem::count(); }

int RPG::rollLoot(const std::string &tableId, inventory::Bag *bag, int seed) {
    std::mt19937 rng(static_cast<unsigned>(seed));
    return LootSystem::roll(tableId, bag, rng);
}

int RPG::registerTraitsFromJson(const std::string &json) {
    return TraitRegistry::loadFromJson(json, nullptr);
}

void RPG::clearTraitDefinitions() { TraitRegistry::clear(); }

int RPG::getTraitDefinitionCount() { return TraitRegistry::count(); }

int RPG::registerSkillDamage(const std::string &skillId, const std::string &damageType,
                             const std::string &formula, const std::string &element,
                             double critChance, int hitChance) {
    SkillDamageSpec spec;
    spec.damageType = damageType;
    spec.formula = formula;
    spec.element = element;
    spec.critChance = critChance;
    spec.hitChance = hitChance;
    auto registered = BattleSystem::registerSkillDamageChecked(skillId, spec);
    return registered ? 1 : 0;
}

void RPG::clearSkillDamage() { BattleSystem::clearSkillDamage(); }

Battle *RPG::newBattle() { return new Battle(); }

GameState *RPG::newGameState() { return new GameState(); }

WorldState *RPG::newWorldState(GameState *gameState) {
    return gameState ? new WorldState(*gameState) : nullptr;
}

GameState *RPG::globalGameState() { return &GameState::global(); }

int RPG::registerClassesFromJson(const std::string &json) {
    return ClassRegistry::loadFromJson(json, nullptr);
}

void RPG::clearClassDefinitions() { ClassRegistry::clear(); }

int RPG::getClassDefinitionCount() { return ClassRegistry::count(); }

void RPG::update(float dt) {
    StatusSystem::update(static_cast<double>(dt)).ignore("RPG script update tick");
    SkillSystem::update(dt);

    ticks_.clear();
    StatusSystem::pollTicks(ticks_);
    changes_.clear();
    StatusSystem::pollChanges(changes_);
    casts_.clear();
    SkillSystem::pollCastEvents(casts_);
    levelUps_.clear();
    LevelSystem::pollLevelUps(levelUps_);
    vitals_.clear();
    VitalsSystem::pollEvents(vitals_);
}

int RPG::getTickEventCount() const { return int(ticks_.size()); }

RPGActor *RPG::getTickEventActor(int index) const {
    if (index < 0 || size_t(index) >= ticks_.size()) return nullptr;
    return ticks_[size_t(index)].actor;
}

int RPG::getTickEventInstanceId(int index) const {
    if (index < 0 || size_t(index) >= ticks_.size()) return 0;
    return ticks_[size_t(index)].instanceId;
}

std::string RPG::getTickEventEffectId(int index) const {
    if (index < 0 || size_t(index) >= ticks_.size()) return {};
    return ticks_[size_t(index)].effectId;
}

std::string RPG::getTickEventSource(int index) const {
    if (index < 0 || size_t(index) >= ticks_.size()) return {};
    return ticks_[size_t(index)].source;
}

int RPG::getTickEventStacks(int index) const {
    if (index < 0 || size_t(index) >= ticks_.size()) return 0;
    return ticks_[size_t(index)].stacks;
}

int RPG::getStatusChangeEventCount() const { return int(changes_.size()); }

RPGActor *RPG::getStatusChangeEventActor(int index) const {
    if (index < 0 || size_t(index) >= changes_.size()) return nullptr;
    return changes_[size_t(index)].actor;
}

int RPG::getStatusChangeEventInstanceId(int index) const {
    if (index < 0 || size_t(index) >= changes_.size()) return 0;
    return changes_[size_t(index)].instanceId;
}

std::string RPG::getStatusChangeEventEffectId(int index) const {
    if (index < 0 || size_t(index) >= changes_.size()) return {};
    return changes_[size_t(index)].effectId;
}

std::string RPG::getStatusChangeEventSource(int index) const {
    if (index < 0 || size_t(index) >= changes_.size()) return {};
    return changes_[size_t(index)].source;
}

std::string RPG::getStatusChangeEventAction(int index) const {
    if (index < 0 || size_t(index) >= changes_.size()) return {};
    return changes_[size_t(index)].action;
}

int RPG::getStatusChangeEventStacks(int index) const {
    if (index < 0 || size_t(index) >= changes_.size()) return 0;
    return changes_[size_t(index)].stacks;
}

std::string RPG::getStatusChangeEventReason(int index) const {
    if (index < 0 || size_t(index) >= changes_.size()) return {};
    return changes_[size_t(index)].reason;
}

int RPG::getCastEventCount() const { return int(casts_.size()); }

RPGActor *RPG::getCastEventCaster(int index) const {
    if (index < 0 || size_t(index) >= casts_.size()) return nullptr;
    return casts_[size_t(index)].caster;
}

RPGActor *RPG::getCastEventTarget(int index) const {
    if (index < 0 || size_t(index) >= casts_.size()) return nullptr;
    return casts_[size_t(index)].target;
}

std::string RPG::getCastEventSkillId(int index) const {
    if (index < 0 || size_t(index) >= casts_.size()) return {};
    return casts_[size_t(index)].skillId;
}

SettlementContext *RPG::newSettlementContext() { return new SettlementContext(); }

void RPG::runSettlement(const std::string &pipeline, SettlementContext *ctx) {
    if (!ctx) return;
    SettlementPipeline::run(pipeline, *ctx);
}

int RPG::getSettlementStageCount(const std::string &pipeline) {
    return SettlementPipeline::stageCount(pipeline);
}

bool RPG::hasSettlementStage(const std::string &pipeline, const std::string &stage) {
    return SettlementPipeline::hasStage(pipeline, stage);
}

bool RPG::setSettlementStageEnabled(const std::string &pipeline, const std::string &stage,
                                     bool enabled) {
    return SettlementPipeline::setStageEnabled(pipeline, stage, enabled);
}

bool RPG::setSettlementStagePriority(const std::string &pipeline, const std::string &stage,
                                      int priority) {
    return SettlementPipeline::setStagePriority(pipeline, stage, priority);
}

bool RPG::removeSettlementStage(const std::string &pipeline, const std::string &stage) {
    return SettlementPipeline::unregisterStage(pipeline, stage);
}

void RPG::clearSettlementPipeline(const std::string &pipeline) {
    SettlementPipeline::clearPipeline(pipeline);
}

void RPG::expose(ssq::Table &table) {
    const HSQUIRRELVM vm = table.getHandle();
    auto cls = table.addClass(name, RPG::create, false);
    expose(cls);

    auto actor = table.addClass<RPGActor>(
        "RPGActor", std::function<RPGActor *()>([]() -> RPGActor * { return nullptr; }), true);
    // 绑定为 float：SimpleSquirrel 只有在 squirrel 以 SQUSEDOUBLE 编译时才认识
    // C++ double（本项目未开启），否则 double 参数/返回值会被当成 INSTANCE
    // 处理，脚本调用直接抛 "bad cast expected INSTANCE got FLOAT"。
    // 同样的坑见 mouse/Mouse.cpp、timer/Timer.h 的注释。
    actor.addFunc("setBaseAttribute", [](RPGActor *a, const std::string &attribute, float value) {
        if (a) a->setBaseAttribute(attribute, double(value));
    });
    actor.addFunc("getBaseAttribute", [](RPGActor *a, const std::string &attribute) -> float {
        return a ? float(a->getBaseAttribute(attribute)) : 0.f;
    });
    actor.addFunc("modifyBaseAttribute", [](RPGActor *a, const std::string &attribute, float delta) {
        if (a) a->modifyBaseAttribute(attribute, double(delta));
    });
    actor.addFunc("hasAttribute", &RPGActor::hasAttribute);
    actor.addFunc("addAttributeModifier",
                  [](RPGActor *a, const std::string &attribute, const std::string &source,
                     const std::string &op, float value, int priority) -> std::string {
                      return a ? a->addAttributeModifier(attribute, source, op, double(value), priority)
                               : std::string{};
                  });
    actor.addFunc("removeAttributeModifier",
                  [](RPGActor *a, const std::string &attribute, const std::string &modifierId) {
                      return a && a->removeAttributeModifier(attribute, modifierId);
                  });
    actor.addFunc("removeAttributeModifiersBySource", &RPGActor::removeAttributeModifiersBySource);
    actor.addFunc("removeAllAttributeModifiersBySource",
                   &RPGActor::removeAllAttributeModifiersBySource);
    actor.addFunc("getFinalAttribute", [](RPGActor *a, const std::string &attribute) -> float {
        return a ? float(a->getFinalAttribute(attribute)) : 0.f;
    });

    actor.addFunc("applyEffect", &RPGActor::applyEffect);
    actor.addFunc("removeStatus", &RPGActor::removeStatus);
    actor.addFunc("removeStatusByEffect", &RPGActor::removeStatusByEffect);
    actor.addFunc("removeStatusBySource", &RPGActor::removeStatusBySource);
    actor.addFunc("removeStatusByTag", &RPGActor::removeStatusByTag);
    actor.addFunc("hasEffect", &RPGActor::hasEffect);
    actor.addFunc("hasStatusTag", &RPGActor::hasStatusTag);
    actor.addFunc("getStatusCount", &RPGActor::getStatusCount);
    actor.addFunc("getStatusEffectId", &RPGActor::getStatusEffectId);
    actor.addFunc("getStatusStacks", &RPGActor::getStatusStacks);
    actor.addFunc("getStatusRemaining", &RPGActor::getStatusRemaining);
    actor.addFunc("getStatusInstanceId", &RPGActor::getStatusInstanceId);
    actor.addFunc("getStatusSource", &RPGActor::getStatusSource);
    actor.addFunc("getStatusProp", &RPGActor::getStatusProp);
    actor.addFunc("setStatusProp", &RPGActor::setStatusProp);

    actor.addFunc("applyBuff", &RPGActor::applyBuff);
    actor.addFunc("removeBuff", &RPGActor::removeBuff);
    actor.addFunc("removeBuffByEffect", &RPGActor::removeBuffByEffect);
    actor.addFunc("removeBuffBySource", &RPGActor::removeBuffBySource);
    actor.addFunc("removeBuffByTag", &RPGActor::removeBuffByTag);
    actor.addFunc("hasBuff", &RPGActor::hasBuff);
    actor.addFunc("hasBuffTag", &RPGActor::hasBuffTag);
    actor.addFunc("getBuffCount", &RPGActor::getBuffCount);
    actor.addFunc("getBuffEffectId", &RPGActor::getBuffEffectId);
    actor.addFunc("getBuffStacks", &RPGActor::getBuffStacks);
    actor.addFunc("getBuffRemaining", &RPGActor::getBuffRemaining);
    actor.addFunc("getBuffInstanceId", &RPGActor::getBuffInstanceId);
    actor.addFunc("getBuffSource", &RPGActor::getBuffSource);
    actor.addFunc("getBuffProp", &RPGActor::getBuffProp);
    actor.addFunc("setBuffProp", &RPGActor::setBuffProp);

    actor.addFunc("learnSkill", &RPGActor::learnSkill);
    actor.addFunc("knowsSkill", &RPGActor::knowsSkill);
    actor.addFunc("forgetSkill", &RPGActor::forgetSkill);
    actor.addFunc("getSkillCooldown", &RPGActor::getSkillCooldown);
    actor.addFunc("setSkillCooldown", &RPGActor::setSkillCooldown);
    actor.addFunc("canCastSkill", &RPGActor::canCastSkill);
    actor.addFunc("canCastSkillReason", &RPGActor::canCastSkillReason);
    actor.addFunc("beginCastSkill", &RPGActor::beginCastSkill);
    actor.addFunc("cancelCastSkill", &RPGActor::cancelCastSkill);
    actor.addFunc("isCastingSkill", &RPGActor::isCastingSkill);
    actor.addFunc("getCastingSkillId", &RPGActor::getCastingSkillId);
    actor.addFunc("getCastProgress", &RPGActor::getCastProgress);

    // 成长：浮点一律包 float（Squirrel 未开 SQUSEDOUBLE）。
    actor.addFunc("getLevel", &RPGActor::getLevel);
    actor.addFunc("getXp", [](RPGActor *a) -> float { return a ? float(a->getXp()) : 0.f; });
    actor.addFunc("getXpToNext", [](RPGActor *a) -> float { return a ? float(a->getXpToNext()) : 0.f; });
    actor.addFunc("setXpToNext", [](RPGActor *a, float value) { if (a) a->setXpToNext(double(value)); });
    actor.addFunc("restoreProgression", [](RPGActor *a, int level, float xp, float xpToNext) -> int {
        if (!a) return 0;
        auto result = a->restoreProgression(level, double(xp), double(xpToNext));
        return result.ok() ? 1 : 0;
    });
    actor.addFunc("gainXp", [](RPGActor *a, float amount) -> bool { return a && a->gainXp(double(amount)); });

    // 当前资源：同浮点 float 包装。
    actor.addFunc("getCurrent", [](RPGActor *a, const std::string &r) -> float {
        return a ? float(a->getCurrent(r)) : 0.f;
    });
    actor.addFunc("getMax", [](RPGActor *a, const std::string &r) -> float {
        return a ? float(a->getMax(r)) : 0.f;
    });
    actor.addFunc("setCurrent",
                  [](RPGActor *a, const std::string &r, float value) { if (a) a->setCurrent(r, double(value)); });
    actor.addFunc("takeDamage", [](RPGActor *a, const std::string &r, float amount, const std::string &source) -> float {
        return a ? float(a->takeDamage(r, double(amount), source)) : 0.f;
    });
    actor.addFunc("heal", [](RPGActor *a, const std::string &r, float amount) -> float {
        return a ? float(a->heal(r, double(amount))) : 0.f;
    });
    actor.addFunc("revive", [](RPGActor *a, const std::string &r, float amount) {
        if (a) a->revive(r, double(amount));
    });
    actor.addFunc("isDead", [](RPGActor *a, const std::string &r) { return a && a->isDead(r); });
    // 销毁 actor（ECS owned），供脚本在击杀/重开时释放。
    actor.addFunc("release", &RPGActor::release);

    // 特征：浮点查询包 float。
    actor.addFunc("applyTrait", [](RPGActor *a, const std::string &t, const std::string &s) -> int {
        return a ? a->applyTrait(t, s) : 0;
    });
    actor.addFunc("removeTrait", &RPGActor::removeTrait);
    actor.addFunc("removeTraitsBySource", &RPGActor::removeTraitsBySource);
    actor.addFunc("removeTraitsByTrait", &RPGActor::removeTraitsByTrait);
    actor.addFunc("hasTrait", &RPGActor::hasTrait);
    actor.addFunc("getTraitCount", &RPGActor::getTraitCount);
    actor.addFunc("getTraitInstanceIdAt", &RPGActor::getTraitInstanceIdAt);
    actor.addFunc("getTraitIdAt", &RPGActor::getTraitIdAt);
    actor.addFunc("getTraitSourceAt", &RPGActor::getTraitSourceAt);
    actor.addFunc("getParamRate", [](RPGActor *a, const std::string &p) -> float {
        return a ? float(a->getParamRate(p)) : 1.f;
    });
    actor.addFunc("getElementRate", [](RPGActor *a, const std::string &e) -> float {
        return a ? float(a->getElementRate(e)) : 1.f;
    });
    actor.addFunc("getStateRate", [](RPGActor *a, const std::string &s) -> float {
        return a ? float(a->getStateRate(s)) : 1.f;
    });
    actor.addFunc("isStateResist", &RPGActor::isStateResist);
    actor.addFunc("getExParam", [](RPGActor *a, const std::string &e) -> float {
        return a ? float(a->getExParam(e)) : 0.f;
    });
    actor.addFunc("getAttackSpeed", [](RPGActor *a) -> float {
        return a ? float(a->getAttackSpeed()) : 0.f;
    });
    actor.addFunc("getAttackTimesAdd", &RPGActor::getAttackTimesAdd);

    // 职业
    actor.addFunc("setClass", &RPGActor::setClass);
    actor.addFunc("getClassId", &RPGActor::getClassId);
    actor.addFunc("hasClass", &RPGActor::hasClass);
    actor.addFunc("checkLevelSkills", &RPGActor::checkLevelSkills);
    actor.addFunc("getClassLearnCount", &RPGActor::getClassLearnCount);
    actor.addFunc("getClassLearnSkillIdAt", &RPGActor::getClassLearnSkillIdAt);
    actor.addFunc("getClassLearnLevelAt", &RPGActor::getClassLearnLevelAt);
    actor.addFunc("checkpointJson", [](RPGActor *value) -> std::string {
        if (!value) return {};
        auto result = value->checkpointJson();
        return result.ok() ? std::move(result).takeValue() : std::string{};
    });
    actor.addFunc("restoreCheckpointJson", [](RPGActor *value, const std::string &json) -> int {
        if (!value) return 0;
        return value->restoreCheckpointJson(json).ok() ? 1 : 0;
    });

    auto ctx = table.addClass<SettlementContext>(
        "SettlementContext", std::function<SettlementContext *()>([]() { return new SettlementContext(); }),
        true);
    // 同上：SettlementContext::get/set 用 double，脚本侧包一层转 float。
    ctx.addFunc("get", [](SettlementContext *c, const std::string &key, float fallback) -> float {
        return c ? float(c->get(key, double(fallback))) : fallback;
    });
    ctx.addFunc("set", [](SettlementContext *c, const std::string &key, float value) {
        if (c) c->set(key, double(value));
    });
    ctx.addFunc("has", &SettlementContext::has);
    ctx.addFunc("addTag", &SettlementContext::addTag);
    ctx.addFunc("hasTag", &SettlementContext::hasTag);

    auto tracker = table.addClass<Tracker>(
        "Tracker", std::function<Tracker *()>([]() { return new Tracker(); }), true);
    tracker.addFunc("syncAuto", &Tracker::syncAuto);
    tracker.addFunc("activate", &Tracker::activate);
    tracker.addFunc("canActivate", &Tracker::canActivate);
    tracker.addFunc("canActivateReason", &Tracker::canActivateReason);
    tracker.addFunc("notify", &Tracker::notify);
    tracker.addFunc("claim", &Tracker::claim);
    tracker.addFunc("reset", &Tracker::reset);
    tracker.addFunc("abandon", &Tracker::abandon);
    tracker.addFunc("fail", &Tracker::fail);
    tracker.addFunc("pollEvents", &Tracker::pollEvents);
    tracker.addFunc("snapshotJson", [](Tracker *t) -> std::string {
        if (!t) return {};
        auto result = t->snapshotJson();
        return result.ok() ? std::move(result).takeValue() : std::string{};
    });
    tracker.addFunc("restoreSnapshotJson", [](Tracker *t, const std::string &json) -> int {
        if (!t) return 0;
        return t->restoreSnapshotJson(json).ok() ? 1 : 0;
    });
    tracker.addFunc("getCount", &Tracker::getCount);
    tracker.addFunc("getId", &Tracker::getId);
    tracker.addFunc("getState", &Tracker::getState);
    tracker.addFunc("hasTag", &Tracker::hasTag);
    tracker.addFunc("getExtra", &Tracker::getExtra);
    tracker.addFunc("getObjectiveCount", &Tracker::getObjectiveCount);
    tracker.addFunc("getObjectiveId", &Tracker::getObjectiveId);
    tracker.addFunc("getObjectiveCurrent", &Tracker::getObjectiveCurrent);
    tracker.addFunc("getObjectiveCountRequired", &Tracker::getObjectiveCountRequired);
    tracker.addFunc("isObjectiveDone", &Tracker::isObjectiveDone);
    tracker.addFunc("getRewardCount", &Tracker::getRewardCount);
    tracker.addFunc("getRewardType", &Tracker::getRewardType);
    tracker.addFunc("getRewardId", &Tracker::getRewardId);
    tracker.addFunc("getRewardAmount",
                    [](Tracker *t, const std::string &id, int index) -> float {
                        return t ? float(t->getRewardAmount(id, index)) : 0.f;
                    });
    tracker.addFunc("getEventCount", &Tracker::getEventCount);
    tracker.addFunc("getEventEntryId", &Tracker::getEventEntryId);
    tracker.addFunc("getEventObjectiveId", &Tracker::getEventObjectiveId);
    tracker.addFunc("getEventAction", &Tracker::getEventAction);
    tracker.addFunc("getEventTopic", &Tracker::getEventTopic);
    tracker.addFunc("getEventTarget", &Tracker::getEventTarget);
    tracker.addFunc("getEventAmount", &Tracker::getEventAmount);
    tracker.addFunc("getEventReason", &Tracker::getEventReason);

    auto battle = table.addClass<Battle>(
        "Battle", std::function<Battle *()>([]() { return new Battle(); }), true);
    battle.addFunc("addActor", &Battle::addActor);
    battle.addFunc("setAction", &Battle::setAction);
    battle.addFunc("setActionChecked",
                   [vm](Battle *value, RPGActor *actor, const std::string &skillId,
                        RPGActor *target) {
                       if (!value)
                           return eve::script::projectResult(
                               vm, eve::Result<void>::failure(eve::Diagnostic::error(
                                       eve::DiagnosticCode::InvalidArgument,
                                       "Battle receiver must not be null", "battle", {},
                                       "rpg.squirrel")));
                       return eve::script::projectResult(
                           vm, value->setActionChecked(actor, skillId, target));
                   });
    battle.addFunc("setActionByPolicyChecked",
                   [vm](Battle *value, RPGActor *actor, const std::string &skillId,
                        const std::string &policy) {
                       if (!value)
                           return eve::script::projectResult(
                               vm, eve::Result<void>::failure(eve::Diagnostic::error(
                                       eve::DiagnosticCode::InvalidArgument,
                                       "Battle receiver must not be null", "battle", {},
                                       "rpg.squirrel")));
                       BattleTargetPolicy parsed;
                       if (policy == "auto") parsed = BattleTargetPolicy::Auto;
                       else if (policy == "self") parsed = BattleTargetPolicy::Self;
                       else if (policy == "lowestHealthAlly")
                           parsed = BattleTargetPolicy::LowestHealthAlly;
                       else if (policy == "lowestHealthEnemy")
                           parsed = BattleTargetPolicy::LowestHealthEnemy;
                       else
                           return eve::script::projectResult(
                               vm, eve::Result<void>::failure(eve::Diagnostic::error(
                                       eve::DiagnosticCode::InvalidArgument,
                                       "unknown battle target policy", "policy", {},
                                       "rpg.squirrel")));
                       return eve::script::projectResult(
                           vm, value->setActionByPolicyChecked(actor, skillId, parsed));
                   });
    battle.addFunc("autoEnemyActions", &Battle::autoEnemyActions);
    battle.addFunc("startRound", &Battle::startRound);
    battle.addFunc("executeNextAction", &Battle::executeNextAction);
    battle.addFunc("isFinished", &Battle::isFinished);
    battle.addFunc("isVictory", &Battle::isVictory);
    battle.addFunc("isDefeat", &Battle::isDefeat);
    battle.addFunc("setPlayerSide", &Battle::setPlayerSide);
    battle.addFunc("getPlayerSide", &Battle::getPlayerSide);
    battle.addFunc("getWinnerSide", &Battle::getWinnerSide);
    battle.addFunc("getTurn", &Battle::getTurn);
    battle.addFunc("isActorAlive", &Battle::isActorAlive);
    battle.addFunc("getActorCount", &Battle::getActorCount);
    battle.addFunc("getActor", &Battle::getActor);
    battle.addFunc("getSide", &Battle::getSide);
    battle.addFunc("pollEvents", &Battle::pollEvents);
    battle.addFunc("getEventCount", &Battle::getEventCount);
    battle.addFunc("getEventAction", &Battle::getEventAction);
    battle.addFunc("getEventSkillId", &Battle::getEventSkillId);
    battle.addFunc("getEventCaster", &Battle::getEventCaster);
    battle.addFunc("getEventTarget", &Battle::getEventTarget);
    battle.addFunc("getEventAmount", [](Battle *b, int i) -> float { return float(b->getEventAmount(i)); });
    battle.addFunc("getEventCrit", &Battle::getEventCrit);

    auto party = table.addClass<Party>(
        "RPGParty", std::function<Party *()>([]() { return new Party(); }), true);
    party.addFunc("addMember", [vm](Party *value, const std::string &memberId, RPGActor *actor) {
        if (!value)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::InvalidArgument,
                        "RPGParty receiver must not be null", "party", {}, "rpg.squirrel")));
        return eve::script::projectResult(vm, value->addMember(memberId, actor));
    });
    party.addFunc("removeMember", [vm](Party *value, const std::string &memberId) {
        if (!value)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::InvalidArgument,
                        "RPGParty receiver must not be null", "party", {}, "rpg.squirrel")));
        return eve::script::projectResult(vm, value->removeMember(memberId));
    });
    party.addFunc("clear", &Party::clear);
    party.addFunc("count", &Party::count);
    party.addFunc("contains", &Party::contains);
    party.addFunc("hasStaleMembers", &Party::hasStaleMembers);
    party.addFunc("getMemberId", &Party::getMemberId);
    party.addFunc("getMemberActor", &Party::getMemberActor);
    party.addFunc("findMemberActor", &Party::findMemberActor);
    party.addFunc("addToBattle", [vm](Party *value, Battle *battleValue, int side) {
        if (!value)
            return eve::script::projectResult(
                vm, eve::Result<int>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::InvalidArgument,
                        "RPGParty receiver must not be null", "party", {}, "rpg.squirrel")),
                [](int count) { return eve::Value(count); });
        return eve::script::projectResult(vm, value->addToBattle(battleValue, side),
                                          [](int count) { return eve::Value(count); });
    });
    party.addFunc("recoverAtCheckpoint",
                  [vm](Party *value, const std::string &healthResource, float healthRatio,
                       const std::string &secondaryResource, float secondaryRatio) {
        if (!value)
            return eve::script::projectResult(
                vm, eve::Result<int>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::InvalidArgument,
                        "RPGParty receiver must not be null", "party", {}, "rpg.squirrel")),
                [](int count) { return eve::Value(count); });
        return eve::script::projectResult(
            vm, value->recoverAtCheckpoint(healthResource, healthRatio, secondaryResource,
                                           secondaryRatio),
            [](int count) { return eve::Value(count); });
    });

    auto gs = table.addClass<GameState>(
        "GameState", std::function<GameState *()>([]() { return new GameState(); }), true);
    gs.addFunc("setSwitch", &GameState::setSwitch);
    gs.addFunc("switchOn", &GameState::switchOn);
    gs.addFunc("switchOff", &GameState::switchOff);
    gs.addFunc("isSwitchOn", &GameState::isSwitchOn);
    gs.addFunc("setVariable", [](GameState *g, const std::string &n, float v) { if (g) g->setVariable(n, double(v)); });
    gs.addFunc("getVariable", [](GameState *g, const std::string &n) -> float { return g ? float(g->getVariable(n)) : 0.f; });
    gs.addFunc("addVariable", [](GameState *g, const std::string &n, float d) { if (g) g->addVariable(n, double(d)); });
    gs.addFunc("setSelfVariable", [](GameState *g, const std::string &s, const std::string &n, float v) { if (g) g->setSelfVariable(s, n, double(v)); });
    gs.addFunc("getSelfVariable", [](GameState *g, const std::string &s, const std::string &n) -> float { return g ? float(g->getSelfVariable(s, n)) : 0.f; });
    gs.addFunc("hasSelfVariable", &GameState::hasSelfVariable);
    gs.addFunc("clear", &GameState::clear);
    gs.addFunc("snapshotJson", [](GameState *g) -> std::string {
        if (!g) return {};
        auto result = g->snapshotJson();
        return result.ok() ? std::move(result).takeValue() : std::string{};
    });
    gs.addFunc("restoreSnapshotJson", [](GameState *g, const std::string &json) -> int {
        if (!g) return 0;
        auto result = g->restoreSnapshotJson(json);
        return result.ok() ? 1 : 0;
    });

    auto worldState = table.addClass<WorldState>(
        "RPGWorldState", std::function<WorldState *()>([]() { return nullptr; }), true);
    worldState.addFunc("isObjectConsumed", &WorldState::isObjectConsumed);
    worldState.addFunc("consumeObject", [vm](WorldState *state, const std::string &mapId,
                                               const std::string &objectId) {
        if (!state)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::InvalidArgument, "RPGWorldState receiver must not be null",
                        "worldState", {}, "rpg.squirrel")));
        return eve::script::projectResult(vm, state->consumeObject(mapId, objectId));
    });
    worldState.addFunc("resetObject", [vm](WorldState *state, const std::string &mapId,
                                             const std::string &objectId) {
        if (!state)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::InvalidArgument, "RPGWorldState receiver must not be null",
                        "worldState", {}, "rpg.squirrel")));
        return eve::script::projectResult(vm, state->resetObject(mapId, objectId));
    });

    auto storyEvent = table.addClass<StoryEventSession>(
        "RPGStoryEventSession",
        std::function<StoryEventSession *()>([]() { return new StoryEventSession(); }), true);
    storyEvent.addFunc("begin", [vm](StoryEventSession *session, const std::string &eventId,
                                     GameState *gameState) {
        if (!session)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::InvalidArgument,
                        "RPGStoryEventSession receiver must not be null", "session", {},
                        "rpg.squirrel")));
        return eve::script::projectResult(vm, session->begin(eventId, gameState));
    });
    storyEvent.addFunc("advance", [vm](StoryEventSession *session, GameState *gameState) {
        if (!session)
            return eve::script::projectResult(
                vm, eve::Result<int>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::InvalidArgument,
                        "RPGStoryEventSession receiver must not be null", "session", {},
                        "rpg.squirrel")),
                [](int remaining) { return eve::Value(remaining); });
        return eve::script::projectResult(vm, session->advance(gameState),
                                          [](int remaining) { return eve::Value(remaining); });
    });
    storyEvent.addFunc("isActive", &StoryEventSession::isActive);
    storyEvent.addFunc("isFinished", &StoryEventSession::isFinished);
    storyEvent.addFunc("getStepIndex", &StoryEventSession::getStepIndex);
    storyEvent.addFunc("getStepCount", &StoryEventSession::getStepCount);
    storyEvent.addFunc("getEventId", &StoryEventSession::getEventId);
    storyEvent.addFunc("getStepKind", &StoryEventSession::getStepKind);
    storyEvent.addFunc("getReference", &StoryEventSession::getReference);
    storyEvent.addFunc("getActorId", &StoryEventSession::getActorId);
    storyEvent.addFunc("getX", [](StoryEventSession *session) -> float {
        return session ? static_cast<float>(session->getX()) : 0.0F;
    });
    storyEvent.addFunc("getY", [](StoryEventSession *session) -> float {
        return session ? static_cast<float>(session->getY()) : 0.0F;
    });
    storyEvent.addFunc("getDuration", [](StoryEventSession *session) -> float {
        return session ? static_cast<float>(session->getDuration()) : 0.0F;
    });

    auto saveSession = table.addClass<RPGSaveSession>(
        "RPGSaveSession", std::function<RPGSaveSession *()>([]() { return new RPGSaveSession(); }), true);
    saveSession.addFunc("bind",
                        [](RPGSaveSession *session, GameState *gameState, Tracker *tracker, RPGActor *actor,
                           inventory::Bag *bag, inventory::EquipmentSet *equipment) -> int {
        if (!session || !gameState || !tracker || !actor || !bag || !equipment) return 0;
        session->bind(*gameState, *tracker, *actor, *bag, *equipment);
        return 1;
    });
    saveSession.addFunc("bindParty",
                        [](RPGSaveSession *session, GameState *gameState, Tracker *tracker, Party *party,
                           inventory::Bag *bag, inventory::EquipmentSet *equipment) -> int {
        if (!session || !gameState || !tracker || !party || !bag || !equipment) return 0;
        session->bindParty(*gameState, *tracker, *party, *bag, *equipment);
        return 1;
    });
    saveSession.addFunc("setContentVersion", [](RPGSaveSession *session, const std::string &version) -> int {
        return session && session->setContentVersion(version).ok() ? 1 : 0;
    });
    saveSession.addFunc("getContentVersion", &RPGSaveSession::getContentVersion);
    saveSession.addFunc("allowCompatibleContentVersion",
                        [](RPGSaveSession *session, const std::string &version) -> int {
                            return session && session->allowCompatibleContentVersion(version).ok() ? 1 : 0;
                        });
    saveSession.addFunc(
        "addIdRenameMigration",
        [](RPGSaveSession *session, const std::string &fromVersion, const std::string &domain,
           const std::string &oldId, const std::string &newId) -> int {
            if (!session) return 0;
            RPGSaveIdDomain parsedDomain;
            if (domain == "item") parsedDomain = RPGSaveIdDomain::Item;
            else if (domain == "quest") parsedDomain = RPGSaveIdDomain::Quest;
            else if (domain == "questObjective") parsedDomain = RPGSaveIdDomain::QuestObjective;
            else if (domain == "skill") parsedDomain = RPGSaveIdDomain::Skill;
            else if (domain == "class") parsedDomain = RPGSaveIdDomain::Class;
            else if (domain == "trait") parsedDomain = RPGSaveIdDomain::Trait;
            else if (domain == "traitSource") parsedDomain = RPGSaveIdDomain::TraitSource;
            else if (domain == "equipmentSlot") parsedDomain = RPGSaveIdDomain::EquipmentSlot;
            else return 0;
            return session->addIdRenameMigration(fromVersion, parsedDomain, oldId, newId).ok() ? 1 : 0;
        });
    saveSession.addFunc("addQuestAdditionMigration",
                        [](RPGSaveSession *session, const std::string &fromVersion,
                           const std::string &questId) -> int {
                            return session && session->addQuestAdditionMigration(fromVersion, questId).ok()
                                       ? 1
                                       : 0;
                        });
    saveSession.addFunc("allowSingleActorPartyMigration",
                        [](RPGSaveSession *session, const std::string &fromVersion) -> int {
        return session && session->allowSingleActorPartyMigration(fromVersion).ok() ? 1 : 0;
    });
    saveSession.addFunc("snapshotJson", [](RPGSaveSession *session) -> std::string {
        if (!session) return {};
        auto result = session->snapshotJson();
        return result.ok() ? std::move(result).takeValue() : std::string{};
    });
    saveSession.addFunc("validateSnapshotJson", [](RPGSaveSession *session, const std::string &json) -> int {
        return session && session->validateSnapshotJson(json).ok() ? 1 : 0;
    });
    saveSession.addFunc("restoreSnapshotJson", [](RPGSaveSession *session, const std::string &json) -> int {
        if (!session) return 0;
        return session->restoreSnapshotJson(json).ok() ? 1 : 0;
    });
}

void RPG::expose(ssq::Class &cls) {
    const HSQUIRRELVM vm = cls.getHandle();
    cls.addFunc("getName", &RPG::getName);
    cls.addFunc("newActor", &RPG::newActor);
    cls.addFunc("newParty", &RPG::newParty);
    cls.addFunc("registerEffectsFromJson", &RPG::registerEffectsFromJson);
    cls.addFunc("clearEffectDefinitions", &RPG::clearEffectDefinitions);
    cls.addFunc("getEffectDefinitionCount", &RPG::getEffectDefinitionCount);
    cls.addFunc("registerSkillsFromJson", &RPG::registerSkillsFromJson);
    cls.addFunc("clearSkillDefinitions", &RPG::clearSkillDefinitions);
    cls.addFunc("getSkillDefinitionCount", &RPG::getSkillDefinitionCount);
    cls.addFunc("registerQuestsFromJson", &RPG::registerQuestsFromJson);
    cls.addFunc("replaceQuestsFromJson", [vm](RPG *rpg, const std::string &json) {
        if (!rpg)
            return eve::script::projectResult(
                vm, eve::Result<int>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::InvalidArgument, "RPG receiver must not be null", "rpg", {},
                        "rpg.squirrel")),
                [](int count) { return eve::Value(count); });
        return eve::script::projectResult(vm, rpg->replaceQuestsFromJson(json),
                                          [](int count) { return eve::Value(count); });
    });
    cls.addFunc("claimQuestRewards",
                [vm](RPG *rpg, Tracker *tracker, GameState *gameState,
                     inventory::Bag *bag, const std::string &questId) {
                    if (!rpg)
                        return eve::script::projectResult(
                            vm, eve::Result<int>::failure(eve::Diagnostic::error(
                                    eve::DiagnosticCode::InvalidArgument,
                                    "claimQuestRewards requires an RPG module", "rpg")),
                            [](int count) { return eve::Value(count); });
                    return eve::script::projectResult(
                        vm, rpg->claimQuestRewards(tracker, gameState, bag, questId),
                        [](int count) { return eve::Value(count); });
                });
    cls.addFunc(
        "collectWorldLoot",
        [vm](RPG *rpg, Tracker *tracker, GameState *gameState, inventory::Bag *bag,
             const std::string &mapId, const std::string &objectId,
             const std::string &requiredQuestId, const std::string &itemId, int itemQuantity,
             const std::string &attributeId, float attributeAmount,
             const std::string &notifyTopic, const std::string &notifyTarget, int notifyAmount) {
            if (!rpg)
                return eve::script::projectResult(
                    vm, eve::Result<int>::failure(eve::Diagnostic::error(
                            eve::DiagnosticCode::InvalidArgument,
                            "collectWorldLoot requires an RPG module", "rpg")),
                    [](int count) { return eve::Value(count); });
            WorldLootRequest request;
            request.mapId = mapId;
            request.objectId = objectId;
            request.requiredQuestId = requiredQuestId;
            request.itemId = itemId;
            request.itemQuantity = itemQuantity;
            request.attributeId = attributeId;
            request.attributeAmount = static_cast<double>(attributeAmount);
            request.notifyTopic = notifyTopic;
            request.notifyTarget = notifyTarget;
            request.notifyAmount = notifyAmount;
            return eve::script::projectResult(
                vm, rpg->collectWorldLoot(tracker, gameState, bag, request),
                [](int count) { return eve::Value(count); });
        });
    cls.addFunc(
        "settleBattleVictory",
        [vm](RPG *rpg, RPGActor *actor, Tracker *tracker, GameState *gameState,
             const std::string &mapId, const std::string &objectId,
             const std::string &requiredQuestId, float xpAmount, float xpGrowth,
             const std::string &attributeId, float attributeAmount,
             const std::string &notifyTopic, const std::string &notifyTarget, int notifyAmount,
             const std::string &defeatCounterId, int defeatCounterAmount,
             const std::string &levelPointAttributeId, int pointsPerLevel) {
            if (!rpg)
                return eve::script::projectResult(
                    vm, eve::Result<BattleVictoryReceipt>::failure(eve::Diagnostic::error(
                            eve::DiagnosticCode::InvalidArgument,
                            "settleBattleVictory requires an RPG module", "rpg")),
                    [](BattleVictoryReceipt receipt) {
                        eve::Value::Object value;
                        value.emplace("levelsGained", eve::Value(receipt.levelsGained));
                        value.emplace("skillsLearned", eve::Value(receipt.skillsLearned));
                        return eve::Value(std::move(value));
                    });
            BattleVictoryRequest request;
            request.mapId = mapId;
            request.objectId = objectId;
            request.requiredQuestId = requiredQuestId;
            request.xpAmount = static_cast<double>(xpAmount);
            request.xpGrowth = static_cast<double>(xpGrowth);
            request.attributeId = attributeId;
            request.attributeAmount = static_cast<double>(attributeAmount);
            request.notifyTopic = notifyTopic;
            request.notifyTarget = notifyTarget;
            request.notifyAmount = notifyAmount;
            request.defeatCounterId = defeatCounterId;
            request.defeatCounterAmount = defeatCounterAmount;
            request.levelPointAttributeId = levelPointAttributeId;
            request.pointsPerLevel = pointsPerLevel;
            return eve::script::projectResult(
                vm, rpg->settleBattleVictory(actor, tracker, gameState, request),
                [](BattleVictoryReceipt receipt) {
                    eve::Value::Object value;
                    value.emplace("levelsGained", eve::Value(receipt.levelsGained));
                    value.emplace("skillsLearned", eve::Value(receipt.skillsLearned));
                    return eve::Value(std::move(value));
                });
        });
    cls.addFunc("replaceShopOffersFromJson", [vm](RPG *rpg, const std::string &json) {
        if (!rpg)
            return eve::script::projectResult(
                vm, eve::Result<int>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::InvalidArgument,
                        "replaceShopOffersFromJson requires an RPG module", "rpg")),
                [](int count) { return eve::Value(count); });
        return eve::script::projectResult(vm, rpg->replaceShopOffersFromJson(json),
                                          [](int count) { return eve::Value(count); });
    });
    cls.addFunc("getShopOfferCount", &RPG::getShopOfferCount);
    cls.addFunc("clearShopOffers", &RPG::clearShopOffers);
    cls.addFunc("hasShopOffer", &RPG::hasShopOffer);
    cls.addFunc("getShopOfferId", &RPG::getShopOfferId);
    cls.addFunc("getShopOfferItemId", &RPG::getShopOfferItemId);
    cls.addFunc("getShopOfferName", &RPG::getShopOfferName);
    cls.addFunc("getShopOfferDescription", &RPG::getShopOfferDescription);
    cls.addFunc("getShopOfferBuyPrice", &RPG::getShopOfferBuyPrice);
    cls.addFunc("getShopOfferSellPrice", &RPG::getShopOfferSellPrice);
    cls.addFunc("buyShopOffer",
                [vm](RPG *rpg, GameState *gameState, inventory::Bag *bag,
                     const std::string &currencyId, const std::string &offerId, int quantity) {
                    if (!rpg)
                        return eve::script::projectResult(
                            vm, eve::Result<int>::failure(eve::Diagnostic::error(
                                    eve::DiagnosticCode::InvalidArgument,
                                    "buyShopOffer requires an RPG module", "rpg")),
                            [](int count) { return eve::Value(count); });
                    return eve::script::projectResult(
                        vm, rpg->buyShopOffer(gameState, bag, currencyId, offerId, quantity),
                        [](int count) { return eve::Value(count); });
                });
    cls.addFunc("sellShopOffer",
                [vm](RPG *rpg, GameState *gameState, inventory::Bag *bag,
                     const std::string &currencyId, const std::string &offerId, int quantity) {
                    if (!rpg)
                        return eve::script::projectResult(
                            vm, eve::Result<int>::failure(eve::Diagnostic::error(
                                    eve::DiagnosticCode::InvalidArgument,
                                    "sellShopOffer requires an RPG module", "rpg")),
                            [](int count) { return eve::Value(count); });
                    return eve::script::projectResult(
                        vm, rpg->sellShopOffer(gameState, bag, currencyId, offerId, quantity),
                        [](int count) { return eve::Value(count); });
                });
    cls.addFunc("replaceEncountersFromJson", [vm](RPG *rpg, const std::string &json) {
        if (!rpg)
            return eve::script::projectResult(
                vm, eve::Result<int>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::InvalidArgument,
                        "replaceEncountersFromJson requires an RPG module", "rpg")),
                [](int count) { return eve::Value(count); });
        return eve::script::projectResult(vm, rpg->replaceEncountersFromJson(json),
                                          [](int count) { return eve::Value(count); });
    });
    cls.addFunc("clearEncounters", &RPG::clearEncounters);
    cls.addFunc("hasEncounter", &RPG::hasEncounter);
    cls.addFunc("getEncounterMemberCount", &RPG::getEncounterMemberCount);
    cls.addFunc("newEncounterActor", &RPG::newEncounterActor);
    cls.addFunc("newEncounterMemberActor", &RPG::newEncounterMemberActor);
    cls.addFunc("getEncounterDisplayName", &RPG::getEncounterDisplayName);
    cls.addFunc("getEncounterMaxHp", &RPG::getEncounterMaxHp);
    cls.addFunc("getEncounterMemberDisplayName", &RPG::getEncounterMemberDisplayName);
    cls.addFunc("getEncounterMemberMaxHp", &RPG::getEncounterMemberMaxHp);
    cls.addFunc("replaceBattleTacticsFromJson", [vm](RPG *rpg, const std::string &json) {
        if (!rpg)
            return eve::script::projectResult(
                vm, eve::Result<int>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::InvalidArgument,
                        "replaceBattleTacticsFromJson requires an RPG module", "rpg")),
                [](int count) { return eve::Value(count); });
        return eve::script::projectResult(vm, rpg->replaceBattleTacticsFromJson(json),
                                          [](int count) { return eve::Value(count); });
    });
    cls.addFunc("clearBattleTactics", &RPG::clearBattleTactics);
    cls.addFunc("queueBattleTactic",
                [vm](RPG *rpg, Battle *battle, RPGActor *actor, const std::string &tacticsId) {
        if (!rpg)
            return eve::script::projectResult(
                vm, eve::Result<std::string>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::InvalidArgument,
                        "queueBattleTactic requires an RPG module", "rpg")),
                [](const std::string &skillId) { return eve::Value(skillId); });
        return eve::script::projectResult(vm, rpg->queueBattleTactic(battle, actor, tacticsId),
                                          [](const std::string &skillId) { return eve::Value(skillId); });
    });
    cls.addFunc("replaceStoryEventsFromJson", [vm](RPG *rpg, const std::string &json) {
        if (!rpg)
            return eve::script::projectResult(
                vm, eve::Result<int>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::InvalidArgument,
                        "replaceStoryEventsFromJson requires an RPG module", "rpg")),
                [](int count) { return eve::Value(count); });
        return eve::script::projectResult(vm, rpg->replaceStoryEventsFromJson(json),
                                          [](int count) { return eve::Value(count); });
    });
    cls.addFunc("clearStoryEvents", &RPG::clearStoryEvents);
    cls.addFunc("getStoryEventCount", &RPG::getStoryEventCount);
    cls.addFunc("hasStoryEvent", &RPG::hasStoryEvent);
    cls.addFunc("newStoryEventSession", &RPG::newStoryEventSession);
    cls.addFunc("settleEncounterVictory",
                [vm](RPG *rpg, RPGActor *actor, Tracker *tracker, GameState *gameState,
                     const std::string &encounterId, const std::string &mapId,
                     const std::string &objectId) {
                    if (!rpg)
                        return eve::script::projectResult(
                            vm, eve::Result<BattleVictoryReceipt>::failure(
                                    eve::Diagnostic::error(
                                        eve::DiagnosticCode::InvalidArgument,
                                        "settleEncounterVictory requires an RPG module", "rpg")),
                            [](BattleVictoryReceipt receipt) {
                                eve::Value::Object value;
                                value.emplace("levelsGained", eve::Value(receipt.levelsGained));
                                value.emplace("skillsLearned", eve::Value(receipt.skillsLearned));
                                return eve::Value(std::move(value));
                            });
                    return eve::script::projectResult(
                        vm, rpg->settleEncounterVictory(actor, tracker, gameState,
                                                        encounterId, mapId, objectId),
                        [](BattleVictoryReceipt receipt) {
                            eve::Value::Object value;
                            value.emplace("levelsGained", eve::Value(receipt.levelsGained));
                            value.emplace("skillsLearned", eve::Value(receipt.skillsLearned));
                            return eve::Value(std::move(value));
                        });
                });
    cls.addFunc("clearQuestDefinitions", &RPG::clearQuestDefinitions);
    cls.addFunc("getQuestDefinitionCount", &RPG::getQuestDefinitionCount);
    cls.addFunc("hasQuestDefinition", &RPG::hasQuestDefinition);
    cls.addFunc("newTracker", &RPG::newTracker);
    cls.addFunc("getLevelUpEventCount", &RPG::getLevelUpEventCount);
    cls.addFunc("getLevelUpEventActor", &RPG::getLevelUpEventActor);
    cls.addFunc("getLevelUpEventPreviousLevel", &RPG::getLevelUpEventPreviousLevel);
    cls.addFunc("getLevelUpEventNewLevel", &RPG::getLevelUpEventNewLevel);
    cls.addFunc("getVitalsEventCount", &RPG::getVitalsEventCount);
    cls.addFunc("getVitalsEventActor", &RPG::getVitalsEventActor);
    cls.addFunc("getVitalsEventResource", &RPG::getVitalsEventResource);
    cls.addFunc("getVitalsEventAction", &RPG::getVitalsEventAction);
    cls.addFunc("getVitalsEventAmount",
                [](RPG *r, int index) -> float { return float(r->getVitalsEventAmount(index)); });
    cls.addFunc("getVitalsEventSource", &RPG::getVitalsEventSource);
    cls.addFunc("getVitalsEventCurrent",
                [](RPG *r, int index) -> float { return float(r->getVitalsEventCurrent(index)); });
    cls.addFunc("getVitalsEventMax",
                [](RPG *r, int index) -> float { return float(r->getVitalsEventMax(index)); });
    cls.addFunc("registerItemStatsFromJson", &RPG::registerItemStatsFromJson);
    cls.addFunc("clearItemStats", &RPG::clearItemStats);
    cls.addFunc("clearAllItemStats", &RPG::clearAllItemStats);
    cls.addFunc("getItemStatCount", &RPG::getItemStatCount);
    cls.addFunc("syncEquipModifiers", &RPG::syncEquipModifiers);
    cls.addFunc("registerLootTablesFromJson", &RPG::registerLootTablesFromJson);
    cls.addFunc("clearLootTables", &RPG::clearLootTables);
    cls.addFunc("getLootTableCount", &RPG::getLootTableCount);
    cls.addFunc("rollLoot", &RPG::rollLoot);
    cls.addFunc("registerTraitsFromJson", &RPG::registerTraitsFromJson);
    cls.addFunc("clearTraitDefinitions", &RPG::clearTraitDefinitions);
    cls.addFunc("getTraitDefinitionCount", &RPG::getTraitDefinitionCount);
    cls.addFunc("registerClassesFromJson", &RPG::registerClassesFromJson);
    cls.addFunc("clearClassDefinitions", &RPG::clearClassDefinitions);
    cls.addFunc("getClassDefinitionCount", &RPG::getClassDefinitionCount);
    cls.addFunc("registerSkillDamage",
                [](RPG *r, const std::string &skillId, const std::string &damageType,
                   const std::string &formula, const std::string &element, float critChance,
                   int hitChance) -> int {
                    return r->registerSkillDamage(skillId, damageType, formula, element,
                                                  double(critChance), hitChance);
                });
    cls.addFunc("clearSkillDamage", &RPG::clearSkillDamage);
    cls.addFunc("newBattle", &RPG::newBattle);
    cls.addFunc("newGameState", &RPG::newGameState);
    cls.addFunc("newWorldState", &RPG::newWorldState);
    cls.addFunc("globalGameState", &RPG::globalGameState);
    cls.addFunc("update", &RPG::update);
    cls.addFunc("getTickEventCount", &RPG::getTickEventCount);
    cls.addFunc("getTickEventActor", &RPG::getTickEventActor);
    cls.addFunc("getTickEventInstanceId", &RPG::getTickEventInstanceId);
    cls.addFunc("getTickEventEffectId", &RPG::getTickEventEffectId);
    cls.addFunc("getTickEventSource", &RPG::getTickEventSource);
    cls.addFunc("getTickEventStacks", &RPG::getTickEventStacks);
    cls.addFunc("getStatusChangeEventCount", &RPG::getStatusChangeEventCount);
    cls.addFunc("getStatusChangeEventActor", &RPG::getStatusChangeEventActor);
    cls.addFunc("getStatusChangeEventInstanceId", &RPG::getStatusChangeEventInstanceId);
    cls.addFunc("getStatusChangeEventEffectId", &RPG::getStatusChangeEventEffectId);
    cls.addFunc("getStatusChangeEventSource", &RPG::getStatusChangeEventSource);
    cls.addFunc("getStatusChangeEventAction", &RPG::getStatusChangeEventAction);
    cls.addFunc("getStatusChangeEventStacks", &RPG::getStatusChangeEventStacks);
    cls.addFunc("getStatusChangeEventReason", &RPG::getStatusChangeEventReason);
    cls.addFunc("getCastEventCount", &RPG::getCastEventCount);
    cls.addFunc("getCastEventCaster", &RPG::getCastEventCaster);
    cls.addFunc("getCastEventTarget", &RPG::getCastEventTarget);
    cls.addFunc("getCastEventSkillId", &RPG::getCastEventSkillId);
    cls.addFunc("newSettlementContext", &RPG::newSettlementContext);
    cls.addFunc("runSettlement", &RPG::runSettlement);
    cls.addFunc("getSettlementStageCount", &RPG::getSettlementStageCount);
    cls.addFunc("hasSettlementStage", &RPG::hasSettlementStage);
    cls.addFunc("setSettlementStageEnabled", &RPG::setSettlementStageEnabled);
    cls.addFunc("setSettlementStagePriority", &RPG::setSettlementStagePriority);
    cls.addFunc("removeSettlementStage", &RPG::removeSettlementStage);
    cls.addFunc("clearSettlementPipeline", &RPG::clearSettlementPipeline);
}

}  // namespace eve::rpg
