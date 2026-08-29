#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Identity.h"
#include "common/RuntimeHandle.h"
#include "effects/EffectContainer.h"
#include "transaction/Transaction.h"

#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <type_traits>

namespace {

template <class Id>
Id parsedId(const char* text) {
    const auto parsed = Id::parse(text);
    return parsed ? *parsed : Id::nil();
}

eve::UuidEntropySource deterministicEntropy() {
    auto cursor = std::make_shared<std::uint8_t>(1);
    return [cursor](std::span<std::uint8_t> bytes) {
        for (auto& byte : bytes) byte = (*cursor)++;
        return true;
    };
}

}  // namespace

static_assert(!std::is_convertible_v<eve::AssetGuid, eve::DocumentId>);
static_assert(!std::is_convertible_v<eve::EventId, eve::TransactionId>);
static_assert(!std::is_convertible_v<eve::TransactionId, eve::OperationId>);
static_assert(!std::is_convertible_v<eve::SceneObjectId, eve::PersistentId>);

TEST_CASE("identity.audit.domainIdsShareOneUuidImplementation") {
    constexpr const char* text  = "01020304-0506-0708-090a-0b0c0d0e0f10";
    const auto            asset = parsedId<eve::AssetGuid>(text);
    REQUIRE(!asset.isNil());
    CHECK_EQ(asset.format(), text);
    CHECK_EQ(eve::DocumentId::fromUuid(asset).format(), text);
    CHECK_EQ(eve::SceneObjectId::fromUuid(asset).format(), text);
    CHECK_EQ(eve::ArtifactId::fromUuid(asset).format(), text);
    CHECK_EQ(eve::EventId::fromUuid(asset).format(), text);
    CHECK_EQ(eve::TransactionId::fromUuid(asset).format(), text);
    CHECK_EQ(eve::OperationId::fromUuid(asset).format(), text);
    CHECK_EQ(eve::EffectId::fromUuid(asset).format(), text);

    CHECK(!eve::AssetGuid::parse("asset-01020304-0506-0708-090a-0b0c0d0e0f10").has_value());
    CHECK(!eve::AssetGuid::parse("{01020304-0506-0708-090a-0b0c0d0e0f10}").has_value());
}

TEST_CASE("identity.audit.logicalNamespaceIsCanonicalAndNamesRemainCaseSensitive") {
    REQUIRE(eve::LogicalId::parse("rpg:Skill.Fire").has_value());
    CHECK(!eve::LogicalId::parse("RPG:Skill.Fire").has_value());
    CHECK(!eve::LogicalId::parse("rpg_:Skill.Fire").has_value());
    CHECK(!eve::LogicalId::parse("rpg:skill/fire").has_value());
    CHECK(eve::LogicalId::fromParts("game_event", "Damage.Applied").has_value());
}

TEST_CASE("identity.audit.canonicalTransactionRestoresWithoutStringAllocators") {
    using namespace std::chrono_literals;
    const auto clock = [] { return std::chrono::system_clock::time_point{std::chrono::milliseconds{0x010203040506}}; };
    const auto entropy = deterministicEntropy();

    eve::transaction::Ledger source(eve::PersistentId::nil(), entropy, clock);
    auto                     created = source.create("combat", "command");
    REQUIRE(created.ok());
    auto* plan = std::move(created).takeValue();
    REQUIRE(plan != nullptr);
    const auto transactionId = plan->identity();
    CHECK(!transactionId.isNil());
    CHECK_EQ(plan->id(), transactionId.format());

    auto staged = plan->stage("debit", "treasury", "{\"amount\":10}");
    REQUIRE(staged.ok());
    const auto operationId = std::move(staged).takeValue();
    CHECK(!operationId.isNil());
    const auto* operation = plan->findOperation(operationId);
    REQUIRE(operation != nullptr);
    CHECK_EQ(operation->id, operationId.format());
    CHECK(plan->markValid(operationId).ok());
    CHECK(plan->validate().ok());
    CHECK(plan->commit().ok());

    const auto snapshot = source.snapshotJson();
    CHECK(snapshot.find("transaction-0000000000000001") == std::string::npos);
    CHECK(snapshot.find("operation-0000000000000001") == std::string::npos);

    eve::transaction::Ledger restored(eve::PersistentId::nil(), deterministicEntropy(), clock);
    auto                     restoredResult = restored.restore(snapshot);
    REQUIRE(restoredResult.ok());
    auto* restoredPlan = restored.find(transactionId);
    REQUIRE(restoredPlan != nullptr);
    CHECK_EQ(restoredPlan->identity(), transactionId);
    CHECK(restoredPlan->findOperation(operationId) != nullptr);

    const auto before          = restored.snapshotJson();
    auto       failedCanonical = restored.restore("{\"version\":1,\"nextTransaction\":\"2\",\"plans\":[");
    CHECK(!failedCanonical.ok());
    CHECK_EQ(restored.snapshotJson(), before);
    CHECK_EQ(restored.snapshotJson(), before);
}

TEST_CASE("identity.audit.runtimeHandleGenerationMakesStaleReuseObservable") {
    struct RegistryTag {};
    using Handle = eve::RuntimeHandle<RegistryTag>;

    constexpr Handle oldHandle(4u, 1u);
    const auto       next = oldHandle.nextGeneration();
    REQUIRE(next.has_value());
    CHECK(next.value() != oldHandle);
    CHECK_EQ(next->index(), oldHandle.index());
    CHECK_EQ(next->generation(), 2u);
    CHECK(!Handle(4u, std::numeric_limits<Handle::generation_type>::max()).nextGeneration().has_value());
    CHECK_EQ(Handle::fromPacked(oldHandle.packed()), oldHandle);
}

TEST_CASE("identity.audit.canonicalEffectUsesUuidAndLegacyFacadeProjectsLocally") {
    using namespace std::chrono_literals;
    const auto clock = [] { return std::chrono::system_clock::time_point{std::chrono::milliseconds{0x010203040506}}; };
    eve::effects::EffectDefinition definition;
    definition.id               = "combat:burn";
    definition.policy.stackMode = eve::effects::StackMode::NewInstance;

    eve::effects::EffectContainer canonical(deterministicEntropy(), clock);
    auto                          applied = canonical.applyCanonical(definition, "actor:1", "spell:1");
    REQUIRE(applied.ok());
    const auto effectId = std::move(applied).takeValue();
    CHECK(!effectId.isNil());
    const auto* effect = canonical.find(effectId);
    REQUIRE(effect != nullptr);
    CHECK_EQ(effect->identity, effectId);
    REQUIRE(canonical.eventAt(0) != nullptr);
    CHECK_EQ(canonical.eventAt(0)->effectIdentity, effectId);
    CHECK(canonical.remove(effectId).ok());

    eve::effects::EffectContainer legacy;
    auto                          legacyResult = legacy.apply(definition, "actor:2", "spell:1");
    REQUIRE(legacyResult.ok());
    CHECK(legacyResult.value().starts_with("effect-"));
    REQUIRE(legacy.find(legacyResult.value()) != nullptr);
    CHECK(legacy.find(legacyResult.value())->identity.isNil());
}
