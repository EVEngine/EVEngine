// clang-format off
/* eve-test-policy:
{"schema":"evengine.test-policy","version":1,"default":"render-window","module":"physics_field_gpu"}
*/
// clang-format on
#include <SDL2/SDL.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>
#include "common/Capability.h"
#include "common/GpuInfo.h"
#include "gpgpu/Gpgpu.h"
#include "graphics/Graphics.h"
#include "graphics/multiscale/AngularView.h"
#include "graphics/multiscale/SphereView.h"
#include "physics/field/gpu/GpuGravitySession.h"
#include "spatial/multiscale/CellPosition.h"
#include "window/Window.h"
#include "zeroerr/unittest.h"

TEST_CASE("physics.field.gpu.publishedTargetsReachSpherePixels") {
    auto*                       window   = eve::window::Window::create();
    auto*                       graphics = eve::graphics::Graphics::create();
    eve::window::WindowSettings settings;
    settings.width  = 320;
    settings.height = 180;
    REQUIRE(window->setWindowSettings(settings));
    graphics->setScreenReadbackEnabled(true);
    auto* compute = eve::gpgpu::Gpgpu::create();
    using namespace eve::physics::field::gpu;
    std::array<GpuSource, 8> sources{};
    for (int i = 0; i < 8; ++i)
        sources[i] = {{{(i & 1) ? .5f : -.5f, (i & 2) ? .5f : -.5f, (i & 4) ? .5f : -.5f}}, .125f};
    const std::array<GpuMotion, 1>        targets{{{{0.f, 0.f, 10.f}, {0.f, 0.f, 0.f}}}};
    const auto                            dt = eve::Duration::fromNanoseconds(16666667);
    const eve::physics::field::FieldStamp stamp{eve::SimulationTick{0}, eve::Revision{0}};
    auto created = GpuGravitySession::create(*compute, *graphics, sources, targets, stamp, dt, {});
    REQUIRE(created.ok());
    auto session = std::move(created).takeValue();

    std::array<eve::spatial::CellPosition, 1> centers{};
    centers[0].cell[2] = 10;
    const std::array<eve::graphics::multiscale::SphereAppearance, 1> appearances{{{2, 900, {.1f, .1f, .8f}}}};
    eve::graphics::multiscale::AngularView                           view;
    view.cellExponent = 900;
    view.aspect       = 320.0 / 180.0;
    auto frame        = eve::graphics::multiscale::SphereFrame::build(centers, appearances, {}, view, 1);
    REQUIRE(frame.ok());
    auto prepared = eve::graphics::multiscale::SphereBatch::prepare(*graphics, frame.value());
    REQUIRE(prepared.ok());
    auto                batch = std::move(prepared).takeValue();
    GpuSphereProjection projection;
    projection.tangentX = 320.f / 180.f;
    projection.radius   = .25f;
    projection.color    = {.8f, .1f, .1f};
    REQUIRE(session->updateSphereBatch(*batch, projection).ok());
    REQUIRE(batch->draw().ok());
    graphics->present();
    const auto red = graphics->getPixel(160, 90);
    REQUIRE(red.r > red.b + .3f);

    auto next = session->prepareStep({eve::SimulationTick{1}, dt}, 0.f);
    REQUIRE(next.ok());
    REQUIRE(session->commit(std::move(next).takeValue()).ok());
    projection.color = {.1f, .1f, .8f};
    REQUIRE(session->updateSphereBatch(*batch, projection).ok());
    REQUIRE(batch->draw().ok());
    graphics->present();
    const auto blue = graphics->getPixel(160, 90);
    REQUIRE(blue.b > blue.r + .3f);
    projection.camera = {0.f, 0.f, 10.f};
    REQUIRE(!session->updateSphereBatch(*batch, projection).ok());
    REQUIRE(batch->draw().ok());
    graphics->present();
    REQUIRE(std::abs(graphics->getPixel(160, 90).b - blue.b) < .01f);
}

