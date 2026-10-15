#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "graphics/Graphics.h"
#include "graphics/Light.h"
#include "graphics/RenderSystem.h"
#include "particles/ParticleEmitter.h"
#include "particles/ParticleRuntime.h"
#include "particles/ParticleSystem.h"
#include "particles/Particles.h"
#include "window/Window.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace eve::particles;

namespace {

[[maybe_unused]] double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

[[maybe_unused]] eve::graphics::Mesh* makeLeftHalfDepthPlane(eve::graphics::Graphics* gfx) {
    const std::array<float, 12> positions = {
        -1.f, -1.f, 0.5f, 0.f, -1.f, 0.5f, 0.f, 1.f, 0.5f, -1.f, 1.f, 0.5f,
    };
    const std::array<float, 12> normals = {
        0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f,
    };
    const std::array<float, 8>         uvs     = {0.f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f, 1.f};
    const std::array<std::uint32_t, 6> indices = {0, 2, 1, 0, 3, 2};
    return gfx->newMeshFromArrays(positions.data(), normals.data(), uvs.data(), 4, indices.data(), 6);
}

[[maybe_unused]] eve::graphics::Mesh* makeFullscreenPlane(eve::graphics::Graphics* gfx) {
    const std::array<float, 12> positions = {
        -1.f, -1.f, 0.5f, 1.f, -1.f, 0.5f, 1.f, 1.f, 0.5f, -1.f, 1.f, 0.5f,
    };
    const std::array<float, 12> normals = {
        0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f,
    };
    const std::array<float, 8>         uvs     = {0.f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f, 1.f};
    const std::array<std::uint32_t, 6> indices = {0, 2, 1, 0, 3, 2};
    return gfx->newMeshFromArrays(positions.data(), normals.data(), uvs.data(), 4, indices.data(), 6);
}

[[maybe_unused]] float colorDistance(const Color& a, const Color& b) {
    const float dr = a.r - b.r;
    const float dg = a.g - b.g;
    const float db = a.b - b.b;
    return std::sqrt(dr * dr + dg * dg + db * db);
}

}  // namespace

TEST_CASE("particles.gpu.unsupportedFeaturesFallBackToCpu") {
    auto* emitter = Particles::create()->newEmitter(256);
    emitter->setRandomSeed(8128);
    emitter->setGpuSimulation(true);
    emitter->setCollision("bounce", 2.f, 0.5f, 0.f);
    emitter->setEmissionRate(120.f);
    emitter->setParticleLifetime(2.f, 2.f);
    emitter->start();

    ParticleSimSystem::update(0.25f);

    CHECK(emitter->getGpuSimulation());
    CHECK(!emitter->gpuSim()->residentActive);
    CHECK_EQ(emitter->gpuSim()->residentHandle, std::uint64_t(0));
    REQUIRE_GT(emitter->getCount(), 0);
    REQUIRE_EQ(emitter->getCount(), emitter->sim()->alive);
}
