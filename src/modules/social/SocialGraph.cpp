#include "social/SocialGraph.h"

#include <algorithm>

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