TEST_CASE("physics.field.gpu.simulationProjectsAndRendersEveryTarget") {
    const bool                  million  = std::getenv("EVE_GPU_FIELD_RENDER_BENCH") != nullptr;
    const int                   side     = million ? 1000 : 4;
    const int                   count    = side * side;
    const int                   frames   = million ? 120 : 2;
    const int                   width    = million ? 1920 : 320;
    const int                   height   = million ? 1080 : 180;
    auto*                       window   = eve::window::Window::create();
    auto*                       graphics = eve::graphics::Graphics::create();
    eve::window::WindowSettings settings;
    settings.width  = static_cast<std::uint16_t>(width);
    settings.height = static_cast<std::uint16_t>(height);
    REQUIRE(window->setWindowSettings(settings));
    graphics->setVSync(false);
    if (million) SDL_HideWindow(static_cast<SDL_Window*>(window->getHandle()));
    auto* compute = eve::gpgpu::Gpgpu::create();
    using namespace eve::physics::field::gpu;
    std::vector<GpuSource>                                   sources(count);
    std::vector<GpuMotion>                                   targets(count);
    std::vector<eve::spatial::CellPosition>                  centers(count);
    std::vector<eve::graphics::multiscale::SphereAppearance> appearances(count, {.25 / side, 900, {.1f, .1f, .8f}});
    for (int i = 0; i < count; ++i) {
        const int   cell   = i & 7;
        const float jitter = (float((i * 37) % 101) / 100.f - .5f) * .02f;
        sources[i]         = {
            {{(cell & 1) ? .5f + jitter : -.5f + jitter, (cell & 2) ? .5f - jitter * .7f : -.5f - jitter * .7f,
              (cell & 4) ? .5f + jitter * .5f : -.5f + jitter * .5f}},
            1.f / count};
        const float x          = -4.5f + 9.f * (float(i % side) + .5f) / side;
        const float y          = -4.5f + 9.f * (float(i / side) + .5f) / side;
        targets[i].position    = {x, y, 10.f};
        centers[i].cell[2]     = 1;
        centers[i].fraction[0] = -.45 + .9 * (double(i % side) + .5) / side;
        centers[i].fraction[1] = -.45 + .9 * (double(i / side) + .5) / side;
    }
    const auto                            dt = eve::Duration::fromNanoseconds(16666667);
    const eve::physics::field::FieldStamp stamp{eve::SimulationTick{0}, eve::Revision{0}};
    auto created = GpuGravitySession::create(*compute, *graphics, sources, targets, stamp, dt, {});
    REQUIRE(created.ok());
    auto                                   session = std::move(created).takeValue();
    eve::graphics::multiscale::AngularView view;
    view.cellExponent = 900;
    view.aspect       = double(width) / height;
    auto frame        = eve::graphics::multiscale::SphereFrame::build(centers, appearances, {}, view, count);
    REQUIRE(frame.ok());
    auto prepared = eve::graphics::multiscale::SphereBatch::prepare(*graphics, frame.value());
    REQUIRE(prepared.ok());
    auto                             batch = std::move(prepared).takeValue();
    std::vector<GpuSphereAppearance> gpuAppearances(count);
    for (int i = 0; i < count; ++i) {
        gpuAppearances[i].radius = (i & 1) ? .002f : .001f;
        gpuAppearances[i].color  = (i & 1) ? std::array<float, 3>{.1f, .1f, .8f} : std::array<float, 3>{.8f, .1f, .1f};
    }
    REQUIRE(session->setSphereAppearances(gpuAppearances).ok());
    GpuSphereProjection projection;
    projection.tangentX       = float(width) / height;
    projection.radius         = million ? .001f : .25f;
    projection.color          = {.8f, .1f, .1f};
    double warmupMilliseconds = 0;
    if (million) {
        const auto warmupStart = std::chrono::steady_clock::now();
        REQUIRE(session->updateSphereBatch(*batch, projection).ok());
        REQUIRE(batch->draw().ok());
        graphics->present();
        warmupMilliseconds =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - warmupStart).count();
    }
    std::vector<double> milliseconds;
    milliseconds.reserve(frames);
    std::vector<double> editMilliseconds;
    editMilliseconds.reserve(4);
    std::array<double, 5> firstStages{};
    for (int i = 1; i <= frames; ++i) {
        const auto start = std::chrono::steady_clock::now();
        auto       next  = session->prepareStep({eve::SimulationTick{static_cast<std::uint64_t>(i)}, dt}, 0.f);
        REQUIRE(next.ok());
        REQUIRE(session->commit(std::move(next).takeValue()).ok());
        const auto simulated = std::chrono::steady_clock::now();
        if (million && i % 30 == 0) {
            const std::array<GpuSphereAppearanceEdit, 1> edit{
                {{static_cast<std::uint32_t>(count - 1),
                  {.002f, 904.f,
                   (i / 30) & 1 ? std::array<float, 3>{.1f, .8f, .1f} : std::array<float, 3>{.1f, .1f, .8f}}}}};
            REQUIRE(session->editSphereAppearances(edit).ok());
        }
        const auto edited = std::chrono::steady_clock::now();
        REQUIRE(session->updateSphereBatch(*batch, projection).ok());
        const auto projected = std::chrono::steady_clock::now();
        REQUIRE(batch->draw().ok());
        const auto drawn = std::chrono::steady_clock::now();
        graphics->present();
        const auto presented = std::chrono::steady_clock::now();
        SDL_Event  event;
        while (SDL_PollEvent(&event)) {
        }
        milliseconds.push_back(
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
        if (million && i % 30 == 0) editMilliseconds.push_back(milliseconds.back());
        if (million && i == 1) {
            firstStages = {std::chrono::duration<double, std::milli>(simulated - start).count(),
                           std::chrono::duration<double, std::milli>(edited - simulated).count(),
                           std::chrono::duration<double, std::milli>(projected - edited).count(),
                           std::chrono::duration<double, std::milli>(drawn - projected).count(),
                           std::chrono::duration<double, std::milli>(presented - drawn).count()};
        }
    }
    REQUIRE(session->stamp().tick == eve::SimulationTick{static_cast<std::uint64_t>(frames)});
    if (million) {
        const auto worstFrame =
            static_cast<int>(std::max_element(milliseconds.begin(), milliseconds.end()) - milliseconds.begin()) + 1;
        const double firstFrame = milliseconds.front();
        std::sort(milliseconds.begin(), milliseconds.end());
        const auto* gpu = eve::cap::query<eve::caps::IGpuInfo>();
        std::cout << "GPU_FIELD_RENDER_BENCH gpu=" << (gpu ? gpu->gpuName() : "unknown")
                  << " width=" << graphics->getPixelWidth() << " height=" << graphics->getPixelHeight()
                  << " sources=" << count << " targets=" << count << " frames=" << frames
                  << " p50_ms=" << milliseconds[59] << " p95_ms=" << milliseconds[113]
                  << " p99_ms=" << milliseconds[118] << " max_ms=" << milliseconds.back()
                  << " worst_frame=" << worstFrame << " first_frame_ms=" << firstFrame << " edit_frame_ms=";
        for (double value : editMilliseconds) std::cout << value << ',';
        std::cout << " warmup_ms=" << warmupMilliseconds << " first_stages_ms=";
        for (double value : firstStages) std::cout << value << ',';
        std::cout << '\n';
    }
}

