#include "rpg/RPG.h"
#include "rpg/Effect.h"
#include "rpg/Loot.h"
#include "rpg/Quest.h"
#include "rpg/Settlement.h"
#include "rpg/Skill.h"
#include "rpg/SkillSystem.h"
#include "rpg/StateCodec.h"
#include "rpg/StatusSystem.h"
#include "rpg/Tracker.h"
#include "rpg/UtilityAI.h"

#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::rpg {

Module_IMPL(RPG, new RPG());

RPGActor* RPG::newActor() { return RPGActor::createActor(); }

int RPG::registerEffectsFromJson(const std::string& json) { return EffectRegistry::loadFromJson(json, nullptr); }

void RPG::clearEffectDefinitions() { EffectRegistry::clear(); }

int RPG::getEffectDefinitionCount() { return EffectRegistry::count(); }

int RPG::registerSkillsFromJson(const std::string& json) { return SkillRegistry::loadFromJson(json, nullptr); }

void RPG::clearSkillDefinitions() { SkillRegistry::clear(); }

int RPG::getSkillDefinitionCount() { return SkillRegistry::count(); }

int       RPG::registerQuestsFromJson(const std::string& json) { return QuestRegistry::loadFromJson(json, nullptr); }
void      RPG::clearQuestDefinitions() { QuestRegistry::clear(); }
int       RPG::getQuestDefinitionCount() { return QuestRegistry::count(); }
Tracker*  RPG::newTracker() { return new Tracker(); }
int       RPG::registerLootTablesFromJson(const std::string& json) { return LootRegistry::loadFromJson(json, nullptr); }
void      RPG::clearLootTableDefinitions() { LootRegistry::clear(); }
int       RPG::getLootTableDefinitionCount() { return LootRegistry::count(); }
void      RPG::setLootRandomSeed(int seed) { LootSystem::setRandomSeed(uint32_t(seed)); }
LootRoll* RPG::rollLoot(const std::string& tableId, float luck) { return LootSystem::roll(tableId, luck); }
int       RPG::registerAIDecisionSetsFromJson(const std::string& json) {
    return AIDecisionRegistry::loadFromJson(json, nullptr);
}
void          RPG::clearAIDecisionSetDefinitions() { AIDecisionRegistry::clear(); }
int           RPG::getAIDecisionSetDefinitionCount() { return AIDecisionRegistry::count(); }
UtilityBrain* RPG::newUtilityBrain() { return new UtilityBrain(); }
std::string   RPG::serializeActor(RPGActor* actor) { return StateCodec::serializeActor(actor); }
bool          RPG::deserializeActor(RPGActor* actor, const std::string& json) {
    return StateCodec::deserializeActor(actor, json, nullptr);
}
std::string RPG::serializeTracker(Tracker* tracker) { return StateCodec::serializeTracker(tracker); }
bool        RPG::deserializeTracker(Tracker* tracker, const std::string& json) {
    return StateCodec::deserializeTracker(tracker, json, nullptr);
}

void  RPG::setCombatRandomSeed(int seed) { CombatSystem::setRandomSeed(uint32_t(seed)); }
float RPG::dealDamage(RPGActor* source, RPGActor* target, float power, const std::string& damageType) {
    return float(CombatSystem::dealDamage(source, target, power, damageType));
}
float RPG::heal(RPGActor* source, RPGActor* target, float amount) {
    return float(CombatSystem::heal(source, target, amount));
}

void RPG::update(float dt) {
    StatusSystem::update(dt);
    SkillSystem::update(dt);

    ticks_.clear();
    StatusSystem::pollTicks(ticks_);
    changes_.clear();
    StatusSystem::pollChanges(changes_);
    casts_.clear();
    SkillSystem::pollCastEvents(casts_);
    CombatSystem::pollEvents(combatEvents_);
    ProgressionSystem::pollEvents(levelEvents_);
}

#define RPG_EVENT_AT(Events, Index) \
    ((Index) >= 0 && size_t(Index) < (Events).size() ? &(Events)[size_t(Index)] : nullptr)
