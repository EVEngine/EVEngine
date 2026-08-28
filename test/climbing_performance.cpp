#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "climbing/ClimbingECS.h"
#include "physics/Body3D.h"
#include "physics/Physics.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"

#include <cstdio>
#include <memory>

namespace {

class PerformanceClimber : public ecs::Entity {
public:
    ENTITY(PerformanceClimber, ecs::Entity)
    void release() override { ecs::DestroyEntity(this); }

    COMPONENT(eve::climbing::ClimbingBody, climbingBody)
    COMPONENT(eve::climbing::ClimbingIntent, climbingIntent)
    COMPONENT(eve::climbing::ClimbingState, climbingState)
    COMPONENT(eve::climbing::ClimbingLinks, climbingLinks)
};

eve::climbing::ClimbingActionDefinition performanceAction(std::string id) {
    eve::climbing::ClimbingActionDefinition action;
    action.id                        = std::move(id);
    action.kind                      = eve::climbing::ClimbingActionKind::Vault;
    action.minHeight                 = 0.4f;
    action.maxHeight                 = 1.2f;
    action.duration                  = eve::Duration::fromNanoseconds(10000000000ll);
    action.landingForward            = 0.75f;
    action.apexHeight                = 0.55f;
    action.maxTranslationWarpPerTick = 1.f;
    action.horizontalWarpBudget      = 100.f;
    action.verticalWarpBudget        = 100.f;
    action.facingWarpBudgetRadians   = 100.f;
    return action;
}

void printSummary(const char* name, const eve::climbing::ClimbingTelemetrySummary& summary) {
    std::printf(
        "climbing.performance workload=%s samples=%u p50_ns=%llu p95_ns=%llu "
        "p50_queries=%u p95_queries=%u max_queries=%u budget_exceeded=%u\n",
        name, summary.sampleCount, static_cast<unsigned long long>(summary.p50Nanoseconds),
        static_cast<unsigned long long>(summary.p95Nanoseconds), summary.p50QueryCount, summary.p95QueryCount,
        summary.maxQueryCount, summary.budgetExceededCount);
}

}  // namespace

