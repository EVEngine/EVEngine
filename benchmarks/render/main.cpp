#include "common/Capability.h"
#include "common/CrashHandler.h"
#include "common/Exception.h"
#include "common/GpuTimer.h"
#include "graphics/Color.h"
#include "graphics/Graphics.h"
#include "graphics/Light.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/Texture.h"
#include "window/Window.h"

#if defined(EVENGINE_BENCH_PARTICLES)
#include "particles/ParticleEmitter.h"
#include "particles/ParticleSystem.h"
#include "particles/Particles.h"
#endif

#include <SDL2/SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using eve::graphics::Camera2D;
using eve::graphics::Camera3D;
using eve::graphics::Color;
using eve::graphics::Graphics;
using eve::graphics::Light3D;
using eve::graphics::Mesh;
using eve::graphics::Renderable2D;
using eve::graphics::Renderable3D;
using eve::graphics::RenderSystem;
using eve::graphics::RenderSystem3D;
using eve::graphics::Texture;

std::size_t parsePositive(char *text, std::size_t fallback) {
    if (text == nullptr) return fallback;
    char *end = nullptr;
    const unsigned long long value = std::strtoull(text, &end, 10);
    return end != text && *end == '\0' && value > 0 ? std::size_t(value) : fallback;
}

int parseEnvFrames(int fallback) {
    const char *env = std::getenv("EVENGINE_PERF_FRAMES");
    if (!env || !env[0]) return fallback;
    const int n = std::atoi(env);
    return n > 8 ? n : 8;
}

void pumpEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
    }
}

void hideScene() {
    if (ecs::current()->getManager<Renderable3D>() != nullptr) {
        auto view = ecs::View<Renderable3D, Renderable3D::MeshRenderer>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [mr] = *it;
            mr->visible = false;
        }
    }
    if (ecs::current()->getManager<Camera3D>() != nullptr) {
        auto camView = ecs::View<Camera3D, Camera3D::Data>();
        for (auto it = camView.begin(); it != camView.end(); ++it) {
            auto [data] = *it;
            data->active = false;
        }
    }
    if (ecs::current()->getManager<Light3D>() != nullptr) {
        auto lightView = ecs::View<Light3D, Light3D::Data>();
        for (auto it = lightView.begin(); it != lightView.end(); ++it) {
            auto [d] = *it;
            d->enabled = false;
            d->castShadow = false;
        }
    }
    if (ecs::current()->getManager<Renderable2D>() != nullptr) {
        auto view = ecs::View<Renderable2D, Renderable2D::Sprite>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [sp] = *it;
            sp->visible = false;
        }
    }
    if (ecs::current()->getManager<Camera2D>() != nullptr) {
        auto camView = ecs::View<Camera2D, Camera2D::Data>();
        for (auto it = camView.begin(); it != camView.end(); ++it) {
            auto [data] = *it;
            data->active = false;
        }
    }
#if defined(EVENGINE_BENCH_PARTICLES)
    if (ecs::current()->getManager<eve::particles::ParticleEmitter>() != nullptr) {
        auto view = ecs::View<eve::particles::ParticleEmitter, eve::particles::ParticleEmitter::Draw>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [draw] = *it;
            draw->visible = false;
        }
    }
#endif
}

Texture *makeSolid(Graphics *gfx, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    const std::uint8_t px[4] = {r, g, b, 255};
    return gfx->newTexture(1, 1, px);
}

int gridSide(std::size_t count) {
    return std::max(1, static_cast<int>(std::ceil(std::sqrt(double(count)))));
}

void spawnSprites(Graphics *gfx, Texture *tex, std::size_t count) {
    auto *cam = Camera2D::createCamera();
    cam->setPosition(float(gfx->getWidth()) * 0.5f, float(gfx->getHeight()) * 0.5f);
    const int side = gridSide(count);
    const float cell = 18.f;
    const float originX = float(gfx->getWidth()) * 0.5f - float(side) * cell * 0.5f;
    const float originY = float(gfx->getHeight()) * 0.5f - float(side) * cell * 0.5f;
    for (std::size_t i = 0; i < count; ++i) {
        auto *sprite = Renderable2D::create();
        sprite->setTexture(tex);
        sprite->setSize(16.f, 16.f);
        sprite->setPosition(originX + float(i % std::size_t(side)) * cell,
                            originY + float(i / std::size_t(side)) * cell);
        sprite->setColor(0.85f, 0.9f, 1.f, 1.f);
        sprite->setReceiveLight(false);
    }
}

