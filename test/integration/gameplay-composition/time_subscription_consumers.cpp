#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "animation/AnimClipRegistry.h"
#include "animation/AnimTrail.h"
#include "animation/Animation.h"
#include "animation/Tween.h"
#include "common/Time.h"
#include "effects/EffectContainer.h"
#include "particles/ParticleEmitter.h"
#include "particles/ParticleRuntime.h"
#include "particles/ParticleSystem.h"
#include "particles/Particles.h"
#include "platform_event/PlatformEvent.h"

#include <cmath>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

class TestEvent final : public eve::platform_event::PlatformEvent {
public:
    void                          pump() override {}
    eve::platform_event::Message* wait() override { return nullptr; }
};

}  // namespace

TEST_CASE("time.checkedConsumers.useSchedulerTickWithoutPrivateRate") {
    auto* particles = eve::particles::Particles::create();
    auto* emitter   = particles->newEmitter(4);
    emitter->setRandomSeed(7);
    emitter->setParticleLifetime(10.f, 10.f);
    emitter->setDirection(0.f);
    emitter->setSpeed(10.f, 10.f);
    emitter->setPlaybackSpeed(0.f);
    emitter->setFixedTimeStep(0.5f, 1);
    emitter->emit(1);

    auto advanced = emitter->advance({eve::SimulationTick(1), eve::Duration::fromNanoseconds(100000000)});
    REQUIRE(advanced.ok());
    CHECK(std::abs(emitter->sim()->particles[0].x - 1.f) < 1e-5f);
    CHECK(emitter->currentSimulationTick() == eve::SimulationTick(1));

    auto duplicate = emitter->advance({eve::SimulationTick(1), eve::Duration::fromNanoseconds(100000000)});
    CHECK(!duplicate);
    CHECK(static_cast<int>(duplicate.code()) == static_cast<int>(eve::StatusCode::Conflict));
}

TEST_CASE("time.checkedConsumers.effectsAndAnimationRecordTick") {
    eve::effects::EffectContainer  effects;
    eve::effects::EffectDefinition definition;
    definition.id       = "temporary";
    definition.duration = 1.0;
    auto applied        = effects.apply(definition, "subject");
    REQUIRE(applied.ok());
    const std::string id = std::move(applied).takeValue();
    REQUIRE(effects.find(id) != nullptr);

    auto effectStep = effects.advance({eve::SimulationTick(3), eve::Duration::fromNanoseconds(250000000)});
    REQUIRE(effectStep.ok());
    CHECK(effectStep.value().tick == eve::SimulationTick(3));
    CHECK(std::abs(effects.find(id)->remaining - 0.75) < 1e-9);

    auto                                   animation = eve::animation::Animation::create();
    std::unique_ptr<eve::animation::Tween> tween(animation->newTween(1.f));
    tween->setFrom("x", 0.f);
    tween->setTo("x", 10.f);
    tween->start();
    auto animationStep = animation->advance({eve::SimulationTick(3), eve::Duration::fromNanoseconds(500000000)});
    REQUIRE(animationStep.ok());
    CHECK(std::abs(tween->get("x") - 5.f) < 1e-4f);
    CHECK(animation->currentTick() == eve::SimulationTick(3));

    (void)effectStep.value();
}

TEST_CASE("time.checkedConsumers.animationChildrenUseInjectedStep") {
    eve::animation::Tween tween(1.f);
    tween.setFrom("x", 0.f);
    tween.setTo("x", 10.f);
    tween.start();
    auto first = tween.advance({eve::SimulationTick(4), eve::Duration::fromNanoseconds(250000000)});
    REQUIRE(first.ok());
    CHECK(std::abs(tween.get("x") - 2.5f) < 1e-4f);
    CHECK(tween.currentTick() == eve::SimulationTick(4));

    eve::animation::AnimTrail trail;
    trail.addPoint(1.f, 2.f);
    auto trailStep = trail.advance({eve::SimulationTick(1), eve::Duration::fromNanoseconds(100000000)});
    REQUIRE(trailStep.ok());
    CHECK(std::abs(trail.getPointAge(0) - 0.1f) < 1e-5f);
    auto duplicate = trail.advance({eve::SimulationTick(1), eve::Duration::zero()});
    CHECK(!duplicate.ok());

    auto*                                  animation = eve::animation::Animation::create();
    std::unique_ptr<eve::animation::Tween> hosted(animation->newTween(1.f));
    hosted->setFrom("x", 0.f);
    hosted->setTo("x", 10.f);
    hosted->start();
    auto direct = hosted->advance({eve::SimulationTick(8), eve::Duration::zero()});
    REQUIRE(direct.ok());
    auto rejectedHost = animation->advance({eve::SimulationTick(8), eve::Duration::zero()});
    CHECK(!rejectedHost.ok());
    CHECK(std::abs(hosted->get("x")) < 1e-5f);
}

