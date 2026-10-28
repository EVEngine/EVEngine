#include <algorithm>
#include <new>
#include <set>
#include <tuple>
#include "common/Profile.h"
#include "crowd/Crowd.h"
#include "rts/RTS.h"

namespace eve::rts {
Result<void> RTS::prepareUnitRemovalBatch(std::span<const ecs::EntityHandle> units) {
    EV_PROFILE_MODULE("rts", "RTS::prepareUnitRemovalBatch");
    // Scope: this facade's owned Unit handles; no ECS View is active.
    // Read Identity/Containment/Crowd/Weapon; write only the same typed references as removeUnitRoot.
    // Crowd removal is prepared before reference publication; structural release follows this method.
    try {
        using Key                    = std::tuple<ecs::Table*, std::type_index, std::uint32_t, std::uint32_t>;
        const auto               key = [](ecs::EntityHandle h) { return Key{h.table, h.type, h.id, h.generation}; };
        std::set<Key>            dead, weapons;
        crowd::AgentRemovalBatch agents;
        agents.agents.reserve(units.size());
        for (auto handle : units) {
            auto* unit = dynamic_cast<Unit*>(ecs::try_get(handle));
            if (!unit || !unit->containment()->occupants.empty())
                return Result<void>::failure(
                    Diagnostic::error(DiagnosticCode::Conflict,
                                      "Casualty batch requires current units with no occupants", "cleanup.units"));
            dead.insert(key(handle));
            weapons.insert(key(unit->weapon()->link.handle()));
            if (crowd_ && crowdLifetime_.isAlive() && unit->crowd()->link.isBound()) {
                const auto settings = unit->crowd();
                agents.agents.push_back({settings->runtimeAgent, settings->runtimeOwner});
            }
        }
        if (!agents.agents.empty()) {
            auto removed = crowd_->removeAgents(agents);
            if (!removed) return Result<void>::failure(removed.status());
        }
        const auto removed = [&](ecs::EntityHandle h) { return dead.contains(key(h)); };
        for (auto h : resourceNodes_)
            if (auto* node = dynamic_cast<ResourceNode*>(ecs::try_get(h)))
                std::erase_if(node->harvest()->workers, removed);
        for (auto h : buildings_)
            if (auto* building = dynamic_cast<Building*>(ecs::try_get(h))) {
                std::erase_if(building->construction()->builders, removed);
                std::erase_if(building->garrison()->occupants, removed);
                std::erase_if(building->rally()->reinforcements, removed);
                if (removed(building->rally()->transport)) {
                    building->rally()->transport       = {};
                    building->rally()->transportActive = false;
                }
                building->capture()->blockedByGarrison = !building->garrison()->occupants.empty();
            }
        for (auto h : units_)
            if (auto* unit = dynamic_cast<Unit*>(ecs::try_get(h))) {
                std::erase_if(unit->containment()->occupants, removed);
                if (unit->containment()->container.isBound() && removed(unit->containment()->container.handle()))
                    unit->containment()->container = {};
                if (removed(unit->combat()->target)) unit->combat()->target = {};
                if (removed(unit->tactics()->escortTarget)) unit->tactics()->escortTarget = {};
                if (removed(unit->supply()->assignedTarget)) unit->supply()->assignedTarget = {};
                if (removed(unit->supply()->convoyLeader)) unit->supply()->convoyLeader = {};
            }
        for (auto h : factions_)
            if (auto* faction = dynamic_cast<Faction*>(ecs::try_get(h)))
                std::erase_if(faction->members()->units, removed);
        for (auto h : players_)
            if (auto* player = dynamic_cast<Player*>(ecs::try_get(h)))
                std::erase_if(player->selection()->units, removed);
        std::erase_if(weapons_, [&](auto h) { return weapons.contains(key(h)); });
        std::erase_if(units_, removed);
        return Result<void>::success(Status::success(StatusCode::Applied));
    } catch (const std::bad_alloc&) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Failed, "Cannot prepare casualty cleanup storage", "cleanup.units"));
    }
}
}  // namespace eve::rts
