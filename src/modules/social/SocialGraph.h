#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace eve::social {

/** @brief Stable, game-defined identifier used by the social graph. */
using EntityId = std::string;

/** @brief Immutable description of one social graph mutation. */
struct SocialChangeEvent {
    std::uint64_t sequence = 0;
    std::string   action;
    EntityId      source;
    EntityId      target;
    std::string   type;
    double        oldValue = 0.0;
    double        newValue = 0.0;
};

/**
 * @brief Indexed ownership, control, assignment, and typed-relation graph.
 *
 * IDs are opaque. String IDs are stored verbatim; integer helpers use a reserved
 * representation so integer 12 cannot collide with the string "12".
 */
class SocialGraph {
public:
    /** @brief Converts a stable integer ID to its canonical graph key. */
    static EntityId integerId(std::int64_t id);

    /** @brief Sets an entity's owner; an empty owner removes ownership. */
    bool setOwner(const EntityId& entity, const EntityId& owner);
    /** @brief Returns an entity's owner, or an empty string when absent. */
    EntityId ownerOf(const EntityId& entity) const;
    /** @brief Returns entities owned by owner in deterministic ID order. */
    std::vector<EntityId> ownedBy(const EntityId& owner) const;

    /** @brief Sets an entity's controller; an empty controller removes control. */
    bool setController(const EntityId& entity, const EntityId& controller);
    /** @brief Returns an entity's controller, or an empty string when absent. */
    EntityId controllerOf(const EntityId& entity) const;
    /** @brief Returns entities controlled by controller in deterministic ID order. */
    std::vector<EntityId> controlledBy(const EntityId& controller) const;

    /** @brief Adds a role-based source-to-target assignment. */
    bool assign(const EntityId& source, const std::string& role, const EntityId& target);
    /** @brief Removes a role-based source-to-target assignment. */
    bool unassign(const EntityId& source, const std::string& role, const EntityId& target);
    /** @brief Tests whether an exact assignment exists. */
    bool isAssigned(const EntityId& source, const std::string& role, const EntityId& target) const;
    /** @brief Returns targets assigned from source with role in deterministic order. */
    std::vector<EntityId> targetsOf(const EntityId& source, const std::string& role) const;
    /** @brief Returns sources assigned to target with role in deterministic order. */
    std::vector<EntityId> assigneesOf(const EntityId& target, const std::string& role) const;

    /** @brief Sets a directed typed relation weight, creating the edge if needed. */
    bool setRelation(const EntityId& source, const EntityId& target, const std::string& type, double value);
    /** @brief Adds delta to a directed typed relation and returns its new weight. */
    double addRelation(const EntityId& source, const EntityId& target, const std::string& type, double delta);
    /** @brief Creates a directed typed relation with weight 1. */
    bool link(const EntityId& source, const EntityId& target, const std::string& type);
    /** @brief Removes an exact directed typed relation. */
    bool removeRelation(const EntityId& source, const EntityId& target, const std::string& type);
    /** @brief Tests whether an exact directed typed relation exists. */
    bool hasRelation(const EntityId& source, const EntityId& target, const std::string& type) const;
    /** @brief Gets an exact relation weight, or fallback when absent. */
    double relation(const EntityId& source, const EntityId& target, const std::string& type,
                    double fallback = 0.0) const;
    /** @brief Returns outgoing relation targets filtered by type and minimum weight. */
    std::vector<EntityId> relationTargets(const EntityId& source, const std::string& type, double minimum = 0.0) const;
    /** @brief Returns incoming relation sources filtered by type and minimum weight. */
    std::vector<EntityId> relationSources(const EntityId& target, const std::string& type, double minimum = 0.0) const;

    /** @brief Removes an entity and every incoming/outgoing graph reference to it. */
    bool removeEntity(const EntityId& entity);
    /** @brief Clears all graph state and pending events. */
    void clear();

    /** @brief Returns queued mutation events in sequence order. */
    const std::vector<SocialChangeEvent>& events() const { return events_; }
    /** @brief Clears queued events without changing graph state. */
    void clearEvents() { events_.clear(); }

private:
    using EntitySet = std::map<EntityId, bool>;
    using PairKey   = std::pair<EntityId, std::string>;
    using EdgeKey   = std::tuple<EntityId, EntityId, std::string>;

    void emit(std::string action, EntityId source = {}, EntityId target = {}, std::string type = {},
              double oldValue = 0.0, double newValue = 0.0);
    static std::vector<EntityId> values(const EntitySet* set);

    std::map<EntityId, EntityId>   owners_;
    std::map<EntityId, EntitySet>  owned_;
    std::map<EntityId, EntityId>   controllers_;
    std::map<EntityId, EntitySet>  controlled_;
    std::map<PairKey, EntitySet>   assignmentTargets_;
    std::map<PairKey, EntitySet>   assignmentSources_;
    std::map<EdgeKey, double>      relations_;
    std::map<PairKey, EntitySet>   relationTargets_;
    std::map<PairKey, EntitySet>   relationSources_;
    std::vector<SocialChangeEvent> events_;
    std::uint64_t                  nextSequence_ = 1;
};

}  // namespace eve::social
