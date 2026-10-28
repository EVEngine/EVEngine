// eve-test-policy: {"schema":"evengine.test-policy","version":1,"module":"weapon"}
#include <memory>
#include <stdexcept>
#include "common/Diagnostic.h"
#include "weapon/WorldHitProbe.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
namespace {
using namespace eve;
using namespace eve::weapon;
Duration      seconds(double value) { return Duration::fromSeconds(value).value(); }
CarrierRecipe recipe() {
    CarrierRecipe spec;
    spec.id                = *LogicalId::parse("composition:bolt");
    spec.speed             = 10;
    spec.lifetime          = seconds(2);
    spec.physicsWorldSweep = true;
    spec.motionOps         = {{CarrierMotionOpKind::IntegrateLinear}};
    spec.triggers          = {{CarrierTriggerKind::OnHit}};
    spec.impacts           = {{CarrierImpactKind::EmitHit, CarrierTriggerKind::OnHit}};
    return spec;
}
class Sweep final : public IWorldSphereSweep {
public:
    Result<std::vector<WorldSphereContact>> query(double, double, double, double, double, double) const override {
        WorldSphereContact contact;
        contact.identity = {0x100000001ull, 0x100000002ull};
        contact.fraction = 0.5;
        contact.x        = 1;
        contact.nx       = -1;
        contact.bodyId   = 2;
        return Result<std::vector<WorldSphereContact>>::success({contact, contact});
    }
};
class Mapper final : public IWorldHitTargetMapper {
public:
    mutable int                              calls  = 0;
    int                                      failAt = 0;
    bool                                     throws = false;
    std::optional<ecs::EntityHandle>         entity;
    Result<std::optional<ecs::EntityHandle>> target(const WorldSphereContact&) const override {
        if (++calls == failAt) {
            if (throws) throw std::runtime_error("injected mapper failure");
            return Result<std::optional<ecs::EntityHandle>>::failure(
                Diagnostic::error(DiagnosticCode::NotFound, "stale gameplay link"));
        }
        return Result<std::optional<ecs::EntityHandle>>::success(entity);
    }
};
}  // namespace
TEST_CASE("carrierComposition.mappingAndLateFailureAreAtomic") {
    auto  mapper              = std::make_unique<Mapper>();
    auto& policy              = *mapper;
    policy.entity             = ecs::EntityHandle{};
    policy.entity->id         = 7;
    policy.entity->generation = 3;
    WorldHitProbe        probe(std::make_unique<Sweep>(), std::move(mapper));
    CombatCarrierRuntime runtime;
    auto                 live = runtime.spawn(recipe(), {{}, {1, 0, 0}});
    REQUIRE(live.ok());
    policy.failAt = 2;
    REQUIRE(!runtime.update(seconds(0.2), nullptr, nullptr, nullptr, &probe).ok());
    REQUIRE(runtime.find(live.value())->age.isZero());
    policy.calls  = 0;
    policy.throws = true;
    REQUIRE(!runtime.update(seconds(0.2), nullptr, nullptr, nullptr, &probe).ok());
    REQUIRE(runtime.find(live.value())->age.isZero());
    policy.failAt = 0;
    auto frame    = runtime.update(seconds(0.2), nullptr, nullptr, nullptr, &probe);
    REQUIRE(frame.ok());
    REQUIRE(frame.value().events.size() == 2);
    REQUIRE(frame.value().events[0].target.id == 7);
    REQUIRE(frame.value().events[0].target.generation == 3);
    REQUIRE(frame.value().events[0].worldContact->bodyId == 2);
    policy.entity.reset();
    auto scenery = probe.query({}, {}, {});
    REQUIRE(scenery.ok());
    REQUIRE(scenery.value()[0].target.id == 0);
    policy.entity = ecs::EntityHandle{};
    REQUIRE(!probe.query({}, {}, {}).ok());
}

TEST_CASE("carrierComposition.absentSessionAndDisabledCollision") {
    WorldHitProbe absent(nullptr);
    REQUIRE(!absent.query({}, {}, {}).ok());
    CombatCarrierRuntime runtime;
    auto                 spec = recipe();
    spec.collisionEnabled     = false;
    REQUIRE(runtime.spawn(spec, {{}, {1, 0, 0}}).ok());
    REQUIRE(runtime.update(seconds(0.1), nullptr, nullptr, nullptr, &absent).ok());
    REQUIRE(runtime.states()[0].motion.position.x == 1);
    WorldHitProbe unmapped(std::make_unique<Sweep>());
    auto          hits = unmapped.query({}, {}, {});
    REQUIRE(hits.ok());
    REQUIRE(hits.value()[0].target.id == 0);
}
