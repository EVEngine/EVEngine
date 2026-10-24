#include <cmath>
#include <limits>
#include <memory>
#include "common/Snapshot.h"
#include "common/WorldSphereSweep.h"
#include "physics/Body3D.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"
#include "physics/WorldSphereSweep.h"
#include "weapon/CombatCarrier.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve;
using namespace eve::physics;
namespace {
struct Box {
    std::unique_ptr<Body3D>  body;
    std::unique_ptr<Shape3D> shape;
    Box(World3D& world, float x)
        : body(world.newBody("static", x, 0, 0)), shape(body->newBoxShape(0.2f, 4, 2, 1, 0.3f, 0)) {}
};
SnapshotHashProvider hashProvider() {
    return [](std::string_view text) -> Result<ContentId> {
        ContentId::Bytes bytes{};
        std::uint64_t    hash = 14695981039346656037ull;
        for (unsigned char c : text) hash = (hash ^ c) * 1099511628211ull;
        for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<std::uint8_t>(hash >> ((i % 8) * 8));
        return Result<ContentId>::success(ContentId(bytes));
    };
}
void contract(RigidBackend3D backend, zeroerr::TestContext* _ZEROERR_TEST_CONTEXT) {
    auto created = World3D::createWithBackend(backend, 0, 0, 0, false);
    REQUIRE(created.ok());
    auto world = std::move(created).takeValue();
    Box  wall(*world, 2);
    world->setQueryFilter(2, 4);
    world->setQueryIgnoredBodyId(wall.body->getId());
    auto bound = worldSphereSweepFactory().bind(world->queryToken(), {0.1f});
    REQUIRE(bound.ok());
    auto hits = bound.value()->query(0, 0, 0, 4, 0, 0);
    REQUIRE(hits.ok());
    REQUIRE(hits.value().size() == 1);
    REQUIRE(std::abs(hits.value()[0].x - 1.8) < 0.035);
    REQUIRE(hits.value()[0].nx < -0.9);
    REQUIRE(hits.value()[0].bodyId == wall.body->getId());
    REQUIRE(hits.value()[0].identity.body == wall.body->runtimeHandle().packed());
    REQUIRE(bound.value()->query(2, 0, 0, 3, 0, 0).value().empty());
    REQUIRE(world->getQueryCategoryBits() == 2);
    REQUIRE(world->getQueryMaskBits() == 4);
    REQUIRE(world->getQueryIgnoredBodyId() == wall.body->getId());
    auto masked = worldSphereSweepFactory().bind(world->queryToken(), {0.1f, ~0u, 0u, -1});
    REQUIRE(masked.ok());
    REQUIRE(masked.value()->query(0, 0, 0, 4, 0, 0).value().empty());
    auto ignored = worldSphereSweepFactory().bind(world->queryToken(), {0.1f, ~0u, ~0u, wall.body->getId()});
    REQUIRE(ignored.ok());
    REQUIRE(ignored.value()->query(0, 0, 0, 4, 0, 0).value().empty());
    REQUIRE(!bound.value()->query(0, 0, 0, std::numeric_limits<double>::infinity(), 0, 0).ok());
    REQUIRE(!worldSphereSweepFactory().bind(world->queryToken(), {-1}).ok());
    const auto token = world->queryToken();
    auto       saved = world->snapshot(hashProvider());
    REQUIRE(saved.ok());
    auto invalid          = saved.value();
    invalid.schemaVersion = SchemaVersion{9999};
    REQUIRE(!world->restore(invalid, hashProvider()).ok());
    REQUIRE(bound.value()->query(0, 0, 0, 4, 0, 0).ok());
    REQUIRE(world->restore(saved.value(), hashProvider()).ok());
    REQUIRE(world->queryToken() != token);
    REQUIRE(!bound.value()->query(0, 0, 0, 4, 0, 0).ok());
    REQUIRE(!worldSphereSweepFactory().bind(token, {0.1f}).ok());
    auto rebound = worldSphereSweepFactory().bind(world->queryToken(), {0.1f});
    REQUIRE(rebound.ok());
    REQUIRE(rebound.value()->query(0, 0, 0, 4, 0, 0).value().size() == 1);
    world->destroy();
    REQUIRE(!rebound.value()->query(0, 0, 0, 4, 0, 0).ok());
    world.reset();
    REQUIRE(!rebound.value()->query(0, 0, 0, 4, 0, 0).ok());
}
}  // namespace
TEST_CASE("carrierWorldSweep.box3dContractAndRestore") { contract(RigidBackend3D::Box3D, _ZEROERR_TEST_CONTEXT); }
#if EVENGINE_HAS_JOLT
TEST_CASE("carrierWorldSweep.joltContractAndRestore") { contract(RigidBackend3D::Jolt, _ZEROERR_TEST_CONTEXT); }
#endif
TEST_CASE("carrierWorldSweep.bodyDestructionAndBudget") {
    World3D world(0, 0, 0, false);
    Box     wall(world, 2);
    auto    bound = worldSphereSweepFactory().bind(world.queryToken(), {0.1f, ~0u, ~0u, wall.body->getId()});
    REQUIRE(bound.ok());
    world.destroyBody(wall.body.get());
    REQUIRE(!bound.value()->query(0, 0, 0, 4, 0, 0).ok());
    auto normal = worldSphereSweepFactory().bind(world.queryToken(), {0.1f});
    REQUIRE(normal.ok());
    REQUIRE(normal.value()->query(0, 0, 0, 4, 0, 0).value().empty());
    std::vector<std::unique_ptr<Box>> boxes;
    for (int i = 0; i < 257; ++i) boxes.push_back(std::make_unique<Box>(world, 1.0f + static_cast<float>(i) * 0.4f));
    auto overflow = normal.value()->query(0, 0, 0, 110, 0, 0);
    REQUIRE(!overflow.ok());
}