TEST_CASE("time.checkedConsumers.particleStatsCommitAsOneFrame") {
    auto* particles = eve::particles::Particles::create();
    auto* emitter   = particles->newEmitter(4);
    REQUIRE(emitter != nullptr);
    CHECK(emitter->getConfigReloadObservation() == std::string("unbound"));

    auto applied = eve::particles::ParticleSimSystem::advance({eve::SimulationTick(1), eve::Duration::zero()});
    REQUIRE(applied.ok());
    const auto committed = eve::particles::particleFrameStats();

    auto duplicate =
        eve::particles::ParticleSimSystem::advance({eve::SimulationTick(1), eve::Duration::fromNanoseconds(1)});
    CHECK(!duplicate.ok());
    const auto afterDuplicate = eve::particles::particleFrameStats();
    CHECK(afterDuplicate.frameIndex == committed.frameIndex);
    CHECK(afterDuplicate.simulationTick == committed.simulationTick);
    CHECK(afterDuplicate.emittersTotal == committed.emittersTotal);
    CHECK(afterDuplicate.particlesAfter == committed.particlesAfter);

    auto invalid =
        eve::particles::ParticleSimSystem::advance({eve::SimulationTick(2), eve::Duration::fromNanoseconds(-1)});
    CHECK(!invalid.ok());
    const auto afterInvalid = eve::particles::particleFrameStats();
    CHECK(afterInvalid.frameIndex == committed.frameIndex);
    CHECK(afterInvalid.simulationTick == committed.simulationTick);
}

TEST_CASE("subscription.eventPoll.containsCallbackFailureAfterCommit") {
    TestEvent event;
    int       observed = 0;
    auto      throwing = event.subscribePoll([&](const eve::platform_event::Message&) {
        ++observed;
        throw std::runtime_error("listener failure");
    });
    auto      message  = std::make_unique<eve::platform_event::Message>("committed");
    event.push(std::move(message));

    auto polled = event.pollOwned();
    REQUIRE(polled.get() != nullptr);
    CHECK_EQ(polled->name, std::string("committed"));
    CHECK_EQ(observed, 1);
    CHECK_EQ(event.pollObserverFailureCount(), std::uint64_t{1});
    throwing.dispose();
}

TEST_CASE("subscription.eventPoll.supportsReentrantSubscribeAndDispose") {
    TestEvent         event;
    int               first    = 0;
    int               second   = 0;
    bool              replaced = false;
    eve::Subscription replacement;
    auto              primary = event.subscribePoll([&](const eve::platform_event::Message&) {
        ++first;
        if (replaced) return;
        replaced = true;
        replacement.dispose();
        replacement = event.subscribePoll([&](const eve::platform_event::Message&) { ++second; });
    });
    replacement               = event.subscribePoll([&](const eve::platform_event::Message&) { ++second; });

    event.pushData("first");
    auto firstPolled = event.pollOwned();
    REQUIRE(firstPolled.get() != nullptr);
    CHECK_EQ(first, 1);
    CHECK_EQ(second, 0);

    event.pushData("second");
    auto secondPolled = event.pollOwned();
    REQUIRE(secondPolled.get() != nullptr);
    CHECK_EQ(first, 2);
    CHECK_EQ(second, 1);
    primary.dispose();
    replacement.dispose();
}

TEST_CASE("subscription.animClipReload.supportsAbsentProviderAndReentrancy") {
    eve::animation::AnimClipRegistry::clear();
    int               first    = 0;
    int               second   = 0;
    bool              replaced = false;
    eve::Subscription replacement;
    auto              primary =
        eve::animation::AnimClipRegistry::subscribeReload([&](const eve::animation::AnimClipRegistry::ReloadEvent&) {
            ++first;
            if (replaced) return;
            replaced = true;
            replacement.dispose();
            replacement = eve::animation::AnimClipRegistry::subscribeReload(
                [&](const eve::animation::AnimClipRegistry::ReloadEvent&) { ++second; });
        });
    replacement = eve::animation::AnimClipRegistry::subscribeReload(
        [&](const eve::animation::AnimClipRegistry::ReloadEvent&) { ++second; });

    auto absent = eve::animation::AnimClipRegistry::reloadPath("missing.anim.txt");
    REQUIRE(absent.ok());
    const int absentCount = std::move(absent).takeValue();
    CHECK_EQ(absentCount, 0);
    CHECK_EQ(first, 1);
    CHECK_EQ(second, 0);

    auto absentAgain = eve::animation::AnimClipRegistry::reloadPath("missing.anim.txt");
    REQUIRE(absentAgain.ok());
    const int absentAgainCount = std::move(absentAgain).takeValue();
    CHECK_EQ(absentAgainCount, 0);
    CHECK_EQ(first, 2);
    CHECK_EQ(second, 1);
    primary.dispose();
    replacement.dispose();
}
