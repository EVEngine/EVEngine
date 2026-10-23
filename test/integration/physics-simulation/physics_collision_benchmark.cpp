#include "physics/Body3D.h"
#include "physics/World3D.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace eve::physics;

TEST_CASE("physics.benchmark.collision.grid1000") {
    // Fixed workload, no hardware-specific time gate. Run in Release for
    // comparable numbers; setup and warmup are excluded from timings.
    World3D world(0.f, -9.8f, 0.f, false);
    auto*   ground = world.newBody("static", 9.f, -0.5f, 9.f);
    ground->newBoxShape(40.f, 1.f, 40.f);
    for (int y = 0; y < 10; ++y)
        for (int z = 0; z < 10; ++z)
            for (int x = 0; x < 10; ++x) {
                auto* body = world.newBody("dynamic", 2.f * x, 0.5f + 1.02f * y, 2.f * z);
                body->newBoxShape(1.f, 1.f, 1.f);
            }
    for (int tick = 0; tick < 120; ++tick) world.updateFull(1.f / 60.f, 4);

    using Clock = std::chrono::steady_clock;
    std::vector<double> samples;
    for (int tick = 0; tick < 300; ++tick) {
        const auto start = Clock::now();
        world.updateFull(1.f / 60.f, 4);
        samples.push_back(std::chrono::duration<double, std::milli>(Clock::now() - start).count());
    }
    std::sort(samples.begin(), samples.end());
    int        hits       = 0;
    const auto queryStart = Clock::now();
    for (int repeat = 0; repeat < 100; ++repeat)
        for (int z = 0; z < 10; ++z)
            for (int x = 0; x < 10; ++x)
                if (world.rayCast(2.f * x, 20.f, 2.f * z, 2.f * x, -2.f, 2.f * z) >= 0) ++hits;
    const double queryMs = std::chrono::duration<double, std::milli>(Clock::now() - queryStart).count();
    REQUIRE_EQ(hits, 10000);
    REQUIRE_EQ(world.getBodyCount(), 1001);
    REQUIRE(std::isfinite(samples.back()));
    std::printf(
        "physics_collision grid1000 dt=1/60 substeps=4 sleep=off warmup=120 samples=300 "
        "step_p50_ms=%.6f step_p95_ms=%.6f step_max_ms=%.6f "
        "rays=10000 rays_ms=%.6f contacts=%d awake=%d backend_bytes=%d\n",
        samples[149], samples[284], samples.back(), queryMs, world.getContactCount(), world.getAwakeBodyCount(),
        world.getMemoryByteCount());
}