int       RPG::getCombatEventCount() const { return int(combatEvents_.size()); }
RPGActor* RPG::getCombatEventSource(int i) const {
    auto* e = RPG_EVENT_AT(combatEvents_, i);
    return e ? e->source : nullptr;
}
RPGActor* RPG::getCombatEventTarget(int i) const {
    auto* e = RPG_EVENT_AT(combatEvents_, i);
    return e ? e->target : nullptr;
}
std::string RPG::getCombatEventAction(int i) const {
    auto* e = RPG_EVENT_AT(combatEvents_, i);
    return e ? e->action : std::string{};
}
std::string RPG::getCombatEventDamageType(int i) const {
    auto* e = RPG_EVENT_AT(combatEvents_, i);
    return e ? e->damageType : std::string{};
}
float RPG::getCombatEventAmount(int i) const {
    auto* e = RPG_EVENT_AT(combatEvents_, i);
    return e ? float(e->amount) : 0.f;
}
bool RPG::getCombatEventCritical(int i) const {
    auto* e = RPG_EVENT_AT(combatEvents_, i);
    return e && e->critical;
}
bool RPG::getCombatEventKilled(int i) const {
    auto* e = RPG_EVENT_AT(combatEvents_, i);
    return e && e->killed;
}
int       RPG::getLevelEventCount() const { return int(levelEvents_.size()); }
RPGActor* RPG::getLevelEventActor(int i) const {
    auto* e = RPG_EVENT_AT(levelEvents_, i);
    return e ? e->actor : nullptr;
}
int RPG::getLevelEventOldLevel(int i) const {
    auto* e = RPG_EVENT_AT(levelEvents_, i);
    return e ? e->oldLevel : 0;
}
int RPG::getLevelEventNewLevel(int i) const {
    auto* e = RPG_EVENT_AT(levelEvents_, i);
    return e ? e->newLevel : 0;
}
int RPG::getLevelEventPointsGranted(int i) const {
    auto* e = RPG_EVENT_AT(levelEvents_, i);
    return e ? e->pointsGranted : 0;
}
#undef RPG_EVENT_AT

int RPG::getTickEventCount() const { return int(ticks_.size()); }

