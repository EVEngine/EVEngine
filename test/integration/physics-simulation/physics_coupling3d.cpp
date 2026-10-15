#include <box3d/box3d.h>
#include <cmath>
#include <memory>
#include "physics/Body3D.h"
#include "physics/Joint3D.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"
#include "physics/mechanics/Coupling3D.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::physics;
namespace {
struct TransmissionRig {
    World3D                  world{0, 0, 0, false};
    std::unique_ptr<Body3D>  ground{world.newBody("static", 0, 0, 0)};
    std::unique_ptr<Body3D>  a{world.newBody("dynamic", -2, 0, 0)};
    std::unique_ptr<Body3D>  b{world.newBody("dynamic", 2, 0, 0)};
    std::unique_ptr<Shape3D> fa{a->newSphereShape(.3f)};
    std::unique_ptr<Shape3D> fb{b->newSphereShape(.5f)};
    TransmissionRig() {
        a->setLinearDamping(0);
        a->setAngularDamping(0);
        b->setLinearDamping(0);
        b->setAngularDamping(0);
    }
    void step(int count) {
        for (int i = 0; i < count; ++i) world.update(1.f / 240);
    }
};
}  // namespace
TEST_CASE("physics.coupling3d.gearTracksMultipleTurnsAndSourceDeath") {
    TransmissionRig          r;
    std::unique_ptr<Joint3D> ja{r.world.newRevoluteJoint(r.ground.get(), r.a.get(), -2, 0, 0, 0, 0, 1)};
    std::unique_ptr<Joint3D> jb{r.world.newRevoluteJoint(r.ground.get(), r.b.get(), 2, 0, 0, 0, 0, 1)};
    GearCoupling3DSettings   settings;
    settings.ratio = 2;
    auto created   = Coupling3D::createGear(r.world, *ja, *jb, settings);
    REQUIRE(created.ok());
    auto gear = std::move(created).takeValue();
    r.a->setAngularVelocity(0, 0, 60);
    r.step(1199);
    auto before = gear->sample();
    REQUIRE(before.ok());
    r.step(1);
    auto state = gear->sample();
    REQUIRE(state.ok());
    CHECK(std::abs(state.value().coordinateA) > 6.3);
    CHECK(std::abs(state.value().error) < .002);
    // Stabilization intentionally adds velocity to remove the preceding phase error.
    const double bias = settings.limits.positionCorrection * before.value().error / (1.f / 240);
    CHECK(std::abs(r.a->getAngularVelocityZ() + 2 * r.b->getAngularVelocityZ() + bias) < 1e-5);
    ja.reset();
    CHECK(!gear->isValid());
    CHECK(!gear->sample().ok());
    REQUIRE(gear->destroy().ok());
}
TEST_CASE("physics.coupling3d.pulleyConservesWeightedLength") {
    TransmissionRig          r;
    PulleyCoupling3DSettings settings;
    settings.groundA = {-2, 4, 0};
    settings.groundB = {2, 4, 0};
    settings.ratio   = 2;
    settings.length  = 12;
    auto created     = Coupling3D::createPulley(r.world, *r.a, *r.b, settings);
    REQUIRE(created.ok());
    auto pulley = std::move(created).takeValue();
    r.a->setLinearVelocity(0, -1, 0);
    r.step(240);
    auto state = pulley->sample();
    REQUIRE(state.ok());
    CHECK(std::abs(state.value().error) < .001);
    CHECK(r.b->getLinearVelocityY() > 0);
    r.a.reset();
    CHECK(!pulley->isValid());
}
TEST_CASE("physics.coupling3d.slackRopeDoesNotPush") {
    TransmissionRig          r;
    PulleyCoupling3DSettings settings;
    settings.groundA     = {-2, 4, 0};
    settings.groundB     = {2, 4, 0};
    settings.ratio       = 2;
    settings.length      = 13;
    settings.tensionOnly = true;
    auto created         = Coupling3D::createPulley(r.world, *r.a, *r.b, settings);
    REQUIRE(created.ok());
    auto pulley = std::move(created).takeValue();
    r.step(20);
    auto state = pulley->sample();
    REQUIRE(state.ok());
    CHECK_EQ(state.value().impulse, 0);
    CHECK_EQ(r.a->getLinearVelocityY(), 0);
    CHECK_EQ(r.b->getLinearVelocityY(), 0);
}

TEST_CASE("physics.coupling3d.mixedRackAndPinion") {
    TransmissionRig          r;
    std::unique_ptr<Joint3D> hinge{r.world.newRevoluteJoint(r.ground.get(), r.a.get(), -2, 0, 0, 0, 0, 1)};
    std::unique_ptr<Joint3D> slide{r.world.newPrismaticJoint(r.ground.get(), r.b.get(), 2, 0, 0, 0, 1, 0)};
    GearCoupling3DSettings   settings;
    settings.ratio = 2;
    auto created   = Coupling3D::createGear(r.world, *hinge, *slide, settings);
    REQUIRE(created.ok());
    auto gear = std::move(created).takeValue();
    r.a->setAngularVelocity(0, 0, 5);
    r.step(240);
    auto state = gear->sample();
    REQUIRE(state.ok());
    CHECK(std::abs(state.value().coordinateA) > .1);
    CHECK(std::abs(state.value().coordinateB) > .05);
    CHECK(std::abs(state.value().error) < .001);
}

