#include "social/Social.h"
#include <algorithm>
#include <simplesquirrel/simplesquirrel.hpp>
#include "social/SocialGraph.h"

// Social.cpp
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

// SocialGraph.cpp
namespace eve::social {
namespace {

bool valid(const EntityId& id) { return !id.empty(); }

template <typename Index, typename Key>
void eraseIndex(Index& index, const Key& key, const EntityId& value) {
    const auto found = index.find(key);
    if (found == index.end()) return;
    found->second.erase(value);
    if (found->second.empty()) index.erase(found);
}

}  // namespace

EntityId SocialGraph::integerId(std::int64_t id) { return "#" + std::to_string(id); }

void SocialGraph::emit(std::string action, EntityId source, EntityId target, std::string type, double oldValue,
                       double newValue) {
    events_.push_back({nextSequence_++, std::move(action), std::move(source), std::move(target), std::move(type),
                       oldValue, newValue});
}

std::vector<EntityId> SocialGraph::values(const EntitySet* set) {
    std::vector<EntityId> result;
    if (!set) return result;
    result.reserve(set->size());
    for (const auto& [id, ignored] : *set) result.push_back(id);
    return result;
}

bool SocialGraph::setOwner(const EntityId& entity, const EntityId& owner) {
    if (!valid(entity)) return false;
    const EntityId old = ownerOf(entity);
    if (old == owner) return false;
    if (!old.empty()) eraseIndex(owned_, old, entity);
    if (owner.empty())
        owners_.erase(entity);
    else {
        owners_[entity]       = owner;
        owned_[owner][entity] = true;
    }
    emit("owner_changed", entity, owner, {}, 0.0, 0.0);
    return true;
}

EntityId SocialGraph::ownerOf(const EntityId& entity) const {
    const auto found = owners_.find(entity);
    return found == owners_.end() ? EntityId{} : found->second;
}

std::vector<EntityId> SocialGraph::ownedBy(const EntityId& owner) const {
    const auto found = owned_.find(owner);
    return values(found == owned_.end() ? nullptr : &found->second);
}

bool SocialGraph::setController(const EntityId& entity, const EntityId& controller) {
    if (!valid(entity)) return false;
    const EntityId old = controllerOf(entity);
    if (old == controller) return false;
    if (!old.empty()) eraseIndex(controlled_, old, entity);
    if (controller.empty())
        controllers_.erase(entity);
    else {
        controllers_[entity]            = controller;
        controlled_[controller][entity] = true;
    }
    emit("controller_changed", entity, controller);
    return true;
}

EntityId SocialGraph::controllerOf(const EntityId& entity) const {
    const auto found = controllers_.find(entity);
    return found == controllers_.end() ? EntityId{} : found->second;
}

std::vector<EntityId> SocialGraph::controlledBy(const EntityId& controller) const {
    const auto found = controlled_.find(controller);
    return values(found == controlled_.end() ? nullptr : &found->second);
}

bool SocialGraph::assign(const EntityId& source, const std::string& role, const EntityId& target) {
    if (!valid(source) || !valid(target) || role.empty()) return false;
    auto& targets = assignmentTargets_[{source, role}];
    if (targets.contains(target)) return false;
    targets[target]                            = true;
    assignmentSources_[{target, role}][source] = true;
    emit("assignment_added", source, target, role);
    return true;
}

bool SocialGraph::unassign(const EntityId& source, const std::string& role, const EntityId& target) {
    const PairKey forward{source, role};
    const auto    found = assignmentTargets_.find(forward);
    if (found == assignmentTargets_.end() || !found->second.contains(target)) return false;
    eraseIndex(assignmentTargets_, forward, target);
    eraseIndex(assignmentSources_, PairKey{target, role}, source);
    emit("assignment_removed", source, target, role);
    return true;
}

bool SocialGraph::isAssigned(const EntityId& source, const std::string& role, const EntityId& target) const {
    const auto found = assignmentTargets_.find({source, role});
    return found != assignmentTargets_.end() && found->second.contains(target);
}

std::vector<EntityId> SocialGraph::targetsOf(const EntityId& source, const std::string& role) const {
    const auto found = assignmentTargets_.find({source, role});
    return values(found == assignmentTargets_.end() ? nullptr : &found->second);
}

std::vector<EntityId> SocialGraph::assigneesOf(const EntityId& target, const std::string& role) const {
    const auto found = assignmentSources_.find({target, role});
    return values(found == assignmentSources_.end() ? nullptr : &found->second);
}

bool SocialGraph::setRelation(const EntityId& source, const EntityId& target, const std::string& type, double value) {
    if (!valid(source) || !valid(target) || type.empty()) return false;
    const EdgeKey key{source, target, type};
    const auto    found = relations_.find(key);
    if (found != relations_.end() && found->second == value) return false;
    const double old                         = found == relations_.end() ? 0.0 : found->second;
    relations_[key]                          = value;
    relationTargets_[{source, type}][target] = true;
    relationSources_[{target, type}][source] = true;
    emit(found == relations_.end() ? "relation_added" : "relation_changed", source, target, type, old, value);
    return true;
}

double SocialGraph::addRelation(const EntityId& source, const EntityId& target, const std::string& type, double delta) {
    const double value = relation(source, target, type) + delta;
    setRelation(source, target, type, value);
    return value;
}

bool SocialGraph::link(const EntityId& source, const EntityId& target, const std::string& type) {
    return setRelation(source, target, type, 1.0);
}

bool SocialGraph::removeRelation(const EntityId& source, const EntityId& target, const std::string& type) {
    const EdgeKey key{source, target, type};
    const auto    found = relations_.find(key);
    if (found == relations_.end()) return false;
    const double old = found->second;
    relations_.erase(found);
    eraseIndex(relationTargets_, PairKey{source, type}, target);
    eraseIndex(relationSources_, PairKey{target, type}, source);
    emit("relation_removed", source, target, type, old, 0.0);
    return true;
}

bool SocialGraph::hasRelation(const EntityId& source, const EntityId& target, const std::string& type) const {
    return relations_.contains({source, target, type});
}

double SocialGraph::relation(const EntityId& source, const EntityId& target, const std::string& type,
                             double fallback) const {
    const auto found = relations_.find({source, target, type});
    return found == relations_.end() ? fallback : found->second;
}

std::vector<EntityId> SocialGraph::relationTargets(const EntityId& source, const std::string& type,
                                                   double minimum) const {
    std::vector<EntityId> result;
    const auto            found = relationTargets_.find({source, type});
    if (found == relationTargets_.end()) return result;
    for (const auto& [target, ignored] : found->second) {
        if (relation(source, target, type) >= minimum) result.push_back(target);
    }
    return result;
}

std::vector<EntityId> SocialGraph::relationSources(const EntityId& target, const std::string& type,
                                                   double minimum) const {
    std::vector<EntityId> result;
    const auto            found = relationSources_.find({target, type});
    if (found == relationSources_.end()) return result;
    for (const auto& [source, ignored] : found->second) {
        if (relation(source, target, type) >= minimum) result.push_back(source);
    }
    return result;
}

bool SocialGraph::removeEntity(const EntityId& entity) {
    if (!valid(entity)) return false;
    bool changed = setOwner(entity, {}) | setController(entity, {});

    const auto owned = ownedBy(entity);
    for (const auto& child : owned) changed = setOwner(child, {}) || changed;
    const auto controlled = controlledBy(entity);
    for (const auto& child : controlled) changed = setController(child, {}) || changed;

    std::vector<std::tuple<EntityId, std::string, EntityId>> assignments;
    for (const auto& [key, targets] : assignmentTargets_) {
        for (const auto& [target, ignored] : targets) {
            if (key.first == entity || target == entity) assignments.emplace_back(key.first, key.second, target);
        }
    }
    for (const auto& [source, role, target] : assignments) changed = unassign(source, role, target) || changed;

    std::vector<EdgeKey> relations;
    for (const auto& [key, value] : relations_) {
        if (std::get<0>(key) == entity || std::get<1>(key) == entity) relations.push_back(key);
    }
    for (const auto& [source, target, type] : relations) changed = removeRelation(source, target, type) || changed;

    if (changed) emit("entity_removed", entity);
    return changed;
}

void SocialGraph::clear() {
    owners_.clear();
    owned_.clear();
    controllers_.clear();
    controlled_.clear();
    assignmentTargets_.clear();
    assignmentSources_.clear();
    relations_.clear();
    relationTargets_.clear();
    relationSources_.clear();
    events_.clear();
    nextSequence_ = 1;
}

}  // namespace eve::social
