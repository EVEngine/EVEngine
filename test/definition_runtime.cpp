#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "card/CardDefinitionRuntime.h"
#include "common/definitions/DefinitionRuntime.h"
#include "definitions/Definitions.h"
#include "effects/EffectDefinitionRuntime.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace {

struct TypedState {
    int         value = 0;
    std::string label;
};

eve::PersistentId testId() {
    const auto parsed = eve::PersistentId::parse("11111111-1111-7111-8111-111111111111");
    return *parsed;
}

eve::DefinitionRef testReference(const char* text) {
    auto parsed = eve::DefinitionRef::parse(text);
    if (!parsed.ok()) return {};
    return std::move(parsed).takeValue();
}

eve::SnapshotHashProvider testHash() {
    return [](std::string_view input) -> eve::Result<eve::ContentId> {
        eve::ContentId::Bytes bytes{};
        std::uint8_t          value = 29;
        for (std::size_t index = 0; index < bytes.size(); ++index) {
            for (const unsigned char byte : input) value = static_cast<std::uint8_t>(value * 33u + byte);
            bytes[index] = static_cast<std::uint8_t>(value + index);
        }
        return eve::Result<eve::ContentId>::success(eve::ContentId(bytes));
    };
}

}  // namespace

TEST_CASE("definitionRuntime.policiesAndGenerationQualifiedStaleChecks") {
    const auto reference = testReference("test:unit");
    auto       created   = eve::definition::RuntimeInstance<TypedState>::create(testId(), reference, eve::Generation(1),
                                                                                TypedState{7, "original"});
    REQUIRE(created.ok());
    auto runtime = std::move(created).takeValue();

    auto wrongGeneration = runtime.checkDefinition(eve::definition::DefinitionHandle{reference, eve::Generation(2)});
    CHECK(!wrongGeneration.ok());
    CHECK(static_cast<int>(wrongGeneration.error()->code()) == static_cast<int>(eve::DiagnosticCode::StaleHandle));

    auto kept = runtime.reload(eve::definition::DefinitionHandle{reference, eve::Generation(2)},
                               eve::definition::ReloadPolicy::KeepInstanceValues, TypedState{99, "new defaults"});
    REQUIRE(kept.ok());
    CHECK(static_cast<int>(kept.value().disposition) == static_cast<int>(eve::definition::ReloadDisposition::Kept));
    CHECK_EQ(runtime.state().value, 7);
    CHECK_EQ(runtime.identity().definitionGeneration.value(), uint64_t{2});

    auto reapplied = runtime.reload(eve::definition::DefinitionHandle{reference, eve::Generation(3)},
                                    eve::definition::ReloadPolicy::ReapplyDefaults, TypedState{42, "defaults"});
    REQUIRE(reapplied.ok());
    CHECK(static_cast<int>(reapplied.value().disposition) ==
          static_cast<int>(eve::definition::ReloadDisposition::DefaultsReapplied));
    CHECK_EQ(runtime.state().value, 42);
    CHECK_EQ(runtime.state().label, std::string("defaults"));

    auto rebuilt =
        runtime.reload(eve::definition::DefinitionHandle{reference, eve::Generation(4)},
                       eve::definition::ReloadPolicy::RebuildInstance, TypedState{100, "ignored"},
                       [](const TypedState& oldState, const eve::definition::InstanceIdentity& identity,
                          const eve::definition::DefinitionHandle& handle) -> eve::Result<TypedState> {
                           if (!identity.isValid() || !handle.isValid())
                               return eve::Result<TypedState>::failure(eve::Diagnostic::error(
                                   eve::DiagnosticCode::InvalidArgument, "test rebuild received invalid identity"));
                           return eve::Result<TypedState>::success(TypedState{oldState.value + 1, "rebuilt"});
                       });
    REQUIRE(rebuilt.ok());
    CHECK(static_cast<int>(rebuilt.value().disposition) ==
          static_cast<int>(eve::definition::ReloadDisposition::Rebuilt));
    CHECK_EQ(runtime.state().value, 43);

    auto failedRebuild = runtime.reload(eve::definition::DefinitionHandle{reference, eve::Generation(5)},
                                        eve::definition::ReloadPolicy::RebuildInstance, TypedState{0, "must not apply"},
                                        [](const TypedState&, const eve::definition::InstanceIdentity&,
                                           const eve::definition::DefinitionHandle&) -> eve::Result<TypedState> {
                                            return eve::Result<TypedState>::failure(eve::Diagnostic::error(
                                                eve::DiagnosticCode::Failed, "injected rebuild failure"));
                                        });
    CHECK(!failedRebuild.ok());
    CHECK_EQ(runtime.identity().definitionGeneration.value(), uint64_t{4});
    CHECK_EQ(runtime.state().value, 43);

    auto rejected = runtime.reload(eve::definition::DefinitionHandle{reference, eve::Generation(5)},
                                   eve::definition::ReloadPolicy::RejectWhileActive, TypedState{0, "must not apply"});
    CHECK(!rejected.ok());
    CHECK(static_cast<int>(rejected.error()->code()) == static_cast<int>(eve::DiagnosticCode::Conflict));
    CHECK_EQ(runtime.identity().definitionGeneration.value(), uint64_t{4});
    CHECK_EQ(runtime.state().value, 43);

    runtime.setActive(false);
    auto inactiveReject = runtime.reload(eve::definition::DefinitionHandle{reference, eve::Generation(5)},
                                         eve::definition::ReloadPolicy::RejectWhileActive, TypedState{0, "still kept"});
    REQUIRE(inactiveReject.ok());
    CHECK(static_cast<int>(inactiveReject.value().disposition) ==
          static_cast<int>(eve::definition::ReloadDisposition::Kept));
    CHECK_EQ(runtime.identity().definitionGeneration.value(), uint64_t{5});
    CHECK_EQ(runtime.state().value, 43);
}

