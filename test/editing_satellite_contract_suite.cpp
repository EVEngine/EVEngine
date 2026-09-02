#include "audio_editing/AudioTarget.h"
#include "audio_editing/AudioEditingProvider.h"
#include "editor/Editor.h"
#include "physics_editing/PhysicsEditingProvider.h"
#include "physics_editing/PhysicsTarget.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <functional>
#include <string>

using namespace eve::editing;

namespace {

/** Shared candidate-publication contract used unchanged by independent satellites. */
template <class PublishingTarget, class MakeOperation, class RejectPublication>
void checkAtomicPublishingContract(PublishingTarget& target, MakeOperation makeOperation,
                                   RejectPublication rejectPublication) {
    const Revision before = target.revision();
    const DomainOperation operation = makeOperation(target);

    rejectPublication(true);
    const auto rejected = target.applyDomainOperation(operation);
    CHECK(!rejected.ok());
    CHECK_EQ(target.revision(), before);

    rejectPublication(false);
    const auto applied = target.applyDomainOperation(operation);
    CHECK(applied.ok());
    CHECK_EQ(target.revision(), before + 1);
}

SelectionSnapshot audioSelection(const eve::audio_editing::AudioSourcePublishingTarget& target) {
    SelectionItem item;
    item.domain = SelectionDomain::Asset;
    item.target = TargetId(target.targetId());
    item.item   = StableId(target.targetId().value());
    item.type   = "audio.source";
    SelectionSnapshot selection;
    selection.channel = "audio";
    selection.items   = {item};
    selection.primary = item;
    return selection;
}

SelectionSnapshot physicsSelection(const eve::physics_editing::PhysicsColliderPublishingTarget& target) {
    SelectionItem item;
    item.domain = SelectionDomain::Scene;
    item.target = TargetId(target.targetId());
    item.item   = StableId("collider");
    item.type   = target.authoringTarget().describe().type;
    SelectionSnapshot selection;
    selection.channel = "scene";
    selection.items   = {item};
    selection.primary = item;
    return selection;
}

class AudioSink final : public eve::audio_editing::IAudioSourceRuntimeSink {
public:
    eve::audio_editing::EditorResult<void> publish(
        const eve::audio_editing::AudioSourceTarget&) override {
        return reject ? eve::editing::failed<void>(
                            Status::Failed, RuleId("test.contract.audio-publication"), "injected failure")
                      : eve::editing::applied<void>();
    }
    bool reject = false;
};

class PhysicsSink final : public eve::physics_editing::IPhysicsColliderRuntimeSink {
public:
    eve::physics_editing::EditorResult<void> publish(
        const eve::physics_editing::PhysicsColliderTarget&) override {
        return reject ? eve::editing::failed<void>(
                            Status::Failed, RuleId("test.contract.physics-publication"), "injected failure")
                      : eve::editing::applied<void>();
    }
    bool reject = false;
};

}  // namespace

TEST_CASE("editing.satellite.audio_and_physics_share_atomic_publication_contract") {
    AudioSink audioSink;
    eve::audio_editing::AudioSourcePublishingTarget audio("audio.contract", &audioSink);
    checkAtomicPublishingContract(
        audio,
        [&](const auto& target) {
            auto operation = target.authoringTarget().makeSet(
                audioSelection(target), PropertyPath("clip.asset"), Value("asset://contract.ogg"),
                PropertySetMode::Absolute);
            REQUIRE(operation.ok());
            return std::move(operation.value());
        },
        [&](bool reject) { audioSink.reject = reject; });

    PhysicsSink physicsSink;
    eve::physics_editing::PhysicsColliderPublishingTarget physics("physics.contract", 3, &physicsSink);
    checkAtomicPublishingContract(
        physics,
        [&](const auto& target) {
            auto operation = target.authoringTarget().makeSet(
                physicsSelection(target), PropertyPath("shape.kind"), Value("sphere"),
                PropertySetMode::Absolute);
            REQUIRE(operation.ok());
            return std::move(operation.value());
        },
        [&](bool reject) { physicsSink.reject = reject; });
}

TEST_CASE("editing.satellite.audio_and_physics_publish_open_factory_providers") {
    eve::editor::Editor host;
    const auto audioHandle = eve::audio_editing::registerEditingProvider(host.extensionProviders());
    const auto physicsHandle = eve::physics_editing::registerEditingProvider(host.extensionProviders());
    REQUIRE(audioHandle.ok());
    REQUIRE(physicsHandle.ok());

    auto audioLease = host.extensionProviders().acquire(audioHandle.value());
    auto physicsLease = host.extensionProviders().acquire(physicsHandle.value());
    REQUIRE(audioLease.ok());
    REQUIRE(physicsLease.ok());
    auto* audioFactory = static_cast<eve::audio_editing::IAudioEditingFactory*>(
        audioLease.value().query(eve::audio_editing::IAudioEditingFactory::capabilityId()));
    auto* physicsFactory = static_cast<eve::physics_editing::IPhysicsEditingFactory*>(
        physicsLease.value().query(eve::physics_editing::IPhysicsEditingFactory::capabilityId()));
    REQUIRE(audioFactory != nullptr);
    REQUIRE(physicsFactory != nullptr);
    const auto audioTarget = audioFactory->createSource("audio.provider-target", nullptr);
    const auto physicsTarget = physicsFactory->createCollider("physics.provider-target", 3, nullptr);
    CHECK(static_cast<bool>(audioTarget));
    CHECK(static_cast<bool>(physicsTarget));
}
