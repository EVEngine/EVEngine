#include <cmath>
#include <limits>
#include "fluids/VolumeFluidCodec.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::fluids;

TEST_CASE("fluids.volume.diffuse.advectionLifetimeAndOwnership") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                      fluid = std::move(created).takeValue();
    const VolumeFluidParticle source{{0.f, 1.f, 0.f}, {1.f, 0.f, 0.f}};
    REQUIRE(fluid->emit(std::span(&source, 1)).ok());
    VolumeFluidDiffuse               pool;
    const VolumeFluidDiffuseParticle p{{0.f, 1.f, 0.f}, {0.f, 0.f, 0.f}, .03f};
    REQUIRE(pool.emit(std::span(&p, 1)).ok());
    const auto saved = pool.snapshot();
    REQUIRE(pool.advance(*fluid, .01f, 1).ok());
    auto next = pool.snapshot();
    REQUIRE(std::abs(next.particles[0].position.x - .01f) < 1e-6f);
    REQUIRE(std::abs(next.particles[0].life - .02f) < 1e-6f);
    REQUIRE(next.particles[0].velocity.x == 1.f);
    REQUIRE(saved.particles[0].position.x == 0.f);
    REQUIRE(fluid->particles()[0].position.x == 0.f);
    REQUIRE(fluid->particles()[0].velocity.x == 1.f);
    VolumeFluidDiffuse replay;
    REQUIRE(replay.restore(saved).ok());
    REQUIRE(replay.advance(*fluid, .01f, 1).ok());
    REQUIRE(replay.snapshot().particles[0].position.x == next.particles[0].position.x);
    REQUIRE(pool.advance(*fluid, .03f, 1).ok());
    REQUIRE(pool.particleCount() == 0);
    REQUIRE(replay.advance(*fluid, .01f, 2).ok());
    REQUIRE(replay.particleCount() == 0);
    REQUIRE(pool.restore(saved).ok());
    fluid.reset();
    auto replacement = VolumeFluid::create({});
    REQUIRE(replacement.ok());
    REQUIRE(pool.advance(*replacement.value(), .01f, 1).ok());
    REQUIRE(pool.particleCount() == 0);
}

TEST_CASE("fluids.volume.diffuse.atomicAdmissionAndStrictState") {
    VolumeFluidDiffuse pool;
    auto               state = pool.snapshot();
    state.capacity           = 2;
    REQUIRE(pool.restore(state).ok());
    std::vector<VolumeFluidDiffuseParticle> batch(2);
    batch[1].life = 0.f;
    REQUIRE(!pool.emit(batch).ok());
    REQUIRE(pool.particleCount() == 0);
    batch[1].life = 1.f;
    REQUIRE(pool.emit(batch).ok());
    REQUIRE(!pool.emit(batch).ok());
    REQUIRE(pool.particleCount() == 2);
    REQUIRE(pool.availableCapacity() == 0);
    std::vector<glm::vec4> renderData;
    pool.copyRenderData(renderData);
    const auto* renderStorage = renderData.data();
    REQUIRE(renderData.size() == 2);
    REQUIRE(renderData[0].w == 2.f);
    pool.copyRenderData(renderData);
    REQUIRE(renderData.data() == renderStorage);
    state        = pool.snapshot();
    auto encoded = encodeVolumeFluidDiffuse(state);
    auto decoded = decodeVolumeFluidDiffuse(encoded);
    REQUIRE(decoded.ok());
    REQUIRE(decoded.value().particles.size() == 2);
    REQUIRE(decodeVolumeFluidDiffuseParticles(*encoded.find("particles")).ok());
    encoded.set("unknown", 1);
    REQUIRE(!decodeVolumeFluidDiffuse(encoded).ok());
    state.version = 2;
    REQUIRE(!pool.restore(state).ok());
    state.version                 = 1;
    state.particles[0].velocity.x = std::numeric_limits<float>::infinity();
    REQUIRE(!pool.restore(state).ok());
    REQUIRE(pool.snapshot().particles[0].velocity.x == 0.f);
    auto fluid = VolumeFluid::create({});
    REQUIRE(fluid.ok());
    REQUIRE(!pool.advance(*fluid.value(), 0.f, 1).ok());
    REQUIRE(!pool.advance(*fluid.value(), .01f, 1000001).ok());
    REQUIRE(pool.snapshot().particles[0].life == 2.f);
    pool.clear();
    REQUIRE(pool.availableCapacity() == 2);
}

TEST_CASE("fluids.volume.diffuse.queryBudgetRollbackAndExpiredFastPath") {
    auto fluid = VolumeFluid::create({});
    REQUIRE(fluid.ok());
    std::vector<VolumeFluidParticle> sources(1000);
    for (auto& p : sources) p.position = {0.f, 1.f, 0.f};
    REQUIRE(fluid.value()->emit(sources).ok());
    VolumeFluidDiffuse pool;
    auto               state = pool.snapshot();
    state.capacity           = 4001;
    state.particles.resize(4001);
    for (auto& p : state.particles) p.position = {0.f, 1.f, 0.f};
    REQUIRE(pool.restore(state).ok());
    REQUIRE(!pool.advance(*fluid.value(), .01f, 1).ok());
    REQUIRE(pool.particleCount() == 4001);
    REQUIRE(pool.snapshot().particles.back().life == 2.f);
    REQUIRE(pool.snapshot().particles.back().position.y == 1.f);
    for (auto& p : state.particles) p.life = .005f;
    REQUIRE(pool.restore(state).ok());
    REQUIRE(pool.advance(*fluid.value(), .01f, 1).ok());
    REQUIRE(pool.particleCount() == 0);
    REQUIRE(fluid.value()->particleCount() == 1000);
}