TEST_CASE("definitionRuntime.cardAdapterProjectsTypedIdentityAndReloads") {
    eve::definitions::DefinitionRegistry registry;
    auto                                 inserted = registry.insert(
        "card", "scout", 1, R"({"name":"Scout","kind":"creature","cost":2,"attack":3,"health":5,"tags":["unit"]})");
    REQUIRE(inserted.ok());

    auto runtimeResult = eve::card::CardDefinitionRuntime::create(registry, testReference("card:scout"), testId(),
                                                                  eve::definition::ReloadPolicy::RebuildInstance);
    REQUIRE(runtimeResult.ok());
    auto runtime = std::move(runtimeResult).takeValue();
    CHECK_EQ(runtime.state().attack, 3);
    CHECK_EQ(runtime.identity().definitionGeneration.value(), uint64_t{1});
    runtime.state().currentHealth = 2;

    auto snapshot = runtime.snapshot(eve::Revision(1), eve::SimulationTick(2), testHash());
    REQUIRE(snapshot.ok());
    runtime.state().currentHealth = 1;
    auto restored                 = runtime.restore(snapshot.value(), testHash());
    REQUIRE(restored.ok());
    CHECK_EQ(runtime.state().currentHealth, 2);

    auto card = eve::card::CardData::createCard();
    REQUIRE(card != nullptr);
    auto projected = runtime.applyTo(card);
    REQUIRE(projected.ok());
    CHECK_EQ(card->stats()->health, 2);
    CHECK_EQ(card->definitionBinding()->identity.instanceId, testId());
    CHECK_EQ(card->definitionBinding()->identity.definitionGeneration.value(), uint64_t{1});
    card->release();

    auto replaced = registry.replace("card", "scout", 2,
                                     R"({"name":"Veteran Scout","kind":"creature","cost":3,"attack":6,"health":4})");
    REQUIRE(replaced.ok());
    auto staleSnapshot = runtime.restore(snapshot.value(), testHash());
    CHECK(!staleSnapshot.ok());
    CHECK_EQ(runtime.state().currentHealth, 2);
    auto reloaded = runtime.reload(eve::definition::ReloadPolicy::RebuildInstance);
    REQUIRE(reloaded.ok());
    CHECK(static_cast<int>(reloaded.value().disposition) ==
          static_cast<int>(eve::definition::ReloadDisposition::Rebuilt));
    CHECK_EQ(runtime.state().attack, 6);
    CHECK_EQ(runtime.state().currentHealth, 2);
    CHECK_EQ(runtime.identity().definitionGeneration.value(), uint64_t{2});
}

TEST_CASE("definitionRuntime.effectAdapterKeepsTypedPolicyAndRejectsActiveReload") {
    eve::definitions::DefinitionRegistry registry;
    auto                                 inserted = registry.insert(
        "effect", "burn", 1,
        R"({"stackKey":"burn","priority":4,"duration":8.0,"magnitude":3.5,"stackCount":1,"maxStacks":3,"stackMode":"reuse","stackCountPolicy":"increment","payload":{"damageType":"fire"}})");
    REQUIRE(inserted.ok());

    auto runtimeResult = eve::effects::EffectDefinitionRuntime::create(
        registry, testReference("effect:burn"), "actor:target", "spell:ember", testId(),
        eve::definition::ReloadPolicy::RejectWhileActive);
    REQUIRE(runtimeResult.ok());
    auto runtime = std::move(runtimeResult).takeValue();
    CHECK(static_cast<int>(runtime.state().policy.stackMode) == static_cast<int>(eve::effects::StackMode::Reuse));
    CHECK(static_cast<int>(runtime.state().policy.stackCount) ==
          static_cast<int>(eve::effects::StackCountPolicy::Increment));

    eve::effects::EffectInstance instance;
    auto                         projected = runtime.applyTo(&instance);
    REQUIRE(projected.ok());
    CHECK(static_cast<bool>(instance.definitionIdentity == runtime.identity()));
    CHECK(static_cast<int>(instance.policy.stackMode) == static_cast<int>(eve::effects::StackMode::Reuse));
    CHECK_EQ(instance.payload.getJson("damageType"), std::string("\"fire\""));

    auto replaced = registry.replace(
        "effect", "burn", 2,
        R"({"stackKey":"burn","priority":5,"duration":10.0,"magnitude":4.0,"stackCount":1,"maxStacks":3})");
    REQUIRE(replaced.ok());
    auto rejected = runtime.reload(eve::definition::ReloadPolicy::RejectWhileActive);
    CHECK(!rejected.ok());
    CHECK(static_cast<int>(rejected.error()->code()) == static_cast<int>(eve::DiagnosticCode::Conflict));
    CHECK_EQ(runtime.identity().definitionGeneration.value(), uint64_t{1});
}
