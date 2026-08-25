#include "particles/ParticleSystem.h"
#include "particles/ParticleEmitter.h"
#include "particles/ParticleConfig.h"
#include "particles/ParticleRuntime.h"
#include "graphics/DrawItem2D.h"
#include "graphics/Graphics.h"
#include "graphics/Light.h"
#include "graphics/RenderSystem.h"
#include "graphics/Canvas.h"
#include "filesystem/Filesystem.h"
#include "common/Module.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
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
    } else if (cfg.renderMode == "axis") {
        item.rotation = cfg.renderAxisDegrees;
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

void appendEmitterItems(const ParticleEmitter::Config& cfg, const ParticleEmitter::Sim& sim,
                        const ParticleEmitter::Draw& draw, int& order, std::vector<graphics::DrawItem2D>& out) {
    if (cfg.sortMode == "none") {
        for (int i = 0; i < sim.alive; ++i) appendParticleItem(cfg, draw, sim.particles[std::size_t(i)], order++, out);
        return;
    }

    std::vector<int> indices(std::size_t(sim.alive));
    for (int i = 0; i < sim.alive; ++i) indices[std::size_t(i)] = i;
    auto age = [&](int index) {
        const Particle& p = sim.particles[std::size_t(index)];
        return p.lifetime > 0.f ? 1.f - p.life / p.lifetime : 1.f;
    };
    if (cfg.sortMode == "oldest") {
        std::stable_sort(indices.begin(), indices.end(), [&](int a, int b) { return age(a) > age(b); });
    } else if (cfg.sortMode == "youngest") {
        std::stable_sort(indices.begin(), indices.end(), [&](int a, int b) { return age(a) < age(b); });
    } else if (cfg.sortMode == "distance" && draw.camera) {
        const float cameraX         = draw.camera->data()->x;
        const float cameraY         = draw.camera->data()->y;
        auto        distanceSquared = [&](int index) {
            const Particle& p  = sim.particles[std::size_t(index)];
            const float     dx = p.x - cameraX;
            const float     dy = p.y - cameraY;
            return dx * dx + dy * dy;
        };
        std::stable_sort(indices.begin(), indices.end(),
                         [&](int a, int b) { return distanceSquared(a) > distanceSquared(b); });
    }
    for (int index : indices) appendParticleItem(cfg, draw, sim.particles[std::size_t(index)], order++, out);
}

int appendRibbonItems(const ParticleEmitter::Config& cfg, const ParticleEmitter::Sim& sim,
                      const ParticleEmitter::Draw& draw, int& order, std::vector<graphics::DrawItem2D>& out) {
    int segments = 0;
    for (int i = 1; i < sim.alive; ++i) {
        const Particle& previous = sim.particles[std::size_t(i - 1)];
        const Particle& current  = sim.particles[std::size_t(i)];
        const float     dx       = current.x - previous.x;
        const float     dy       = current.y - previous.y;
        const float     length   = std::sqrt(dx * dx + dy * dy);
        if (length < cfg.ribbonMinSegmentLength) continue;

        appendParticleItem(cfg, draw, current, order++, out);
        auto&       item = out.back();
        const float h    = item.h * cfg.ribbonWidth;
        item.x           = (previous.x + current.x - length) * 0.5f;
        item.y           = (previous.y + current.y - h) * 0.5f;
        item.w           = length;
        item.h           = h;
        item.rotation    = std::atan2(dy, dx) * kRad2Deg;
        ++segments;
    }
    return segments;
}

/** True when the emitter center is well outside the camera view (skip sim). */
bool emitterOffscreen(const ParticleEmitter::Config &cfg, const ParticleEmitter::Draw &draw) {
    auto *cam = draw.camera;
    if (!cam) return false;
    if (cfg.cullDistance > 0.f) {
        const float dx = cfg.x - cam->data()->x;
        const float dy = cfg.y - cam->data()->y;
        if (dx * dx + dy * dy > cfg.cullDistance * cfg.cullDistance) return true;
    }
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

int liveParticleCount(ParticleEmitter* emitter) {
    auto gpu = emitter->gpuSim();
    return gpu->residentActive ? gpu->estimatedAlive + emitter->sim()->alive : emitter->sim()->alive;
}

bool supportsResidentGpu(const ParticleEmitter::Config& cfg, const ParticleEmitter::Draw& draw) {
    // The first resident backend intentionally accepts only behavior that can
    // remain entirely on the GPU. Gameplay callbacks, custom curves and
    // per-particle lights keep using the deterministic CPU path.
    return draw.canvas == nullptr && draw.shader == nullptr && cfg.collisionMode == "none" &&
           !cfg.collisionBoundsEnabled && !cfg.worldCollision && cfg.forceFields.empty() && cfg.subEmitters.empty() &&
           !cfg.lights.enabled && cfg.velocityCurve.empty() && cfg.sizeCurve.empty() && cfg.rotationCurve.empty() &&
           cfg.colorGradient.empty() && cfg.sortMode == "none" && cfg.renderMode != "ribbon";
}

graphics::GpuParticleSpawn gpuSpawn(const Particle& particle) {
    graphics::GpuParticleSpawn out;
    out.x          = particle.x;
    out.y          = particle.y;
    out.vx         = particle.vx;
    out.vy         = particle.vy;
    out.life       = particle.life;
    out.lifetime   = particle.lifetime;
    out.size       = particle.size;
    out.rotation   = particle.rot;
    out.spin       = particle.spin;
    out.frame      = particle.frame;
    out.radial     = particle.radial;
    out.tangential = particle.tangential;
    out.ax         = particle.ax;
    out.ay         = particle.ay;
    out.noisePhase = particle.noisePhase;
    return out;
}

void deactivateResidentGpu(graphics::Graphics* gfx, ParticleEmitter::GpuSim& gpu) {
    if (gfx && gpu.residentHandle != graphics::kInvalidGpuParticleHandle)
        gfx->releaseGpuParticleEmitter(gpu.residentHandle);
    gpu.residentHandle = graphics::kInvalidGpuParticleHandle;
    gpu.residentActive = false;
    gpu.estimatedAlive = 0;
    gpu.timeline       = 0.0;
    gpu.deathTimes.clear();
}

bool advanceResidentGpu(graphics::Graphics* gfx, ParticleEmitter* emitter, float dt) {
    auto cfg = emitter->config();
    auto sim = emitter->sim();
    auto gpu = emitter->gpuSim();
    if (!gfx || !gfx->canSubmitGpuParticles()) return false;

    if (gpu->residentHandle == graphics::kInvalidGpuParticleHandle) {
        gpu->residentHandle = gfx->createGpuParticleEmitter(std::uint32_t(sim->particles.size()));
        if (gpu->residentHandle == graphics::kInvalidGpuParticleHandle) return false;
    }

    const bool  hadLastPosition  = sim->hasLastPos;
    const float previousX        = sim->lastX;
    const float previousY        = sim->lastY;
    const int   capacity         = int(sim->particles.size());
    const int   conservativeRoom = std::max(0, capacity - gpu->estimatedAlive - sim->alive);
    if (sim->spawnQuota < 0)
        sim->spawnQuota = conservativeRoom;
    else
        sim->spawnQuota = std::min(sim->spawnQuota, conservativeRoom);

    const float simulatedDt = advanceEmitterSim(*cfg, *sim, dt);
    gpu->timeline += simulatedDt;
    auto minHeap = std::greater<float>{};
    while (!gpu->deathTimes.empty() && gpu->deathTimes.front() <= float(gpu->timeline) + 1e-6f) {
        std::pop_heap(gpu->deathTimes.begin(), gpu->deathTimes.end(), minHeap);
        gpu->deathTimes.pop_back();
    }

    const int room     = std::max(0, capacity - int(gpu->deathTimes.size()));
    const int accepted = std::min(sim->alive, room);
    if (sim->alive > accepted) sim->droppedSpawnsThisFrame += sim->alive - accepted;
    std::vector<graphics::GpuParticleSpawn> spawns;
    spawns.reserve(std::size_t(accepted));
    for (int i = 0; i < accepted; ++i) {
        const Particle& particle = sim->particles[std::size_t(i)];
        if (particle.life <= 0.f) continue;
        spawns.push_back(gpuSpawn(particle));
        gpu->deathTimes.push_back(float(gpu->timeline) + particle.life);
        std::push_heap(gpu->deathTimes.begin(), gpu->deathTimes.end(), minHeap);
    }

    graphics::GpuParticleUpdate update;
    update.dt             = simulatedDt;
    update.emitterX       = cfg->x;
    update.emitterY       = cfg->y;
    update.gravityX       = cfg->gravityX;
    update.gravityY       = cfg->gravityY;
    update.damping        = cfg->damping;
    update.velocityLimit  = cfg->limitVelocity;
    update.noiseStrength  = cfg->noiseStrength;
    update.noiseFrequency = cfg->noiseFrequency;
    update.noiseSpeed     = cfg->noiseSpeed;
    update.time           = sim->emitterAge;
    update.frameRate      = cfg->frameRate;
    if (cfg->simSpace == "local" && hadLastPosition) {
        update.localOffsetX = cfg->x - previousX;
        update.localOffsetY = cfg->y - previousY;
    }

    if (!gfx->updateGpuParticleEmitter(gpu->residentHandle, update, spawns.data(), std::uint32_t(spawns.size()))) {
        // Before activation the CPU state remains authoritative and already
        // advanced. Once resident, a transient submit failure freezes state
        // rather than losing it through an implicit CPU migration.
        if (!gpu->residentActive) deactivateResidentGpu(gfx, *gpu);
        gpu->failed = true;
        return true;
    }

    for (int i = 0; i < sim->alive; ++i) sim->particles[std::size_t(i)].life = 0.f;
    sim->alive          = 0;
    gpu->residentActive = true;
    gpu->estimatedAlive = int(gpu->deathTimes.size());
    return true;
}

}  // namespace

void ParticleSimSystem::update(float dt) {
    auto &stats = mutableParticleFrameStats();
    const uint64_t nextFrame = stats.frameIndex + 1;
    stats = ParticleFrameStats{};
    stats.frameIndex = nextFrame;
    const auto begin = std::chrono::steady_clock::now();

    if (ecs::current()->getManager<ParticleEmitter>() == nullptr) return;

    std::vector<ParticleEmitter *> allEmitters;
    std::vector<ParticleEmitter *> emitters;
    {
        auto view = ecs::View<ParticleEmitter, ParticleEmitter::Config>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [cfg] = *it;
            if (!cfg->entity) continue;
            allEmitters.push_back(cfg->entity);
            auto sim = cfg->entity->sim();
            if (sim->active || liveParticleCount(cfg->entity) > 0) emitters.push_back(cfg->entity);
        }
    }
    std::stable_sort(emitters.begin(), emitters.end(), [](ParticleEmitter *a, ParticleEmitter *b) {
        return a->config()->priority > b->config()->priority;
    });

    const auto &budget = particleBudgetConfig();
    stats.emittersTotal = int(emitters.size());
    for (auto *emitter : allEmitters) {
        auto sim = emitter->sim();
        stats.particlesBefore += liveParticleCount(emitter);
        sim->spawnedThisFrame = 0;
        sim->droppedSpawnsThisFrame = 0;
        sim->spawnQuota = budget.maxParticles > 0 ? 0 : -1;
    }
    int reservedAlive = 0;
    for (auto* emitter : emitters) reservedAlive += liveParticleCount(emitter);

    auto* gfx = eve::ModuleManager::getInstance<eve::graphics::Graphics>("Graphics");

    int processedAlive = 0;
    for (auto *emitter : emitters) {
        auto cfg = emitter->config();
        auto sim = emitter->sim();
        auto draw = emitter->draw();
        const int aliveBefore = liveParticleCount(emitter);
        reservedAlive -= aliveBefore;

        if (budget.qualityLevel < cfg->minimumQuality) {
            ++stats.emittersQualitySkipped;
            processedAlive += aliveBefore;
            continue;
        }

        const bool offscreen = emitterOffscreen(*cfg, *draw);
        const bool culled    = cfg->cullingMode == "pause"    ? offscreen
                               : cfg->cullingMode == "always" ? false
                                                              : (aliveBefore <= 0 && offscreen);
        if (culled) {
            ++stats.emittersCulled;
            processedAlive += aliveBefore;
            continue;
        }

        if (budget.maxSimulatedEmitters > 0 &&
            stats.emittersSimulated >= budget.maxSimulatedEmitters) {
            ++stats.emittersBudgetSkipped;
            processedAlive += aliveBefore;
            continue;
        }

        int spawnQuota = cfg->maxSpawnPerFrame > 0 ? cfg->maxSpawnPerFrame : -1;
        if (budget.maxParticles > 0) {
            const int existingTotal = processedAlive + aliveBefore + reservedAlive;
            const int globalRoom = std::max(0, budget.maxParticles - existingTotal);
            spawnQuota = spawnQuota < 0 ? globalRoom : std::min(spawnQuota, globalRoom);
        }
        sim->spawnQuota = spawnQuota;

        auto attach = emitter->attach();
        auto skinSrc = emitter->skinSource();
        auto gpuSim  = emitter->gpuSim();
        syncEmitterSources(*cfg, *sim, *attach, *skinSrc);
        const bool gpuEligible = supportsResidentGpu(*cfg, *draw);
        if (gpuSim->residentActive && (!cfg->gpuSimulation || !gpuEligible)) deactivateResidentGpu(gfx, *gpuSim);
        const bool wantsResident = cfg->gpuSimulation && gpuEligible;
        const bool handled       = wantsResident && advanceResidentGpu(gfx, emitter, dt);
        if (!handled) advanceEmitterSim(*cfg, *sim, dt);
        ++stats.emittersSimulated;
        processedAlive += liveParticleCount(emitter);
        if (budget.maxParticles > 0 || cfg->maxSpawnPerFrame > 0) sim->spawnQuota = 0;
    }

    stats.particlesAfter = 0;
    stats.particlesSpawned = 0;
    stats.droppedSpawns = 0;
    for (auto *emitter : allEmitters) {
        auto sim = emitter->sim();
        auto gpu = emitter->gpuSim();
        stats.particlesAfter += liveParticleCount(emitter);
        stats.particlesSpawned += sim->spawnedThisFrame;
        stats.droppedSpawns += sim->droppedSpawnsThisFrame;
        if (gpu->residentActive) {
            ++stats.gpuResidentEmitters;
            stats.gpuResidentParticles += gpu->estimatedAlive;
        }
        sim->spawnQuota = -1;
    }
    stats.particlesKilled =
        std::max(0, stats.particlesBefore + stats.particlesSpawned - stats.particlesAfter);
    stats.simulationMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin)
            .count();
}