TEST_CASE("physics.coupling3d.offCenterPulleyAppliesTorqueAndForceCap") {
    TransmissionRig          r;
    PulleyCoupling3DSettings settings;
    settings.localAnchorA    = {.2f, 0, 0};
    settings.groundA         = {-1.8f, 4, 0};
    settings.groundB         = {2, 4, 0};
    settings.ratio           = 2;
    settings.length          = 12;
    settings.limits.maxForce = .001f;
    auto created             = Coupling3D::createPulley(r.world, *r.a, *r.b, settings);
    REQUIRE(created.ok());
    auto pulley = std::move(created).takeValue();
    r.a->setLinearVelocity(0, -1, 0);
    r.step(1);
    auto state = pulley->sample();
    REQUIRE(state.ok());
    CHECK(state.value().saturated);
    CHECK(std::abs(state.value().impulse) <= settings.limits.maxForce / 240.0 + 1e-10);
    CHECK(r.a->getAngularVelocityZ() > 0);
    REQUIRE(pulley->setEnabled(false).ok());
    r.step(1);
    auto disabled = pulley->sample();
    REQUIRE(disabled.ok());
    CHECK(!disabled.value().active);
    CHECK_EQ(disabled.value().impulse, 0);
}

TEST_CASE("physics.coupling3d.worldFirstAndWrapperFirstLifetime") {
    TransmissionRig          r;
    PulleyCoupling3DSettings settings;
    settings.groundA = {-2, 4, 0};
    settings.groundB = {2, 4, 0};
    settings.length  = 8;
    auto first       = Coupling3D::createPulley(r.world, *r.a, *r.b, settings);
    REQUIRE(first.ok());
    auto wrapper = std::move(first).takeValue();
    wrapper.reset();
    CHECK(r.a->isValid());
    r.step(1);
    auto second = Coupling3D::createPulley(r.world, *r.a, *r.b, settings);
    REQUIRE(second.ok());
    wrapper = std::move(second).takeValue();
    r.world.destroy();
    CHECK(!wrapper->isValid());
    REQUIRE(wrapper->destroy().ok());
}

namespace {
struct FailureProbe {
    bool    reject        = true;
    Body3D* body          = nullptr;
    float   observedSpeed = 0;
};
class RejectingTransmissionBackend final : public ISimulationBackend {
public:
    RejectingTransmissionBackend(void* state, FailureProbe& probe)
        : probe_(probe),
          delegate_(detail::makeCallbackSimulationBackend(state, &advance, SimulationBackendKind::MockAccelerator,
                                                          SimulationDeterminism::ToleranceBounded)) {}
    eve::Result<void> step(const eve::SimulationStep& step, const SimulationSettings& settings) override {
        probe_.observedSpeed = probe_.body->getLinearVelocityY();
        if (probe_.reject)
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Failed, "injected transmission backend failure", "physics.test.coupling"));
        return delegate_->step(step, settings);
    }
    SimulationObservation observation() const noexcept override { return delegate_->observation(); }
    SimulationBackendKind kind() const noexcept override { return delegate_->kind(); }
    SimulationDeterminism determinism() const noexcept override { return delegate_->determinism(); }

private:
    static void advance(void* state, const eve::SimulationStep& step, const SimulationSettings& settings) noexcept {
        b3World_Step(*static_cast<b3WorldId*>(state), static_cast<float>(step.delta.seconds()), settings.subStepCount);
    }
    FailureProbe&                       probe_;
    std::unique_ptr<ISimulationBackend> delegate_;
};
class RejectingTransmissionProvider final : public IAcceleratorBackendProvider {
public:
    FailureProbe probe;
    bool         supports(SimulationBackendDomain domain) const noexcept override {
        return domain == SimulationBackendDomain::World3D;
    }
    eve::Result<std::unique_ptr<ISimulationBackend>> create(SimulationBackendDomain, void* state) override {
        return eve::Result<std::unique_ptr<ISimulationBackend>>::success(
            std::make_unique<RejectingTransmissionBackend>(state, probe));
    }
};
}  // namespace
TEST_CASE("physics.coupling3d.backendFailureRollsBackProjectionAndPhase") {
    RejectingTransmissionProvider provider;
    auto                          registered = AcceleratorBackendProviderRegistration::registerProvider(provider);
    REQUIRE(registered.ok());
    auto            registration = std::move(registered).takeValue();
    TransmissionRig r;
    REQUIRE(r.world.backendKind() == SimulationBackendKind::MockAccelerator);
    provider.probe.body = r.a.get();
    PulleyCoupling3DSettings settings;
    settings.groundA = {-2, 4, 0};
    settings.groundB = {2, 4, 0};
    settings.length  = 8;
    auto created     = Coupling3D::createPulley(r.world, *r.a, *r.b, settings);
    REQUIRE(created.ok());
    auto pulley = std::move(created).takeValue();
    r.a->setLinearVelocity(0, -1, 0);
    const eve::SimulationStep step{eve::SimulationTick{1}, eve::Duration::fromNanoseconds(4166667)};
    auto                      failed = r.world.step(step);
    CHECK(!failed.ok());
    CHECK(provider.probe.observedSpeed != -1);
    CHECK_EQ(r.a->getLinearVelocityY(), -1);
    CHECK_EQ(r.b->getLinearVelocityY(), 0);
    CHECK_EQ(r.world.simulationObservation().stepCount, 0u);
    auto state = pulley->sample();
    REQUIRE(state.ok());
    CHECK_EQ(state.value().impulse, 0);
    CHECK_EQ(state.value().error, 0);
    provider.probe.reject = false;
    REQUIRE(r.world.step(step).ok());
    CHECK_EQ(r.world.simulationObservation().stepCount, 1u);
}
