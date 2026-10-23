#include "zeroerr/assert.h"
#include "crowd/Crowd.h"
#include "zeroerr/unittest.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <limits>

using namespace eve::crowd;

TEST_CASE("crowd.batch.relocationChecksWholeDestinationBatchAndPreservesIdentity") {
    Crowd crowd;
    crowd.resizeField(16, 16, 1.f, 0.f, 0.f);
    crowd.setBlocked(10, 10, true);
    const auto a = crowd.agentRef(crowd.addAgent(2.f, 2.f, 0.f, 0.5f)).takeValue();
    const auto b = crowd.agentRef(crowd.addAgent(4.f, 2.f, 0.f, 0.5f)).takeValue();
    REQUIRE(!crowd.relocateAgents({{{a, 6.f, 2.f}, {b, 6.f, 2.f}}, 100}).ok());
    CHECK_EQ(crowd.readAgent(a).value().x, 2.f);
    REQUIRE(!crowd.relocateAgents({{{a, 10.5f, 10.5f}}, 100}).ok());
    REQUIRE(!crowd.relocateAgents({{{a, 8.f, 2.f}}, 1}).ok());
    CHECK_EQ(crowd.readAgent(a).value().x, 2.f);
    REQUIRE(crowd.relocateAgents({{{a, 4.f, 2.f}, {b, 2.f, 2.f}}, 100}).ok());
    CHECK(crowd.isAgentAlive(a));
    CHECK(crowd.isAgentAlive(b));
    CHECK_EQ(crowd.readAgent(a).value().x, 4.f);
    CHECK_EQ(crowd.readAgent(b).value().x, 2.f);
}

TEST_CASE("crowd.batch.relocationResetsVelocityButKeepsSteering") {
    Crowd crowd;
    crowd.setClampToField(false);
    crowd.setDefaultSpeed(4.f);
    crowd.setSeparationWeight(0.f);
    crowd.setArriveRadius(0.1f);
    const auto  reference = crowd.agentRef(crowd.addAgent(0.f, 0.f, 0.f, 0.5f)).takeValue();
    AgentUpdate update;
    update.reference = reference;
    update.intent    = {AgentAction::Seek, true, 20.f, 0.f};
    REQUIRE(crowd.updateAgents({{update}}).ok());
    REQUIRE(crowd.advance(0.1f).ok());
    CHECK(crowd.readAgent(reference).value().vx > 0.f);
    REQUIRE(crowd.relocateAgents({{{reference, 10.f, 0.f}}, 100}).ok());
    auto state = crowd.readAgent(reference).takeValue();
    CHECK_EQ(state.speed, 0.f);
    CHECK_EQ(state.vx, 0.f);
    CHECK_EQ(state.action, static_cast<int>(AgentAction::Seek));
    REQUIRE(crowd.advance(0.1f).ok());
    CHECK(crowd.readAgent(reference).value().x > 10.f);
}

TEST_CASE("crowd.registry.swapPopKeepsIdentityAndReuseRejectsOldGeneration") {
    Crowd      crowd;
    const int  a     = crowd.addNamedAgent("a", 1.f, 0.f, 0.f, 1.f);
    const int  b     = crowd.addNamedAgent("b", 2.f, 0.f, 0.f, 1.f);
    const auto first = crowd.agentRef(a).takeValue(), second = crowd.agentRef(b).takeValue();
    REQUIRE(crowd.removeAgent(a));
    CHECK(!crowd.isAgentAlive(first));
    CHECK(crowd.isAgentAlive(second));
    CHECK_EQ(crowd.readAgent(second).value().x, 2.f);
    CHECK_EQ(crowd.resolveNamedAgent("b").value(), second);
    const auto replacement = crowd.agentRef(crowd.addNamedAgent("replacement", 3.f, 0.f, 0.f, 1.f)).takeValue();
    CHECK_EQ(replacement.handle.index(), first.handle.index());
    CHECK(replacement.handle.generation() != first.handle.generation());
    CHECK(!crowd.readAgent(first).ok());
}

