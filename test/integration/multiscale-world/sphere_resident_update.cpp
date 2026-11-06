// clang-format off
/* eve-test-policy:
{"schema":"evengine.test-policy","version":1,"default":"render-window","module":"graphics_multiscale"}
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
#include "gpgpu/GpuBuffer.h"
#include "graphics/Graphics.h"
#include "graphics/multiscale/AngularView.h"
#include "graphics/multiscale/SphereView.h"
#include "spatial/multiscale/CellPosition.h"
#include "window/Window.h"
#include "zeroerr/unittest.h"

TEST_CASE("graphics.multiscale.sphereBatchAcceptsResidentPackets") {
    auto*                       window   = eve::window::Window::create();
    auto*                       graphics = eve::graphics::Graphics::create();
    eve::window::WindowSettings settings;
    settings.width  = 320;
    settings.height = 180;
    REQUIRE(window->setWindowSettings(settings));
    graphics->setScreenReadbackEnabled(true);
    auto* compute = eve::gpgpu::Gpgpu::create();
    using namespace eve::graphics::multiscale;
    std::array<eve::spatial::CellPosition, 1> centers{};
    centers[0].cell[2] = 10;
    const std::array<SphereAppearance, 1> appearance{{{2, 900, {.1f, .1f, .8f}}}};
    AngularView                           view;
    view.cellExponent = 900;
    view.aspect       = 320.0 / 180.0;
    auto frame        = SphereFrame::build(centers, appearance, {}, view, 1);
    REQUIRE(frame.ok());
    auto built = SphereBatch::prepare(*graphics, frame.value());
    REQUIRE(built.ok());
    auto batch = std::move(built).takeValue();
    REQUIRE(batch->draw().ok());
    graphics->present();
    const auto blue = graphics->getPixel(160, 90);
    REQUIRE(blue.b > blue.r + .3f);

    const std::array<float, 16>            residentSphere{0,   0,   0.625f, 0.125f, 320.f / 180.f, 1,    904, 0,
                                                          .8f, .1f, .1f,    0,      -.2f,          -.3f, .2f, .3f};
    std::unique_ptr<eve::gpgpu::GpuBuffer> buffer(compute->newBuffer(int(sizeof(residentSphere))));
    REQUIRE(buffer != nullptr);
    buffer->uploadBytes(residentSphere.data(), sizeof(residentSphere));
    const auto resident  = buffer->residentView();
    auto       shortView = resident;
    shortView.sizeBytes -= 16;
    REQUIRE(!batch->updateResidentPackets(shortView, 1).ok());
    REQUIRE(!batch->updateResidentPackets(resident, 2).ok());
    REQUIRE(batch->updateResidentPackets(resident, 1).ok());
    REQUIRE(batch->draw().ok());
    graphics->present();
    const auto red = graphics->getPixel(160, 90);
    REQUIRE(red.r > red.b + .3f);
    REQUIRE(batch->draw().ok());
    REQUIRE(!batch->updateResidentPackets(resident, 1).ok());  // Active pass rejects before writes.
    graphics->present();
    REQUIRE(std::abs(graphics->getPixel(160, 90).r - red.r) < .01f);
}

TEST_CASE("graphics.multiscale.residentMillionFrameTiming") {
    const bool                  million  = std::getenv("EVE_RESIDENT_SPHERE_BENCH") != nullptr;
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
    using namespace eve::graphics::multiscale;
    std::vector<eve::spatial::CellPosition> centers(count);
    std::vector<SphereAppearance>           appearance(count, {.25 / side, 900, {.1f, .1f, .8f}});
    std::vector<std::array<float, 16>>      packets(count);
    for (int i = 0; i < count; ++i) {
        centers[i].cell[2]     = 1;
        centers[i].fraction[0] = -.45 + .9 * (i % side + .5) / side;
        centers[i].fraction[1] = -.45 + .9 * (i / side + .5) / side;
        const float x          = -0.9f + 1.8f * float(i % side) / side;
        const float y          = -0.9f + 1.8f * float(i / side) / side;
        packets[i] = {x,         y,         .625f,     .001f,    float(width) / height, 1, 904, 0, .1f, .1f, .8f, 0,
                      x - .002f, y - .002f, x + .002f, y + .002f};
    }
    AngularView view;
    view.cellExponent = 900;
    view.aspect       = double(width) / height;
    auto frame        = SphereFrame::build(centers, appearance, {}, view, count);
    REQUIRE(frame.ok());
    auto built = SphereBatch::prepare(*graphics, frame.value());
    REQUIRE(built.ok());
    auto                                   batch = std::move(built).takeValue();
    std::unique_ptr<eve::gpgpu::GpuBuffer> buffer(compute->newBuffer(int(packets.size() * sizeof(packets[0]))));
    REQUIRE(buffer != nullptr);
    buffer->uploadBytes(packets.data(), packets.size() * sizeof(packets[0]));
    const auto resident = buffer->residentView();
    for (int warmup = 0; warmup < (million ? 4 : 1); ++warmup) {
        REQUIRE(batch->updateResidentPackets(resident, count).ok());
        REQUIRE(batch->draw().ok());
        graphics->present();
    }
    std::vector<double> milliseconds;
    milliseconds.reserve(frames);
    for (int i = 0; i < frames; ++i) {
        const auto start = std::chrono::steady_clock::now();
        REQUIRE(batch->updateResidentPackets(resident, count).ok());
        REQUIRE(batch->draw().ok());
        graphics->present();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
        }
        milliseconds.push_back(
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    }
    std::sort(milliseconds.begin(), milliseconds.end());
    const auto* gpu = eve::cap::query<eve::caps::IGpuInfo>();
    if (million)
        std::cout << "RESIDENT_SPHERE_BENCH gpu=" << (gpu ? gpu->gpuName() : "unknown")
                  << " width=" << graphics->getPixelWidth() << " height=" << graphics->getPixelHeight()
                  << " objects=" << count << " frames=" << frames << " p50_ms=" << milliseconds[59]
                  << " p95_ms=" << milliseconds[113] << " p99_ms=" << milliseconds[118]
                  << " max_ms=" << milliseconds.back() << '\n';
}
