#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "physics/Body.h"
#include "physics/Physics.h"
#include "physics/PhysicsLink.h"
#include "physics/SimulationBackend.h"
#include "physics/World.h"

#include "common/Capability.h"

#include <Box2D/Box2D.h>

#include <cstdint>
#include <cmath>
#include <memory>

using namespace eve::physics;

namespace {

class CapabilityReset {
public:
    CapabilityReset() { eve::cap::detail::clearAllRaw(); }
    ~CapabilityReset() { eve::cap::detail::clearAllRaw(); }
};

class MockAcceleratorProvider final : public IAcceleratorBackendProvider {
public:
    [[nodiscard]] bool supports(SimulationBackendDomain domain) const noexcept override {
        return domain == SimulationBackendDomain::World2D;
    }

    [[nodiscard]] eve::Result<std::unique_ptr<ISimulationBackend>> create(
        SimulationBackendDomain domain, void* /*state*/) override {
        if (!supports(domain)) {
            return eve::Result<std::unique_ptr<ISimulationBackend>>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Unsupported,
                                       "Mock accelerator does not implement this domain"));
        }
        return eve::Result<std::unique_ptr<ISimulationBackend>>::success(
            detail::makeMockAcceleratorBackend());
    }
};

void checkObservableContract(ISimulationBackend& backend) {
    const eve::SimulationStep step{eve::SimulationTick{1},
                                   eve::Duration::fromNanoseconds(16666667)};
    auto applied = backend.step(step, SimulationSettings{});
    const bool appliedOk = applied.ok();
    CHECK(appliedOk);
    const SimulationObservation observation = backend.observation();
    CHECK_EQ(observation.stepCount, std::uint64_t(1));
    CHECK(observation.lastTick == step.tick);
    CHECK_EQ(observation.simulatedDuration, step.delta);

    auto duplicate = backend.step(step, SimulationSettings{});
    const bool duplicateOk = duplicate.ok();
    CHECK(!duplicateOk);
    CHECK_EQ(duplicate.code(), eve::StatusCode::Rejected);
}

}  // namespace

TEST_CASE("physics.core.backendContract.isHeadlessAndObservable") {
    auto *physics = Physics::create();
    std::unique_ptr<World> world(physics->newWorld(0.f, 900.f));
    REQUIRE(world.get() != nullptr);

    Body *body = world->newBody("dynamic", 0.f, 0.f);
    body->newCircleFixture(10.f);
    const float y0 = body->getY();

    auto backend = detail::makeBox2DSimulationBackend(world->raw());
    REQUIRE(backend.get() != nullptr);
    CHECK_EQ(backend->kind(), SimulationBackendKind::Cpu);
    CHECK_EQ(backend->determinism(), SimulationDeterminism::ToleranceBounded);
    CHECK_EQ(backend->observation().stepCount, std::uint64_t(0));

    auto stepped = backend->step(
        SimulationStep{eve::SimulationTick{1}, eve::Duration::fromNanoseconds(16666667)},
        SimulationSettings{});
    const bool steppedOk = stepped.ok();
    REQUIRE(steppedOk);
    const SimulationObservation observation = backend->observation();
    CHECK_EQ(observation.stepCount, std::uint64_t(1));
    CHECK(observation.lastTick == eve::SimulationTick{1});
    CHECK(std::fabs(observation.simulatedSeconds - 0.016666667) < 1e-9);
    CHECK(std::fabs(observation.lastDeltaSeconds - 0.016666667f) < 1e-6f);
    CHECK_GT(body->getY(), y0);
}

TEST_CASE("physics.core.backendContract.cpuAndMockShareObservableRules") {
    CapabilityReset reset;
    b2World rawWorld(b2Vec2_zero);

    auto cpu = detail::makeBox2DSimulationBackend(&rawWorld);
    REQUIRE(cpu.get() != nullptr);
    checkObservableContract(*cpu);

    auto mock = detail::makeMockAcceleratorBackend();
    REQUIRE(mock.get() != nullptr);
    CHECK_EQ(mock->kind(), SimulationBackendKind::MockAccelerator);
    checkObservableContract(*mock);
}

