#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/ECS.h"
#include "common/definitions/RuntimeSnapshot.h"
#include "definitions/Definitions.h"
#include "rpg/RPGActor.h"
#include "rpg/Skill.h"
#include "rpg/SkillConditionCodec.h"
#include "rpg/SkillDefinitionRuntime.h"
#include "vehicle/VehicleDefinitionRuntime.h"

#include <cstdint>
#include <string_view>

namespace {

eve::PersistentId testId() {
    const auto parsed = eve::PersistentId::parse("22222222-2222-7222-8222-222222222222");
    return *parsed;
}

eve::SnapshotHashProvider testHash() {
    return [](std::string_view input) -> eve::Result<eve::ContentId> {
        std::uint64_t first = 14695981039346656037ull;
        std::uint64_t second = 1099511628211ull;
        for (const unsigned char byte : input) {
            first ^= byte;
            first *= 1099511628211ull;
            second ^= static_cast<std::uint64_t>(byte + 0x9du);
            second *= 14029467366897019727ull;
        }
        eve::ContentId::Bytes bytes{};
        for (std::size_t index = 0; index < sizeof(first); ++index) {
            bytes[index] = static_cast<std::uint8_t>((first >> (index * 8u)) & 0xffu);
            bytes[8 + index] = static_cast<std::uint8_t>((second >> (index * 8u)) & 0xffu);
        }
        return eve::Result<eve::ContentId>::success(eve::ContentId(bytes));
    };
}

}  // namespace

TEST_CASE("definitionRuntime.skillAdapterUsesCanonicalRegistryAndAtomicSnapshot") {
    eve::rpg::SkillRegistry::clear();
    eve::rpg::SkillDefinition definition;
    definition.id = "fireball";
    definition.cooldown = 8.f;
    definition.castTime = 1.25f;
    definition.targetType = "single";
    definition.tags = {"fire"};
    definition.castCondition = eve::decision::Condition::compare(
        "skill.cooldown", eve::decision::CompareOperator::Equal, eve::Value(0));
    eve::rpg::SkillRegistry::registerSkill(definition);

    auto runtimeResult = eve::rpg::SkillDefinitionRuntime::create(testId(), "fireball");
    REQUIRE(runtimeResult.ok());
    auto runtime = std::move(runtimeResult).takeValue();
    const auto originalGeneration = runtime.identity().definitionGeneration;
    auto typed = runtime.definition();
    REQUIRE(typed.ok());
    CHECK_EQ(typed.value().targetType, std::string("single"));
    CHECK_EQ(static_cast<int>(typed.value().castCondition.kind()),
             static_cast<int>(eve::decision::ConditionKind::Compare));
    runtime.state().cooldownRemaining = 4.f;

    auto snapshot = runtime.snapshot(eve::Revision(3), eve::SimulationTick(12), testHash());
    REQUIRE(snapshot.ok());
    runtime.state().cooldownRemaining = 0.f;
    auto restored = runtime.restore(snapshot.value(), testHash());
    REQUIRE(restored.ok());
    CHECK_EQ(runtime.state().cooldownRemaining, 4.f);

    eve::rpg::SkillDefinition replacement = definition;
    replacement.cooldown = 3.f;
    eve::rpg::SkillRegistry::registerSkill(replacement);
    auto rejectedRestore = runtime.restore(snapshot.value(), testHash());
    CHECK(!rejectedRestore.ok());
    CHECK_EQ(static_cast<int>(rejectedRestore.error()->code()),
             static_cast<int>(eve::DiagnosticCode::StaleHandle));
    CHECK_EQ(runtime.state().cooldownRemaining, 4.f);

    runtime.setActive(true);
    auto rejectedReload = runtime.reload(eve::definition::ReloadPolicy::RejectWhileActive);
    CHECK(!rejectedReload.ok());
    CHECK_EQ(runtime.identity().definitionGeneration, originalGeneration);
    runtime.setActive(false);
    auto reloaded = runtime.reload(eve::definition::ReloadPolicy::RebuildInstance);
    REQUIRE(reloaded.ok());
    CHECK_EQ(runtime.state().cooldownRemaining, 3.f);

    ecs::Table world;
    ecs::ScopedTable guard(world);
    auto* actor = eve::rpg::RPGActor::createActor();
    REQUIRE(actor != nullptr);
    auto projected = runtime.applyTo(actor);
    REQUIRE(projected.ok());
    CHECK(actor->knowsSkill("fireball"));
    CHECK_EQ(actor->getSkillCooldown("fireball"), 3.f);
    actor->release();
}

TEST_CASE("definitionRuntime.vehicleAdapterProjectsEntityAndRejectsStaleSnapshot") {
    eve::definitions::DefinitionRegistry registry;
    auto inserted = registry.insert(
        "vehicle", "scout", 1,
        R"({"category":"vehicle","mobility":"kinematic","maxSpeed":80,"radius":6,"maxHealth":100,"tags":["recon"]})");
    REQUIRE(inserted.ok());
    auto reference = eve::DefinitionRef::parse("vehicle:scout");
    REQUIRE(reference.ok());
    auto runtimeResult = eve::vehicle::VehicleDefinitionRuntime::create(
        registry, std::move(reference).takeValue(), testId());
    REQUIRE(runtimeResult.ok());
    auto runtime = std::move(runtimeResult).takeValue();
    runtime.state().x = -12.f;
    runtime.state().y = 4.f;
    runtime.state().health = 75.f;
    runtime.state().faction = "blue";

    ecs::Table world;
    ecs::ScopedTable guard(world);
    auto* entity = eve::vehicle::VehicleEntity::createVehicle();
    REQUIRE(entity != nullptr);
    auto projected = runtime.applyTo(entity);
    REQUIRE(projected.ok());
    CHECK_EQ(entity->motion()->x, -12.f);
    CHECK_EQ(entity->health()->hp, 75.f);
    CHECK_EQ(entity->identity()->faction, std::string("blue"));
    CHECK_EQ(entity->definitionBinding()->identity.definitionGeneration,
             runtime.identity().definitionGeneration);
    CHECK_EQ(entity->definition()->def->category, std::string("vehicle"));

    auto snapshot = runtime.snapshot(eve::Revision(4), eve::SimulationTick(20), testHash());
    REQUIRE(snapshot.ok());
    runtime.state().x = 99.f;
    auto restored = runtime.restore(snapshot.value(), testHash());
    REQUIRE(restored.ok());
    CHECK_EQ(runtime.state().x, -12.f);

    auto replaced = registry.replace(
        "vehicle", "scout", 2,
        R"({"category":"vehicle","mobility":"kinematic","maxSpeed":90,"radius":6,"maxHealth":120})");
    REQUIRE(replaced.ok());
    auto stale = runtime.restore(snapshot.value(), testHash());
    CHECK(!stale.ok());
    CHECK_EQ(static_cast<int>(stale.error()->code()), static_cast<int>(eve::DiagnosticCode::StaleHandle));
    CHECK_EQ(runtime.state().x, -12.f);
    entity->release();
}