TEST_CASE("physics.field.gpu.individualSphereAppearanceSurvivesFailedReplacement") {
    auto*                       window   = eve::window::Window::create();
    auto*                       graphics = eve::graphics::Graphics::create();
    eve::window::WindowSettings settings;
    settings.width  = 320;
    settings.height = 180;
    REQUIRE(window->setWindowSettings(settings));
    graphics->setScreenReadbackEnabled(true);
    auto* compute = eve::gpgpu::Gpgpu::create();
    using namespace eve::physics::field::gpu;
    std::array<GpuSource, 8> sources{};
    for (int i = 0; i < 8; ++i)
        sources[i] = {{{(i & 1) ? .5f : -.5f, (i & 2) ? .5f : -.5f, (i & 4) ? .5f : -.5f}}, .125f};
    const std::array<GpuMotion, 2>        targets{{{{-2.f, 0.f, 10.f}, {}}, {{2.f, 0.f, 10.f}, {}}}};
    const auto                            dt = eve::Duration::fromNanoseconds(16666667);
    const eve::physics::field::FieldStamp stamp{eve::SimulationTick{0}, eve::Revision{0}};
    auto created = GpuGravitySession::create(*compute, *graphics, sources, targets, stamp, dt, {});
    REQUIRE(created.ok());
    auto                                      session = std::move(created).takeValue();
    std::array<eve::spatial::CellPosition, 2> centers{};
    for (auto& center : centers) center.cell[2] = 10;
    const std::array<eve::graphics::multiscale::SphereAppearance, 2> appearances{
        {{2, 900, {.1f, .1f, .8f}}, {2, 900, {.1f, .1f, .8f}}}};
    eve::graphics::multiscale::AngularView view;
    view.cellExponent = 900;
    view.aspect       = 320.0 / 180.0;
    auto frame        = eve::graphics::multiscale::SphereFrame::build(centers, appearances, {}, view, 2);
    REQUIRE(frame.ok());
    auto prepared = eve::graphics::multiscale::SphereBatch::prepare(*graphics, frame.value());
    REQUIRE(prepared.ok());
    auto                               batch = std::move(prepared).takeValue();
    std::array<GpuSphereAppearance, 2> perTarget{{{.4f, 904.f, {.8f, .1f, .1f}}, {.6f, 904.f, {.1f, .1f, .8f}}}};
    REQUIRE(session->setSphereAppearances(perTarget).ok());
    perTarget[1].radius = -1.f;
    REQUIRE(!session->setSphereAppearances(perTarget).ok());
    GpuSphereProjection projection;
    projection.tangentX = 320.f / 180.f;
    REQUIRE(session->updateSphereBatch(*batch, projection).ok());
    REQUIRE(batch->draw().ok());
    graphics->present();
    const auto left  = graphics->getPixel(142, 90);
    const auto right = graphics->getPixel(178, 90);
    REQUIRE(left.r > left.b + .3f);
    REQUIRE(right.b > right.r + .3f);
    const std::array<GpuSphereAppearanceEdit, 1> edit{{{1, {.6f, 904.f, {.1f, .8f, .1f}}}}};
    REQUIRE(session->editSphereAppearances(edit).ok());
    REQUIRE(session->updateSphereBatch(*batch, projection).ok());
    REQUIRE(batch->draw().ok());
    graphics->present();
    const auto editedLeft  = graphics->getPixel(142, 90);
    const auto editedRight = graphics->getPixel(178, 90);
    REQUIRE(editedLeft.r > editedLeft.g + .3f);
    REQUIRE(editedRight.g > editedRight.b + .3f);
    const std::array<GpuSphereAppearanceEdit, 2> unsorted{
        {{1, {.6f, 904.f, {.8f, .1f, .1f}}}, {0, {.4f, 904.f, {.1f, .1f, .8f}}}}};
    REQUIRE(!session->editSphereAppearances(unsorted).ok());
    REQUIRE(session->updateSphereBatch(*batch, projection).ok());
    REQUIRE(batch->draw().ok());
    graphics->present();
    REQUIRE(graphics->getPixel(178, 90).g > graphics->getPixel(178, 90).b + .3f);
}
