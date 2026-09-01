#include "rpg/RPGActor.h"
#include "rpg/AttributeSystem.h"
#include "rpg/ClassSystem.h"
#include "rpg/LevelSystem.h"
#include "rpg/StatusSystem.h"
#include "rpg/SkillSystem.h"
#include "rpg/TraitSystem.h"
#include "rpg/VitalsSystem.h"

#include <algorithm>
#include <utility>

namespace eve::rpg {

namespace {

// 按 handle（id + generation）而非裸指针跟踪存活 actor：
// ECS 的 entity 槽位在销毁后会被复用，复用后地址相同但其实是另一个实体，
// 裸指针 + is_entity_visible 无法区分这种复用（复用后 flags 会被重置，
// 旧的裸指针看起来又"活"了），必须连同 generation 一起校验才安全。
std::vector<ecs::EntityHandle> &registryStorage() {
    static std::vector<ecs::EntityHandle> v;
    return v;
}

}  // namespace

RPGActor *RPGActor::createActor() {
    RPGActor *actor = RPGActor::create();
    // Touch each component once so accessors are cheap/valid immediately.
    actor->attributes();
    actor->statuses();
    actor->skills();
    actor->progression();
    actor->vitals();
    actor->traits();
    actor->classInfo();
    registryStorage().push_back(ecs::handle_of(actor));
    return actor;
}

const std::vector<RPGActor *> &RPGActor::liveActors() {
    static std::vector<RPGActor *> resolved;
    auto &handles = registryStorage();

    handles.erase(std::remove_if(handles.begin(), handles.end(),
                                  [](const ecs::EntityHandle &h) { return ecs::try_get(h) == nullptr; }),
                  handles.end());

    resolved.clear();
    resolved.reserve(handles.size());
    for (const ecs::EntityHandle &h : handles)
        resolved.push_back(static_cast<RPGActor *>(ecs::try_get(h)));
    return resolved;
}

// ---- Attributes ----

void RPGActor::setBaseAttribute(const std::string &attribute, double value) {
    AttributeSystem::setBase(this, attribute, value);
}

double RPGActor::getBaseAttribute(const std::string &attribute) {
    return AttributeSystem::getBase(this, attribute);
}

void RPGActor::modifyBaseAttribute(const std::string &attribute, double delta) {
    AttributeSystem::modifyBase(this, attribute, delta);
}

bool RPGActor::hasAttribute(const std::string &attribute) {
    return AttributeSystem::hasAttribute(this, attribute);
}

eve::Result<ModifierId> RPGActor::addAttributeModifier(AttributeModifier modifier) {
    return AttributeSystem::addModifier(this, std::move(modifier));
}

std::string RPGActor::addAttributeModifier(const std::string &attribute, const std::string &source,
                                            const std::string &op, double value, int priority) {
    return AttributeSystem::addModifier(this, attribute, source, op, value, priority);
}

eve::Result<void> RPGActor::removeAttributeModifier(const ModifierId &modifierId) {
    return AttributeSystem::removeModifier(this, modifierId);
}

bool RPGActor::removeAttributeModifier(const std::string &attribute, const std::string &modifierId) {
    return AttributeSystem::removeModifier(this, attribute, modifierId);
}

int RPGActor::removeAttributeModifiersBySource(const std::string &attribute,
                                                const std::string &source) {
    auto result = AttributeSystem::removeModifiersBySource(this, attribute, source);
    return result.ok() ? result.value() : 0;
}

int RPGActor::removeAllAttributeModifiersBySource(const std::string &source) {
    return AttributeSystem::removeAllModifiersBySource(this, source);
}

double RPGActor::getFinalAttribute(const std::string &attribute) {
    return AttributeSystem::getFinal(this, attribute);
}

// ---- Status ----

int RPGActor::applyEffect(const std::string &effectId, const std::string &source) {
    auto result = StatusSystem::apply(this, effectId, source);
    return result.ok() ? std::move(result).takeValue() : -1;
}

bool RPGActor::removeStatus(int instanceId) { return StatusSystem::remove(this, instanceId).ok(); }

int RPGActor::removeStatusByEffect(const std::string &effectId) {
    return StatusSystem::removeByEffect(this, effectId);
}

int RPGActor::removeStatusBySource(const std::string &source) {
    return StatusSystem::removeBySource(this, source);
}

int RPGActor::removeStatusByTag(const std::string &tag) {
    return StatusSystem::removeByTag(this, tag);
}

bool RPGActor::hasEffect(const std::string &effectId) { return StatusSystem::hasEffect(this, effectId); }

bool RPGActor::hasStatusTag(const std::string &tag) { return StatusSystem::hasTag(this, tag); }

int RPGActor::getStatusCount() { return StatusSystem::getActiveCount(this); }

std::string RPGActor::getStatusEffectId(int index) {
    return StatusSystem::getActiveEffectId(this, index);
}

int RPGActor::getStatusStacks(int index) { return StatusSystem::getActiveStacks(this, index); }

float RPGActor::getStatusRemaining(int index) { return StatusSystem::getActiveRemaining(this, index); }

int RPGActor::getStatusInstanceId(int index) { return StatusSystem::getActiveInstanceId(this, index); }

std::string RPGActor::getStatusSource(int index) { return StatusSystem::getActiveSource(this, index); }

std::string RPGActor::getStatusProp(int instanceId, const std::string &key,
                                     const std::string &fallback) {
    return StatusSystem::getProp(this, instanceId, key, fallback);
}

bool RPGActor::setStatusProp(int instanceId, const std::string &key, const std::string &value) {
    return StatusSystem::setProp(this, instanceId, key, value);
}

int RPGActor::applyBuff(const std::string &effectId, const std::string &source) {
    return applyEffect(effectId, source);
}

bool RPGActor::removeBuff(int instanceId) { return removeStatus(instanceId); }

int RPGActor::removeBuffByEffect(const std::string &effectId) {
    return removeStatusByEffect(effectId);
}

int RPGActor::removeBuffBySource(const std::string &source) {
    return removeStatusBySource(source);
}

int RPGActor::removeBuffByTag(const std::string &tag) { return removeStatusByTag(tag); }

bool RPGActor::hasBuff(const std::string &effectId) { return hasEffect(effectId); }

bool RPGActor::hasBuffTag(const std::string &tag) { return hasStatusTag(tag); }

int RPGActor::getBuffCount() { return getStatusCount(); }

std::string RPGActor::getBuffEffectId(int index) { return getStatusEffectId(index); }

int RPGActor::getBuffStacks(int index) { return getStatusStacks(index); }

float RPGActor::getBuffRemaining(int index) { return getStatusRemaining(index); }

int RPGActor::getBuffInstanceId(int index) { return getStatusInstanceId(index); }

std::string RPGActor::getBuffSource(int index) { return getStatusSource(index); }

std::string RPGActor::getBuffProp(int instanceId, const std::string &key,
                                   const std::string &fallback) {
    return getStatusProp(instanceId, key, fallback);
}

bool RPGActor::setBuffProp(int instanceId, const std::string &key, const std::string &value) {
    return setStatusProp(instanceId, key, value);
}

// ---- Skills ----

void RPGActor::learnSkill(const std::string &skillId) { SkillSystem::learn(this, skillId); }

bool RPGActor::knowsSkill(const std::string &skillId) { return SkillSystem::knows(this, skillId); }

bool RPGActor::forgetSkill(const std::string &skillId) { return SkillSystem::forget(this, skillId); }

float RPGActor::getSkillCooldown(const std::string &skillId) {
    return SkillSystem::getCooldownRemaining(this, skillId);
}

void RPGActor::setSkillCooldown(const std::string &skillId, float seconds) {
    SkillSystem::setCooldownRemaining(this, skillId, seconds);
}

bool RPGActor::canCastSkill(const std::string &skillId) {
    return SkillSystem::canCast(this, skillId, nullptr);
}

std::string RPGActor::canCastSkillReason(const std::string &skillId) {
    std::string reason;
    SkillSystem::canCast(this, skillId, &reason);
    return reason;
}

bool RPGActor::beginCastSkill(const std::string &skillId, RPGActor *target) {
    return SkillSystem::beginCast(this, skillId, target, nullptr);
}

void RPGActor::cancelCastSkill() { SkillSystem::cancelCast(this); }

bool RPGActor::isCastingSkill() { return SkillSystem::isCasting(this); }

std::string RPGActor::getCastingSkillId() { return SkillSystem::getCastingSkillId(this); }

float RPGActor::getCastProgress() { return SkillSystem::getCastProgress(this); }

int RPGActor::getLevel() { return LevelSystem::getLevel(this); }

double RPGActor::getXp() { return LevelSystem::getXp(this); }

double RPGActor::getXpToNext() { return LevelSystem::getXpToNext(this); }

void RPGActor::setXpToNext(double value) { LevelSystem::setXpToNext(this, value); }

eve::Result<void> RPGActor::restoreProgression(int level, double xp, double xpToNext) {
    return LevelSystem::restoreProgression(this, level, xp, xpToNext);
}

bool RPGActor::gainXp(double amount) { return LevelSystem::gainXp(this, amount); }

double RPGActor::getCurrent(const std::string &resource) {
    return VitalsSystem::getCurrent(this, resource);
}

double RPGActor::getMax(const std::string &resource) {
    return VitalsSystem::getMax(this, resource);
}

void RPGActor::setCurrent(const std::string &resource, double value) {
    VitalsSystem::setCurrent(this, resource, value);
}

double RPGActor::takeDamage(const std::string &resource, double amount,
                            const std::string &source) {
    return VitalsSystem::takeDamage(this, resource, amount, source);
}

double RPGActor::heal(const std::string &resource, double amount) {
    return VitalsSystem::heal(this, resource, amount);
}

void RPGActor::revive(const std::string &resource, double amount) {
    VitalsSystem::revive(this, resource, amount);
}

bool RPGActor::isDead(const std::string &resource) {
    return VitalsSystem::isDead(this, resource);
}

int RPGActor::applyTrait(const std::string &traitId, const std::string &source) {
    return TraitSystem::apply(this, traitId, source);
}

bool RPGActor::removeTrait(int instanceId) { return TraitSystem::remove(this, instanceId); }

int RPGActor::removeTraitsBySource(const std::string &source) {
    return TraitSystem::removeBySource(this, source);
}

int RPGActor::removeTraitsByTrait(const std::string &traitId) {
    return TraitSystem::removeByTrait(this, traitId);
}

bool RPGActor::hasTrait(const std::string &traitId) { return TraitSystem::hasTrait(this, traitId); }

int RPGActor::getTraitCount() { return TraitSystem::getCount(this); }

int RPGActor::getTraitInstanceIdAt(int index) {
    return TraitSystem::getInstanceIdAt(this, index);
}

std::string RPGActor::getTraitIdAt(int index) { return TraitSystem::getTraitIdAt(this, index); }

std::string RPGActor::getTraitSourceAt(int index) {
    return TraitSystem::getSourceAt(this, index);
}

double RPGActor::getParamRate(const std::string &param) {
    return TraitSystem::getParamRate(this, param);
}

double RPGActor::getElementRate(const std::string &element) {
    return TraitSystem::getElementRate(this, element);
}

double RPGActor::getStateRate(const std::string &stateId) {
    return TraitSystem::getStateRate(this, stateId);
}

bool RPGActor::isStateResist(const std::string &stateId) {
    return TraitSystem::isStateResist(this, stateId);
}

double RPGActor::getExParam(const std::string &exParam) {
    return TraitSystem::getExParam(this, exParam);
}

double RPGActor::getAttackSpeed() { return TraitSystem::getAttackSpeed(this); }

int RPGActor::getAttackTimesAdd() { return TraitSystem::getAttackTimesAdd(this); }

std::vector<std::string> RPGActor::getAttackElements() {
    return TraitSystem::getAttackElements(this);
}

std::vector<std::string> RPGActor::getAttackStates() {
    return TraitSystem::getAttackStates(this);
}

bool RPGActor::setClass(const std::string &classId) { return ClassSystem::setClass(this, classId); }

std::string RPGActor::getClassId() { return ClassSystem::getClassId(this); }

bool RPGActor::hasClass(const std::string &classId) {
    return ClassSystem::hasClass(this, classId);
}

int RPGActor::checkLevelSkills() { return ClassSystem::checkLevelSkills(this); }

int RPGActor::getClassLearnCount() { return ClassSystem::getLearnCount(this); }

std::string RPGActor::getClassLearnSkillIdAt(int index) {
    return ClassSystem::getLearnSkillIdAt(this, index);
}

int RPGActor::getClassLearnLevelAt(int index) {
    return ClassSystem::getLearnLevelAt(this, index);
}

}  // namespace eve::rpg
