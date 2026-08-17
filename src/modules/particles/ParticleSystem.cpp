#include "particles/ParticleSystem.h"
#include "particles/ParticleEmitter.h"
#include "particles/ParticleConfig.h"
#include "graphics/DrawItem2D.h"
#include "graphics/Graphics.h"
#include "graphics/Light.h"
#include "graphics/RenderSystem.h"
#include "graphics/Canvas.h"
#include "filesystem/Filesystem.h"
#include "common/Module.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::particles {

namespace {

constexpr float kRad2Deg = 180.f / 3.14159265358979323846f;

void sampleColor(const ParticleEmitter::Config &cfg, float t, Color &out) {
    if (!cfg.colorGradient.empty()) {
        float r, g, b, a;
        cfg.colorGradient.sample(t, r, g, b, a);
        out = Color(r, g, b, a);
        return;
    }
    t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
    out.r = cfg.colorStart.r + (cfg.colorEnd.r - cfg.colorStart.r) * t;
    out.g = cfg.colorStart.g + (cfg.colorEnd.g - cfg.colorStart.g) * t;
    out.b = cfg.colorStart.b + (cfg.colorEnd.b - cfg.colorStart.b) * t;
    out.a = cfg.colorStart.a + (cfg.colorEnd.a - cfg.colorStart.a) * t;
}

void sampleScale(const ParticleEmitter::Config &cfg, float t, float &scale) {
    if (!cfg.sizeCurve.empty()) {
        scale = cfg.sizeCurve.sample(t, 1.f);
        return;
    }
    t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
    scale = cfg.sizeStart + (cfg.sizeEnd - cfg.sizeStart) * t;
}

void flipbookUV(const ParticleEmitter::Config &cfg, float frame, float &u0, float &v0, float &u1,
                float &v1) {
    const int total = cfg.hframes * cfg.vframes;
    if (total <= 1 || cfg.hframes <= 0 || cfg.vframes <= 0) {
        u0 = 0.f;
        v0 = 0.f;
        u1 = 1.f;
        v1 = 1.f;
        return;
    }
    int fi = int(std::floor(frame));
    fi = ((fi % total) + total) % total;
    const int col = fi % cfg.hframes;
    const int row = fi / cfg.hframes;
    const float iw = 1.f / float(cfg.hframes);
    const float ih = 1.f / float(cfg.vframes);
    u0 = float(col) * iw;
    v0 = float(row) * ih;
    u1 = u0 + iw;
    v1 = v0 + ih;
}

void appendParticleItem(const ParticleEmitter::Config &cfg, const ParticleEmitter::Draw &draw,
                        const Particle &p, int order, std::vector<graphics::DrawItem2D> &out) {
    const float t = p.lifetime > 0.f ? 1.f - (p.life / p.lifetime) : 1.f;
    Color c;
    sampleColor(cfg, t, c);
    float scale;
    sampleScale(cfg, t, scale);
    scale *= p.size > 0.f ? p.size : 1.f;
    const float w = cfg.particleW * scale;
    const float h = cfg.particleH * scale;

    graphics::DrawItem2D item;
    item.x = p.x - w * 0.5f;
    item.y = p.y - h * 0.5f;
    item.w = w;
    item.h = h;
    if (cfg.renderMode == "stretched") {
        // Elongate along the velocity direction (comet / streak style).
        const float speed = std::sqrt(p.vx * p.vx + p.vy * p.vy);
        const float len = std::max(w, speed * cfg.stretchFactor);
        item.w = len;
        item.rotation = std::atan2(p.vy, p.vx) * kRad2Deg;
    } else {
        item.rotation = p.rot * kRad2Deg;
        if (!cfg.rotationCurve.empty()) item.rotation += cfg.rotationCurve.sample(t, 0.f);
    }
    item.order = order;
    item.hasOrder = true;
    item.color = c;
    item.layer = draw.layer;
    item.blend = draw.blend;
    item.texture = draw.texture;
    item.shader = draw.shader;
    item.canvas = draw.canvas;
    item.camera = draw.camera;
    item.receiveLight = false;
    if (draw.texture && (cfg.hframes > 1 || cfg.vframes > 1)) {
        flipbookUV(cfg, p.frame, item.u0, item.v0, item.u1, item.v1);
        item.hasUV = true;
    }
    out.push_back(item);
}

/** True when the emitter center is well outside the camera view (skip sim). */
bool emitterOffscreen(const ParticleEmitter::Config &cfg, const ParticleEmitter::Draw &draw) {
    auto *cam = draw.camera;
    if (!cam) return false;
    auto *gfx = eve::ModuleManager::getInstance<eve::graphics::Graphics>("Graphics");
    if (!gfx) return false;
    const float viewW = draw.canvas ? float(draw.canvas->getWidth()) : float(gfx->getWidth());
    const float viewH = draw.canvas ? float(draw.canvas->getHeight()) : float(gfx->getHeight());
    if (viewW <= 0.f || viewH <= 0.f) return false;
    const float z = cam->data()->zoom > 0.f ? cam->data()->zoom : 1e-4f;
    const float sx = (cfg.x - cam->data()->x) * z + viewW * 0.5f;
    const float sy = (cfg.y - cam->data()->y) * z + viewH * 0.5f;
    const float maxHalf =
        std::max(cfg.particleW, cfg.particleH) *
            std::max(std::abs(cfg.sizeStart), std::abs(cfg.sizeEnd)) * 0.5f +
        1.f;
    const float margin =
        cfg.speedMax * cfg.lifeMax + std::max(cfg.areaX, cfg.areaY) + maxHalf;
    return sx < -margin || sx > viewW + margin || sy < -margin || sy > viewH + margin;
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
    if (ecs::current()->getManager<ParticleEmitter>() == nullptr) return;

    auto view = ecs::View<ParticleEmitter, ParticleEmitter::Config, ParticleEmitter::Sim,
                          ParticleEmitter::Draw, ParticleEmitter::Attach,
                          ParticleEmitter::SkinSource, ParticleEmitter::GpuSim>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [cfg, sim, draw, attach, skinSrc, gpuSim] = *it;
        if (sim->alive <= 0 && emitterOffscreen(*cfg, *draw)) continue;
        syncEmitterSources(*cfg, *sim, *attach, *skinSrc);
        if (cfg->gpuSimulation) {
            if (!stepEmitterSimGpu(*cfg, *sim, *gpuSim, dt)) stepEmitterSim(*cfg, *sim, dt);
        } else {
            stepEmitterSim(*cfg, *sim, dt);
        }
    }
}

