// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "role": "tests",
  "default": "render-window",
  "module": "physics_cloth"
}
*/
// clang-format on

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>
#include "LayeredClothGeometryOracle.h"
#include "gpgpu/Gpgpu.h"
#include "graphics/Graphics.h"
#include "physics/cloth/ClothGPU.h"
#include "physics/cloth/LayeredClothGPU.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
namespace {
struct GpuClothFixture {
    eve::window::Window*     win = nullptr;
    eve::graphics::Graphics* gfx = nullptr;
    eve::gpgpu::Gpgpu&       initialize(zeroerr::TestContext* _ZEROERR_TEST_CONTEXT) {
        REQUIRE(std::getenv("EVENGINE_TEST_NO_RENDER") == nullptr);
        win = eve::window::Window::create();
        gfx = eve::graphics::Graphics::create();
        REQUIRE(win != nullptr);
        REQUIRE(gfx != nullptr);
        eve::window::WindowSettings settings;
        settings.width    = 320;
        settings.height   = 240;
        settings.centered = true;
        REQUIRE(win->setWindowSettings(settings));
        auto* compute = eve::gpgpu::Gpgpu::create();
        REQUIRE(compute != nullptr);
        REQUIRE(compute->isAvailable());
        return *compute;
    }
    ~GpuClothFixture() {
        if (win) win->close();
    }
};
using eve::physics::LayeredClothGPU;
void freeAll(LayeredClothGPU& cloth, float mass = 1.f) {
    for (int i = 0; i < cloth.getParticleCount(); ++i) cloth.setParticleInverseMass(i, mass);
}
using cloth_test_geometry::sheetSeparation;
}  // namespace
TEST_CASE("PhysicsLayeredClothGPU.stabilityAndSynchronousTimingBaseline") {
    GpuClothFixture        fixture;
    auto&                  compute = fixture.initialize(_ZEROERR_TEST_CONTEXT);
    [[maybe_unused]] auto* gfx     = fixture.gfx;
    LayeredClothGPU        cloth(compute, *gfx, 3, 4, 4, 0.25f, 0.09f, 0, 2, 0);
    cloth.setSolver(2, 8);
    cloth.setWind(0.3f, 0, 0.2f);
    std::vector<double> samples;
    std::ofstream       csv(std::filesystem::temp_directory_path() / "layered-cloth-gpu-benchmark.csv");
    REQUIRE(csv.good());
    csv << "frame,particles,substeps,iterations,update_wall_ms\n";
    for (int frame = 0; frame < 124; frame++) {
        const auto   begin  = std::chrono::steady_clock::now();
        auto         result = cloth.update(1.f / 60.f);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
        REQUIRE(result.ok());
        if (frame >= 4) {
            samples.push_back(ms);
            csv << frame << ",48,2,8," << ms << '\n';
        }
    }
    auto positions = cloth.readPositions();
    for (float value : positions) REQUIRE(std::isfinite(value));
    for (int sheet = 0; sheet < 3; sheet++)
        for (int col = 0; col < 4; col++)
            REQUIRE(std::abs(positions[size_t((sheet * 16 + col) * 4 + 1)] - (2.f + float(sheet) * 0.09f)) < 1e-6f);
    std::sort(samples.begin(), samples.end());
    std::cout << "cloth baseline: 48 particles / 2 substeps / 8 iterations; synchronous update wall ms: median="
              << samples[60] << ", p95=" << samples[113] << ", max=" << samples.back()
              << "; hash overflow events in last step=" << cloth.readDiagnostics()[3] << '\n';
}

