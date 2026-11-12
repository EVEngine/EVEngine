// clang-format off
/* eve-test-policy:
{"schema":"evengine.test-policy","version":1,"default":"render-window","module":"physics_field_gpu"}
*/
// clang-format on
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>
#include "gpgpu/Gpgpu.h"
#include "gpgpu/GpuBuffer.h"
#include "graphics/Graphics.h"
#include "physics/field/gpu/GpuGravitySession.h"
#include "physics/field/gpu/GpuMortonKeys.h"
#include "window/Window.h"
#include "zeroerr/unittest.h"

namespace {
std::uint32_t spread3(std::uint32_t value) {
    value &= 1023u;
    value = (value | (value << 16)) & 0x030000ffu;
    value = (value | (value << 8)) & 0x0300f00fu;
    value = (value | (value << 4)) & 0x030c30c3u;
    return (value | (value << 2)) & 0x09249249u;
}
std::uint32_t expectedKey(const std::array<float, 3>& position) {
    std::array<std::uint32_t, 3> cell{};
    for (unsigned axis = 0; axis < 3; ++axis) {
        const float normalized = (position[axis] + 1.f) * 512.f;
        cell[axis]             = static_cast<std::uint32_t>(normalized < 1024.f ? normalized : 1023.f);
    }
    return spread3(cell[0]) | (spread3(cell[1]) << 1) | (spread3(cell[2]) << 2);
}
}  // namespace

TEST_CASE("physics.field.gpu.mortonKeysPublishArbitrarySpatialSourcesAtomically") {
    auto*                       window   = eve::window::Window::create();
    auto*                       graphics = eve::graphics::Graphics::create();
    eve::window::WindowSettings settings;
    settings.width  = 320;
    settings.height = 180;
    REQUIRE(window->setWindowSettings(settings));
    auto* compute = eve::gpgpu::Gpgpu::create();
    using namespace eve::physics::field::gpu;
    std::array<GpuSource, 8>               sources{{{{-1.f, -1.f, -1.f}, 1.f},
                                                    {{1.f, 1.f, 1.f}, 2.f},
                                                    {{0.f, 0.f, 0.f}, 3.f},
                                                    {{-.25f, .75f, .5f}, 4.f},
                                                    {{.75f, -.5f, -.25f}, 5.f},
                                                    {{-.875f, .25f, -.75f}, 6.f},
                                                    {{.125f, -.625f, .875f}, 7.f},
                                                    {{.5f, .5f, -.5f}, 8.f}}};
    sources[7].position = sources[2].position;
    std::unique_ptr<eve::gpgpu::GpuBuffer> sourceRows(compute->newBuffer(sizeof(sources)));
    REQUIRE(sourceRows != nullptr);
    sourceRows->uploadBytes(sources.data(), sizeof(sources));
    GpuMortonBounds bounds{{-1.f, -1.f, -1.f}, {1.f, 1.f, 1.f}};
    auto            prepared = GpuMortonKeys::prepare(*compute, *graphics, sources.size(), bounds);
    REQUIRE(prepared.ok());
    auto keys = std::move(prepared).takeValue();
    REQUIRE(!keys->residentView().ok());
    REQUIRE(keys->update(*sourceRows).ok());
    auto resident = keys->residentView();
    REQUIRE(resident.ok());
    CHECK(resident.value().sizeBytes >= sources.size() * sizeof(GpuMortonKey));
    auto first = keys->snapshot();
    REQUIRE(first.ok());
    for (std::size_t i = 0; i < sources.size(); ++i) {
        CHECK(first.value()[i].sourceIndex == i);
        CHECK(first.value()[i].key == expectedKey(sources[i].position));
    }
    REQUIRE(keys->sort().ok());
    REQUIRE(keys->sort().ok());
    auto ordered = keys->snapshot();
    REQUIRE(ordered.ok());
    for (std::size_t i = 1; i < sources.size(); ++i) {
        CHECK(ordered.value()[i - 1].key <= ordered.value()[i].key);
        if (ordered.value()[i - 1].key == ordered.value()[i].key)
            CHECK(ordered.value()[i - 1].sourceIndex < ordered.value()[i].sourceIndex);
    }
    sources[3].position[0] = 2.f;
    sourceRows->uploadBytes(sources.data(), sizeof(sources));
    REQUIRE(!keys->update(*sourceRows).ok());
    auto retained = keys->snapshot();
    REQUIRE(retained.ok());
    for (std::size_t i = 0; i < sources.size(); ++i) {
        CHECK(retained.value()[i].key == ordered.value()[i].key);
        CHECK(retained.value()[i].sourceIndex == ordered.value()[i].sourceIndex);
    }
    sources[3].position[0] = -.5f;
    sourceRows->uploadBytes(sources.data(), sizeof(sources));
    REQUIRE(keys->update(*sourceRows).ok());
    auto second = keys->snapshot();
    REQUIRE(second.ok());
    CHECK(second.value()[3].key == expectedKey(sources[3].position));
    CHECK(second.value()[3].key != first.value()[3].key);
    REQUIRE(keys->sort().ok());
    auto reordered = keys->snapshot();
    REQUIRE(reordered.ok());
    for (std::size_t i = 1; i < sources.size(); ++i)
        CHECK(reordered.value()[i - 1].key <= reordered.value()[i].key);
}

