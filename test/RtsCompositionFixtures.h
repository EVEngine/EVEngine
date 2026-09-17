#pragma once

#include "common/ECS.h"
#include "common/Identity.h"
#include "common/Module.h"
#include "common/Runtime.h"
#include "rts/RTS.h"
#include "rts/RTSAction.h"
#include "rts/RTSContent.h"
#include "rts/RTSEconomy.h"
#include "rts/RTSMatch.h"
#include "rts/RTSSystems.h"
#include "rts/RTSTech.h"

#include "action/Action.h"
#include "crowd/Crowd.h"
#include "definitions/Definitions.h"
#include "economy/Economy.h"
#include "map/Fov.h"
#include "map/Pathfinder.h"
#include "sensing/Sensing.h"
#include "weapon/WeaponDefinitionRuntime.h"
#include "weapon/WeaponSystem.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <type_traits>
#include <utility>
#include <vector>

namespace eve::test::rts {

using eve::rts::Building;
using eve::rts::CommandSpec;
using eve::rts::Faction;
using eve::rts::FormationKind;
using eve::rts::FormationSpec;
using eve::rts::Match;
using eve::rts::OrderKind;
using eve::rts::Player;
using eve::rts::ResourceNode;
using eve::rts::Unit;
using eve::rts::WorldPosition;

[[maybe_unused]] inline eve::SubjectRef subject(const char* text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

class Scout final : public Unit {
public:
    ENTITY(Scout, Unit)

    void release() override { ecs::DestroyEntity(this); }
};

class PendingRTSExecutor final : public eve::rts::IRTSActionExecutor {
public:
    eve::Result<eve::rts::ActionExecutionResult> execute(Unit&, const eve::rts::OrderRecord&,
                                                         const eve::SimulationStep&) override {
        return eve::Result<eve::rts::ActionExecutionResult>::success({eve::rts::ActionDisposition::Pending});
    }
};

class FireControlArmorRule final : public eve::combat::IDamageRule {
public:
    eve::SubjectRef armored;
    eve::SubjectRef light;

    eve::Result<eve::combat::DamageAmounts> evaluate(const eve::combat::DamageRequest& request,
                                                     const eve::combat::CombatState&   target) const override {
        const bool strong = (request.damageType == "damage.piercing" && target.subject == armored) ||
                            (request.damageType == "damage.explosive" && target.subject == light);
        const double factor = strong ? 2.0 : 0.5;
        return eve::Result<eve::combat::DamageAmounts>::success(
            {request.healthDamage * factor, request.poiseDamage * factor, 1.0});
    }
};

[[maybe_unused]] inline void initializeScout(Scout& scout) {
    scout.identity()->self = ecs::handle_of(&scout);
    (void)scout.motion();
    (void)scout.orders();
    (void)scout.action();
}

}  // namespace eve::test::rts

using namespace eve::test::rts;

static_assert(std::is_base_of_v<ecs::Entity, Unit>);
static_assert(std::is_base_of_v<ecs::Entity, Building>);
static_assert(std::is_base_of_v<ecs::Entity, Player>);
static_assert(std::is_base_of_v<ecs::Entity, Faction>);
static_assert(!std::is_base_of_v<Unit, Building>);
static_assert(!std::is_base_of_v<Building, Unit>);
static_assert(!std::is_base_of_v<Player, Faction>);