TEST_CASE("climbing.performance.reportsOrdinaryCandidateDenseAndActiveP50P95") {
    constexpr std::uint64_t sampleCount = 120;
    const eve::Duration     fixedDelta  = eve::Duration::fromNanoseconds(16666667);

    eve::climbing::ClimbingTelemetrySummary ordinarySummary;
    {
        ecs::Table                             table;
        ecs::ScopedTable                       tableScope(table);
        std::unique_ptr<eve::physics::World3D> world(eve::physics::Physics::create()->newWorld3D(0.f, 0.f, 0.f, false));
        world->newBody("static", 0.f, -0.1f, 0.f)->newBoxShape(20.f, 0.2f, 20.f);
        auto* character = world->newBody("kinematic", 0.f, 0.9f, 0.f);
        character->newCapsuleShape(1.8f, 0.3f);

        PerformanceClimber* entity = PerformanceClimber::create();
        auto                state  = eve::climbing::ClimbingState::create();
        REQUIRE(state.ok());
        *entity->climbingState() = std::move(state).takeValue();
        auto link                = eve::physics::PhysicsLink::fromBody(*character);
        REQUIRE(link.ok());
        entity->climbingLinks()->physicsBody = std::move(link).takeValue();
        auto runtime                         = eve::climbing::Climbing::resolve(entity->climbingState()->runtime);
        REQUIRE(runtime.isBound());
        entity->climbingIntent()->move = {1.f, 0.f, 0.f};
        runtime->clearTelemetry();
        for (std::uint64_t tick = 1; tick <= sampleCount; ++tick)
            REQUIRE(eve::climbing::ClimbingMotionSystem::step<PerformanceClimber>(
                        *world, {eve::SimulationTick(tick), fixedDelta})
                        .ok());
        ordinarySummary = runtime->telemetry().summarize(eve::climbing::ClimbingWorkload::Ordinary);
        REQUIRE(entity->climbingState()->releaseRuntime().ok());
        entity->release();
    }

    eve::climbing::ClimbingTelemetrySummary candidateSummary;
    {
        std::unique_ptr<eve::physics::World3D> world(eve::physics::Physics::create()->newWorld3D(0.f, 0.f, 0.f, false));
        world->newBody("static", 0.f, 0.3f, 1.f)->newBoxShape(1.35f, 0.6f, 0.65f);
        for (int index = 0; index < 16; ++index) {
            const float x = index % 2 == 0 ? -1.35f : 1.35f;
            const float z = -1.2f + static_cast<float>(index / 2) * 0.3f;
            world->newBody("static", x, 0.1f, z)->newBoxShape(0.1f, 0.2f, 0.1f);
        }
        eve::climbing::ClimbingRuntime           runtime;
        eve::climbing::ClimbingProfileDefinition profile;
        profile.maxProbeDistance   = 1.5f;
        profile.maxCandidates      = 8;
        profile.maxTotalWarpBudget = 100.f;
        for (int index = 0; index < 8; ++index)
            profile.actions.push_back(performanceAction("parkour:dense_vault_" + std::to_string(index)));
        REQUIRE(runtime.setProfile(std::move(profile)).ok());
        runtime.clearTelemetry();
        eve::climbing::ClimbingCandidateSet candidates;
        const eve::climbing::ClimbingPose   pose{{0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 4.f, -1};
        for (std::uint64_t tick = 1; tick <= sampleCount; ++tick)
            REQUIRE(runtime.probeInto(*world, pose, candidates, eve::SimulationTick(tick)).ok());
        candidateSummary = runtime.telemetry().summarize(eve::climbing::ClimbingWorkload::CandidateProbe);
    }

    eve::climbing::ClimbingTelemetrySummary activeSummary;
    {
        std::unique_ptr<eve::physics::World3D> world(eve::physics::Physics::create()->newWorld3D(0.f, 0.f, 0.f, false));
        world->newBody("static", 0.f, 0.3f, 1.f)->newBoxShape(1.35f, 0.6f, 0.65f);
        eve::climbing::ClimbingRuntime           runtime;
        eve::climbing::ClimbingProfileDefinition profile;
        profile.maxWarpResidual    = 100.f;
        profile.maxTotalWarpBudget = 100.f;
        profile.actions.push_back(performanceAction("parkour:active_vault"));
        REQUIRE(runtime.setProfile(std::move(profile)).ok());
        const eve::climbing::ClimbingPose pose{{0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 4.f, -1};
        auto                              begun = runtime.tryBegin(*world, pose, eve::SimulationTick(1));
        if (!begun.ok() && begun.error())
            std::printf("climbing.performance active begin failed: %s\n", begun.error()->message().c_str());
        REQUIRE(begun.ok());
        runtime.clearTelemetry();
        for (std::uint64_t index = 0; index < sampleCount; ++index)
            REQUIRE(runtime.advance(*world, {eve::SimulationTick(index + 2), fixedDelta}).ok());
        activeSummary = runtime.telemetry().summarize(eve::climbing::ClimbingWorkload::Active);
    }

    CHECK_EQ(ordinarySummary.sampleCount, static_cast<std::uint32_t>(sampleCount));
    CHECK_EQ(candidateSummary.sampleCount, static_cast<std::uint32_t>(sampleCount));
    CHECK_EQ(activeSummary.sampleCount, static_cast<std::uint32_t>(sampleCount));
    CHECK(ordinarySummary.p50Nanoseconds > 0);
    CHECK(candidateSummary.p50Nanoseconds > 0);
    CHECK(activeSummary.p50Nanoseconds > 0);
    CHECK_EQ(ordinarySummary.budgetExceededCount, std::uint32_t(0));
    CHECK_EQ(candidateSummary.budgetExceededCount, std::uint32_t(0));
    CHECK_EQ(activeSummary.budgetExceededCount, std::uint32_t(0));
    CHECK(ordinarySummary.maxQueryCount <= eve::climbing::ClimbingQueryBudgets::Ordinary);
    CHECK(candidateSummary.maxQueryCount <= eve::climbing::ClimbingQueryBudgets::CandidateProbe);
    CHECK(activeSummary.maxQueryCount <= eve::climbing::ClimbingQueryBudgets::Active);
    printSummary("ordinary", ordinarySummary);
    printSummary("candidate_dense", candidateSummary);
    printSummary("active", activeSummary);
}
