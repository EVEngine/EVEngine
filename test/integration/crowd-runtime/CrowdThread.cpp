#include "crowd/thread/CrowdThread.h"
#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>
#include "crowd/Crowd.h"
#include "crowd/CrowdWorkScheduler.h"
#include "thread/JobSystem.h"
#include "zeroerr/unittest.h"
using namespace eve;
using namespace eve::crowd;
namespace {
void populate(Crowd& crowd) {
    crowd.setClampToField(false);
    crowd.setDefaultSpeed(0.f);
    crowd.setSeparationWeight(0.f);
    for (int i = 0; i < 300; ++i)
        REQUIRE(crowd.addNamedAgent("scheduler/" + std::to_string(i), float(i % 30) * 0.7f, float(i / 30) * 0.7f, 0.f,
                                    1.f) >= 0);
}
class FailAfterBody final : public CrowdWorkScheduler {
public:
    explicit FailAfterBody(std::shared_ptr<CrowdWorkScheduler> inner) : inner_(std::move(inner)) {}
    Result<void> execute(int partitions, const std::function<void(int)>& body) override {
        ++calls;
        return inner_->execute(partitions, [&](int index) {
            body(index);
            if (calls == failCall && index == 0) throw std::runtime_error("Injected gather failure");
        });
    }
    int calls    = 0;
    int failCall = 4;

private:
    std::shared_ptr<CrowdWorkScheduler> inner_;
};
}  // namespace
TEST_CASE("crowd.thread.serialParallelContactAndReportsAreBitExact") {
    auto jobs    = std::shared_ptr<thread::JobSystem>(thread::createJobSystem(3));
    auto created = crowd_thread::makeJobSystemCrowdScheduler(jobs);
    REQUIRE(created.ok());
    Crowd serial, parallel;
    populate(serial);
    populate(parallel);
    REQUIRE(parallel.setWorkScheduler(CrowdExecution::Parallel, created.value()).ok());
    for (int tick = 1; tick <= 3; ++tick) {
        const CrowdStep step{SimulationTick(tick), Duration::fromNanoseconds(10000000), {}};
        auto            a = serial.advance(step), b = parallel.advance(step);
        REQUIRE(a.ok());
        REQUIRE(b.ok());
        CHECK_EQ(a.value().pairVisits, b.value().pairVisits);
        CHECK_EQ(a.value().contactPairsChecked, b.value().contactPairsChecked);
        for (int i = 0; i < 300; ++i) {
            const auto left = serial.getAgentState(i), right = parallel.getAgentState(i);
            CHECK_EQ(left.x, right.x);
            CHECK_EQ(left.y, right.y);
            CHECK_EQ(left.vx, right.vx);
            CHECK_EQ(left.vy, right.vy);
        }
    }
}
TEST_CASE("crowd.thread.predictiveAvoidanceMatchesSerialWithFourWorkers") {
    auto jobs    = std::shared_ptr<thread::JobSystem>(thread::createJobSystem(4));
    auto created = crowd_thread::makeJobSystemCrowdScheduler(jobs);
    REQUIRE(created.ok());
    Crowd serial, parallel;
    for (Crowd* value : {&serial, &parallel}) {
        value->setClampToField(false);
        value->setSeparationWeight(0.f);
        REQUIRE(value->configureAvoidance({true, 2.f, 0.1f, 16}).ok());
        for (int i = 0; i < 640; ++i) {
            const float x = float(i % 32) * 0.7f, y = float(i / 32) * 0.7f;
            const int slot = value->addAgent(x, y, 0.f, 0.4f);
            REQUIRE(slot >= 0);
            REQUIRE(value->setAgentAction(slot, "seek"));
            REQUIRE(value->setAgentTarget(slot, 24.f - x, y));
        }
    }
    REQUIRE(parallel.setWorkScheduler(CrowdExecution::Parallel, created.value()).ok());
    for (int tick = 1; tick <= 3; ++tick) {
        const CrowdStep step{SimulationTick(tick), Duration::fromNanoseconds(10000000), {}};
        auto a = serial.advance(step), b = parallel.advance(step);
        REQUIRE(a.ok());
        REQUIRE(b.ok());
        CHECK_EQ(a.value().spatialVisits, b.value().spatialVisits);
        CHECK_EQ(a.value().candidatesVisited, b.value().candidatesVisited);
        CHECK_EQ(a.value().avoidanceChecks, b.value().avoidanceChecks);
        CHECK_EQ(a.value().avoidanceTruncations, b.value().avoidanceTruncations);
        for (int i = 0; i < 640; ++i) {
            const auto left = serial.getAgentState(i), right = parallel.getAgentState(i);
            CHECK_EQ(left.x, right.x);
            CHECK_EQ(left.y, right.y);
            CHECK_EQ(left.vx, right.vx);
            CHECK_EQ(left.vy, right.vy);
        }
    }
    const auto before = serial.getAgentState(0);
    CrowdStep limited{SimulationTick(4), Duration::fromNanoseconds(10000000), {}};
    limited.budget.maxPredictionChecks = 1;
    auto failedSerial = serial.advance(limited), failedParallel = parallel.advance(limited);
    REQUIRE(!failedSerial.ok());
    REQUIRE(!failedParallel.ok());
    CHECK_EQ(failedSerial.status().describe(), failedParallel.status().describe());
    CHECK_EQ(serial.getAgentState(0).x, before.x);
    CHECK_EQ(parallel.getAgentState(0).x, before.x);
}
TEST_CASE("crowd.thread.workerFailureJoinsAndInFlightOwnerSurvivesRelease") {
    auto jobs    = std::shared_ptr<thread::JobSystem>(thread::createJobSystem(3));
    auto created = crowd_thread::makeJobSystemCrowdScheduler(jobs);
    REQUIRE(created.ok());
    auto              scheduler = std::move(created).takeValue();
    std::atomic<int>  completed{0};
    std::atomic<bool> backgroundObserved{false};
    const auto        ownerThread = std::this_thread::get_id();
    auto              result      = scheduler->execute(19, [&](int index) {
        if (std::this_thread::get_id() != ownerThread && !backgroundObserved.exchange(true)) {
            jobs.reset();
            throw std::runtime_error("background worker failure");
        }
        if (index == 0) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!backgroundObserved.load()) {
                if (std::chrono::steady_clock::now() >= deadline)
                    throw std::runtime_error("No background worker executed a partition");
                std::this_thread::yield();
            }
        }
        ++completed;
    });
    REQUIRE(!result.ok());
    CHECK(backgroundObserved.load());
    CHECK_EQ(completed.load(), 18);
    auto expired = scheduler->execute(1, [&](int) { ++completed; });
    REQUIRE(!expired.ok());
    CHECK_EQ(completed.load(), 18);
    auto stoppedJobs      = std::shared_ptr<thread::JobSystem>(thread::createJobSystem(2));
    auto stoppedScheduler = crowd_thread::makeJobSystemCrowdScheduler(stoppedJobs);
    REQUIRE(stoppedScheduler.ok());
    stoppedJobs->stop();
    auto stopped = stoppedScheduler.value()->execute(1, [&](int) { ++completed; });
    REQUIRE(!stopped.ok());
    CHECK_EQ(completed.load(), 18);
}
TEST_CASE("crowd.thread.partialGatherFailureRollsBackWithoutSerialRetry") {
    auto jobs    = std::shared_ptr<thread::JobSystem>(thread::createJobSystem(3));
    auto created = crowd_thread::makeJobSystemCrowdScheduler(jobs);
    REQUIRE(created.ok());
    auto  injected = std::make_shared<FailAfterBody>(created.value());
    Crowd reference, failing;
    populate(reference);
    populate(failing);
    REQUIRE(failing.setWorkScheduler(CrowdExecution::Parallel, injected).ok());
    const CrowdStep step{SimulationTick(1), Duration::fromNanoseconds(10000000), {}};
    REQUIRE(!failing.advance(step).ok());
    CHECK_EQ(injected->calls, 4);
    for (int i = 0; i < 300; ++i) {
        CHECK_EQ(failing.getAgentState(i).x, reference.getAgentState(i).x);
        CHECK_EQ(failing.getAgentState(i).y, reference.getAgentState(i).y);
    }
    REQUIRE(failing.setWorkScheduler(CrowdExecution::Parallel, created.value()).ok());
    REQUIRE(failing.advance(step).ok());
    REQUIRE(reference.advance(step).ok());
    for (int i = 0; i < 300; ++i) {
        CHECK_EQ(failing.getAgentState(i).x, reference.getAgentState(i).x);
        CHECK_EQ(failing.getAgentState(i).y, reference.getAgentState(i).y);
    }
}

