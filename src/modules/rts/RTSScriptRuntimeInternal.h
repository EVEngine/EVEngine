#pragma once
#include <map>
#include <string>
#include "action/Action.h"
#include "crowd/Crowd.h"
#include "economy/EconomyLedgerResourceAccount.h"
#include "map/Fov.h"
#include "map/Pathfinder.h"
#include "rts/RTS.h"
#include "sensing/Sensing.h"
namespace eve::rts {
struct RTS::ScriptRuntime {
    struct EconomySlot {
        economy::EconomyLedger                ledger;
        economy::EconomyLedgerResourceAccount account{ledger};
    };

    struct PaidProduction {
        SubjectRef         producer;
        SubjectRef         resultSubject;
        std::string        kind;
        std::string        product;
        std::string        taskId;
        std::string        orderId;
        resource::CostSpec refund;
    };

    struct PaidConstruction {
        SubjectRef         building;
        SubjectRef         faction;
        resource::CostSpec cost;
    };

    struct Checkpoint {
        crowd::Crowd::Snapshot                                  crowdState;
        RTSStateSnapshot                                        roots;
        std::map<std::string, economy::EconomyLedger::Snapshot> economies;
        std::map<std::string, map::Fov::Snapshot>               fovs;
        std::map<std::string, SubjectRef>                       pendingProductionSubjects;
        std::vector<PaidProduction>                             paidProduction;
        std::vector<PaidConstruction>                           paidConstruction;
        RTSCommandLog                                           commandLog;
        std::vector<float>                                      navigationCosts;
        std::vector<float>                                      terrainElevations;
        std::uint64_t                                           nextTick             = 1;
        std::uint64_t                                           aiProductionSequence = 1;
    };

    action::ActionRuntime                               actions;
    ActionAdapter                                       adapter{actions};
    map::Pathfinder                                     pathfinder;
    crowd::Crowd                                        crowd;
    sensing::SensingWorld                               sensing;
    combat::DamageRuntime                               damage;
    definitions::DefinitionRegistry                     definitions;
    RTSCommandLog                                       commandLog;
    std::map<std::string, std::unique_ptr<EconomySlot>> economies;
    std::map<std::string, std::unique_ptr<map::Fov>>    fovs;
    std::map<std::string, SubjectRef>                   pendingProductionSubjects;
    std::vector<PaidProduction>                         paidProduction;
    std::vector<PaidConstruction>                       paidConstruction;
    std::map<std::string, Checkpoint>                   checkpoints;
    std::uint64_t                                       nextTick             = 1;
    std::uint64_t                                       aiProductionSequence = 1;
    bool                                                spawningProduction   = false;
    int                                                 width                = 0;
    int                                                 height               = 0;
    float                                               cellSize             = 1.0f;
    float                                               originX              = 0.0f;
    float                                               originY              = 0.0f;
    std::vector<float>                                  terrainElevations;
    bool                                                configured = false;
};

}  // namespace eve::rts