TEST_CASE("physics.field.gpu.mortonKeysScaleWithResidentSources") {
    const bool                  million  = std::getenv("EVE_GPU_MORTON_BENCH") != nullptr;
    const int                   count    = million ? 1000000 : 1024;
    const int                   frames   = million ? 120 : 2;
    auto*                       window   = eve::window::Window::create();
    auto*                       graphics = eve::graphics::Graphics::create();
    eve::window::WindowSettings settings;
    settings.width  = 320;
    settings.height = 180;
    REQUIRE(window->setWindowSettings(settings));
    auto* compute = eve::gpgpu::Gpgpu::create();
    using namespace eve::physics::field::gpu;
    std::vector<GpuSource> sources(count);
    for (int i = 0; i < count; ++i) {
        sources[i].position = {float((i * 37) % 1023 - 511) / 512.f, float((i * 71) % 1023 - 511) / 512.f,
                               float((i * 113) % 1023 - 511) / 512.f};
        sources[i].mass     = 1.f / count;
    }
    std::unique_ptr<eve::gpgpu::GpuBuffer> sourceRows(compute->newBuffer(static_cast<int>(count * sizeof(GpuSource))));
    REQUIRE(sourceRows != nullptr);
    sourceRows->uploadBytes(sources.data(), sources.size() * sizeof(GpuSource));
    auto prepared = GpuMortonKeys::prepare(*compute, *graphics, sources.size(), {{-1.f, -1.f, -1.f}, {1.f, 1.f, 1.f}});
    REQUIRE(prepared.ok());
    auto                keys = std::move(prepared).takeValue();
    std::vector<double> milliseconds, sortMilliseconds;
    milliseconds.reserve(frames);
    sortMilliseconds.reserve(frames);
    for (int frame = 0; frame < frames; ++frame) {
        const auto start = std::chrono::steady_clock::now();
        REQUIRE(keys->update(*sourceRows).ok());
        milliseconds.push_back(
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
        const auto sortStart = std::chrono::steady_clock::now();
        REQUIRE(keys->sort().ok());
        sortMilliseconds.push_back(
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - sortStart).count());
    }
    auto snapshot = keys->snapshot();
    REQUIRE(snapshot.ok());
    for (int index = 0; index < count; ++index) {
        const auto& row = snapshot.value()[index];
        CHECK(row.sourceIndex < static_cast<std::uint32_t>(count));
        if (row.sourceIndex < static_cast<std::uint32_t>(count))
            CHECK(row.key == expectedKey(sources[row.sourceIndex].position));
        if (index != 0) CHECK(snapshot.value()[index - 1].key <= row.key);
    }
    if (million) {
        const double first = milliseconds.front();
        std::sort(milliseconds.begin(), milliseconds.end());
        std::sort(sortMilliseconds.begin(), sortMilliseconds.end());
        std::cout << "GPU_MORTON_BENCH sources=" << count << " frames=" << frames << " first_ms=" << first
                  << " p50_ms=" << milliseconds[59] << " p95_ms=" << milliseconds[113]
                  << " p99_ms=" << milliseconds[118] << " max_ms=" << milliseconds.back()
                  << " sort_p50_ms=" << sortMilliseconds[59] << " sort_p95_ms=" << sortMilliseconds[113]
                  << " sort_p99_ms=" << sortMilliseconds[118] << " sort_max_ms=" << sortMilliseconds.back() << '\n';
    }
}
