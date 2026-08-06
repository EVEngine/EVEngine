#include "particles/ParticleSystem.h"
#include "particles/ParticleEmitter.h"
#include "particles/ParticleConfig.h"
#include "graphics/Graphics.h"
#include "filesystem/Filesystem.h"
#include "common/Module.h"

#include <algorithm>
#include <string>
#include <vector>

namespace eve::particles {

namespace {

void sampleColor(const ParticleEmitter::Config &cfg, float t, Color &out) {
    t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
    out.r = cfg.colorStart.r + (cfg.colorEnd.r - cfg.colorStart.r) * t;
    out.g = cfg.colorStart.g + (cfg.colorEnd.g - cfg.colorStart.g) * t;
    out.b = cfg.colorStart.b + (cfg.colorEnd.b - cfg.colorStart.b) * t;
    out.a = cfg.colorStart.a + (cfg.colorEnd.a - cfg.colorStart.a) * t;
}

void drawOne(const ParticleEmitter::Config &cfg, const ParticleEmitter::Sim &sim,
             const ParticleEmitter::Draw &draw, graphics::Graphics *gfx) {
    if (!gfx || !draw.visible || sim.alive <= 0) return;

    for (int i = 0; i < sim.alive; ++i) {
        const Particle &p = sim.particles[size_t(i)];
        const float t = 1.f - (p.life / p.lifetime);
        Color c;
        sampleColor(cfg, t, c);
        const float scale = cfg.sizeStart + (cfg.sizeEnd - cfg.sizeStart) * t;
        const float w = cfg.particleW * scale;
        const float h = cfg.particleH * scale;
        const float hx = w * 0.5f;
        const float hy = h * 0.5f;
        if (draw.texture)
            gfx->drawTexturedRect(draw.texture, p.x - hx, p.y - hy, w, h, c);
        else
            gfx->drawSolidRect(p.x - hx, p.y - hy, w, h, c);
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
    if (!gfx) return;
    if (ecs::ComponentManager<ParticleEmitter>::inst().registy == nullptr) return;

    struct Item {
        ParticleEmitter::Config *cfg;
        ParticleEmitter::Sim *sim;
        ParticleEmitter::Draw *draw;
    };
    std::vector<Item> items;

    auto view = ecs::View<ParticleEmitter, ParticleEmitter::Config, ParticleEmitter::Sim,
                          ParticleEmitter::Draw>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [cfg, sim, draw] = *it;
        if (!draw->visible || sim->alive <= 0) continue;
        items.push_back(Item{cfg, sim, draw});
    }

    std::stable_sort(items.begin(), items.end(), [](const Item &a, const Item &b) {
        return a.draw->layer < b.draw->layer;
    });

    for (const Item &it : items) drawOne(*it.cfg, *it.sim, *it.draw, gfx);
}

int ParticleConfigSystem::poll() {
    if (ecs::ComponentManager<ParticleEmitter>::inst().registy == nullptr) return 0;

    auto *fs = eve::ModuleManager::getInstance<eve::filesystem::Filesystem>("Filesystem");
    if (!fs) fs = eve::filesystem::Filesystem::create();

    // Drain filesystem watch events → mark matching emitters for reload.
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
            // Fallback: mtime poll when watch missed the change.
            const int64_t mt = fileModtime(res->path);
            if (mt >= 0 && mt != res->modtime) dirty = true;
        }
        if (!dirty) continue;
        if (reloadConfigFile(cfg->entity, nullptr)) ++reloaded;
    }
    return reloaded;
}

}  // namespace eve::particles