TEST_CASE("crowd.thread.predictiveVelocityAndBudgetAreBitExact") {
    auto jobs      = std::shared_ptr<thread::JobSystem>(thread::createJobSystem(3));
    auto scheduler = crowd_thread::makeJobSystemCrowdScheduler(jobs);
    REQUIRE(scheduler.ok());
    Crowd serial, parallel;
    for (auto* crowd : {&serial, &parallel}) {
        crowd->setClampToField(false);
        crowd->setResolveOverlaps(false);
        crowd->setDefaultSpeed(4.f);
        crowd->setSeparationWeight(0.f);
        REQUIRE(crowd->configureAvoidance({true, 2.f, 0.1f, 16}).ok());
        for (int i = 0; i < 300; ++i) {
            const int agent = crowd->addNamedAgent("prediction/" + std::to_string(i), float(i % 30) * 1.2f,
                                                   float(i / 30) * 1.2f, 0.f, 0.4f);
            REQUIRE(agent >= 0);
            REQUIRE(crowd->setAgentAction(agent, "seek"));
            REQUIRE(crowd->setAgentTarget(agent, i % 2 ? -10.f : 50.f, float(i / 30) * 1.2f));
        }
    }
    REQUIRE(parallel.setWorkScheduler(CrowdExecution::Parallel, scheduler.value()).ok());
    for (int tick = 1; tick <= 3; ++tick) {
        const CrowdStep step{SimulationTick(tick), Duration::fromNanoseconds(10000000), {}};
        auto            a = serial.advance(step), b = parallel.advance(step);
        REQUIRE(a.ok());
        REQUIRE(b.ok());
        CHECK(a.value().avoidanceChecks > 0);
        CHECK_EQ(a.value().avoidanceChecks, b.value().avoidanceChecks);
        CHECK_EQ(a.value().avoidanceTruncations, b.value().avoidanceTruncations);
        CHECK_EQ(a.value().candidatesVisited, b.value().candidatesVisited);
        for (int i = 0; i < 300; ++i) {
            const auto left = serial.getAgentState(i), right = parallel.getAgentState(i);
            CHECK_EQ(left.x, right.x);
            CHECK_EQ(left.y, right.y);
            CHECK_EQ(left.vx, right.vx);
            CHECK_EQ(left.vy, right.vy);
            CHECK_EQ(left.heading, right.heading);
            CHECK_EQ(left.speed, right.speed);
        }
    }
}
TEST_CASE("crowd.thread.partialVelocityFailureRollsBackClockAndMotion") {
    for (int failPhase : {1, 2}) {
        auto jobs      = std::shared_ptr<thread::JobSystem>(thread::createJobSystem(3));
        auto scheduler = crowd_thread::makeJobSystemCrowdScheduler(jobs);
        REQUIRE(scheduler.ok());
        auto failing      = std::make_shared<FailAfterBody>(scheduler.value());
        failing->failCall = failPhase;
        Crowd crowd;
        crowd.setClampToField(false);
        crowd.setResolveOverlaps(false);
        crowd.setDefaultSpeed(4.f);
        crowd.setWanderWeight(0.2f);
        REQUIRE(crowd.configureAvoidance({true, 2.f, 0.1f, 16}).ok());
        for (int i = 0; i < 300; ++i) {
            const int agent = crowd.addAgent(float(i % 30), float(i / 30), 0.f, 0.2f);
            REQUIRE(agent >= 0);
            REQUIRE(crowd.setAgentAction(agent, i % 2 ? "boids" : "seek"));
            REQUIRE(crowd.setAgentTarget(agent, 50.f, 0.f));
        }
        auto snapshot = crowd.captureSnapshot();
        REQUIRE(snapshot.ok());
        Crowd reference;
        auto  restored = reference.prepareRestore(snapshot.value());
        REQUIRE(restored.ok());
        REQUIRE(reference.commitRestore(std::move(restored).takeValue()).ok());
        REQUIRE(crowd.setWorkScheduler(CrowdExecution::Parallel, failing).ok());
        const CrowdStep step{SimulationTick(1), Duration::fromNanoseconds(10000000), {}};
        REQUIRE(!crowd.advance(step).ok());
        CHECK_EQ(failing->calls, failPhase);
        for (int i = 0; i < 300; ++i) {
            CHECK_EQ(crowd.getAgentState(i).x, float(i % 30));
            CHECK_EQ(crowd.getAgentState(i).y, float(i / 30));
            CHECK_EQ(crowd.getAgentState(i).speed, 0.f);
            CHECK_EQ(crowd.getAgentState(i).heading, 0.f);
        }
        REQUIRE(crowd.setWorkScheduler(CrowdExecution::Parallel, scheduler.value()).ok());
        REQUIRE(crowd.advance(step).ok());
        REQUIRE(reference.advance(step).ok());
        for (int i = 0; i < 300; ++i) {
            const auto actual = crowd.getAgentState(i), expected = reference.getAgentState(i);
            CHECK_EQ(actual.x, expected.x);
            CHECK_EQ(actual.y, expected.y);
            CHECK_EQ(actual.vx, expected.vx);
            CHECK_EQ(actual.vy, expected.vy);
            CHECK_EQ(actual.heading, expected.heading);
            CHECK_EQ(actual.speed, expected.speed);
        }
    }
}