RPGActor* RPG::getTickEventActor(int index) const {
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

RPGActor* RPG::getStatusChangeEventActor(int index) const {
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

RPGActor* RPG::getCastEventCaster(int index) const {
    if (index < 0 || size_t(index) >= casts_.size()) return nullptr;
    return casts_[size_t(index)].caster;
}

RPGActor* RPG::getCastEventTarget(int index) const {
    if (index < 0 || size_t(index) >= casts_.size()) return nullptr;
    return casts_[size_t(index)].target;
}

std::string RPG::getCastEventSkillId(int index) const {
    if (index < 0 || size_t(index) >= casts_.size()) return {};
    return casts_[size_t(index)].skillId;
}

SettlementContext* RPG::newSettlementContext() { return new SettlementContext(); }

void RPG::runSettlement(const std::string& pipeline, SettlementContext* ctx) {
    if (!ctx) return;
    SettlementPipeline::run(pipeline, *ctx);
}

int RPG::getSettlementStageCount(const std::string& pipeline) { return SettlementPipeline::stageCount(pipeline); }

bool RPG::hasSettlementStage(const std::string& pipeline, const std::string& stage) {
    return SettlementPipeline::hasStage(pipeline, stage);
}

bool RPG::setSettlementStageEnabled(const std::string& pipeline, const std::string& stage, bool enabled) {
    return SettlementPipeline::setStageEnabled(pipeline, stage, enabled);
}

bool RPG::setSettlementStagePriority(const std::string& pipeline, const std::string& stage, int priority) {
    return SettlementPipeline::setStagePriority(pipeline, stage, priority);
}

bool RPG::removeSettlementStage(const std::string& pipeline, const std::string& stage) {
    return SettlementPipeline::unregisterStage(pipeline, stage);
}

void RPG::clearSettlementPipeline(const std::string& pipeline) { SettlementPipeline::clearPipeline(pipeline); }

void RPG::expose(ssq::Table& table) {
    auto cls = table.addClass(name, RPG::create, false);
    expose(cls);

    auto actor =
        table.addClass<RPGActor>("RPGActor", std::function<RPGActor*()>([]() -> RPGActor* { return nullptr; }), true);
    // 绑定为 float：SimpleSquirrel 只有在 squirrel 以 SQUSEDOUBLE 编译时才认识
    // C++ double（本项目未开启），否则 double 参数/返回值会被当成 INSTANCE
    // 处理，脚本调用直接抛 "bad cast expected INSTANCE got FLOAT"。
    // 同样的坑见 mouse/Mouse.cpp、timer/Timer.h 的注释。
    actor.addFunc("setBaseAttribute", [](RPGActor* a, const std::string& attribute, float value) {
        if (a) a->setBaseAttribute(attribute, double(value));
    });
    actor.addFunc("getBaseAttribute", [](RPGActor* a, const std::string& attribute) -> float {
        return a ? float(a->getBaseAttribute(attribute)) : 0.f;
    });
    actor.addFunc("modifyBaseAttribute", [](RPGActor* a, const std::string& attribute, float delta) {
        if (a) a->modifyBaseAttribute(attribute, double(delta));
    });
    actor.addFunc("hasAttribute", &RPGActor::hasAttribute);
    actor.addFunc("addAttributeModifier",
                  [](RPGActor* a, const std::string& attribute, const std::string& source, const std::string& op,
                     float value, int priority) -> std::string {
                      return a ? a->addAttributeModifier(attribute, source, op, double(value), priority)
                               : std::string{};
                  });
    actor.addFunc("removeAttributeModifier", &RPGActor::removeAttributeModifier);
    actor.addFunc("removeAttributeModifiersBySource", &RPGActor::removeAttributeModifiersBySource);
    actor.addFunc("removeAllAttributeModifiersBySource", &RPGActor::removeAllAttributeModifiersBySource);
    actor.addFunc("getFinalAttribute", [](RPGActor* a, const std::string& attribute) -> float {
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
    actor.addFunc("configureProgression", &RPGActor::configureProgression);
    actor.addFunc("gainExperience", &RPGActor::gainExperience);
    actor.addFunc("getLevel", &RPGActor::getLevel);
    actor.addFunc("getExperience", &RPGActor::getExperience);
    actor.addFunc("getExperienceForNextLevel", &RPGActor::getExperienceForNextLevel);
    actor.addFunc("getUnspentPoints", &RPGActor::getUnspentPoints);
    actor.addFunc("spendPoints", &RPGActor::spendPoints);

    auto ctx = table.addClass<SettlementContext>(
        "SettlementContext", std::function<SettlementContext*()>([]() { return new SettlementContext(); }), true);
    // 同上：SettlementContext::get/set 用 double，脚本侧包一层转 float。
    ctx.addFunc("get", [](SettlementContext* c, const std::string& key, float fallback) -> float {
        return c ? float(c->get(key, double(fallback))) : fallback;
    });
    ctx.addFunc("set", [](SettlementContext* c, const std::string& key, float value) {
        if (c) c->set(key, double(value));
    });
    ctx.addFunc("has", &SettlementContext::has);
    ctx.addFunc("addTag", &SettlementContext::addTag);
    ctx.addFunc("hasTag", &SettlementContext::hasTag);

    auto tracker =
        table.addClass<Tracker>("QuestTracker", std::function<Tracker*()>([]() { return new Tracker(); }), true);
    tracker.addFunc("destroy", &Tracker::destroy);
    tracker.addFunc("syncAuto", &Tracker::syncAuto);
    tracker.addFunc("activate", &Tracker::activate);
    tracker.addFunc("canActivate", &Tracker::canActivate);
    tracker.addFunc("canActivateReason", &Tracker::canActivateReason);
    tracker.addFunc("notify", &Tracker::notify);
    tracker.addFunc("claim", &Tracker::claim);
    tracker.addFunc("reset", &Tracker::reset);
    tracker.addFunc("abandon", &Tracker::abandon);
    tracker.addFunc("fail", &Tracker::fail);
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
    tracker.addFunc("getRewardAmount", &Tracker::getRewardAmount);
    tracker.addFunc("pollEvents", &Tracker::pollEvents);
    tracker.addFunc("getEventCount", &Tracker::getEventCount);
    tracker.addFunc("getEventEntryId", &Tracker::getEventEntryId);
    tracker.addFunc("getEventObjectiveId", &Tracker::getEventObjectiveId);
    tracker.addFunc("getEventAction", &Tracker::getEventAction);
    tracker.addFunc("getEventTopic", &Tracker::getEventTopic);
    tracker.addFunc("getEventTarget", &Tracker::getEventTarget);
    tracker.addFunc("getEventReason", &Tracker::getEventReason);
    tracker.addFunc("getEventAmount", &Tracker::getEventAmount);

    auto loot = table.addClass<LootRoll>("LootRoll", std::function<LootRoll*()>([]() { return new LootRoll(); }), true);
    loot.addFunc("destroy", &LootRoll::destroy);
    loot.addFunc("getCount", &LootRoll::getCount);
    loot.addFunc("getItemId", &LootRoll::getItemId);
    loot.addFunc("getQuantity", &LootRoll::getQuantity);

    auto brain = table.addClass<UtilityBrain>(
        "UtilityBrain", std::function<UtilityBrain*()>([]() { return new UtilityBrain(); }), true);
    brain.addFunc("destroy", &UtilityBrain::destroy);
    brain.addFunc("setDecisionSet", &UtilityBrain::setDecisionSet);
    brain.addFunc("getDecisionSet", &UtilityBrain::getDecisionSet);
    brain.addFunc("setInput", &UtilityBrain::setInput);
    brain.addFunc("getInput", &UtilityBrain::getInput);
    brain.addFunc("chooseAction", &UtilityBrain::chooseAction);
    brain.addFunc("getLastScore", &UtilityBrain::getLastScore);
}

void RPG::expose(ssq::Class& cls) {
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
    cls.addFunc("registerLootTablesFromJson", &RPG::registerLootTablesFromJson);
    cls.addFunc("clearLootTableDefinitions", &RPG::clearLootTableDefinitions);
    cls.addFunc("getLootTableDefinitionCount", &RPG::getLootTableDefinitionCount);
    cls.addFunc("setLootRandomSeed", &RPG::setLootRandomSeed);
    cls.addFunc("rollLoot", &RPG::rollLoot);
    cls.addFunc("registerAIDecisionSetsFromJson", &RPG::registerAIDecisionSetsFromJson);
    cls.addFunc("clearAIDecisionSetDefinitions", &RPG::clearAIDecisionSetDefinitions);
    cls.addFunc("getAIDecisionSetDefinitionCount", &RPG::getAIDecisionSetDefinitionCount);
    cls.addFunc("newUtilityBrain", &RPG::newUtilityBrain);
    cls.addFunc("serializeActor", &RPG::serializeActor);
    cls.addFunc("deserializeActor", &RPG::deserializeActor);
    cls.addFunc("serializeTracker", &RPG::serializeTracker);
    cls.addFunc("deserializeTracker", &RPG::deserializeTracker);
    cls.addFunc("setCombatRandomSeed", &RPG::setCombatRandomSeed);
    cls.addFunc("dealDamage", &RPG::dealDamage);
    cls.addFunc("heal", &RPG::heal);
    cls.addFunc("getCombatEventCount", &RPG::getCombatEventCount);
    cls.addFunc("getCombatEventSource", &RPG::getCombatEventSource);
    cls.addFunc("getCombatEventTarget", &RPG::getCombatEventTarget);
    cls.addFunc("getCombatEventAction", &RPG::getCombatEventAction);
    cls.addFunc("getCombatEventDamageType", &RPG::getCombatEventDamageType);
    cls.addFunc("getCombatEventAmount", &RPG::getCombatEventAmount);
    cls.addFunc("getCombatEventCritical", &RPG::getCombatEventCritical);
    cls.addFunc("getCombatEventKilled", &RPG::getCombatEventKilled);
    cls.addFunc("getLevelEventCount", &RPG::getLevelEventCount);
    cls.addFunc("getLevelEventActor", &RPG::getLevelEventActor);
    cls.addFunc("getLevelEventOldLevel", &RPG::getLevelEventOldLevel);
    cls.addFunc("getLevelEventNewLevel", &RPG::getLevelEventNewLevel);
    cls.addFunc("getLevelEventPointsGranted", &RPG::getLevelEventPointsGranted);
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