TEST_CASE("crowd.registry.clearAndWrongOwnerCannotResolve") {
    Crowd      first, second;
    const auto old   = first.agentRef(first.addAgent(1.f, 0.f, 0.f, 1.f)).takeValue();
    const auto other = second.agentRef(second.addAgent(2.f, 0.f, 0.f, 1.f)).takeValue();
    REQUIRE_EQ(old.handle, other.handle);
    REQUIRE(old.owner != other.owner);
    REQUIRE(!second.readAgent(old).ok());
    first.clearAgents();
    const auto current = first.agentRef(first.addAgent(3.f, 0.f, 0.f, 1.f)).takeValue();
    REQUIRE(!first.readAgent(old).ok());
    REQUIRE(first.isAgentAlive(current));
}

TEST_CASE("crowd.registry.spawnTransactionPreservesOwnerAndLiveReferences") {
    Crowd            crowd;
    const auto       old = crowd.agentRef(crowd.addNamedAgent("old", 1.5f, 0.f, 0.f, 1.f)).takeValue();
    AgentCreateBatch batch;
    batch.agents = {{"new", 0.f, 0.f, 0.f, 1.f, {}}};
    auto created = crowd.createAgents(batch);
    REQUIRE(created.ok());
    const auto reference = created.value().created.front().reference;
    CHECK_EQ(reference.owner, old.owner);
    CHECK(crowd.isAgentAlive(old));
    CHECK(crowd.isAgentAlive(reference));
    CHECK(crowd.readAgent(old).value().x > 1.5f);
    REQUIRE(!crowd.createAgents(batch).ok());
    CHECK(crowd.isAgentAlive(reference));
    CHECK_EQ(crowd.resolveNamedAgent("new").value(), reference);
}

TEST_CASE("crowd.batch.updateAdmissionIsAtomicAndDuplicateReferencesReject") {
    Crowd       crowd;
    const auto  reference = crowd.agentRef(crowd.addAgent(1.f, 2.f, 0.f, 1.f)).takeValue();
    AgentUpdate update;
    update.reference        = reference;
    update.data             = 17;
    update.intent           = {AgentAction::Seek, true, 100.f, 0.f};
    AgentUpdate invalid     = update;
    invalid.reference.owner = 0;
    REQUIRE(!crowd.updateAgents({{update, invalid}}).ok());
    CHECK_EQ(crowd.readAgent(reference).value().data, 0);
    REQUIRE(!crowd.updateAgents({{update, update}}).ok());
    CHECK_EQ(crowd.readAgent(reference).value().data, 0);
    invalid          = update;
    invalid.maxSpeed = std::numeric_limits<float>::infinity();
    REQUIRE(!crowd.updateAgents({{invalid}}).ok());
    auto applied = crowd.updateAgents({{update}});
    REQUIRE(applied.ok());
    CHECK_EQ(applied.value().applied, 1u);
    CHECK_EQ(crowd.readAgent(reference).value().data, 17);
    CHECK_EQ(crowd.readAgent(reference).value().action, static_cast<int>(AgentAction::Seek));
}

TEST_CASE("crowd.batch.removalReResolvesAfterSwapAndInvalidBatchDoesNothing") {
    Crowd      crowd;
    const auto a = crowd.agentRef(crowd.addAgent(1.f, 0.f, 0.f, 1.f)).takeValue();
    const auto b = crowd.agentRef(crowd.addAgent(2.f, 0.f, 0.f, 1.f)).takeValue();
    const auto c = crowd.agentRef(crowd.addAgent(3.f, 0.f, 0.f, 1.f)).takeValue();
    REQUIRE(!crowd.removeAgents({{a, {}}}).ok());
    REQUIRE(!crowd.removeAgents({{a, a}}).ok());
    CHECK_EQ(crowd.getAgentCount(), 3);
    REQUIRE(crowd.removeAgents({{a, c}}).ok());
    CHECK_EQ(crowd.getAgentCount(), 1);
    CHECK(!crowd.isAgentAlive(a));
    CHECK(!crowd.isAgentAlive(c));
    CHECK_EQ(crowd.readAgent(b).value().x, 2.f);
}

