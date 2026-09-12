#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "physics/Body.h"
#include "physics/Body3D.h"
#include "physics/Physics.h"
#include "physics/PhysicsLink.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"
#include "physics/backend/SimulationBackend.h"
#include "physics/World.h"

#include "common/Capability.h"

#include <Box2D/Box2D.h>
#include <box3d/box3d.h>

#include <cmath>
#include <cstdint>
#include <memory>
#include <stdexcept>

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

    [[nodiscard]] eve::Result<std::unique_ptr<ISimulationBackend>> create(SimulationBackendDomain domain,
                                                                          void * /*state*/) override {
        if (!supports(domain)) {
            return eve::Result<std::unique_ptr<ISimulationBackend>>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Unsupported, "Mock accelerator does not implement this domain"));
        }
        return eve::Result<std::unique_ptr<ISimulationBackend>>::success(detail::makeMockAcceleratorBackend());
    }
};

class FailingProvider final : public IAcceleratorBackendProvider {
public:
    explicit FailingProvider(bool throws) : throws_(throws) {}

    [[nodiscard]] bool supports(SimulationBackendDomain) const noexcept override { return true; }

    [[nodiscard]] eve::Result<std::unique_ptr<ISimulationBackend>> create(SimulationBackendDomain,
                                                                          void*) override {
        if (throws_) throw std::runtime_error("accelerator setup exploded");
        return eve::Result<std::unique_ptr<ISimulationBackend>>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "accelerator rejected shared state", "provider.state"));
    }

private:
    bool throws_ = false;
};

class FailSecondStepBackend final : public ISimulationBackend {
public:
    explicit FailSecondStepBackend(b3WorldId* world)
        : delegate_(detail::makeCallbackSimulationBackend(world, &stepBox3D, SimulationBackendKind::MockAccelerator,
                                                          SimulationDeterminism::ToleranceBounded)) {}

    [[nodiscard]] eve::Result<void> step(const eve::SimulationStep& step,
                                         const SimulationSettings& settings) override {
        if (delegate_->observation().stepCount != 0) {
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Failed, "injected second-step failure", "physics.test.step"));
        }
        return delegate_->step(step, settings);
    }
    [[nodiscard]] SimulationObservation observation() const noexcept override { return delegate_->observation(); }
    [[nodiscard]] SimulationBackendKind kind() const noexcept override { return delegate_->kind(); }
    [[nodiscard]] SimulationDeterminism determinism() const noexcept override { return delegate_->determinism(); }
    [[nodiscard]] eve::Result<void> restoreObservation(const SimulationObservation& value) override {
        return delegate_->restoreObservation(value);
    }

private:
    static void stepBox3D(void* context, const eve::SimulationStep& step,
                          const SimulationSettings& settings) noexcept {
        b3World_Step(*static_cast<b3WorldId*>(context), static_cast<float>(step.delta.seconds()),
                     settings.subStepCount);
    }

    std::unique_ptr<ISimulationBackend> delegate_;
};

class FailSecondStepProvider final : public IAcceleratorBackendProvider {
public:
    [[nodiscard]] bool supports(SimulationBackendDomain domain) const noexcept override {
        return domain == SimulationBackendDomain::World3D;
    }
    [[nodiscard]] eve::Result<std::unique_ptr<ISimulationBackend>> create(SimulationBackendDomain,
                                                                          void* state) override {
        return eve::Result<std::unique_ptr<ISimulationBackend>>::success(
            std::make_unique<FailSecondStepBackend>(static_cast<b3WorldId*>(state)));
    }
};

void checkObservableContract(ISimulationBackend &backend) {
    const eve::SimulationStep step{eve::SimulationTick{1}, eve::Duration::fromNanoseconds(16666667)};
    auto                      applied   = backend.step(step, SimulationSettings{});
    const bool                appliedOk = applied.ok();
    CHECK(appliedOk);
    const SimulationObservation observation = backend.observation();
    CHECK_EQ(observation.stepCount, std::uint64_t(1));
    CHECK(observation.lastTick == step.tick);
    CHECK_EQ(observation.simulatedDuration, step.delta);

    auto       duplicate   = backend.step(step, SimulationSettings{});
    const bool duplicateOk = duplicate.ok();
    CHECK(!duplicateOk);
    CHECK_EQ(duplicate.code(), eve::StatusCode::Rejected);
}

}  // namespace

