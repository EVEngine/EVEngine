#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "rpg/Effect.h"
#include "rpg/RPGActor.h"
#include "rpg/StatusSystem.h"

#include <cmath>
#include <string>
#include <utility>
#include <vector>

using namespace eve::rpg;

namespace {

bool approx(float left, float right, float epsilon = 1.0e-4f) { return std::abs(left - right) <= epsilon; }

void drainEvents() {
    std::vector<StatusChangeEvent> changes;
    std::vector<StatusTickEvent>   ticks;
    StatusSystem::pollChanges(changes);
    StatusSystem::pollTicks(ticks);
}

}  // namespace

TEST_CASE("rpg.status.adapterUsesCanonicalEffectContainerAndStableLegacyProjection") {
    drainEvents();
    EffectRegistry::clear();

    EffectDefinition definition;
    definition.id             = "adapter.canonical";
    definition.durationPolicy = "infinite";
    definition.stackPolicy    = "stack";
    definition.maxStacks      = 2;
    definition.modifiers.push_back({"armor", "add", 4.0, 0});
    EffectRegistry::registerEffect(definition);

    RPGActor *actor = RPGActor::createActor();
    actor->setBaseAttribute("armor", 10.0);
    const int legacyId = actor->applyEffect(definition.id, "source");
    REQUIRE(legacyId > 0);

    REQUIRE(actor->statuses()->container.effectCount() == 1);
    const auto *canonical = actor->statuses()->container.effectAt(0);
    REQUIRE(canonical != nullptr);
    // effectAt() is an immediate borrow. Re-applying the status stages and
    // publishes a replacement container, so retain the identity value rather
    // than the instance address across that structural mutation.
    const std::string canonicalId = canonical->id;
    CHECK(!canonicalId.empty());
    CHECK_EQ(actor->statuses()->effectByLegacyId.at(legacyId), canonicalId);
    CHECK_EQ(actor->statuses()->legacyIdByEffect.at(canonicalId), legacyId);
    CHECK_EQ(actor->getFinalAttribute("armor"), 14.0);

    const int reapplied = actor->applyEffect(definition.id, "source");
    CHECK_EQ(reapplied, legacyId);
    CHECK_EQ(actor->statuses()->container.effectCount(), 1);
    const auto *reappliedEffect = actor->statuses()->container.find(canonicalId);
    REQUIRE(reappliedEffect != nullptr);
    CHECK_EQ(reappliedEffect->stackCount, 2u);
    CHECK_EQ(actor->getFinalAttribute("armor"), 18.0);

    actor->release();
    EffectRegistry::clear();
}

TEST_CASE("rpg.status.adapterPreservesDurationStackAndModifierParity") {
    drainEvents();
    EffectRegistry::clear();

    EffectDefinition definition;
    definition.id             = "adapter.parity";
    definition.durationPolicy = "duration";
    definition.duration       = 2.0f;
    definition.stackPolicy    = "stack";
    definition.maxStacks      = 2;
    definition.modifiers.push_back({"attack", "add", 3.0, 0});
    EffectRegistry::registerEffect(definition);

    RPGActor *actor = RPGActor::createActor();
    actor->setBaseAttribute("attack", 10.0);
    const int id = actor->applyEffect(definition.id);
    REQUIRE(id > 0);
    CHECK_EQ(actor->getFinalAttribute("attack"), 13.0);

    auto advanced = StatusSystem::update(0.5);
    REQUIRE(advanced.ok());
    const auto summary = std::move(advanced).takeValue();
    CHECK_EQ(summary.expired, 0u);
    CHECK(approx(actor->getStatusRemaining(0), 1.5f));

    CHECK_EQ(actor->applyEffect(definition.id), id);
    CHECK_EQ(actor->getStatusStacks(0), 2);
    CHECK(approx(actor->getStatusRemaining(0), 2.0f));
    CHECK_EQ(actor->getFinalAttribute("attack"), 16.0);

    CHECK_EQ(actor->applyEffect(definition.id), id);
    CHECK_EQ(actor->getStatusStacks(0), 2);
    CHECK_EQ(actor->getFinalAttribute("attack"), 16.0);

    auto expired = StatusSystem::update(2.0);
    REQUIRE(expired.ok());
    CHECK_EQ(std::move(expired).takeValue().expired, 1u);
    CHECK_EQ(actor->getStatusCount(), 0);
    CHECK_EQ(actor->getFinalAttribute("attack"), 10.0);

    actor->release();
    EffectRegistry::clear();
}

