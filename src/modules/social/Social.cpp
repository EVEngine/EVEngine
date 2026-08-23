#include "social/Social.h"

#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::social {
namespace {

std::string at(const std::vector<EntityId>& ids, int index) {
    return index < 0 || static_cast<std::size_t>(index) >= ids.size() ? std::string{} : ids[index];
}

const SocialChangeEvent* eventAt(const SocialGraph& graph, int index) {
    const auto& events = graph.events();
    return index < 0 || static_cast<std::size_t>(index) >= events.size() ? nullptr : &events[index];
}

}  // namespace

Module_IMPL(Social, new Social());

std::string Social::idFromInt(int id) const { return SocialGraph::integerId(id); }

bool Social::setOwner(const std::string& entity, const std::string& owner) { return graph_.setOwner(entity, owner); }
bool Social::setOwnerInt(int entity, int owner) {
    return graph_.setOwner(SocialGraph::integerId(entity), SocialGraph::integerId(owner));
}
std::string Social::ownerOf(const std::string& entity) const { return graph_.ownerOf(entity); }
std::string Social::ownerOfInt(int entity) const { return graph_.ownerOf(SocialGraph::integerId(entity)); }
int Social::ownedCount(const std::string& owner) const { return static_cast<int>(graph_.ownedBy(owner).size()); }
std::string Social::ownedAt(const std::string& owner, int index) const { return at(graph_.ownedBy(owner), index); }

bool Social::setController(const std::string& entity, const std::string& controller) {
    return graph_.setController(entity, controller);
}
bool Social::setControllerInt(int entity, int controller) {
    return graph_.setController(SocialGraph::integerId(entity), SocialGraph::integerId(controller));
}
std::string Social::controllerOf(const std::string& entity) const { return graph_.controllerOf(entity); }
std::string Social::controllerOfInt(int entity) const { return graph_.controllerOf(SocialGraph::integerId(entity)); }
int         Social::controlledCount(const std::string& controller) const {
    return static_cast<int>(graph_.controlledBy(controller).size());
}
std::string Social::controlledAt(const std::string& controller, int index) const {
    return at(graph_.controlledBy(controller), index);
}

bool Social::assign(const std::string& source, const std::string& role, const std::string& target) {
    return graph_.assign(source, role, target);
}
bool Social::assignInt(int source, const std::string& role, int target) {
    return graph_.assign(SocialGraph::integerId(source), role, SocialGraph::integerId(target));
}
bool Social::unassign(const std::string& source, const std::string& role, const std::string& target) {
    return graph_.unassign(source, role, target);
}
bool Social::isAssigned(const std::string& source, const std::string& role, const std::string& target) const {
    return graph_.isAssigned(source, role, target);
}
int Social::targetCount(const std::string& source, const std::string& role) const {
    return static_cast<int>(graph_.targetsOf(source, role).size());
}
std::string Social::targetAt(const std::string& source, const std::string& role, int index) const {
    return at(graph_.targetsOf(source, role), index);
}
int Social::assigneeCount(const std::string& target, const std::string& role) const {
    return static_cast<int>(graph_.assigneesOf(target, role).size());
}
std::string Social::assigneeAt(const std::string& target, const std::string& role, int index) const {
    return at(graph_.assigneesOf(target, role), index);
}

bool Social::setRelation(const std::string& source, const std::string& target, const std::string& type, float value) {
    return graph_.setRelation(source, target, type, value);
}
bool Social::setRelationInt(int source, int target, const std::string& type, float value) {
    return graph_.setRelation(SocialGraph::integerId(source), SocialGraph::integerId(target), type, value);
}
float Social::addRelation(const std::string& source, const std::string& target, const std::string& type, float delta) {
    return static_cast<float>(graph_.addRelation(source, target, type, delta));
}
bool Social::link(const std::string& source, const std::string& target, const std::string& type) {
    return graph_.link(source, target, type);
}
bool Social::removeRelation(const std::string& source, const std::string& target, const std::string& type) {
    return graph_.removeRelation(source, target, type);
}
bool Social::hasRelation(const std::string& source, const std::string& target, const std::string& type) const {
    return graph_.hasRelation(source, target, type);
}
float Social::relation(const std::string& source, const std::string& target, const std::string& type,
                       float fallback) const {
    return static_cast<float>(graph_.relation(source, target, type, fallback));
}
int Social::relationTargetCount(const std::string& source, const std::string& type, float minimum) const {
    return static_cast<int>(graph_.relationTargets(source, type, minimum).size());
}
std::string Social::relationTargetAt(const std::string& source, const std::string& type, float minimum,
                                     int index) const {
    return at(graph_.relationTargets(source, type, minimum), index);
}
int Social::relationSourceCount(const std::string& target, const std::string& type, float minimum) const {
    return static_cast<int>(graph_.relationSources(target, type, minimum).size());
}
std::string Social::relationSourceAt(const std::string& target, const std::string& type, float minimum,
                                     int index) const {
    return at(graph_.relationSources(target, type, minimum), index);
}

