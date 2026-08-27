#include "rpg/AttributeSystem.h"
#include "rpg/RPGActor.h"

#include <utility>

namespace eve::rpg {

namespace {

eve::Diagnostic invalidArgument(std::string message, std::string path = {}) {
    return eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, std::move(message),
                                  std::move(path));
}

eve::Result<ModifierId> actorRequired(RPGActor*) {
    return eve::Result<ModifierId>::failure(
        invalidArgument("attribute operation requires an actor", "actor"));
}

eve::Result<void> actorRequiredVoid(RPGActor*) {
    return eve::Result<void>::failure(
        invalidArgument("attribute operation requires an actor", "actor"));
}

eve::Result<int> actorRequiredCount(RPGActor*) {
    return eve::Result<int>::failure(
        invalidArgument("attribute operation requires an actor", "actor"));
}

}  // namespace

AttributeOpTable& AttributeSystem::customOps() {
    static AttributeOpTable table;
    return table;
}

void AttributeSystem::setBase(RPGActor* actor, const std::string& attribute, double value) {
    if (actor) actor->attributes()->values.setBase(attribute, value);
}

double AttributeSystem::getBase(RPGActor* actor, const std::string& attribute) {
    return actor ? actor->attributes()->values.getBase(attribute) : 0.0;
}

void AttributeSystem::modifyBase(RPGActor* actor, const std::string& attribute, double delta) {
    if (actor) actor->attributes()->values.modifyBase(attribute, delta);
}

bool AttributeSystem::hasAttribute(RPGActor* actor, const std::string& attribute) {
    return actor && actor->attributes()->values.has(attribute);
}

eve::Result<ModifierId> AttributeSystem::addModifier(RPGActor* actor, AttributeModifier modifier) {
    if (!actor) return actorRequired(actor);
    return actor->attributes()->values.addModifier(std::move(modifier));
}

std::string AttributeSystem::addModifier(RPGActor* actor, const std::string& attribute,
                                         const std::string& source, const std::string& operation,
                                         double value, int priority) {
    if (!actor || attribute.empty()) return {};

    auto parsed = ::eve::attributes::parseAttributeOperation(operation, value);
    AttributeModifier modifier;
    modifier.attribute = attribute;
    modifier.source = source;
    modifier.priority = priority;
    if (parsed.ok()) {
        modifier.operation = parsed.value().operation;
        modifier.value = parsed.value().value;
    } else if (customOps().has(operation)) {
        parsed.ignore("RPG custom operation compatibility fallback");
        modifier.operation = AttributeOperation::Custom;
        modifier.policyId = operation;
        modifier.value = value;
    } else {
        parsed.ignore("unknown RPG attribute operation");
        return {};
    }

    auto result = addModifier(actor, std::move(modifier));
    if (!result.ok()) return {};
    return result.value();
}

eve::Result<void> AttributeSystem::removeModifier(RPGActor* actor, const ModifierId& modifierId) {
    if (!actor) return actorRequiredVoid(actor);
    return actor->attributes()->values.removeModifier(modifierId);
}

bool AttributeSystem::removeModifier(RPGActor* actor, const std::string& attribute,
                                     const std::string& modifierId) {
    if (!actor) return false;
    auto result = actor->attributes()->values.removeModifier(attribute, modifierId);
    return result.ok();
}

eve::Result<int> AttributeSystem::removeModifiersBySource(
    RPGActor* actor, const std::string& attribute, const std::string& source) {
    if (!actor) return actorRequiredCount(actor);
    return actor->attributes()->values.removeBySource(source, attribute);
}

int AttributeSystem::removeAllModifiersBySource(RPGActor* actor, const std::string& source) {
    auto result = actor ? actor->attributes()->values.removeBySource(source)
                        : actorRequiredCount(actor);
    return result.ok() ? result.value() : 0;
}

double AttributeSystem::getFinal(RPGActor* actor, const std::string& attribute) {
    return actor ? actor->attributes()->values.getFinal(attribute, 0.0, &customOps()) : 0.0;
}

void AttributeSystem::invalidate(RPGActor* actor, const std::string& attribute) {
    if (actor) actor->attributes()->values.invalidate(attribute);
}

}  // namespace eve::rpg