TEST_CASE("rpg.status.adapterPeriodicExecutorProducesTicksAndExpiresWithoutPostExpiryTicks") {
    drainEvents();
    EffectRegistry::clear();

    EffectDefinition definition;
    definition.id             = "adapter.periodic";
    definition.durationPolicy = "duration";
    definition.duration       = 2.5f;
    definition.period         = 1.0f;
    definition.stackPolicy    = "none";
    EffectRegistry::registerEffect(definition);

    RPGActor *actor = RPGActor::createActor();
    const int id    = actor->applyEffect(definition.id, "caster");
    REQUIRE(id > 0);

    auto first = StatusSystem::update(1.0);
    REQUIRE(first.ok());
    CHECK_EQ(std::move(first).takeValue().ticks, 1u);
    std::vector<StatusTickEvent> ticks;
    StatusSystem::pollTicks(ticks);
    REQUIRE(ticks.size() == 1);
    CHECK_EQ(ticks[0].instanceId, id);
    CHECK_EQ(ticks[0].effectId, definition.id);

    auto second = StatusSystem::update(1.0);
    REQUIRE(second.ok());
    CHECK_EQ(std::move(second).takeValue().ticks, 1u);
    ticks.clear();
    StatusSystem::pollTicks(ticks);
    CHECK_EQ(ticks.size(), 1u);

    auto finalStep = StatusSystem::update(0.6);
    REQUIRE(finalStep.ok());
    const auto finalSummary = std::move(finalStep).takeValue();
    CHECK_EQ(finalSummary.expired, 1u);
    CHECK_EQ(actor->getStatusCount(), 0);
    ticks.clear();
    StatusSystem::pollTicks(ticks);
    CHECK_EQ(ticks.size(), 0u);

    actor->release();
    EffectRegistry::clear();
}

TEST_CASE("rpg.status.customStackPolicyUsesCandidateAndRollsBackOnFailure") {
    drainEvents();
    EffectRegistry::clear();

    EffectDefinition definition;
    definition.id             = "adapter.custom.rollback";
    definition.durationPolicy = "duration";
    definition.duration       = 4.0f;
    definition.stackPolicy    = "candidatePolicy";
    definition.modifiers.push_back({"attack", "add", 2.0, 0});
    EffectRegistry::registerEffect(definition);

    StatusSystem::registerStackPolicy(
        "candidatePolicy", [](RPGActor *, StatusInstance &candidate, const EffectDefinition &, const std::string &) {
            candidate.stacks += 1;
            candidate.remaining += 10.0f;
            return -1;
        });

    RPGActor *actor = RPGActor::createActor();
    actor->setBaseAttribute("attack", 5.0);
    const int id = actor->applyEffect(definition.id);
    REQUIRE(id > 0);
    CHECK_EQ(actor->getFinalAttribute("attack"), 7.0);
    CHECK(approx(actor->getStatusRemaining(0), 4.0f));
    const auto *canonical = actor->statuses()->container.effectAt(0);
    REQUIRE(canonical != nullptr);
    const int eventsBefore = actor->statuses()->container.eventCount();

    CHECK_EQ(actor->applyEffect(definition.id), -1);
    CHECK_EQ(actor->getStatusStacks(0), 1);
    CHECK(approx(actor->getStatusRemaining(0), 4.0f));
    CHECK_EQ(actor->getFinalAttribute("attack"), 7.0);
    CHECK_EQ(actor->statuses()->container.eventCount(), eventsBefore);
    CHECK_EQ(actor->statuses()->container.effectAt(0)->id, canonical->id);

    StatusSystem::unregisterStackPolicy("candidatePolicy");
    actor->release();
    EffectRegistry::clear();
}

