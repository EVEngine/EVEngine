#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "pixelworld/PixelWorld.h"
#include "pixelworld_thread/PixelWorldThread.h"
#include "thread/JobSystem.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

using namespace eve::pixelworld;

TEST_CASE("pixelworld.parallel_thermal_candidates_are_bit_exact_with_reference") {
    PixelWorld reference(7001);
    PixelWorld parallel(7001);
    for (PixelWorld* world : {&reference, &parallel}) {
        for (int x = -70; x <= 70; ++x) world->setMaterial(x, 80, "stone");
        world->paintCircle(-35, 20, 18, "sand");
        world->paintCircle(35, 20, 18, "water");
        world->paintCircle(0, 40, 12, "lava");
        world->paintCircle(0, 58, 16, "ice");
    }

    std::unique_ptr<eve::thread::JobSystem> jobs(eve::thread::createJobSystem(4));
    eve::pixelworld_thread::JobSystemPixelScheduler scheduler(*jobs);
    CHECK_EQ(scheduler.workerCount(), std::size_t(4));
    for (std::uint64_t tick = 1; tick <= 100; ++tick) {
        reference.advance(eve::SimulationTick(tick)).expect("reference tick");
        const StepStats stats = parallel.advanceScheduled(eve::SimulationTick(tick), scheduler)
                                    .expect("scheduled tick");
        CHECK(stats.parallelTasks > 1);
    }
    const auto expected = reference.saveSnapshot().expect("reference snapshot");
    const auto actual = parallel.saveSnapshot().expect("parallel snapshot");
    REQUIRE_EQ(expected.size(), actual.size());
    CHECK(std::equal(expected.begin(), expected.end(), actual.begin()));
}

TEST_CASE("pixelworld.benchmark_million_active_pixels_reports_p50_p95") {
    if (!std::getenv("EVENGINE_PIXELWORLD_MILLION_BENCHMARK")) return;
    PixelWorld world(7002);
    for (int y = 0; y < 1024; ++y)
        for (int x = 0; x < 1024; ++x) world.setMaterial(x, y, "stone");
    std::unique_ptr<eve::thread::JobSystem> jobs(eve::thread::createJobSystem(4));
    eve::pixelworld_thread::JobSystemPixelScheduler scheduler(*jobs);
    std::vector<double> milliseconds;
    std::uint64_t totalReactions = 0;
    for (std::uint64_t tick = 1; tick <= 10; ++tick) {
        for (int cy = 0; cy < 16; ++cy)
            for (int cx = 0; cx < 16; ++cx)
                world.setMaterial(cx * kPixelChunkSize, cy * kPixelChunkSize, "stone");
        const auto started = std::chrono::steady_clock::now();
        const auto stats = world.advanceScheduled(eve::SimulationTick(tick), scheduler)
                               .expect("million-pixel scheduled tick");
        milliseconds.push_back(std::chrono::duration<double, std::milli>(
                                   std::chrono::steady_clock::now() - started).count());
        totalReactions += stats.reactions;
        CHECK_EQ(stats.chunksVisited, std::uint32_t(256));
    }
    std::sort(milliseconds.begin(), milliseconds.end());
    const double p50 = milliseconds[milliseconds.size() / 2];
    const double p95 = milliseconds.back();
    std::cout << "PIXELWORLD_BENCHMARK_JSON={\"occupiedPixels\":1048576,\"chunks\":256,"
                 "\"workload\":\"uniform-stone-force-awake\",\"samples\":10,\"workers\":"
              << scheduler.workerCount()
              << ",\"reactions\":" << totalReactions << ",\"p50Ms\":" << p50
              << ",\"p95Ms\":" << p95 << "}" << std::endl;
    CHECK(p50 > 0.0);
    CHECK(p95 >= p50);
}

TEST_CASE("pixelworld.benchmark_million_mobile_pixels_reports_p50_p95") {
    if (!std::getenv("EVENGINE_PIXELWORLD_ACTIVE_BENCHMARK")) return;
    PixelWorld world(7003);
    for (int y = 0; y < 1024; ++y)
        for (int x = 0; x < 1024; ++x) world.setMaterial(x, y, "sand");
    std::unique_ptr<eve::thread::JobSystem> jobs(eve::thread::createJobSystem(4));
    eve::pixelworld_thread::JobSystemPixelScheduler scheduler(*jobs);
    std::vector<double> milliseconds;
    std::uint64_t totalVisited = 0;
    std::uint64_t totalMoved = 0;
    for (std::uint64_t tick = 1; tick <= 5; ++tick) {
        const auto started = std::chrono::steady_clock::now();
        const auto stats = world.advanceScheduled(eve::SimulationTick(tick), scheduler)
                               .expect("million-mobile-pixel scheduled tick");
        milliseconds.push_back(std::chrono::duration<double, std::milli>(
                                   std::chrono::steady_clock::now() - started).count());
        totalVisited += stats.cellsVisited;
        totalMoved += stats.cellsMoved;
        CHECK(stats.chunksVisited > 0);
        CHECK(stats.parallelTasks > 1);
    }
    std::sort(milliseconds.begin(), milliseconds.end());
    const double p50 = milliseconds[milliseconds.size() / 2];
    const double p95 = milliseconds.back();
    std::cout << "PIXELWORLD_ACTIVE_BENCHMARK_JSON={\"mobilePixels\":1048576,\"chunks\":256,"
                 "\"workload\":\"packed-sand-over-empty\",\"samples\":5,\"workers\":"
              << scheduler.workerCount() << ",\"visited\":" << totalVisited
              << ",\"moved\":" << totalMoved << ",\"p50Ms\":" << p50
              << ",\"p95Ms\":" << p95 << "}" << std::endl;
    CHECK(p50 > 0.0);
    CHECK(p95 >= p50);
}
