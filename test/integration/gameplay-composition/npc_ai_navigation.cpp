#include "npc_ai/Navigation.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <map>

using namespace eve::npc_ai;

namespace {
class FixedRequestFactory final : public INavigationRequestFactory {
public:
    eve::Result<NavigationRequest> create(const TaskContext& context, const TaskSpec&) const override {
        return eve::Result<NavigationRequest>::success(
            {context.agent, {0.0, 0.0, 0.0}, {10.0, 0.0, 0.0}, 0.5, 0.25, "walk", context.simulationTick});
    }
};

class FakeNavigationProvider final : public INavigationProvider {
public:
    eve::Result<NavigationTicket> begin(const NavigationRequest& request) override {
        ++begins;
        lastRequest = request;
        const NavigationTicket ticket(nextIndex++, 1);
        polls.emplace(ticket, 0);
        return eve::Result<NavigationTicket>::success(ticket);
    }

    eve::Result<NavigationProgress> poll(NavigationTicket ticket, std::uint64_t) override {
        auto found = polls.find(ticket);
        if (found == polls.end()) {
            return eve::Result<NavigationProgress>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::StaleHandle, "injected stale navigation ticket", {}, {}, "npc_ai.test"));
        }
        ++found->second;
        ++pollCount;
        if (arriveOnSecondPoll && found->second >= 2) {
            polls.erase(found);
            return eve::Result<NavigationProgress>::success({NavigationPhase::Arrived, {0.0, 0.0, 0.0}, 0.0});
        }
        return eve::Result<NavigationProgress>::success({NavigationPhase::Moving, {1.0, 0.0, 0.0}, 10.0});
    }

    void abandon(NavigationTicket ticket) noexcept override {
        ++abandons;
        polls.erase(ticket);
    }

    bool              arriveOnSecondPoll = true;
    int               begins             = 0;
    int               pollCount          = 0;
    int               abandons           = 0;
    NavigationRequest lastRequest;

private:
    std::uint32_t                   nextIndex = 0;
    std::map<NavigationTicket, int> polls;
};

BehaviorDefinition movingBehavior() {
    BehaviorDefinition behavior;
    behavior.id           = "moving";
    behavior.initialState = "move";
    StateDefinition move;
    move.id = "move";
    move.tasks.push_back({"go", "move_to"});
    move.transitions.push_back({"done", "task.succeeded.go", {}, 1});
    move.transitions.push_back({"idle", "interrupt", {}, 2});
    StateDefinition done;
    done.id = "done";
    StateDefinition idle;
    idle.id         = "idle";
    behavior.states = {move, done, idle};
    return behavior;
}
}  // namespace

TEST_CASE("npc_ai.navigationTaskCompletesAsynchronously") {
    NpcAiWorld world;
    auto       provider = std::make_unique<FakeNavigationProvider>();
    auto*      observed = provider.get();
    auto       service  = NavigationTaskService::create(std::move(provider), std::make_unique<FixedRequestFactory>());
    REQUIRE(service.ok());
    REQUIRE(world.registerTaskService("move_to", std::move(service.value())).ok());
    REQUIRE(world.registerBehavior(movingBehavior()).ok());
    auto agent = world.createAgent("moving");
    REQUIRE(agent.ok());

    REQUIRE(world.tick({1, 0.016, 1, 4}).ok());
    CHECK_EQ(observed->begins, 1);
    CHECK_EQ(observed->pollCount, 1);
    REQUIRE(world.tick({2, 0.016, 1, 4}).ok());
    CHECK_EQ(observed->pollCount, 2);
    REQUIRE(world.tick({3, 0.016, 1, 4}).ok());
    auto snapshot = world.snapshot(agent.value());
    REQUIRE(snapshot.ok());
    CHECK_EQ(snapshot.value().activeState, std::string("done"));
    CHECK_EQ(observed->lastRequest.requestedTick, 1u);
}

TEST_CASE("npc_ai.navigationTicketIsAbandonedWhenStateExits") {
    NpcAiWorld world;
    auto       provider          = std::make_unique<FakeNavigationProvider>();
    provider->arriveOnSecondPoll = false;
    auto* observed               = provider.get();
    auto  service = NavigationTaskService::create(std::move(provider), std::make_unique<FixedRequestFactory>());
    REQUIRE(service.ok());
    REQUIRE(world.registerTaskService("move_to", std::move(service.value())).ok());
    REQUIRE(world.registerBehavior(movingBehavior()).ok());
    auto agent = world.createAgent("moving");
    REQUIRE(agent.ok());
    REQUIRE(world.tick({1, 0.016, 1, 4}).ok());
    REQUIRE(world.signal(agent.value(), "interrupt").ok());
    REQUIRE(world.tick({2, 0.016, 1, 4}).ok());
    CHECK_EQ(observed->abandons, 1);
    auto snapshot = world.snapshot(agent.value());
    REQUIRE(snapshot.ok());
    CHECK_EQ(snapshot.value().activeState, std::string("idle"));
}