TEST_CASE("physics.core.backendContract.isHeadlessAndObservable") {
    auto                  *physics = Physics::create();
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

    auto       stepped = backend->step(SimulationStep{eve::SimulationTick{1}, eve::Duration::fromNanoseconds(16666667)},
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
    b2World         rawWorld(b2Vec2_zero);

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
    b2World         rawWorld(b2Vec2_zero);

    auto       absent   = detail::selectSimulationBackend(SimulationBackendDomain::World2D,
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
    auto       present   = detail::selectSimulationBackend(SimulationBackendDomain::World2D,
                                                           detail::makeBox2DSimulationBackend(&rawWorld), &rawWorld, true);
    const bool presentOk = present.ok();
    REQUIRE(presentOk);
    CHECK(present.diagnostics().empty());
    auto presentSelection = std::move(present).takeValue();
    CHECK(!presentSelection.usedFallback);
    CHECK_EQ(presentSelection.actualKind, SimulationBackendKind::MockAccelerator);
}

TEST_CASE("physics.core.backendFallbackPreservesProviderFailureDiagnostics") {
    CapabilityReset reset;
    b2World rawWorld(b2Vec2_zero);

    FailingProvider provider(false);
    eve::cap::provide<IAcceleratorBackendProvider>(&provider);
    auto selected = detail::selectSimulationBackend(SimulationBackendDomain::World2D,
                                                     detail::makeBox2DSimulationBackend(&rawWorld), &rawWorld, true);
    REQUIRE(selected.ok());
    REQUIRE_EQ(selected.diagnostics().size(), std::size_t(2));
    CHECK_EQ(selected.diagnostics()[1].code(), eve::DiagnosticCode::Conflict);
    CHECK_EQ(selected.diagnostics()[1].message(), std::string("accelerator rejected shared state"));
    CHECK_EQ(selected.diagnostics()[1].path(), std::string("provider.state"));
}

TEST_CASE("physics.core.backendFallbackPreservesProviderExceptionMessage") {
    CapabilityReset reset;
    b2World rawWorld(b2Vec2_zero);

    FailingProvider provider(true);
    eve::cap::provide<IAcceleratorBackendProvider>(&provider);
    auto selected = detail::selectSimulationBackend(SimulationBackendDomain::World2D,
                                                     detail::makeBox2DSimulationBackend(&rawWorld), &rawWorld, true);
    REQUIRE(selected.ok());
    REQUIRE_EQ(selected.diagnostics().size(), std::size_t(2));
    CHECK_EQ(selected.diagnostics()[1].code(), eve::DiagnosticCode::CallbackFailure);
    CHECK_EQ(selected.diagnostics()[1].message(), std::string("accelerator setup exploded"));
}

TEST_CASE("physics.core.world3dFailedStepPreservesPreviousContactEvents") {
    CapabilityReset reset;
    FailSecondStepProvider provider;
    eve::cap::provide<IAcceleratorBackendProvider>(&provider);

    World3D world(0.f, -9.8f, 0.f, false);
    std::unique_ptr<Body3D> bodyA(world.newBody("dynamic", 0.f, 0.f, 0.f));
    std::unique_ptr<Body3D> bodyB(world.newBody("dynamic", 0.f, 0.f, 0.f));
    std::unique_ptr<Shape3D> shapeA(bodyA->newSphereShape(1.f));
    std::unique_ptr<Shape3D> shapeB(bodyB->newSphereShape(1.f));

    auto first = world.step({eve::SimulationTick{1}, eve::Duration::fromNanoseconds(16666667)});
    REQUIRE(first.ok());
    REQUIRE_GT(world.getBeginContactCount(), 0);
    const int previousCount = world.getBeginContactCount();

    auto failed = world.step({eve::SimulationTick{2}, eve::Duration::fromNanoseconds(16666667)});
    CHECK(!failed.ok());
    CHECK_EQ(world.getBeginContactCount(), previousCount);
    CHECK_EQ(world.simulationTick(), eve::SimulationTick{1});
}

TEST_CASE("physics.core.worldUpdateNeedsNoGraphics") {
    auto                  *physics = Physics::create();
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
    auto                  *physics = Physics::create();
    std::unique_ptr<World> world(physics->newWorld(0.f, 0.f));
    REQUIRE(world.get() != nullptr);
    Body *body = world->newBody("dynamic", 0.f, 0.f);
    body->newCircleFixture(5.f);

    const eve::SimulationStep step{eve::SimulationTick{1}, eve::Duration::fromNanoseconds(16666667)};
    auto                      stepped   = world->step(step);
    const bool                steppedOk = stepped.ok();
    REQUIRE(steppedOk);
    CHECK(world->simulationTick() == step.tick);
    CHECK_EQ(world->simulationObservation().lastTick, step.tick);
    CHECK(world->backendSelectionStatus().code() == eve::StatusCode::Applied);

    auto       linkResult = PhysicsLink::fromBody(*body);
    const bool linkOk     = linkResult.ok();
    REQUIRE(linkOk);
    const PhysicsLink link       = std::move(linkResult).takeValue();
    auto              resolved   = link.resolve(*world);
    const bool        resolvedOk = resolved.ok();
    REQUIRE(resolvedOk);
    CHECK(std::move(resolved).takeValue() == body);

    body->destroy();
    auto       stale   = link.resolve(*world);
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
