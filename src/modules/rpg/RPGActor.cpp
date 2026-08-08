#include "rpg/RPGActor.h"
#include "rpg/AttributeSystem.h"
#include "rpg/StatusSystem.h"
#include "rpg/SkillSystem.h"

#include <algorithm>

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

std::string RPGActor::addAttributeModifier(const std::string &attribute, const std::string &source,
                                            const std::string &op, double value, int priority) {
    return AttributeSystem::addModifier(this, attribute, source, op, value, priority);
}

bool RPGActor::removeAttributeModifier(const std::string &attribute, const std::string &modifierId) {
    return AttributeSystem::removeModifier(this, attribute, modifierId);
}

int RPGActor::removeAttributeModifiersBySource(const std::string &attribute,
                                                const std::string &source) {
    return AttributeSystem::removeModifiersBySource(this, attribute, source);
}

int RPGActor::removeAllAttributeModifiersBySource(const std::string &source) {
    return AttributeSystem::removeAllModifiersBySource(this, source);
}

double RPGActor::getFinalAttribute(const std::string &attribute) {
    return AttributeSystem::getFinal(this, attribute);
}

// ---- Status ----

int RPGActor::applyEffect(const std::string &effectId, const std::string &source) {
    return StatusSystem::apply(this, effectId, source);
}

bool RPGActor::removeStatus(int instanceId) { return StatusSystem::remove(this, instanceId); }

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

int RPGActor::getStatusCount() { return StatusSystem::getActiveCount(this); }

std::string RPGActor::getStatusEffectId(int index) {
    return StatusSystem::getActiveEffectId(this, index);
}

int RPGActor::getStatusStacks(int index) { return StatusSystem::getActiveStacks(this, index); }

float RPGActor::getStatusRemaining(int index) { return StatusSystem::getActiveRemaining(this, index); }

int RPGActor::getStatusInstanceId(int index) { return StatusSystem::getActiveInstanceId(this, index); }

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

}  // namespace eve::rpg
