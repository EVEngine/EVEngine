#include <Box2D/Box2D.h>
#include <box3d/box3d.h>
#include <cmath>
#include <memory>
#include "physics/Body.h"
#include "physics/Body3D.h"
#include "physics/Fixture.h"
#include "physics/Shape3D.h"
#include "physics/World.h"
#include "physics/World3D.h"
#include "physics/forces/ForceBatch.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve;
using namespace eve::physics;
namespace {
const SimulationStep forceStep{SimulationTick{1}, Duration::fromNanoseconds(10000000)};
SnapshotHashProvider digest() {
    return [](std::string_view input) -> Result<ContentId> {
        ContentId::Bytes bytes{};
        std::uint64_t    hash = 14695981039346656037ull;
        for (unsigned char c : input) hash = (hash ^ c) * 1099511628211ull;
        for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<std::uint8_t>(hash >> ((i % 8) * 8));
        return Result<ContentId>::success(ContentId(bytes));
    };
}
class RejectingForceBackend final : public ISimulationBackend {
public:
    RejectingForceBackend(void* state, SimulationBackendDomain domain, bool& reject)
        : reject_(reject),
          delegate_(eve::physics::detail::makeCallbackSimulationBackend(
              state, domain == SimulationBackendDomain::World2D ? &advance2D : &advance3D,
              SimulationBackendKind::MockAccelerator, SimulationDeterminism::ToleranceBounded)) {}
    Result<void> step(const SimulationStep& step, const SimulationSettings& settings) override {
        if (reject_)
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::Failed, "injected force step failure"));
        return delegate_->step(step, settings);
    }
    SimulationObservation observation() const noexcept override { return delegate_->observation(); }
    SimulationBackendKind kind() const noexcept override { return delegate_->kind(); }
    SimulationDeterminism determinism() const noexcept override { return delegate_->determinism(); }

