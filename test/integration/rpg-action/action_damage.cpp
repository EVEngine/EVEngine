// clang-format off
/* eve-test-policy:
{"schema":"evengine.test-policy","version":1,"module":"rpg"}
*/
// clang-format on
#include "rpg/ActionDamageSink.h"
#include "rpg/RPGActor.h"
#include "rpg/VitalsSystem.h"
#include "zeroerr/unittest.h"

#include <limits>

namespace {
using namespace eve;
SubjectRef identity(const char* text) {
    const auto id = PersistentId::parse(text);
    REQUIRE(id.has_value());
    return SubjectRef::fromPersistentId(*id);
}
bool same(ecs::EntityHandle a, ecs::EntityHandle b) {
    return a.table == b.table && a.type == b.type && a.id == b.id && a.generation == b.generation;
}
struct Fixture {
    ecs::Table        world;
    ecs::ScopedTable  scope{world};
    rpg::RPGActor*    hero        = rpg::RPGActor::createActor();
    rpg::RPGActor*    enemy       = rpg::RPGActor::createActor();
    ecs::EntityHandle heroHandle  = ecs::handle_of(hero);
    ecs::EntityHandle enemyHandle = ecs::handle_of(enemy);
    SubjectRef        heroId      = identity("01020304-0506-0708-890a-0b0c0d0e0f10");
    SubjectRef        enemyId     = identity("01020304-0506-0708-890a-0b0c0d0e0f11");
    Fixture() {
        enemy->setBaseAttribute("hp", 100);
        enemy->setCurrent("hp", 100);
        enemy->setBaseAttribute("armor", 100);
    }
    rpg::RPGDamageResolver resolver() {
        return [this](ecs::EntityHandle handle) -> std::optional<rpg::RPGDamageTarget> {
            if (same(handle, heroHandle)) return rpg::RPGDamageTarget{heroHandle, heroId};
            if (same(handle, enemyHandle)) return rpg::RPGDamageTarget{enemyHandle, enemyId};
            return std::nullopt;
        };
    }
    action::ActionNotifyContext context() {
        action::ActionNotifyContext result;
        result.source  = heroHandle;
        result.targets = {enemyHandle};
        result.tick    = SimulationTick(7);
        return result;
    }
    action::ActionTimelineEvent hit() {
        action::ActionTimelineEvent event;
        event.type    = *LogicalId::parse("combat:damage");
        event.payload = {{"damageType", "Damage.Physical"}, {"amount", 300.0}};
        return event;
    }
};
}  // namespace

TEST_CASE("RPG action damage dispatch uses Vitals and emits death only for a live transition") {
    Fixture f;
    auto    registry = action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());
    rpg::RPGActionDamageSink sink(f.resolver());
    auto                     applied = registry.value().dispatch(f.hit(), f.context());
    REQUIRE(applied.ok());
    CHECK_EQ(f.enemy->getCurrent("hp"), 0.0);
    CHECK_EQ(f.enemy->getBaseAttribute("hp"), 100.0);
    auto receipts = sink.takeReceipts();
    REQUIRE_EQ(receipts.size(), 1u);
    CHECK(receipts[0].source == f.heroId);
    CHECK(receipts[0].target == f.enemyId);
    CHECK_EQ(receipts[0].applied, 100.0);
    CHECK_EQ(receipts[0].tick.value(), 7u);
    CHECK(receipts[0].killed);
    auto repeated = registry.value().dispatch(f.hit(), f.context());
    REQUIRE(repeated.ok());
    auto later = sink.takeReceipts();
    REQUIRE_EQ(later.size(), 1u);
    CHECK(!later[0].killed);
    CHECK_EQ(later[0].applied, 0.0);
    CHECK(sink.takeReceipts().empty());
}

TEST_CASE("RPG action damage missing and destroyed providers fail without health mutation") {
    Fixture f;
    auto    registry = action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());
    auto missing = registry.value().dispatch(f.hit(), f.context());
    REQUIRE(!missing.ok());
    {
        rpg::RPGActionDamageSink sink(f.resolver());
        REQUIRE(sink.supports(f.enemyHandle));
    }
    auto removed = registry.value().dispatch(f.hit(), f.context());
    REQUIRE(!removed.ok());
    CHECK_EQ(f.enemy->getCurrent("hp"), 100.0);
}

TEST_CASE("RPG action damage rejects stale source and target generations") {
    Fixture                     f;
    rpg::RPGActionDamageSink    sink(f.resolver());
    action::ActionDamageBinding binding;
    binding.damageType = "Damage.Physical";
    binding.amount     = 10;
    f.hero->release();
    auto staleSource = sink.apply(binding, f.context());
    REQUIRE(!staleSource.ok());
    CHECK_EQ(f.enemy->getCurrent("hp"), 100.0);
    f.enemy->release();
    auto* replacement = rpg::RPGActor::createActor();
    replacement->setBaseAttribute("hp", 50);
    replacement->setCurrent("hp", 50);
    auto staleTarget = sink.apply(binding, f.context());
    REQUIRE(!staleTarget.ok());
    CHECK(!sink.supports(f.enemyHandle));
    CHECK_EQ(replacement->getCurrent("hp"), 50.0);
    CHECK(sink.takeReceipts().empty());
}

TEST_CASE("RPG action damage preview unsupported motion and invalid magnitude never commit") {
    Fixture                     f;
    rpg::RPGActionDamageSink    sink(f.resolver());
    action::ActionDamageBinding binding;
    binding.damageType = "Damage.Physical";
    binding.amount     = 10;
    auto context       = f.context();
    context.preview    = true;
    auto preview       = sink.apply(binding, context);
    REQUIRE(preview.ok());
    context.preview     = false;
    binding.poiseAmount = 1;
    auto unsupported    = sink.apply(binding, context);
    REQUIRE(!unsupported.ok());
    binding.poiseAmount = 0;
    binding.amount      = std::numeric_limits<double>::quiet_NaN();
    auto invalid        = sink.apply(binding, context);
    REQUIRE(!invalid.ok());
    CHECK_EQ(f.enemy->getCurrent("hp"), 100.0);
    CHECK(sink.takeReceipts().empty());
}
