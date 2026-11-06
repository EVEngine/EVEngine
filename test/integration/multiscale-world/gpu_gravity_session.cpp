// clang-format off
/* eve-test-policy:
{"schema":"evengine.test-policy","version":1,"default":"render-window","module":"physics_field_gpu"}
*/
// clang-format on
#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <vector>
#include "common/Capability.h"
#include "common/GpuInfo.h"
#include "gpgpu/Gpgpu.h"
#include "graphics/Graphics.h"
#include "physics/field/gpu/GpuGravitySession.h"
#include "window/Window.h"
#include "zeroerr/unittest.h"

namespace {
using namespace eve::physics::field::gpu;
std::vector<GpuSource> compactSources(int count) {
    std::vector<GpuSource> sources(count);
    for (int i = 0; i < count; ++i) {
        const int cell = i & 7;
        const float jitter = (float((i * 37) % 101) / 100.f - .5f) * .02f;
        sources[i] = {{{(cell & 1) ? .5f + jitter : -.5f + jitter,
                        (cell & 2) ? .5f - jitter * .7f : -.5f - jitter * .7f,
                        (cell & 4) ? .5f + jitter * .5f : -.5f + jitter * .5f}}, 1.f / count};
    }
    return sources;
}
}  // namespace

TEST_CASE("physics.field.gpu.sessionOwnsCandidateAndRejectsNearTargets") {
    auto* window = eve::window::Window::create();
    auto* graphics = eve::graphics::Graphics::create();
    eve::window::WindowSettings settings;
    settings.width = 320;
    settings.height = 180;
    REQUIRE(window->setWindowSettings(settings));
    auto* compute = eve::gpgpu::Gpgpu::create();
    constexpr int count = 1024;
    const auto sources = compactSources(count);
    std::vector<GpuMotion> targets(count);
    for (int i = 0; i < count; ++i) targets[i].position = {10.f + float(i % 101) * .001f, 0.f, .25f};
    const auto dt = eve::Duration::fromNanoseconds(16666667);
    const eve::physics::field::FieldStamp initialStamp{eve::SimulationTick{0}, eve::Revision{0}};
    auto built = GpuGravitySession::create(*compute, *graphics, sources, targets, initialStamp, dt, {});
    REQUIRE(built.ok());
    auto session = std::move(built).takeValue();
    auto initial = session->snapshot();
    REQUIRE(initial.ok());
    CHECK(initial.value().sources[0].position == sources[0].position);
    CHECK(initial.value().targets[0].position == targets[0].position);

    const eve::SimulationStep step{eve::SimulationTick{1}, dt};
    {
        auto canceled = session->prepareStep(step, .001f);
        REQUIRE(canceled.ok());
        auto stillPublished = session->snapshot();
        REQUIRE(stillPublished.ok());
        CHECK(stillPublished.value().stamp == initialStamp);
        CHECK(stillPublished.value().sources[0].position == sources[0].position);
    }
    auto prepared = session->prepareStep(step, .001f);
    REQUIRE(prepared.ok());
    auto committed = session->commit(std::move(prepared).takeValue());
    REQUIRE(committed.ok());
    auto published = session->snapshot();
    REQUIRE(published.ok());
    CHECK(published.value().stamp.tick == step.tick);
    CHECK(published.value().stamp.revision == eve::Revision{1});
    CHECK(std::abs(published.value().sources[0].position[0] - (sources[0].position[0] + .001f)) < 1e-6f);

    std::array<double, 3> acceleration{};
    for (int i = 0; i < count; ++i) {
        const auto& source = sources[i];
        const double x = source.position[0] + ((i & 1) == 0 ? .001f : -.001f);
        const std::array<double, 3> delta{x - targets[0].position[0],
                                          source.position[1] - targets[0].position[1],
                                          source.position[2] - targets[0].position[2]};
        const double distance = std::hypot(delta[0], delta[1], delta[2]);
        const double factor = source.mass / (distance * distance * distance);
        for (int axis = 0; axis < 3; ++axis) acceleration[axis] += factor * delta[axis];
    }
    for (int axis = 0; axis < 3; ++axis) {
        const double expected = targets[0].position[axis] + acceleration[axis] * dt.seconds() * dt.seconds();
        CHECK(std::abs(published.value().targets[0].position[axis] - expected) < 1e-5);
    }
    auto repeated = session->prepareStep(step, 0.f);
    CHECK(!repeated.ok());
    auto motionOverflow = session->prepareStep({eve::SimulationTick{2}, dt}, .001f);
    CHECK(!motionOverflow.ok());

    targets[0].position = {.5f, .5f, .5f};
    auto nearBuilt = GpuGravitySession::create(*compute, *graphics, sources, targets, initialStamp, dt, {});
    REQUIRE(nearBuilt.ok());
    auto nearSession = std::move(nearBuilt).takeValue();
    auto rejected = nearSession->prepareStep(step, .001f);
    CHECK(!rejected.ok());
    auto nearPublished = nearSession->snapshot();
    REQUIRE(nearPublished.ok());
    CHECK(nearPublished.value().stamp == initialStamp);
    CHECK(nearPublished.value().sources[0].position == sources[0].position);
    CHECK(nearPublished.value().targets[0].position == targets[0].position);

    targets[0].position = {10.f, 0.f, .25f};
    targets[0].velocity = {std::numeric_limits<float>::max(), 0.f, 0.f};
    const auto longStep = eve::Duration::fromNanoseconds(2000000000);
    auto overflowBuilt = GpuGravitySession::create(*compute, *graphics, sources, targets, initialStamp, longStep, {});
    REQUIRE(overflowBuilt.ok());
    auto overflowSession = std::move(overflowBuilt).takeValue();
    auto overflowRejected = overflowSession->prepareStep({eve::SimulationTick{1}, longStep}, 0.f);
    CHECK(!overflowRejected.ok());
    auto overflowPublished = overflowSession->snapshot();
    REQUIRE(overflowPublished.ok());
    CHECK(overflowPublished.value().stamp == initialStamp);
    CHECK(overflowPublished.value().targets[0].velocity == targets[0].velocity);
}