bool Social::removeEntity(const std::string& entity) { return graph_.removeEntity(entity); }
bool Social::removeEntityInt(int entity) { return graph_.removeEntity(SocialGraph::integerId(entity)); }
void Social::clear() { graph_.clear(); }

int Social::eventCount() const { return static_cast<int>(graph_.events().size()); }
int Social::eventSequence(int index) const {
    const auto* event = eventAt(graph_, index);
    return event ? static_cast<int>(event->sequence) : 0;
}
std::string Social::eventAction(int index) const {
    const auto* event = eventAt(graph_, index);
    return event ? event->action : std::string{};
}
std::string Social::eventSource(int index) const {
    const auto* event = eventAt(graph_, index);
    return event ? event->source : std::string{};
}
std::string Social::eventTarget(int index) const {
    const auto* event = eventAt(graph_, index);
    return event ? event->target : std::string{};
}
std::string Social::eventType(int index) const {
    const auto* event = eventAt(graph_, index);
    return event ? event->type : std::string{};
}
float Social::eventOldValue(int index) const {
    const auto* event = eventAt(graph_, index);
    return event ? static_cast<float>(event->oldValue) : 0.0F;
}
float Social::eventNewValue(int index) const {
    const auto* event = eventAt(graph_, index);
    return event ? static_cast<float>(event->newValue) : 0.0F;
}
void Social::clearEvents() { graph_.clearEvents(); }

void Social::expose(ssq::Table& table) {
    auto cls = table.addClass(name, Social::create, false);
    expose(cls);
}

void Social::expose(ssq::Class& cls) {
    cls.addFunc("getName", &Social::getName);
    cls.addFunc("idFromInt", &Social::idFromInt);
    cls.addFunc("setOwner", &Social::setOwner);
    cls.addFunc("setOwnerInt", &Social::setOwnerInt);
    cls.addFunc("ownerOf", &Social::ownerOf);
    cls.addFunc("ownerOfInt", &Social::ownerOfInt);
    cls.addFunc("ownedCount", &Social::ownedCount);
    cls.addFunc("ownedAt", &Social::ownedAt);
    cls.addFunc("setController", &Social::setController);
    cls.addFunc("setControllerInt", &Social::setControllerInt);
    cls.addFunc("controllerOf", &Social::controllerOf);
    cls.addFunc("controllerOfInt", &Social::controllerOfInt);
    cls.addFunc("controlledCount", &Social::controlledCount);
    cls.addFunc("controlledAt", &Social::controlledAt);
    cls.addFunc("assign", &Social::assign);
    cls.addFunc("assignInt", &Social::assignInt);
    cls.addFunc("unassign", &Social::unassign);
    cls.addFunc("isAssigned", &Social::isAssigned);
    cls.addFunc("targetCount", &Social::targetCount);
    cls.addFunc("targetAt", &Social::targetAt);
    cls.addFunc("assigneeCount", &Social::assigneeCount);
    cls.addFunc("assigneeAt", &Social::assigneeAt);
    cls.addFunc("setRelation", &Social::setRelation);
    cls.addFunc("setRelationInt", &Social::setRelationInt);
    cls.addFunc("addRelation", &Social::addRelation);
    cls.addFunc("link", &Social::link);
    cls.addFunc("removeRelation", &Social::removeRelation);
    cls.addFunc("hasRelation", &Social::hasRelation);
    cls.addFunc("relation", &Social::relation);
    cls.addFunc("relationTargetCount", &Social::relationTargetCount);
    cls.addFunc("relationTargetAt", &Social::relationTargetAt);
    cls.addFunc("relationSourceCount", &Social::relationSourceCount);
    cls.addFunc("relationSourceAt", &Social::relationSourceAt);
    cls.addFunc("removeEntity", &Social::removeEntity);
    cls.addFunc("removeEntityInt", &Social::removeEntityInt);
    cls.addFunc("clear", &Social::clear);
    cls.addFunc("eventCount", &Social::eventCount);
    cls.addFunc("eventSequence", &Social::eventSequence);
    cls.addFunc("eventAction", &Social::eventAction);
    cls.addFunc("eventSource", &Social::eventSource);
    cls.addFunc("eventTarget", &Social::eventTarget);
    cls.addFunc("eventType", &Social::eventType);
    cls.addFunc("eventOldValue", &Social::eventOldValue);
    cls.addFunc("eventNewValue", &Social::eventNewValue);
    cls.addFunc("clearEvents", &Social::clearEvents);
}

}  // namespace eve::social