void ParticleRenderSystem::render(graphics::Graphics *gfx) {
    if (!gfx) return;
    if (ecs::current()->getManager<ParticleEmitter>() == nullptr) return;

    std::vector<graphics::DrawItem2D> items;
    auto view = ecs::View<ParticleEmitter, ParticleEmitter::Config, ParticleEmitter::Sim,
                          ParticleEmitter::Draw>();
    bool anyCanvas = false;
    int order = 0;
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [cfg, sim, draw] = *it;
        if (!draw->visible || sim->alive <= 0) continue;
        if (draw->canvas) anyCanvas = true;
        for (int i = 0; i < sim->alive; ++i) {
            appendParticleItem(*cfg, *draw, sim->particles[size_t(i)], order++, items);
        }
    }
    if (items.empty()) return;

    // Unified 2D sprite path: rotation / flipbook UV / blend / layer sorting
    // and camera handling all come from RenderSystem::drawItems.
    graphics::RenderSystem::drawItems(*gfx, items, false);
    if (anyCanvas) gfx->setCanvas();
}

void ParticleLightSystem::update() {
    if (ecs::current()->getManager<ParticleEmitter>() == nullptr) return;

    // Pass 1: collect emitters. Creating Light2D entities inside a deferred
    // View would stage them and invalidate stored raw pointers on publish.
    std::vector<ParticleEmitter *> emitters;
    {
        auto view = ecs::View<ParticleEmitter, ParticleEmitter::Config>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [cfg] = *it;
            if (cfg->entity) emitters.push_back(cfg->entity);
        }
    }

    // Pass 2: create/sync lights with no View active (stable entity pointers).
    for (auto *em : emitters) {
        auto cfg = em->config();
        auto sim = em->sim();
        auto draw = em->draw();
        auto lights = em->lights();
        if (!cfg->lights.enabled) {
            for (auto *l : lights->pool)
                if (l) l->setEnabled(false);
            continue;
        }
        const int maxL = cfg->lights.max > 0 ? (cfg->lights.max > 8 ? 8 : cfg->lights.max) : 0;
        while (int(lights->pool.size()) < maxL)
            lights->pool.push_back(graphics::Light2D::createLight());
        const int n = sim->alive < maxL ? sim->alive : maxL;
        for (int i = 0; i < maxL; ++i) {
            graphics::Light2D *l = lights->pool[size_t(i)];
            if (i < n) {
                const Particle &p = sim->particles[size_t(i)];
                l->setPosition(p.x, p.y);
                l->setRadius(cfg->lights.radius);
                l->setColor(cfg->lights.r, cfg->lights.g, cfg->lights.b, cfg->lights.intensity);
                l->setCanvas(draw->canvas);
                l->setEnabled(true);
            } else {
                l->setEnabled(false);
            }
        }
    }
}

int ParticleConfigSystem::poll() {
    if (ecs::current()->getManager<ParticleEmitter>() == nullptr) return 0;

    // Watch events are drained by load.nut / HotReload; use modtime as fallback.
    int reloaded = 0;
    auto view =
        ecs::View<ParticleEmitter, ParticleEmitter::Config, ParticleEmitter::Resource>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [cfg, res] = *it;
        if (!res->autoReload || res->path.empty() || !cfg->entity) continue;

        const int64_t mt = fileModtime(res->path);
        if (mt < 0 || mt == res->modtime) continue;
        if (reloadConfigFile(cfg->entity, nullptr)) ++reloaded;
    }
    return reloaded;
}

}  // namespace eve::particles