TEST_CASE("physics.field.gpu.sessionMillionRowTiming") {
    // The normal CTest run stays small. Set the environment variable for an explicit local measurement.
    const char* requested = std::getenv("EVE_GPU_FIELD_BENCH_ROWS");
    if (!requested) return;
    const int count = std::atoi(requested);
    const char* requestedFrames = std::getenv("EVE_GPU_FIELD_BENCH_FRAMES");
    const int frames = requestedFrames ? std::atoi(requestedFrames) : 120;
    REQUIRE(count > 0);
    REQUIRE(count <= 1000000);
    REQUIRE(frames > 0);
    REQUIRE(frames <= 1000);
    auto* window = eve::window::Window::create();
    auto* graphics = eve::graphics::Graphics::create();
    eve::window::WindowSettings settings;
    settings.width = 320;
    settings.height = 180;
    REQUIRE(window->setWindowSettings(settings));
    auto* compute = eve::gpgpu::Gpgpu::create();
    const auto sources = compactSources(count);
    std::vector<GpuMotion> targets(count);
    for (int i = 0; i < count; ++i)
        targets[i].position = {10.f + float(i % 101) * .001f, 0.f, .25f};
    const auto dt = eve::Duration::fromNanoseconds(16666667);
    const eve::physics::field::FieldStamp initialStamp{eve::SimulationTick{0}, eve::Revision{0}};
    const auto createStart = std::chrono::steady_clock::now();
    auto built = GpuGravitySession::create(*compute, *graphics, sources, targets, initialStamp, dt, {});
    REQUIRE(built.ok());
    auto session = std::move(built).takeValue();
    const auto createEnd = std::chrono::steady_clock::now();
    std::vector<double> milliseconds;
    milliseconds.reserve(frames);
    for (int frame = 1; frame <= frames; ++frame) {
        const auto start = std::chrono::steady_clock::now();
        auto prepared = session->prepareStep({eve::SimulationTick{static_cast<std::uint64_t>(frame)}, dt}, 0.f);
        REQUIRE(prepared.ok());
        auto committed = session->commit(std::move(prepared).takeValue());
        REQUIRE(committed.ok());
        const auto end = std::chrono::steady_clock::now();
        milliseconds.push_back(std::chrono::duration<double, std::milli>(end - start).count());
    }
    std::sort(milliseconds.begin(), milliseconds.end());
    const auto createMs = std::chrono::duration<double, std::milli>(createEnd - createStart).count();
    const auto* device = eve::cap::query<eve::caps::IGpuInfo>();
    std::cout << "GPU session device=" << (device ? device->gpuName() : "unknown")
              << " rows=" << count << " create_ms=" << createMs
              << " step_p50_ms=" << milliseconds[(frames - 1) / 2]
              << " step_p95_ms=" << milliseconds[(frames * 95 + 99) / 100 - 1]
              << " step_p99_ms=" << milliseconds[(frames * 99 + 99) / 100 - 1]
              << " step_max_ms=" << milliseconds.back() << '\n';
    CHECK(session->stamp().tick == eve::SimulationTick{static_cast<std::uint64_t>(frames)});
}
