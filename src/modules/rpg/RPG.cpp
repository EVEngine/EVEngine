#include "rpg/RPG.h"
#include "rpg/Battle.h"
#include "rpg/BattleSystem.h"
#include "rpg/Class.h"
#include "rpg/Effect.h"
#include "rpg/EquipmentSystem.h"
#include "rpg/GameState.h"
#include "rpg/LevelSystem.h"
#include "rpg/LootSystem.h"
#include "rpg/Quest.h"
#include "rpg/Skill.h"
#include "rpg/StatusSystem.h"
#include "rpg/SkillSystem.h"
#include "rpg/Settlement.h"
#include "rpg/Tracker.h"
#include "rpg/Trait.h"
#include "rpg/VitalsSystem.h"

#include <random>

#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::rpg {

Module_IMPL(RPG, new RPG());

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

void RPG::clearQuestDefinitions() { QuestRegistry::clear(); }

int RPG::getQuestDefinitionCount() { return QuestRegistry::count(); }

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
    BattleSystem::registerSkillDamage(skillId, spec);
    return 1;
}

void RPG::clearSkillDamage() { BattleSystem::clearSkillDamage(); }

Battle *RPG::newBattle() { return new Battle(); }

GameState *RPG::newGameState() { return new GameState(); }

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
}

void RPG::expose(ssq::Class &cls) {
    cls.addFunc("getName", &RPG::getName);
    cls.addFunc("newActor", &RPG::newActor);
    cls.addFunc("registerEffectsFromJson", &RPG::registerEffectsFromJson);
    cls.addFunc("clearEffectDefinitions", &RPG::clearEffectDefinitions);
    cls.addFunc("getEffectDefinitionCount", &RPG::getEffectDefinitionCount);
    cls.addFunc("registerSkillsFromJson", &RPG::registerSkillsFromJson);
    cls.addFunc("clearSkillDefinitions", &RPG::clearSkillDefinitions);
    cls.addFunc("getSkillDefinitionCount", &RPG::getSkillDefinitionCount);
    cls.addFunc("registerQuestsFromJson", &RPG::registerQuestsFromJson);
    cls.addFunc("clearQuestDefinitions", &RPG::clearQuestDefinitions);
    cls.addFunc("getQuestDefinitionCount", &RPG::getQuestDefinitionCount);
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