TEST_CASE("crowd.batch.selectedFrameOwnsValuesAndRejectPreservesOutput") {
    Crowd                       crowd;
    const auto                  a = crowd.agentRef(crowd.addAgent(1.f, 0.f, 0.f, 1.f)).takeValue();
    const auto                  b = crowd.agentRef(crowd.addAgent(2.f, 0.f, 0.f, 1.f)).takeValue();
    AgentFrame                  frame;
    const std::vector<AgentRef> selected{b, a};
    REQUIRE(crowd.copyAgentStates(selected, frame).ok());
    CHECK_EQ(frame.agents[0].reference, b);
    CHECK_EQ(frame.agents[1].state.x, 1.f);
    REQUIRE(crowd.removeAgents({{a}}).ok());
    CHECK_EQ(frame.agents[1].state.x, 1.f);
    REQUIRE(!crowd.copyAgentStates(selected, frame).ok());
    CHECK_EQ(frame.agents.size(), 2u);
    CHECK_EQ(frame.agents[0].state.x, 2.f);
    REQUIRE(crowd.copyAgentFrame(frame).ok());
    CHECK_EQ(frame.agents.size(), 1u);
    CHECK_EQ(frame.agents[0].reference, b);
}

TEST_CASE("crowd.registry.scriptReferencesAndBatchesHaveCheckedBoundary") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    auto    table = vm.addTable("eve");
    auto    cls   = table.addClass<Crowd>("Crowd");
    Crowd::expose(cls);
    vm.run(vm.compileSource(R"(
        local crowd = eve.Crowd();
        local slot = crowd.addNamedAgent("a", 1.0, 2.0, 0.0, 1.0);
        local ref = crowd.agentRef(slot).value;
        assert(crowd.resolveNamedAgent("a").value.generation == ref.generation);
        assert(crowd.isAgentAlive(ref));
        assert(crowd.readAgent(ref).value.x == 1.0);
        assert(!crowd.readAgent({ owner = ref.owner.tofloat(), index = ref.index, generation = ref.generation }).ok);
        local changed = crowd.updateAgents([{ reference = ref, data = 19, holdPosition = true }]);
        assert(changed.ok && changed.value.applied == 1);
        assert(crowd.readAgent(ref).value.data == 19);
        assert(crowd.relocateAgents([{ reference = ref, x = 4.0, y = 5.0 }], 100).ok);
        assert(crowd.readAgent(ref).value.x == 4.0);
        assert(!crowd.relocateAgents([{ reference = ref, x = 6.0, y = 5.0 }], 1).ok);
        assert(!crowd.updateAgents([{ reference = ref, data = 4 }, { reference = ref, data = 6 }]).ok);
        assert(crowd.readAgent(ref).value.data == 19);
        local frame = crowd.copyAgentStates([ref]);
        assert(frame.ok && frame.value.agents[0].state.data == 19);
        assert(crowd.copyAgentFrame().value.agents.len() == 1);
        assert(crowd.removeAgents([ref]).ok);
        assert(!crowd.readAgent(ref).ok && !crowd.isAgentAlive(ref));
        assert(frame.value.agents[0].state.x == 4.0);
    )"));
}

TEST_CASE("crowd.registry.scriptMovementBindingRejectsCoercionAndPartialUpdates") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    auto    table = vm.addTable("eve");
    auto    cls   = table.addClass<Crowd>("Crowd");
    Crowd::expose(cls);
    vm.run(vm.compileSource(R"(
        local c=eve.Crowd();
        local ref=c.agentRef(c.addNamedAgent("class",1.0,1.0,0.0,0.1)).value;
        assert(c.updateAgents([{reference=ref,movement={classId=7,revision=2}}]).ok);
        assert(c.readAgent(ref).value.movement.classId=="7");
        assert(c.copyAgentFrame().value.agents[0].state.movement.revision=="2");
        assert(!c.updateAgents([{reference=ref,data=10,movement={classId=7.0,revision=2}}]).ok);
        assert(!c.updateAgents([{reference=ref,movement={classId=7}}]).ok);
        assert(!c.updateAgents([{reference=ref,movement={classId=0,revision=2}}]).ok);
        assert(c.readAgent(ref).value.data==0);
        assert(c.updateAgents([{reference=ref,movement={classId=0,revision=0}}]).ok);
        assert(c.readAgent(ref).value.movement.classId=="0");
    )"));
}