TEST_CASE("physics.core.backendFallbackIsStructuredAndObservable") {
    CapabilityReset reset;
    b2World rawWorld(b2Vec2_zero);

    auto absent = detail::selectSimulationBackend(
        SimulationBackendDomain::World2D,
        detail::makeBox2DSimulationBackend(&rawWorld), &rawWorld, true);
    const bool absentOk = absent.ok();
    REQUIRE(absentOk);
    CHECK_EQ(absent.status().code(), eve::StatusCode::Applied);
    REQUIRE_EQ(absent.diagnostics().size(), std::size_t(1));
    CHECK_EQ(absent.diagnostics().front().code(), eve::DiagnosticCode::Unsupported);
    auto absentSelection = std::move(absent).takeValue();
    CHECK(absentSelection.usedFallback);
    CHECK_EQ(absentSelection.actualKind, SimulationBackendKind::Cpu);

    MockAcceleratorProvider provider;
    eve::cap::provide<IAcceleratorBackendProvider>(&provider);
    auto present = detail::selectSimulationBackend(
        SimulationBackendDomain::World2D,
        detail::makeBox2DSimulationBackend(&rawWorld), &rawWorld, true);
    const bool presentOk = present.ok();
    REQUIRE(presentOk);
    CHECK(present.diagnostics().empty());
    auto presentSelection = std::move(present).takeValue();
    CHECK(!presentSelection.usedFallback);
    CHECK_EQ(presentSelection.actualKind, SimulationBackendKind::MockAccelerator);
}

TEST_CASE("physics.core.worldUpdateNeedsNoGraphics") {
    auto *physics = Physics::create();
    std::unique_ptr<World> world(physics->newWorld(0.f, 0.f));
    REQUIRE(world.get() != nullptr);

    Body *body = world->newBody("dynamic", 20.f, 30.f);
    body->newCircleFixture(5.f);
    body->setLinearVelocity(60.f, 0.f);
    world->update(1.f / 60.f);

    CHECK(std::fabs(body->getX() - 21.f) < 0.05f);
    CHECK(std::fabs(body->getY() - 30.f) < 0.05f);
}

TEST_CASE("physics.core.worldAcceptsInjectedTickAndPhysicsLinkResolvesStale") {
    auto *physics = Physics::create();
    std::unique_ptr<World> world(physics->newWorld(0.f, 0.f));
    REQUIRE(world.get() != nullptr);
    Body* body = world->newBody("dynamic", 0.f, 0.f);
    body->newCircleFixture(5.f);

    const eve::SimulationStep step{eve::SimulationTick{1},
                                   eve::Duration::fromNanoseconds(16666667)};
    auto stepped = world->step(step);
    const bool steppedOk = stepped.ok();
    REQUIRE(steppedOk);
    CHECK(world->simulationTick() == step.tick);
    CHECK_EQ(world->simulationObservation().lastTick, step.tick);
    CHECK(world->backendSelectionStatus().code() == eve::StatusCode::Applied);

    auto linkResult = PhysicsLink::fromBody(*body);
    const bool linkOk = linkResult.ok();
    REQUIRE(linkOk);
    const PhysicsLink link = std::move(linkResult).takeValue();
    auto resolved = link.resolve(*world);
    const bool resolvedOk = resolved.ok();
    REQUIRE(resolvedOk);
    CHECK(std::move(resolved).takeValue() == body);

    body->destroy();
    auto stale = link.resolve(*world);
    const bool staleOk = stale.ok();
    CHECK(!staleOk);
    CHECK_EQ(stale.code(), eve::StatusCode::Rejected);
}

TEST_CASE("physics.core.backendContract.rejectsNullWorld") {
    bool threw = false;
    try {
        [[maybe_unused]] auto backend = detail::makeBox2DSimulationBackend(nullptr);
    } catch (...) {
        threw = true;
    }
    CHECK(threw);
}