void spawnCubes(Mesh *mesh, Texture *tex, std::size_t count, bool lit, bool shadows) {
    const int side = gridSide(count);
    const float spacing = 1.6f;
    const float origin = -float(side - 1) * spacing * 0.5f;
    auto *cam = Camera3D::createCamera();
    const float span = float(side) * spacing;
    cam->setEye(0.f, span * 0.85f, span * 1.15f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->data()->nearZ = 0.1f;
    cam->data()->farZ = std::max(80.f, span * 4.f);
    cam->data()->ambientR = lit ? 0.08f : 0.55f;
    cam->data()->ambientG = lit ? 0.09f : 0.55f;
    cam->data()->ambientB = lit ? 0.11f : 0.6f;
    if (lit) {
        auto *light = Light3D::createLight("dir");
        light->setDirection(0.35f, 1.f, 0.25f);
        light->setColor(1.f, 0.97f, 0.92f, 2.4f);
        light->setCastShadow(shadows);
    } else {
        RenderSystem3D::setDirectionalLight(0.f, 1.f, 0.f, 0.f, 0.f, 0.f);
    }
    for (std::size_t i = 0; i < count; ++i) {
        auto *ent = Renderable3D::create();
        ent->setMesh(mesh);
        ent->setTexture(tex);
        ent->setPosition(origin + float(i % std::size_t(side)) * spacing, 0.f,
                         origin + float(i / std::size_t(side)) * spacing);
        ent->setMetallic(0.05f);
        ent->setRoughness(0.55f);
        if (shadows) ent->setCastShadow(true);
    }
}

struct FrameStats {
    std::vector<double> cpuMs;
    std::vector<double> gpuMs;
};

void recordGpu(FrameStats &stats) {
    if (auto *timer = eve::cap::query<eve::service::IGpuTimer>()) {
        if (timer->gpuTimingAvailable()) stats.gpuMs.push_back(double(timer->gpuFrameMs()));
    }
}

template <typename Fn>
FrameStats measureFrames(int warmup, int timed, Fn &&draw) {
    FrameStats stats;
    stats.cpuMs.reserve(std::size_t(timed));
    for (int i = 0; i < warmup; ++i) {
        draw();
        pumpEvents();
    }
    using Clock = std::chrono::steady_clock;
    for (int i = 0; i < timed; ++i) {
        const auto started = Clock::now();
        draw();
        pumpEvents();
        stats.cpuMs.push_back(std::chrono::duration<double, std::milli>(Clock::now() - started).count());
        recordGpu(stats);
    }
    return stats;
}

double percentile(std::vector<double> values, int pct) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const std::size_t index =
        std::min(values.size() - 1, (values.size() * std::size_t(pct) + 99) / 100 - 1);
    return values[index];
}

double mean(const std::vector<double> &values) {
    if (values.empty()) return 0.0;
    double sum = 0.0;
    for (double v : values) sum += v;
    return sum / double(values.size());
}

void printJson(std::string_view workload, std::size_t items, int frames, int width, int height,
               bool vsync, std::string_view backend, const FrameStats &stats) {
    const double p50 = percentile(stats.cpuMs, 50);
    const double p95 = percentile(stats.cpuMs, 95);
    const double avgMs = mean(stats.cpuMs);
    const double avgFps = avgMs > 0.0 ? 1000.0 / avgMs : 0.0;
    const double gpuP50 = percentile(stats.gpuMs, 50);
    const double gpuP95 = percentile(stats.gpuMs, 95);
    std::cout << "RENDER_BENCHMARK_JSON={\"workload\":\"" << workload << "\",\"items\":" << items
              << ",\"frames\":" << frames << ",\"width\":" << width << ",\"height\":" << height
              << ",\"vsync\":" << (vsync ? 1 : 0) << ",\"backend\":\"" << backend
              << "\",\"p50Ms\":" << p50 << ",\"p95Ms\":" << p95 << ",\"avgMs\":" << avgMs
              << ",\"avgFps\":" << avgFps << ",\"gpuSamples\":" << stats.gpuMs.size()
              << ",\"gpuP50Ms\":" << gpuP50 << ",\"gpuP95Ms\":" << gpuP95 << "}\n";
}

void usage() {
    std::cerr
        << "Usage: render_benchmark [workload] [frames] [items] [width] [height]\n"
           "  workload: all|clear|2d-rects|2d-textured|2d-sprites|3d-cubes|3d-lit|3d-shadows"
#if defined(EVENGINE_BENCH_PARTICLES)
           "|gpu-particles"
#endif
           "\n"
           "  EVENGINE_PERF_FRAMES overrides [frames] (default 60).\n"
           "  Build with -DEVENGINE_BUILD_RENDER_BENCHMARK=ON.\n";
}

std::size_t defaultItems(std::string_view workload, std::size_t requested) {
    if (requested > 0) return requested;
    if (workload.rfind("3d-", 0) == 0) return 1024;
    if (workload == "gpu-particles") return 16384;
    if (workload == "clear") return 0;
    return 10000;
}

