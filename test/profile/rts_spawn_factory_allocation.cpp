// eve-test-policy:
// {"schema":"evengine.test-policy","version":1,"role":"compile-contract","default":"module-composition","module":"rts_spawn"}
// Intercept ordinary process-local allocations only in an OBJECT consumer.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <simplesquirrel/simplesquirrel.hpp>
#include "action/Action.h"
#include "action/ActionCapture.h"
#include "attributes/AttributeProjectionCapture.h"
#include "combat/Damage.h"
#include "common/Capability.h"
#include "common/StateAccess.h"
#include "integration/rts_spawn/RTSCrowdRestoreFixture.h"
#include "integration/rts_spawn/RTSRootFileFixture.h"
#include "integration/rts_spawn/RTSSpawnFixture.h"
#include "integration/rts_spawn/RTSSpawnRestoreFixture.h"
#include "integration/rts_spawn/RTSWeaponRestoreFixture.h"
#include "rts/RTSActionCapture.h"
#include "rts/RTSAttributeCapture.h"
#include "rts/RTSCombatRestore.h"
#include "rts/RTSFrameRestore.h"
#include "rts/RTSOrderState.h"
#include "rts/RTSWeaponRestore.h"
#include "rts/spawn/RTSSpawnCapture.h"
#include "sensing/Sensing.h"
#include "sensing/SensingCapture.h"
#include "weapon/AmmoPoolRestore.h"
#include "weapon/Weapon.h"
#include "weapon/WeaponAttributes.h"
#include "weapon/WeaponCapture.h"
#include "weapon/WeaponGraphRestore.h"
#include "weapon/WeaponRestore.h"
#include "weapon/WeaponSystem.h"
namespace {
thread_local bool        measuring = false, injected = false;
thread_local std::size_t allocations = 0, failure = std::numeric_limits<std::size_t>::max();
enum class Stage {
    Prepare,
    Commit,
    Remove,
    Lifecycle,
    Capture,
    RootCapture,
    ActionCapture,
    ActionPrepare,
    ActionRestore,
    CorrelationSeal,
    CorrelationParse,
    CorrelationCapture,
    CorrelationPrepare,
    CorrelationRestore,
    CrowdCapture,
    CrowdPrepare,
    CrowdRestore,
    AttributeCapture,
    AttributePrepare,
    AttributeRestore,
    FramePrepare,
    FrameRestore,
    CombatPrepare,
    CombatRestore,
    SensingCapture,
    SensingPrepare,
    SensingRestore,
    WeaponPrepare,
    WeaponCommit,
    WeaponCommitCreate,
    RequestCapture,
    RequestPrepare,
    RequestRestore,
    SharedFlowCapture,
    SharedFlowPrepare,
    SharedFlowRestore,
    BodyCrowdSeal,
    BodyCrowdParse,
    BodyCrowdCapture,
    BodyCrowdPrepare,
    BodyCrowdRestore,
    BodyWeaponPrepare,
    BodyWeaponCommit,
    SourceFactory,
    SourceBind,
    UnitAttributeCapture,
    UnitAttributePrepare,
    UnitAttributeRestore,
    GraphPrepare,
    GraphCommit,
    GraphCommitCreate,
    PoolRestorePrepare,
    PoolRestoreCommit,
    PoolGraph,
    Pool
};
void require(bool value, const char* message, std::size_t index) {
    if (!value) {
        std::fprintf(stderr, "Factory allocation %zu: %s\n", index, message);
        std::exit(2);
    }
}
std::size_t weapons(ecs::Table& table) {
    std::size_t result = 0;
    auto        view   = ecs::View<eve::weapon::WeaponEntity>(table);
    for (auto it = view.begin(); it != view.end(); ++it) ++result;
    return result;
}
std::size_t members(test_rts_spawn::Fixture& fixture) {
    ecs::ScopedTable scope(*fixture.table);
    auto             view = ecs::View<eve::rts::Faction>(*fixture.table);
    return view.begin().entity()->members()->units.size();
}
std::size_t requestNavigationAttempt(Stage stage, std::size_t index, bool inject) {
    using namespace eve;
    std::optional<crowd::Crowd::Snapshot>    frozen;
    std::optional<crowd::Crowd::RestorePlan> prepared;
    std::size_t                              count       = 0;
    bool                                     published   = false;
    std::uint64_t                            mappedOwner = 0;
    ssq::VM                                  vm(1024, ssq::Libs::ALL);
    auto                                     eve = vm.addTable("eve");
    auto                                     cls = eve.addClass<crowd::Crowd>("Crowd");
    crowd::Crowd::expose(cls);
    vm.addFunc("attemptNative", [&](crowd::Crowd* source, crowd::Crowd* target, std::int64_t owner) {
        const crowd::FlowRequestRef ref{RuntimeHandle<crowd::CrowdFlowRequestTag>(0, 1),
                                        static_cast<std::uint64_t>(owner)};
        if (stage != Stage::RequestCapture) frozen.emplace(source->captureSnapshot().expect("source requests"));
        if (stage == Stage::RequestRestore) {
            prepared.emplace(target->prepareRestore(*frozen).expect("request candidate"));
            mappedOwner = prepared->remapFlowRequest(ref).expect("candidate request reference").owner;
        }
        crowd::AgentFrame before;
        source->copyAgentFrame(before).expect("source frame before attempt");
        bool success = false;
        failure      = index;
        allocations  = 0;
        injected     = false;
        measuring    = true;
        try {
            if (stage == Stage::RequestCapture) {
                auto result = source->captureSnapshot();
                if (result) {
                    frozen.emplace(std::move(result).takeValue());
                    success = true;
                }
            } else if (stage == Stage::RequestPrepare) {
                auto result = target->prepareRestore(*frozen);
                if (result) {
                    prepared.emplace(std::move(result).takeValue());
                    success = true;
                }
            } else {
                auto result = target->commitRestore(std::move(*prepared));
                success     = result.ok();
            }
        } catch (const std::bad_alloc&) {
        }
        measuring      = false;
        count          = allocations;
        const bool hit = injected;
        failure        = std::numeric_limits<std::size_t>::max();
        require(inject ? hit && !success : success, "native request allocation boundary", index);
        published = stage == Stage::RequestRestore && success;
        require(target->getAgentCount() == (published ? 1 : 0), "request attempt partially published agents", index);
        crowd::AgentFrame after;
        source->copyAgentFrame(after).expect("source frame after attempt");
        require(after.tick == before.tick && after.agents.size() == before.agents.size() &&
                    after.agents[0].reference == before.agents[0].reference &&
                    after.agents[0].state.flowGeneration == before.agents[0].state.flowGeneration,
                "request attempt changed source agent/selection", index);
        if (stage == Stage::RequestRestore) require(count == 0, "request publication allocated", index);
        return published;
    });
    vm.addFunc("finishNative", [&](crowd::Crowd* source, crowd::Crowd* target, std::int64_t owner) -> std::int64_t {
        if (!published) {
            if (!frozen) frozen.emplace(source->captureSnapshot().expect("request capture retry"));
            if (!prepared) prepared.emplace(target->prepareRestore(*frozen).expect("request prepare retry"));
            mappedOwner = prepared
                              ->remapFlowRequest(
                                  {RuntimeHandle<crowd::CrowdFlowRequestTag>(0, 1), static_cast<std::uint64_t>(owner)})
                              .expect("retry request reference")
                              .owner;
            target->commitRestore(std::move(*prepared)).expect("same request candidate retry");
        }
        return static_cast<std::int64_t>(mappedOwner);
    });
    vm.run(vm.compileSource(R"(
        local c=eve.Crowd(), t=eve.Crowd();
        c.resizeField(16,16,1.0,0.0,0.0); t.resizeField(3,3,1.0,0.0,0.0);
        local pending=c.requestFlowBuild(15,15).value; assert(c.advanceFlowRequest(pending,11).ok);
        local leased=c.requestFlowBuild(14,14).value;
        while(c.getFlowRequest(leased).value.state=="pending") assert(c.advanceFlowRequest(leased,1000000).ok);
        assert(c.acquireFlowRequestLease(leased).ok);
        local agent=c.agentRef(c.addNamedAgent("selected",1.5,1.5,0.0,0.1)).value;
        assert(c.updateAgents([{reference=agent,flowRequest=leased}]).ok);
        local original=t.requestFlowBuild(2,2).value;
        local published=attemptNative(c,t,pending.owner);
        if(!published) assert(t.getFlowRequest(original).value.state=="pending");
        else assert(!t.getFlowRequest(original).ok);
        local owner=finishNative(c,t,pending.owner);
        assert(owner!=pending.owner && owner!=original.owner);
        assert(!t.getFlowRequest(original).ok);
        local restored=clone pending, pin=clone leased;
        restored.owner=owner; pin.owner=owner;
        local expected=c.advanceFlowRequest(pending,3).value;
        local actual=t.advanceFlowRequest(restored,3).value;
        assert(actual.state==expected.state && actual.heapPops==expected.heapPops && actual.directionCells==expected.directionCells);
        local a=c.sampleFlowRequestLease(leased,1.5,1.5).value, b=t.sampleFlowRequestLease(pin,1.5,1.5).value;
        assert(a.state==b.state && a.x==b.x && a.y==b.y);
    )"));
    return count;
}
std::size_t bodyCrowdAttempt(Stage stage, std::size_t index, bool inject) {
    using namespace eve;
    const bool shared = stage == Stage::SharedFlowCapture || stage == Stage::SharedFlowPrepare ||
                        stage == Stage::SharedFlowRestore || stage == Stage::BodyCrowdSeal ||
                        stage == Stage::BodyCrowdParse;
    const bool captureStage = stage == Stage::BodyCrowdCapture || stage == Stage::SharedFlowCapture;
    const bool prepareStage = stage == Stage::BodyCrowdPrepare || stage == Stage::SharedFlowPrepare;
    const bool restoreStage = stage == Stage::BodyCrowdRestore || stage == Stage::SharedFlowRestore;
    std::optional<test_rts_spawn::SharedFlowFixture> flow;
    std::optional<test_rts_spawn::Fixture>           ordinary;
    if (shared)
        flow.emplace();
    else {
        ordinary.emplace(test_rts_spawn::crowdMoveCommand());
        ordinary->sourceWorld->advance({SimulationTick(1), Duration(0)}).expect("source body members");
        test_rts_spawn::crowdStep(*ordinary, 1);
    }
    auto&                            source     = shared ? static_cast<test_rts_spawn::Fixture&>(*flow) : *ordinary;
    const auto                       flowBefore = source.domain->getCrowdFlowNavigationStatus();
    const auto                       epoch      = source.request.source.owner.child("crowd-allocation");
    auto                             frozen     = source.domain->captureCrowd(epoch).expect("source Crowd scope");
    auto                             saved      = test_rts_spawn::freezeSpawn(source, epoch);
    test_rts_spawn::RestoredRTSSpawn target(saved);
    const auto before       = target.domain->canonicalStateJson().expect("target roots before Crowd attempt");
    const auto sourceBefore = source.domain->canonicalStateJson().expect("source roots before Crowd attempt");
    std::optional<rts::PreparedRTSCrowdRestore> prepared;
    if (restoreStage)
        prepared.emplace(rts::PreparedRTSCrowdRestore::prepare(*target.domain, frozen).expect("Crowd CPU candidate"));
    std::optional<SnapshotEnvelope> archive;
    std::string                     archiveBytes;
    const bool                      archiveStage = stage == Stage::BodyCrowdSeal || stage == Stage::BodyCrowdParse;
    if (archiveStage) {
        archive.emplace(
            frozen.seal(Revision(8), SimulationTick(1), test_rts_spawn::restoreHash).expect("Crowd baseline archive"));
        archiveBytes = serializeSnapshotEnvelope(*archive).expect("Crowd baseline wire");
    }
    bool success = false;
    failure      = index;
    allocations  = 0;
    injected     = false;
    measuring    = true;
    try {
        if (stage == Stage::BodyCrowdSeal) {
            auto result = frozen.seal(Revision(8), SimulationTick(1), test_rts_spawn::restoreHash);
            success     = result.ok();
        } else if (stage == Stage::BodyCrowdParse) {
            auto result = rts::RTSCrowdCapture::parse(*archive, epoch, test_rts_spawn::restoreHash);
            success     = result.ok();
        } else if (captureStage) {
            auto result = source.domain->captureCrowd(epoch);
            success     = result.ok();
        } else if (prepareStage) {
            auto result = rts::PreparedRTSCrowdRestore::prepare(*target.domain, frozen);
            if (result) {
                prepared.emplace(std::move(result).takeValue());
                success = true;
            }
        } else {
            auto result = target.domain->restoreCrowd(std::move(*prepared));
            success     = result.ok();
        }
    } catch (const std::bad_alloc&) {
    }
    measuring        = false;
    const auto count = allocations;
    const bool hit   = injected;
    failure          = std::numeric_limits<std::size_t>::max();
    require(inject ? hit && !success : success, "Crowd body allocation boundary", index);
    require(target.domain->canonicalStateJson().expect("target roots after attempt") == before,
            "Crowd attempt changed native roots", index);
    require(source.domain->canonicalStateJson().expect("source roots after attempt") == sourceBefore,
            "Crowd attempt changed source roots", index);
    require(target.core->lastConsumedTick() == SimulationTick(1), "Crowd attempt advanced core", index);
    const bool published = restoreStage && success;
    require(target.crowd.getAgentCount() == (published ? 2 : 0), "Crowd attempt partially published backend", index);
    {
        ecs::ScopedTable scope(*target.table);
        for (const auto& member : frozen.members()) {
            const auto settings = target.domain->findUnit(member.subject)->crowd();
            require(settings->link.isBound() == published, "Crowd attempt partially published Unit link", index);
            require(target.crowd.isAgentAlive({settings->runtimeAgent, settings->runtimeOwner}) == published,
                    "Crowd attempt leaked/missed agent binding", index);
        }
    }
    auto sourceAfter = source.domain->captureCrowd(epoch).expect("source unchanged native backend");
    require(sourceAfter.frame()->get().tick == frozen.frame()->get().tick, "Crowd capture advanced clock", index);
    for (std::size_t i = 0; i < frozen.frame()->get().agents.size(); ++i) {
        const auto& a = sourceAfter.frame()->get().agents[i];
        const auto& b = frozen.frame()->get().agents[i];
        require(a.reference == b.reference && a.state.x == b.state.x && a.state.y == b.state.y &&
                    a.state.vx == b.state.vx && a.state.vy == b.state.vy,
                "Crowd attempt changed source agents", index);
    }
    if (archiveStage) {
        require(
            serializeSnapshotEnvelope(
                frozen.seal(Revision(8), SimulationTick(1), test_rts_spawn::restoreHash).expect("Crowd source retry"))
                    .expect("Crowd source retry wire") == archiveBytes,
            "Crowd archive mutated source facts", index);
        auto parsed =
            rts::RTSCrowdCapture::parse(*archive, epoch, test_rts_spawn::restoreHash).expect("Crowd same input retry");
        require(
            serializeSnapshotEnvelope(
                parsed.seal(Revision(8), SimulationTick(1), test_rts_spawn::restoreHash).expect("Crowd parsed retry"))
                    .expect("Crowd parsed retry wire") == archiveBytes,
            "Crowd archive changed facts/graph", index);
        frozen = std::move(parsed);
    }
    if (!published) {
        if (!prepared)
            prepared.emplace(
                rts::PreparedRTSCrowdRestore::prepare(*target.domain, frozen).expect("Crowd prepare retry"));
        target.domain->restoreCrowd(std::move(*prepared)).expect("same Crowd candidate retry");
    }
    require(target.crowd.getAgentCount() == 2, "Crowd retry lost native agents", index);
    const auto flowAfter = target.domain->getCrowdFlowNavigationStatus();
    require(flowAfter.pendingGoals == flowBefore.pendingGoals && flowAfter.readyGoals == flowBefore.readyGoals &&
                flowAfter.prepared == flowBefore.prepared && flowAfter.cacheHits == flowBefore.cacheHits &&
                flowAfter.lastWork == flowBefore.lastWork && flowAfter.lastStarts == flowBefore.lastStarts,
            "Crowd restore lost shared-flow jobs/quota watermarks", index);
    if (restoreStage) require(count == 0, "Crowd body publication allocated", index);
    return count;
}
std::size_t bodyWeaponAttempt(Stage stage, std::size_t index, bool inject) {
    using namespace eve;
    auto saved = test_rts_spawn::freezeWeaponScope();
    require(saved.sourceWorld.expired(), "source weapon world retained", index);
    const auto wire =
        serializeSnapshotEnvelope(saved.weapons->seal(Revision(1), SimulationTick(1), test_rts_spawn::restoreHash)
                                      .expect("body graph freeze"))
            .expect("body graph text");
    const auto epoch = saved.weapons->saveEpoch();
    saved.weapons    = std::make_shared<const rts::RTSWeaponCapture>(
        rts::RTSWeaponCapture::parse(
            parseSnapshotEnvelope(wire, test_rts_spawn::restoreHash).expect("body graph input"), epoch,
            test_rts_spawn::restoreHash)
            .expect("body graph admission"));
    test_rts_spawn::RestoredRTSSpawn target(saved.session);
    ecs::ScopedTable                 scope(*target.table);
    const auto                       before = target.domain->canonicalStateJson().expect("body roots before attempt");
    const auto queries = cap::listenerCount<IStateQuery>(), mutations = cap::listenerCount<IStateMutation>();
    std::optional<rts::PreparedRTSWeaponRestore> prepared;
    std::optional<rts::RestoredRTSWeaponPools>   pools;
    if (stage == Stage::BodyWeaponCommit)
        prepared.emplace(
            rts::PreparedRTSWeaponRestore::prepare(*target.domain, *saved.weapons).expect("body CPU candidate"));
    bool success = false;
    failure      = index;
    allocations  = 0;
    injected     = false;
    measuring    = true;
    try {
        if (stage == Stage::BodyWeaponPrepare) {
            auto result = rts::PreparedRTSWeaponRestore::prepare(*target.domain, *saved.weapons);
            if (result) {
                prepared.emplace(std::move(result).takeValue());
                success = true;
            }
        } else {
            auto result = target.domain->restoreWeapons(std::move(*prepared));
            if (result) {
                pools.emplace(std::move(result).takeValue());
                success = true;
            }
        }
    } catch (const std::bad_alloc&) {
    }
    measuring        = false;
    const auto count = allocations;
    const bool hit   = injected;
    failure          = std::numeric_limits<std::size_t>::max();
    require(inject ? hit && !success : success, "body weapon allocation boundary", index);
    require(target.domain->canonicalStateJson().expect("after body attempt") == before,
            "body graph changed native roots", index);
    require(target.core->lastConsumedTick() == SimulationTick(1), "body graph advanced core", index);
    const bool published = stage == Stage::BodyWeaponCommit && success;
    if (!published) {
        require(weapons(*target.table) == 0, "provisional guns escaped rollback", index);
        auto view = ecs::View<weapon::AmmoPoolEntity>(*target.table);
        require(view.begin() == view.end(), "provisional pools escaped rollback", index);
        require(cap::listenerCount<IStateQuery>() == queries && cap::listenerCount<IStateMutation>() == mutations,
                "provisional attributes escaped rollback", index);
        for (const auto& member : saved.weapons->members()) {
            const bool bound = member.kind == rts::RTSWeaponBodyKind::Unit
                                   ? target.domain->findUnit(member.body)->weapon()->link.isBound()
                                   : target.domain->findBuilding(member.body)->weapon()->link.isBound();
            require(!bound, "body link published before graph acceptance", index);
        }
    }
    if (!published) {
        if (!prepared)
            prepared.emplace(
                rts::PreparedRTSWeaponRestore::prepare(*target.domain, *saved.weapons).expect("prepare retry"));
        pools.emplace(target.domain->restoreWeapons(std::move(*prepared)).expect("same body graph candidate retry"));
    }
    require(weapons(*target.table) == 2 && pools->pools().size() == 1, "graph ownership lost aliases", index);
    auto* first  = dynamic_cast<weapon::WeaponEntity*>(target.domain->findUnit(saved.first)->weapon()->link.resolve());
    auto* second = dynamic_cast<weapon::WeaponEntity*>(target.domain->findUnit(saved.second)->weapon()->link.resolve());
    require(first && second && first->state()->ammoPool.resolve() == second->state()->ammoPool.resolve(),
            "shared pool split during reconstruction", index);
    require(first->state()->ammoPool.resolve()->state()->count == 9 && first->state()->resource.reloading,
            "restore consumed ammunition/progress", index);
    require(target.domain->findBuilding(saved.building)->weapon()->link.resolve() == first, "body alias was duplicated",
            index);
    return count;
}
std::size_t sourceRestoreAttempt(Stage stage, std::size_t index, bool inject) {
    using namespace eve;
    test_rts_spawn::SavedRTSSpawn saved;
    {
        test_rts_spawn::Fixture source;
        source.sourceWorld->advance({SimulationTick(1), Duration(100000000)}).expect("actual source publication");
        saved = test_rts_spawn::freezeSpawn(source, source.request.source.owner.child("allocation-source"));
    }
    const auto epoch = saved.provenance->saveEpoch();
    const auto sourceText =
        serializeSnapshotEnvelope(saved.provenance->seal(Revision(1), SimulationTick(1), test_rts_spawn::restoreHash)
                                      .expect("source envelope"))
            .expect("source wire");
    saved.provenance = std::make_shared<const rts_spawn::RTSSpawnCapture>(
        rts_spawn::RTSSpawnCapture::parse(
            parseSnapshotEnvelope(sourceText, test_rts_spawn::restoreHash).expect("source input"), epoch,
            test_rts_spawn::restoreHash)
            .expect("source admission"));
    test_rts_spawn::RestoredRTSSpawn target(saved);
    const auto                       before = target.domain->canonicalStateJson().expect("original roots");
    const auto queries = cap::listenerCount<IStateQuery>(), mutations = cap::listenerCount<IStateMutation>();
    std::unique_ptr<rts_spawn::RTSSpawnFactory> prepared;
    bool                                        success = false;
    failure                                             = index;
    allocations                                         = 0;
    injected                                            = false;
    measuring                                           = true;
    try {
        if (stage == Stage::SourceFactory) {
            auto result = rts_spawn::RTSSpawnFactory::prepareRestored(*target.domain, saved.provenance);
            if (result) {
                prepared = std::move(result).takeValue();
                success  = true;
            }
        } else {
            auto result = target.factory->restoreSources(*target.core, {}, {});
            success     = result.ok();
        }
    } catch (const std::bad_alloc&) {
    }
    measuring        = false;
    const auto count = allocations;
    const bool hit   = injected;
    failure          = std::numeric_limits<std::size_t>::max();
    require(inject ? hit && !success : success, "source restoration allocation boundary", index);
    require(target.domain->canonicalStateJson().expect("after source attempt") == before,
            "source attempt mutated native roots", index);
    require(target.core->lastConsumedTick() == SimulationTick(1), "source attempt advanced core", index);
    require(cap::listenerCount<IStateQuery>() == queries && cap::listenerCount<IStateMutation>() == mutations,
            "source attempt changed capabilities", index);
    if (stage == Stage::SourceFactory || !success) {
        require(!target.factory->capture(saved.provenance->saveEpoch(), {}).ok(),
                "failed source staging published index", index);
        target.factory->restoreSources(*target.core, {}, {}).expect("same source candidate retry");
    }
    const auto& receipt = saved.provenance->receipts()[0];
    auto lease = target.core->resolveSavedSource(spawn::savedSpawnerKey(receipt.source)).expect("actual fresh source");
    spawn::BatchRequest request{receipt.operation, receipt.units, lease.source(), receipt.ordinal, lease};
    auto                replay = target.factory->prepare(request, receipt.positions).expect("restored receipt");
    auto&               token  = std::get<std::unique_ptr<spawn::IPreparedSpawn>>(replay);
    require(token->isPublished(), "restored receipt recreated bodies", index);
    token->commit().expect("receipt retry");
    require(target.factory->inspect(receipt.units[0].subject).expect("origin index").origin == lease.source(),
            "source did not rebind actual origin", index);
    require(target.domain->canonicalStateJson().expect("after source replay") == before,
            "receipt replay mutated native roots", index);
    return count;
}
std::size_t unitAttributeAttempt(Stage stage, std::size_t index, bool inject) {
    using namespace eve;
    test_rts_spawn::Fixture fixture;
    fixture.commit();
    ecs::ScopedTable  scope(*fixture.table);
    const std::string name(96, 'm');
    const auto        epoch = fixture.request.source.owner.child("full-unit-attributes");
    for (std::size_t i = 0; i < 2; ++i) {
        auto& values = fixture.domain->findUnit(fixture.request.units[i].subject)->attributes()->values;
        values.setBase(name, 20 + double(i)).expect("full native custom base");
        values.addModifier({std::string(96, 'i'), name, std::string(96, 's'), attributes::AttributeOperation::Add, 3})
            .expect("full native modifier");
    }
    std::optional<rts::RTSUnitAttributeCapture>         frozen;
    std::optional<rts::PreparedRTSUnitAttributeRestore> prepared;
    if (stage != Stage::UnitAttributeCapture) {
        frozen.emplace(fixture.domain->captureUnitAttributes(epoch).expect("source full scope"));
        for (std::size_t i = 0; i < 2; ++i)
            fixture.domain->findUnit(fixture.request.units[i].subject)
                ->attributes()
                ->values.setBase(name, 99 + double(i))
                .expect("old target values");
        if (stage == Stage::UnitAttributeRestore)
            prepared.emplace(
                rts::PreparedRTSUnitAttributeRestore::prepare(*fixture.domain, *frozen).expect("full candidate"));
    }
    std::array<Revision, 2> revisions;
    for (std::size_t i = 0; i < 2; ++i)
        revisions[i] = fixture.domain->findUnit(fixture.request.units[i].subject)
                           ->attributes()
                           ->values.capture()
                           .expect("old revision")
                           .revision();
    const auto queries = cap::listenerCount<IStateQuery>(), mutations = cap::listenerCount<IStateMutation>();
    failure      = index;
    allocations  = 0;
    injected     = false;
    measuring    = true;
    bool success = false;
    try {
        if (stage == Stage::UnitAttributeCapture) {
            auto captured = fixture.domain->captureUnitAttributes(epoch);
            if (captured) {
                frozen.emplace(std::move(captured).takeValue());
                success = true;
            }
        } else if (stage == Stage::UnitAttributePrepare) {
            auto candidate = rts::PreparedRTSUnitAttributeRestore::prepare(*fixture.domain, *frozen);
            if (candidate) {
                prepared.emplace(std::move(candidate).takeValue());
                success = true;
            }
        } else {
            auto result = fixture.domain->restoreUnitAttributes(std::move(*prepared));
            success     = result.ok();
        }
    } catch (const std::bad_alloc&) {
    }
    measuring        = false;
    const auto count = allocations;
    const bool hit   = injected;
    failure          = std::numeric_limits<std::size_t>::max();
    require(inject ? hit && !success : success, "native unit attributes allocation boundary", index);
    require(!fixture.table->is_deferred() && fixture.domain->unitCount() == 2, "attribute scope changed topology/View",
            index);
    require(queries == cap::listenerCount<IStateQuery>() && mutations == cap::listenerCount<IStateMutation>(),
            "attribute candidate changed capabilities", index);
    for (std::size_t i = 0; i < 2; ++i) {
        auto&        values   = fixture.domain->findUnit(fixture.request.units[i].subject)->attributes()->values;
        const bool   restored = stage == Stage::UnitAttributeRestore && success;
        const double expected = (stage == Stage::UnitAttributeCapture || restored ? 23 : 102) + double(i);
        require(values.getFinal(name).expect("post-attempt full value") == expected,
                "attribute attempt changed earlier member", index);
        if (!restored)
            require(values.capture().expect("post-attempt revision").revision() == revisions[i],
                    "attribute attempt changed revision", index);
    }
    if (!frozen) frozen.emplace(fixture.domain->captureUnitAttributes(epoch).expect("capture retry"));
    if (stage != Stage::UnitAttributeRestore || !success) {
        if (!prepared)
            prepared.emplace(
                rts::PreparedRTSUnitAttributeRestore::prepare(*fixture.domain, *frozen).expect("prepare retry"));
        fixture.domain->restoreUnitAttributes(std::move(*prepared)).expect("same candidate retry");
    }
    for (std::size_t i = 0; i < 2; ++i) {
        auto& values = fixture.domain->findUnit(fixture.request.units[i].subject)->attributes()->values;
        require(values.getFinal(name).expect("restored full value") == 23 + double(i),
                "native scope lost custom values/modifiers", index);
    }
    if (stage == Stage::UnitAttributeRestore) require(count == 0, "native full attribute commit allocated", index);
    return count;
}
std::size_t graphAttempt(Stage stage, std::size_t index, bool inject);
std::size_t attempt(Stage stage, std::size_t index, bool inject) {
    if (stage == Stage::RequestCapture || stage == Stage::RequestPrepare || stage == Stage::RequestRestore)
        return requestNavigationAttempt(stage, index, inject);
    if (stage == Stage::SharedFlowCapture || stage == Stage::SharedFlowPrepare || stage == Stage::SharedFlowRestore)
        return bodyCrowdAttempt(stage, index, inject);
    if (stage == Stage::BodyCrowdCapture || stage == Stage::BodyCrowdPrepare || stage == Stage::BodyCrowdRestore ||
        stage == Stage::BodyCrowdSeal || stage == Stage::BodyCrowdParse)
        return bodyCrowdAttempt(stage, index, inject);
    if (stage == Stage::BodyWeaponPrepare || stage == Stage::BodyWeaponCommit)
        return bodyWeaponAttempt(stage, index, inject);
    if (stage == Stage::SourceFactory || stage == Stage::SourceBind) return sourceRestoreAttempt(stage, index, inject);
    if (stage == Stage::UnitAttributeCapture || stage == Stage::UnitAttributePrepare ||
        stage == Stage::UnitAttributeRestore)
        return unitAttributeAttempt(stage, index, inject);
    if (stage == Stage::GraphPrepare || stage == Stage::GraphCommit || stage == Stage::GraphCommitCreate)
        return graphAttempt(stage, index, inject);
    using namespace eve;
    auto command    = std::make_shared<rts::CommandSpec>();
    command->kind   = rts::OrderKind::AttackMove;
    command->target = {20, 30};
    test_rts_spawn::Fixture                fixture(command);
    std::unique_ptr<spawn::IPreparedSpawn> token;
    if (stage == Stage::Commit) {
        auto candidate = fixture.factory->prepare(fixture.request, fixture.positions).expect("prepare commit fixture");
        token          = std::move(std::get<std::unique_ptr<spawn::IPreparedSpawn>>(candidate));
    }
    const bool correlationStage = stage == Stage::CorrelationSeal || stage == Stage::CorrelationParse ||
                                  stage == Stage::CorrelationCapture || stage == Stage::CorrelationPrepare ||
                                  stage == Stage::CorrelationRestore;
    const bool actionStage = correlationStage || stage == Stage::ActionCapture || stage == Stage::ActionPrepare ||
                             stage == Stage::ActionRestore;
    const bool crowdStage =
        stage == Stage::CrowdCapture || stage == Stage::CrowdPrepare || stage == Stage::CrowdRestore;
    const bool poolRestoreStage = stage == Stage::PoolRestorePrepare || stage == Stage::PoolRestoreCommit;
    const bool weaponRestoreStage =
        stage == Stage::WeaponPrepare || stage == Stage::WeaponCommit || stage == Stage::WeaponCommitCreate;
    const bool attributeStage =
        stage == Stage::AttributeCapture || stage == Stage::AttributePrepare || stage == Stage::AttributeRestore;
    const bool frameStage  = stage == Stage::FramePrepare || stage == Stage::FrameRestore;
    const bool combatStage = stage == Stage::CombatPrepare || stage == Stage::CombatRestore;
    const bool sensingStage =
        stage == Stage::SensingCapture || stage == Stage::SensingPrepare || stage == Stage::SensingRestore;
    if (stage == Stage::Remove || stage == Stage::Lifecycle || stage == Stage::Capture || stage == Stage::RootCapture ||
        stage == Stage::Pool || stage == Stage::PoolGraph || actionStage || crowdStage || sensingStage || combatStage ||
        frameStage || attributeStage || weaponRestoreStage || poolRestoreStage)
        fixture.commit();
    ecs::ScopedTable        ownerScope(*fixture.table);
    weapon::WeaponEntity*   gun      = nullptr;
    weapon::AmmoPoolEntity* previous = nullptr;
    weapon::AmmoPoolEntity* next     = nullptr;
    if (stage == Stage::Pool || stage == Stage::PoolGraph || poolRestoreStage) {
        gun = dynamic_cast<weapon::WeaponEntity*>(
            fixture.domain->findUnit(fixture.request.units[0].subject)->weapon()->link.resolve());
        previous                 = weapon::AmmoPoolEntity::createPool();
        next                     = weapon::AmmoPoolEntity::createPool();
        previous->state()->count = 5;
        next->state()->count     = 9;
        gun->state()->ammoPool   = weapon::AmmoPoolLink::bind(*gun, *previous).expect("original shared pool");
        if (stage == Stage::PoolGraph) {
            previous->identity()->id = next->identity()->id = "same-name";
            auto* otherGun                                  = dynamic_cast<weapon::WeaponEntity*>(
                fixture.domain->findUnit(fixture.request.units[1].subject)->weapon()->link.resolve());
            otherGun->state()->ammoPool =
                weapon::AmmoPoolLink::bind(*otherGun, *next).expect("distinct same-name pool");
        }
    }
    weapon::CapturedAmmoPool                       savedPoolRestore;
    weapon::AmmoPoolLink                           oldPoolRestoreLink;
    std::optional<weapon::PreparedAmmoPoolRestore> poolRestorePlan;
    std::optional<weapon::AmmoPoolRestoreReceipt>  poolPublished;
    if (poolRestoreStage) {
        oldPoolRestoreLink         = gun->state()->ammoPool;
        savedPoolRestore.identity  = {std::string(96, 'i'), std::string(96, 't')};
        savedPoolRestore.state     = {12, 40};
        savedPoolRestore.archiveId = fixture.request.source.owner.child("native-pool-restore");
        if (stage == Stage::PoolRestoreCommit)
            poolRestorePlan.emplace(weapon::PreparedAmmoPoolRestore::prepare(savedPoolRestore, fixture.table, *previous)
                                        .expect("pool baseline prepare"));
    }
    Revision selectedRevision = Revision::zero();
    if (stage == Stage::Capture || stage == Stage::RootCapture) {
        auto* unit                     = fixture.domain->findUnit(fixture.request.units[0].subject);
        auto* target                   = fixture.domain->findUnit(fixture.request.units[1].subject);
        gun                            = dynamic_cast<weapon::WeaponEntity*>(unit->weapon()->link.resolve());
        previous                       = weapon::AmmoPoolEntity::createPool();
        previous->identity()->id       = "allocation-shared-ammunition-pool";
        previous->identity()->ammoType = "allocation-shared-ammunition-type";
        previous->state()->count       = 5;
        for (const auto& request : fixture.request.units) {
            auto* member                 = fixture.domain->findUnit(request.subject);
            auto* memberGun              = dynamic_cast<weapon::WeaponEntity*>(member->weapon()->link.resolve());
            memberGun->state()->ammoPool = weapon::AmmoPoolLink::bind(*memberGun, *previous).expect("capture pool");
        }
        gun->state()->resource.value   = 0;
        gun->state()->resource.reserve = 7;
        weapon::WeaponSystem::startReload(*gun);
        weapon::WeaponSystem::update(*gun, .05f);
        gun->state()->lastRequest.targetHandle = ecs::handle_of(target);
        weapon::WeaponAttributeAdapter::setBase(*gun, "mana", 3).expect("capture selected base");
        weapon::WeaponAttributeAdapter::addModifier(*gun, "allocation-selected-modifier", "mana", "allocation-source",
                                                    attributes::AttributeOperation::Add, 2)
            .expect("capture selected modifier");
        selectedRevision = gun->attributes()->values.revision();
    }
    action::ActionRuntime                        actions, restoredActions;
    rts::ActionAdapter                           adapter(actions), restoredAdapter(restoredActions);
    std::optional<SnapshotEnvelope>              correlationEnvelope;
    std::optional<rts::RTSActionCapture>         frozenCorrelations;
    std::optional<rts::PreparedRTSActionRestore> preparedCorrelations;
    rts::OrderRecord                             correlatedOrder;
    correlatedOrder.id   = std::string(96, 'o');
    correlatedOrder.kind = rts::OrderKind::Move;
    std::optional<action::ActionCapture>         frozenActions;
    std::optional<action::PreparedActionRestore> preparedActions;
    action::ActionExecutionId                    actionId;
    std::vector<action::ActionEntityBinding>     actionBindings;
    const auto            actionEpoch = fixture.request.source.owner.child("action-checkpoint-allocation");
    sensing::SensingWorld sensed, restoredSensing;
    std::optional<sensing::SensingCapture>         frozenSensing;
    std::optional<sensing::PreparedSensingRestore> preparedSensing;
    std::string                                    sensingBefore, targetSensingBefore, queryBefore;
    sensing::QuerySpec                             sensingQuery;
    sensingQuery.axes     = sensing::QueryAxes::XYZ;
    sensingQuery.shape    = sensing::QuerySphere{0, 0, 0, 100};
    sensingQuery.maxRange = 100;
    const sensing::QueryOrigin sensingOrigin{0, 0, std::nullopt, 0, sensing::CoordinateSpace::World3D};
    rts::RTSCombatSnapshot     frozenCombat;
    combat::DamageRuntime      damage;
    std::optional<rts::PreparedRTSCombatRestore> preparedCombat;
    if (sensingStage || combatStage) {
        for (std::size_t i = 0; i < fixture.request.units.size(); ++i) {
            auto* unit = fixture.domain->findUnit(fixture.request.units[i].subject);
            sensed
                .upsertPosition(unit->identity()->subject.format(), unit->motion()->x, unit->motion()->y,
                                float(i) + .5f, "allocation-faction", "unit,allocation", "")
                .expect("actual subject facts");
            sensed.setZones(unit->identity()->subject.format(), "zone:allocation").expect("actual zones");
        }
        sensed.rebuildIndex(sensing::SpatialIndexKind::XYZ, 2.5f).expect("actual 3D index");
        sensed.query(sensingOrigin, sensingQuery).expect("actual indexed source query");
        sensingBefore = sensed.snapshotJson();
        queryBefore   = sensed.debugLastQueryJson();
        restoredSensing.upsert("old-target", 0, 0, "", "", "").expect("old sensing target");
        restoredSensing.rebuildIndex(sensing::SpatialIndexKind::XY, 4).expect("old index");
        targetSensingBefore = restoredSensing.snapshotJson();
        if (stage != Stage::SensingCapture) frozenSensing.emplace(sensed.capture().expect("sensing baseline freeze"));
        if (stage == Stage::SensingRestore)
            preparedSensing.emplace(
                sensing::PreparedSensingRestore::prepare(*frozenSensing).expect("sensing baseline prepare"));
    }
    if (combatStage) {
        for (const auto& unit : fixture.request.units) {
            frozenCombat.mirroredSubjects.insert(unit.subject.format());
            frozenCombat.blockedSubjects.insert(unit.subject.format());
        }
        fixture.domain->setCombatProviders(&restoredSensing, &damage);
        if (stage == Stage::CombatRestore)
            preparedCombat.emplace(rts::PreparedRTSCombatRestore::prepare(*fixture.domain, frozenCombat, *frozenSensing)
                                       .expect("joint baseline prepare"));
    }
    std::optional<weapon::WeaponCapture>         frozenWeapon;
    std::optional<weapon::PreparedWeaponRestore> preparedWeapon;
    weapon::WeaponEntity*                        restoredGun = nullptr;
    Revision                                     oldWeaponAttributeRevision;
    if (weaponRestoreStage) {
        restoredGun = dynamic_cast<weapon::WeaponEntity*>(
            fixture.domain->findUnit(fixture.request.units[0].subject)->weapon()->link.resolve());
        weapon::WeaponAttributeAdapter::setBase(*restoredGun, "mana", 3).expect("weapon saved attributes");
        weapon::WeaponAttributeAdapter::addModifier(*restoredGun, std::string(96, 'w'), "mana", std::string(96, 's'),
                                                    attributes::AttributeOperation::Add, 2)
            .expect("weapon saved modifier");
        const auto epoch = fixture.request.source.owner.child("weapon-restore-allocation");
        auto       pools = weapon::WeaponPoolBindings::capture(*fixture.table, epoch).expect("weapon saved graph");
        frozenWeapon.emplace(
            weapon::WeaponCapture::capture(*restoredGun, epoch, {}, pools).expect("weapon saved facts"));
        restoredGun->state()->cooldown = 7;
        if (stage == Stage::WeaponCommitCreate) {
            restoredGun->getComponentManager().getComponentBuffer<weapon::WeaponEntity::Attributes>()->reset_slot(
                restoredGun->id);
        } else {
            weapon::WeaponAttributeAdapter::setBase(*restoredGun, "mana", 99).expect("old weapon attributes");
            oldWeaponAttributeRevision = restoredGun->attributes()->values.revision();
        }
        if (stage != Stage::WeaponPrepare)
            preparedWeapon.emplace(
                weapon::PreparedWeaponRestore::prepare(*frozenWeapon, fixture.table, *restoredGun, {}, {})
                    .expect("weapon restore baseline prepare"));
    }
    std::optional<attributes::AttributeProjectionCapture>  frozenAttributes;
    std::optional<attributes::PreparedAttributeProjection> preparedAttributes;
    weapon::WeaponEntity*                                  attributeSource = nullptr;
    weapon::WeaponEntity*                                  attributeTarget = nullptr;
    Revision                                               attributeRevision, previousAttributeRevision;
    attributes::ModifierSequence                           attributeSequence = 0;
    if (attributeStage) {
        auto getGun = [&](std::size_t i) {
            return dynamic_cast<weapon::WeaponEntity*>(
                fixture.domain->findUnit(fixture.request.units[i].subject)->weapon()->link.resolve());
        };
        attributeSource = getGun(0);
        attributeTarget = getGun(1);
        weapon::WeaponAttributeAdapter::setBase(*attributeSource, "mana", 3).expect("source attribute base");
        for (int i = 0; i < 2; ++i)
            weapon::WeaponAttributeAdapter::addModifier(*attributeSource, std::string(96, 'm'), "mana",
                                                        std::string(96, 's'), attributes::AttributeOperation::Add, 2)
                .expect("source modifier gap");
        weapon::WeaponAttributeAdapter::setBase(*attributeTarget, "mana", 99).expect("old target attributes");
        attributeRevision         = attributeSource->attributes()->values.revision();
        attributeSequence         = attributeSource->attributes()->values.modifierAt(0)->sequence;
        previousAttributeRevision = attributeTarget->attributes()->values.revision();
        if (stage != Stage::AttributeCapture)
            frozenAttributes.emplace(attributeSource->attributes()->values.capture().expect("attribute baseline"));
        if (stage == Stage::AttributeRestore)
            preparedAttributes.emplace(attributes::PreparedAttributeProjection::prepare(*frozenAttributes)
                                           .expect("attribute candidate baseline"));
    }
    rts::RTSFrameState                          frozenFrame;
    std::optional<rts::PreparedRTSFrameRestore> preparedFrame;
    if (frameStage) {
        frozenFrame.damage.resize(1);
        frozenFrame.damage[0].request.damageType = std::string(96, 'd');
        frozenFrame.damage[0].request.target     = fixture.request.units[0].subject;
        frozenFrame.combat.resize(1);
        frozenFrame.combat[0].sequence = 1;
        frozenFrame.lifecycle.resize(1);
        frozenFrame.lifecycle[0].sequence     = 2;
        frozenFrame.lifecycle[0].event.detail = std::string(96, 'l');
        frozenFrame.sequence                  = 3;
        frozenFrame.preserveOnNextStep        = true;
        frozenFrame.gameplay.resize(1);
        frozenFrame.gameplay[0].sequence = 1;
        frozenFrame.gameplay[0].type     = std::string(96, 'g');
        frozenFrame.gameplay[0].payload  = Value(Value::Object{{"data", Value(std::string(128, 'p'))}});
        frozenFrame.nextGameplaySequence = 2;
        rts::RTSFrameState previousFrame;
        previousFrame.damage.resize(1);
        previousFrame.damage[0].sequence           = 4;
        previousFrame.damage[0].request.damageType = "old-target-damage";
        previousFrame.sequence                     = 5;
        previousFrame.gameplay.resize(1);
        previousFrame.gameplay[0].sequence = 9;
        previousFrame.gameplay[0].payload  = Value("old-target-payload");
        previousFrame.nextGameplaySequence = 10;
        fixture.domain
            ->restoreFrameState(rts::PreparedRTSFrameRestore::prepare(previousFrame).expect("old frame candidate"))
            .expect("old target frame");
        if (stage == Stage::FrameRestore)
            preparedFrame.emplace(rts::PreparedRTSFrameRestore::prepare(frozenFrame).expect("frame baseline prepare"));
    }
    crowd::Crowd                             restoredCrowd;
    std::optional<crowd::Crowd::Snapshot>    frozenCrowd;
    std::optional<crowd::Crowd::RestorePlan> preparedCrowd;
    crowd::AgentState                        crowdBefore;
    if (crowdStage) {
        fixture.crowd.resizeField(8, 8, 16.f, 0.f, 0.f);
        auto build = fixture.crowd.prepareFlowBuild(7, 7).expect("flow preparation");
        while (build.state() == crowd::Crowd::FlowBuildState::Pending)
            fixture.crowd.advanceFlowBuild(build, 16).expect("actual flow advancement");
        auto lease = fixture.crowd.acquireFlowField(std::move(build)).expect("actual cache publication");
        for (int slot = 0; slot < 2; ++slot) {
            crowd::AgentUpdate update;
            update.reference = fixture.crowd.agentRef(slot).expect("actual agent");
            update.flow      = lease;
            fixture.crowd.updateAgents({{update}}).expect("actual agent flow");
        }
        fixture.crowd.advance({SimulationTick(1), Duration(10000000), {}}).expect("actual motion");
        crowdBefore =
            fixture.crowd.readAgent(fixture.crowd.agentRef(0).expect("actual source")).expect("motion baseline");
        restoredCrowd.setClampToField(false);
        require(restoredCrowd.addNamedAgent("old-target", 77, 78, 0, 1) >= 0, "target fixture", index);
        if (stage != Stage::CrowdCapture)
            frozenCrowd.emplace(fixture.crowd.captureSnapshot().expect("crowd baseline freeze"));
        if (stage == Stage::CrowdRestore)
            preparedCrowd.emplace(restoredCrowd.prepareRestore(*frozenCrowd).expect("crowd baseline prepare"));
    }
    if (actionStage) {
        auto* source   = fixture.domain->findUnit(fixture.request.units[0].subject);
        auto* target   = fixture.domain->findUnit(fixture.request.units[1].subject);
        actionBindings = {{ecs::handle_of(source), source->identity()->subject},
                          {ecs::handle_of(target), target->identity()->subject}};
        action::ActionDefinition definition;
        definition.id                          = *LogicalId::parse("checkpoint:actual-domain-action");
        definition.timing                      = {Duration(10), Duration(50), Duration(20)};
        definition.targetingMode               = action::TargetingMode::Explicit;
        definition.metadata["checkpoint-data"] = std::string(96, 'd');
        action::ActionTimeline timeline;
        timeline.actionId = definition.id;
        timeline.duration = Duration(80);
        timeline.animationSections.push_back({*LogicalId::parse("section:checkpoint"),
                                              "memory://allocation-action-clip", Duration(0), Duration(80),
                                              Duration(0)});
        definition.timeline = timeline;
        action::ActionRequest request;
        request.actionId                         = definition.id;
        request.source                           = ecs::handle_of(source);
        request.sourceSubject                    = source->identity()->subject;
        request.targetEntities                   = {ecs::handle_of(target)};
        request.targetSubjects                   = {target->identity()->subject};
        request.parameters["checkpoint-request"] = std::string(96, 'r');
        actionId = actions.submit(definition, request).expect("actual action submission");
        actions.setSectionTimingOverride(actionId, 0, Duration(160)).expect("actual physical override");
        actions.advance(actionId, SimulationTick(1), Duration(10)).expect("actual windup advancement");
        if (correlationStage) {
            source->motion()->arrived = false;
            adapter.execute(*source, correlatedOrder, {SimulationTick(1), Duration(10)}).expect("actual pending order");
            if (stage != Stage::CorrelationCapture)
                frozenCorrelations.emplace(
                    adapter.capture(actionEpoch, fixture.table, actionBindings).expect("correlated baseline freeze"));
            if (stage == Stage::CorrelationSeal || stage == Stage::CorrelationParse)
                correlationEnvelope.emplace(
                    frozenCorrelations->seal(Revision(17), SimulationTick(1), test_rts_spawn::restoreHash)
                        .expect("correlation baseline envelope"));
            if (stage == Stage::CorrelationRestore)
                preparedCorrelations.emplace(
                    rts::PreparedRTSActionRestore::prepare(*frozenCorrelations, fixture.table, actionBindings)
                        .expect("correlated baseline prepare"));
        }
        if (stage != Stage::ActionCapture)
            frozenActions.emplace(
                actions.capture(actionEpoch, fixture.table, actionBindings).expect("action baseline freeze"));
        if (stage == Stage::ActionRestore)
            preparedActions.emplace(
                action::PreparedActionRestore::prepare(*frozenActions, fixture.table, actionBindings)
                    .expect("prepared actual action"));
    }
    const std::array      future{fixture.request.units[0].definition, fixture.request.units[0].definition};
    const auto            saveEpoch = fixture.request.source.owner.child("capture-allocation");
    spawn::RemovalRequest removal;
    removal.operation = fixture.request.operation.child("remove");
    removal.source    = fixture.request.source;
    removal.policy    = spawn::DetachPolicy::RemoveUnits;
    for (const auto& unit : fixture.request.units) removal.subjects.push_back(unit.subject);
    std::vector<rts::RTSFrameDamageEvent> events(1);
    events[0].outcome.target         = fixture.request.units[0].subject;
    events[0].outcome.previousHealth = 40;
    events[0].outcome.health         = 0;
    if (stage == Stage::Lifecycle) {
        ecs::ScopedTable scope(*fixture.table);
        fixture.domain->findUnit(events[0].outcome.target)->durability()->alive = false;
    }
    const auto rootBefore =
        stage == Stage::RootCapture ? fixture.domain->canonicalStateJson().expect("root baseline") : std::string{};
    const auto rootBuffers =
        stage == Stage::RootCapture
            ? fixture.domain->findUnit(fixture.request.units[0].subject)->getComponentManager().components.size()
            : 0;
    const auto queries = cap::listenerCount<IStateQuery>(), mutations = cap::listenerCount<IStateMutation>();
    bool       success = false;
    allocations        = 0;
    injected           = false;
    failure            = index;
    measuring          = true;
    try {
        if (stage == Stage::Prepare) {
            auto result = fixture.factory->prepare(fixture.request, fixture.positions);
            success     = result.ok();
        } else if (stage == Stage::Commit) {
            auto result = token->commit();
            success     = result && result.value() == spawn::CommitOutcome::Committed;
        } else if (stage == Stage::Remove) {
            auto result = fixture.factory->remove(removal);
            success     = result && result.value().completed == 2;
        } else if (stage == Stage::AttributeCapture) {
            auto result = attributeSource->attributes()->values.capture();
            success     = result.ok();
        } else if (stage == Stage::AttributePrepare) {
            auto result = attributes::PreparedAttributeProjection::prepare(*frozenAttributes);
            success     = result.ok();
        } else if (stage == Stage::AttributeRestore) {
            auto result = attributeTarget->attributes()->values.restore(std::move(*preparedAttributes),
                                                                        ecs::handle_of(attributeTarget));
            success     = result.ok();
        } else if (stage == Stage::FramePrepare) {
            auto result = rts::PreparedRTSFrameRestore::prepare(frozenFrame);
            success     = result.ok();
        } else if (stage == Stage::FrameRestore) {
            auto result = fixture.domain->restoreFrameState(std::move(*preparedFrame));
            success     = result.ok();
        } else if (stage == Stage::CombatPrepare) {
            auto result = rts::PreparedRTSCombatRestore::prepare(*fixture.domain, frozenCombat, *frozenSensing);
            success     = result.ok();
        } else if (stage == Stage::CombatRestore) {
            auto result = fixture.domain->restoreCombat(std::move(*preparedCombat));
            success     = result.ok();
        } else if (stage == Stage::SensingCapture) {
            auto result = sensed.capture();
            success     = result.ok();
        } else if (stage == Stage::SensingPrepare) {
            auto result = sensing::PreparedSensingRestore::prepare(*frozenSensing);
            success     = result.ok();
        } else if (stage == Stage::SensingRestore) {
            auto result = restoredSensing.restore(std::move(*preparedSensing));
            success     = result.ok();
        } else if (stage == Stage::CrowdCapture) {
            auto result = fixture.crowd.captureSnapshot();
            success     = result.ok();
        } else if (stage == Stage::CrowdPrepare) {
            auto result = restoredCrowd.prepareRestore(*frozenCrowd);
            success     = result.ok();
        } else if (stage == Stage::CrowdRestore) {
            auto result = restoredCrowd.commitRestore(std::move(*preparedCrowd));
            success     = result.ok();
        } else if (stage == Stage::CorrelationSeal) {
            auto result = frozenCorrelations->seal(Revision(17), SimulationTick(1), test_rts_spawn::restoreHash);
            success     = result.ok();
        } else if (stage == Stage::CorrelationParse) {
            auto result = rts::RTSActionCapture::parse(*correlationEnvelope, actionEpoch, test_rts_spawn::restoreHash);
            success     = result.ok();
        } else if (stage == Stage::CorrelationCapture) {
            auto result = adapter.capture(actionEpoch, fixture.table, actionBindings);
            success     = result.ok();
        } else if (stage == Stage::CorrelationPrepare) {
            auto result = rts::PreparedRTSActionRestore::prepare(*frozenCorrelations, fixture.table, actionBindings);
            success     = result.ok();
        } else if (stage == Stage::CorrelationRestore) {
            auto result = restoredAdapter.restore(std::move(*preparedCorrelations));
            success     = result.ok();
        } else if (stage == Stage::ActionCapture) {
            auto result = actions.capture(actionEpoch, fixture.table, actionBindings);
            success     = result.ok();
        } else if (stage == Stage::ActionPrepare) {
            auto result = action::PreparedActionRestore::prepare(*frozenActions, fixture.table, actionBindings);
            success     = result.ok();
        } else if (stage == Stage::ActionRestore) {
            auto result = restoredActions.restore(std::move(*preparedActions));
            success     = result.ok();
        } else if (stage == Stage::RootCapture) {
            auto result = fixture.domain->snapshotState();
            success     = result && result.value().units.size() == 2 && result.value().factions.size() == 1;
        } else if (stage == Stage::Capture) {
            auto result = fixture.factory->capture(saveEpoch, future);
            success     = result && result.value().units().size() == 2 && result.value().futureUnits().size() == 1;
        } else if (stage == Stage::WeaponPrepare) {
            auto result = weapon::PreparedWeaponRestore::prepare(*frozenWeapon, fixture.table, *restoredGun, {}, {});
            success     = result.ok();
        } else if (stage == Stage::WeaponCommit || stage == Stage::WeaponCommitCreate) {
            auto result = preparedWeapon->commit();
            success     = result.ok();
        } else if (stage == Stage::PoolRestorePrepare) {
            auto result = weapon::PreparedAmmoPoolRestore::prepare(savedPoolRestore, fixture.table, *previous);
            success     = result.ok();
        } else if (stage == Stage::PoolRestoreCommit) {
            auto result = poolRestorePlan->commit();
            success     = result.ok();
            if (result) poolPublished.emplace(std::move(result).takeValue());
        } else if (stage == Stage::PoolGraph) {
            auto result = weapon::WeaponPoolBindings::capture(*fixture.table, saveEpoch);
            success     = result && result.value().size() == 2;
        } else if (stage == Stage::Pool) {
            auto result = weapon::AmmoPoolLink::bind(*gun, *next);
            success     = result.ok();
            if (result) gun->state()->ammoPool = std::move(result).takeValue();
        } else {
            auto result = fixture.factory->collectLifecycle(events);
            success = result && result.value().size() == 1 && result.value()[0].reason == spawn::RemovalReason::Death;
        }
    } catch (const std::bad_alloc&) {
    }
    measuring        = false;
    const auto count = allocations;
    const bool hit   = injected;
    failure          = std::numeric_limits<std::size_t>::max();
    require(!inject || (hit && !success), "injection must abort the operation", index);
    require(inject || success, "baseline must complete", index);
    const auto expected = (stage == Stage::Remove || stage == Stage::Lifecycle || stage == Stage::Capture ||
                           stage == Stage::RootCapture || stage == Stage::Pool || stage == Stage::PoolGraph ||
                           actionStage || crowdStage || sensingStage || combatStage || frameStage || attributeStage ||
                           weaponRestoreStage || poolRestoreStage)
                              ? 2u
                              : 0u;
    const auto actual = (success && stage == Stage::Commit) ? 2u : (success && stage == Stage::Remove) ? 0u : expected;
    require(fixture.count() == actual && fixture.crowd.getAgentCount() == static_cast<int>(actual) &&
                weapons(*fixture.table) == actual && members(fixture) == actual,
            "native roots/projections/members diverged", index);
    auto population = fixture.sourceWorld->inspect(fixture.request.source).expect("population observation");
    require(population.alive == 0 && population.total == 0, "factory must not mutate core population ledger", index);
    if (inject) {
        require(cap::listenerCount<IStateQuery>() == queries && cap::listenerCount<IStateMutation>() == mutations,
                "failed operation changed attribute registrations", index);
        for (const auto& unit : fixture.request.units) {
            auto snapshot = fixture.factory->inspect(unit.subject);
            require(snapshot.ok() == (expected == 2), "factory index changed before full publication", index);
            if (snapshot)
                require(snapshot.value().origin == fixture.request.source, "failed removal/lifecycle released origin",
                        index);
        }
    }
    if (stage == Stage::Prepare || stage == Stage::Commit) {
        auto  candidate = fixture.factory->prepare(fixture.request, fixture.positions).expect("same-world retry");
        auto& retry     = std::get<std::unique_ptr<spawn::IPreparedSpawn>>(candidate);
        require(retry->isPublished() == (success && stage == Stage::Commit),
                "journal recorded an uncommitted operation", index);
        require(retry->commit().expect("retry publication") == spawn::CommitOutcome::Committed, "retry did not commit",
                index);
        require(fixture.count() == 2 && weapons(*fixture.table) == 2 && members(fixture) == 2, "retry lost roots",
                index);
        ecs::ScopedTable scope(*fixture.table);
        for (const auto& unit : fixture.request.units) {
            auto snapshot = fixture.factory->inspect(unit.subject).expect("published binding");
            require(snapshot.health == 40 && snapshot.origin == fixture.request.source, "published index is incomplete",
                    index);
            auto* native = fixture.domain->findUnit(unit.subject);
            require(native && native->orders()->values.orderCount() == 1, "initial command was lost/duplicated", index);
        }
    } else if (stage == Stage::Remove) {
        require(fixture.factory->remove(removal).expect("removal retry").completed == 2, "retry removal receipt",
                index);
        require(fixture.count() == 0 && fixture.crowd.getAgentCount() == 0 && weapons(*fixture.table) == 0 &&
                    members(fixture) == 0,
                "retry removal left native facts", index);
        auto  replay = fixture.factory->prepare(fixture.request, fixture.positions).expect("retained creation receipt");
        auto& confirmed = std::get<std::unique_ptr<spawn::IPreparedSpawn>>(replay);
        require(confirmed->isPublished(), "removal discarded creation receipt", index);
        confirmed->commit().expect("removed operation replay");
        require(fixture.count() == 0, "removed operation recreated units", index);
    } else if (weaponRestoreStage) {
        auto* buffer = restoredGun->getComponentManager().getComponentBuffer<weapon::WeaponEntity::Attributes>();
        if (stage == Stage::WeaponPrepare || !success) {
            require(restoredGun->state()->cooldown == 7, "failed weapon restore changed phase", index);
            if (stage == Stage::WeaponCommitCreate)
                require(!buffer || !buffer->has(restoredGun->id), "failed weapon restore left optional component",
                        index);
            else
                require(restoredGun->attributes()->values.revision() == oldWeaponAttributeRevision &&
                            restoredGun->attributes()->values.getFinal("mana").expect("old weapon attribute") == 101,
                        "failed weapon restore changed attributes", index);
            require(cap::listenerCount<IStateQuery>() == queries && cap::listenerCount<IStateMutation>() == mutations,
                    "failed weapon restore changed capabilities", index);
        }
        if (stage == Stage::WeaponPrepare || success)
            preparedWeapon.emplace(
                weapon::PreparedWeaponRestore::prepare(*frozenWeapon, fixture.table, *restoredGun, {}, {})
                    .expect("weapon prepare retry"));
        preparedWeapon->commit().expect("same weapon candidate retry");
        const auto& facts = frozenWeapon->facts()->get();
        require(restoredGun->state()->cooldown == facts.state.cooldown && restoredGun->state()->ownedStages &&
                    restoredGun->state()->stages == restoredGun->state()->ownedStages.get() &&
                    restoredGun->attributes()->values.revision() == facts.attributes->revision &&
                    restoredGun->attributes()->values.getFinal("mana").expect("restored weapon attribute") == 5,
                "weapon native facts diverged", index);
        if (stage == Stage::WeaponCommit) require(count == 0, "existing weapon publication allocated", index);
    } else if (poolRestoreStage) {
        if (stage == Stage::PoolRestorePrepare || !success)
            require(previous->state()->count == 5 && previous->state()->max == -1 &&
                        oldPoolRestoreLink.resolve() == previous,
                    "failed pool restore changed facts or lifetime", index);
        require(cap::listenerCount<IStateQuery>() == queries && cap::listenerCount<IStateMutation>() == mutations,
                "pool restoration changed capabilities", index);
        if (stage == Stage::PoolRestorePrepare)
            poolRestorePlan.emplace(weapon::PreparedAmmoPoolRestore::prepare(savedPoolRestore, fixture.table, *previous)
                                        .expect("pool prepare retry"));
        if (!poolPublished) poolPublished.emplace(poolRestorePlan->commit().expect("same pool candidate retry"));
        require(oldPoolRestoreLink.isStale() && poolPublished->pool.resolve() == previous &&
                    poolPublished->archiveId == savedPoolRestore.archiveId && previous->state()->count == 12 &&
                    previous->state()->max == 40 && previous->identity()->id == savedPoolRestore.identity.id &&
                    previous->identity()->ammoType == savedPoolRestore.identity.ammoType,
                "pool restoration lost facts or adopted old lifetime", index);
        gun->state()->ammoPool = poolPublished->pool;
        if (stage == Stage::PoolRestoreCommit) require(count == 0, "pool publication allocated", index);

    } else if (stage == Stage::PoolGraph) {
        require(!fixture.table->is_deferred() && previous->state()->count == 5 && next->state()->count == 9,
                "pool graph changed world or left a View active", index);
        auto pools = weapon::WeaponPoolBindings::capture(*fixture.table, saveEpoch).expect("pool graph retry");
        require(pools.size() == 2, "pool graph merged same names", index);
        std::array<PersistentId, 2> archive;
        for (std::size_t i = 0; i < 2; ++i) {
            auto* member = dynamic_cast<weapon::WeaponEntity*>(
                fixture.domain->findUnit(fixture.request.units[i].subject)->weapon()->link.resolve());
            auto captured =
                weapon::WeaponCapture::capture(*member, saveEpoch, {}, pools).expect("pool graph actual alias");
            archive[i] = captured.facts()->get().pool->archiveId;
        }
        require(!archive[0].isNil() && archive[0] != archive[1], "pool graph aliases diverged", index);
    } else if (attributeStage) {
        auto& source = attributeSource->attributes()->values;
        auto& target = attributeTarget->attributes()->values;
        require(source.revision() == attributeRevision && source.modifierAt(0)->sequence == attributeSequence &&
                    source.getFinal("mana").expect("source value") == 5,
                "attribute source changed", index);
        require(cap::listenerCount<IStateQuery>() == queries && cap::listenerCount<IStateMutation>() == mutations,
                "attribute candidate published capabilities", index);
        if (stage != Stage::AttributeRestore || !success)
            require(target.revision() == previousAttributeRevision &&
                        target.getFinal("mana").expect("old target value") == 99,
                    "failed attribute attempt published", index);
        auto facts = source.capture().expect("attribute capture retry");
        target
            .restore(attributes::PreparedAttributeProjection::prepare(facts).expect("attribute prepare retry"),
                     ecs::handle_of(attributeTarget))
            .expect("attribute exact publish retry");
        require(target.revision() == attributeRevision && target.initialized() &&
                    target.getFinal("mana").expect("restored value") == 5 &&
                    target.modifierAt(0)->sequence == attributeSequence,
                "attribute exact state lost", index);
        target.addModifier({"next", "mana", "source", attributes::AttributeOperation::Add, 1})
            .expect("native counter continuation");
        require(target.modifierAt(1)->sequence == attributeSequence + 1, "attribute counter renumbered", index);
        if (stage == Stage::AttributeRestore) require(count == 0, "attribute publish allocated", index);
    } else if (frameStage) {
        auto current = fixture.domain->captureFrameState().expect("frame observation");
        if (stage != Stage::FrameRestore || !success)
            require(current.damage.size() == 1 && current.combat.empty() && current.lifecycle.empty() &&
                        current.gameplay.size() == 1 && current.sequence == 5 && current.nextGameplaySequence == 10 &&
                        !current.preserveOnNextStep && current.damage[0].sequence == 4 &&
                        current.damage[0].request.damageType == "old-target-damage" &&
                        current.gameplay[0].sequence == 9 &&
                        current.gameplay[0].payload.toJson().expect("old target payload") == "\"old-target-payload\"",
                    "failed frame attempt partially published", index);
        auto retry = rts::PreparedRTSFrameRestore::prepare(frozenFrame).expect("frame prepare retry");
        fixture.domain->restoreFrameState(std::move(retry)).expect("frame publish retry");
        current = fixture.domain->captureFrameState().expect("published frame");
        require(current.damage.size() == 1 && current.combat.size() == 1 && current.lifecycle.size() == 1 &&
                    current.gameplay.size() == 1 && current.sequence == 3 && current.nextGameplaySequence == 2 &&
                    current.preserveOnNextStep &&
                    current.damage[0].request.damageType == frozenFrame.damage[0].request.damageType &&
                    current.lifecycle[0].event.detail == frozenFrame.lifecycle[0].event.detail &&
                    current.gameplay[0].payload.toJson().expect("published payload") ==
                        frozenFrame.gameplay[0].payload.toJson().expect("source payload"),
                "frame facts diverged", index);
        if (stage == Stage::FrameRestore) require(count == 0, "frame publication allocated", index);
    } else if (combatStage) {
        require(sensed.snapshotJson() == sensingBefore && sensed.debugLastQueryJson() == queryBefore,
                "joint preparation changed source", index);
        auto flags = fixture.domain->captureCombatState().expect("joint flags");
        if (stage != Stage::CombatRestore || !success) {
            require(restoredSensing.snapshotJson() == targetSensingBefore && flags.mirroredSubjects.empty() &&
                        flags.blockedSubjects.empty(),
                    "joint failure partially published", index);
        }
        auto retry = rts::PreparedRTSCombatRestore::prepare(*fixture.domain, frozenCombat, *frozenSensing)
                         .expect("joint prepare retry");
        fixture.domain->restoreCombat(std::move(retry)).expect("joint publish retry");
        flags = fixture.domain->captureCombatState().expect("joint published flags");
        require(restoredSensing.snapshotJson() == sensingBefore &&
                    flags.mirroredSubjects == frozenCombat.mirroredSubjects &&
                    flags.blockedSubjects == frozenCombat.blockedSubjects,
                "joint published facts diverged", index);
        require(restoredSensing.query(sensingOrigin, sensingQuery).expect("joint derived aliases").size() == 2,
                "joint index aliases lost", index);
        if (stage == Stage::CombatRestore) require(count == 0, "joint publication allocated", index);
        fixture.domain->setCombatProviders(nullptr, nullptr);
    } else if (sensingStage) {
        require(sensed.snapshotJson() == sensingBefore && sensed.debugLastQueryJson() == queryBefore,
                "sensing attempt changed source facts or observations", index);
        if (stage != Stage::SensingRestore || !success)
            require(restoredSensing.snapshotJson() == targetSensingBefore &&
                        restoredSensing.indexKind() == sensing::SpatialIndexKind::XY,
                    "failed sensing attempt replaced target", index);
        auto frozen = sensed.capture().expect("sensing freeze retry");
        restoredSensing.restore(sensing::PreparedSensingRestore::prepare(frozen).expect("sensing prepare retry"))
            .expect("sensing publish retry");
        require(restoredSensing.snapshotJson() == sensingBefore &&
                    restoredSensing.indexKind() == sensing::SpatialIndexKind::XYZ &&
                    restoredSensing.capture().expect("restored policy").cellSize() == 2.5f,
                "restored facts or index policy diverged", index);
        require(restoredSensing.query(sensingOrigin, sensingQuery).expect("restored index aliases").size() == 2,
                "restored index lost subjects", index);
        if (stage == Stage::SensingRestore) require(count == 0, "prepared sensing publication allocated", index);
    } else if (crowdStage) {
        const auto current =
            fixture.crowd.readAgent(fixture.crowd.agentRef(0).expect("source reference")).expect("source motion");
        require(current.x == crowdBefore.x && current.vx == crowdBefore.vx &&
                    current.flowGeneration == crowdBefore.flowGeneration,
                "crowd operation changed source motion or selected flow", index);
        require(!fixture.crowd.configureFlowCache(0).ok(), "crowd operation released original agent pins", index);
        if (stage != Stage::CrowdRestore || !success)
            require(restoredCrowd.getAgentCount() == 1 && restoredCrowd.resolveNamedAgent("old-target").ok(),
                    "failed operation replaced target", index);
        auto snapshot = fixture.crowd.captureSnapshot().expect("same-boundary crowd retry");
        auto retry    = restoredCrowd.prepareRestore(snapshot).expect("same-boundary crowd prepare retry");
        restoredCrowd.commitRestore(std::move(retry)).expect("crowd retry publish");
        const auto actual =
            restoredCrowd.readAgent(restoredCrowd.agentRef(0).expect("restored reference")).expect("restored motion");
        require(actual.x == crowdBefore.x && actual.vx == crowdBefore.vx &&
                    actual.flowGeneration == crowdBefore.flowGeneration,
                "restored motion or selected flow diverged", index);
        require(!restoredCrowd.configureFlowCache(0).ok(), "restored agents lost cache protection", index);
        for (int slot = 0; slot < 2; ++slot) {
            crowd::AgentUpdate update;
            update.reference = restoredCrowd.agentRef(slot).expect("restored binding");
            update.flow      = crowd::FlowLease{};
            restoredCrowd.updateAgents({{update}}).expect("explicit restored unbind");
        }
        restoredCrowd.configureFlowCache(0).expect("source snapshot must not pin restored eviction");
        if (stage == Stage::CrowdRestore) require(count == 0, "prepared Crowd publication allocated", index);
    } else if (actionStage) {
        const std::size_t expectedActions = correlationStage ? 2 : 1;
        require(actions.executionCount() == expectedActions && actions.find(actionId)->totalElapsed() == Duration(5),
                "action checkpoint operation changed source cursor", index);
        auto capture =
            actions.capture(actionEpoch, fixture.table, actionBindings).expect("same-boundary action capture retry");
        require(
            capture.nextExecutionId().value() == expectedActions + 1 && capture.executions().size() == expectedActions,
            "action capture changed id watermark or history", index);
        auto plan = action::PreparedActionRestore::prepare(capture, fixture.table, actionBindings)
                        .expect("same-world prepare retry");
        restoredActions.restore(std::move(plan)).expect("same-world publication retry");
        require(restoredActions.executionCount() == expectedActions &&
                    restoredActions.find(actionId)->totalElapsed() == Duration(5) &&
                    ecs::try_get(restoredActions.find(actionId)->request().targetEntities[0]) ==
                        fixture.domain->findUnit(fixture.request.units[1].subject),
                "restored native action cursor or target binding diverged", index);
        if (stage == Stage::ActionRestore) require(count == 0, "prepared action publication allocated", index);
        if (correlationStage) {
            auto correlated =
                adapter.capture(actionEpoch, fixture.table, actionBindings).expect("correlated retry freeze");
            require(correlated.pendingCount() == 1, "failed operation lost source order correlation", index);
            if (stage == Stage::CorrelationSeal || stage == Stage::CorrelationParse) {
                const auto text = serializeSnapshotEnvelope(
                                      correlated.seal(Revision(17), SimulationTick(1), test_rts_spawn::restoreHash)
                                          .expect("retry seal"))
                                      .expect("retry text");
                require(text == serializeSnapshotEnvelope(*correlationEnvelope).expect("baseline text"),
                        "failed correlation codec changed source facts", index);
                correlated = rts::RTSActionCapture::parse(
                                 parseSnapshotEnvelope(text, test_rts_spawn::restoreHash).expect("retry envelope"),
                                 actionEpoch, test_rts_spawn::restoreHash)
                                 .expect("retry parse");
            }
            auto retry = rts::PreparedRTSActionRestore::prepare(correlated, fixture.table, actionBindings)
                             .expect("correlated retry prepare");
            restoredAdapter.restore(std::move(retry)).expect("correlated retry publish");
            restoredAdapter
                .execute(*fixture.domain->findUnit(fixture.request.units[0].subject), correlatedOrder,
                         {SimulationTick(2), Duration(10)})
                .expect("continue same order");
            require(restoredActions.executionCount() == expectedActions, "restore resubmitted pending order", index);
            require(restoredAdapter.capture(actionEpoch, fixture.table, actionBindings)
                            .expect("after retry")
                            .pendingCount() == 1,
                    "restore lost pending order", index);
            if (stage == Stage::CorrelationRestore) require(count == 0, "correlated publication allocated", index);
        }
    } else if (stage == Stage::Capture || stage == Stage::RootCapture) {
        require(gun->state()->ammoPool.resolve() == previous && previous->state()->count == 5 &&
                    gun->state()->resource.reloading && gun->state()->resource.reloadProgress == .05f &&
                    gun->state()->resource.reserve == 7 && gun->attributes()->values.revision() == selectedRevision &&
                    gun->attributes()->values.modifierCount() == 1,
                "capture changed actual weapon/pool/attribute facts", index);
        if (stage == Stage::RootCapture) {
            require(fixture.domain->canonicalStateJson().expect("actual root after failed capture") == rootBefore &&
                        fixture.domain->findUnit(fixture.request.units[0].subject)
                                ->getComponentManager()
                                .components.size() == rootBuffers,
                    "root capture mutated components or deterministic state", index);
            auto roots = fixture.domain->snapshotState().expect("native root capture retry");
            require(roots.units.size() == 2 && roots.factions.size() == 1,
                    "native root retry omitted authoritative roots", index);
        }
        auto captured = fixture.factory->capture(saveEpoch, future).expect("same-boundary capture retry");
        require(captured.units().size() == 2 && captured.futureUnits().size() == 1 && captured.receipts().size() == 1 &&
                    captured.lifecycleSequence() == 0,
                "capture retry lost facts or advanced lifecycle", index);
        for (const auto& member : captured.units())
            require(member.weapon != nullptr, "capture omitted live weapon participant", index);
        for (const auto& member : captured.units()) {
            const auto& facts = member.weapon->facts()->get();
            require(facts.pool && facts.pool->state.count == 5 && facts.request.targetHandle.table == nullptr,
                    "capture retry lost shared pool or retained runtime target", index);
            if (member.subject == fixture.request.units[0].subject)
                require(facts.attributes && facts.attributes->modifiers.size() == 1 &&
                            facts.target == fixture.request.units[1].subject && facts.state.resource.reloading,
                        "capture retry omitted actual weapon participants", index);
        }
        previous->release();
        auto replay = fixture.factory->prepare(fixture.request, fixture.positions).expect("capture retained receipt");
        require(std::get<std::unique_ptr<spawn::IPreparedSpawn>>(replay)->isPublished(), "capture changed receipt",
                index);
    } else if (stage == Stage::Pool) {
        require(gun->state()->ammoPool.resolve() == (inject ? previous : next),
                "failed binding replaced the old relationship", index);
        gun->state()->ammoPool = weapon::AmmoPoolLink::bind(*gun, *next).expect("same-world pool binding retry");
        require(gun->state()->ammoPool.resolve() == next && previous->state()->count == 5 && next->state()->count == 9,
                "pool retry lost binding or changed ammunition", index);
        previous->release();
        next->release();
    } else {
        if (inject) {
            auto facts = fixture.factory->collectLifecycle(events).expect("lifecycle retry");
            require(facts.size() == 1 && facts[0].sequence == 1, "failed lifecycle lost a fact or advanced sequence",
                    index);
        }
        require(fixture.factory->collectLifecycle(events).expect("lifecycle repeat").empty(),
                "lifecycle duplicated facts", index);
        require(events.size() == 1, "bridge consumed shared damage input", index);
        require(!fixture.factory->inspect(fixture.request.units[0].subject).ok(), "dead origin still indexed", index);
        require(fixture.factory->inspect(fixture.request.units[1].subject).ok(), "unaffected unit lost its binding",
                index);
    }
    return count;
}
std::size_t graphAttempt(Stage stage, std::size_t index, bool inject) {
    using namespace eve;
    test_rts_spawn::Fixture source;
    source.commit();
    auto getGun = [](test_rts_spawn::Fixture& fixture, std::size_t i) {
        ecs::ScopedTable scope(*fixture.table);
        return dynamic_cast<weapon::WeaponEntity*>(
            fixture.domain->findUnit(fixture.request.units[i].subject)->weapon()->link.resolve());
    };
    const auto                         epoch = source.request.source.owner.child("graph-allocation");
    std::vector<weapon::WeaponCapture> frozen;
    {
        ecs::ScopedTable scope(*source.table);
        auto*            pool      = weapon::AmmoPoolEntity::createPool();
        pool->identity()->id       = std::string(96, 'p');
        pool->identity()->ammoType = std::string(96, 't');
        pool->state()->count       = 9;
        pool->state()->max         = 40;
        for (std::size_t i = 0; i < 2; ++i) {
            auto* gun              = getGun(source, i);
            gun->state()->ammoPool = weapon::AmmoPoolLink::bind(*gun, *pool).expect("source graph link");
            gun->state()->cooldown = .25f + float(i);
            weapon::WeaponAttributeAdapter::setBase(*gun, "mana", 3 + static_cast<int>(i))
                .expect("source graph attributes");
            weapon::WeaponAttributeAdapter::addModifier(*gun, std::string(96, 'm'), "mana", std::string(96, 's'),
                                                        attributes::AttributeOperation::Add, 2)
                .expect("source graph modifier");
        }
        auto pools = weapon::WeaponPoolBindings::capture(*source.table, epoch).expect("source graph freeze");
        for (std::size_t i = 0; i < 2; ++i)
            frozen.push_back(
                weapon::WeaponCapture::capture(*getGun(source, i), epoch, {}, pools).expect("source member freeze"));
    }
    test_rts_spawn::Fixture destination;
    destination.commit();
    ecs::ScopedTable scope(*destination.table);
    auto*            pool = weapon::AmmoPoolEntity::createPool();
    pool->identity()->id  = "original-pool";
    pool->state()->count  = 55;
    auto oldLink = weapon::AmmoPoolLink::bind(*getGun(destination, 0), *pool).expect("original graph lifetime");
    for (std::size_t i = 0; i < 2; ++i) {
        auto* gun              = getGun(destination, i);
        gun->state()->ammoPool = oldLink;
        gun->state()->cooldown = 7 + float(i);
        gun->aim()->yaw        = 99 + float(i);
        if (stage != Stage::GraphCommitCreate)
            weapon::WeaponAttributeAdapter::setBase(*gun, "mana", 99).expect("original graph attributes");
    }
    const std::array members{weapon::WeaponGraphMember{std::cref(frozen[0]), ecs::handle_of(getGun(destination, 0))},
                             weapon::WeaponGraphMember{std::cref(frozen[1]), ecs::handle_of(getGun(destination, 1))}};
    const auto       poolText =
        frozen[0].facts()->get().pool->encodeState().expect("graph pool encoding").toJson().expect("graph pool text");
    const auto       poolValue  = Value::fromJson(poolText).expect("graph pool wire");
    auto             parsedPool = weapon::CapturedAmmoPool::prepareState(poolValue).expect("graph pool admission");
    const std::array pools{weapon::WeaponGraphPool{std::move(parsedPool), ecs::handle_of(pool)}};
    std::optional<weapon::PreparedWeaponGraphRestore> prepared;
    std::optional<weapon::WeaponGraphRestoreReceipt>  receipt;
    if (stage != Stage::GraphPrepare)
        prepared.emplace(weapon::PreparedWeaponGraphRestore::prepare(epoch, destination.table, members, pools, {})
                             .expect("graph baseline prepare"));
    const auto queries = cap::listenerCount<IStateQuery>(), mutations = cap::listenerCount<IStateMutation>();
    bool       success = false;
    allocations        = 0;
    injected           = false;
    failure            = index;
    measuring          = true;
    try {
        if (stage == Stage::GraphPrepare) {
            auto result = weapon::PreparedWeaponGraphRestore::prepare(epoch, destination.table, members, pools, {});
            success     = result.ok();
        } else {
            auto result = prepared->commit();
            success     = result.ok();
            if (result) receipt.emplace(std::move(result).takeValue());
        }
    } catch (const std::bad_alloc&) {
    }
    measuring        = false;
    const auto count = allocations;
    const auto hit   = injected;
    failure          = std::numeric_limits<std::size_t>::max();
    require(inject ? hit && !success : success, "graph injection did not abort operation", index);
    require(!destination.table->is_deferred() && destination.count() == 2 && weapons(*destination.table) == 2,
            "graph attempt changed roots or retained View", index);
    if (stage == Stage::GraphPrepare || !success) {
        require(pool->state()->count == 55 && pool->identity()->id == "original-pool" && oldLink.resolve() == pool,
                "failed graph attempt published pool facts or lifetime", index);
        for (std::size_t i = 0; i < 2; ++i) {
            auto* gun = getGun(destination, i);
            require(gun->state()->cooldown == 7 + float(i) && gun->aim()->yaw == 99 + float(i) &&
                        gun->state()->ammoPool.resolve() == pool,
                    "failed graph attempt published weapon prefix", index);
            auto* buffer = gun->getComponentManager().getComponentBuffer<weapon::WeaponEntity::Attributes>();
            if (stage == Stage::GraphCommitCreate)
                require(!buffer || !buffer->has(gun->id), "failed graph attempt retained created Attributes", index);
            else
                require(gun->attributes()->values.getFinal("mana").expect("old graph mana") == 99,
                        "failed graph attempt changed original attributes", index);
        }
        require(cap::listenerCount<IStateQuery>() == queries && cap::listenerCount<IStateMutation>() == mutations,
                "failed graph attempt changed capabilities", index);
    }
    if (stage == Stage::GraphPrepare)
        prepared.emplace(weapon::PreparedWeaponGraphRestore::prepare(epoch, destination.table, members, pools, {})
                             .expect("graph preparation retry"));
    if (!receipt) receipt.emplace(prepared->commit().expect("same graph candidate retry"));
    require(receipt->weapons == 2 && receipt->pools.size() == 1 && receipt->saveEpoch == epoch &&
                receipt->pools[0].pool.resolve() == pool && pool->state()->count == 9 && oldLink.isStale(),
            "graph receipt or pool lifetime diverged", index);
    for (std::size_t i = 0; i < 2; ++i) {
        auto* gun = getGun(destination, i);
        require(
            gun->state()->ammoPool.resolve() == pool && gun->state()->cooldown == .25f + float(i) &&
                gun->attributes()->values.getFinal("mana").expect("restored graph mana") == 5 + double(i) &&
                getGun(source, i)->attributes()->values.getFinal("mana").expect("source graph mana") == 5 + double(i),
            "graph exact native state or source changed", index);
    }
    if (stage == Stage::GraphCommit) require(count == 0, "existing graph publication allocated", index);
    return count;
}
std::size_t backendArchiveAttempt(std::size_t index, bool inject, bool encode) {
    using namespace eve;
    std::size_t count = 0;
    ssq::VM     vm(1024, ssq::Libs::ALL);
    auto        eve = vm.addTable("eve");
    auto        cls = eve.addClass<crowd::Crowd>("Crowd");
    crowd::Crowd::expose(cls);
    vm.addFunc("codecNative", [&](crowd::Crowd* source, crowd::Crowd* target, std::int64_t owner) -> std::int64_t {
        const auto snapshot     = source->captureSnapshot().expect("actual pending/leased registry");
        const auto value        = snapshot.encodeState().expect("baseline backend");
        const auto text         = value.toJson().expect("baseline bytes");
        const auto before       = target->getFieldStatus();
        const auto sourceBefore = source->getFieldStatus();
        const auto queries = cap::listenerCount<IStateQuery>(), mutations = cap::listenerCount<IStateMutation>();
        allocations  = 0;
        failure      = inject ? index : std::numeric_limits<std::size_t>::max();
        injected     = false;
        measuring    = true;
        bool success = false;
        try {
            if (encode) {
                auto result = snapshot.encodeState();
                success     = result.ok();
            } else {
                auto result = crowd::Crowd::Snapshot::prepareState(value);
                success     = result.ok();
            }
        } catch (const std::bad_alloc&) {
        }
        measuring      = false;
        count          = allocations;
        const bool hit = injected;
        failure        = std::numeric_limits<std::size_t>::max();
        require(inject ? hit && !success : success, "backend codec must abort each injected allocation", index);
        const auto after       = target->getFieldStatus();
        const auto sourceAfter = source->getFieldStatus();
        require(after.owner == before.owner && after.revision == before.revision && target->getAgentCount() == 0 &&
                    sourceAfter.owner == sourceBefore.owner &&
                    sourceAfter.inputRevision == sourceBefore.inputRevision &&
                    cap::listenerCount<IStateQuery>() == queries && cap::listenerCount<IStateMutation>() == mutations,
                "backend codec changed live owners/fields/agents/listeners", index);
        require(snapshot.encodeState().expect("same source retry").toJson().expect("repeat bytes") == text,
                "backend codec changed snapshot", index);
        auto parsed = crowd::Crowd::Snapshot::prepareState(value).expect("retry load");
        require(parsed.encodeState().expect("parsed snapshot").toJson().expect("parsed bytes") == text,
                "backend codec lost graph/work facts", index);
        auto plan = target->prepareRestore(parsed).expect("native candidate");
        require(!plan.remapFlowRequest({RuntimeHandle<crowd::CrowdFlowRequestTag>(0, 1), std::uint64_t(owner)}).ok(),
                "persistent load reinstated original owner", index);
        const auto mapped = plan.remapFlowRequest(parsed.requestReference(0, 1).expect("archive correspondence"))
                                .expect("new live candidate reference");
        target->commitRestore(std::move(plan)).expect("native publish retry");
        return std::int64_t(mapped.owner);
    });
    vm.run(vm.compileSource(R"(
        local c=eve.Crowd();c.resizeField(8,8,1.0,0.0,0.0);
        local p=c.requestFlowBuild(7,7).value;assert(c.advanceFlowRequest(p,5).ok);
        local r=c.requestFlowBuild(6,6).value;while(c.getFlowRequest(r).value.state=="pending")assert(c.advanceFlowRequest(r,100).ok);
        assert(c.acquireFlowRequestLease(r).ok);
        local a=c.agentRef(c.addNamedAgent("saved",1.5,1.5,0.0,0.1)).value;assert(c.updateAgents([{reference=a,flowRequest=r}]).ok);
        local retired=c.requestFlowBuild(5,5).value;assert(c.releaseFlowRequest(retired).ok);
        local t=eve.Crowd();t.resizeField(2,2,1.0,0.0,0.0);
        local q=clone p;q.owner=codecNative(c,t,p.owner);assert(q.owner!=p.owner && !t.getFlowRequest(p).ok);
        while(c.getFlowRequest(p).value.state=="pending") {
            local expected=c.advanceFlowRequest(p,3).value;local actual=t.advanceFlowRequest(q,3).value;
            assert(expected.state==actual.state && expected.heapPops==actual.heapPops && expected.directionCells==actual.directionCells);
        }
        local selected=clone r;selected.owner=q.owner;assert(t.sampleFlowRequestLease(selected,1.5,1.5).ok);
        assert(t.releaseFlowRequest(selected).ok);t.clearAgents();
    )"));
    return count;
}
std::size_t fieldArchiveAttempt(std::size_t index, bool inject, bool encode) {
    using namespace eve;
    crowd::CrowdField source;
    source.resize(8, 6, 2, -3, 4);
    source.setGoal(7, 5);
    source.addGoal(0, 5);
    source.setBlocked(3, 3, true);
    source.build();
    source.setCellCost(2, 2, 7);
    const auto   value = source.encodeState().expect("field baseline");
    const auto   text  = value.toJson().expect("baseline bytes");
    crowd::Crowd backend;
    backend.resizeField(2, 2, 1, 0, 0);
    const auto before  = backend.getFieldStatus();
    const auto queries = cap::listenerCount<IStateQuery>(), mutations = cap::listenerCount<IStateMutation>();
    allocations  = 0;
    failure      = inject ? index : std::numeric_limits<std::size_t>::max();
    injected     = false;
    measuring    = true;
    bool success = false;
    try {
        if (encode) {
            auto result = source.encodeState();
            success     = result.ok();
        } else {
            auto result = crowd::CrowdField::prepareState(value);
            success     = result.ok();
        }
    } catch (const std::bad_alloc&) {
    }
    measuring        = false;
    const auto count = allocations;
    const bool hit   = injected;
    failure          = std::numeric_limits<std::size_t>::max();
    require(inject ? hit && !success : success, "field codec must abort every injected allocation", index);
    const auto after = backend.getFieldStatus();
    require(after.owner == before.owner && after.revision == before.revision &&
                after.inputRevision == before.inputRevision && backend.getAgentCount() == 0 &&
                cap::listenerCount<IStateQuery>() == queries && cap::listenerCount<IStateMutation>() == mutations,
            "field codec mutated live backend or registered providers", index);
    require(source.encodeState().expect("source retry").toJson().expect("repeat bytes") == text,
            "field codec changed source storage/history", index);
    auto parsed = crowd::CrowdField::prepareState(value).expect("same input retry");
    require(parsed.encodeState().expect("parsed field").toJson().expect("parsed bytes") == text,
            "field codec changed facts/capacities", index);
    source.build();
    parsed.build();
    require(source.encodeState().expect("source build").toJson().expect("source built bytes") ==
                parsed.encodeState().expect("restored build").toJson().expect("restored built bytes"),
            "field continuation differs from canonical build", index);
    return count;
}
std::size_t actionArchiveAttempt(std::size_t index, bool inject, bool seal) {
    using namespace eve;
    struct Effect final : transaction::ITransactionParticipant {
        int          commits = 0;
        Result<void> prepare(const transaction::TransactionContext&) override { return Result<void>::success(); }
        Result<void> commit(const transaction::TransactionContext&) override {
            ++commits;
            return Result<void>::success();
        }
        Result<void> rollback(const transaction::TransactionContext&) override { return Result<void>::success(); }
        Result<void> compensate(const transaction::TransactionContext&) override {
            --commits;
            return Result<void>::success();
        }
    } effect;
    struct Resolver final : action::IActionTargetResolver {
        Result<sensing::TargetSet> resolve(const sensing::TargetingQuery& query) const override {
            sensing::TargetSet result;
            result.addSubject(query.origin).expect("actual selected subject");
            result.setPrimary(query.origin).expect("primary");
            result.setPoint(query.originLocation).expect("selected point");
            result.setArea(*query.spec.worldArea).expect("selected geometry");
            return Result<sensing::TargetSet>::success(std::move(result));
        }
    } resolver;
    auto                   world = std::make_shared<ecs::Table>();
    action::ActionServices services;
    services.transactionEffect = &effect;
    services.targeting         = &resolver;
    action::ActionRuntime    source(services), target;
    action::ActionDefinition def;
    def.id            = *LogicalId::parse("action:runtime-wire");
    def.timing        = {Duration(10), Duration(50), Duration(20)};
    def.metadata      = {{"saved", std::string(96, 'm')}};
    def.targetingMode = action::TargetingMode::Query;
    sensing::TargetingSpec spec;
    spec.worldArea =
        sensing::WorldArea::cone2D(sensing::WorldPoint::world2D(0, 0).expect("origin"), 3, 4, .5f, 9).expect("cone");
    def.targetingSpec = spec;
    action::ActionTimeline timeline;
    timeline.actionId        = def.id;
    timeline.duration        = Duration(80);
    timeline.splitTimestamps = {Duration(40)};
    action::ActionTrack track;
    track.id    = *LogicalId::parse("track:wire");
    track.label = std::string(96, 't');
    track.notifies.push_back({*LogicalId::parse("notify:wire"),
                              *LogicalId::parse("event:wire"),
                              Duration(0),
                              {{"saved", std::string(96, 'p')}},
                              true});
    timeline.tracks.push_back(track);
    def.timeline                = timeline;
    const auto            epoch = *PersistentId::parse("01900000-0000-7000-8000-000000000904");
    action::ActionRequest req;
    req.actionId      = def.id;
    req.transactionId = std::string(96, 'x');
    req.parameters    = {{"payload", std::string(96, 'r')}};
    req.sourceSubject = SubjectRef::fromPersistentId(epoch.child("producer"));
    req.targetingQuery =
        sensing::TargetingQuery{*req.sourceSubject, sensing::WorldPoint::world2D(1, 2).expect("origin"), spec};
    const auto id = source.submit(def, req).expect("actual source action");
    source.setSectionTimingOverride(id, 1, Duration(80)).expect("actual override");
    source.advance(id, SimulationTick(1), Duration(30)).expect("commit once");
    const auto frozen = source.capture(epoch, world, {}).expect("owning facts");
    const auto base = frozen.seal(Revision(3), SimulationTick(1), test_rts_spawn::restoreHash).expect("baseline seal");
    const auto wire = serializeSnapshotEnvelope(base).expect("baseline wire");
    action::ActionDefinition old;
    old.id = *LogicalId::parse("action:existing");
    action::ActionRequest oldReq;
    oldReq.actionId    = old.id;
    const auto prior   = target.submit(old, oldReq).expect("existing target");
    const auto queries = cap::listenerCount<IStateQuery>(), mutations = cap::listenerCount<IStateMutation>();
    allocations    = 0;
    failure        = inject ? index : std::numeric_limits<std::size_t>::max();
    injected       = false;
    bool completed = false;
    measuring      = true;
    try {
        if (seal) {
            auto result = frozen.seal(Revision(3), SimulationTick(1), test_rts_spawn::restoreHash);
            measuring   = false;
            require(result.ok(), "action seal refused", index);
        } else {
            auto result = action::ActionCapture::parse(base, epoch, test_rts_spawn::restoreHash);
            measuring   = false;
            require(result.ok(), "action parse refused", index);
        }
        completed = true;
    } catch (const std::bad_alloc&) {
        measuring = false;
    }
    const auto count = allocations;
    require(!inject || (injected && !completed), "action archive swallowed allocation failure", index);
    require(target.executionCount() == 1 && target.find(prior)->phase() == action::ActionPhase::Requested &&
                effect.commits == 1 && cap::listenerCount<IStateQuery>() == queries &&
                cap::listenerCount<IStateMutation>() == mutations,
            "action codec changed live state", index);
    require(serializeSnapshotEnvelope(
                frozen.seal(Revision(3), SimulationTick(1), test_rts_spawn::restoreHash).expect("source retry"))
                    .expect("wire retry") == wire,
            "action codec changed source", index);
    auto parsed = action::ActionCapture::parse(base, epoch, test_rts_spawn::restoreHash).expect("parse retry");
    target.restore(action::PreparedActionRestore::prepare(parsed, world, {}).expect("native candidate"))
        .expect("native publish");
    const auto expected = source.advance(id, SimulationTick(2), Duration(20)).expect("source cursor");
    const auto actual   = target.advance(id, SimulationTick(2), Duration(20)).expect("restored cursor");
    require(actual.totalElapsed == expected.totalElapsed && actual.phase == expected.phase &&
                actual.timelineEvents.size() == expected.timelineEvents.size() && effect.commits == 1,
            "action replayed effect or lost cursor", index);
    return count;
}
std::size_t actionDefinitionAttempt(std::size_t index, bool inject, bool encode) {
    using namespace eve;
    action::ActionDefinition source;
    source.id        = *LogicalId::parse("action:definition-wire");
    source.timing    = {Duration(100), Duration(200), Duration(300)};
    source.condition = condition::Condition::policyCall("policy.ready", Value::object({{"key", std::string(96, 'a')}}));
    source.metadata  = {{"payload", std::string(96, 'm')}};
    source.cost      = resource::CostSpec::from({{"mana", 7}}).expect("definition cost");
    source.effectIds = {std::string(96, 'e')};
    source.activeExecutionRequired = true;
    source.targetingMode           = action::TargetingMode::Query;
    sensing::TargetingSpec spec;
    spec.maxCount    = 2;
    spec.countPolicy = sensing::CountPolicy::TruncateToMax;
    spec.worldArea =
        sensing::WorldArea::cone2D(sensing::WorldPoint::world2D(0, 0).expect("apex"), 1, 0, .5f, 8).expect("cone");
    source.targetingSpec = spec;
    action::ActionTimeline timeline;
    timeline.actionId        = source.id;
    timeline.duration        = Duration(600);
    timeline.splitTimestamps = {Duration(100), Duration(300)};
    action::ActionAnimationSection section;
    section.id           = *LogicalId::parse("section:body");
    section.animationUri = std::string(96, 'a');
    section.end          = Duration(600);
    section.blendIn      = Duration(10);
    section.blendOut     = Duration(20);
    section.sourceStart  = Duration(30);
    section.sourceEnd    = Duration(630);
    timeline.animationSections.push_back(section);
    action::ActionTrack track;
    track.id    = *LogicalId::parse("track:body");
    track.label = std::string(96, 't');
    track.notifies.push_back({*LogicalId::parse("notify:hit"),
                              *LogicalId::parse("event:hit"),
                              Duration(200),
                              {{"payload", std::string(96, 'p')}},
                              true});
    track.states.push_back(
        {*LogicalId::parse("state:window"), *LogicalId::parse("event:window"), Duration(100), Duration(400), {}, true});
    timeline.tracks.push_back(std::move(track));
    source.timeline                = timeline;
    const auto               value = source.encodeState().expect("definition baseline");
    const auto               wire  = value.toJson().expect("definition baseline text");
    const auto               input = Value::fromJson(wire).expect("definition input");
    action::ActionRuntime    runtime;
    action::ActionDefinition old;
    old.id = *LogicalId::parse("action:existing");
    action::ActionRequest oldRequest;
    oldRequest.actionId = old.id;
    const auto first    = runtime.submit(old, oldRequest).expect("existing execution");
    const auto queries = cap::listenerCount<IStateQuery>(), mutations = cap::listenerCount<IStateMutation>();
    allocations    = 0;
    failure        = inject ? index : std::numeric_limits<std::size_t>::max();
    injected       = false;
    bool completed = false;
    measuring      = true;
    try {
        if (encode) {
            auto result = source.encodeState();
            measuring   = false;
            require(result.ok(), "definition encoding refused source", index);
        } else {
            auto result = action::ActionDefinition::prepareState(input);
            measuring   = false;
            require(result.ok(), "definition reading refused input", index);
        }
        completed = true;
    } catch (const std::bad_alloc&) {
        measuring = false;
    }
    measuring        = false;
    const auto count = allocations;
    require(inject ? injected && !completed : completed, "definition codec allocation boundary", index);
    require(runtime.find(first) && runtime.find(first)->phase() == action::ActionPhase::Requested &&
                cap::listenerCount<IStateQuery>() == queries && cap::listenerCount<IStateMutation>() == mutations,
            "definition codec mutated runtime or listeners", index);
    require(source.encodeState().expect("definition retry").toJson().expect("definition retry text") == wire,
            "definition codec mutated source", index);
    auto parsed = action::ActionDefinition::prepareState(input).expect("definition parse retry");
    require(parsed.encodeState().expect("parsed definition").toJson().expect("parsed wire") == wire,
            "definition retry lost facts", index);
    action::ActionRequest request;
    request.actionId       = parsed.id;
    request.targetingQuery = sensing::TargetingQuery{
        SubjectRef::fromPersistentId(*PersistentId::parse("01900000-0000-7000-8000-000000000903")),
        sensing::WorldPoint::world2D(0, 0).expect("origin"), *parsed.targetingSpec};
    auto submitted = runtime.submit(std::move(parsed), std::move(request)).expect("real submission after codec retry");
    require(
        submitted.value() == first.value() + 1 && runtime.find(submitted)->phase() == action::ActionPhase::Requested,
        "definition codec advanced runtime or changed allocator", index);
    return count;
}
std::size_t sourceArchiveAttempt(std::size_t index, bool inject, bool seal) {
    using namespace eve;
    test_rts_spawn::SavedRTSSpawn saved;
    std::weak_ptr<ecs::Table>     retired;
    {
        test_rts_spawn::Fixture source;
        source.sourceWorld->advance({SimulationTick(1), Duration(100000000)}).expect("actual source creation");
        retired = source.table;
        saved   = test_rts_spawn::freezeSpawn(source, source.request.source.owner.child("source-codec-allocation"));
    }
    require(retired.expired(), "source codec retained world", index);
    const auto epoch = saved.provenance->saveEpoch();
    const auto envelope =
        saved.provenance->seal(Revision(13), SimulationTick(1), test_rts_spawn::restoreHash).expect("source baseline");
    const auto                       text = serializeSnapshotEnvelope(envelope).expect("source baseline wire");
    test_rts_spawn::RestoredRTSSpawn target(saved);
    const auto                       before = target.domain->canonicalStateJson().expect("target source baseline");
    const auto queries = cap::listenerCount<IStateQuery>(), mutations = cap::listenerCount<IStateMutation>();
    allocations    = 0;
    failure        = inject ? index : std::numeric_limits<std::size_t>::max();
    injected       = false;
    bool completed = false;
    measuring      = true;
    try {
        if (seal) {
            auto result = saved.provenance->seal(Revision(13), SimulationTick(1), test_rts_spawn::restoreHash);
            measuring   = false;
            require(result.ok(), "source seal refused valid capture", index);
        } else {
            auto result = rts_spawn::RTSSpawnCapture::parse(envelope, epoch, test_rts_spawn::restoreHash);
            measuring   = false;
            require(result.ok(), "source parse refused valid input", index);
        }
        completed = true;
    } catch (const std::bad_alloc&) {
        measuring = false;
    }
    measuring        = false;
    const auto count = allocations;
    require(inject ? injected && !completed : completed, "source codec allocation boundary", index);
    require(target.domain->canonicalStateJson().expect("target after source codec") == before &&
                cap::listenerCount<IStateQuery>() == queries && cap::listenerCount<IStateMutation>() == mutations,
            "source codec changed target/listeners", index);
    require(
        serializeSnapshotEnvelope(
            saved.provenance->seal(Revision(13), SimulationTick(1), test_rts_spawn::restoreHash).expect("source retry"))
                .expect("source retry wire") == text,
        "source codec changed captured facts", index);
    auto parsed = rts_spawn::RTSSpawnCapture::parse(
                      parseSnapshotEnvelope(text, test_rts_spawn::restoreHash).expect("source input"), epoch,
                      test_rts_spawn::restoreHash)
                      .expect("source parse retry");
    require(parsed.sourceIncarnation().isZero(), "source parser minted runtime incarnation", index);
    require(serializeSnapshotEnvelope(
                parsed.seal(Revision(13), SimulationTick(1), test_rts_spawn::restoreHash).expect("parsed source seal"))
                    .expect("parsed source wire") == text,
            "source retry lost facts", index);
    saved.provenance = std::make_shared<const rts_spawn::RTSSpawnCapture>(std::move(parsed));
    test_rts_spawn::RestoredRTSSpawn restored(saved);
    ecs::ScopedTable                 restoredScope(*restored.table);
    restored.factory->restoreSources(*restored.core, {}, {}).expect("parsed source native binding");
    const auto& receipt = saved.provenance->receipts()[0];
    auto lease = restored.core->resolveSavedSource(spawn::savedSpawnerKey(receipt.source)).expect("fresh actual lease");
    spawn::BatchRequest request{receipt.operation, receipt.units, lease.source(), receipt.ordinal, lease};
    auto                replay = restored.factory->prepare(request, receipt.positions).expect("parsed receipt replay");
    auto&               token  = std::get<std::unique_ptr<spawn::IPreparedSpawn>>(replay);
    require(token->isPublished(), "parsed receipt recreated units", index);
    token->commit().expect("parsed receipt confirmation");
    require(restored.domain->unitCount() == 2 && !lease.source().incarnation.isZero(),
            "parsed source retry lost bodies/authority", index);
    return count;
}
std::size_t archetypeValueAttempt(std::size_t index, bool inject, bool encode) {
    using namespace eve;
    std::optional<rts::PreparedUnitArchetype>      recipe;
    std::weak_ptr<definitions::DefinitionRegistry> retired;
    {
        test_rts_spawn::Fixture source;
        retired = source.registry;
        recipe.emplace(rts::PreparedUnitArchetype::prepare(*source.registry, *LogicalId::parse("unit:fighter"))
                           .expect("actual source recipe"));
    }
    require(retired.expired(), "recipe retained source registry", index);
    const auto              value = recipe->encodeState().expect("recipe baseline");
    const auto              text  = value.toJson().expect("recipe baseline wire");
    const auto              input = Value::fromJson(text).expect("recipe owning input");
    test_rts_spawn::Fixture target;
    ecs::ScopedTable        targetScope(*target.table);
    const auto              before = target.domain->canonicalStateJson().expect("target before recipe codec");
    const auto queries = cap::listenerCount<IStateQuery>(), mutations = cap::listenerCount<IStateMutation>();
    allocations    = 0;
    failure        = inject ? index : std::numeric_limits<std::size_t>::max();
    injected       = false;
    bool completed = false;
    measuring      = true;
    try {
        if (encode) {
            auto result = recipe->encodeState();
            measuring   = false;
            require(result.ok(), "recipe encoder rejected source", index);
        } else {
            auto result = rts::PreparedUnitArchetype::prepareState(input);
            measuring   = false;
            require(result.ok(), "recipe parser rejected source", index);
        }
        completed = true;
    } catch (const std::bad_alloc&) {
        measuring = false;
    }
    measuring        = false;
    const auto count = allocations;
    require(inject ? injected && !completed : completed, "recipe codec allocation boundary", index);
    require(target.domain->canonicalStateJson().expect("target after recipe codec") == before && target.count() == 0 &&
                weapons(*target.table) == 0 && cap::listenerCount<IStateQuery>() == queries &&
                cap::listenerCount<IStateMutation>() == mutations,
            "recipe codec published state/listeners", index);
    require(recipe->encodeState().expect("source recipe retry").toJson().expect("source wire") == text,
            "recipe codec mutated source", index);
    auto parsed = rts::PreparedUnitArchetype::prepareState(input).expect("recipe parsing retry");
    require(parsed.encodeState().expect("parsed recipe retry").toJson().expect("retry wire") == text,
            "recipe retry lost source content", index);
    const auto subject = SubjectRef::fromPersistentId(target.request.source.owner.child("recipe-wire-retry"));
    const auto faction = SubjectRef::fromPersistentId(*PersistentId::parse("01900000-0000-7000-8000-000000000889"));
    const std::array requests{rts::UnitBatchRequest{subject, parsed, 12, 12, {}}};
    auto*            team = target.domain->findFaction(faction);
    require(team != nullptr, "native retry requires actual owning faction", index);
    auto batch = target.domain->prepareUnitBatch(*team, requests).expect("native recipe retry");
    require(batch.commit().expect("native recipe publication").state == rts::UnitBatchState::Committed,
            "parsed recipe native batch blocked", index);
    ecs::ScopedTable scope(*target.table);
    auto*            unit = target.domain->findUnit(subject);
    auto*            gun  = unit ? dynamic_cast<weapon::WeaponEntity*>(unit->weapon()->link.resolve()) : nullptr;
    require(unit && gun && unit->durability()->state.maxHealth == 40 && unit->motion()->speed == 3 &&
                gun->definition()->def->damage == 10,
            "parsed recipe native facts differ", index);
    return count;
}
std::size_t bodyWeaponArchiveAttempt(std::size_t index, bool inject, bool seal) {
    using namespace eve;
    auto saved = test_rts_spawn::freezeWeaponScope();
    require(saved.sourceWorld.expired(), "body archive retained source", index);
    const auto epoch = saved.weapons->saveEpoch();
    const auto envelope =
        saved.weapons->seal(Revision(23), SimulationTick(1), test_rts_spawn::restoreHash).expect("graph baseline");
    const auto                       text = serializeSnapshotEnvelope(envelope).expect("graph baseline wire");
    test_rts_spawn::RestoredRTSSpawn target(saved.session);
    const auto                       before = target.domain->canonicalStateJson().expect("target before graph codec");
    const auto queries = cap::listenerCount<IStateQuery>(), mutations = cap::listenerCount<IStateMutation>();
    allocations    = 0;
    failure        = inject ? index : std::numeric_limits<std::size_t>::max();
    injected       = false;
    bool completed = false;
    measuring      = true;
    try {
        if (seal) {
            auto result = saved.weapons->seal(Revision(23), SimulationTick(1), test_rts_spawn::restoreHash);
            measuring   = false;
            require(result.ok(), "graph sealing rejected valid capture", index);
        } else {
            auto result = rts::RTSWeaponCapture::parse(envelope, epoch, test_rts_spawn::restoreHash);
            measuring   = false;
            require(result.ok(), "graph parsing rejected valid envelope", index);
        }
        completed = true;
    } catch (const std::bad_alloc&) {
        measuring = false;
    }
    measuring        = false;
    const auto count = allocations;
    require(inject ? injected && !completed : completed, "graph codec allocation boundary", index);
    require(target.domain->canonicalStateJson().expect("target after graph codec") == before &&
                cap::listenerCount<IStateQuery>() == queries && cap::listenerCount<IStateMutation>() == mutations &&
                weapons(*target.table) == 0,
            "graph codec changed target or listeners", index);
    require(serializeSnapshotEnvelope(saved.weapons->seal(Revision(23), SimulationTick(1), test_rts_spawn::restoreHash)
                                          .expect("graph sealing retry"))
                    .expect("graph retry wire") == text,
            "graph codec changed source", index);
    auto parsed = rts::RTSWeaponCapture::parse(
                      parseSnapshotEnvelope(text, test_rts_spawn::restoreHash).expect("graph wire input"), epoch,
                      test_rts_spawn::restoreHash)
                      .expect("graph parsing retry");
    require(
        serializeSnapshotEnvelope(
            parsed.seal(Revision(23), SimulationTick(1), test_rts_spawn::restoreHash).expect("parsed graph sealing"))
                .expect("parsed graph wire") == text,
        "graph retry lost facts", index);
    saved.weapons.reset();
    auto prepared =
        rts::PreparedRTSWeaponRestore::prepare(*target.domain, parsed).expect("parsed graph native preparation");
    auto pools = target.domain->restoreWeapons(std::move(prepared)).expect("parsed graph native publication");
    ecs::ScopedTable scope(*target.table);
    auto* first  = dynamic_cast<weapon::WeaponEntity*>(target.domain->findUnit(saved.first)->weapon()->link.resolve());
    auto* second = dynamic_cast<weapon::WeaponEntity*>(target.domain->findUnit(saved.second)->weapon()->link.resolve());
    require(first && second && weapons(*target.table) == 2 && pools.pools().size() == 1 &&
                target.domain->findBuilding(saved.building)->weapon()->link.resolve() == first &&
                first->state()->ammoPool.resolve() == second->state()->ammoPool.resolve() &&
                first->state()->ammoPool.resolve()->state()->count == 9,
            "parsed graph native retry lost aliases or pool facts", index);
    return count;
}
std::size_t weaponValueAttempt(std::size_t index, bool inject, bool encode) {
    using namespace eve;
    auto saved = test_rts_spawn::freezeWeaponScope(true);
    require(saved.sourceWorld.expired(), "complete weapon retained source", index);
    std::shared_ptr<const weapon::WeaponCapture> frozen;
    for (const auto& member : saved.weapons->members())
        if (member.weapon) {
            frozen = member.weapon;
            break;
        }
    require(bool(frozen), "complete weapon fact missing", index);
    saved.weapons.reset();
    const auto value     = frozen->encodeState().expect("complete encoding baseline");
    const auto text      = value.toJson().expect("complete wire baseline");
    const auto input     = Value::fromJson(text).expect("complete owning input");
    const auto queries   = cap::listenerCount<IStateQuery>();
    const auto mutations = cap::listenerCount<IStateMutation>();
    allocations          = 0;
    failure              = inject ? index : std::numeric_limits<std::size_t>::max();
    injected             = false;
    bool completed       = false;
    measuring            = true;
    try {
        if (encode) {
            auto result = frozen->encodeState();
            measuring   = false;
            require(result.ok(), "complete encoding rejected source", index);
        } else {
            auto result = weapon::WeaponCapture::prepareState(input);
            measuring   = false;
            require(result.ok(), "complete parsing rejected input", index);
        }
        completed = true;
    } catch (const std::bad_alloc&) {
        measuring = false;
    }
    measuring        = false;
    const auto count = allocations;
    require(!injected || !completed, "complete codec swallowed allocation failure", index);
    require(cap::listenerCount<IStateQuery>() == queries && cap::listenerCount<IStateMutation>() == mutations,
            "complete codec changed listeners", index);
    require(frozen->encodeState().expect("source retry").toJson().expect("source wire") == text,
            "complete codec mutated detached source", index);
    auto parsed = weapon::WeaponCapture::prepareState(input).expect("complete retry");
    require(parsed.encodeState().expect("repeat complete value").toJson().expect("repeat wire") == text,
            "complete retry lost native facts", index);
    return count;
}
std::size_t poolValueAttempt(std::size_t index, bool inject, bool encode) {
    using namespace eve;
    std::optional<weapon::CapturedAmmoPool> saved;
    std::weak_ptr<ecs::Table>               sourceWorld;
    weapon::AmmoPoolLink                    retired;
    {
        auto source = std::make_shared<ecs::Table>();
        sourceWorld = source;
        ecs::ScopedTable origin(*source);
        auto*            pool      = weapon::AmmoPoolEntity::createPool();
        auto*            gun       = weapon::WeaponEntity::createWeapon();
        pool->identity()->id       = std::string(96, 'n');
        pool->identity()->ammoType = std::string(96, 'a');
        pool->state()->count       = 9;
        pool->state()->max         = -7;
        retired                    = weapon::AmmoPoolLink::bind(*gun, *pool).expect("actual source lifetime");
        saved.emplace(weapon::CapturedAmmoPool{*pool->identity(), *pool->state(),
                                               PersistentId::parse("01900000-0000-7000-8000-000000000882").value()});
    }
    require(sourceWorld.expired() && retired.isStale(), "pool value retained source lifetime", index);
    const auto       value = saved->encodeState().expect("pool encoding baseline");
    const auto       text  = value.toJson().expect("pool wire baseline");
    const auto       input = Value::fromJson(text).expect("owning pool wire input");
    auto             world = std::make_shared<ecs::Table>();
    ecs::ScopedTable target(*world);
    auto*            pool = weapon::AmmoPoolEntity::createPool();
    auto*            gun  = weapon::WeaponEntity::createWeapon();
    pool->identity()->id  = "old target";
    pool->state()->count  = 71;
    auto       old        = weapon::AmmoPoolLink::bind(*gun, *pool).expect("old target lifetime");
    const auto queries    = cap::listenerCount<IStateQuery>();
    const auto mutations  = cap::listenerCount<IStateMutation>();
    allocations           = 0;
    failure               = inject ? index : std::numeric_limits<std::size_t>::max();
    injected              = false;
    bool completed        = false;
    measuring             = true;
    try {
        if (encode) {
            auto result = saved->encodeState();
            measuring   = false;
            require(result.ok(), "pool encoding refused valid state", index);
        } else {
            auto result = weapon::CapturedAmmoPool::prepareState(input);
            measuring   = false;
            require(result.ok(), "pool admission refused valid state", index);
        }
        completed = true;
    } catch (const std::bad_alloc&) {
        measuring = false;
    }
    measuring        = false;
    const auto count = allocations;
    require(!injected || !completed, "pool value swallowed injected failure", index);
    require(old.resolve() == pool && pool->identity()->id == "old target" && pool->state()->count == 71,
            "pool value attempt mutated destination/lifetime", index);
    require(cap::listenerCount<IStateQuery>() == queries && cap::listenerCount<IStateMutation>() == mutations,
            "pool value attempt changed capability listeners", index);
    require(saved->encodeState().expect("encoding retry").toJson().expect("retry text") == text,
            "pool value changed detached source", index);
    auto parsed = weapon::CapturedAmmoPool::prepareState(input).expect("pool parsing retry");
    require(parsed.encodeState().expect("retry state").toJson().expect("retry wire") == text,
            "pool parsing retry changed facts", index);
    auto prepared = weapon::PreparedAmmoPoolRestore::prepare(parsed, world, *pool).expect("native pool retry");
    auto receipt  = prepared.commit().expect("pool native publication");
    require(old.isStale() && receipt.pool.resolve() == pool && receipt.archiveId == saved->archiveId &&
                pool->state()->count == saved->state.count && pool->state()->max == saved->state.max &&
                pool->identity()->id == saved->identity.id && pool->identity()->ammoType == saved->identity.ammoType,
            "pool value/native publication diverged", index);
    return count;
}
std::size_t externalNavigationArchiveAttempt(std::size_t index, bool inject, bool seal) {
    using namespace eve;
    crowd::Crowd source, target;
    source.resizeField(4, 4, 1.f, 0.f, 0.f);
    target.resizeField(2, 2, 1.f, 0.f, 0.f);
    auto pending = source.prepareFlowBuild(3, 3).expect("pending external plan");
    source.advanceFlowBuild(pending, 2).expect("partial external work");
    auto built = source.prepareFlowBuild(0, 0).expect("leased external plan");
    while (built.state() == crowd::Crowd::FlowBuildState::Pending)
        source.advanceFlowBuild(built, 1000).expect("finish lease source");
    auto             lease   = source.acquireFlowField(std::move(built)).expect("external shared lease");
    auto             backend = source.captureSnapshot().expect("external backend");
    const std::array views{crowd::Crowd::NavigationWorkView{std::cref(pending), std::cref(lease)}};
    auto             captured = backend.captureNavigation(views).expect("external capture");
    const auto       epoch    = *PersistentId::parse("01900000-0000-7000-8000-000000000773");
    const auto       envelope =
        captured.seal(epoch, Revision(5), SimulationTick(3), test_rts_spawn::restoreHash).expect("external baseline");
    const auto bytes        = serializeSnapshotEnvelope(envelope).expect("external baseline wire");
    const auto before       = target.getFieldStatus();
    const auto sourceBefore = source.getFieldStatus();
    const auto cacheBefore  = source.getFlowCacheStatus();
    const auto backendBytes = backend.encodeState().expect("baseline backend").toJson().expect("backend wire");
    allocations             = 0;
    failure                 = inject ? index : std::numeric_limits<std::size_t>::max();
    injected                = false;
    bool success            = false;
    measuring               = true;
    try {
        if (seal) {
            auto result = captured.seal(epoch, Revision(5), SimulationTick(3), test_rts_spawn::restoreHash);
            success     = result.ok();
        } else {
            auto result = backend.parseNavigation(envelope, epoch, test_rts_spawn::restoreHash);
            success     = result.ok();
        }
    } catch (const std::bad_alloc&) {
    }
    measuring        = false;
    const auto count = allocations;
    require(inject ? injected && !success : success, "external archive swallowed allocation failure", index);
    require(target.getFieldStatus().owner == before.owner && target.getFieldStatus().revision == before.revision &&
                target.getAgentCount() == 0 && source.getFieldStatus().owner == sourceBefore.owner &&
                source.getFlowCacheStatus().residentBytes == cacheBefore.residentBytes,
            "external archive changed live backend", index);
    require(backend.encodeState().expect("backend retry").toJson().expect("backend retry wire") == backendBytes,
            "external archive mutated detached backend", index);
    require(serializeSnapshotEnvelope(captured.seal(epoch, Revision(5), SimulationTick(3), test_rts_spawn::restoreHash)
                                          .expect("same capture retry"))
                    .expect("same wire retry") == bytes,
            "external archive changed source facts", index);
    auto restored  = backend.parseNavigation(envelope, epoch, test_rts_spawn::restoreHash).expect("same input retry");
    auto candidate = target.prepareRestore(backend).expect("backend candidate");
    auto work      = candidate.prepareNavigation(restored).expect("external candidate");
    target.commitRestore(std::move(candidate)).expect("external backend publication");
    while (pending.state() == crowd::Crowd::FlowBuildState::Pending) {
        const auto expected = source.advanceFlowBuild(pending, 2).expect("source continuation");
        const auto actual   = target.advanceFlowBuild(work[0].plan, 2).expect("retry continuation");
        require(expected.state == actual.state && expected.heapPops == actual.heapPops &&
                    expected.directionCells == actual.directionCells,
                "external archive lost exact work cursor", index);
    }
    const auto expected = source.sampleFlowField(lease, 2.5f, 2.5f).expect("source lease sample");
    const auto actual   = target.sampleFlowField(work[0].lease, 2.5f, 2.5f).expect("restored lease sample");
    require(expected.direction.x == actual.direction.x && expected.direction.y == actual.direction.y,
            "external archive lost cache alias", index);
    require(!target.sampleFlowField(lease, 2.5f, 2.5f).ok(), "old owner authorized after retry", index);
    return count;
}
std::size_t projectileArchiveAttempt(std::size_t index, bool inject, const char* mode) {
    using namespace eve;
    test_rts_spawn::Fixture fixture;
    fixture.sourceWorld->advance({SimulationTick(1), Duration(0)}).expect("real projectile actors");
    ecs::ScopedTable scope(*fixture.table);
    const auto       sourceId = fixture.request.units[0].subject, targetId = fixture.request.units[1].subject;
    const auto factionId = SubjectRef::fromPersistentId(*PersistentId::parse("01900000-0000-7000-8000-000000000889"));
    auto*      actor     = fixture.domain->findUnit(targetId);
    weapon::ProjectileRuntime pool;
    pool.configurePool(2).expect("bounded projectile pool");
    weapon::ProjectileDefinition def;
    def.id                 = *LogicalId::fromParts("projectile", "allocation-flight");
    def.mode               = weapon::ProjectileMode::Homing;
    def.speed              = 4;
    def.maxTurnRateDegrees = 90;
    def.lifetime           = Duration(3000000000);
    const auto handle      = pool.spawn(def, {{}, {1, 0, 0}, ecs::handle_of(actor)}).expect("actual homing trajectory");
    rts::RTSProjectileSystemSnapshot saved;
    saved.runtime = pool.snapshot();
    saved.runtime.slots[handle.slot].state->target.reset();
    saved.runtime.slots[1].generation = UINT32_MAX;
    rts::RTSProjectilePayloadSnapshot payload;
    payload.key                = handle.packedKey();
    payload.source             = sourceId;
    payload.faction            = factionId;
    payload.target             = targetId;
    payload.trajectoryTarget   = targetId;
    payload.observer           = sourceId;
    payload.targetPoint        = {4, 5};
    payload.damageType         = std::string(96, 'd');
    payload.damage             = 7;
    payload.requiredTargetTags = {std::string(96, 't')};
    saved.payloads.push_back(payload);
    const auto               value = saved.encodeState().expect("projectile value baseline");
    const auto               wire  = value.toJson().expect("projectile wire baseline");
    rts::RTSProjectileSystem target;
    const auto               before =
        target.snapshot().encodeState().expect("old projectile target").toJson().expect("old target wire");
    auto resolver = [&](SubjectRef id) -> ecs::Entity* {
        auto resolved = fixture.domain->resolveOwnedSubject(id);
        if (!resolved) return nullptr;
        return ecs::try_get(resolved.value());
    };
    allocations  = 0;
    failure      = inject ? index : std::numeric_limits<std::size_t>::max();
    injected     = false;
    bool success = false;
    measuring    = true;
    try {
        if (std::strcmp(mode, "projectile-encode") == 0) {
            auto result = saved.encodeState();
            success     = result.ok();
        } else if (std::strcmp(mode, "projectile-parse") == 0) {
            auto result = rts::RTSProjectileSystemSnapshot::prepareState(value);
            success     = result.ok();
        } else {
            auto result = target.restore(saved, resolver);
            success     = result.ok();
        }
    } catch (const std::bad_alloc&) {
    }
    measuring        = false;
    const auto count = allocations;
    failure          = std::numeric_limits<std::size_t>::max();
    require(inject ? injected && !success : success, "projectile archive swallowed allocation failure", index);
    require(saved.encodeState().expect("same projectile source retry").toJson().expect("same source wire") == wire,
            "projectile archive changed source", index);
    const bool published = std::strcmp(mode, "projectile-restore") == 0 && success;
    if (!published)
        require(target.snapshot().encodeState().expect("intact old pool").toJson().expect("intact pool wire") == before,
                "projectile failure partially published", index);
    auto parsed = rts::RTSProjectileSystemSnapshot::prepareState(value).expect("same projectile input retry");
    if (!published) target.restore(parsed, resolver).expect("same projectile restore retry");
    rts::RTSProjectileSystem expected;
    expected.restore(parsed, resolver).expect("canonical source system");
    combat::DamageRuntime damage;
    target.step({SimulationTick(2), Duration(1000000)}, damage).expect("restored homing work");
    expected.step({SimulationTick(2), Duration(1000000)}, damage).expect("expected homing work");
    require(target.snapshot().encodeState().expect("actual continuation").toJson().expect("actual continuation wire") ==
                expected.snapshot()
                    .encodeState()
                    .expect("expected continuation")
                    .toJson()
                    .expect("expected continuation wire"),
            "projectile retry lost exact trajectory/payload continuation", index);
    return count;
}
std::size_t orderFileAttempt(std::size_t index, bool inject, const char* mode) {
    using namespace eve;
    rts::OrderComponent source, target;
    rts::CommandSpec    command;
    command.kind            = rts::OrderKind::Attack;
    command.definitionId    = "long-original-order-recipe-for-allocation-coverage";
    command.target          = {7.25f, -4};
    command.secondaryTarget = {9, 3};
    command.priority        = 11;
    command.radius          = 2.5f;
    command.timeoutSeconds  = 19.25;
    command.append          = true;
    const auto id           = source.enqueue(command, 3).expect("source command");
    command.kind            = rts::OrderKind::Patrol;
    command.priority        = 0;
    source.enqueue(command).expect("retained queued command");
    target.enqueue(command).expect("old target command");
    rts::RTSOrderState saved{source.snapshotState().expect("source facts"), {}};
    const auto         value   = saved.encodeState().expect("order value");
    const auto         wire    = value.toJson().expect("order wire");
    const auto         before  = target.snapshot().expect("target baseline");
    bool               success = false;
    allocations                = 0;
    failure                    = index;
    injected                   = false;
    measuring                  = true;
    try {
        if (std::strcmp(mode, "order-encode") == 0) {
            auto r  = saved.encodeState();
            success = r.ok();
        } else if (std::strcmp(mode, "order-parse") == 0) {
            auto r  = rts::RTSOrderState::prepareState(value);
            success = r.ok();
        } else {
            auto r  = target.restoreState(saved.orders);
            success = r.ok();
        }
    } catch (const std::bad_alloc&) {
    }
    measuring        = false;
    const auto count = allocations;
    require(inject ? injected && !success : success, "order file swallowed failure", index);
    require(saved.encodeState().expect("source retry").toJson().expect("source retry wire") == wire,
            "order file changed source", index);
    const bool published = std::strcmp(mode, "order-restore") == 0 && success;
    if (!published)
        require(target.snapshot().expect("intact target") == before, "order file partially published", index);
    auto parsed = rts::RTSOrderState::prepareState(value).expect("same file retry");
    if (!published) target.restoreState(parsed.orders).expect("same native retry");
    require(target.snapshot().expect("restored queue") == saved.orders.queueJson, "order history changed", index);
    auto current = target.current().expect("restored command");
    require(current.id == id && current.formationSlot == 3 && current.radius == 2.5f, "order extension changed", index);
    target.complete(id).expect("continued completion");
    require(target.current().expect("queued continuation").kind == rts::OrderKind::Patrol, "order continuation lost",
            index);
    return count;
}
std::size_t effectFileAttempt(std::size_t index, bool inject, const char* mode) {
    using namespace eve;
    rts::RTSEffectAdapter source, target;
    const auto subject = SubjectRef::fromPersistentId(*PersistentId::parse("01900000-0000-7000-8000-000000000e63"));
    rts::RTSEffectDefinition def;
    def.id        = "persisted-periodic-suppression-long-effect-id";
    def.kind      = rts::RTSEffectKind::Suppression;
    def.period    = .25;
    def.duration  = 3;
    def.magnitude = 7.5;
    def.tags      = {"long-effect-tag-for-independent-payload-copy", "second-tag"};
    source.apply(def, subject).expect("source effect");
    source.advance({SimulationTick(2), Duration::fromSeconds(.375).expect("delta")}).expect("partial source work");
    auto       old     = target.apply(def, subject).expect("old target effect");
    auto       saved   = source.snapshot();
    const auto value   = saved.encodeState().expect("effect value");
    const auto wire    = value.toJson().expect("effect file");
    const auto before  = target.snapshot().encodeState().expect("target facts").toJson().expect("target file");
    bool       success = false;
    allocations        = 0;
    failure            = index;
    injected           = false;
    measuring          = true;
    try {
        if (std::strcmp(mode, "effect-encode") == 0) {
            auto r  = saved.encodeState();
            success = r.ok();
        } else if (std::strcmp(mode, "effect-parse") == 0) {
            auto r  = rts::RTSEffectSnapshot::prepareState(value);
            success = r.ok();
        } else {
            auto r  = target.restore(saved);
            success = r.ok();
        }
    } catch (const std::bad_alloc&) {
    }
    measuring        = false;
    const auto count = allocations;
    require(inject ? injected && !success : success, "effect file swallowed allocation failure", index);
    require(saved.encodeState().expect("same source retry").toJson().expect("same source wire") == wire,
            "effect file changed source facts", index);
    const bool published = std::strcmp(mode, "effect-restore") == 0 && success;
    if (!published) {
        require(target.snapshot().encodeState().expect("intact target").toJson().expect("intact wire") == before,
                "effect restore partially published", index);
        require(target.resolve(old).ok(), "failed effect restore invalidated old handle", index);
    }
    auto parsed = rts::RTSEffectSnapshot::prepareState(value).expect("same file retry");
    if (!published) target.restore(parsed).expect("same native retry");
    require(!target.resolve(old).ok(), "successful effect restore kept old handle", index);
    require(target.snapshot().encodeState().expect("restored facts").toJson().expect("restored wire") == wire,
            "effect restore changed history or settlement", index);
    const SimulationStep next{SimulationTick(3), Duration::fromSeconds(.375).expect("next delta")};
    source.advance(next).expect("source continuation");
    target.advance(next).expect("restored continuation");
    require(target.snapshot().encodeState().expect("actual effects").toJson().expect("actual wire") ==
                source.snapshot().encodeState().expect("expected effects").toJson().expect("expected wire"),
            "effect restore lost periodic/event progress", index);
    return count;
}
std::size_t rootFileAttempt(std::size_t index, bool inject, const char* mode) {
    using namespace eve;
    using namespace test_rts_root_file;
    Fixture    source, old;
    const auto saved       = source.domain.snapshotState().expect("actual complete roots");
    const auto attributes  = source.domain.captureUnitAttributes(epoch()).expect("authoritative attributes");
    const auto value       = saved.encodeState().expect("root values");
    const auto bytes       = value.toJson().expect("root file");
    const auto oldBytes    = wire(old.domain);
    auto       materialize = [&]() {
        auto             admitted = rts::RTSStateSnapshot::prepareState(value).expect("independent root admission");
        auto             world     = std::make_shared<ecs::Table>();
        auto             candidate = std::make_unique<rts::RTS>(world);
        ecs::ScopedTable scope(*world);
        candidate->rebuildState(admitted).expect("isolated native roots");
        auto plan = rts::PreparedRTSUnitAttributeRestore::prepare(*candidate, attributes).expect("full attributes");
        candidate->restoreUnitAttributes(std::move(plan)).expect("native attributes publication");
        return std::pair{std::move(world), std::move(candidate)};
    };
    bool success = false;
    allocations  = 0;
    failure      = index;
    injected     = false;
    measuring    = true;
    try {
        if (std::strcmp(mode, "root-value-encode") == 0) {
            auto r  = saved.encodeState();
            success = r.ok();
        } else if (std::strcmp(mode, "root-value-parse") == 0) {
            auto r  = rts::RTSStateSnapshot::prepareState(value);
            success = r.ok();
        } else {
            auto candidate = materialize();
            success        = bool(candidate.second);
        }
    } catch (const std::bad_alloc&) {
    }
    measuring        = false;
    const auto count = allocations;
    require(inject ? injected && !success : success, "root file swallowed allocation failure", index);
    require(wire(source.domain) == bytes && wire(old.domain) == oldBytes, "root load mutated observable worlds", index);
    require(saved.encodeState().expect("same saved retry").toJson().expect("same saved wire") == bytes,
            "root load mutated detached facts", index);
    auto [world, candidate] = materialize();
    require(wire(*candidate) == bytes, "root retry lost exact fields or relationships", index);
    advance(source.domain, *source.world, 2);
    advance(*candidate, *world, 2);
    require(wire(*candidate) == wire(source.domain), "root retry lost motion/production/effect continuation", index);
    return count;
}
}  // namespace
void* operator new(std::size_t bytes) {
    if (measuring && allocations++ == failure) {
        injected = true;
        throw std::bad_alloc();
    }
    if (auto* result = std::malloc(bytes ? bytes : 1)) return result;
    throw std::bad_alloc();
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void  operator delete(void* value) noexcept { std::free(value); }
void  operator delete[](void* value) noexcept { std::free(value); }
void  operator delete(void* value, std::size_t) noexcept { std::free(value); }
void  operator delete[](void* value, std::size_t) noexcept { std::free(value); }
int   main(int argc, char** argv) {
    const char* mode = argc > 1 ? argv[1] : "commit";

    if (std::strcmp(mode, "root-value-encode") == 0 || std::strcmp(mode, "root-value-parse") == 0 ||
        std::strcmp(mode, "root-value-materialize") == 0) {
        const auto never = std::numeric_limits<std::size_t>::max();
        rootFileAttempt(never, false, mode);
        const auto count = rootFileAttempt(never, false, mode);
        for (std::size_t i = 0; i < count; ++i) rootFileAttempt(i, true, mode);
        std::printf("RTS %s: %zu ordinary allocation failpoints, intact worlds and exact native root retry\n", mode,
                      count);
        return 0;
    }
    if (std::strcmp(mode, "effect-encode") == 0 || std::strcmp(mode, "effect-parse") == 0 ||
        std::strcmp(mode, "effect-restore") == 0) {
        const auto never = std::numeric_limits<std::size_t>::max();
        effectFileAttempt(never, false, mode);
        const auto count = effectFileAttempt(never, false, mode);
        for (std::size_t i = 0; i < count; ++i) effectFileAttempt(i, true, mode);
        std::printf("RTS %s: %zu ordinary allocation failpoints, intact lifecycle and exact settlement retry\n", mode,
                      count);
        return 0;
    }
    if (std::strcmp(mode, "order-encode") == 0 || std::strcmp(mode, "order-parse") == 0 ||
        std::strcmp(mode, "order-restore") == 0) {
        const auto never = std::numeric_limits<std::size_t>::max();
        orderFileAttempt(never, false, mode);
        const auto count = orderFileAttempt(never, false, mode);
        for (std::size_t i = 0; i < count; ++i) orderFileAttempt(i, true, mode);
        std::printf("RTS %s: %zu ordinary allocation failpoints, intact orders and exact native retry\n", mode, count);
        return 0;
    }
    if (std::strcmp(mode, "projectile-encode") == 0 || std::strcmp(mode, "projectile-parse") == 0 ||
        std::strcmp(mode, "projectile-restore") == 0) {
        const auto never = std::numeric_limits<std::size_t>::max();
        projectileArchiveAttempt(never, false, mode);
        const auto count = projectileArchiveAttempt(never, false, mode);
        for (std::size_t i = 0; i < count; ++i) projectileArchiveAttempt(i, true, mode);
        std::printf("RTS %s: %zu ordinary allocation failpoints, intact pool and exact homing/payload retry\n", mode,
                      count);
        return 0;
    }
    if (std::strcmp(mode, "pool-value-encode") == 0 || std::strcmp(mode, "pool-value-parse") == 0) {
        const bool encode = std::strcmp(mode, "pool-value-encode") == 0;
        const auto never  = std::numeric_limits<std::size_t>::max();
        poolValueAttempt(never, false, encode);
        const auto count = poolValueAttempt(never, false, encode);
        for (std::size_t index = 0; index < count; ++index) poolValueAttempt(index, true, encode);
        std::printf("RTS %s: %zu allocation failpoints, intact pool lifetime and exact native retry\n", mode, count);
        return 0;
    }
    if (std::strcmp(mode, "weapon-value-encode") == 0 || std::strcmp(mode, "weapon-value-parse") == 0) {
        const bool encode = std::strcmp(mode, "weapon-value-encode") == 0;
        const auto never  = std::numeric_limits<std::size_t>::max();
        weaponValueAttempt(never, false, encode);
        const auto count = weaponValueAttempt(never, false, encode);
        for (std::size_t index = 0; index < count; ++index) weaponValueAttempt(index, true, encode);
        std::printf("RTS %s: %zu allocation failpoints, intact detached source and exact full retry\n", mode, count);
        return 0;
    }
    if (std::strcmp(mode, "body-weapons-seal") == 0 || std::strcmp(mode, "body-weapons-parse") == 0) {
        const bool seal  = std::strcmp(mode, "body-weapons-seal") == 0;
        const auto never = std::numeric_limits<std::size_t>::max();
        bodyWeaponArchiveAttempt(never, false, seal);
        const auto count = bodyWeaponArchiveAttempt(never, false, seal);
        for (std::size_t index = 0; index < count; ++index) bodyWeaponArchiveAttempt(index, true, seal);
        std::printf("RTS %s: %zu allocation failpoints, intact world and exact graph/native retry\n", mode, count);
        return 0;
    }
    if (std::strcmp(mode, "archetype-encode") == 0 || std::strcmp(mode, "archetype-parse") == 0) {
        const bool encode = std::strcmp(mode, "archetype-encode") == 0;
        const auto never  = std::numeric_limits<std::size_t>::max();
        archetypeValueAttempt(never, false, encode);
        const auto count = archetypeValueAttempt(never, false, encode);
        for (std::size_t index = 0; index < count; ++index) archetypeValueAttempt(index, true, encode);
        std::printf("RTS %s: %zu allocation failpoints, intact world and exact recipe/native retry\n", mode, count);
        return 0;
    }
    if (std::strcmp(mode, "source-seal") == 0 || std::strcmp(mode, "source-parse") == 0) {
        const bool seal  = std::strcmp(mode, "source-seal") == 0;
        const auto never = std::numeric_limits<std::size_t>::max();
        sourceArchiveAttempt(never, false, seal);
        const auto count = sourceArchiveAttempt(never, false, seal);
        for (std::size_t index = 0; index < count; ++index) sourceArchiveAttempt(index, true, seal);
        std::printf("RTS %s: %zu allocation failpoints, intact world and exact source/native retry\n", mode, count);
        return 0;
    }
    if (std::strcmp(mode, "action-def-encode") == 0 || std::strcmp(mode, "action-def-parse") == 0) {
        const bool encode = std::strcmp(mode, "action-def-encode") == 0;
        const auto never  = std::numeric_limits<std::size_t>::max();
        actionDefinitionAttempt(never, false, encode);
        const auto count = actionDefinitionAttempt(never, false, encode);
        for (std::size_t index = 0; index < count; ++index) actionDefinitionAttempt(index, true, encode);
        std::printf("Action %s: %zu allocation failpoints, intact runtime and exact definition/native retry\n", mode,
                      count);
        return 0;
    }
    if (std::strcmp(mode, "action-seal") == 0 || std::strcmp(mode, "action-parse") == 0) {
        const bool seal  = std::strcmp(mode, "action-seal") == 0;
        const auto never = std::numeric_limits<std::size_t>::max();
        actionArchiveAttempt(never, false, seal);
        const auto count = actionArchiveAttempt(never, false, seal);
        for (std::size_t index = 0; index < count; ++index) actionArchiveAttempt(index, true, seal);
        std::printf("Action %s: %zu allocation failpoints, intact runtime and exact cursor/no-effect-replay retry\n",
                      mode, count);
        return 0;
    }
    if (std::strcmp(mode, "field-encode") == 0 || std::strcmp(mode, "field-parse") == 0) {
        const bool encode = std::strcmp(mode, "field-encode") == 0;
        const auto never  = std::numeric_limits<std::size_t>::max();
        fieldArchiveAttempt(never, false, encode);
        const auto count = fieldArchiveAttempt(never, false, encode);
        for (std::size_t index = 0; index < count; ++index) fieldArchiveAttempt(index, true, encode);
        std::printf("Crowd %s: %zu ordinary allocation failpoints, intact backend and exact field/capacity retry\n",
                      mode, count);
        return 0;
    }
    if (std::strcmp(mode, "backend-encode") == 0 || std::strcmp(mode, "backend-parse") == 0) {
        const bool encode = std::strcmp(mode, "backend-encode") == 0;
        const auto never  = std::numeric_limits<std::size_t>::max();
        backendArchiveAttempt(never, false, encode);
        const auto count = backendArchiveAttempt(never, false, encode);
        for (std::size_t index = 0; index < count; ++index) backendArchiveAttempt(index, true, encode);
        std::printf(
            "Crowd %s: %zu ordinary allocation failpoints, intact backends and exact request/alias/pending retry\n",
            mode, count);
        return 0;
    }
    if (std::strcmp(mode, "external-seal") == 0 || std::strcmp(mode, "external-parse") == 0) {
        const bool seal  = std::strcmp(mode, "external-seal") == 0;
        const auto never = std::numeric_limits<std::size_t>::max();
        externalNavigationArchiveAttempt(never, false, seal);
        const auto count = externalNavigationArchiveAttempt(never, false, seal);
        for (std::size_t index = 0; index < count; ++index) externalNavigationArchiveAttempt(index, true, seal);
        std::printf("Crowd %s: %zu ordinary allocation failpoints, exact external work/lease retry\n", mode, count);
        return 0;
    }
    if (std::strcmp(mode, "body-crowd-seal") == 0 || std::strcmp(mode, "body-crowd-parse") == 0) {
        const auto stage = std::strcmp(mode, "body-crowd-seal") == 0 ? Stage::BodyCrowdSeal : Stage::BodyCrowdParse;
        const auto never = std::numeric_limits<std::size_t>::max();
        attempt(stage, never, false);
        const auto count = attempt(stage, never, false);
        std::printf("RTS %s: enumerating %zu ordinary allocation failpoints\n", mode, count);
        std::fflush(stdout);
        for (std::size_t i = 0; i < count; ++i) attempt(stage, i, true);
        std::printf("RTS %s: %zu ordinary allocation failpoints, intact worlds and exact Crowd/native retry\n", mode,
                      count);
        return 0;
    }
    Stage stage = Stage::Commit;
    if (std::strcmp(mode, "prepare") == 0)
        stage = Stage::Prepare;
    else if (std::strcmp(mode, "remove") == 0)
        stage = Stage::Remove;
    else if (std::strcmp(mode, "lifecycle") == 0)
        stage = Stage::Lifecycle;
    else if (std::strcmp(mode, "capture") == 0)
        stage = Stage::Capture;
    else if (std::strcmp(mode, "root-capture") == 0)
        stage = Stage::RootCapture;
    else if (std::strcmp(mode, "action-capture") == 0)
        stage = Stage::ActionCapture;
    else if (std::strcmp(mode, "action-prepare") == 0)
        stage = Stage::ActionPrepare;
    else if (std::strcmp(mode, "action-restore") == 0)
        stage = Stage::ActionRestore;
    else if (std::strcmp(mode, "correlation-seal") == 0)
        stage = Stage::CorrelationSeal;
    else if (std::strcmp(mode, "correlation-parse") == 0)
        stage = Stage::CorrelationParse;
    else if (std::strcmp(mode, "correlation-capture") == 0)
        stage = Stage::CorrelationCapture;
    else if (std::strcmp(mode, "correlation-prepare") == 0)
        stage = Stage::CorrelationPrepare;
    else if (std::strcmp(mode, "correlation-restore") == 0)
        stage = Stage::CorrelationRestore;
    else if (std::strcmp(mode, "crowd-capture") == 0)
        stage = Stage::CrowdCapture;
    else if (std::strcmp(mode, "crowd-prepare") == 0)
        stage = Stage::CrowdPrepare;
    else if (std::strcmp(mode, "crowd-restore") == 0)
        stage = Stage::CrowdRestore;
    else if (std::strcmp(mode, "attribute-capture") == 0)
        stage = Stage::AttributeCapture;
    else if (std::strcmp(mode, "attribute-prepare") == 0)
        stage = Stage::AttributePrepare;
    else if (std::strcmp(mode, "attribute-restore") == 0)
        stage = Stage::AttributeRestore;
    else if (std::strcmp(mode, "frame-prepare") == 0)
        stage = Stage::FramePrepare;
    else if (std::strcmp(mode, "frame-restore") == 0)
        stage = Stage::FrameRestore;
    else if (std::strcmp(mode, "combat-prepare") == 0)
        stage = Stage::CombatPrepare;
    else if (std::strcmp(mode, "combat-restore") == 0)
        stage = Stage::CombatRestore;
    else if (std::strcmp(mode, "sensing-capture") == 0)
        stage = Stage::SensingCapture;
    else if (std::strcmp(mode, "sensing-prepare") == 0)
        stage = Stage::SensingPrepare;
    else if (std::strcmp(mode, "sensing-restore") == 0)
        stage = Stage::SensingRestore;
    else if (std::strcmp(mode, "weapon-prepare") == 0)
        stage = Stage::WeaponPrepare;
    else if (std::strcmp(mode, "weapon-commit") == 0)
        stage = Stage::WeaponCommit;
    else if (std::strcmp(mode, "weapon-create-attributes") == 0)
        stage = Stage::WeaponCommitCreate;
    else if (std::strcmp(mode, "request-capture") == 0)
        stage = Stage::RequestCapture;
    else if (std::strcmp(mode, "request-prepare") == 0)
        stage = Stage::RequestPrepare;
    else if (std::strcmp(mode, "request-restore") == 0)
        stage = Stage::RequestRestore;
    else if (std::strcmp(mode, "shared-flow-capture") == 0)
        stage = Stage::SharedFlowCapture;
    else if (std::strcmp(mode, "shared-flow-prepare") == 0)
        stage = Stage::SharedFlowPrepare;
    else if (std::strcmp(mode, "shared-flow-restore") == 0)
        stage = Stage::SharedFlowRestore;
    else if (std::strcmp(mode, "body-crowd-capture") == 0)
        stage = Stage::BodyCrowdCapture;
    else if (std::strcmp(mode, "body-crowd-prepare") == 0)
        stage = Stage::BodyCrowdPrepare;
    else if (std::strcmp(mode, "body-crowd-restore") == 0)
        stage = Stage::BodyCrowdRestore;
    else if (std::strcmp(mode, "body-weapon-prepare") == 0)
        stage = Stage::BodyWeaponPrepare;
    else if (std::strcmp(mode, "body-weapon-materialize") == 0)
        stage = Stage::BodyWeaponCommit;
    else if (std::strcmp(mode, "source-restore-factory") == 0)
        stage = Stage::SourceFactory;
    else if (std::strcmp(mode, "source-restore-bind") == 0)
        stage = Stage::SourceBind;
    else if (std::strcmp(mode, "unit-attribute-capture") == 0)
        stage = Stage::UnitAttributeCapture;
    else if (std::strcmp(mode, "unit-attribute-prepare") == 0)
        stage = Stage::UnitAttributePrepare;
    else if (std::strcmp(mode, "unit-attribute-restore") == 0)
        stage = Stage::UnitAttributeRestore;
    else if (std::strcmp(mode, "graph-restore-prepare") == 0)
        stage = Stage::GraphPrepare;
    else if (std::strcmp(mode, "graph-restore-commit") == 0)
        stage = Stage::GraphCommit;
    else if (std::strcmp(mode, "graph-restore-create-attributes") == 0)
        stage = Stage::GraphCommitCreate;
    else if (std::strcmp(mode, "pool-restore-prepare") == 0)
        stage = Stage::PoolRestorePrepare;
    else if (std::strcmp(mode, "pool-restore-commit") == 0)
        stage = Stage::PoolRestoreCommit;
    else if (std::strcmp(mode, "pool-graph") == 0)
        stage = Stage::PoolGraph;
    else if (std::strcmp(mode, "pool") == 0)
        stage = Stage::Pool;
    else if (std::strcmp(mode, "commit") != 0)
        return 1;
    const auto never = std::numeric_limits<std::size_t>::max();
    attempt(stage, never, false);
    const auto count = attempt(stage, never, false);
    for (std::size_t index = 0; index < count; ++index) attempt(stage, index, true);
    std::printf(
        "RTS factory %s: %zu allocation failpoints, intact domain/index/journal and successful same-world retry\n",
        mode, count);
}
