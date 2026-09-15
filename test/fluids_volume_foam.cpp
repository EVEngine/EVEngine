#include <cmath>
#include <glm/geometric.hpp>
#include "fluids/VolumeFluidCodec.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::fluids;
namespace {
std::unique_ptr<VolumeFluid> shear() {
    auto created = VolumeFluid::create({});
    if (!created) return {};
    auto                             fluid = std::move(created).takeValue();
    std::vector<VolumeFluidParticle> particles(2);
    particles[0].position = {-.05f, 1.f, 0.f};
    particles[0].velocity = {0.f, -1.f, 0.f};
    particles[1].position = {.05f, 1.f, 0.f};
    particles[1].velocity = {0.f, 1.f, 0.f};
    if (!fluid->emit(particles)) return {};
    return fluid;
}
}  // namespace
TEST_CASE("fluids.volume.foam.thresholdsBudgetAndReplay") {
    auto fluid = shear();
    REQUIRE(bool(fluid));
    VolumeFluidFoam foam;
    auto            state             = foam.snapshot();
    state.settings.rate               = 100.f;
    state.settings.vorticityThreshold = 0.f;
    state.settings.densityThreshold   = 1e9f;
    state.settings.randomness         = .01f;
    state.settings.maxPerStep         = 1;
    REQUIRE(foam.restore(state).ok());
    VolumeFluidDiffuse pool;
    auto               emitted = foam.advance(*fluid, pool, .02f);
    REQUIRE(emitted.ok());
    REQUIRE(emitted.value() == 1);
    const auto  p       = pool.snapshot().particles[0];
    const float nearest = std::min(glm::length(p.position - glm::vec3(-.05f, 1.f, 0.f)),
                                   glm::length(p.position - glm::vec3(.05f, 1.f, 0.f)));
    REQUIRE(nearest <= .010001f);
    REQUIRE(p.life == 2.f);
    VolumeFluidFoam    replay;
    VolumeFluidDiffuse replayPool;
    REQUIRE(replay.restore(state).ok());
    REQUIRE(replay.advance(*fluid, replayPool, .02f).ok());
    REQUIRE(replayPool.snapshot().particles[0].position == p.position);
    REQUIRE(replay.snapshot().randomState == foam.snapshot().randomState);
    REQUIRE(fluid->particles()[0].position.x == -.05f);
    state.settings.densityThreshold = 0.f;
    REQUIRE(foam.restore(state).ok());
    REQUIRE(foam.advance(*fluid, pool, .02f).ok());
    REQUIRE(pool.particleCount() == 1);
    REQUIRE(foam.snapshot().randomState == state.randomState);
    state.settings.densityThreshold   = 1e9f;
    state.settings.vorticityThreshold = 1e6f;
    REQUIRE(foam.restore(state).ok());
    REQUIRE(foam.advance(*fluid, pool, .02f).ok());
    REQUIRE(pool.particleCount() == 1);
}
TEST_CASE("fluids.volume.foam.creditCapacityAndStrictRestore") {
    auto fluid = shear();
    REQUIRE(bool(fluid));
    VolumeFluidFoam foam;
    auto            state             = foam.snapshot();
    state.settings.rate               = 1.f;
    state.credit                      = "0.99";
    state.settings.vorticityThreshold = 0.f;
    state.settings.densityThreshold   = 1e9f;
    REQUIRE(foam.restore(state).ok());
    VolumeFluidDiffuse pool;
    auto               capacity = pool.snapshot();
    capacity.capacity           = 1;
    REQUIRE(pool.restore(capacity).ok());
    auto emitted = foam.advance(*fluid, pool, .02f);
    REQUIRE(emitted.ok());
    REQUIRE(emitted.value() == 1);
    auto saved = foam.snapshot();
    REQUIRE(std::abs(std::stod(saved.credit) - .01) < 1e-8);
    REQUIRE(decodeVolumeFluidFoam(encodeVolumeFluidFoam(saved)).ok());
    auto encoded = encodeVolumeFluidFoam(saved);
    encoded.set("unknown", 3);
    REQUIRE(!decodeVolumeFluidFoam(encoded).ok());
    state.credit = "1";
    REQUIRE(!foam.restore(state).ok());
    state.credit      = "0";
    state.randomState = 0;
    REQUIRE(!foam.restore(state).ok());
    REQUIRE(foam.snapshot().credit == saved.credit);
    REQUIRE(foam.snapshot().randomState == saved.randomState);
    REQUIRE(!foam.advance(*fluid, pool, -.01f).ok());
    REQUIRE(foam.snapshot().credit == saved.credit);
    saved.credit = "0.99";
    REQUIRE(foam.restore(saved).ok());
    emitted = foam.advance(*fluid, pool, .02f);
    REQUIRE(emitted.ok());
    REQUIRE(emitted.value() == 0);
    REQUIRE(foam.snapshot().randomState == saved.randomState);
    pool.clear();
    emitted = foam.advance(*fluid, pool, 0.f);
    REQUIRE(emitted.ok());
    REQUIRE(emitted.value() == 0);
}
TEST_CASE("fluids.volume.foam.queryFailureRollback") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                             fluid = std::move(created).takeValue();
    std::vector<VolumeFluidParticle> particles(2001);
    for (auto& p : particles) p.position = {0.f, 1.f, 0.f};
    REQUIRE(fluid->emit(particles).ok());
    VolumeFluidFoam    foam;
    VolumeFluidDiffuse pool;
    auto               state = foam.snapshot();
    REQUIRE(!foam.advance(*fluid, pool, .02f).ok());
    REQUIRE(foam.snapshot().credit == state.credit);
    REQUIRE(foam.snapshot().randomState == state.randomState);
    REQUIRE(pool.particleCount() == 0);
    state.settings.rate = 0.f;
    REQUIRE(foam.restore(state).ok());
    REQUIRE(foam.advance(*fluid, pool, .02f).ok());
    REQUIRE(pool.particleCount() == 0);
}

