// clang-format off
/* eve-test-policy:
{"schema":"evengine.test-policy","version":1,"role":"tests","default":"module-composition","module":"rts","task":"rts_spawn","modules":["rts","spawn","rts_spawn","crowd","definitions","action","map"]}
*/
// clang-format on
#include <simplesquirrel/simplesquirrel.hpp>
#include "RTSCrowdRestoreFixture.h"
#include "map/Pathfinder.h"
#include "map/PathfinderState.h"
#include "rts/RTSNavigationCapture.h"
#include "zeroerr/unittest.h"
namespace test_rts_spawn {
using namespace eve;
namespace {
rts::RTSCrowdCapture wireCrowd(const rts::RTSCrowdCapture& source) {
    const auto epoch    = source.saveEpoch();
    const auto tick     = source.frame()->get().tick;
    auto       envelope = source.seal(Revision(9), tick, restoreHash).expect("seal full RTS Crowd");
    auto       text     = serializeSnapshotEnvelope(envelope).expect("write RTS Crowd file");
    auto       read     = parseSnapshotEnvelope(text, restoreHash).expect("read RTS Crowd file");
    auto       parsed = rts::RTSCrowdCapture::parse(std::move(read), epoch, restoreHash).expect("admit RTS Crowd file");
    auto       repeated = parsed.seal(Revision(9), tick, restoreHash).expect("reseal admitted RTS Crowd");
    CHECK_EQ(serializeSnapshotEnvelope(repeated).expect("repeat file"), text);
    return parsed;
}
}  // namespace
TEST_CASE("RTS native Crowd bindings preserve velocity and next movement after root reconstruction") {
    for (bool persistent : {false, true}) {
        Fixture source(crowdMoveCommand());
        source.sourceWorld->advance({SimulationTick(1), Duration(0)}).expect("real members");
        crowdStep(source, 1);
        const auto epoch    = source.request.source.owner.child("crowd-continuation");
        auto       captured = source.domain->captureCrowd(epoch).expect("Crowd capture");
        if (persistent) captured = wireCrowd(captured);
        auto             saved = freezeSpawn(source, epoch);
        RestoredRTSSpawn target(saved);
        auto candidate = rts::PreparedRTSCrowdRestore::prepare(*target.domain, captured).expect("binding preparation");
        REQUIRE_EQ(target.crowd.getAgentCount(), 0);
        target.domain->restoreCrowd(std::move(candidate)).expect("native backend and binding publication");
        REQUIRE_EQ(target.crowd.getAgentCount(), 2);
        for (const auto& member : captured.members()) {
            REQUIRE(member.frameIndex);
            const auto&      old = captured.frame()->get().agents[*member.frameIndex];
            ecs::ScopedTable scope(*target.table);
            auto             settings = target.domain->findUnit(member.subject)->crowd();
            crowd::AgentRef  ref{settings->runtimeAgent, settings->runtimeOwner};
            REQUIRE(target.crowd.isAgentAlive(ref));
            CHECK(ref.owner != old.reference.owner);
            CHECK(!target.crowd.isAgentAlive(old.reference));
            const auto state = target.crowd.readAgent(ref).expect("new native agent");
            CHECK_EQ(state.vx, old.state.vx);
            CHECK_EQ(state.vy, old.state.vy);
            CHECK((state.vx != 0.f || state.vy != 0.f));
            CHECK_EQ(settings->radius, member.radius);
            CHECK_EQ(settings->heading, member.heading);
            CHECK_EQ(settings->link.key(), member.key);
        }
        crowdStep(source, 2);
        {
            ecs::ScopedTable scope(*target.table);
            rts::CrowdMotionSystem::step({SimulationTick(2), Duration(100000000)}, target.crowd)
                .expect("continued target step");
        }
        for (const auto& member : captured.members()) {
            auto a = source.crowd.readAgent(source.crowd.resolveNamedAgent(member.key).expect("source key"))
                         .expect("source state");
            auto b = target.crowd.readAgent(target.crowd.resolveNamedAgent(member.key).expect("target key"))
                         .expect("target state");
            CHECK_EQ(a.x, b.x);
            CHECK_EQ(a.y, b.y);
            CHECK_EQ(a.vx, b.vx);
            CHECK_EQ(a.vy, b.vy);
            CHECK_EQ(a.heading, b.heading);
        }
        REQUIRE(!target.domain->restoreCrowd(std::move(candidate)).ok());
        REQUIRE(!rts::PreparedRTSCrowdRestore::prepare(*target.domain, captured).ok());
    }
}
TEST_CASE("RTS native Crowd restoration rejects changed targets without publishing and permits retry") {
    auto saved = freezeCrowdScope();
    REQUIRE(saved.sourceWorld.expired());
    RestoredRTSSpawn target(saved.session), foreign(saved.session);
    auto candidate = rts::PreparedRTSCrowdRestore::prepare(*target.domain, *saved.crowd).expect("CPU candidate");
    REQUIRE(!foreign.domain->restoreCrowd(std::move(candidate)).ok());
    {
        auto view = ecs::View<rts::Unit>(*target.table);
        REQUIRE(!target.domain->restoreCrowd(std::move(candidate)).ok());
        CHECK_EQ(target.crowd.getAgentCount(), 0);
    }
    {
        ecs::ScopedTable scope(*target.table);
        auto*            unit      = target.domain->findUnit(saved.crowd->members()[0].subject);
        const auto       originalX = unit->motion()->x;
        unit->motion()->x          = originalX + 1.f;
        REQUIRE(!target.domain->restoreCrowd(std::move(candidate)).ok());
        CHECK_EQ(target.crowd.getAgentCount(), 0);
        CHECK(!unit->crowd()->link.isBound());
        unit->motion()->x   = originalX;
        unit->crowd()->link = rts::CrowdLink::bind("claimed").expect("claim");
        REQUIRE(!target.domain->restoreCrowd(std::move(candidate)).ok());
        unit->crowd()->link.reset();
        auto* buffer = unit->getComponentManager().getComponentBuffer<rts::Unit::Crowd>();
        buffer->reset_slot(unit->id);
        REQUIRE(!target.domain->restoreCrowd(std::move(candidate)).ok());
        CHECK(!buffer->has(unit->id));
        CHECK_EQ(target.crowd.getAgentCount(), 0);
        REQUIRE(buffer->emplace(unit->id));
    }
    crowd::Crowd alternate;
    target.domain->setCrowdProvider(&alternate);
    REQUIRE(!target.domain->restoreCrowd(std::move(candidate)).ok());
    target.domain->setCrowdProvider(&target.crowd);
    saved.crowd.reset();
    target.domain->restoreCrowd(std::move(candidate)).expect("same candidate after source capture destruction");
    CHECK_EQ(target.crowd.getAgentCount(), 2);
}
TEST_CASE("RTS native Crowd restoration never claims replacement Unit generations or retains world owners") {
    auto             saved = freezeCrowdScope();
    RestoredRTSSpawn target(saved.session);
    auto candidate = rts::PreparedRTSCrowdRestore::prepare(*target.domain, *saved.crowd).expect("original generation");
    {
        ecs::ScopedTable scope(*target.table);
        const auto       id       = saved.crowd->members()[0].subject;
        auto*            original = target.domain->findUnit(id);
        const auto       retired  = ecs::handle_of(original);
        original->release();
        auto* replacement = rts::Unit::createUnit(id);
        REQUIRE(!ecs::try_get(retired));
        REQUIRE(!target.domain->restoreCrowd(std::move(candidate)).ok());
        CHECK(!replacement->crowd()->link.isBound());
        CHECK_EQ(target.crowd.getAgentCount(), 0);
        replacement->release();
    }
    std::optional<rts::PreparedRTSCrowdRestore> orphan;
    std::weak_ptr<ecs::Table>                   witness;
    {
        RestoredRTSSpawn temporary(saved.session);
        witness = temporary.table;
        orphan.emplace(
            rts::PreparedRTSCrowdRestore::prepare(*temporary.domain, *saved.crowd).expect("weak target candidate"));
    }
    REQUIRE(witness.expired());
    orphan.reset();
}
TEST_CASE("RTS native Crowd capture rejects unowned agents and missing providers") {
    Fixture    source;
    const auto epoch = source.request.source.owner.child("empty-crowd");
    auto       empty = source.domain->captureCrowd(epoch).expect("empty owned scope");
    empty            = wireCrowd(empty);
    REQUIRE(empty.members().empty());
    source.crowd.addNamedAgent("unowned", 1.f, 2.f, 0.f, .5f);
    REQUIRE(!source.domain->captureCrowd(epoch).ok());
    REQUIRE(source.crowd.removeNamedAgent("unowned"));
    source.domain->setCrowdProvider(nullptr);
    REQUIRE(!source.domain->captureCrowd(epoch).ok());
    REQUIRE(!source.domain->captureCrowd({}).ok());
}
TEST_CASE("RTS native Crowd binding restore preserves pending detach and rename transitions") {
    Fixture source(crowdMoveCommand());
    source.sourceWorld->advance({SimulationTick(1), Duration(0)}).expect("source population");
    crowdStep(source, 1);
    const auto detached = source.request.units[0].subject;
    const auto renamed  = source.request.units[1].subject;
    {
        ecs::ScopedTable scope(*source.table);
        source.domain->findUnit(detached)->crowd()->link.reset();
        source.domain->findUnit(renamed)->crowd()->link =
            rts::CrowdLink::bind("renamed-agent").expect("pending rename");
    }
    const auto epoch       = source.request.source.owner.child("pending-bindings");
    auto       frozen      = source.domain->captureCrowd(epoch).expect("pending transitions");
    frozen                 = wireCrowd(frozen);
    auto             saved = freezeSpawn(source, epoch);
    RestoredRTSSpawn target(saved);
    auto plan = rts::PreparedRTSCrowdRestore::prepare(*target.domain, frozen).expect("pending native candidate");
    target.domain->restoreCrowd(std::move(plan)).expect("pending transition publication");
    REQUIRE_EQ(target.crowd.getAgentCount(), 2);
    {
        ecs::ScopedTable scope(*target.table);
        const auto       a = target.domain->findUnit(detached)->crowd();
        const auto       b = target.domain->findUnit(renamed)->crowd();
        CHECK(!a->link.isBound());
        CHECK(target.crowd.isAgentAlive({a->runtimeAgent, a->runtimeOwner}));
        CHECK(b->runtimeBindingKey != b->link.key());
    }
    crowdStep(source, 2);
    {
        ecs::ScopedTable scope(*target.table);
        rts::CrowdMotionSystem::step({SimulationTick(2), Duration(100000000)}, target.crowd)
            .expect("pending target transitions");
    }
    REQUIRE_EQ(source.crowd.getAgentCount(), 1);
    REQUIRE_EQ(target.crowd.getAgentCount(), 1);
    const auto a = source.crowd.readAgent(source.crowd.resolveNamedAgent("renamed-agent").expect("source rename"))
                       .expect("source state");
    const auto b = target.crowd.readAgent(target.crowd.resolveNamedAgent("renamed-agent").expect("target rename"))
                       .expect("target state");
    CHECK_EQ(a.x, b.x);
    CHECK_EQ(a.y, b.y);
    CHECK_EQ(a.vx, b.vx);
    CHECK_EQ(a.vy, b.vy);
}
TEST_CASE("RTS native Crowd candidates reject dead providers and preserve empty unlinked scopes") {
    auto             saved = freezeCrowdScope();
    RestoredRTSSpawn target(saved.session);
    auto             backend = std::make_unique<crowd::Crowd>();
    target.domain->setCrowdProvider(backend.get());
    auto plan =
        rts::PreparedRTSCrowdRestore::prepare(*target.domain, *saved.crowd).expect("physical provider candidate");
    backend.reset();
    REQUIRE(!target.domain->restoreCrowd(std::move(plan)).ok());
    target.domain->setCrowdProvider(&target.crowd);
    REQUIRE(!target.domain->restoreCrowd(std::move(plan)).ok());
    Fixture    source;
    const auto epoch = source.request.source.owner.child("unlinked-native");
    const auto id    = SubjectRef::fromPersistentId(epoch.child("unlinked-unit"));
    {
        ecs::ScopedTable scope(*source.table);
        source.domain->newUnit(id).expect("non-generated unlinked root");
    }
    auto capture = source.domain->captureCrowd(epoch).expect("unlinked body scope");
    REQUIRE_EQ(capture.members().size(), std::size_t(1));
    REQUIRE(!capture.members()[0].frameIndex);
    auto             roots = freezeSpawn(source, epoch);
    RestoredRTSSpawn unlinked(roots);
    auto candidate = rts::PreparedRTSCrowdRestore::prepare(*unlinked.domain, capture).expect("unlinked candidate");
    unlinked.domain->restoreCrowd(std::move(candidate)).expect("unlinked publication");
    REQUIRE_EQ(unlinked.crowd.getAgentCount(), 0);
    ecs::ScopedTable scope(*unlinked.table);
    REQUIRE(!unlinked.domain->findUnit(id)->crowd()->link.isBound());
}
TEST_CASE("RTS native Crowd candidate remaps captured request tokens through the actual backend") {
    std::optional<rts::RTSCrowdCapture>         frozen;
    std::unique_ptr<rts::RTS>                   target;
    std::optional<rts::PreparedRTSCrowdRestore> prepared;
    ssq::VM                                     vm(1024, ssq::Libs::ALL);
    auto                                        eve = vm.addTable("eve");
    auto                                        cls = eve.addClass<crowd::Crowd>("Crowd");
    crowd::Crowd::expose(cls);
    vm.addFunc("freezeRTS", [&](crowd::Crowd* backend) {
        auto     table = std::make_shared<ecs::Table>();
        rts::RTS source(table);
        source.setCrowdProvider(backend);
        frozen.emplace(source.captureCrowd(*PersistentId::parse("01900000-0000-7000-8000-000000000d31"))
                           .expect("actual RTS request registry capture"));
    });
    vm.addFunc("prepareRTS", [&](crowd::Crowd* backend) {
        target = std::make_unique<rts::RTS>(std::make_shared<ecs::Table>());
        target->setCrowdProvider(backend);
        prepared.emplace(
            rts::PreparedRTSCrowdRestore::prepare(*target, *frozen).expect("actual RTS request candidate"));
    });
    vm.addFunc("remapRTS", [&](std::int64_t owner, int index, int generation) -> std::int64_t {
        auto mapped =
            prepared->remapFlowRequest({RuntimeHandle<crowd::CrowdFlowRequestTag>(
                                            static_cast<std::uint32_t>(index), static_cast<std::uint32_t>(generation)),
                                        static_cast<std::uint64_t>(owner)});
        return mapped ? static_cast<std::int64_t>(mapped.value().owner) : 0;
    });
    vm.addFunc("commitRTS", [&]() {
        frozen.reset();
        target->restoreCrowd(std::move(*prepared)).expect("actual RTS request publication");
    });
    vm.addFunc("closeRTS", [&]() { target.reset(); });
    vm.run(vm.compileSource(R"(
        local c=eve.Crowd(); c.resizeField(8,8,1.0,0.0,0.0);
        local token=c.requestFlowBuild(7,7).value;
        assert(c.advanceFlowRequest(token,5).ok);
        freezeRTS(c);
        local expected=c.advanceFlowRequest(token,3).value;
        c=null; collectgarbage();
        local t=eve.Crowd(); prepareRTS(t);
        local mapped=clone token;
        mapped.owner=remapRTS(token.owner,token.index,token.generation);
        assert(mapped.owner!=0 && mapped.owner!=token.owner);
        assert(!t.getFlowRequest(mapped).ok);
        commitRTS();
        assert(remapRTS(token.owner,token.index,token.generation)==0);
        assert(!t.getFlowRequest(token).ok);
        local actual=t.advanceFlowRequest(mapped,3).value;
        assert(actual.state==expected.state && actual.heapPops==expected.heapPops && actual.directionCells==expected.directionCells);
        closeRTS();
    )"));
}
TEST_CASE("RTS shared-flow restore preserves pending jobs quotas group projection and continued movement") {
    for (bool persistent : {false, true}) {
        SharedFlowFixture source;
        const auto        epoch  = source.request.source.owner.child("shared-flow-jobs");
        const auto        before = source.domain->getCrowdFlowNavigationStatus();
        REQUIRE(before.pendingGoals > 0);
        REQUIRE_EQ(before.lastWork, 3);
        auto capture = source.domain->captureCrowd(epoch).expect("native jobs and groups");
        if (persistent) capture = wireCrowd(capture);
        auto             saved = freezeSpawn(source, epoch);
        RestoredRTSSpawn target(saved);
        auto prepared = rts::PreparedRTSCrowdRestore::prepare(*target.domain, capture).expect("native job candidates");
        target.domain->restoreCrowd(std::move(prepared)).expect("backend bodies and job owner publication");
        action::ActionRuntime actions;
        rts::ActionAdapter    executor(actions);
        auto                  equal = [](const auto& a, const auto& b) {
            return a.pendingGoals == b.pendingGoals && a.readyGoals == b.readyGoals && a.lastWork == b.lastWork &&
                   a.lastStarts == b.lastStarts && a.prepared == b.prepared && a.cacheHits == b.cacheHits &&
                   a.cachePressure == b.cachePressure;
        };
        REQUIRE(equal(before, target.domain->getCrowdFlowNavigationStatus()));
        source.step(1, 0);
        {
            ecs::ScopedTable scope(*target.table);
            target.domain->step({SimulationTick(1), Duration(0)}, executor).expect("same tick reconciliation");
        }
        REQUIRE(equal(before, target.domain->getCrowdFlowNavigationStatus()));
        source.domain->configureCrowdFlowNavigation({1000, 1}).expect("source work quota");
        target.domain->configureCrowdFlowNavigation({1000, 1}).expect("target work quota");
        for (std::uint64_t tick = 2; tick <= 8; ++tick) {
            source.step(tick);
            {
                ecs::ScopedTable scope(*target.table);
                target.domain->step({SimulationTick(tick), Duration(100000000)}, executor)
                    .expect("restored shared-flow step");
            }
            REQUIRE(
                equal(source.domain->getCrowdFlowNavigationStatus(), target.domain->getCrowdFlowNavigationStatus()));
            for (const auto& member : capture.members()) {
                const auto a =
                    source.crowd.readAgent(source.crowd.resolveNamedAgent(member.key).expect("source binding"))
                        .expect("source state");
                const auto b =
                    target.crowd.readAgent(target.crowd.resolveNamedAgent(member.key).expect("target binding"))
                        .expect("target state");
                CHECK_EQ(a.x, b.x);
                CHECK_EQ(a.y, b.y);
                CHECK_EQ(a.vx, b.vx);
                CHECK_EQ(a.vy, b.vy);
                CHECK_EQ(a.flowGeneration, b.flowGeneration);
            }
        }
        CHECK(source.domain->getCrowdFlowNavigationStatus().readyGoals > 0);
        CHECK_EQ(target.domain->getCrowdFlowNavigationStatus().prepared, before.prepared);
    }
}
TEST_CASE("RTS persistent shared-flow jobs continue identically after source world destruction") {
    SavedRTSSpawn                                    saved;
    std::weak_ptr<ecs::Table>                        sourceWorld;
    std::string                                      wire;
    PersistentId                                     epoch;
    std::vector<crowd::AgentFrame>                   frames;
    std::vector<rts::CrowdFlowNavigationObservation> observations;
    {
        SharedFlowFixture source;
        sourceWorld = source.table;
        epoch       = source.request.source.owner.child("file-source-retirement");
        source.domain->configureCrowdFlowNavigation({1000, 1}).expect("future shared work quota");
        auto captured = source.domain->captureCrowd(epoch).expect("active pending jobs");
        wire          = serializeSnapshotEnvelope(
                   captured.seal(Revision(5), SimulationTick(1), restoreHash).expect("seal active source file"))
                   .expect("write active source file");
        saved = freezeSpawn(source, epoch);
        for (std::uint64_t tick = 2; tick <= 8; ++tick) {
            source.step(tick);
            crowd::AgentFrame frame;
            source.crowd.copyAgentFrame(frame).expect("expected native continuation");
            frames.push_back(std::move(frame));
            observations.push_back(source.domain->getCrowdFlowNavigationStatus());
        }
    }
    REQUIRE(sourceWorld.expired());
    auto envelope = parseSnapshotEnvelope(wire, restoreHash).expect("read after world destruction");
    auto captured = rts::RTSCrowdCapture::parse(std::move(envelope), epoch, restoreHash).expect("admit without source");
    RestoredRTSSpawn target(saved);
    auto candidate = rts::PreparedRTSCrowdRestore::prepare(*target.domain, captured).expect("file backend candidate");
    target.domain->restoreCrowd(std::move(candidate)).expect("publish without source world");
    action::ActionRuntime actions;
    rts::ActionAdapter    executor(actions);
    for (std::uint64_t tick = 2; tick <= 8; ++tick) {
        ecs::ScopedTable scope(*target.table);
        target.domain->step({SimulationTick(tick), Duration(100000000)}, executor)
            .expect("restored shared job continuation");
        crowd::AgentFrame actual;
        target.crowd.copyAgentFrame(actual).expect("actual native continuation");
        const auto& expected = frames[tick - 2];
        REQUIRE_EQ(actual.agents.size(), expected.agents.size());
        for (std::size_t i = 0; i < actual.agents.size(); ++i) {
            CHECK_EQ(actual.agents[i].state.x, expected.agents[i].state.x);
            CHECK_EQ(actual.agents[i].state.y, expected.agents[i].state.y);
            CHECK_EQ(actual.agents[i].state.vx, expected.agents[i].state.vx);
            CHECK_EQ(actual.agents[i].state.vy, expected.agents[i].state.vy);
            CHECK(!target.crowd.isAgentAlive(expected.agents[i].reference));
        }
        const auto  stats  = target.domain->getCrowdFlowNavigationStatus();
        const auto& before = observations[tick - 2];
        CHECK_EQ(stats.lastWork, before.lastWork);
        CHECK_EQ(stats.lastStarts, before.lastStarts);
        CHECK_EQ(stats.pendingGoals, before.pendingGoals);
        CHECK_EQ(stats.readyGoals, before.readyGoals);
        CHECK_EQ(stats.prepared, before.prepared);
        CHECK_EQ(stats.cacheHits, before.cacheHits);
    }
}
TEST_CASE("RTS persistent Crowd rejects corrupt correspondences and job fields") {
    SharedFlowFixture source;
    const auto        epoch    = source.request.source.owner.child("crowd-file-corruption");
    auto              capture  = source.domain->captureCrowd(epoch).expect("shared-flow capture");
    const auto        envelope = capture.seal(Revision(7), SimulationTick(1), restoreHash).expect("shared-flow seal");
    for (int mode = 0; mode < 4; ++mode) {
        auto  payload = envelope.payload;
        auto& root    = *payload.getIf<Value::Object>();
        if (mode == 0) root.emplace("unknown", true);
        if (mode == 1) {
            auto& rows   = *root.at("members").getIf<Value::Array>();
            auto& member = *rows[0].getIf<Value::Array>();
            member[3]    = "not-a-saved-agent";
        }
        if (mode == 2) {
            auto& navigation = *root.at("navigation").getIf<Value::Array>();
            auto& jobs       = *navigation[10].getIf<Value::Array>();
            auto& job        = *jobs[0].getIf<Value::Array>();
            job[1]           = Value::Array{Value::Array{0, 0}};
        }
        if (mode == 3) {
            auto& nav   = *root.at("navigation").getIf<Value::Array>();
            auto& nodes = *nav[11].getIf<Value::Array>();
            REQUIRE(!nodes.empty());
            nodes[0] = Value::Array{Value::Array{2147483647, 0}};
        }
        auto corrupt = makeSnapshotEnvelope(envelope.type, envelope.schema, envelope.schemaVersion, epoch,
                                            envelope.revision, envelope.tick, std::move(payload), restoreHash)
                           .expect("seal corrupted facts");
        REQUIRE(!rts::RTSCrowdCapture::parse(std::move(corrupt), epoch, restoreHash).ok());
    }
    auto restored = wireCrowd(capture);
    CHECK_EQ(restored.members().size(), capture.members().size());
}
TEST_CASE("RTS persistent Crowd hash callback may retire the capture") {
    SharedFlowFixture                   source;
    const auto                          epoch = source.request.source.owner.child("crowd-file-retirement");
    std::optional<rts::RTSCrowdCapture> capture(source.domain->captureCrowd(epoch).expect("capture"));
    const auto                          saved    = freezeSpawn(source, epoch);
    const SnapshotHashProvider          retiring = [&](std::string_view text) {
        capture.reset();
        return restoreHash(text);
    };
    auto envelope = capture->seal(Revision(4), SimulationTick(1), retiring).expect("seal after callback retirement");
    REQUIRE(!capture);
    auto parsed = rts::RTSCrowdCapture::parse(std::move(envelope), epoch, restoreHash).expect("read detached capture");
    RestoredRTSSpawn target(saved);
    auto prepared = rts::PreparedRTSCrowdRestore::prepare(*target.domain, parsed).expect("prepare detached file");
    target.domain->restoreCrowd(std::move(prepared)).expect("publish detached file");
    CHECK_EQ(target.crowd.getAgentCount(), source.crowd.getAgentCount());
}
TEST_CASE("RTS shared-flow restore survives source death and preserves deferred disable") {
    for (bool persistent : {false, true}) {
        std::optional<rts::RTSCrowdCapture> capture;
        SavedRTSSpawn                       saved;
        std::weak_ptr<ecs::Table>           sourceWorld;
        {
            SharedFlowFixture source(1000000);
            source.domain->clearCrowdFlowNavigation();
            capture.emplace(source.domain->captureCrowd(source.request.source.owner.child("disabled-flow"))
                                .expect("disabled service with pending binding clear"));
            if (persistent) *capture = wireCrowd(*capture);
            saved       = freezeSpawn(source, capture->saveEpoch());
            sourceWorld = source.table;
        }
        REQUIRE(sourceWorld.expired());
        RestoredRTSSpawn target(saved);
        auto             prepared =
            rts::PreparedRTSCrowdRestore::prepare(*target.domain, *capture).expect("detached disabled policy");
        capture.reset();
        target.domain->restoreCrowd(std::move(prepared)).expect("disabled policy publication");
        action::ActionRuntime actions;
        rts::ActionAdapter    executor(actions);
        {
            ecs::ScopedTable scope(*target.table);
            target.domain->step({SimulationTick(2), Duration(100000000)}, executor).expect("canonical deferred clear");
        }
        crowd::AgentFrame frame;
        target.crowd.copyAgentFrame(frame).expect("cleared restored selections");
        for (const auto& row : frame.agents) CHECK_EQ(row.state.flowGeneration, 0);
        CHECK_EQ(target.domain->getCrowdFlowNavigationStatus().prepared, 0);
    }
}
TEST_CASE("RTS shared-flow restore preserves a reformed member outside destination cells") {
    SharedFlowFixture source;
    const auto        moving   = source.request.units[1].subject;
    const auto        waiting  = source.request.units[0].subject;
    auto              relocate = [&](rts::WorldPosition position) {
        ecs::ScopedTable scope(*source.table);
        auto*            unit     = source.domain->findUnit(moving);
        auto             settings = unit->crowd();
        source.crowd.relocateAgents({{{{settings->runtimeAgent, settings->runtimeOwner}, position.x, position.y}}})
            .expect("canonical source teleport");
        unit->motion()->x = position.x;
        unit->motion()->y = position.y;
    };
    rts::WorldPosition otherSlot;
    {
        ecs::ScopedTable scope(*source.table);
        otherSlot = source.domain->findUnit(waiting)->orders()->values.current().expect("other member slot").target;
    }
    relocate(otherSlot);
    source.step(2);
    REQUIRE_EQ(source.domain->getCrowdFlowNavigationStatus().prepared, std::uint64_t(2));
    relocate({otherSlot.x, otherSlot.y - 1.f});
    const auto       epoch   = source.request.source.owner.child("reformed-flow-member");
    auto             capture = source.domain->captureCrowd(epoch).expect("reformed source field choice");
    auto             saved   = freezeSpawn(source, epoch);
    RestoredRTSSpawn target(saved);
    auto candidate = rts::PreparedRTSCrowdRestore::prepare(*target.domain, capture).expect("reformed native candidate");
    target.domain->restoreCrowd(std::move(candidate)).expect("reformed publication");
    action::ActionRuntime actions;
    rts::ActionAdapter    executor(actions);
    source.domain->configureCrowdFlowNavigation({1000, 1}).expect("source quota");
    target.domain->configureCrowdFlowNavigation({1000, 1}).expect("target quota");
    for (std::uint64_t tick = 3; tick <= 8; ++tick) {
        source.step(tick);
        {
            ecs::ScopedTable scope(*target.table);
            target.domain->step({SimulationTick(tick), Duration(100000000)}, executor).expect("reformed target step");
        }
        const auto a = source.domain->getCrowdFlowNavigationStatus(), b = target.domain->getCrowdFlowNavigationStatus();
        REQUIRE_EQ(a.prepared, b.prepared);
        CHECK_EQ(a.cacheHits, b.cacheHits);
        CHECK_EQ(a.pendingGoals, b.pendingGoals);
        CHECK_EQ(a.readyGoals, b.readyGoals);
        CHECK_EQ(a.lastWork, b.lastWork);
        CHECK_EQ(a.lastStarts, b.lastStarts);
        const auto& key = capture.members()[0].subject == moving ? capture.members()[0].key : capture.members()[1].key;
        const auto  expected =
            source.crowd.readAgent(source.crowd.resolveNamedAgent(key).expect("source reformed agent"))
                .expect("source pose");
        const auto actual = target.crowd.readAgent(target.crowd.resolveNamedAgent(key).expect("target reformed agent"))
                                .expect("target pose");
        CHECK_EQ(expected.x, actual.x);
        CHECK_EQ(expected.y, actual.y);
    }
}
TEST_CASE("RTS shared-flow restored ready job and agent pins retire independently of frozen source") {
    std::optional<rts::RTSCrowdCapture> capture;
    SavedRTSSpawn                       saved;
    std::weak_ptr<ecs::Table>           sourceWorld;
    {
        SharedFlowFixture source(1000000);
        REQUIRE(source.domain->getCrowdFlowNavigationStatus().readyGoals > 0);
        capture.emplace(source.domain->captureCrowd(source.request.source.owner.child("ready-shared-flow"))
                            .expect("ready native field job"));
        *capture    = wireCrowd(*capture);
        saved       = freezeSpawn(source, capture->saveEpoch());
        sourceWorld = source.table;
    }
    REQUIRE(sourceWorld.expired());
    RestoredRTSSpawn target(saved);
    auto prepared = rts::PreparedRTSCrowdRestore::prepare(*target.domain, *capture).expect("ready field job candidate");
    target.domain->restoreCrowd(std::move(prepared)).expect("ready job publication");
    REQUIRE(target.domain->getCrowdFlowNavigationStatus().readyGoals > 0);
    REQUIRE(!target.crowd.configureFlowCache(0).ok());
    target.domain->clearCrowdFlowNavigation();
    REQUIRE(!target.crowd.configureFlowCache(0).ok());
    action::ActionRuntime actions;
    rts::ActionAdapter    executor(actions);
    {
        ecs::ScopedTable scope(*target.table);
        target.domain->step({SimulationTick(2), Duration(100000000)}, executor).expect("retire remaining agent leases");
    }
    REQUIRE(target.crowd.configureFlowCache(0).ok());
    REQUIRE_EQ(capture->frame()->get().agents.size(), std::size_t(2));
}

rts::RTSNavigationCapture navigationWireRoundTrip(const rts::RTSNavigationCapture& source) {
    auto       saved = source.seal(Revision(9), SimulationTick(2), restoreHash).expect("seal navigation tracker");
    const auto text  = serializeSnapshotEnvelope(saved).expect("navigation file");
    auto parsed = rts::RTSNavigationCapture::parse(parseSnapshotEnvelope(text, restoreHash).expect("read navigation"),
                                                   source.saveEpoch(), restoreHash)
                      .expect("admit navigation");
    REQUIRE_EQ(serializeSnapshotEnvelope(parsed.seal(Revision(9), SimulationTick(2), restoreHash).expect("repeat seal"))
                   .expect("repeat navigation"),
               text);
    return parsed;
}
TEST_CASE("RTS navigation archive rejects invalid settings publications and progress without mutation") {
    map::Pathfinder   navigation(32, 32);
    SharedFlowFixture source;
    source.domain->setNavigationProvider(&navigation);
    source.domain
        ->configureCrowdSurfaceTracking({crowd::NavigationSurfaceId(1), crowd::MovementClassId(1), 1, 1, .35f, 1000000})
        .expect("tracker");
    source.step(2);
    const auto epoch   = source.request.source.owner.child("navigation-archive");
    auto       capture = source.domain->captureNavigation(epoch, navigation).expect("freeze");
    const auto base    = capture.seal(Revision(9), SimulationTick(2), restoreHash).expect("seal");
    auto       tracker = [](Value& payload) -> Value::Array& {
        return *payload.getIf<Value::Object>()->at("tracker").getIf<Value::Array>();
    };
    auto settings    = [&](Value& payload) -> Value::Array& { return *tracker(payload)[0].getIf<Value::Array>(); };
    auto publication = [&](Value& payload) -> Value::Array& { return *tracker(payload)[3].getIf<Value::Array>(); };
    auto refused     = [&](SnapshotEnvelope value) {
        value = makeSnapshotEnvelope(value.type, value.schema, value.schemaVersion, value.instanceId, value.revision,
                                         value.tick, std::move(value.payload), restoreHash)
                    .expect("rehash input");
        REQUIRE(!rts::RTSNavigationCapture::parse(std::move(value), epoch, restoreHash).ok());
    };
    auto edit = [&](auto mutate) {
        auto value = base;
        mutate(value.payload);
        refused(std::move(value));
    };
    edit([](Value& v) { v.set("unknown", true); });
    edit([](Value& v) { v.getIf<Value::Object>()->erase("tracker"); });
    edit([&](Value& v) { tracker(v).push_back(false); });
    edit([&](Value& v) { settings(v)[0] = "0"; });
    edit([&](Value& v) { settings(v)[1] = "01"; });
    edit([&](Value& v) { settings(v)[2] = "0"; });
    edit([&](Value& v) { settings(v)[4] = -1.f; });
    edit([&](Value& v) { settings(v)[5] = 10000001; });
    edit([&](Value& v) { settings(v)[5] = true; });
    edit([&](Value& v) { publication(v)[0] = "2"; });
    edit([&](Value& v) { publication(v)[2] = "0"; });
    edit([&](Value& v) { publication(v)[3] = "9"; });
    edit([&](Value& v) { publication(v)[5] = .75f; });
    edit([&](Value& v) { tracker(v)[4] = "0"; });
    edit([&](Value& v) { tracker(v)[1].getIf<Value::Array>()->at(0) = "0"; });
    edit([&](Value& v) { tracker(v)[5] = 1; });
    edit([](Value& v) { v.getIf<Value::Object>()->at("desired").getIf<Value::Array>()->at(0) = 0; });
    edit([](Value& v) {
        v.getIf<Value::Object>()->at("desired").getIf<Value::Array>()->at(1) = std::numeric_limits<double>::max();
    });
    edit([](Value& v) { v.getIf<Value::Object>()->at("provider").set("version", 99); });
    auto bad          = base;
    bad.schemaVersion = SchemaVersion(2);
    refused(bad);
    bad      = base;
    bad.type = "other";
    refused(bad);
    bad            = base;
    bad.instanceId = epoch.child("other");
    refused(bad);
    bad = base;
    bad.payload.set("tracker", Value{});
    REQUIRE(!rts::RTSNavigationCapture::parse(bad, epoch, restoreHash).ok());
    SnapshotHashProvider unavailable = [](std::string_view) {
        return Result<ContentId>::failure(Diagnostic::error(DiagnosticCode::Unsupported, "digest unavailable"));
    };
    REQUIRE(!capture.seal(Revision(9), SimulationTick(2), unavailable).ok());
    REQUIRE(!capture.seal({}, SimulationTick(2), restoreHash).ok());
    REQUIRE_EQ(serializeSnapshotEnvelope(source.domain->captureNavigation(epoch, navigation)
                                             .expect("same facts")
                                             .seal(Revision(9), SimulationTick(2), restoreHash)
                                             .expect("same seal"))
                   .expect("same bytes"),
               serializeSnapshotEnvelope(base).expect("original bytes"));
    auto parsed   = navigationWireRoundTrip(capture);
    auto provider = map::Pathfinder::prepareRestored(parsed.provider()->get(), std::nullopt).expect("exact provider");
    auto bodies   = source.domain->captureCrowd(epoch).expect("body facts");
    RestoredRTSSpawn target(freezeSpawn(source, epoch));
    target.domain->restoreCrowd(rts::PreparedRTSCrowdRestore::prepare(*target.domain, bodies).expect("body candidate"))
        .expect("body publish");
    {
        ecs::ScopedTable scope(*target.table);
        auto*            unit = target.domain->findUnit(source.request.units[0].subject);
        REQUIRE(unit != nullptr);
        unit->navigation()->trafficRecoveryTarget = rts::WorldPosition{3, 4};
        unit->navigation()->trafficWaiting        = true;
        unit->navigation()->plannedOrderId        = "saved-traffic";
        target.domain->restoreNavigation(parsed, *provider).expect("publish tracker without setter side effects");
        REQUIRE(unit->navigation()->trafficRecoveryTarget.has_value());
        REQUIRE_EQ(unit->navigation()->trafficRecoveryTarget->x, 3.f);
        REQUIRE(unit->navigation()->trafficWaiting);
        REQUIRE_EQ(unit->navigation()->plannedOrderId, "saved-traffic");
    }
    const auto before = target.crowd.getFieldStatus();
    REQUIRE_EQ(before.surface.surfaceRevision, source.crowd.getFieldStatus().surface.surfaceRevision);
    auto moved = std::move(parsed);
    REQUIRE(!parsed.seal(Revision(9), SimulationTick(2), restoreHash).ok());
}
TEST_CASE("RTS navigation archive retains previous publication through reconfiguration and source retirement") {
    std::optional<rts::RTSNavigationCapture> capture;
    std::optional<rts::RTSCrowdCapture>      bodies;
    SavedRTSSpawn                            saved;
    std::weak_ptr<ecs::Table>                retired;
    std::uint64_t                            previous = 0;
    {
        map::Pathfinder   navigation(32, 32);
        SharedFlowFixture source;
        retired = source.table;
        source.domain->setNavigationProvider(&navigation);
        source.domain
            ->configureCrowdSurfaceTracking(
                {crowd::NavigationSurfaceId(1), crowd::MovementClassId(1), 1, 1, .35f, 1000000})
            .expect("old tracking");
        source.step(2);
        previous = source.crowd.getFieldStatus().surface.surfaceRevision;
        source.domain
            ->configureCrowdSurfaceTracking(
                {crowd::NavigationSurfaceId(1), crowd::MovementClassId(1), 1, 2, .35f, 1000000})
            .expect("new cost model");
        const auto epoch = source.request.source.owner.child("reconfigured-navigation");
        capture.emplace(navigationWireRoundTrip(
            source.domain->captureNavigation(epoch, navigation).expect("unpublished reconfiguration")));
        bodies.emplace(source.domain->captureCrowd(epoch).expect("old publication backend"));
        saved = freezeSpawn(source, epoch);
    }
    REQUIRE(retired.expired());
    auto provider =
        map::Pathfinder::prepareRestored(capture->provider()->get(), std::nullopt).expect("source-retired provider");
    RestoredRTSSpawn target(saved);
    target.domain->restoreCrowd(rts::PreparedRTSCrowdRestore::prepare(*target.domain, *bodies).expect("bodies"))
        .expect("publish bodies");
    target.domain->restoreNavigation(*capture, *provider).expect("publish reconfigured tracker");
    REQUIRE_EQ(target.crowd.getFieldStatus().surface.surfaceRevision, previous);
    action::ActionRuntime actions;
    rts::ActionAdapter    executor(actions);
    {
        ecs::ScopedTable scope(*target.table);
        target.domain->step({SimulationTick(3), Duration(100000000)}, executor).expect("first new cost publication");
    }
    REQUIRE_EQ(target.crowd.getFieldStatus().surface.surfaceRevision, previous + 1);
    REQUIRE_EQ(target.crowd.getFieldStatus().surface.costModelRevision, 2u);
}
TEST_CASE("RTS tracked navigation restore preserves publication and pending shared-flow continuation") {
    map::Pathfinder   navigation(32, 32);
    SharedFlowFixture source;
    source.domain->setNavigationProvider(&navigation);
    source.domain
        ->configureCrowdSurfaceTracking({crowd::NavigationSurfaceId(1), crowd::MovementClassId(1), 1, 1, .35f, 1000000})
        .expect("tracker");
    source.step(2);
    REQUIRE(source.domain->getCrowdFlowNavigationStatus().pendingGoals > 0);
    navigation.setBlocked(10, 10, false);  // Changed hint with identical content must not republish the surface.
    const auto epoch    = source.request.source.owner.child("surface-tracker");
    auto       captured = navigationWireRoundTrip(
        source.domain->captureNavigation(epoch, navigation).expect("tracked native navigation"));
    auto bodies   = source.domain->captureCrowd(epoch).expect("matching native Crowd");
    auto saved    = freezeSpawn(source, epoch);
    auto provider = map::Pathfinder::prepareRestored(captured.provider()->get(), std::nullopt).expect("provider");
    RestoredRTSSpawn target(saved);
    target.domain->restoreCrowd(rts::PreparedRTSCrowdRestore::prepare(*target.domain, bodies).takeValue())
        .expect("body and job restore");
    const auto before = target.crowd.getFieldStatus();
    const auto jobs   = target.domain->getCrowdFlowNavigationStatus();
    target.domain->restoreNavigation(captured, *provider).expect("tracker restore");
    REQUIRE_EQ(target.crowd.getFieldStatus().inputRevision, before.inputRevision);
    action::ActionRuntime actions;
    rts::ActionAdapter    executor(actions);
    {
        ecs::ScopedTable scope(*target.table);
        target.domain->step({SimulationTick(2), Duration(0)}, executor).expect("same-boundary reconciliation");
    }
    REQUIRE(target.crowd.getFieldStatus().surface == before.surface);
    REQUIRE_EQ(target.crowd.getFieldStatus().inputRevision, before.inputRevision);
    REQUIRE_EQ(target.domain->getCrowdFlowNavigationStatus().prepared, jobs.prepared);
    REQUIRE_EQ(target.domain->getCrowdFlowNavigationStatus().lastWork, jobs.lastWork);
    for (std::uint64_t tick = 3; tick <= 7; ++tick) {
        source.step(tick);
        ecs::ScopedTable scope(*target.table);
        target.domain->step({SimulationTick(tick), Duration(100000000)}, executor).expect("continued tracked RTS");
        REQUIRE(target.crowd.getFieldStatus().surface == source.crowd.getFieldStatus().surface);
        REQUIRE_EQ(target.crowd.getFieldStatus().inputRevision, source.crowd.getFieldStatus().inputRevision);
        REQUIRE_EQ(target.domain->getCrowdFlowNavigationStatus().prepared,
                   source.domain->getCrowdFlowNavigationStatus().prepared);
        REQUIRE_EQ(target.domain->getCrowdFlowNavigationStatus().lastWork,
                   source.domain->getCrowdFlowNavigationStatus().lastWork);
        for (const auto& member : bodies.members()) {
            auto a = source.crowd.readAgent(source.crowd.resolveNamedAgent(member.key).takeValue()).takeValue();
            auto b = target.crowd.readAgent(target.crowd.resolveNamedAgent(member.key).takeValue()).takeValue();
            REQUIRE_EQ(a.x, b.x);
            REQUIRE_EQ(a.y, b.y);
        }
    }
}
TEST_CASE("RTS tracked navigation retains pending rebind and rejects claimed or unrelated providers") {
    map::Pathfinder   navigation(32, 32);
    SharedFlowFixture source;
    const auto        epoch = source.request.source.owner.child("pending-surface");
    source.domain->setNavigationProvider(&navigation);
    source.domain
        ->configureCrowdSurfaceTracking({crowd::NavigationSurfaceId(1), crowd::MovementClassId(1), 1, 1, .35f, 1000000})
        .expect("tracker");
    source.step(2);
    source.domain->setNavigationProvider(&navigation, {1.25f, 0.f, 0.f});
    auto captured = navigationWireRoundTrip(source.domain->captureNavigation(epoch, navigation).takeValue());
    auto bodies   = source.domain->captureCrowd(epoch).takeValue();
    auto provider = map::Pathfinder::prepareRestored(captured.provider()->get(), std::nullopt).takeValue();
    RestoredRTSSpawn target(freezeSpawn(source, epoch));
    target.domain->restoreCrowd(rts::PreparedRTSCrowdRestore::prepare(*target.domain, bodies).takeValue())
        .expect("Crowd");
    map::Pathfinder unrelated(32, 32);
    REQUIRE(!target.domain->restoreNavigation(captured, unrelated).ok());
    target.domain->restoreNavigation(captured, *provider).expect("retry exact navigation");
    REQUIRE(!target.domain->restoreNavigation(captured, *provider).ok());
    const auto before = target.crowd.getFieldStatus().surface.surfaceRevision;
    source.step(3);
    action::ActionRuntime actions;
    rts::ActionAdapter    executor(actions);
    {
        ecs::ScopedTable scope(*target.table);
        target.domain->step({SimulationTick(3), Duration(100000000)}, executor).expect("pending rebind");
    }
    REQUIRE_EQ(target.crowd.getFieldStatus().surface.surfaceRevision, before + 1);
    REQUIRE(target.crowd.getFieldStatus().surface == source.crowd.getFieldStatus().surface);
    REQUIRE_EQ(target.crowd.getFieldStatus().inputRevision, source.crowd.getFieldStatus().inputRevision);
    REQUIRE(!source.domain->captureNavigation({}, navigation).ok());
    REQUIRE(!source.domain->captureNavigation(epoch, unrelated).ok());
    source.domain->setNavigationProvider(&navigation, {}, [](const auto&, const auto&) {});
    REQUIRE(!source.domain->captureNavigation(epoch, navigation).ok());
}

TEST_CASE("RTS navigation restore distinguishes untracked and not-yet-published surfaces") {
    for (bool tracked : {false, true}) {
        map::Pathfinder   navigation(32, 32);
        SharedFlowFixture source(3, -.5f);  // Match RTS center-based grid conversion before first publication.
        source.domain->setNavigationProvider(&navigation);
        if (tracked)
            source.domain
                ->configureCrowdSurfaceTracking(
                    {crowd::NavigationSurfaceId(1), crowd::MovementClassId(1), 1, 1, .35f, 1000000})
                .expect("unpublished tracker");
        const auto epoch    = source.request.source.owner.child("initial-tracker");
        auto       captured = navigationWireRoundTrip(source.domain->captureNavigation(epoch, navigation).takeValue());
        auto       bodies   = source.domain->captureCrowd(epoch).takeValue();
        auto       provider = map::Pathfinder::prepareRestored(captured.provider()->get(), std::nullopt).takeValue();
        RestoredRTSSpawn target(freezeSpawn(source, epoch));
        target.domain->restoreCrowd(rts::PreparedRTSCrowdRestore::prepare(*target.domain, bodies).takeValue())
            .expect("Crowd");
        target.domain->restoreNavigation(captured, *provider).expect("unpublished or absent tracker");
        const auto before = target.crowd.getFieldStatus().surface.surfaceRevision;
        source.step(2);
        action::ActionRuntime actions;
        rts::ActionAdapter    executor(actions);
        {
            ecs::ScopedTable scope(*target.table);
            target.domain->step({SimulationTick(2), Duration(100000000)}, executor).expect("first tracker step");
        }
        REQUIRE(target.crowd.getFieldStatus().surface == source.crowd.getFieldStatus().surface);
        REQUIRE_EQ(target.crowd.getFieldStatus().surface.surfaceRevision, before + (tracked ? 1u : 0u));
        REQUIRE_EQ(target.domain->getCrowdFlowNavigationStatus().prepared,
                   source.domain->getCrowdFlowNavigationStatus().prepared);
    }
}
}  // namespace test_rts_spawn
