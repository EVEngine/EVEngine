// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "module": "physics_backend",
  "default": "module-composition",
  "origins": [
    "physics_core_boundary.cpp"
  ]
}
*/
// clang-format on

#include "physics/Box2DSimulationBackend.h"
#include "zeroerr/assert.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "physics/Body.h"
#include "physics/Body3D.h"
#include "physics/Physics.h"
#include "physics/PhysicsLink.h"
#include "physics/Shape3D.h"
#include "physics/World.h"
#include "physics/World3D.h"
#include "physics/backend/SimulationBackend.h"

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
                                                                          void* /*state*/) override {
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

    [[nodiscard]] eve::Result<std::unique_ptr<ISimulationBackend>> create(SimulationBackendDomain, void*) override {
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

    [[nodiscard]] eve::Result<void> step(const eve::SimulationStep& step, const SimulationSettings& settings) override {
        if (delegate_->observation().stepCount != 0) {
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Failed, "injected second-step failure", "physics.test.step"));
        }
        return delegate_->step(step, settings);
    }
    [[nodiscard]] SimulationObservation observation() const noexcept override { return delegate_->observation(); }
    [[nodiscard]] SimulationBackendKind kind() const noexcept override { return delegate_->kind(); }
    [[nodiscard]] SimulationDeterminism determinism() const noexcept override { return delegate_->determinism(); }
    [[nodiscard]] eve::Result<void>     restoreObservation(const SimulationObservation& value) override {
        return delegate_->restoreObservation(value);
    }

private:
    static void stepBox3D(void* context, const eve::SimulationStep& step, const SimulationSettings& settings) noexcept {
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

[[maybe_unused]] void checkObservableContract(ISimulationBackend& backend) {
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


TEST_CASE("physics.core.backendContract.rejectsNullWorld") {
    bool threw = false;
    try {
        [[maybe_unused]] auto backend = detail::makeBox2DSimulationBackend(nullptr);
    } catch (...) {
        threw = true;
    }
    REQUIRE(threw);
}