TEST_CASE("fluids.volume.foam.actorSourceMatchesEmitterOwnership") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                             fluid = std::move(created).takeValue();
    std::vector<VolumeFluidParticle> particles(4);
    for (size_t i = 0; i < particles.size(); ++i) {
        const bool right        = i >= 2;
        const bool upper        = (i % 2) == 1;
        particles[i].position   = {right ? .55f : -.55f, upper ? 1.05f : .95f, 0.f};
        particles[i].velocity   = {upper ? 1.f : -1.f, 0.f, 0.f};
        particles[i].actorGroup = right ? 9u : 3u;
    }
    REQUIRE(fluid->emit(particles).ok());
    VolumeFluidFoam foam;
    auto            state             = foam.snapshot();
    state.settings.rate               = 100.f;
    state.settings.randomness         = 0.f;
    state.settings.vorticityThreshold = 0.f;
    state.settings.densityThreshold   = 1e9f;
    state.settings.maxPerStep         = 1;
    REQUIRE(foam.restore(state).ok());
    VolumeFluidDiffuse pool;
    auto               emitted = foam.advanceFromActor(*fluid, pool, .02f, 9);
    REQUIRE(emitted.ok());
    REQUIRE(emitted.value() == 1);
    REQUIRE(pool.particleCount() == 1);
    REQUIRE(pool.snapshot().particles[0].position.x > .4f);

    REQUIRE(fluid->step(1.f / 120.f, 1).ok());
    VolumeFluidFoam interpolated;
    REQUIRE(interpolated.restore(state).ok());
    VolumeFluidDiffuse interpolatedPool;
    emitted = interpolated.advanceFromActorInterpolated(*fluid, interpolatedPool, .02f, 9, 0.f);
    REQUIRE(emitted.ok());
    REQUIRE(emitted.value() == 1);
    REQUIRE(std::abs(interpolatedPool.snapshot().particles[0].position.x - .55f) < 1e-5f);
    const auto interpolationState = interpolated.snapshot();
    REQUIRE(!interpolated.advanceFromActorInterpolated(*fluid, interpolatedPool, .02f, 9, -.01f).ok());
    REQUIRE(interpolated.snapshot().credit == interpolationState.credit);
    REQUIRE(interpolated.snapshot().randomState == interpolationState.randomState);

    VolumeFluidFoam absent;
    REQUIRE(absent.restore(state).ok());
    VolumeFluidDiffuse absentPool;
    const auto         before = absent.snapshot();
    emitted                   = absent.advanceFromActor(*fluid, absentPool, .02f, 77);
    REQUIRE(emitted.ok());
    REQUIRE(emitted.value() == 0);
    REQUIRE(absentPool.particleCount() == 0);
    REQUIRE(absent.snapshot().randomState == before.randomState);
    REQUIRE(!absent.advanceFromActor(*fluid, absentPool, -.01f, 3).ok());
}
