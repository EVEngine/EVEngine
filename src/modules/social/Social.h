#pragma once

#include "common/Module.h"
#include "social/SocialGraph.h"

#include <string>

namespace eve::social {

/** @brief Script-facing module for generic social graph composition. */
class Social : public Module {
public:
    Module_REG(Social);
    Social()           = default;
    ~Social() override = default;

    /** @brief Returns the canonical graph key for an integer entity ID. */
    std::string idFromInt(int id) const;

    /** @brief Sets ownership between string IDs. */
    bool setOwner(const std::string& entity, const std::string& owner);
    /** @brief Sets ownership between integer IDs. */
    bool setOwnerInt(int entity, int owner);
    /** @brief Gets owner of a string ID. */
    std::string ownerOf(const std::string& entity) const;
    /** @brief Gets owner of an integer ID as a canonical key. */
    std::string ownerOfInt(int entity) const;
    /** @brief Gets count of deterministically ordered entities owned by owner. */
    int ownedCount(const std::string& owner) const;
    /** @brief Gets an owned entity by deterministic index. */
    std::string ownedAt(const std::string& owner, int index) const;

    /** @brief Sets control between string IDs. */
    bool setController(const std::string& entity, const std::string& controller);
    /** @brief Sets control between integer IDs. */
    bool setControllerInt(int entity, int controller);
    /** @brief Gets controller of a string ID. */
    std::string controllerOf(const std::string& entity) const;
    /** @brief Gets controller of an integer ID as a canonical key. */
    std::string controllerOfInt(int entity) const;
    /** @brief Gets count of deterministically ordered controlled entities. */
    int controlledCount(const std::string& controller) const;
    /** @brief Gets a controlled entity by deterministic index. */
    std::string controlledAt(const std::string& controller, int index) const;

    /** @brief Adds a role-based assignment between string IDs. */
    bool assign(const std::string& source, const std::string& role, const std::string& target);
    /** @brief Adds a role-based assignment between integer IDs. */
    bool assignInt(int source, const std::string& role, int target);
    /** @brief Removes an exact assignment. */
    bool unassign(const std::string& source, const std::string& role, const std::string& target);
    /** @brief Tests an exact assignment. */
    bool isAssigned(const std::string& source, const std::string& role, const std::string& target) const;
    /** @brief Gets target count for source and role. */
    int targetCount(const std::string& source, const std::string& role) const;
    /** @brief Gets an assignment target by deterministic index. */
    std::string targetAt(const std::string& source, const std::string& role, int index) const;
    /** @brief Gets assignee count for target and role. */
    int assigneeCount(const std::string& target, const std::string& role) const;
    /** @brief Gets an assignee by deterministic index. */
    std::string assigneeAt(const std::string& target, const std::string& role, int index) const;

    /** @brief Sets a directed typed relation between string IDs. */
    bool setRelation(const std::string& source, const std::string& target, const std::string& type, float value);
    /** @brief Sets a directed typed relation between integer IDs. */
    bool setRelationInt(int source, int target, const std::string& type, float value);
    /** @brief Adds to a relation and returns its new value. */
    float addRelation(const std::string& source, const std::string& target, const std::string& type, float delta);
    /** @brief Creates a weight-one directed typed relation. */
    bool link(const std::string& source, const std::string& target, const std::string& type);
    /** @brief Removes an exact relation. */
    bool removeRelation(const std::string& source, const std::string& target, const std::string& type);
    /** @brief Tests an exact relation. */
    bool hasRelation(const std::string& source, const std::string& target, const std::string& type) const;
    /** @brief Gets relation value or fallback. */
    float relation(const std::string& source, const std::string& target, const std::string& type, float fallback) const;
    /** @brief Gets outgoing relation target count after threshold filtering. */
    int relationTargetCount(const std::string& source, const std::string& type, float minimum) const;
    /** @brief Gets outgoing relation target by deterministic filtered index. */
    std::string relationTargetAt(const std::string& source, const std::string& type, float minimum, int index) const;
    /** @brief Gets incoming relation source count after threshold filtering. */
    int relationSourceCount(const std::string& target, const std::string& type, float minimum) const;
    /** @brief Gets incoming relation source by deterministic filtered index. */
    std::string relationSourceAt(const std::string& target, const std::string& type, float minimum, int index) const;

    /** @brief Removes a string-ID entity and every graph reference to it. */
    bool removeEntity(const std::string& entity);
    /** @brief Removes an integer-ID entity and every graph reference to it. */
    bool removeEntityInt(int entity);
    /** @brief Clears all graph state and events. */
    void clear();

    /** @brief Gets queued change event count. */
    int eventCount() const;
    /** @brief Gets event sequence by index, or zero when out of range. */
    int eventSequence(int index) const;
    /** @brief Gets event action by index. */
    std::string eventAction(int index) const;
    /** @brief Gets event source by index. */
    std::string eventSource(int index) const;
    /** @brief Gets event target by index. */
    std::string eventTarget(int index) const;
    /** @brief Gets event type/role by index. */
    std::string eventType(int index) const;
    /** @brief Gets event old numeric value by index. */
    float eventOldValue(int index) const;
    /** @brief Gets event new numeric value by index. */
    float eventNewValue(int index) const;
    /** @brief Clears queued events only. */
    void clearEvents();

    /** @brief Gives C++ callers direct access to this module's graph. */
    SocialGraph& graph() { return graph_; }

private:
    SocialGraph graph_;
};

}  // namespace eve::social