private:
    static void advance2D(void* state, const SimulationStep& step, const SimulationSettings& settings) noexcept {
        static_cast<b2World*>(state)->Step(static_cast<float>(step.delta.seconds()), settings.velocityIterations,
                                           settings.positionIterations);
    }
    static void advance3D(void* state, const SimulationStep& step, const SimulationSettings& settings) noexcept {
        b3World_Step(*static_cast<b3WorldId*>(state), static_cast<float>(step.delta.seconds()), settings.subStepCount);
    }
    bool&                               reject_;
    std::unique_ptr<ISimulationBackend> delegate_;
};
class RejectingForceProvider final : public IAcceleratorBackendProvider {
public:
    bool reject = true;
    bool supports(SimulationBackendDomain domain) const noexcept override {
        return domain == SimulationBackendDomain::World2D || domain == SimulationBackendDomain::World3D;
    }
    Result<std::unique_ptr<ISimulationBackend>> create(SimulationBackendDomain domain, void* state) override {
        return Result<std::unique_ptr<ISimulationBackend>>::success(
            std::make_unique<RejectingForceBackend>(state, domain, reject));
    }
};
}  // namespace
TEST_CASE("physics.forces.receiptRejectsWrongStepAndRetriesBackendOnce") {
    RejectingForceProvider provider;
    auto                   registered = AcceleratorBackendProviderRegistration::registerProvider(provider);
    REQUIRE(registered.ok());
    auto registration = std::move(registered).takeValue();
    auto check        = [&](auto& world, auto& body, double meter) {
        provider.reject = true;
        REQUIRE(world.backendKind() == SimulationBackendKind::MockAccelerator);
        auto link = PhysicsLink::fromBody(body);
        REQUIRE(link.ok());
        ForceLoad load{link.value(), {2 * body.getMass(), 0, 0}, {}, {}};
        auto      batch = ForceBatch::prepare(world, std::span(&load, 1));
        REQUIRE(batch.ok());
        CHECK(!batch.value().commit(world, {SimulationTick{1}, Duration::zero()}).ok());
        auto submitted = batch.value().commit(world, forceStep);
        REQUIRE(submitted.ok());
        auto receipt = submitted.value();
        CHECK(receipt.status() == ForceStepStatus::Pending);
        CHECK(receipt.step().tick == forceStep.tick);
        CHECK(receipt.step().delta == forceStep.delta);
        auto other = ForceBatch::prepare(world, std::span(&load, 1));
        REQUIRE(other.ok());
        CHECK(!other.value().commit(world, {SimulationTick{2}, forceStep.delta}).ok());
        CHECK(!world.step({SimulationTick{2}, forceStep.delta}).ok());
        CHECK(!world.step({forceStep.tick, Duration::fromNanoseconds(20000000)}).ok());
        CHECK(!world.step(forceStep).ok());
        CHECK(receipt.status() == ForceStepStatus::Pending);
        CHECK_EQ(world.simulationObservation().stepCount, 0u);
        CHECK_EQ(body.getLinearVelocityX(), 0);
        CHECK(!batch.value().commit(world, forceStep).ok());
        CHECK(!world.snapshot(digest()).ok());
        provider.reject = false;
        REQUIRE(world.step(forceStep).ok());
        CHECK(receipt.status() == ForceStepStatus::Applied);
        CHECK(std::abs(body.getLinearVelocityX() / meter - .02) < 1e-6);
        REQUIRE(world.step({SimulationTick{2}, forceStep.delta}).ok());
        CHECK(std::abs(body.getLinearVelocityX() / meter - .02) < 1e-6);
        world.destroy();
        CHECK(receipt.status() == ForceStepStatus::Applied);
    };
    World                    planar(0, 0, false, 30);
    std::unique_ptr<Body>    a(planar.newBody("dynamic", 0, 0));
    std::unique_ptr<Fixture> fa(a->newCircleFixture(30));
    check(planar, *a, 30);
    World3D                  spatial(0, 0, 0, false);
    std::unique_ptr<Body3D>  b(spatial.newBody("dynamic", 0, 0, 0));
    std::unique_ptr<Shape3D> fb(b->newSphereShape(1));
    b->setLinearDamping(0);
    check(spatial, *b, 1);
}
TEST_CASE("physics.forces.receiptRestoreAndDestroyCancelPendingOnly") {
    auto check = [&](auto& world, auto& body) {
        auto snapshot = world.snapshot(digest());
        REQUIRE(snapshot.ok());
        auto link = PhysicsLink::fromBody(body);
        REQUIRE(link.ok());
        ForceLoad load{link.value(), {1, 0, 0}, {}, {}};
        auto      first  = ForceBatch::prepare(world, std::span(&load, 1));
        auto      second = ForceBatch::prepare(world, std::span(&load, 1));
        REQUIRE(first.ok());
        REQUIRE(second.ok());
        auto r1 = first.value().commit(world, forceStep);
        auto r2 = second.value().commit(world, forceStep);
        REQUIRE(r1.ok());
        REQUIRE(r2.ok());
        auto corrupt          = snapshot.value();
        corrupt.schemaVersion = SchemaVersion{999};
        CHECK(!world.restore(corrupt, digest()).ok());
        CHECK(r1.value().status() == ForceStepStatus::Pending);
        CHECK(r2.value().status() == ForceStepStatus::Pending);
        REQUIRE(world.restore(snapshot.value(), digest()).ok());
        CHECK(r1.value().status() == ForceStepStatus::Cancelled);
        CHECK(r2.value().status() == ForceStepStatus::Cancelled);
        CHECK(!link.value().resolve(world).ok());
        // Restore removed raw pending forces; the new world can advance at a different duration.
        REQUIRE(world.step({SimulationTick{1}, Duration::fromNanoseconds(20000000)}).ok());
        auto empty = ForceBatch::prepare(world, {});
        REQUIRE(empty.ok());
        auto pending = empty.value().commit(world, {SimulationTick{2}, forceStep.delta});
        REQUIRE(pending.ok());
        auto moved = std::move(pending.value());
        CHECK(pending.value().status() == ForceStepStatus::Cancelled);
        world.destroy();
        CHECK(moved.status() == ForceStepStatus::Cancelled);
    };
    World                    planar(0, 0, false, 30);
    std::unique_ptr<Body>    a(planar.newBody("dynamic", 0, 0));
    std::unique_ptr<Fixture> fa(a->newCircleFixture(30));
    check(planar, *a);
    World3D                  spatial(0, 0, 0, false);
    std::unique_ptr<Body3D>  b(spatial.newBody("dynamic", 0, 0, 0));
    std::unique_ptr<Shape3D> fb(b->newSphereShape(1));
    check(spatial, *b);
}
TEST_CASE("physics.forces.receiptRequiresAutomaticForceClearing2D") {
    World world(0, 0, false, 30);
    auto  batch = ForceBatch::prepare(world, {});
    REQUIRE(batch.ok());
    world.raw()->SetAutoClearForces(false);
    CHECK(!batch.value().commit(world, forceStep).ok());
    world.raw()->SetAutoClearForces(true);
    auto receipt = batch.value().commit(world, forceStep);
    REQUIRE(receipt.ok());
    world.raw()->SetAutoClearForces(false);
    CHECK(!world.step(forceStep).ok());
    CHECK(receipt.value().status() == ForceStepStatus::Pending);
    world.raw()->SetAutoClearForces(true);
    REQUIRE(world.step(forceStep).ok());
    CHECK(receipt.value().status() == ForceStepStatus::Applied);
}
