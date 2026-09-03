#pragma once

/**
 * @file RTSSnapshot.h
 * @brief Stable in-memory RTS root snapshots independent of ECS handles.
 */

#include "rts/RTSSystems.h"

#include <map>
#include <vector>

namespace eve::rts {

/** @brief Persisted worker projection with stable references to its assignments. */
struct RTSWorkerSnapshot {
    std::string resourceType;
    SubjectRef resourceNode;
    SubjectRef dropoff;
    float cargo = 0.0f;
    float capacity = 0.0f;
    float gatherRate = 0.0f;
    float buildRate = 1.0f;
    float repairRate = 0.0f;
    bool autoAssign = false;
};

/** @brief One module-owned unit snapshot; external provider links are intentionally excluded. */
struct RTSUnitSnapshot {
    SubjectRef subject;
    LogicalId definition;
    std::string displayName;
    attributes::AttributeProjectionSnapshot attributes;
    std::vector<std::string> tags;
    RTSEffectSnapshot effects;
    SubjectRef faction;
    Unit::Motion motion;
    Unit::Navigation navigation;
    Unit::Vision vision;
    RTSWorkerSnapshot worker;
    Unit::Combat combat;
    SubjectRef combatTarget;
    Unit::Durability durability;
    Unit::Shield shield;
    Unit::Veterancy veterancy;
    Unit::Command command;
    SubjectRef commandSource;
    SubjectRef commandUplink;
    Unit::Abilities abilities;
    SubjectRef abilityTarget;
    Unit::Capture capture;
    std::size_t containmentCapacity = 0;
    SubjectRef container;
    std::vector<SubjectRef> occupants;
    Unit::Supply supply;
    SubjectRef supplyTarget;
    SubjectRef convoyLeader;
    Unit::Morale morale;
    Unit::Artillery artillery;
    SubjectRef fireSupportRequester;
    SubjectRef observedFireSpotter;
    Unit::Tactics tactics;
    SubjectRef escortTarget;
    Unit::Technology technology;
    OrderComponent::Snapshot orders;
    std::map<std::string, SubjectRef> orderTargets;
};

/** @brief One module-owned building snapshot with stable RTS relationships. */
struct RTSBuildingSnapshot {
    SubjectRef subject;
    LogicalId definition;
    std::string displayName;
    std::vector<std::string> tags;
    RTSEffectSnapshot effects;
    SubjectRef faction;
    Building::Placement placement;
    Building::Construction construction;
    std::vector<SubjectRef> builders;
    Building::Integrity integrity;
    Building::Shield shield;
    Building::Capture capture;
    SubjectRef capturingFaction;
    Building::Dropoff dropoff;
    Building::Rally rally;
    SubjectRef rallyCommandTarget;
    SubjectRef rallyTransport;
    std::vector<SubjectRef> reinforcements;
    Building::Combat combat;
    SubjectRef combatTarget;
    SubjectRef airDefenseNetworkRoot;
    Building::Garrison garrison;
    std::vector<SubjectRef> occupants;
    Building::Supply supply;
    Building::Vision vision;
    Building::Technology technology;
    Building::Infrastructure infrastructure;
    Building::Command command;
    Building::IndirectFire indirectFire;
    std::string productionJson;
    OrderComponent::Snapshot orders;
    std::map<std::string, SubjectRef> orderTargets;
};

/** @brief One module-owned resource-node snapshot with stable worker assignments. */
struct RTSResourceNodeSnapshot {
    SubjectRef subject;
    std::string displayName;
    ResourceNode::Position position;
    ResourceNode::Stock stock;
    std::size_t workerCapacity = 1;
    std::vector<SubjectRef> workers;
};

/** @brief Player-local RTS selection; authority/economy/social providers remain external. */
struct RTSPlayerSnapshot {
    SubjectRef subject;
    std::string displayName;
    std::vector<SubjectRef> units;
    std::vector<SubjectRef> buildings;
};

/** @brief Faction-owned strategy, workforce, intelligence, technology, and stable membership. */
struct RTSFactionSnapshot {
    SubjectRef subject;
    std::string displayName;
    std::vector<SubjectRef> units;
    std::vector<SubjectRef> buildings;
    Faction::Strategy strategy;
    Faction::Workforce workforce;
    Faction::ProductionPolicy productionPolicy;
    Faction::Intel intel;
    Faction::Technology technology;
};

/** @brief Match participant projection without process-local faction links. */
struct RTSMatchParticipantSnapshot {
    SubjectRef faction;
    int team = 0;
    bool eliminated = false;
    bool surrendered = false;
    std::string reason;
};

/** @brief Independent RTS match state and retained deterministic events. */
struct RTSMatchSnapshot {
    SubjectRef subject;
    Match::Rules rules;
    std::vector<RTSMatchParticipantSnapshot> participants;
    Match::State state;
    Match::Events events;
};

/** @brief Complete RTS-owned root state; shared provider state remains with its canonical modules. */
struct RTSStateSnapshot {
    std::uint32_t version = 1;
    std::vector<RTSUnitSnapshot> units;
    std::vector<RTSBuildingSnapshot> buildings;
    std::vector<RTSResourceNodeSnapshot> resourceNodes;
    std::vector<RTSPlayerSnapshot> players;
    std::vector<RTSFactionSnapshot> factions;
    std::vector<RTSMatchSnapshot> matches;
    RTSProjectileSystemSnapshot projectiles;
};

}  // namespace eve::rts