TEST_CASE("crowd.thread.preferredBoidsSeekFlowAndHoldAreBitExact") {
    auto jobs      = std::shared_ptr<thread::JobSystem>(thread::createJobSystem(3));
    auto scheduler = crowd_thread::makeJobSystemCrowdScheduler(jobs);
    REQUIRE(scheduler.ok());
    Crowd serial, parallel;
    for (auto* crowd : {&serial, &parallel}) {
        crowd->setClampToField(false);
        crowd->setResolveOverlaps(false);
        crowd->setDefaultSpeed(4.f);
        crowd->setSeparationRadius(2.f);
        crowd->setPerceptionRadius(3.f);
        crowd->setSeparationWeight(0.3f);
        crowd->setAlignmentWeight(0.2f);
        crowd->setCohesionWeight(0.2f);
        crowd->setWanderWeight(0.2f);
        crowd->resizeField(60, 30, 1.f, 0.f, 0.f);
        crowd->buildFlowField(50, 15);
        for (int i = 0; i < 300; ++i) {
            const int agent = crowd->addNamedAgent("preferred/" + std::to_string(i), float(i % 30) + 1.f,
                                                   float(i / 30) + 1.f, 0.f, 0.2f);
            REQUIRE(agent >= 0);
            REQUIRE(crowd->setAgentAction(agent, i % 3 == 0 ? "boids" : i % 3 == 1 ? "seek" : "flow"));
            REQUIRE(crowd->setAgentTarget(agent, 50.f, 15.f));
            if (i % 17 == 0) REQUIRE(crowd->setAgentInteraction(agent, {1.f, true, 1, 1}).ok());
        }
    }
    REQUIRE(parallel.setWorkScheduler(CrowdExecution::Parallel, scheduler.value()).ok());
    for (int tick = 1; tick <= 3; ++tick) {
        const CrowdStep step{SimulationTick(tick), Duration::fromNanoseconds(10000000), {}};
        auto            a = serial.advance(step), b = parallel.advance(step);
        REQUIRE(a.ok());
        REQUIRE(b.ok());
        CHECK(a.value().preferredNeighborsPeak > 0);
        CHECK_EQ(a.value().preferredNeighborsPeak, b.value().preferredNeighborsPeak);
        CHECK_EQ(a.value().preferredVisits, b.value().preferredVisits);
        CHECK_EQ(a.value().candidatesVisited, b.value().candidatesVisited);
        for (int i = 0; i < 300; ++i) {
            const auto left = serial.getAgentState(i), right = parallel.getAgentState(i);
            CHECK_EQ(left.x, right.x);
            CHECK_EQ(left.y, right.y);
            CHECK_EQ(left.vx, right.vx);
            CHECK_EQ(left.vy, right.vy);
            CHECK_EQ(left.heading, right.heading);
            CHECK_EQ(left.speed, right.speed);
        }
    }
}