void ParticleRenderSystem::render(graphics::Graphics *gfx) {
    auto &stats = mutableParticleFrameStats();
    stats.renderedParticles = 0;
    stats.renderCulledEmitters = 0;
    stats.renderMs = 0.0;
    const auto begin = std::chrono::steady_clock::now();
    if (!gfx) return;
    if (ecs::current()->getManager<ParticleEmitter>() == nullptr) return;

    const auto &budget = particleBudgetConfig();
    std::vector<graphics::DrawItem2D> items;
    auto view = ecs::View<ParticleEmitter, ParticleEmitter::Config, ParticleEmitter::Sim,
                          ParticleEmitter::Draw>();
    bool anyCanvas = false;
    int order = 0;
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [cfg, sim, draw] = *it;
        auto* emitter         = cfg->entity;
        if (!emitter) continue;
        auto      gpu   = emitter->gpuSim();
        const int alive = liveParticleCount(emitter);
        if (!draw->visible || alive <= 0) continue;
        if (budget.qualityLevel < cfg->minimumQuality || emitterOffscreen(*cfg, *draw)) {
            ++stats.renderCulledEmitters;
            continue;
        }
        if (gpu->residentActive) {
            graphics::GpuParticleDraw gpuDraw;
            gpuDraw.texture        = draw->texture;
            gpuDraw.blend          = draw->blend;
            gpuDraw.viewportWidth  = float(gfx->getWidth());
            gpuDraw.viewportHeight = float(gfx->getHeight());
            if (draw->camera) {
                gpuDraw.cameraEnabled = true;
                gpuDraw.cameraX       = draw->camera->data()->x;
                gpuDraw.cameraY       = draw->camera->data()->y;
                gpuDraw.cameraZoom    = draw->camera->data()->zoom;
            }
            gpuDraw.particleWidth  = cfg->particleW;
            gpuDraw.particleHeight = cfg->particleH;
            gpuDraw.sizeStart      = cfg->sizeStart;
            gpuDraw.sizeEnd        = cfg->sizeEnd;
            gpuDraw.stretchFactor  = cfg->stretchFactor;
            gpuDraw.facing         = cfg->renderMode == "stretched" ? graphics::GpuParticleFacingMode::Velocity
                                     : cfg->renderMode == "axis"    ? graphics::GpuParticleFacingMode::Axis
                                                                    : graphics::GpuParticleFacingMode::ParticleRotation;
            gpuDraw.axisRotationRadians = cfg->renderAxisDegrees / kRad2Deg;
            gpuDraw.colorStart[0]  = cfg->colorStart.r;
            gpuDraw.colorStart[1]  = cfg->colorStart.g;
            gpuDraw.colorStart[2]  = cfg->colorStart.b;
            gpuDraw.colorStart[3]  = cfg->colorStart.a;
            gpuDraw.colorEnd[0]    = cfg->colorEnd.r;
            gpuDraw.colorEnd[1]    = cfg->colorEnd.g;
            gpuDraw.colorEnd[2]    = cfg->colorEnd.b;
            gpuDraw.colorEnd[3]    = cfg->colorEnd.a;
            gpuDraw.hframes        = cfg->hframes;
            gpuDraw.vframes        = cfg->vframes;
            if (gfx->drawGpuParticleEmitter(gpu->residentHandle, gpuDraw))
                stats.renderedParticles += gpu->estimatedAlive;
            else
                ++stats.renderCulledEmitters;
            continue;
        }
        if (draw->canvas) anyCanvas = true;
        if (cfg->renderMode == "ribbon")
            stats.renderedParticles += appendRibbonItems(*cfg, *sim, *draw, order, items);
        else {
            appendEmitterItems(*cfg, *sim, *draw, order, items);
            stats.renderedParticles += sim->alive;
        }
    }

    // Unified 2D sprite path: rotation / flipbook UV / blend / layer sorting
    // and camera handling all come from RenderSystem::drawItems.
    if (!items.empty()) graphics::RenderSystem::drawItems(*gfx, items, false);
    if (anyCanvas) gfx->setCanvas();
    stats.renderMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin)
            .count();
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