TEST_CASE("carrierWorldSweep.carrierPierceSnapshotAndFailure") {
    using namespace eve::weapon;
    World3D world(0, 0, 0, false);
    Box     wall(world, 2);
    auto    bound = worldSphereSweepFactory().bind(world.queryToken(), {0.1f});
    REQUIRE(bound.ok());
    class Probe final : public ICarrierHitProbe {
    public:
        IWorldSphereSweep& sweep;
        explicit Probe(IWorldSphereSweep& source) : sweep(source) {}
        Result<std::vector<Contact>> query(CarrierHandle, const CarrierMotion& a,
                                           const CarrierMotion& b) const override {
            auto result =
                sweep.query(a.position.x, a.position.y, a.position.z, b.position.x, b.position.y, b.position.z);
            if (!result) return Result<std::vector<Contact>>::failure(result.status());
            std::vector<Contact> contacts;
            for (const auto& hit : result.value()) {
                Contact c;
                c.point        = {hit.x, hit.y, hit.z};
                c.normal       = {hit.nx, hit.ny, hit.nz};
                c.fraction     = hit.fraction;
                c.worldContact = hit;
                contacts.push_back(c);
            }
            return Result<std::vector<Contact>>::success(std::move(contacts));
        }
    } probe(*bound.value());
    auto          seconds = [](double t) { return Duration::fromSeconds(t).value(); };
    CarrierRecipe spec;
    spec.id                = *LogicalId::parse("world:bolt");
    spec.speed             = 10;
    spec.lifetime          = seconds(3);
    spec.physicsWorldSweep = true;
    spec.motionOps         = {{CarrierMotionOpKind::IntegrateLinear}};
    spec.triggers          = {{CarrierTriggerKind::OnHit}, {CarrierTriggerKind::OnExpire}};
    CarrierImpact pierce{CarrierImpactKind::Pierce, CarrierTriggerKind::OnHit};
    pierce.pierceCount = 10;
    spec.impacts       = {{CarrierImpactKind::EmitTrigger, CarrierTriggerKind::OnHit},
                          pierce,
                          {CarrierImpactKind::Release, CarrierTriggerKind::OnExpire}};
    CombatCarrierRuntime pool;
    auto                 live = pool.spawn(spec, {{0, 0, 0}, {1, 0, 0}});
    REQUIRE(live.ok());
    auto frame = pool.update(seconds(0.3), nullptr, nullptr, nullptr, &probe);
    REQUIRE(frame.ok());
    REQUIRE(frame.value().events.size() == 1);
    REQUIRE(frame.value().events[0].target.id == 0);
    REQUIRE(pool.find(live.value())->piercedWorldBodies.size() == 1);
    auto saved = pool.snapshot();
    REQUIRE(saved.schemaVersion == 4);
    CombatCarrierRuntime restored;
    REQUIRE(restored.registerRecipe(spec).ok());
    REQUIRE(restored.restore(saved).ok());
    class Reverse final : public ICarrierMotionFunction {
        Result<CarrierMotion> evaluate(const CarrierState& before, const CarrierMotion&, Duration dt) const override {
            CarrierMotion next = before.motion;
            next.velocity      = {-10, 0, 0};
            next.position.x -= 10 * dt.seconds();
            return Result<CarrierMotion>::success(next);
        }
    } reverse;
    auto returned = restored.updateWithMotion(seconds(0.3), reverse, nullptr, nullptr, nullptr, &probe);
    REQUIRE(returned.ok());
    REQUIRE(returned.value().events.empty());
    auto bad = saved;
    bad.slots[live.value().slot].state->piercedWorldBodies.push_back(
        saved.slots[live.value().slot].state->piercedWorldBodies[0]);
    REQUIRE(!restored.restore(bad).ok());
    const auto age = restored.find(live.value())->age;
    world.destroy();
    REQUIRE(!restored.update(seconds(0.1), nullptr, nullptr, nullptr, &probe).ok());
    REQUIRE(restored.find(live.value())->age == age);
}
