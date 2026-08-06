#include "particles/ParticleSystem.h"
#include "particles/ParticleEmitter.h"
#include "particles/ParticleConfig.h"
#include "graphics/Graphics.h"
#include "graphics/RenderSystem.h"
#include "graphics/Canvas.h"
#include "filesystem/Filesystem.h"
#include "common/Module.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(EVENGINE_ANDROID)
#include <android/log.h>
#endif

namespace eve::particles {

namespace {

float clampZoom(float z) { return z <= 0.f ? 1e-4f : z; }

struct ViewCam {
    float x = 0.f;
    float y = 0.f;
    float zoom = 1.f;
    bool valid = false;
};

ViewCam fromEntity(graphics::Camera2D *ent) {
    ViewCam v;
    if (!ent) return v;
    auto d = ent->data();
    v.valid = true;
    v.x = d->x;
    v.y = d->y;
    v.zoom = d->zoom;
    return v;
}

void applyCamera(float wx, float wy, float ww, float wh, const ViewCam &cam, int viewW, int viewH,
                 float &sx, float &sy, float &sw, float &sh) {
    if (!cam.valid) {
        sx = wx;
        sy = wy;
        sw = ww;
        sh = wh;
        return;
    }
    const float z = clampZoom(cam.zoom);
    sx = (wx - cam.x) * z + float(viewW) * 0.5f;
    sy = (wy - cam.y) * z + float(viewH) * 0.5f;
    sw = ww * z;
    sh = wh * z;
}

void sampleColor(const ParticleEmitter::Config &cfg, float t, Color &out) {
    t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
    out.r = cfg.colorStart.r + (cfg.colorEnd.r - cfg.colorStart.r) * t;
    out.g = cfg.colorStart.g + (cfg.colorEnd.g - cfg.colorStart.g) * t;
    out.b = cfg.colorStart.b + (cfg.colorEnd.b - cfg.colorStart.b) * t;
    out.a = cfg.colorStart.a + (cfg.colorEnd.a - cfg.colorStart.a) * t;
}

void drawOne(const ParticleEmitter::Config &cfg, const ParticleEmitter::Sim &sim,
             const ParticleEmitter::Draw &draw, graphics::Graphics *gfx, const ViewCam &cam) {
    if (!gfx || !draw.visible || sim.alive <= 0) return;

    const int viewW = draw.canvas ? draw.canvas->getWidth() : gfx->getWidth();
    const int viewH = draw.canvas ? draw.canvas->getHeight() : gfx->getHeight();

    for (int i = 0; i < sim.alive; ++i) {
        const Particle &p = sim.particles[size_t(i)];
        const float t = 1.f - (p.life / p.lifetime);
        Color c;
        sampleColor(cfg, t, c);
        const float scale =
            (cfg.sizeStart + (cfg.sizeEnd - cfg.sizeStart) * t) * (p.size > 0.f ? p.size : 1.f);
        const float w = cfg.particleW * scale;
        const float h = cfg.particleH * scale;
        const float hx = w * 0.5f;
        const float hy = h * 0.5f;
        float sx, sy, sw, sh;
        applyCamera(p.x - hx, p.y - hy, w, h, cam, viewW, viewH, sx, sy, sw, sh);
        if (draw.texture)
            gfx->drawTexturedRect(draw.texture, sx, sy, sw, sh, c);
        else
            gfx->drawSolidRect(sx, sy, sw, sh, c);
    }
}

int64_t fileModtime(const std::string &path) {
    auto *fs = eve::ModuleManager::getInstance<eve::filesystem::Filesystem>("Filesystem");
    if (!fs) fs = eve::filesystem::Filesystem::create();
    eve::filesystem::Filesystem::Info info{};
    if (!fs->getInfo(path, info)) return -1;
    return info.modtime;
}

}  // namespace

void ParticleSimSystem::update(float dt) {
    if (ecs::ComponentManager<ParticleEmitter>::inst().registy == nullptr) return;

    auto view = ecs::View<ParticleEmitter, ParticleEmitter::Config, ParticleEmitter::Sim>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [cfg, sim] = *it;
        stepEmitterSim(*cfg, *sim, dt);
    }
}