int runWorkload(Graphics *gfx, std::string_view workload, int frames, std::size_t requestedItems) {
    hideScene();
    gfx->setVSync(false);
    gfx->setScreenReadbackEnabled(false);
    gfx->setBackgroundColor(Color(0.08f, 0.09f, 0.12f, 1.f));
    const std::size_t items = defaultItems(workload, requestedItems);
    const int warmup = 20;
    Texture *white = makeSolid(gfx, 230, 230, 240);
    Texture *tint = makeSolid(gfx, 180, 90, 40);
    Mesh *cube = gfx->newMeshCube(1.f);

    FrameStats stats;
    if (workload == "clear") {
        stats = measureFrames(warmup, frames, [&] {
            gfx->clearScreen();
            gfx->present();
        });
    } else if (workload == "2d-rects") {
        stats = measureFrames(warmup, frames, [&] {
            gfx->clearScreen();
            const int side = gridSide(items);
            const float cell = 12.f;
            for (std::size_t i = 0; i < items; ++i) {
                const float x = float(i % std::size_t(side)) * cell;
                const float y = float(i / std::size_t(side)) * cell;
                gfx->drawSolidRect(x, y, 10.f, 10.f, 0.95f, 0.45f, 0.2f, 1.f);
            }
            gfx->present();
        });
    } else if (workload == "2d-textured") {
        stats = measureFrames(warmup, frames, [&] {
            gfx->clearScreen();
            const int side = gridSide(items);
            const float cell = 12.f;
            for (std::size_t i = 0; i < items; ++i) {
                const float x = float(i % std::size_t(side)) * cell;
                const float y = float(i / std::size_t(side)) * cell;
                gfx->drawTexturedRect(white, x, y, 10.f, 10.f, 1.f, 1.f, 1.f, 1.f);
            }
            gfx->present();
        });
    } else if (workload == "2d-sprites") {
        spawnSprites(gfx, white, items);
        stats = measureFrames(warmup, frames, [&] { RenderSystem::render(*gfx); });
    } else if (workload == "3d-cubes" || workload == "3d-lit" || workload == "3d-shadows") {
        spawnCubes(cube, tint, items, workload != "3d-cubes", workload == "3d-shadows");
        stats = measureFrames(warmup, frames, [&] {
            RenderSystem3D::render(*gfx);
            RenderSystem::render(*gfx);
        });
    }
#if defined(EVENGINE_BENCH_PARTICLES)
    else if (workload == "gpu-particles") {
        if (!gfx->supportsGpuParticles()) {
            std::cerr << "gpu-particles skipped: backend has no resident GPU particles\n";
            return 0;
        }
        auto *particles = eve::particles::Particles::create();
        auto *emitter = particles->newEmitter(int(std::max<std::size_t>(items, 1)));
        emitter->setTexture(white);
        emitter->setGpuSimulation(true);
        emitter->setPosition(float(gfx->getWidth()) * 0.5f, float(gfx->getHeight()) * 0.5f);
        emitter->setEmissionRate(0.f);
        emitter->setParticleLifetime(8.f, 8.f);
        emitter->setParticleSize(6.f, 10.f);
        emitter->setSpeed(40.f, 120.f);
        emitter->setSpread(6.2831853f);
        emitter->setBlendMode("additive");
        emitter->emit(int(items));
        stats = measureFrames(warmup, frames, [&] {
            eve::particles::ParticleSimSystem::update(1.f / 60.f);
            gfx->clearScreen();
            eve::particles::ParticleRenderSystem::render(gfx);
            gfx->present();
        });
    }
#endif
    else {
        usage();
        return 2;
    }

    printJson(workload, items, frames, gfx->getWidth(), gfx->getHeight(), gfx->isVSync(),
              gfx->getBackendName(), stats);
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
#if defined(EVENGINE_WINDOWS) || defined(_WIN32)
    eve::installCrashHandler();
#endif
    const std::string_view workload = argc > 1 ? std::string_view(argv[1]) : "all";
    if (workload == "-h" || workload == "--help") {
        usage();
        return 0;
    }
    const int frames = parseEnvFrames(int(parsePositive(argc > 2 ? argv[2] : nullptr, 60)));
    const std::size_t items = parsePositive(argc > 3 ? argv[3] : nullptr, 0);
    const int width = int(parsePositive(argc > 4 ? argv[4] : nullptr, 1280));
    const int height = int(parsePositive(argc > 5 ? argv[5] : nullptr, 720));

    try {
        auto *win = eve::window::Window::create();
        auto *gfx = Graphics::create();
        if (!win || !gfx) {
            std::cerr << "failed to create Window/Graphics modules\n";
            return 1;
        }
        eve::window::WindowSettings settings;
        settings.width = static_cast<std::uint16_t>(width);
        settings.height = static_cast<std::uint16_t>(height);
        settings.centered = true;
        settings.vsync = 0;
        settings.resizable = false;
        if (!win->setWindowSettings(settings)) {
            std::cerr << "failed to open benchmark window\n";
            return 1;
        }
        win->setWindowTitle("EVEngine render benchmark");
        gfx->setVSync(false);

        int status = 0;
        if (workload == "all") {
            const char *names[] = {"clear",       "2d-rects", "2d-textured", "2d-sprites",
                                   "3d-cubes",    "3d-lit",   "3d-shadows",
#if defined(EVENGINE_BENCH_PARTICLES)
                                   "gpu-particles"
#endif
            };
            for (const char *name : names) {
                const int rc = runWorkload(gfx, name, frames, items);
                if (rc != 0) status = rc;
            }
        } else {
            status = runWorkload(gfx, workload, frames, items);
        }
        win->close();
        return status;
    } catch (const eve::Exception &ex) {
        std::cerr << "render benchmark failed: " << ex.what() << '\n';
        return 1;
    }
}