TEST_CASE("PhysicsLayeredClothGPU.fallingSheetsRemainSeparatedForOneSimulatedMinute") {
    GpuClothFixture        fixture;
    auto&                  compute = fixture.initialize(_ZEROERR_TEST_CONTEXT);
    [[maybe_unused]] auto* gfx     = fixture.gfx;
    LayeredClothGPU        cloth(compute, *gfx, 4, 2, 2, 0.5f, 0.03f, 0, 0.25f, 0);
    cloth.setSolver(2, 8);
    cloth.setContact(0.08f, 0.4f);
    cloth.setFloor(0);
    freeAll(cloth);
    float highest = 0, leastGap = 100;
    for (int frame = 0; frame < 1800; frame++) {
        auto result = cloth.update(1.f / 30.f);
        REQUIRE(result.ok());
        if (frame % 120 != 119) continue;
        const auto pose = cloth.readPositions();
        for (int i = 0; i < 16; i++) {
            for (int k = 0; k < 4; k++) REQUIRE(std::isfinite(pose[size_t(i * 4 + k)]));
            REQUIRE(pose[size_t(i * 4 + 1)] >= -1e-6f);
            highest = std::max(highest, pose[size_t(i * 4 + 1)]);
        }
        const double separation = sheetSeparation(pose);
        leastGap                = std::min(leastGap, float(separation));
    }
    std::cout << "60-second stack: max height=" << highest << ", minimum geometric layer gap=" << leastGap << '\n';
    REQUIRE(highest < 1.f);
    REQUIRE(leastGap > 0.03f);
    const double finalGap = sheetSeparation(cloth.readPositions());
    REQUIRE(finalGap > 0.075);
    std::cout << "60-second final geometric gap=" << finalGap << '\n';
    cloth.reset();
    auto result = cloth.update(1.f / 30.f);
    REQUIRE(result.ok());
    for (float value : cloth.readPositions()) REQUIRE(std::isfinite(value));
}

TEST_CASE("softbody.gpu.clothLargeSelfCollision") {
    GpuClothFixture                         fixture;
    auto&                                   compute = fixture.initialize(_ZEROERR_TEST_CONTEXT);
    std::unique_ptr<eve::physics::ClothGPU> cloth(new eve::physics::ClothGPU(&compute, 100, 80, 12.f, 20.f, 20.f));
    REQUIRE(cloth.get() != nullptr);
    REQUIRE_EQ(cloth->getParticleCount(), 8000);
    // Keep the top row pinned so the 8000-particle sheet hangs like a curtain
    // under gravity. This exercises the spatially-hashed self-collision at
    // scale and verifies solver stability, not perfect separation in a pile.
    cloth->setGravity(0.f, 180.f);
    cloth->setBounds(0.f, 0.f, 1200.f, 1000.f);
    cloth->setParticleSize(3.f);
    cloth->setStiffness(0.9f);
    cloth->setIterations(24);
    cloth->setDamping(0.3f);
    cloth->setSelfCollision(true);

    for (int f = 0; f < 120; ++f) {
        cloth->update(1.f / 60.f);
    }

    for (int i = 0; i < 200; ++i) {
        const int idx = (i * 79) % cloth->getParticleCount();
        REQUIRE(std::isfinite(cloth->getParticleX(idx)));
        REQUIRE(std::isfinite(cloth->getParticleY(idx)));
    }
    // The upper half of the hanging sheet stays near its rest column (x = 620)
    // and rest heights — the solver is stable at 8000 particles.
    for (int r = 1; r <= 40; r += 3) {
        const int idx = r * 100 + 50;
        REQUIRE(std::fabs(cloth->getParticleX(idx) - 620.f) < 30.f);
        REQUIRE(std::fabs(cloth->getParticleY(idx) - (20.f + float(r) * 12.f)) < 15.f);
    }
    // Everything stays inside the bounds.
    for (int i = 0; i < 400; ++i) {
        const int idx = (i * 131) % cloth->getParticleCount();
        REQUIRE_GE(cloth->getParticleX(idx), 0.f);
        REQUIRE_LE(cloth->getParticleX(idx), 1200.f);
        REQUIRE_GE(cloth->getParticleY(idx), 0.f);
        REQUIRE_LE(cloth->getParticleY(idx), 1000.f);
    }
}