void ParticleRenderSystem::render(graphics::Graphics *gfx) {
    if (!gfx) {
#if defined(EVENGINE_ANDROID)
        __android_log_print(ANDROID_LOG_WARN, "EVEngine", "ParticleRender: gfx is null");
#endif
        return;
    }
    if (ecs::ComponentManager<ParticleEmitter>::inst().registy == nullptr) return;

    struct Item {
        ParticleEmitter::Config *cfg;
        ParticleEmitter::Sim *sim;
        ParticleEmitter::Draw *draw;
        ViewCam cam;
    };
    std::vector<Item> items;

    auto view = ecs::View<ParticleEmitter, ParticleEmitter::Config, ParticleEmitter::Sim,
                          ParticleEmitter::Draw>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [cfg, sim, draw] = *it;
        if (!draw->visible || sim->alive <= 0) continue;

        // Screen-space by default (matches Graphics::drawSolidRect). Call
        // setCamera() explicitly when world/camera space is needed.
        items.push_back(Item{cfg, sim, draw, fromEntity(draw->camera)});
    }

#if defined(EVENGINE_ANDROID)
    if (!items.empty()) {
        const auto &it0 = items.front();
        __android_log_print(ANDROID_LOG_INFO, "EVEngine",
                            "ParticleRender: emitters=%zu alive0=%d pos=(%.1f,%.1f) cam=%d",
                            items.size(), it0.sim->alive, it0.cfg->x, it0.cfg->y,
                            it0.cam.valid ? 1 : 0);
    }
#endif

    std::stable_sort(items.begin(), items.end(), [](const Item &a, const Item &b) {
        const bool aOff = a.draw->canvas != nullptr;
        const bool bOff = b.draw->canvas != nullptr;
        if (aOff != bOff) return aOff && !bOff;
        if (a.draw->canvas != b.draw->canvas) return a.draw->canvas < b.draw->canvas;
        return a.draw->layer < b.draw->layer;
    });

    // Draw into targets without clear/present — caller owns frame lifecycle.
    graphics::Canvas *current = reinterpret_cast<graphics::Canvas *>(static_cast<uintptr_t>(1));
    for (const Item &it : items) {
        graphics::Canvas *next = it.draw->canvas;
        if (next != current) {
            gfx->setCanvas(next);
            current = next;
        }
        drawOne(*it.cfg, *it.sim, *it.draw, gfx, it.cam);
    }
    if (current != nullptr) gfx->setCanvas();
}

int ParticleConfigSystem::poll() {
    if (ecs::ComponentManager<ParticleEmitter>::inst().registy == nullptr) return 0;

    auto *fs = eve::ModuleManager::getInstance<eve::filesystem::Filesystem>("Filesystem");
    if (!fs) fs = eve::filesystem::Filesystem::create();

    std::vector<std::string> dirtyPaths;
    for (;;) {
        std::string kind = fs->pollWatch();
        if (kind.empty()) break;
        if (kind != "modified" && kind != "added" && kind != "movedTo") continue;
        dirtyPaths.push_back(fs->getLastWatchPath());
    }

    int reloaded = 0;
    auto view =
        ecs::View<ParticleEmitter, ParticleEmitter::Config, ParticleEmitter::Resource>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [cfg, res] = *it;
        if (!res->autoReload || res->path.empty() || !cfg->entity) continue;

        bool dirty = false;
        for (const auto &p : dirtyPaths) {
            if (p == res->path) {
                dirty = true;
                break;
            }
        }
        if (!dirty) {
            const int64_t mt = fileModtime(res->path);
            if (mt >= 0 && mt != res->modtime) dirty = true;
        }
        if (!dirty) continue;
        if (reloadConfigFile(cfg->entity, nullptr)) ++reloaded;
    }
    return reloaded;
}

}  // namespace eve::particles