TEST_CASE("crowd.registry.updatePlanOwnsValuesAndRejectsMaskBeforeMutation") {
    Crowd            crowd;
    const auto       a = crowd.agentRef(crowd.addAgent(0.f, 0.f, 0.f, 0.2f)).takeValue();
    const auto       b = crowd.agentRef(crowd.addAgent(2.f, 0.f, 0.f, 0.2f)).takeValue();
    AgentUpdateBatch batch;
    AgentUpdate      first;
    first.reference = a;
    first.data      = 11;
    AgentUpdate second;
    second.reference = b;
    second.data      = 22;
    batch.agents     = {first, second};
    auto prepared    = crowd.prepareAgentUpdates(batch);
    REQUIRE(prepared.ok());
    batch.agents[0].data         = 99;
    auto               plan      = std::move(prepared).takeValue();
    const std::uint8_t invalid[] = {1, 2};
    REQUIRE(!crowd.commitAgentUpdates(std::move(plan), invalid).ok());
    CHECK_EQ(crowd.readAgent(a).takeValue().data, 0);
    const std::uint8_t selected[] = {1, 0};
    auto               committed  = crowd.commitAgentUpdates(std::move(plan), selected);
    REQUIRE(committed.ok());
    CHECK_EQ(committed.value().applied, 1u);
    CHECK_EQ(crowd.readAgent(a).takeValue().data, 11);
    CHECK_EQ(crowd.readAgent(b).takeValue().data, 0);
    REQUIRE(!crowd.commitAgentUpdates(std::move(plan), {}).ok());
}
TEST_CASE("crowd.registry.updatePlanResolvesCompactionAndRejectsStaleUnselectedIdentity") {
    Crowd            crowd;
    const int        removed = crowd.addAgent(-2.f, 0.f, 0.f, 0.2f);
    const auto       a       = crowd.agentRef(crowd.addAgent(0.f, 0.f, 0.f, 0.2f)).takeValue();
    const auto       b       = crowd.agentRef(crowd.addAgent(2.f, 0.f, 0.f, 0.2f)).takeValue();
    AgentUpdateBatch batch;
    AgentUpdate      first;
    first.reference = a;
    first.data      = 11;
    AgentUpdate second;
    second.reference = b;
    second.data      = 22;
    batch.agents     = {first, second};
    auto prepared    = crowd.prepareAgentUpdates(batch);
    REQUIRE(prepared.ok());
    REQUIRE(crowd.removeAgent(removed));
    REQUIRE(crowd.commitAgentUpdates(std::move(prepared).takeValue(), {}).ok());
    CHECK_EQ(crowd.readAgent(a).takeValue().data, 11);
    CHECK_EQ(crowd.readAgent(b).takeValue().data, 22);
    first.data      = 33;
    batch.agents[0] = first;
    auto stale      = crowd.prepareAgentUpdates(batch);
    REQUIRE(stale.ok());
    REQUIRE(crowd.removeAgent(crowd.getAgentCount() - 1));
    const std::uint8_t selection[] = {0, 1};
    REQUIRE(!crowd.commitAgentUpdates(std::move(stale).takeValue(), selection).ok());
    CHECK_EQ(crowd.readAgent(b).takeValue().data, 22);
}
TEST_CASE("crowd.registry.updatePlanRestoreAndProviderDestructionAreSafe") {
    Crowd::AgentUpdatePlan orphan;
    {
        Crowd       crowd;
        const auto  ref = crowd.agentRef(crowd.addAgent(0.f, 0.f, 0.f, 0.2f)).takeValue();
        AgentUpdate update;
        update.reference = ref;
        update.data      = 4;
        AgentUpdateBatch batch;
        batch.agents.push_back(update);
        auto prepared = crowd.prepareAgentUpdates(batch);
        REQUIRE(prepared.ok());
        orphan = std::move(prepared).takeValue();
    }
    Crowd      crowd;
    const auto ref = crowd.agentRef(crowd.addAgent(0.f, 0.f, 0.f, 0.2f)).takeValue();
    REQUIRE(!crowd.commitAgentUpdates(std::move(orphan), {}).ok());
    auto saved = crowd.captureSnapshot();
    REQUIRE(saved.ok());
    AgentUpdate update;
    update.reference = ref;
    update.data      = 4;
    AgentUpdateBatch batch;
    batch.agents.push_back(update);
    auto prepared = crowd.prepareAgentUpdates(batch);
    REQUIRE(prepared.ok());
    auto restored = crowd.prepareRestore(saved.value());
    REQUIRE(restored.ok());
    REQUIRE(crowd.commitRestore(std::move(restored).takeValue()).ok());
    REQUIRE(!crowd.commitAgentUpdates(std::move(prepared).takeValue(), {}).ok());
    CHECK_EQ(crowd.getAgentState(0).data, 0);
}