TEST_CASE("rpg.status.adapterCopyIsIndependentAndLegacyIdsRemainStable") {
    drainEvents();
    EffectRegistry::clear();

    EffectDefinition definition;
    definition.id             = "adapter.copy";
    definition.durationPolicy = "infinite";
    definition.stackPolicy    = "stack";
    definition.maxStacks      = 3;
    definition.modifiers.push_back({"armor", "add", 1.0, 0});
    EffectRegistry::registerEffect(definition);

    RPGActor *original = RPGActor::createActor();
    original->setBaseAttribute("armor", 1.0);
    const int originalId = original->applyEffect(definition.id);
    REQUIRE(originalId > 0);
    original->setStatusProp(originalId, "owner", "original");

    RPGActor::Statuses copiedStatuses   = *original->statuses();
    const int          copiedEventCount = copiedStatuses.container.eventCount();
    auto               firstClone       = copiedStatuses.container.clone();
    auto               secondClone      = copiedStatuses.container.clone();
    CHECK_EQ(firstClone.eventCount(), copiedEventCount);
    CHECK_EQ(secondClone.eventCount(), copiedEventCount);
    firstClone.clearEvents();
    CHECK_EQ(firstClone.eventCount(), 0);
    CHECK_EQ(secondClone.eventCount(), copiedEventCount);

    ::eve::effects::EffectDefinition stagingDefinition;
    stagingDefinition.id       = "adapter.copy.staging";
    stagingDefinition.stackKey = stagingDefinition.id;
    auto firstStaged           = firstClone.apply(stagingDefinition, "actor", "staging");
    auto secondStaged          = secondClone.apply(stagingDefinition, "actor", "staging");
    REQUIRE(firstStaged.ok());
    REQUIRE(secondStaged.ok());
    const std::string firstStagedId = std::move(firstStaged).takeValue();
    CHECK_EQ(std::move(secondStaged).takeValue(), firstStagedId);
    REQUIRE(firstClone.eventAt(0) != nullptr);
    REQUIRE(secondClone.eventAt(copiedEventCount) != nullptr);
    CHECK_EQ(firstClone.eventAt(0)->sequence, 2u);
    CHECK_EQ(secondClone.eventAt(copiedEventCount)->sequence, 2u);
    CHECK(copiedStatuses.container.find(firstStagedId) == nullptr);

    auto      copiedAttributes         = original->attributes()->values;
    RPGActor *copy                     = RPGActor::createActor();
    copy->statuses()->container        = copiedStatuses.container.clone();
    copy->statuses()->metadata         = copiedStatuses.metadata;
    copy->statuses()->legacyIdByEffect = copiedStatuses.legacyIdByEffect;
    copy->statuses()->effectByLegacyId = copiedStatuses.effectByLegacyId;
    copy->statuses()->nextInstanceId   = copiedStatuses.nextInstanceId;
    copy->attributes()->values         = std::move(copiedAttributes);

    CHECK_EQ(copy->getStatusCount(), 1);
    CHECK_EQ(copy->getStatusInstanceId(0), originalId);
    CHECK_EQ(copy->getStatusProp(originalId, "owner"), "original");
    CHECK_EQ(copy->getFinalAttribute("armor"), 2.0);

    CHECK_EQ(original->applyEffect(definition.id), originalId);
    CHECK_EQ(original->getStatusStacks(0), 2);
    CHECK_EQ(original->getFinalAttribute("armor"), 3.0);
    CHECK_EQ(copy->getStatusStacks(0), 1);
    CHECK_EQ(copy->getFinalAttribute("armor"), 2.0);

    CHECK(original->removeStatus(originalId));
    const int nextId = original->applyEffect(definition.id);
    CHECK(nextId > originalId);
    CHECK_EQ(copy->getStatusCount(), 1);
    CHECK_EQ(copy->getStatusInstanceId(0), originalId);

    original->release();
    copy->release();
    EffectRegistry::clear();
}

TEST_CASE("rpg.status.adapterRejectsInvalidModifierWithoutPartialState") {
    drainEvents();
    EffectRegistry::clear();

    EffectDefinition definition;
    definition.id             = "adapter.invalid.modifier";
    definition.durationPolicy = "infinite";
    definition.modifiers.push_back({"attack", "not-a-policy", 5.0, 0});
    EffectRegistry::registerEffect(definition);

    RPGActor *actor = RPGActor::createActor();
    actor->setBaseAttribute("attack", 10.0);
    auto result = StatusSystem::apply(actor, definition.id);
    CHECK(!result.ok());
    CHECK_EQ(result.code(), eve::StatusCode::Rejected);
    CHECK_EQ(actor->getStatusCount(), 0);
    CHECK_EQ(actor->getFinalAttribute("attack"), 10.0);
    CHECK_EQ(actor->statuses()->container.eventCount(), 0);

    actor->release();
    EffectRegistry::clear();
}
